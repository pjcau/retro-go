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
    a = (a << 8) | (a >> 8);
    b = (b << 8) | (b >> 8);
    unsigned s = a ^ b;
    unsigned v = ((s & 0xF7DEU) >> 1) + (a & b) + (s & 0x0821U);
    return (v << 8) | (v >> 8);
}

/* rep[i]: how many output pixels source pixel i (0..src_count-1) is drawn as;
   the sum of rep[] is width. PIXEL(i) is the output colour of source pixel i.
   src_count is map[width-1] + 1, not the source width: the map rounds, and the
   last output pixels of some sizes (160 -> 480, for one) come from the pixel
   just past the source line, as the map loop always read it. */
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
