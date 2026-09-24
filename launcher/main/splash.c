#include "splash.h"

#include <math.h>
#include <string.h>

/*
 * Timeline (frames at 30 fps):
 *   0 - 27   a few thin rings drift into the centre while the lit area shrinks to it
 *  27 - 37   white flash out of the centre, title unmasked from its middle row outward
 *  37 - 126  title holds on a starfield, two shine sweeps, footer dithers in
 * 126 - 144  everything closes back into the centre, then black
 * The audio follows the same beats: rising sweep, "pling" chord with echo, sparkle on the shines.
 */

#define T_FLASH  27
#define T_HOLD   37
#define T_SHINE1 50
#define T_SHINE2 92
#define T_FOOTER 58
#define T_CLOSE  126
#define T_BLACK  144

#define W SPLASH_WIDTH
#define H SPLASH_HEIGHT

const uint32_t splash_palette[16] = {
    0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
    0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
};

// One step darker for each colour; repeated application always ends on 0 (black).
static const uint8_t darker[16] = {0, 0, 1, 1, 2, 1, 5, 6, 2, 4, 9, 3, 1, 5, 8, 9};

static const uint8_t bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Darken colour `c` by `level` steps (fractional part dithered).
static uint8_t dim(uint8_t c, float level, int x, int y)
{
    if (level <= 0.f)
        return c;
    int k = (int)(level + bayer4[y & 3][x & 3] / 16.f);
    while (k-- > 0 && c)
        c = darker[c];
    return c;
}

// ---------------------------------------------------------------------------- fonts

typedef struct
{
    char ch;
    uint8_t w;
    const char *rows; // h rows of w chars, '#' = set
} glyph_t;

static const glyph_t big_font[] = { // 5x7
    {'G', 5, ".###.#...##....#.####...##...#.###."},
    {'A', 5, ".###.#...##...#######...##...##...#"},
    {'M', 5, "#...###.###.#.##.#.##...##...##...#"},
    {'E', 5, "######....#....####.#....#....#####"},
    {'B', 5, "####.#...##...#####.#...##...#####."},
    {'R', 5, "####.#...##...#####.#.#..#..#.#...#"},
    {'O', 5, ".###.#...##...##...##...##...#.###."},
    {'W', 5, "#...##...##...##.#.##.#.###.###...#"},
    {'!', 2, "##########..##"},
    {' ', 3, NULL},
};

static const glyph_t small_font[] = { // Nx5
    {'C', 3, ".###..#..#...##"},
    {'P', 3, "##.#.###.#..#.."},
    {'J', 3, "..#..#..##.#.#."},
    {'&', 4, ".#..#.#..#..#.#..#.#"},
    {'b', 3, "#..#..##.#.###."},
    {'y', 3, "#.##.#.##..###."},
    {'2', 3, "##...#.#.#..###"},
    {'0', 3, ".#.#.##.##.#.#."},
    {'6', 3, ".###..##.#.#.#."},
    {' ', 2, NULL},
};

static const glyph_t *find_glyph(const glyph_t *font, int count, char ch)
{
    for (int i = 0; i < count; i++)
        if (font[i].ch == ch)
            return &font[i];
    return &font[count - 1]; // space
}

static const char TITLE[] = "GAME BRO!";
static const char FOOTER[] = "CPJ & CP by 2026";

#define BIG_COUNT (int)(sizeof(big_font) / sizeof(glyph_t))
#define SMALL_COUNT (int)(sizeof(small_font) / sizeof(glyph_t))
#define TITLE_SCALE 3
#define TITLE_GH 7
#define TITLE_Y 62
#define FOOTER_Y 148

static int text_width(const glyph_t *font, int count, const char *s, int scale)
{
    int w = 0;
    for (; *s; s++)
        w += (find_glyph(font, count, *s)->w + 1) * scale;
    return w - scale;
}

// Glyph row (0..6) of the title pixel at (x, y), or -1 if not lit.
static int title_row(int x, int y)
{
    static int x0 = -1;
    if (x0 < 0)
        x0 = (W - text_width(big_font, BIG_COUNT, TITLE, TITLE_SCALE)) / 2;
    int gy = (y - TITLE_Y) / TITLE_SCALE;
    if (y < TITLE_Y || gy >= TITLE_GH || x < x0)
        return -1;
    int cx = x0;
    for (const char *s = TITLE; *s; s++)
    {
        const glyph_t *g = find_glyph(big_font, BIG_COUNT, *s);
        int gw = g->w * TITLE_SCALE;
        if (x < cx + gw)
        {
            int gx = (x - cx) / TITLE_SCALE;
            return (g->rows && g->rows[gy * g->w + gx] == '#') ? gy : -1;
        }
        cx += gw + TITLE_SCALE;
        if (x < cx)
            return -1;
    }
    return -1;
}

static void draw_small_text(uint8_t *out, const char *s, int y, uint8_t color)
{
    int x = (W - text_width(small_font, SMALL_COUNT, s, 1)) / 2;
    for (; *s; s++)
    {
        const glyph_t *g = find_glyph(small_font, SMALL_COUNT, *s);
        for (int gy = 0; g->rows && gy < 5; gy++)
            for (int gx = 0; gx < g->w; gx++)
                if (g->rows[gy * g->w + gx] == '#')
                    out[(y + gy) * W + x + gx] = color;
        x += g->w + 1;
    }
}

// ---------------------------------------------------------------------------- video

static uint32_t hash32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352d;
    v ^= v >> 15;
    v *= 0x846ca68b;
    v ^= v >> 16;
    return v;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Rectangular distance from the centre, scaled to the 3:2 screen (0 at centre, ~240 at the edge).
static int ring_dist(int x, int y)
{
    int dx = 2 * x - (W - 1), dy = 2 * y - (H - 1);
    dx = dx < 0 ? -dx : dx;
    dy = (dy < 0 ? -dy : dy) * 3 / 2;
    return dx > dy ? dx : dy;
}

// Ring colour by distance: white at the centre, dark blue at the edge
static const uint8_t ring_ramp[] = {7, 15, 14, 8, 2, 1, 1};

static void draw_tunnel(uint8_t *out, int f)
{
    float pos = f * 4.f;                              // constant inward drift
    float fade_in = clampf(3.f - f * 0.5f, 0.f, 6.f); // first 6 frames rise out of black
    float radius = 280.f - clampf((f - 6) / (T_FLASH - 6.f), 0.f, 1.f) * 280.f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            int d = ring_dist(x, y);
            uint8_t c = ((int)(d + pos) % 40 < 5) ? ring_ramp[d / 40] : 0;
            float level = fade_in + clampf((d - radius) / 16.f, 0.f, 6.f);
            out[y * W + x] = dim(c, level, x, y);
        }
}

static void draw_stars(uint8_t *out, int f, float level)
{
    for (int i = 0; i < 70; i++)
    {
        uint32_t h = hash32(i * 2654435761u + 1);
        int dx = (int)(h & 0xFF) - 128, dy = (int)((h >> 8) & 0xFF) - 128;
        if (dx * dx + dy * dy < 400)
            continue;
        float speed = 0.004f + ((h >> 16) & 0xFF) / 255.f * 0.012f;
        float r = fmodf(((h >> 24) / 255.f) + f * speed, 1.f); // drift out from the centre
        int x = W / 2 + (int)(dx * r * 1.1f), y = H / 2 + (int)(dy * r * 0.75f);
        if (x < 0 || x >= W || y < 0 || y >= H)
            continue;
        uint8_t c = r < 0.35f ? 5 : (r < 0.7f ? 6 : 7);
        if (((f + i) & 15) == 0)
            c = 12; // twinkle
        out[y * W + x] = dim(c, level, x, y);
    }
}

static const uint8_t title_rows[TITLE_GH] = {7, 10, 10, 9, 9, 8, 8};

static void draw_title(uint8_t *out, int f, int reveal)
{
    int shine = -1000;
    if (f >= T_SHINE1 && f < T_SHINE1 + 18)
        shine = (f - T_SHINE1) * 14 - 40;
    else if (f >= T_SHINE2 && f < T_SHINE2 + 18)
        shine = (f - T_SHINE2) * 14 - 40;

    int y0 = TITLE_Y - 2, y1 = TITLE_Y + TITLE_GH * TITLE_SCALE + 3;
    int mid = (TITLE_Y * 2 + TITLE_GH * TITLE_SCALE) / 2;
    for (int y = y0; y < y1; y++)
    {
        if (y - mid > reveal || mid - y > reveal + 1)
            continue;
        for (int x = 0; x < W; x++)
        {
            int row = title_row(x, y);
            uint8_t *p = &out[y * W + x];
            if (row >= 0)
            {
                int s = x + y - shine - 60;
                *p = (s >= 0 && s < 4) ? 7 : (s >= 4 && s < 7) ? 15 : title_rows[row];
            }
            else if (title_row(x - 2, y - 2) >= 0)
                *p = 1; // drop shadow
            else if (title_row(x - 1, y) >= 0 || title_row(x + 1, y) >= 0 ||
                     title_row(x, y - 1) >= 0 || title_row(x, y + 1) >= 0)
                *p = 2; // outline
        }
    }
}

void splash_render(uint8_t *out, int f)
{
    if (f < T_FLASH)
    {
        draw_tunnel(out, f);
        return;
    }

    memset(out, 0, W * H);

    // Stars fade in after the flash, and everything closes into the centre at the end
    draw_stars(out, f, clampf((T_HOLD + 4 - f) / 4.f, 0.f, 6.f));
    draw_title(out, f, (f - T_FLASH - 2) * 2);
    draw_small_text(out, FOOTER, FOOTER_Y, 6);
    if (f < T_FOOTER + 16)
    {
        // Footer dithers in over ~half a second
        float level = clampf((T_FOOTER + 14 - f) / 4.f, 0.f, 6.f);
        for (int y = FOOTER_Y; y < FOOTER_Y + 5; y++)
            for (int x = 0; x < W; x++)
                out[y * W + x] = dim(out[y * W + x], level, x, y);
    }

    // White flash expanding from the centre
    if (f < T_HOLD)
    {
        float radius = (f - T_FLASH + 1) * 110.f; // fills the screen in 3 frames
        float level = (f - T_FLASH - 2) / 1.6f;    // then dithers white -> black
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (ring_dist(x, y) < radius)
                {
                    uint8_t c = dim(7, level, x, y);
                    if (c)
                        out[y * W + x] = c;
                }
    }

    // Close into the centre
    if (f >= T_CLOSE)
    {
        float radius = 260.f - (f - T_CLOSE) / (float)(T_BLACK - T_CLOSE) * 260.f;
        float global = (f - T_CLOSE) / 6.f;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                float level = global * 0.5f + clampf((ring_dist(x, y) - radius) / 12.f, 0.f, 6.f);
                out[y * W + x] = dim(out[y * W + x], level, x, y);
            }
    }
}

// ---------------------------------------------------------------------------- audio

static float square(float phase, float duty)
{
    return (phase - floorf(phase)) < duty ? 1.f : -1.f;
}

static float triangle(float phase)
{
    float p = phase - floorf(phase);
    return 4.f * (p < 0.5f ? p : 1.f - p) - 1.f;
}

static float note(float t, float t0, float freq, float decay, float duty, float gain)
{
    if (t < t0)
        return 0.f;
    float dt = t - t0;
    float env = expf(-dt * decay) * (dt < 0.004f ? dt / 0.004f : 1.f);
    return square(freq * dt, duty) * env * gain;
}

// The jingle without echo, as a pure function of time.
static float dry(float t)
{
    float s = 0.f;

    const float t0 = T_FLASH / (float)SPLASH_FPS; // the flash

    // Intro: soft square sweep 110 -> 880 Hz with the rings
    if (t < t0)
    {
        const float k = 3.f / t0 * 0.6931472f;
        float phase = 110.f * (expf(k * t) - 1.f) / k;
        s += square(phase, 0.25f) * (t / t0) * 0.18f;
    }

    // Flash: "pling" arpeggio into an E major chord, triangle bass underneath
    s += note(t, t0 + 0.00f, 659.25f, 9.f, 0.125f, 0.22f);
    s += note(t, t0 + 0.06f, 987.77f, 9.f, 0.125f, 0.22f);
    s += note(t, t0 + 0.12f, 1318.5f, 2.2f, 0.125f, 0.20f);
    s += note(t, t0 + 0.12f, 1661.2f, 2.4f, 0.25f, 0.12f);
    s += note(t, t0 + 0.12f, 1975.5f, 2.6f, 0.25f, 0.10f);
    if (t > t0)
        s += triangle(164.81f * (t - t0)) * expf(-(t - t0) * 1.6f) * 0.35f;

    // Sparkles on the two shine sweeps
    for (int i = 0; i < 4; i++)
    {
        static const float sparkle[4] = {2637.f, 3136.f, 3951.f, 5274.f};
        s += note(t, (T_SHINE1 + 6) / (float)SPLASH_FPS + i * 0.045f, sparkle[i], 14.f, 0.5f, 0.06f);
        s += note(t, (T_SHINE2 + 6) / (float)SPLASH_FPS + i * 0.045f, sparkle[3 - i], 14.f, 0.5f, 0.045f);
    }
    return s;
}

float splash_sample(int n)
{
    float t = n / (float)SPLASH_RATE;
    float s = dry(t) + 0.35f * dry(t - 0.15f) + 0.12f * dry(t - 0.30f);
    s *= clampf((T_BLACK / (float)SPLASH_FPS - t) / 0.5f, 0.f, 1.f); // fade out with the picture
    return clampf(s, -1.f, 1.f);
}

// ---------------------------------------------------------------------------- playback

#ifndef SPLASH_HOST
#include <rg_system.h>

void splash_play(void)
{
    uint8_t *canvas = rg_alloc(W * H, MEM_ANY | MEM_NOPANIC);
    uint16_t *frame = rg_alloc(W * 2 * H * 2 * 2, MEM_SLOW | MEM_NOPANIC);
    rg_audio_frame_t *audio = rg_alloc((SPLASH_RATE / SPLASH_FPS + 2) * sizeof(rg_audio_frame_t), MEM_ANY | MEM_NOPANIC);
    if (!canvas || !frame || !audio)
    {
        RG_LOGE("splash: out of memory");
        goto done;
    }

    uint16_t pal[16];
    for (int i = 0; i < 16; i++)
    {
        uint32_t c = splash_palette[i];
        pal[i] = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
    }

    uint32_t held = rg_input_read_gamepad();
    int64_t start = rg_system_timer();

    for (int f = 0; f < SPLASH_FRAMES; f++)
    {
        if (rg_input_read_gamepad() & ~held)
            break; // any newly pressed button skips

        splash_render(canvas, f);
        for (int y = 0; y < H; y++)
        {
            uint16_t *line = frame + y * 2 * (W * 2);
            for (int x = 0; x < W; x++)
                line[x * 2] = line[x * 2 + 1] = pal[canvas[y * W + x]];
            memcpy(line + W * 2, line, W * 2 * 2);
        }
        rg_display_write_rect(0, 0, W * 2, H * 2, W * 2 * 2, frame, 0);

        int n0 = (int)((int64_t)f * SPLASH_RATE / SPLASH_FPS);
        int n1 = (int)((int64_t)(f + 1) * SPLASH_RATE / SPLASH_FPS);
        for (int n = n0; n < n1; n++)
        {
            int16_t v = (int16_t)(splash_sample(n) * 26000.f);
            audio[n - n0] = (rg_audio_frame_t){v, v};
        }
        rg_audio_submit(audio, n1 - n0);

        int64_t wait = start + (int64_t)(f + 1) * 1000000 / SPLASH_FPS - rg_system_timer();
        if (wait > 0)
            rg_usleep(wait);
    }

done:
    free(canvas);
    free(frame);
    free(audio);
    rg_display_clear(C_BLACK);
}
#endif
