/* Neo Geo band rendering (V1 of the Arcade 60 fps plan, `NEOBAND=1` builds).
 *
 * Included by vidhrdw/neogeo.c after the strip plotters, so it shares their
 * statics. The frame is drawn in 16-line bands into a buffer in internal RAM,
 * and each finished band is copied into the frame bitmap (V2 hands it to the
 * display instead). The image must be byte-identical to the full-frame path:
 * scripts/neogeo_frames.py compare proves it on the PC.
 *
 * One walk over the 381 sprite strips (neoband_palette) does what the
 * full-frame path did in two: it collects the pens in use for palette_recalc()
 * (as neogeo_palette() does) and records every tile strip that touches the
 * screen, in drawing order, in a list per band. The bands then draw from the
 * list: the backdrop clear, the strips of the band (same plotter, same
 * arguments, clipped to the band), the fix layer rows of the band.
 */
#if NEOBAND

#include <string.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#define NB_INTERNAL_ALLOC(n) heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#else
#define NB_INTERNAL_ALLOC(n) malloc(n)
#endif

#define NB_LINES     16                 /* lines per band */
#define NB_MAX_BANDS 16                 /* 224 visible lines / 16 = 14 */
#define NB_MAX_TILES 4096               /* tile strips per frame; beyond it the frame takes the full-frame path */
#define NB_MAX_NODES (2 * NB_MAX_TILES) /* a strip of up to 16 lines touches at most two bands */
#define NB_SAFETY    16                 /* as osd_alloc_bitmap: the plotters write up to 15 pixels past the edges */

struct nb_tile                          /* one NeoMVSDrawGfx call, minus what it recomputes */
{
	uint32_t tileno;
	int16_t sx, sy;
	uint8_t color, flip;            /* flip: bit 0 x, bit 1 y */
	uint8_t rzx, yskip;             /* x and y pixels drawn (16 = no zoom) */
	uint8_t rzy;                    /* 255 = no y zoom; else dday restarts the y dda */
	int16_t dday;
	uint16_t xmask;                 /* dda_x_skip as bits, when rzx != 16 */
};
struct nb_node { uint16_t tile, next; };

static struct nb_tile *nb_tiles;
static struct nb_node *nb_nodes;
static uint16_t nb_head[NB_MAX_BANDS], nb_tail[NB_MAX_BANDS];
static int nb_ntiles, nb_nnodes, nb_overflow;
static unsigned char *nb_buf;           /* NB_LINES rows of nb_stride bytes, internal RAM */
static unsigned char **nb_lines;        /* the band bitmap's line table, indexed by screen y */
static int nb_stride, nb_lines_n;
static struct osd_bitmap nb_bitmap;
static int nb_ready;                    /* 1 once the buffers exist, -1 if they could not be made */

static int nb_init(const struct osd_bitmap *bitmap)
{
	if (nb_ready)
		return nb_ready > 0;
	nb_stride = ((bitmap->width + 7) & ~7) + 2 * NB_SAFETY;
	nb_lines_n = bitmap->height;
	nb_buf = NB_INTERNAL_ALLOC(NB_LINES * nb_stride);
	nb_lines = calloc(nb_lines_n, sizeof *nb_lines);
	nb_tiles = malloc(NB_MAX_TILES * sizeof *nb_tiles);
	nb_nodes = malloc(NB_MAX_NODES * sizeof *nb_nodes);
	if (!nb_buf || !nb_lines || !nb_tiles || !nb_nodes)
	{
		printf("neoband: no memory (band %d bytes, list %d bytes)\n", NB_LINES * nb_stride,
			(int)(NB_MAX_TILES * sizeof *nb_tiles + NB_MAX_NODES * sizeof *nb_nodes));
		nb_ready = -1;
		return 0;
	}
	memset(nb_buf, 0, NB_LINES * nb_stride);
	nb_bitmap.width = bitmap->width;
	nb_bitmap.height = bitmap->height;
	nb_bitmap.depth = 8;
	nb_bitmap._private = NULL;
	nb_bitmap.line = nb_lines;
	printf("neoband: %d-line bands, %d bytes in internal RAM, %d tile strips a frame\n",
		NB_LINES, NB_LINES * nb_stride, NB_MAX_TILES);
	nb_ready = 1;
	return 1;
}

static void nb_link(int band, int tile)
{
	struct nb_node *n;
	if (nb_nnodes >= NB_MAX_NODES) { nb_overflow = 1; return; }
	n = &nb_nodes[nb_nnodes];
	n->tile = tile;
	n->next = 0xffff;
	if (nb_head[band] == 0xffff)
		nb_head[band] = nb_nnodes;
	else
		nb_nodes[nb_tail[band]].next = nb_nnodes;
	nb_tail[band] = nb_nnodes++;
}

/* The one walk: the pens in use (neogeo_palette's tile part, same tests) and
   the tile list. Returns 1 when the list holds the whole frame. */
MAMEGO_HOT static int neoband_walk(const struct rectangle *clip, const unsigned char *vidram,
		unsigned int neogeo_frame_counter, int colmask[256])
{
	unsigned int *pen_usage = Machine->gfx[2]->pen_usage;
	int sx = 0, sy = 0, oy = 0, my = 0, zx = 1, rzy = 1;
	int offs, i, count, y;
	int tileno, tileatr, t1, t2, t3;
	char fullmode = 0;
	int ddax = 0, dday = 0, rzx = 15, yskip = 0;
	unsigned xmask = 0xffff;

	nb_ntiles = nb_nnodes = nb_overflow = 0;
	memset(nb_head, 0xff, sizeof nb_head);

	for (count = 0; count < 0x300; count += 2)
	{
		t3 = READ_WORD(&vidram[0x10000 + count]);
		t1 = READ_WORD(&vidram[0x10400 + count]);
		t2 = READ_WORD(&vidram[0x10800 + count]);

		if (t1 & 0x40)                   /* this column is placed next to the last one */
		{
			sx += rzx;
			if (sx >= 0x1F0)
				sx -= 0x200;
			zx = (t3 >> 8) & 0x0f;
			sy = oy;
		}
		else                             /* a new block */
		{
			zx = (t3 >> 8) & 0x0f;
			rzy = t3 & 0xff;
			sx = (t2 >> 7);
			if (sx >= 0x1F0)
				sx -= 0x200;
			my = t1 & 0x3f;
			if (my == 0x20) fullmode = 1;
			else if (my >= 0x21) fullmode = 2;
			else fullmode = 0;
			sy = 0x200 - (t1 >> 7);
			if (clip->max_y - clip->min_y > 8 || clip->min_y == Machine->visible_area.min_y)
			{
				if (sy > 0x110) sy -= 0x200;
				if (fullmode == 2 || (fullmode == 1 && rzy == 0xff))
				{
					while (sy < 0) sy += 2 * (rzy + 1);
				}
			}
			oy = sy;
			if (rzy < 0xff && my < 0x10 && my)
			{
				my = (my * 256) / (rzy + 1);
				if (my > 0x10) my = 0x10;
			}
			if (my > 0x20) my = 0x20;
			ddax = 0;
		}

		if (my == 0) continue;

		if (zx != 15)                    /* x zoom: the column pattern, carried across chained columns */
		{
			rzx = 0;
			xmask = 0;
			for (i = 0; i < 16; i++)
			{
				ddax -= zx + 1;
				if (ddax <= 0)
				{
					ddax += 15 + 1;
					xmask |= 1u << i;
					rzx++;
				}
			}
		}
		else
		{
			rzx = 16;
			xmask = 0xffff;
		}

		if (sx >= 320) continue;

		if (rzy == 255)
			yskip = 16;
		else
			dday = 0;

		offs = count << 6;

		for (y = 0; y < my; y++)
		{
			int dday0 = dday;
			tileno = READ_WORD(&vidram[offs]);
			offs += 2;
			tileatr = READ_WORD(&vidram[offs]);
			offs += 2;

			if (high_tile && tileatr & 0x10) tileno += 0x10000;
			if (vhigh_tile && tileatr & 0x20) tileno += 0x20000;
			if (vvhigh_tile && tileatr & 0x40) tileno += 0x40000;

			if (tileatr & 0x8) tileno = (tileno & ~7) + ((tileno + neogeo_frame_counter) & 7);
			else if (tileatr & 0x4) tileno = (tileno & ~3) + ((tileno + neogeo_frame_counter) & 3);

			if (fullmode == 2 || (fullmode == 1 && rzy == 0xff))
			{
				if (sy >= 248) sy -= 2 * (rzy + 1);
			}
			else if (fullmode == 1)
			{
				if (y == 0x10) sy -= 2 * (rzy + 1);
			}
			else if (sy > 0x110) sy -= 0x200;

			if (rzy != 255)
			{
				yskip = 0;
				for (i = 0; i < 16; i++)
				{
					dday -= rzy + 1;
					if (dday <= 0)
					{
						dday += 256;
						yskip++;
					}
				}
			}

			if (sy + yskip - 1 >= clip->min_y && sy <= clip->max_y)
			{
				tileno %= no_of_tiles;
				if (pen_usage[tileno] == 0)
					decodetile(tileno);
				colmask[tileatr >> 8] |= pen_usage[tileno];

				/* the plotter would draw it (sx > -16, not fully transparent): record it */
				if (sx > -16 && (pen_usage[tileno] & ~1) != 0 && !nb_overflow)
				{
					if (nb_ntiles >= NB_MAX_TILES)
						nb_overflow = 1;
					else
					{
						struct nb_tile *t = &nb_tiles[nb_ntiles];
						int y0 = sy < clip->min_y ? clip->min_y : sy;
						int y1 = sy + yskip - 1 > clip->max_y ? clip->max_y : sy + yskip - 1;
						int b0 = (y0 - clip->min_y) / NB_LINES, b1 = (y1 - clip->min_y) / NB_LINES, b;
						t->tileno = tileno;
						t->sx = sx;
						t->sy = sy;
						t->color = tileatr >> 8;
						t->flip = tileatr & 0x03;
						t->rzx = rzx;
						t->yskip = yskip;
						t->rzy = rzy;
						t->dday = dday0;
						t->xmask = xmask;
						for (b = b0; b <= b1; b++)
							nb_link(b, nb_ntiles);
						nb_ntiles++;
					}
				}
			}
			sy += yskip;
		}
	}
	return !nb_overflow;
}

/* neogeo_palette() for the band path: fix-layer pens, the walk, palette_recalc().
   Returns 1 when the bands can draw this frame from the list. */
static int neoband_palette(const struct rectangle *clip, const unsigned char *vidram,
		unsigned int neogeo_frame_counter, int fix_bank)
{
	int colmask[256];
	unsigned int *pen_usage;
	int pal_base, color, code, offs, i, ok;

	palette_init_used_colors();

	pen_usage = Machine->gfx[fix_bank]->pen_usage;
	pal_base = Machine->drv->gfxdecodeinfo[fix_bank].color_codes_start;
	for (color = 0; color < 16; color++) colmask[color] = 0;
	for (offs = 0xe000; offs < 0xea00; offs += 2)
	{
		code = READ_WORD(&vidram[offs]);
		color = code >> 12;
		colmask[color] |= pen_usage[code & 0xfff];
	}
	for (color = 0; color < 16; color++)
		for (i = 1; i < 16; i++)
			if (colmask[color] & (1 << i))
				palette_used_colors[pal_base + 16 * color + i] = PALETTE_COLOR_VISIBLE;

	pal_base = Machine->drv->gfxdecodeinfo[2].color_codes_start;
	for (color = 0; color < 256; color++) colmask[color] = 0;
	ok = neoband_walk(clip, vidram, neogeo_frame_counter, colmask);
	for (color = 0; color < 256; color++)
		for (i = 1; i < 16; i++)
			if (colmask[color] & (1 << i))
				palette_used_colors[pal_base + 16 * color + i] = PALETTE_COLOR_VISIBLE;

	palette_used_colors[4095] = PALETTE_COLOR_VISIBLE;
	palette_recalc();
	return ok;
}

/* Draw the frame from the list, band by band, into the frame bitmap. */
MAMEGO_HOT static void neoband_draw(struct osd_bitmap *bitmap, const struct rectangle *clip, int fix_bank)
{
	const struct GfxElement *gfx = Machine->gfx[2];
	struct GfxElement *fixgfx = Machine->gfx[fix_bank];
	const unsigned int *fix_usage = fixgfx->pen_usage;
	int nbands = (clip->max_y - clip->min_y) / NB_LINES + 1;
	int b, y, x, i, width = clip->max_x - clip->min_x + 1;

	for (b = 0; b < nbands; b++)
	{
		struct rectangle band;
		int node;
		band.min_x = clip->min_x;
		band.max_x = clip->max_x;
		band.min_y = clip->min_y + b * NB_LINES;
		band.max_y = band.min_y + NB_LINES - 1;
		if (band.max_y > clip->max_y)
			band.max_y = clip->max_y;
		/* every row, not only the band's: drawgfx takes the row pitch from
		   line[1] - line[0], and the clip keeps it inside the band */
		for (y = 0; y < nb_bitmap.height; y++)
			nb_lines[y] = nb_buf + (y - band.min_y) * nb_stride + NB_SAFETY;

		VPROF_PUSH(PROF_VCLEAR);
		fillbitmap(&nb_bitmap, Machine->pens[4095], &band);
		VPROF_POP();

		VPROF_PUSH(PROF_VSPR);
		neoband_exact_top = band.min_y > clip->min_y;
		for (node = nb_head[b]; node != 0xffff; node = nb_nodes[node].next)
		{
			const struct nb_tile *t = &nb_tiles[nb_nodes[node].tile];
			if (t->rzx != 16)
				for (i = 0; i < 16; i++)
					dda_x_skip[i] = (t->xmask >> i) & 1;
			if (t->rzy != 255)       /* the y dda of this tile, as the full-frame walk ran it */
			{
				int dday = t->dday, yskip = 0;
				dda_y_skip[0] = 0;
				for (i = 0; i < 16; i++)
				{
					dda_y_skip[i + 1] = 0;
					dday -= t->rzy + 1;
					if (dday <= 0)
					{
						dday += 256;
						yskip++;
						dda_y_skip[yskip]++;
					}
					else dda_y_skip[yskip]++;
				}
			}
			NeoMVSDrawGfx(nb_lines, gfx, t->tileno, t->color, t->flip & 1, t->flip & 2,
				t->sx, t->sy, t->rzx, t->yskip, &band);
		}
		neoband_exact_top = 0;
		VPROF_POP();

		VPROF_PUSH(PROF_VFIX);
		for (y = band.min_y / 8; y <= band.max_y / 8; y++)
			for (x = 0; x < 40; x++)
			{
				int byte1 = READ_WORD(&vidram[0xE000 + 2 * (y + 32 * x)]);
				int byte2 = byte1 >> 12;
				byte1 &= 0xfff;
				if ((fix_usage[byte1] & ~1) == 0) continue;
				PROF_BYTES(PROF_VFIX, 64, 64, 1);
				drawgfx(&nb_bitmap, fixgfx, byte1, byte2, 0, 0, x * 8, y * 8, &band, TRANSPARENCY_PEN, 0);
			}
		VPROF_POP();

		VPROF_PUSH(PROF_VCOPY);
		for (y = band.min_y; y <= band.max_y; y++)
			memcpy(bitmap->line[y] + clip->min_x, nb_lines[y] + clip->min_x, width);
		PROF_BYTES(PROF_VCOPY, 0, width * (band.max_y - band.min_y + 1), 1);
		VPROF_POP();

	}
}

#endif /* NEOBAND */
