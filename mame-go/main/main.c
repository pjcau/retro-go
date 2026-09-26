/*
 * mame-go: MAME 0.37b5 (mame2000-libretro) on retro-go.
 *
 * The core keeps its own libretro frontend (src/libretro/), which already
 * implements every osd_* function MAME needs. This file is the libretro
 * *host*: it answers the environment calls and turns the video, audio and
 * input callbacks into rg_display / rg_audio / rg_input.
 */
#include <rg_system.h>
#include <string.h>
#include <stdlib.h>

#include "libretro.h"

void hs_close(void); /* hiscore.c */

/* Same rate as the other apps: the PDM driver derives its DAC-mode clocks
 * from sample_rate / 100, and 22050 made mame-go far louder than the volume
 * setting allowed. */
#define AUDIO_SAMPLE_RATE 32000
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
            var->value = "32000"; /* AUDIO_SAMPLE_RATE */
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

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) /* duplicate frame */
        return;
    if (!updates[0] || updates[0]->width != (int)width || updates[0]->height != (int)height)
    {
        for (int i = 0; i < 2; i++)
        {
            rg_surface_free(updates[i]);
            updates[i] = rg_surface_create(width, height, RG_PIXEL_565_LE, MEM_FAST);
        }
    }
    rg_surface_t *surface = updates[current];
    for (unsigned y = 0; y < height; y++)
        memcpy((uint8_t *)surface->data + y * surface->stride, (const uint8_t *)data + y * pitch, width * 2);
    rg_display_submit(surface, 0);
    current ^= 1;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    rg_audio_submit((const rg_audio_frame_t *)data, frames);
    return frames;
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
    return rg_surface_save_image_file(updates[current ^ 1], filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    size_t size = retro_serialize_size();
    void *buf = size ? malloc(size) : NULL;
    bool ok = buf && retro_serialize(buf, size);
    if (ok)
    {
        FILE *fp = fopen(filename, "wb");
        ok = fp && fwrite(buf, 1, size, fp) == size;
        if (fp)
            fclose(fp);
    }
    free(buf);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    size_t size = retro_serialize_size();
    void *buf = size ? malloc(size) : NULL;
    FILE *fp = fopen(filename, "rb");
    bool ok = buf && fp && fread(buf, 1, size, fp) == size && fgetc(fp) == EOF && retro_unserialize(buf, size);
    if (fp)
        fclose(fp);
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
    if (rg_settings_get_number(NS_APP, "DispScaling", -1) == -1)
        rg_display_set_scaling(RG_DISPLAY_SCALING_OFF);
    if (rg_display_get_scaling() == RG_DISPLAY_SCALING_OFF &&
        (av.geometry.base_width > rg_display_get_width() || av.geometry.base_height > rg_display_get_height()))
        rg_display_set_scaling(RG_DISPLAY_SCALING_FIT); /* would be cropped */

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        retro_run(); /* the machine is fully up only after its first frame */
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
        if (audio_buffer_status)
            audio_buffer_status(true, 50, late > frame_us);

        joystick = rg_input_read_gamepad();
        if (joystick & RG_KEY_MENU)
            rg_gui_game_menu();
        else if (joystick & RG_KEY_OPTION)
            rg_gui_options_menu();

        int64_t start = rg_system_timer();
        retro_run(); /* audio_batch_cb blocks on the I2S DMA, which paces the loop */
        rg_system_tick(rg_system_timer() - start);
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
