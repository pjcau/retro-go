/* rg_scale_line against the reference it replaces (rg_display.c: the
 * map_viewport_to_source_x loop, then the horizontal filter pass), on the
 * real map formula, for the Neo Geo's 304 -> 434, 1:1, downscales, 3x, and
 * random sizes; palette, 565 LE and 565 BE sources; filter on and off.
 * Build and run: test/run_scale_line_test.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../rg_scale_line.h"

#define FLOAT_TO_INT(x) ((int)((x) + 0.5f))   /* rg_display.c */
static uint32_t rnd(void) { static uint32_t s = 0x9E3779B9; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

static int16_t map[4096];
static uint8_t rep[4096];
static int src_count;                 /* map[width-1] + 1: what the scaler is given */

static void build_map(int src_w, int width)
{
    float step = (float)src_w / width;
    memset(rep, 0, sizeof rep);
    for (int x = 0; x <= width; x++)
        map[x] = FLOAT_TO_INT(x * step);
    for (int x = 0; x < width; x++)
        rep[map[x]]++;
    src_count = map[width - 1] + 1;
}

/* the reference: rg_display.c RENDER_LINE + the filter_x pass */
static void reference(int fmt, const void *src, const uint16_t *pal, uint16_t *out, int width, int filter_x)
{
    for (int xx = 0; xx < width; xx++)
    {
        int x = map[xx];
        if (fmt == 0) out[xx] = pal[((const uint8_t *)src)[x]];
        else if (fmt == 1) { uint16_t v = ((const uint16_t *)src)[x]; out[xx] = (v << 8) | (v >> 8); }
        else out[xx] = ((const uint16_t *)src)[x];
    }
    if (filter_x)
        for (int x = 1; x < width - 1; ++x)
            if (map[x] == map[x - 1])
                out[x] = rg_blend_pixels(out[x - 1], out[x + 1]);
}

int main(void)
{
    static const int sizes[][2] = { {304, 434}, {320, 320}, {288, 320}, {256, 480}, {160, 480}, {512, 480}, {384, 480}, {640, 320}, {224, 434} };
    static uint8_t src8[4096]; static uint16_t src16[4096], pal[256], ref[4096], got[4096];
    int fail = 0, cases = 0;
    for (int t = 0; t < 400; t++)
    {
        int src_w, width;
        if (t < (int)(sizeof sizes / sizeof *sizes)) { src_w = sizes[t][0]; width = sizes[t][1]; }
        else { src_w = 16 + rnd() % 700; width = (16 + rnd() % 700) & ~1; }
        build_map(src_w, width);
        for (int i = 0; i < 256; i++) pal[i] = rnd();
        for (int i = 0; i < src_w + 2; i++) { src8[i] = rnd(); src16[i] = rnd(); }
        /* runs of equal pixels, as real frames have, so the blend fast path and the slow one both run */
        for (int i = 1; i < src_w; i++) if (rnd() % 3) { src8[i] = src8[i - 1]; src16[i] = src16[i - 1]; }
        for (int fmt = 0; fmt < 3; fmt++)
            for (int f = 0; f < 2; f++)
            {
                memset(got, 0xAA, sizeof got);
                reference(fmt, fmt ? (void *)src16 : (void *)src8, pal, ref, width, f);
                if (fmt == 0) rg_scale_line_pal(src8, pal, rep, src_count, got, width, f);
                else if (fmt == 1) rg_scale_line_565le(src16, rep, src_count, got, width, f);
                else rg_scale_line_565be(src16, rep, src_count, got, width, f);
                cases++;
                if (memcmp(ref, got, width * 2) || got[width] != 0xAAAA)
                {
                    int x = 0; while (x < width && ref[x] == got[x]) x++;
                    printf("FAIL %d -> %d fmt %d filter %d: first difference at x=%d (ref %04x got %04x), overrun %s\n",
                        src_w, width, fmt, f, x, ref[x], got[x], got[width] != 0xAAAA ? "yes" : "no");
                    if (++fail > 10) return 1;
                }
            }
    }
    printf("scale_line_test: %d cases, %s\n", cases, fail ? "FAILURES" : "PASS");
    return fail != 0;
}
