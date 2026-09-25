#include "splash.h"

#include <math.h>
#include <stdlib.h>
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

// Darken colour `c` by `level16` / 16 steps (fractional part dithered). Integer-only: it runs per pixel.
static inline uint8_t dim16(uint8_t c, int level16, int x, int y)
{
    if (level16 <= 0 || !c)
        return c;
    int k = (level16 + bayer4[y & 3][x & 3]) >> 4;
    while (k-- > 0 && c)
        c = darker[c];
    return c;
}

static inline uint8_t dim(uint8_t c, float level, int x, int y)
{
    return dim16(c, (int)(level * 16.f), x, y);
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

// Title layer, built once: 0 = empty, 1 = drop shadow, 2 = outline, 3 + glyph row = letter.
#define TMASK_Y0 (TITLE_Y - 1)
#define TMASK_H (TITLE_GH * TITLE_SCALE + 3)
static uint8_t *title_mask;

static uint8_t *get_title_mask(void)
{
    if (title_mask || !(title_mask = malloc(TMASK_H * W)))
        return title_mask;
    for (int my = 0; my < TMASK_H; my++)
        for (int x = 0; x < W; x++)
        {
            int y = TMASK_Y0 + my, row = title_row(x, y);
            uint8_t k = 0;
            if (row >= 0)
                k = 3 + row;
            else if (title_row(x - 2, y - 2) >= 0)
                k = 1;
            else if (title_row(x - 1, y) >= 0 || title_row(x + 1, y) >= 0 ||
                     title_row(x, y - 1) >= 0 || title_row(x, y + 1) >= 0)
                k = 2;
            title_mask[my * W + x] = k;
        }
    return title_mask;
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
    int pos = f * 4;                                                 // constant inward drift
    int fade16 = 48 - f * 8 > 0 ? 48 - f * 8 : 0;                    // first 6 frames rise out of black
    int radius = 280 - 280 * (f < 6 ? 0 : f > T_FLASH ? T_FLASH - 6 : f - 6) / (T_FLASH - 6);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            int d = ring_dist(x, y);
            uint8_t c = ((d + pos) % 40 < 5) ? ring_ramp[d / 40] : 0;
            int edge = d - radius;
            out[y * W + x] = dim16(c, fade16 + (edge < 0 ? 0 : edge > 96 ? 96 : edge), x, y);
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
    const uint8_t *mask = get_title_mask();
    if (!mask)
        return;

    int shine = -1000;
    if (f >= T_SHINE1 && f < T_SHINE1 + 18)
        shine = (f - T_SHINE1) * 14 - 40;
    else if (f >= T_SHINE2 && f < T_SHINE2 + 18)
        shine = (f - T_SHINE2) * 14 - 40;

    int mid = (TITLE_Y * 2 + TITLE_GH * TITLE_SCALE) / 2;
    for (int my = 0; my < TMASK_H; my++)
    {
        int y = TMASK_Y0 + my;
        if (y - mid > reveal || mid - y > reveal + 1)
            continue;
        const uint8_t *m = mask + my * W;
        uint8_t *p = out + y * W;
        for (int x = 0; x < W; x++)
        {
            uint8_t k = m[x];
            if (k >= 3)
            {
                int s = x + y - shine - 60;
                p[x] = (s >= 0 && s < 4) ? 7 : (s >= 4 && s < 7) ? 15 : title_rows[k - 3];
            }
            else if (k)
                p[x] = k; // 1 = shadow (dark blue), 2 = outline (dark purple)
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
        int level16 = (int)((f - T_FLASH - 2) / 1.6f * 16.f); // then dithers white -> black
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (ring_dist(x, y) < radius)
                {
                    uint8_t c = dim16(7, level16, x, y);
                    if (c)
                        out[y * W + x] = c;
                }
    }

    // Close into the centre
    if (f >= T_CLOSE)
    {
        int radius = 260 - 260 * (f - T_CLOSE) / (T_BLACK - T_CLOSE);
        int global16 = (f - T_CLOSE) * 4 / 3;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                uint8_t *p = &out[y * W + x];
                if (!*p)
                    continue;
                int edge = (ring_dist(x, y) - radius) * 4 / 3;
                *p = dim16(*p, global16 + (edge < 0 ? 0 : edge > 96 ? 96 : edge), x, y);
            }
    }
}

// ---------------------------------------------------------------------------- audio

// A small stateful synth: phase accumulators and multiplicative envelopes, so a sample costs a
// handful of multiply-adds. (Evaluating every note as a closed-form function of time, echo taps
// included, needed ~45 expf per sample and ran slower than real time on the ESP32-S3.)

#define ATTACK 128 // samples (4 ms)
#define ECHO1 (SPLASH_RATE * 15 / 100) // 0.15 s
#define ECHO2 SPLASH_ECHO_LEN          // 0.30 s

typedef struct
{
    float time, freq, decay, duty, gain; // duty < 0 = triangle
} note_t;

typedef struct
{
    int start, age;
    float phase, inc, env, mul, duty, gain;
} voice_t;

#define T0 (T_FLASH / (float)SPLASH_FPS)
#define TS1 ((T_SHINE1 + 6) / (float)SPLASH_FPS)
#define TS2 ((T_SHINE2 + 6) / (float)SPLASH_FPS)

static const note_t score[] = {
    // Flash: "pling" arpeggio into an E major chord, triangle bass underneath
    {T0 + 0.00f, 659.25f, 9.0f, 0.125f, 0.22f},
    {T0 + 0.06f, 987.77f, 9.0f, 0.125f, 0.22f},
    {T0 + 0.12f, 1318.5f, 2.2f, 0.125f, 0.20f},
    {T0 + 0.12f, 1661.2f, 2.4f, 0.25f, 0.12f},
    {T0 + 0.12f, 1975.5f, 2.6f, 0.25f, 0.10f},
    {T0 + 0.00f, 164.81f, 1.6f, -1.f, 0.35f},
    // Sparkles on the two shine sweeps
    {TS1 + 0.000f, 2637.f, 14.f, 0.5f, 0.06f},
    {TS1 + 0.045f, 3136.f, 14.f, 0.5f, 0.06f},
    {TS1 + 0.090f, 3951.f, 14.f, 0.5f, 0.06f},
    {TS1 + 0.135f, 5274.f, 14.f, 0.5f, 0.06f},
    {TS2 + 0.000f, 5274.f, 14.f, 0.5f, 0.045f},
    {TS2 + 0.045f, 3951.f, 14.f, 0.5f, 0.045f},
    {TS2 + 0.090f, 3136.f, 14.f, 0.5f, 0.045f},
    {TS2 + 0.135f, 2637.f, 14.f, 0.5f, 0.045f},
};
#define VOICES (int)(sizeof(score) / sizeof(note_t))

static struct
{
    voice_t voice[VOICES];
    float *delay;
    int pos, n;
    float sweep_phase, sweep_inc, sweep_mul;
} synth;

void splash_audio_init(float *delay)
{
    memset(&synth, 0, sizeof(synth));
    memset(delay, 0, SPLASH_ECHO_LEN * sizeof(float));
    synth.delay = delay;
    for (int i = 0; i < VOICES; i++)
    {
        const note_t *n = &score[i];
        synth.voice[i] = (voice_t){
            .start = (int)(n->time * SPLASH_RATE),
            .inc = n->freq / SPLASH_RATE,
            .env = 1.f,
            .mul = expf(-n->decay / SPLASH_RATE),
            .duty = n->duty,
            .gain = n->gain,
        };
    }
    // Intro: square sweep 110 -> 880 Hz (3 octaves) up to the flash
    synth.sweep_inc = 110.f / SPLASH_RATE;
    synth.sweep_mul = powf(2.f, 3.f / (T0 * SPLASH_RATE));
}

float splash_audio_next(void)
{
    const int n = synth.n++;
    const int n_flash = (int)(T0 * SPLASH_RATE);
    float s = 0.f;

    if (n < n_flash)
    {
        s += (synth.sweep_phase < 0.25f ? 0.18f : -0.18f) * n / n_flash;
        synth.sweep_phase += synth.sweep_inc;
        if (synth.sweep_phase >= 1.f)
            synth.sweep_phase -= 1.f;
        synth.sweep_inc *= synth.sweep_mul;
    }

    for (int i = 0; i < VOICES; i++)
    {
        voice_t *v = &synth.voice[i];
        if (n < v->start || v->env < 0.0005f)
            continue;
        float amp = v->env * v->gain * (v->age < ATTACK ? v->age / (float)ATTACK : 1.f);
        float p = v->phase;
        s += amp * (v->duty < 0.f ? 4.f * (p < 0.5f ? p : 1.f - p) - 1.f : (p < v->duty ? 1.f : -1.f));
        v->phase = p + v->inc >= 1.f ? p + v->inc - 1.f : p + v->inc;
        v->env *= v->mul;
        v->age++;
    }

    // Echo: taps at 0.15 s and 0.30 s of the dry signal
    float *d = synth.delay;
    float out = s + 0.35f * d[(synth.pos + SPLASH_ECHO_LEN - ECHO1) % SPLASH_ECHO_LEN] + 0.12f * d[synth.pos];
    d[synth.pos] = s;
    synth.pos = (synth.pos + 1) % SPLASH_ECHO_LEN;

    out *= clampf((T_BLACK * SPLASH_RATE / SPLASH_FPS - n) / (0.5f * SPLASH_RATE), 0.f, 1.f); // fade with the picture
    return clampf(out, -1.f, 1.f);
}

// ---------------------------------------------------------------------------- playback

#ifndef SPLASH_HOST
#include <rg_system.h>

#define BAND 8           // logical rows per LCD write (16 panel rows)
#define AUDIO_CHUNK 512  // samples per rg_audio_submit
#define TOTAL_SAMPLES (SPLASH_FRAMES * SPLASH_RATE / SPLASH_FPS)

static volatile bool audio_stop, audio_done;

// The jingle runs in its own task so the sound stays continuous whatever the frame rate.
// rg_audio_submit blocks on the I2S DMA (or sleeps the equivalent time when muted), which paces it.
static void audio_task(void *arg)
{
    rg_audio_frame_t *buf = arg;
    for (int n = 0; n < TOTAL_SAMPLES && !audio_stop; n += AUDIO_CHUNK)
    {
        int count = TOTAL_SAMPLES - n < AUDIO_CHUNK ? TOTAL_SAMPLES - n : AUDIO_CHUNK;
        for (int i = 0; i < count; i++)
        {
            int16_t v = (int16_t)(splash_audio_next() * 26000.f);
            buf[i] = (rg_audio_frame_t){v, v};
        }
        rg_audio_submit(buf, count);
    }
    audio_done = true;
}

void splash_play(void)
{
    uint8_t *canvas = rg_alloc(W * H, MEM_FAST | MEM_NOPANIC);
    uint32_t *band = rg_alloc(W * 2 * BAND * 2 * 2, MEM_FAST | MEM_NOPANIC);
    rg_audio_frame_t *audio = rg_alloc(AUDIO_CHUNK * sizeof(rg_audio_frame_t), MEM_FAST | MEM_NOPANIC);
    float *delay = rg_alloc(SPLASH_ECHO_LEN * sizeof(float), MEM_SLOW | MEM_NOPANIC);
    bool audio_running = false;
    if (!canvas || !band || !audio || !delay || !get_title_mask())
    {
        RG_LOGE("splash: out of memory");
        goto done;
    }

    // Two identical pixels per word, already in panel byte order (written with NOSWAP)
    uint32_t pal2[16];
    for (int i = 0; i < 16; i++)
    {
        uint32_t c = splash_palette[i];
        uint16_t px = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
        px = (px >> 8) | (px << 8);
        pal2[i] = px | ((uint32_t)px << 16);
    }

    splash_audio_init(delay);
    audio_stop = audio_done = false;
    audio_running = rg_task_create("splash_snd", &audio_task, audio, 3 * 1024, RG_TASK_PRIORITY_3, 1) != NULL;
    audio_done = !audio_running;

    uint32_t held = rg_input_read_gamepad();
    int64_t start = rg_system_timer(), t_render = 0, t_lcd = 0;
    int drawn = 0, last = -1;

    while (1)
    {
        if (rg_input_read_gamepad() & ~held)
            break; // any newly pressed button skips

        // The frame is picked by the clock: a slow frame makes the next one skip ahead, never stretches time
        int64_t elapsed = rg_system_timer() - start;
        int f = (int)(elapsed * SPLASH_FPS / 1000000);
        if (f >= SPLASH_FRAMES)
            break;
        if (f == last)
        {
            rg_usleep((int64_t)(f + 1) * 1000000 / SPLASH_FPS - elapsed);
            continue;
        }
        last = f;

        int64_t t = rg_system_timer();
        splash_render(canvas, f);
        t_render += rg_system_timer() - t;

        for (int y0 = 0; y0 < H; y0 += BAND)
        {
            t = rg_system_timer();
            for (int y = 0; y < BAND; y++)
            {
                const uint8_t *src = canvas + (y0 + y) * W;
                uint32_t *line = band + y * 2 * W;
                for (int x = 0; x < W; x++)
                    line[x] = pal2[src[x]];
                memcpy(line + W, line, W * 4);
            }
            t_render += rg_system_timer() - t, t = rg_system_timer();
            rg_display_write_rect(0, y0 * 2, W * 2, BAND * 2, W * 4, (uint16_t *)band, RG_DISPLAY_WRITE_NOSWAP);
            t_lcd += rg_system_timer() - t;
        }
        drawn++;
    }

    if (drawn)
        RG_LOGI("splash: %d of %d frames drawn in %d ms, per frame: render %d us, lcd %d us\n", drawn, SPLASH_FRAMES,
                (int)((rg_system_timer() - start) / 1000), (int)(t_render / drawn), (int)(t_lcd / drawn));

done:
    audio_stop = true;
    for (int i = 0; i < 200 && !audio_done; i++)
        rg_task_delay(10); // the task owns `audio` until it exits
    if (audio_done)
    {
        free(audio);
        free(delay);
    }
    free(canvas);
    free(band);
    free(title_mask);
    title_mask = NULL;
    rg_display_clear(C_BLACK);
}
#endif
