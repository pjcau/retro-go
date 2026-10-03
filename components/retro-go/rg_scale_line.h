/* rg_scale_line: one viewport line scaled horizontally from its source line,
 * driven by the per-source-pixel repeat pattern instead of a map lookup per
 * output pixel, with the horizontal filter fused in (D2 of the Arcade 60 fps
 * plan). Byte-identical to the map loop + the separate filter pass of
 * rg_display.c: test/scale_line_test.c proves it on the PC against that code.
 *
 * The filter pass replaced every output pixel x in [1, width-2] that repeats
 * the pixel before it (same source pixel) by the blend of its neighbours, left
 * to right. For a source pixel drawn n times that gives n-1 copies of its colour
 * followed by the blend of its colour with the next drawn source pixel's - except
 * at the last output pixel, which the pass never touched. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

static inline unsigned rg_blend_pixels(unsigned a, unsigned b)
{
    // Fast path (taken 80-90% of the time)
    if (a == b)
        return a;

    // Not the original author, but a good explanation is found at:
    // https://medium.com/@luc.trudeau/fast-averaging-of-high-color-16-bit-pixels-cb4ac7fd1488
    // The pixels are big-endian 565: swap, average, swap back. The swaps are
    // masked to 16 bits: without the mask the high byte of a and b stayed in
    // bits 16-23 and came back OR-ed into the result's high byte (the low
    // green bits and the blue one step too bright on blended pixels).
    a = ((a << 8) | (a >> 8)) & 0xFFFFU;
    b = ((b << 8) | (b >> 8)) & 0xFFFFU;
    unsigned s = a ^ b;
    unsigned v = ((s & 0xF7DEU) >> 1) + (a & b) + (s & 0x0821U);
    return ((v << 8) | (v >> 8)) & 0xFFFFU;
}

// dst[x] = rg_blend_pixels(a[x], b[x]) for n pixels: the vertical filter's
// line. Two pixels per 32-bit word when the three lines are word aligned (the
// average of two 16-bit lanes never carries into the next lane).
static inline void rg_blend_line(uint16_t *dst, const uint16_t *a, const uint16_t *b, int n)
{
    int x = 0;
    if (!(((uintptr_t)dst | (uintptr_t)a | (uintptr_t)b) & 3))
    {
        const uint32_t *a32 = (const uint32_t *)a, *b32 = (const uint32_t *)b;
        uint32_t *d32 = (uint32_t *)dst;
        for (; x + 2 <= n; x += 2)
        {
            uint32_t p = *a32++, q = *b32++;
            if (p != q)
            {
                p = ((p & 0x00FF00FFU) << 8) | ((p >> 8) & 0x00FF00FFU);
                q = ((q & 0x00FF00FFU) << 8) | ((q >> 8) & 0x00FF00FFU);
                uint32_t s = p ^ q;
                uint32_t v = ((s & 0xF7DEF7DEU) >> 1) + (p & q) + (s & 0x08210821U);
                p = ((v & 0x00FF00FFU) << 8) | ((v >> 8) & 0x00FF00FFU);
            }
            *d32++ = p;
        }
    }
    for (; x < n; x++)
        dst[x] = rg_blend_pixels(a[x], b[x]);
}

#define RG_SCALE_LINE_BODY(PIXEL)                                              \
    {                                                                          \
        int i = 0, left = width;                                               \
        while (i < src_count && rep[i] == 0) i++;                              \
        while (i < src_count)                                                  \
        {                                                                      \
            unsigned c = PIXEL(i);                                             \
            int n = rep[i], j = i + 1;                                         \
            while (j < src_count && rep[j] == 0) j++;                          \
            left -= n;                                                         \
            for (; n > 1; n--)                                                 \
                *dst++ = c;                                                    \
            if (rep[i] > 1 && filter_x && left > 0 && j < src_count)           \
                *dst++ = rg_blend_pixels(c, PIXEL(j));                         \
            else                                                               \
                *dst++ = c;                                                    \
            i = j;                                                             \
        }                                                                      \
    }

static inline void rg_scale_line_pal(const uint8_t *src, const uint16_t *pal, const uint8_t *rep,
                                     int src_count, uint16_t *dst, int width, bool filter_x)
{
    #define PIXEL_PAL(i) (pal[src[i]])
    RG_SCALE_LINE_BODY(PIXEL_PAL)
    #undef PIXEL_PAL
}

// The palette scaler for the common upscale between 1x and 2x (the Neo Geo's
// 304 -> 434, the CPS1's 384 -> 480): every source pixel is drawn once or
// twice (rep[i] is 1 or 2 for all i < src_count; the caller checks it once per
// viewport). No test per pixel: each pixel is stored twice and the pointer
// moves by its count, the next pixel overwriting the spare copy. The last
// source pixel is written exactly, so nothing is written past the line.
// Same output as rg_scale_line_pal (test/scale_line_test.c).
static inline void rg_scale_line_pal12(const uint8_t *src, const uint16_t *pal, const uint8_t *rep,
                                       int src_count, uint16_t *dst, bool filter_x)
{
    const int last = src_count - 1;
    int i = 0;
    if (last < 0)
        return;
    if (!filter_x)
    {
        for (; i + 4 <= last; i += 4)
        {
            unsigned c0 = pal[src[i]], c1 = pal[src[i + 1]], c2 = pal[src[i + 2]], c3 = pal[src[i + 3]];
            dst[0] = c0; dst[1] = c0; dst += rep[i];
            dst[0] = c1; dst[1] = c1; dst += rep[i + 1];
            dst[0] = c2; dst[1] = c2; dst += rep[i + 2];
            dst[0] = c3; dst[1] = c3; dst += rep[i + 3];
        }
        for (; i < last; i++)
        {
            unsigned c = pal[src[i]];
            dst[0] = c; dst[1] = c; dst += rep[i];
        }
    }
    else
    {
        unsigned c = pal[src[0]];
        for (; i < last; i++)
        {
            unsigned next = pal[src[i + 1]];
            dst[0] = c;
            if (rep[i] == 2)                 // the second copy leans on the next pixel
                dst[1] = rg_blend_pixels(c, next);
            dst += rep[i];
            c = next;
        }
    }
    {
        unsigned c = pal[src[last]];         // no next pixel: plain copies
        dst[0] = c;
        if (rep[last] == 2)
            dst[1] = c;
    }
}

// The same idea for every upscale and every source format: when each source
// pixel is drawn 1 to R times (R = 2 or 4; the caller checks the repeat table
// once per viewport), a pixel is stored R times and the pointer moves by its
// count, the next pixel overwriting the spare copies. The last R source pixels
// are written exactly, so nothing is written past the line. With the filter,
// the last copy of a pixel drawn more than once leans on the next pixel, as in
// rg_scale_line_*. Same output (test/scale_line_test.c).
//   NES 256 -> 341, Game Boy 160 -> 355, Master System 256 -> 427, SNES 256 ->
//   366, GBA 240 -> 480 ...: R = 2 up to 2x, R = 4 up to 4x.
#define RG_SCALE_UP_BODY(PIXEL, R)                                             \
    {                                                                          \
        int i = 0, fast = src_count - (R);                                     \
        for (; i < fast; i++)                                                  \
        {                                                                      \
            unsigned c = PIXEL(i), n = rep[i];                                 \
            dst[0] = c; dst[1] = c;                                            \
            if ((R) > 2) { dst[2] = c; dst[3] = c; }                           \
            if (filter_x && n > 1)                                             \
                dst[n - 1] = rg_blend_pixels(c, PIXEL(i + 1));                 \
            dst += n;                                                          \
        }                                                                      \
        for (; i < src_count; i++)                                             \
        {                                                                      \
            unsigned c = PIXEL(i), n = rep[i], k;                              \
            for (k = 0; k < n; k++)                                            \
                dst[k] = c;                                                    \
            if (filter_x && n > 1 && i + 1 < src_count)                        \
                dst[n - 1] = rg_blend_pixels(c, PIXEL(i + 1));                 \
            dst += n;                                                          \
        }                                                                      \
    }

#define RG_SCALE_UP_FUNCS(NAME, SRCTYPE, DECL, PIXEL)                                                                 \
    static inline void rg_scale_line_##NAME##_up2(const SRCTYPE *src, DECL const uint8_t *rep, int src_count,          \
                                                 uint16_t *dst, bool filter_x) RG_SCALE_UP_BODY(PIXEL, 2)              \
    static inline void rg_scale_line_##NAME##_up4(const SRCTYPE *src, DECL const uint8_t *rep, int src_count,          \
                                                 uint16_t *dst, bool filter_x) RG_SCALE_UP_BODY(PIXEL, 4)

#define RG_UP_PAL(i) (pal[src[i]])
#define RG_UP_LE(i)  ((uint16_t)((src[i] << 8) | (src[i] >> 8)))
#define RG_UP_BE(i)  (src[i])
#define RG_UP_NODECL
#define RG_UP_COMMA ,
RG_SCALE_UP_FUNCS(pal, uint8_t, const uint16_t *pal RG_UP_COMMA, RG_UP_PAL)
RG_SCALE_UP_FUNCS(565le, uint16_t, RG_UP_NODECL, RG_UP_LE)
RG_SCALE_UP_FUNCS(565be, uint16_t, RG_UP_NODECL, RG_UP_BE)

static inline void rg_scale_line_565le(const uint16_t *src, const uint8_t *rep,
                                       int src_count, uint16_t *dst, int width, bool filter_x)
{
    #define PIXEL_LE(i) ((uint16_t)((src[i] << 8) | (src[i] >> 8)))
    RG_SCALE_LINE_BODY(PIXEL_LE)
    #undef PIXEL_LE
}

static inline void rg_scale_line_565be(const uint16_t *src, const uint8_t *rep,
                                       int src_count, uint16_t *dst, int width, bool filter_x)
{
    #define PIXEL_BE(i) (src[i])
    RG_SCALE_LINE_BODY(PIXEL_BE)
    #undef PIXEL_BE
}
