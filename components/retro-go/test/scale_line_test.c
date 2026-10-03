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
    static const int sizes[][2] = { {304, 434}, {320, 320}, {288, 320}, {256, 480}, {160, 480}, {512, 480}, {384, 480}, {640, 320}, {224, 434}, {384, 434}, {304, 480}, {320, 480} };
    int cases12 = 0, blends = 0;
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
                if (fmt == 0)
                {
                    /* the 1x-2x scaler, where it applies: same line, nothing past its end */
                    int simple = 1;
                    for (int i = 0; i < src_count; i++) if (rep[i] != 1 && rep[i] != 2) simple = 0;
                    if (simple)
                    {
                        static uint16_t got12[4096];
                        memset(got12, 0xAA, sizeof got12);
                        rg_scale_line_pal12(src8, pal, rep, src_count, got12, f);
                        cases12++;
                        if (memcmp(ref, got12, width * 2) || got12[width] != 0xAAAA)
                        {
                            int x = 0; while (x < width && ref[x] == got12[x]) x++;
                            printf("FAIL pal12 %d -> %d filter %d: first difference at x=%d (ref %04x got %04x), overrun %s\n",
                                src_w, width, f, x, ref[x], got12[x], got12[width] != 0xAAAA ? "yes" : "no");
                            if (++fail > 10) return 1;
                        }
                    }
                }
                if (memcmp(ref, got, width * 2) || got[width] != 0xAAAA)
                {
                    int x = 0; while (x < width && ref[x] == got[x]) x++;
                    printf("FAIL %d -> %d fmt %d filter %d: first difference at x=%d (ref %04x got %04x), overrun %s\n",
                        src_w, width, fmt, f, x, ref[x], got[x], got[width] != 0xAAAA ? "yes" : "no");
                    if (++fail > 10) return 1;
                }
            }
    }
    /* rg_blend_line against rg_blend_pixels, aligned and not, odd and even lengths; and
       rg_blend_pixels against the average computed channel by channel */
    for (int t = 0; t < 2000; t++)
    {
        static uint16_t a[1100], b[1100], d[1100];
        int n = 1 + rnd() % 1000, oa = rnd() & 1, ob = rnd() & 1, od = rnd() & 1;
        for (int i = 0; i < 1100; i++) { a[i] = rnd(); b[i] = rnd() % 4 ? a[i] : rnd(); d[i] = 0xAAAA; }
        if (t % 3 == 0) oa = ob = od = 0;
        rg_blend_line(d + od, a + oa, b + ob, n);
        blends++;
        for (int x = 0; x < n; x++)
        {
            unsigned p = a[oa + x], q = b[ob + x], want = rg_blend_pixels(p, q);
            unsigned ps = ((p << 8) | (p >> 8)) & 0xFFFF, qs = ((q << 8) | (q >> 8)) & 0xFFFF;
            unsigned r = (((ps >> 11) & 31) + ((qs >> 11) & 31) + 1) >> 1, g = (((ps >> 5) & 63) + ((qs >> 5) & 63) + 1) >> 1, bl = ((ps & 31) + (qs & 31) + 1) >> 1;
            unsigned avg = (r << 11) | (g << 5) | bl, avg_be = ((avg << 8) | (avg >> 8)) & 0xFFFF;
            if (d[od + x] != want || (p != q && want != avg_be) || (p == q && want != p))
            {
                printf("FAIL blend n=%d x=%d: a %04x b %04x line %04x pixel %04x per-channel average %04x\n", n, x, p, q, d[od + x], want, avg_be);
                if (++fail > 10) return 1;
                break;
            }
        }
        if (d[od + n] != 0xAAAA) { printf("FAIL blend line overrun n=%d\n", n); fail++; }
    }
    printf("scale_line_test: %d cases, %d on the 1x-2x scaler, %d blended lines, %s\n", cases, cases12, blends, fail ? "FAILURES" : "PASS");
    return fail != 0;
}
