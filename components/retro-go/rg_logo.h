#pragma once

// The loading logo: "GAME" over "BRO!" in the boot splash's 5x7 font and colours
// (launcher/main/splash.c), drawn 2x into the 48x48 box the hourglass had. It
// moves in a 16-frame loop: the letters hop one after another and a shine
// sweeps across them. Pure function of (x, y, frame), so the host preview
// (scripts/loading_logo_preview.c) renders exactly what the board draws.

#include <stdint.h>

#define RG_LOGO_SIZE 48
#define RG_LOGO_FRAMES 16

// PICO-8 colours, as in the splash
static const uint32_t rg_logo_palette[16] = {
    0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
    0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
};

// 5x7 glyphs (2 wide for '!'), '#' = set
static const struct { char ch; uint8_t w; const char *rows; } rg_logo_font[] = {
    {'G', 5, ".###.#...##....#.####...##...#.###."},
    {'A', 5, ".###.#...##...#######...##...##...#"},
    {'M', 5, "#...###.###.#.##.#.##...##...##...#"},
    {'E', 5, "######....#....####.#....#....#####"},
    {'B', 5, "####.#...##...#####.#...##...#####."},
    {'R', 5, "####.#...##...#####.#.#..#..#.#...#"},
    {'O', 5, ".###.#...##...##...##...##...#.###."},
    {'!', 2, "##########..##"},
};

// Each line: its glyphs (index into rg_logo_font), left edge and top in box pixels
static const struct { uint8_t glyph[4]; int8_t x0, y0; } rg_logo_lines[2] = {
    {{0, 1, 2, 3}, 1, 8},  // GAME: 4 x 12 - 2 = 46 px
    {{4, 5, 6, 7}, 4, 27}, // BRO!: 3 x 12 + 6 - 2 = 40 px
};

// Glyph rows: white top, then yellow, orange, red
static const uint8_t rg_logo_row_colour[7] = {7, 10, 10, 9, 9, 8, 8};

// How high letter n (0..7) is lifted at frame f: a hop that runs along the letters
static inline int rg_logo_lift(int n, int f)
{
    static const int8_t hop[RG_LOGO_FRAMES] = {0, 2, 4, 4, 2, 0};
    return hop[(f - n + RG_LOGO_FRAMES) % RG_LOGO_FRAMES];
}

// Glyph row (0..6) lit at box pixel (x, y) in frame f, or -1
static inline int rg_logo_lit(int x, int y, int f)
{
    for (int l = 0; l < 2; l++)
    {
        int gx = rg_logo_lines[l].x0;
        for (int i = 0; i < 4; i++)
        {
            int g = rg_logo_lines[l].glyph[i], w = rg_logo_font[g].w;
            if (x >= gx && x < gx + w * 2)
            {
                int gy = rg_logo_lines[l].y0 - rg_logo_lift(l * 4 + i, f);
                int r = (y - gy) >> 1, c = (x - gx) >> 1;
                if (y >= gy && r < 7 && rg_logo_font[g].rows[r * w + c] == '#')
                    return r;
                break;
            }
            gx += w * 2 + 2;
        }
    }
    return -1;
}

// Palette index of box pixel (x, y) in frame f
static inline uint8_t rg_logo_pixel(int x, int y, int f)
{
    int r = rg_logo_lit(x, y, f);
    if (r >= 0)
    {
        int s = x + y - (f * 8 - 24); // the shine: a diagonal band crossing the box once a loop
        return (s >= 0 && s < 3) ? 7 : (s >= 3 && s < 5) ? 15 : rg_logo_row_colour[r];
    }
    if (rg_logo_lit(x - 1, y, f) >= 0 || rg_logo_lit(x + 1, y, f) >= 0 ||
        rg_logo_lit(x, y - 1, f) >= 0 || rg_logo_lit(x, y + 1, f) >= 0)
        return 2; // outline, dark purple
    if (rg_logo_lit(x - 2, y - 2, f) >= 0)
        return 1; // shadow, dark blue
    return 0;
}
