/*
 * mame-go: MAME 0.37b5 (mame2000-libretro) on retro-go.
 *
 * The core keeps its own libretro frontend (src/libretro/), which already
 * implements every osd_* function MAME needs. This file is the libretro
 * *host*: it answers the environment calls and turns the video, audio and
 * input callbacks into rg_display / rg_audio / rg_input.
 */
#include <rg_system.h>
#include <esp_partition.h>
#include <string.h>
#include <stdlib.h>

#include "libretro.h"

void hs_close(void); /* hiscore.c */

/* Same rate as the other apps: the PDM driver derives its DAC-mode clocks
 * from sample_rate / 100, and 22050 made mame-go far louder than the volume
 * setting allowed. */
#define AUDIO_SAMPLE_RATE 32000
/* `AUDIO_MIX_HZ=16000`: the emulated chips render at half the rate and the
 * samples are doubled on the way out, so the speaker still gets 32000 Hz (the
 * rule above; DOOM, Duke Nukem 3D and the Neo Geo Pocket do the same). The
 * sound board's cost follows the rate the chips render at. Only 32000 (as is)
 * and 16000 (doubled) are supported. */
#ifndef AUDIO_MIX_HZ
#define AUDIO_MIX_HZ 32000
#endif
#if AUDIO_MIX_HZ != 32000 && AUDIO_MIX_HZ != 16000
#error "AUDIO_MIX_HZ must be 32000 or 16000"
#endif
#define AUDIO_STR_(x) #x
#define AUDIO_STR(x) AUDIO_STR_(x)
#define SYSTEM_DIR RG_STORAGE_ROOT "/retro-go/mame"
#define SAVE_DIR RG_BASE_PATH_SAVES "/arcade"

static rg_app_t *app;
static rg_surface_t *updates[2];
static int current;
static uint32_t joystick;
static retro_audio_buffer_status_callback_t audio_buffer_status;

/* ------------------------------------------------------------------ libretro callbacks */

static bool environment_cb(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = SYSTEM_DIR;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = SAVE_DIR;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        struct retro_variable *var = data;
        if (strcmp(var->key, "mame2000-sample_rate") == 0)
            var->value = AUDIO_STR(AUDIO_MIX_HZ);
        else if (strcmp(var->key, "mame2000-frameskip") == 0)
            var->value = "auto"; /* driven by audio_buffer_status, see mame_task() */
        else if (strcmp(var->key, "mame2000-frameskip_interval") == 0)
            var->value = "2"; /* at most 2 skipped frames in a row */
        else
            return false;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
    {
        const struct retro_audio_buffer_status_callback *cb = data;
        audio_buffer_status = cb ? cb->callback : NULL;
        return true;
    }
    /* RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER is refused on purpose:
     * the core's blitter only redraws dirty blocks, so it needs one persistent
     * buffer. Handing it our two alternating surfaces made them diverge (flicker). */
    default:
        return false;
    }
}

#ifdef MAMEPROF
/* `MAMEPROF=1`: sampling profiler, core 0's interrupted PC at every FreeRTOS
 * tick (1 kHz), in 64-byte slices; "MAMESAMPLE pc count %" lines (the top 100) once, after 1800 frames
 * (symbols: xtensa-esp32s3-elf-addr2line -e build/mame-go.elf). The port keeps
 * the task's exception frame pointer in its TCB: word 1 is the PC. */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#define SAMP_N 4096
static struct { uint32_t pc, n; } *samp;
static volatile bool samp_on;
static uint32_t samp_total;
static uint32_t samp_core;              /* MAMEPROF_CORE=1 builds sample core 1 */
static void IRAM_ATTR samp_tick(void)
{
    if (!samp_on) return;
    TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(samp_core);
    if (!t) return;
    uint32_t *f = *(uint32_t **)t, pc = f[1];
    /* in the chip's ROM (no symbols): count the caller instead (a0, windowed:
       the top 2 bits are the call size, the rest the return address) */
    if (pc < 0x40060000u || (pc & ~63u) == 0x4037e440u)   /* ROM; the wait seen in IRAM */
        pc = (f[3] & 0x3FFFFFFFu) | 0x40000000u;
    pc &= ~63u;
    uint32_t h = (pc >> 6) & (SAMP_N - 1);   /* 64-byte slices */
    samp_total++;
    for (int i = 0; i < 16; i++, h = (h + 1) & (SAMP_N - 1))
        if (samp[h].pc == pc || samp[h].n == 0) { samp[h].pc = pc; samp[h].n++; return; }
}
static void samp_start(void)
{
    samp = heap_caps_calloc(SAMP_N, sizeof(*samp), MALLOC_CAP_SPIRAM);
#ifdef MAMEPROF_CORE1
    samp_core = 1;
#endif
    if (samp && esp_register_freertos_tick_hook_for_cpu(samp_tick, samp_core) == ESP_OK) samp_on = true;
}
static void samp_dump(void)
{
    samp_on = false;
    for (int k = 0; k < 100; k++)
    {
        int best = -1;
        for (int i = 0; i < SAMP_N; i++)
            if (samp[i].n && (best < 0 || samp[i].n > samp[best].n)) best = i;
        if (best < 0) break;
        printf("MAMESAMPLE %08x %u %.2f\n", (unsigned)samp[best].pc, (unsigned)samp[best].n, 100.0 * samp[best].n / samp_total);
        samp[best].n = 0;
    }
    printf("MAMESAMPLE total %u\n", (unsigned)samp_total);
}
#endif

#ifdef MAMEBENCH
/* `MAMEBENCH=1`: deterministic benchmark. From the resumed state the input
 * comes from a frame-numbered script, every frame is drawn (no auto frameskip),
 * and every 300 frames a "MAMEBENCH" console line gives the hash of the frame:
 * the same build must print the same hashes, and an interpreter change or the
 * dynarec must not change them. With NEOPROF=1 the ms per part come alongside. */
static uint32_t bench_frame, bench_hash;
static bool bench_hash_next;

/* MAMEBENCH=2: a coin and START before the script, so that it plays the first
 * mission instead of watching the attract loop (a save state cannot be used: it
 * only loads in the firmware that wrote it). Same numbers as tools/neoframes.c
 * "--input play". */
#define BENCH_COIN_AT  600
#define BENCH_START_AT 720
#define BENCH_PLAY_AT  900

static uint32_t bench_input(uint32_t f)
{
#if MAMEBENCH >= 2
    if (f < BENCH_PLAY_AT)
    {
        if (f >= BENCH_COIN_AT && f < BENCH_COIN_AT + 6) return RG_KEY_SELECT;      /* coin */
        if (f >= BENCH_START_AT && f < BENCH_START_AT + 6) return RG_KEY_START;
        return 0;
    }
    f -= BENCH_PLAY_AT;
#endif
    uint32_t k = RG_KEY_RIGHT;                           /* walk right, fire and jump now and then */
    if (f % 20 < 2) k |= RG_KEY_A;
    if (f % 90 < 3) k |= RG_KEY_B;
    if (f % 600 >= 300 && f % 600 < 360) k = RG_KEY_LEFT | (f % 20 < 2 ? RG_KEY_A : 0);
    return k;
}
#endif

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) /* duplicate frame */
        return;
#ifdef MAMEBENCH
    if (bench_hash_next)
    {
        uint32_t h = 2166136261u;
        for (unsigned y = 0; y < height; y++)
        {
            const uint16_t *p = (const uint16_t *)((const uint8_t *)data + y * pitch);
            for (unsigned x = 0; x < width; x++)
                h = (h ^ p[x]) * 16777619u;
        }
        bench_hash = h;
        bench_hash_next = false;
    }
#endif
    static bool single;
    static unsigned failed_w, failed_h;   /* no room for a surface of this size */
    if ((!updates[0] && (width != failed_w || height != failed_h))
        || (updates[0] && (updates[0]->width != (int)width || updates[0]->height != (int)height)))
    {
        if (updates[1] != updates[0])
            rg_surface_free(updates[1]);
        rg_surface_free(updates[0]);
        updates[0] = updates[1] = NULL;
        for (int i = 0; i < 2; i++)
            /* internal RAM for the small 8-bit boards; a 384x224 CPS1 frame
               (172 KB, twice) does not fit there */
            updates[i] = rg_surface_create(width, height, RG_PIXEL_565_LE, width * height * 2 <= 128 * 1024 ? MEM_FAST : MEM_ANY);
        /* no room for the second one (CPS1 with 16-bit graphics): one surface,
           the display finishes with it before the next frame is copied in */
        single = !updates[1];
        failed_w = updates[0] ? 0 : width;
        failed_h = updates[0] ? 0 : height;
        if (single)
            updates[1] = updates[0];
    }
    if (!updates[0])
    {
        /* not even one (Street Fighter II: 2 MB of tiles in PSRAM): the
           display reads the core's own frame buffer, and is waited for
           before the core may draw the next frame into it */
        static rg_surface_t *direct;
        if (!direct && !(direct = rg_surface_create(0, 0, RG_PIXEL_565_LE, 0)))
            return;
        direct->width = width;
        direct->height = height;
        direct->stride = pitch;
        direct->data = (void *)data;
        rg_display_submit(direct, 0);
        rg_display_sync(true);
        return;
    }
    if (single)
        rg_display_sync(true);
    rg_surface_t *surface = updates[current];
    for (unsigned y = 0; y < height; y++)
        memcpy((uint8_t *)surface->data + y * surface->stride, (const uint8_t *)data + y * pitch, width * 2);
    rg_display_submit(surface, 0);
    current ^= 1;
}

/* Neo Geo frames converted on the second core (mame2000 libretro/video.c,
 * mamego_present_indexed): the core hands over its pen bitmap and the pen
 * -> RGB565 table instead of blitting + copying on core 0 (Metal Slug 2:
 * 3.6 ms per frame). The table is copied here, the bitmap is left alone by
 * MAME until the next call, which first waits for this conversion. */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
static struct
{
    const void *pix;
    int bits, width, height, pitch;
    uint16_t *pal;
    int pal_size;
    void (*render)(void);
} present;
static TaskHandle_t present_task;
static SemaphoreHandle_t present_done;
static volatile bool present_busy;

static void present_task_main(void *arg)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef NEOPROF
        extern volatile int64_t mamego_core1_us[3];
        int64_t t0 = esp_timer_get_time();
#endif
        if (present.render) /* Neo Geo sprites of this frame (vidhrdw/neogeo.c) */
        {
            present.render();
#ifdef NEOPROF
            extern volatile int mamego_render_count[3];
            mamego_render_count[1]++;
#endif
        }
#ifdef NEOPROF
        int64_t t1 = esp_timer_get_time();
        mamego_core1_us[0] += t1 - t0;
#endif
        rg_surface_t *surface = updates[current];
        const uint16_t *pal = present.pal;
        for (int y = 0; y < present.height; y++)
        {
            uint16_t *dst = (uint16_t *)((uint8_t *)surface->data + y * surface->stride);
            if (present.bits == 16)
            {
                const uint16_t *src = (const uint16_t *)present.pix + y * present.pitch;
                for (int x = 0; x < present.width; x++)
                    dst[x] = pal[src[x]];
            }
            else
            {
                const uint8_t *src = (const uint8_t *)present.pix + y * present.pitch;
                for (int x = 0; x < present.width; x++)
                    dst[x] = pal[src[x]];
            }
        }
        rg_display_submit(surface, 0);
#ifdef NEOPROF
        mamego_core1_us[1] += esp_timer_get_time() - t1;
#endif
        current ^= 1;
        present_busy = false;
        xSemaphoreGive(present_done);
    }
}

#ifdef NEOPROF
/* core 1 idle time: the idle hook runs back to back while core 1 has nothing
   to do; gaps longer than 50 us mean a task ran in between */
#include <esp_freertos_hooks.h>
static volatile int64_t core1_idle_us, core1_idle_last;
static bool core1_idle_hook(void)
{
    int64_t t = esp_timer_get_time();
    if (core1_idle_last && t - core1_idle_last < 50)
        core1_idle_us += t - core1_idle_last;
    core1_idle_last = t;
    return false; /* keep spinning, so the next call comes right away */
}
int64_t mamego_core1_idle_us(void)
{
    static bool registered;
    if (!registered)
        registered = esp_register_freertos_idle_hook_for_cpu(core1_idle_hook, 1) == ESP_OK;
    return core1_idle_us;
}
int64_t mamego_display_busy_us(void)
{
    return rg_display_get_counters().busyTime;
}
int64_t mamego_display_dmawait_us(void)
{
    return rg_display_get_counters().dmaWaitTime;
}
int64_t mamego_display_sends(void)
{
    return rg_display_get_counters().sendCount;
}
#endif

/* the conversion task is still needed when the driver left the sprites of the
   frame to core 1 (mamego_frame_render, builds without NEO_NO_DEFER) */
static bool present_task_needed(void)
{
    extern void (*mamego_frame_render)(void);
    return mamego_frame_render != NULL;
}

#if NEOBAND >= 2
/* V2 of the Arcade 60 fps plan: the Neo Geo renderer (vidhrdw/neogeo_band.c)
 * draws the frame in 16-line bands in two internal-RAM buffers and hands each
 * finished band to the display task (rg_display_submit_band), which scales and
 * sends it while the next band is drawn. The PSRAM frame bitmap is no longer
 * written or read. mamego_band_wait(idx) blocks until buffer idx is free. */
#ifndef NB_LINES
#define NB_LINES 8                      /* as vidhrdw/neogeo_band.c */
#endif
static rg_surface_t band_frame;        /* format, palette, size of the frame the bands belong to */
/* Band buffers: BAND_INTERNAL in internal RAM (the fast path), BAND_PSRAM in
 * PSRAM for the moments the emulator outruns the display (its cheap bands,
 * sky and flat ground, come faster than the LCD bus takes them): a band drawn
 * in PSRAM costs that band V1's traffic, but the emulator never waits. */
#ifndef BAND_INTERNAL
#define BAND_INTERNAL (NB_LINES >= 16 ? 2 : 4)   /* the same 11 KB of internal RAM either way */
#endif
#define BAND_PSRAM 32
#define BAND_SLOTS (BAND_INTERNAL + BAND_PSRAM)
static uint8_t *band_buf[BAND_SLOTS];
static rg_band_t bands[BAND_SLOTS];
static QueueHandle_t band_free_int, band_free_ps;   /* free slot indices */
static bool bands_presented;           /* this frame went out as bands: present_indexed() has nothing to do */

static void band_done(void *arg)
{
    int idx = (int)(intptr_t)arg;
    xQueueSend(idx < BAND_INTERNAL ? band_free_int : band_free_ps, &idx, portMAX_DELAY);
}

/* the renderer's band size is known at its first frame */
bool mamego_band_setup(size_t bytes)
{
    if (band_free_int)
        return true;
    band_free_int = xQueueCreate(BAND_INTERNAL, sizeof(int));
    band_free_ps = xQueueCreate(BAND_PSRAM, sizeof(int));
    band_frame.palette = malloc(256 * sizeof(uint16_t));
    int n_int = 0, n_ps = 0;
    for (int i = 0; i < BAND_SLOTS; i++)
    {
        band_buf[i] = heap_caps_malloc(bytes, i < BAND_INTERNAL ? MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT : MALLOC_CAP_SPIRAM);
        if (!band_buf[i])
            continue;
        memset(band_buf[i], 0, bytes);
        xQueueSend(i < BAND_INTERNAL ? band_free_int : band_free_ps, &i, 0);
        if (i < BAND_INTERNAL) n_int++; else n_ps++;
    }
    RG_LOGI("band buffers: %d internal, %d PSRAM, %u bytes each", n_int, n_ps, (unsigned)bytes);
    return n_int > 0 && band_frame.palette;
}

/* a free band buffer: internal when there is one, else a PSRAM slot, else the
 * next internal one; *psram says which it was */
extern volatile int rg_display_band_starving;
void *mamego_band_acquire(int *idx, int *psram)
{
    *psram = 0;
    if (xQueueReceive(band_free_int, idx, 0) == pdTRUE)
        return band_buf[*idx];
    /* short of internal buffers: tell the display, which moves the oldest unsent
     * internal band to its PSRAM stage and gives the buffer back (one block's
     * time, ~0.2 ms); a PSRAM slot only when that does not come in time */
    rg_display_band_starving = 1;
    if (xQueueReceive(band_free_int, idx, 1) == pdTRUE)
        return band_buf[*idx];
    if (xQueueReceive(band_free_ps, idx, 0) == pdTRUE)
    {
        *psram = 1;
        return band_buf[*idx];
    }
    xQueueReceive(band_free_int, idx, portMAX_DELAY);
    return band_buf[*idx];
}

void mamego_band_present(int idx, const void *rows, int first, int count, int width, int height,
                         int pitch, const uint16_t *pal)
{
    if (first == 0)
    {
        for (int i = 0; i < 256; i++)
            band_frame.palette[i] = (uint16_t)((pal[i] << 8) | (pal[i] >> 8));
        band_frame.format = RG_PIXEL_PAL565_BE;
        band_frame.width = width;
        band_frame.height = height;
        band_frame.stride = pitch;
        band_frame.offset = 0;
        band_frame.data = (void *)rows;  /* not read: the bands carry the pixels */
    }
#ifdef MAMEBENCH
    /* the same bytes in the same order as the whole-frame hash of present_indexed() */
    static uint32_t h;
    if (bench_hash_next)
    {
        if (first == 0)
            h = 2166136261u;
        for (int y = 0; y < count; y++)
        {
            const uint8_t *p = (const uint8_t *)rows + y * pitch;
            for (int x = 0; x < width; x++)
                h = (h ^ p[x]) * 16777619u;
        }
        if (first + count >= height)
        {
            bench_hash = h;
            bench_hash_next = false;
        }
    }
#endif
    bands[idx].frame = &band_frame;
    bands[idx].rows = rows;
    bands[idx].first = first;
    bands[idx].count = count;
    bands[idx].done = band_done;
    bands[idx].arg = (void *)(intptr_t)idx;
    bands_presented = true;
    rg_display_submit_band(&bands[idx]);
}
#endif

static int present_indexed(const void *pix, int bits, int width, int height, int pitch, const void *palette, int colors)
{
    static bool no_memory;
#if NEOBAND >= 2
    if (bands_presented && pix)          /* the frame is already on its way, band by band */
    {
        bands_presented = false;
        return 1;
    }
#endif
    if (present_busy)
    {
#ifdef NEOPROF
        extern volatile int64_t mamego_wait_us[2];
        int64_t t0 = rg_system_timer();
        xSemaphoreTake(present_done, portMAX_DELAY);
        mamego_wait_us[1] += rg_system_timer() - t0;
#else
        xSemaphoreTake(present_done, portMAX_DELAY);
#endif
    }
    if (!pix) /* the core only waits for the previous frame */
        return 1;
#ifdef MAMEBENCH
    if (bench_hash_next)                    /* the core's pen bitmap (Neo Geo, CPS1) */
    {
        uint32_t h = 2166136261u;
        for (int y = 0; y < height; y++)
        {
            const uint8_t *p = (const uint8_t *)pix + y * pitch * (bits / 8);
            for (int x = 0; x < width * (bits / 8); x++)
                h = (h ^ p[x]) * 16777619u;
        }
        bench_hash = h;
        bench_hash_next = false;
    }
#endif
    if (bits == 8 && !present_task_needed())
    {
        /* 8-bit pens: the display reads MAME's bitmap itself (a palette
         * surface, no copy) and looks the colours up while it scales. The
         * conversion on core 1 (21 % of it in Metal Slug) and a 16-bit
         * write + read of the frame in PSRAM are gone. MAME draws its next
         * frame into the other bitmap, the one submitted last time: the
         * display must be done with it first (core 0 takes longer per frame
         * than the display, so this does not wait in practice). */
        static rg_surface_t *ds[2];
        static int dcur;
        if (!ds[0] && (!(ds[0] = rg_surface_create(0, 0, RG_PIXEL_PAL565_BE, 0)) ||
                       !(ds[1] = rg_surface_create(0, 0, RG_PIXEL_PAL565_BE, 0))))
            return 0;
        rg_display_sync(true);
        rg_surface_t *d = ds[dcur];
        const uint16_t *src_pal = palette;
        for (int i = 0; i < 256 && i < colors; i++)
            d->palette[i] = (uint16_t)((src_pal[i] << 8) | (src_pal[i] >> 8));
        d->data = (void *)pix;
        d->width = width;
        d->height = height;
        d->stride = pitch;
        d->offset = 0;
        rg_display_submit(d, 0);
        dcur ^= 1;
        return 1;
    }
    if (no_memory) /* declined once for memory: the core's own path from now on */
        return 0;
    if (!present_task)
    {
        present_done = xSemaphoreCreateBinary();
        if (xTaskCreatePinnedToCore(present_task_main, "mame_present", 6144, NULL, 5, &present_task, 1) != pdPASS)
            return 0;
    }
    if (colors > present.pal_size)
    {
        free(present.pal);
        present.pal = malloc(colors * sizeof(uint16_t));
        present.pal_size = present.pal ? colors : 0;
        if (!present.pal)
            return 0;
    }
    if (!updates[0] || updates[0]->width != width || updates[0]->height != height)
    {
        /* PSRAM: with the YM2610 and present tasks on core 1 there is no
           room for 2 x 136 KB of internal RAM (the core 1 task writes it) */
        if (updates[1] != updates[0])
            rg_surface_free(updates[1]);
        rg_surface_free(updates[0]);
        updates[0] = updates[1] = NULL;
        for (int i = 0; i < 2; i++)
            updates[i] = rg_surface_create(width, height, RG_PIXEL_565_LE, MEM_SLOW);
        if (!updates[0] || !updates[1])
        {
            /* no room for two frames (SF2): stay on the core's path */
            rg_surface_free(updates[0]);
            rg_surface_free(updates[1]);
            updates[0] = updates[1] = NULL;
            no_memory = true;
            return 0;
        }
    }
    if (bits == 16)
        for (int i = 0; i < colors; i++)
            present.pal[i] = (uint16_t)((const uint32_t *)palette)[i];
    else
        memcpy(present.pal, palette, colors * sizeof(uint16_t));
    {
        extern void (*mamego_frame_render)(void);
        present.render = mamego_frame_render; /* drawn on this core before converting */
        mamego_frame_render = NULL;
    }
    present.pix = pix;
    present.bits = bits;
    present.width = width;
    present.height = height;
    present.pitch = pitch;
    present_busy = true;
    xTaskNotifyGive(present_task);
    return 1;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
#if AUDIO_MIX_HZ == 16000
    /* 16 kHz from the core, 32 kHz to the speaker: every sample, then the
       midpoint to the next one (the last sample of the previous call is kept
       so the line is unbroken across calls) */
    static rg_audio_frame_t *out;        /* 2.2 KB, in PSRAM: the internal RAM has none to spare */
    static int16_t prev_l, prev_r;
    const rg_audio_frame_t *in = (const rg_audio_frame_t *)data;
    if (!out && !(out = heap_caps_malloc(2 * 280 * sizeof(*out), MALLOC_CAP_SPIRAM)))
        return frames;                   /* no buffer: silence rather than a crash */
    while (frames)
    {
        size_t n = frames > 280 ? 280 : frames;
        for (size_t i = 0; i < n; i++)
        {
            out[2 * i].left = (int16_t)(((int)prev_l + in[i].left) >> 1);
            out[2 * i].right = (int16_t)(((int)prev_r + in[i].right) >> 1);
            out[2 * i + 1] = in[i];
            prev_l = in[i].left;
            prev_r = in[i].right;
        }
        rg_audio_submit(out, n * 2);
        in += n;
        frames -= n;
    }
    return (size_t)(in - (const rg_audio_frame_t *)data);
#else
    rg_audio_submit((const rg_audio_frame_t *)data, frames);
    return frames;
#endif
}

static void audio_cb(int16_t left, int16_t right)
{
    rg_audio_frame_t frame = {left, right};
    rg_audio_submit(&frame, 1);
}

static void input_poll_cb(void)
{
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;

    static const struct { uint32_t rg; unsigned retro; } map[] = {
        {RG_KEY_UP, RETRO_DEVICE_ID_JOYPAD_UP},       {RG_KEY_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
        {RG_KEY_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},   {RG_KEY_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
        {RG_KEY_A, RETRO_DEVICE_ID_JOYPAD_A},         {RG_KEY_B, RETRO_DEVICE_ID_JOYPAD_B},
        {RG_KEY_X, RETRO_DEVICE_ID_JOYPAD_X},         {RG_KEY_Y, RETRO_DEVICE_ID_JOYPAD_Y},
        {RG_KEY_L, RETRO_DEVICE_ID_JOYPAD_L},         {RG_KEY_R, RETRO_DEVICE_ID_JOYPAD_R},
        {RG_KEY_START, RETRO_DEVICE_ID_JOYPAD_START}, {RG_KEY_SELECT, RETRO_DEVICE_ID_JOYPAD_SELECT}, /* coin */
    };

    int16_t bits = 0;
    for (size_t i = 0; i < RG_COUNT(map); i++)
        if (joystick & map[i].rg)
            bits |= 1 << map[i].retro;

    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return bits;
    return (bits >> id) & 1;
}

/* ------------------------------------------------------------------ retro-go handlers */

static bool screenshot_handler(const char *filename, int width, int height)
{
    const rg_surface_t *s = updates[current ^ 1];
    if (!s)
        return false;
    size_t n = strlen(filename);
    if (n > 4 && !strcmp(filename + n - 4, ".raw"))
    {
        /* bench (console `shot`): the frame as is, RGB565 with a width/height
           header, no PNG encoder (it needs memory the big games do not leave) */
        FILE *fp = fopen(filename, "wb");
        if (!fp)
            return false;
        uint16_t wh[2] = {(uint16_t)s->width, (uint16_t)s->height};
        fwrite(wh, 2, 2, fp);
        for (int y = 0; y < s->height; y++)
            fwrite((const uint8_t *)s->data + s->offset + y * s->stride, 2, s->width, fp);
        fclose(fp);
        return true;
    }
    return rg_surface_save_image_file(s, filename, width, height);
}

/* Big read-only ROM regions (gfx, sound samples) live in the "mamerom" flash
 * partition, memory-mapped, instead of PSRAM (common.c mamego_regions_to_flash).
 * A region already there from the last run of the game is only mapped; the
 * flash is rewritten only when its content differs. */
unsigned char *mamego_flash_store(const unsigned char *data, size_t len, size_t *offset)
{
    static const esp_partition_t *part;
    static const uint8_t *map;
    static esp_partition_mmap_handle_t handle;
    size_t off = *offset, size = (len + 0xFFF) & ~(size_t)0xFFF; /* erase sectors */

    if (!part)
    {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "mamerom");
        if (!part || esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, (const void **)&map, &handle) != ESP_OK)
        {
            RG_LOGW("mamerom partition unavailable, ROMs stay in PSRAM");
            part = NULL;
            return NULL;
        }
    }
    if (off + size > part->size)
        return NULL;
    if (memcmp(map + off, data, len) != 0)
    {
        RG_LOGI("mamerom: writing %u bytes at 0x%x", (unsigned)len, (unsigned)off);
        if (esp_partition_erase_range(part, off, size) != ESP_OK || esp_partition_write(part, off, data, len) != ESP_OK ||
            memcmp(map + off, data, len) != 0)
        {
            RG_LOGE("mamerom: write failed, region stays in PSRAM");
            return NULL;
        }
    }
    *offset = off + size;
    return (unsigned char *)map + off;
}

/* State buffer: the heap, or on a Neo Geo game (PSRAM nearly all given to
   the sprite page cache) the page cache itself, borrowed between frames. */
static int present_indexed(const void *pix, int bits, int width, int height, int pitch, const void *palette, int colors);

static void *state_buffer(size_t size, bool *borrowed)
{
    extern void *neospr_borrow(size_t size);
    if (present_task)
        present_indexed(NULL, 0, 0, 0, 0, NULL, 0); /* core 1 done with the frame and the page cache */
    void *buf = size ? malloc(size) : NULL;
    *borrowed = false;
    if (!buf && size && (buf = neospr_borrow(size)))
        *borrowed = true;
    return buf;
}

size_t mamego_psram_free(size_t *largest)
{
    *largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void mamego_mem_report(const char *where)
{
    printf("mem %s: internal %u KB (largest %u), PSRAM %u KB (largest %u)\n", where,
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
}

static bool save_state_handler(const char *filename)
{
    size_t size = retro_serialize_size();
    bool borrowed;
    void *buf = state_buffer(size, &borrowed);
    bool ok = buf && retro_serialize(buf, size);
    if (ok)
    {
        FILE *fp = fopen(filename, "wb");
        ok = fp && fwrite(buf, 1, size, fp) == size;
        if (fp)
            fclose(fp);
    }
    if (!borrowed)
        free(buf);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    size_t size = retro_serialize_size();
    bool borrowed;
    void *buf = state_buffer(size, &borrowed);
    FILE *fp = fopen(filename, "rb");
    size_t got = 0;
    bool ok = false;
    const char *why = "no buffer";
    if (buf && !fp) why = "no file";
    else if (buf && fp)
    {
        got = fread(buf, 1, size, fp);
        if (got != size) why = "short file";
        else if (fgetc(fp) != EOF) why = "file larger than the state";
        else if (!retro_unserialize(buf, size)) why = "refused by the core";
        else { ok = true; why = "ok"; }
    }
    if (!ok)
    {
        long len = fp ? (fseek(fp, 0, SEEK_END), ftell(fp)) : -1;
        RG_LOGW("state %s: %s (state %u bytes, file %ld, read %u)", filename, why, (unsigned)size, len, (unsigned)got);
    }
    if (fp)
        fclose(fp);
    if (!borrowed)
        free(buf);
    return ok;
}

static bool reset_handler(bool hard)
{
    retro_reset();
    return true;
}

static void event_handler(int event, void *arg)
{
    /* Records (.hi) are written when the machine stops, which never happens
     * on retro-go: the app is simply rebooted into the launcher. */
    if (event == RG_EVENT_SHUTDOWN)
        hs_close();
    if (event == RG_EVENT_REDRAW && updates[current ^ 1])
        rg_display_submit(updates[current ^ 1], 0);
}

/* MAME needs far more than the 8 KB main task stack (ROM loading, drivers),
 * so the core runs on its own task, like duke3d-go. */
static void mame_task(void *arg)
{
    retro_set_environment(environment_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    struct retro_game_info game = {app->romPath, NULL, 0, NULL};
    {
        extern int (*mamego_present_indexed)(const void *, int, int, int, int, const void *, int);
        mamego_present_indexed = present_indexed;
    }
    if (!retro_load_game(&game))
        RG_PANIC("This game is not supported, or its ROM set is incomplete");

    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    RG_LOGI("mame-go: %ux%u @ %.2f fps, audio %.0f Hz", av.geometry.base_width, av.geometry.base_height,
            av.timing.fps, av.timing.sample_rate);
    rg_system_set_tick_rate(av.timing.fps);

    /* Default to 1:1: these boards are at most 224-256 lines and a non-integer
     * upscale (Pac-Man 288 -> 320 lines) doubles every ninth line of the maze.
     * "DispScaling" is rg_display.c's per-app key; absent = never chosen. */
    bool scaling_never_chosen = rg_settings_get_number(NS_APP, "DispScaling", -1) == -1;
    /* A 2026-09-28 bench build saved "filter off" for mame-go (it looked bad
     * at the Neo Geo's 1.43x): put the default back once. */
    if (!rg_settings_get_number(NS_APP, "DispFilterFix", 0))
    {
        rg_display_set_filter(RG_DISPLAY_FILTER_BOTH);
        rg_settings_set_number(NS_APP, "DispFilterFix", 1);
    }

    if (scaling_never_chosen)
        rg_display_set_scaling(RG_DISPLAY_SCALING_OFF);
    if (rg_display_get_scaling() == RG_DISPLAY_SCALING_OFF &&
        (av.geometry.base_width > rg_display_get_width() || av.geometry.base_height > rg_display_get_height()))
        rg_display_set_scaling(RG_DISPLAY_SCALING_FIT); /* would be cropped */
    /* Neo Geo (304x224, /roms/neogeo/): nearly full screen, the whole height
     * with the aspect kept (434x320 on this panel). The menu can still pick
     * another mode for the session. */
    if (app->romPath && strstr(app->romPath, "/neogeo/"))
        rg_display_set_scaling(RG_DISPLAY_SCALING_FIT);
    /* Wide 16-bit boards (CPS1: 384x224) fit 1:1 but leave a third of the
     * panel empty: the whole width with the aspect kept (480x280). Only when
     * the user never chose a mode for mame-go. */
    else if (av.geometry.base_width >= 384 && scaling_never_chosen)
        rg_display_set_scaling(RG_DISPLAY_SCALING_FIT);

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        /* the machine is fully up only after its first frame, and the Neo Geo
         * and CPS1 sound boards move to core 1 a few frames in: from then on
         * their state is part of every save (4280 bytes on the Neo Geo), so a
         * state saved in play was "larger than the state" when loaded after
         * one frame and no resume ever worked. Load where saves are made. */
        for (int i = 0; i < 6; i++)
            retro_run();
        rg_emu_load_state(app->saveSlot);
    }

    /* Auto frameskip: when the game keeps up, audio_batch_cb blocks on the
     * I2S DMA and the loops average one frame period (individual loops
     * jitter, the DMA drains in chunks). The accumulated lateness says when
     * the emulation is really behind -- a whole frame -- and the core then
     * skips drawing (not emulating) the next frame. */
    const int64_t frame_us = 1000000 / av.timing.fps;
    int64_t loop_start = rg_system_timer(), late = 0;

    while (true)
    {
        int64_t now = rg_system_timer();
        late += (now - loop_start) - frame_us;
        late = RG_MAX(late, -2 * frame_us); /* ahead time cannot be banked */
        late = RG_MIN(late, 8 * frame_us);  /* forget long stalls (menus) */
        loop_start = now;
#ifdef MAMEBENCH
        if (audio_buffer_status)
#if MAMEBENCH >= 3
            audio_buffer_status(true, 50, bench_frame & 1);  /* MAMEBENCH=3: every other frame not drawn
                                                                (the fixed 1-in-2 frameskip), same script as 2 */
#else
            audio_buffer_status(true, 50, false);    /* draw every frame */
#endif
        joystick = rg_input_read_gamepad();
        if (!(joystick & (RG_KEY_MENU | RG_KEY_OPTION)))
            joystick = bench_input(bench_frame);
        bench_hash_next = bench_frame % 300 == 299;
#else
        if (audio_buffer_status)
            audio_buffer_status(true, 50, late > frame_us);

        joystick = rg_input_read_gamepad();
#endif
        if (joystick & RG_KEY_MENU)
            rg_gui_game_menu();
        else if (joystick & RG_KEY_OPTION)
            rg_gui_options_menu();

        int64_t start = rg_system_timer();
        retro_run(); /* audio_batch_cb blocks on the I2S DMA, which paces the loop */
        rg_system_tick(rg_system_timer() - start);
#ifdef MAMEBENCH
        if (++bench_frame % 300 == 0)
            printf("MAMEBENCH frames %u hash %08x\n", (unsigned)bench_frame, (unsigned)bench_hash);
#ifdef MAMEPROF
#if MAMEBENCH >= 2
        if (bench_frame == 1300) samp_start();      /* mission 1 being played */
        if (bench_frame == 2800) samp_dump();
#else
        if (bench_frame == 300) samp_start();       /* after the warm-up */
        if (bench_frame == 1800) samp_dump();
#endif
#endif
#endif
    }
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);
    rg_storage_mkdir(SYSTEM_DIR);
    rg_storage_mkdir(SAVE_DIR);

    rg_task_create("mame", &mame_task, NULL, 32 * 1024, RG_TASK_PRIORITY_5, 0);
    while (1)
        rg_task_delay(1000);
}
