/* Neo Geo sprite pager for mame-go (see README_MAMEGO.md).
 *
 * Neo Geo sprite ROMs (the C ROMs, 4-64 MB) do not fit the 8 MB of PSRAM
 * next to the program and the rest of the board, and the driver decodes
 * every tile in place the first time it draws it. Instead, the first launch
 * converts the sprite regions once into an SD file of already decoded tiles
 * plus their pen_usage table:
 *
 *     <core_sys_directory>/neospr/<game>_gfx<n>.spr
 *     header | pen_usage[tiles] (u32) | tiles[tiles] (128 bytes, decoded)
 *
 * and the renderer reads tiles through a PSRAM cache of 8 KB pages (64
 * tiles), least recently used first; pages used in the current frame are
 * not evicted. Transparent tiles are skipped from pen_usage alone, without
 * touching the card. The region itself stays unallocated (a 16-byte stub
 * with the full length, so the driver's tile count is unchanged).
 */
#include "driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#define NEOSPR_TILE        128
#define NEOSPR_PAGE_TILES  64
#define NEOSPR_PAGE        (NEOSPR_TILE * NEOSPR_PAGE_TILES)
#define NEOSPR_MAX_REGIONS 2   /* REGION_GFX2 and REGION_GFX3 */
#define NEOSPR_VERSION     1

struct neospr_header
{
	char magic[8];          /* "NEOSPR\0\0" */
	uint32_t version;
	uint32_t region_size;
	uint32_t tiles;
	uint32_t rom_key;       /* sum of the source ROMs' CRCs and lengths */
	uint32_t data_offset;   /* file offset of tile 0 */
	uint32_t reserved[9];
};

struct neospr_region
{
	FILE *f;
	unsigned char *stub;
	uint32_t tiles;
	uint32_t data_offset;
	uint32_t first_page;    /* global page number of this region's tile 0 */
};

static struct neospr_region regions[NEOSPR_MAX_REGIONS];
static int region_count;

static unsigned char *cache;
static int cache_slots;
static int32_t *page_slot;  /* global page -> slot, -1 when not cached */
static int32_t *slot_page;
static uint32_t *slot_used; /* frame number of the last use */
static uint32_t total_pages, frame, next_victim;
static unsigned misses_frame, misses_total, frames_total, worst_frame;

extern char core_sys_directory[];
extern int neogeo_mvs_vh_start(void);
extern void init_mgd2(void);

/* Pageable only for the Neo Geo video hardware, and not for the mgd2 sets
   whose driver_init reshuffles the whole sprite region in memory. */
int neospr_wanted(int type)
{
	if (type != REGION_GFX2 && type != REGION_GFX3)
		return 0;
#ifndef ESP_PLATFORM
	if (getenv("NEOSPR") && !strcmp(getenv("NEOSPR"), "0")) /* PC A/B runs */
		return 0;
#endif
	if (!Machine->drv || Machine->drv->vh_start != neogeo_mvs_vh_start)
		return 0;
	if (Machine->gamedrv->driver_init == init_mgd2)
		return 0;
	return region_count < NEOSPR_MAX_REGIONS;
}

int neospr_active(void)
{
	return region_count > 0;
}

int neospr_owns(const unsigned char *region)
{
	int i;
	for (i = 0; i < region_count; i++)
		if (regions[i].stub == region)
			return 1;
	return 0;
}

static void decode_tile(const unsigned char *raw, uint32_t *out, uint32_t *pens)
{
	/* same bit order as decodetile() in vidhrdw/neogeo.c */
	int x, y;
	uint32_t usage = 0;

	for (y = 0; y < 16; y++)
	{
		uint32_t dw = 0;
		for (x = 0; x < 8; x++)
		{
			unsigned pen;
			pen  = ((raw[64 + 4*y + 3] >> x) & 1) << 3;
			pen |= ((raw[64 + 4*y + 1] >> x) & 1) << 2;
			pen |= ((raw[64 + 4*y + 2] >> x) & 1) << 1;
			pen |=  (raw[64 + 4*y    ] >> x) & 1;
			dw |= pen << 4*(7-x);
			usage |= 1u << pen;
		}
		*out++ = dw;

		dw = 0;
		for (x = 0; x < 8; x++)
		{
			unsigned pen;
			pen  = ((raw[4*y + 3] >> x) & 1) << 3;
			pen |= ((raw[4*y + 1] >> x) & 1) << 2;
			pen |= ((raw[4*y + 2] >> x) & 1) << 1;
			pen |=  (raw[4*y    ] >> x) & 1;
			dw |= pen << 4*(7-x);
			usage |= 1u << pen;
		}
		*out++ = dw;
	}
	*pens = usage;
}

static void *open_rom(const struct RomModule *romp)
{
	const struct GameDriver *drv = Machine->gamedrv;
	void *f = NULL;
	char crc[9];

	do { f = osd_fopen(drv->name, romp->name, OSD_FILETYPE_ROM, 0); drv = drv->clone_of; } while (!f && drv);
	if (f)
		return f;
	sprintf(crc, "%08x", (unsigned int)romp->crc);
	drv = Machine->gamedrv;
	do { f = osd_fopen(drv->name, crc, OSD_FILETYPE_ROM, 0); drv = drv->clone_of; } while (!f && drv);
	return f;
}

/* Convert the region's ROMs (ROM_LOAD_GFX_EVEN/ODD pairs) into the .spr
   file. A pair is streamed 64 KB at a time: besides the two zip entries
   the file layer keeps in memory, only a 128 KB interleave buffer is used.
   The byte placement is exactly readroms()'s for ROM_LOAD_EVEN/ODD. */
#define NEOSPR_CHUNK (64 * 1024)
static int build_file(FILE *out, const struct RomModule *first, int entries, uint32_t tiles, uint32_t data_offset)
{
	uint32_t *pens = calloc(tiles, sizeof(uint32_t));
	unsigned char *chunk = malloc(2 * NEOSPR_CHUNK), *part = malloc(NEOSPR_CHUNK);
	int done[64] = {0};
	int i, j, ret = -1;

	if (!pens || !chunk || !part || entries > 64)
		goto out;

	for (i = 0; i < entries; i++)
	{
		const struct RomModule *group[8];
		void *files[8];
		unsigned base, length, pos, t;
		int n = 0, k;

		if (done[i])
			continue;
		base = first[i].offset & ~1;
		length = first[i].length & ~ROMFLAG_MASK;
		for (j = i; j < entries && n < 8; j++)
		{
			if (done[j] || (first[j].offset & ~1) != base || (first[j].length & ~ROMFLAG_MASK) != length)
				continue;
			if (!(files[n] = open_rom(&first[j])))
			{
				printf("neospr: cannot open %s\n", first[j].name);
				while (n--) osd_fclose(files[n]);
				goto out;
			}
			printf("neospr: converting %s\n", first[j].name);
			group[n++] = &first[j];
			done[j] = 1;
		}

		for (pos = 0; pos < length; pos += NEOSPR_CHUNK)
		{
			unsigned len = length - pos < NEOSPR_CHUNK ? length - pos : NEOSPR_CHUNK;

			memset(chunk, 0, 2 * len);
			for (k = 0; k < n; k++)
			{
			#ifdef MSB_FIRST
				unsigned lane = group[k]->offset - base;
			#else
				unsigned lane = (group[k]->offset - base) ^ 1;
			#endif
				unsigned b;
				if (osd_fread(files[k], part, len) != (int)len)
					memset(part, 0, len);
				for (b = 0; b < len; b++)
					chunk[lane + 2 * b] = part[b];
			}
			for (t = 0; t < 2 * len / NEOSPR_TILE; t++)
			{
				uint32_t tile = (base + 2 * pos) / NEOSPR_TILE + t;
				uint32_t decoded[32];
				decode_tile(chunk + t * NEOSPR_TILE, decoded, &pens[tile]);
				memcpy(chunk + t * NEOSPR_TILE, decoded, NEOSPR_TILE);
			}
			fseek(out, data_offset + base + 2 * pos, SEEK_SET);
			if (fwrite(chunk, 1, 2 * len, out) != 2 * len)
			{
				printf("neospr: write failed (card full?)\n");
				for (k = 0; k < n; k++) osd_fclose(files[k]);
				goto out;
			}
		}
		for (k = 0; k < n; k++)
			osd_fclose(files[k]);
	}

	fseek(out, sizeof(struct neospr_header), SEEK_SET);
	if (fwrite(pens, sizeof(uint32_t), tiles, out) == tiles)
		ret = 0;
out:
	free(pens); free(chunk); free(part);
	return ret;
}

/* Called by readroms() instead of allocating a Neo Geo sprite region.
   `first` points at the region's first ROM entry, `entries` ROM_LOADs follow.
   Returns the stub to store as the region pointer, or NULL to load it the
   normal way (unsupported layout, SD error). */
unsigned char *neospr_region_load(int type, const struct RomModule *first, int entries, unsigned region_size)
{
	struct neospr_region *reg = &regions[region_count];
	struct neospr_header h, want;
	char path[1024];
	uint32_t key = region_size;
	FILE *f;
	int i;

	for (i = 0; i < entries; i++)
	{
		const struct RomModule *r = &first[i];
		/* only plain even/odd byte pairs: no CONTINUE/RELOAD/nibble/quad/wide */
		if (!r->name || r->name == (char *)-1 || !(r->length & ROMFLAG_ALTERNATE)
			|| (r->length & (ROMFLAG_NIBBLE | ROMFLAG_QUAD | ROMFLAG_WIDE)))
		{
			printf("neospr: region %d has an unsupported ROM layout, loading it into RAM\n", type);
			return NULL;
		}
		key += (uint32_t)r->crc + (r->length & ~ROMFLAG_MASK) + r->offset;
	}

	memset(&want, 0, sizeof(want));
	memcpy(want.magic, "NEOSPR", 6);
	want.version = NEOSPR_VERSION;
	want.region_size = region_size;
	want.tiles = region_size / NEOSPR_TILE;
	want.rom_key = key;
	want.data_offset = (sizeof(want) + want.tiles * sizeof(uint32_t) + 4095) & ~4095u;

	snprintf(path, sizeof(path), "%s/neospr", core_sys_directory);
	mkdir(path, 0777);
	snprintf(path, sizeof(path), "%s/neospr/%s_gfx%d.spr", core_sys_directory, Machine->gamedrv->name,
		type - REGION_GFX1 + 1);

	f = fopen(path, "rb");
	if (!f || fread(&h, sizeof(h), 1, f) != 1 || memcmp(&h, &want, sizeof(h)) != 0)
	{
		if (f)
			fclose(f);
		printf("neospr: preparing %s (one time, %u KB)\n", path, region_size / 1024);
		if (!(f = fopen(path, "w+b")) || fwrite(&want, sizeof(want), 1, f) != 1
			|| build_file(f, first, entries, want.tiles, want.data_offset) != 0)
		{
			if (f)
				fclose(f);
			remove(path);
			return NULL;
		}
		/* close it now: FAT only records the size on close, and a crash
		   later in this run would otherwise make the next launch rebuild */
		fclose(f);
		if (!(f = fopen(path, "rb")))
			return NULL;
	}

	/* Nothing else stays allocated until neospr_start(): the program and
	   sample ROMs load next and need large contiguous PSRAM blocks (a
	   256 KB pen_usage table here made the 3 MB sample region fail). */
	reg->f = f;
	reg->tiles = want.tiles;
	reg->data_offset = want.data_offset;
	reg->first_page = total_pages;
	reg->stub = malloc(16);
	total_pages += (want.tiles + NEOSPR_PAGE_TILES - 1) / NEOSPR_PAGE_TILES;
	region_count++;
	printf("neospr: %s, %u tiles paged from the card\n", path, want.tiles);
	return reg->stub;
}

/* Fill the driver's pen_usage (tiles of all paged regions, in order) and
   size the page cache from what is left in PSRAM. */
int neospr_start(uint32_t *pen_usage, unsigned total_tiles)
{
	size_t bytes;
	uint32_t i, n = 0;
	int r;

	for (r = 0; r < region_count; r++)
	{
		if (n + regions[r].tiles > total_tiles)
			return -1;
		fseek(regions[r].f, sizeof(struct neospr_header), SEEK_SET);
		if (fread(pen_usage + n, sizeof(uint32_t), regions[r].tiles, regions[r].f) != regions[r].tiles)
			return -1;
		n += regions[r].tiles;
	}

#ifdef ESP_PLATFORM
	{
		size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
		bytes = largest > 1536 * 1024 ? largest - 1024 * 1024 : 512 * 1024;
	}
#else
	bytes = getenv("NEOSPR_CACHE_KB") ? (size_t)atoi(getenv("NEOSPR_CACHE_KB")) * 1024 : 2048 * 1024;
#endif
#ifdef ESP_PLATFORM
	if (bytes > 3 * 1024 * 1024)
		bytes = 3 * 1024 * 1024;
#endif
	cache_slots = bytes / NEOSPR_PAGE;
	cache = malloc((size_t)cache_slots * NEOSPR_PAGE);
	page_slot = malloc(total_pages * sizeof(int32_t));
	slot_page = malloc(cache_slots * sizeof(int32_t));
	slot_used = calloc(cache_slots, sizeof(uint32_t));
	if (!cache || !page_slot || !slot_page || !slot_used)
		return -1;
	for (i = 0; i < total_pages; i++)
		page_slot[i] = -1;
	for (i = 0; i < (uint32_t)cache_slots; i++)
		slot_page[i] = -1;
	frame = 1;
	printf("neospr: page cache %d KB (%d pages of %d tiles)\n", cache_slots * NEOSPR_PAGE / 1024, cache_slots, NEOSPR_PAGE_TILES);
	return 0;
}

static int pick_victim(void)
{
	/* least recently used slot, skipping the ones this frame already uses */
	int best = -1, i;
	uint32_t oldest = ~0u;

	for (i = 0; i < cache_slots; i++)
	{
		int s = (next_victim + i) % cache_slots;
		if (slot_page[s] < 0)
			return s;
		if (slot_used[s] != frame && slot_used[s] < oldest)
		{
			oldest = slot_used[s];
			best = s;
		}
	}
	if (best < 0) /* the whole cache is in this frame: take the next one */
		best = next_victim;
	next_victim = (best + 1) % cache_slots;
	return best;
}

uint32_t *neospr_tile(int tileno)
{
	struct neospr_region *reg = &regions[0];
	uint32_t page, in_region = tileno;
	int slot;

	if (region_count > 1 && in_region >= regions[0].tiles)
	{
		in_region -= regions[0].tiles;
		reg = &regions[1];
	}
	page = reg->first_page + in_region / NEOSPR_PAGE_TILES;
	slot = page_slot[page];
	if (slot < 0)
	{
		unsigned char *dst;
		uint32_t first_tile = in_region & ~(NEOSPR_PAGE_TILES - 1);
		uint32_t count = reg->tiles - first_tile < NEOSPR_PAGE_TILES ? reg->tiles - first_tile : NEOSPR_PAGE_TILES;

		slot = pick_victim();
		if (slot_page[slot] >= 0)
			page_slot[slot_page[slot]] = -1;
		dst = cache + (size_t)slot * NEOSPR_PAGE;
		fseek(reg->f, reg->data_offset + first_tile * NEOSPR_TILE, SEEK_SET);
		if (fread(dst, NEOSPR_TILE, count, reg->f) != count)
			memset(dst, 0, NEOSPR_PAGE);
		slot_page[slot] = page;
		page_slot[page] = slot;
		misses_frame++;
	}
	slot_used[slot] = frame;
	return (uint32_t *)(cache + (size_t)slot * NEOSPR_PAGE + (in_region % NEOSPR_PAGE_TILES) * NEOSPR_TILE);
}

void neospr_frame(void)
{
	misses_total += misses_frame;
	if (misses_frame > worst_frame)
		worst_frame = misses_frame;
	misses_frame = 0;
	frame++;
	if (++frames_total % 600 == 0)
	{
		printf("neospr: %u page reads in the last 600 frames, worst frame %u\n", misses_total, worst_frame);
		misses_total = worst_frame = 0;
	}
}

void neospr_stop(void)
{
	int r;
	for (r = 0; r < region_count; r++)
	{
		if (regions[r].f)
			fclose(regions[r].f);
		/* the stub is freed with the memory regions */
	}
	memset(regions, 0, sizeof(regions));
	region_count = 0;
	free(cache); free(page_slot); free(slot_page); free(slot_used);
	cache = NULL; page_slot = slot_page = NULL; slot_used = NULL;
	total_pages = cache_slots = 0;
	misses_frame = misses_total = frames_total = worst_frame = next_victim = 0;
}
