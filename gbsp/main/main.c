#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>

#include "../components/gbsp-libretro/common.h"
#include "../components/gbsp-libretro/memmap.h"
#include "../components/gbsp-libretro/sound.h"
#include "../components/gbsp-libretro/gba_memory.h"
#include "../components/gbsp-libretro/gba_cc_lut.h"

#define AUDIO_SAMPLE_RATE (GBA_SOUND_FREQUENCY)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
boot_mode selected_boot_mode = boot_game;

u32 skip_next_frame = 0;
int sprite_limit = 1;

gbsp_memory_t *gbsp_memory;
#ifdef GBAPROF
extern int64_t gbaprof_render_us;
extern u32 gbaprof_instr;
u32 gbaprof_pageloads;
extern u32 gamepak_buffer_count;
#endif

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;

static const char *SETTING_SOUND_EMULATION = "sound";

void netpacket_poll_receive()
{
}

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

#ifdef GBAPROF
/* sampling profiler: core 0's interrupted PC at every FreeRTOS tick (1 kHz).
   The port stores the task's SP (its exception frame) in the TCB on ISR
   entry; XT_STK_PC is word 1 of that frame. */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#define SAMP_N 1024
static struct samp_s { uint32_t pc, n; } *samp;   /* PSRAM: internal RAM is full */
static volatile bool samp_on;
static void IRAM_ATTR samp_tick(void)
{
    if (!samp_on || !samp)
        return;
    TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(0);
    if (!t)
        return;
    uint32_t pc = (*(uint32_t **)t)[1];
    uint32_t h = (pc >> 2) & (SAMP_N - 1);
    for (int i = 0; i < 16; i++, h = (h + 1) & (SAMP_N - 1))
        if (samp[h].pc == pc || samp[h].n == 0)
        {
            samp[h].pc = pc;
            samp[h].n++;
            return;
        }
}
static void samp_dump(void)
{
    uint32_t total = 0;
    if (!samp)
        return;
    samp_on = false;
    for (int i = 0; i < SAMP_N; i++)
        total += samp[i].n;
    for (int k = 0; k < 80; k++)
    {
        int best = -1;
        for (int i = 0; i < SAMP_N; i++)
            if (samp[i].n && (best < 0 || samp[i].n > samp[best].n))
                best = i;
        if (best < 0)
            break;
        printf("GBASAMPLE %08x %u %.2f\n", (unsigned)samp[best].pc, (unsigned)samp[best].n, 100.0 * samp[best].n / total);
        samp[best].n = 0;
    }
    printf("GBASAMPLE total %u\n", (unsigned)total);
}
#endif

/* the state buffer: malloc, or the last ROM cache block when PSRAM is short */
static void *state_buffer(bool *borrowed)
{
    void *buffer = malloc(GBA_STATE_MEM_SIZE);
    *borrowed = false;
    if (!buffer && (buffer = gamepak_borrow_block()))
        *borrowed = true;
    return buffer;
}

static void state_buffer_free(void *buffer, bool borrowed)
{
    if (borrowed)
        gamepak_return_block();
    else
        free(buffer);
}

static bool save_state_handler(const char *filename)
{
    bool borrowed;
    void *buffer = state_buffer(&borrowed);
    if (!buffer)
        return false;
    gba_save_state(buffer);
    bool success = rg_storage_write_file(filename, buffer, GBA_STATE_MEM_SIZE, 0);
    state_buffer_free(buffer, borrowed);
    return success;
}

static bool load_state_handler(const char *filename)
{
    size_t buffer_len = GBA_STATE_MEM_SIZE;
    bool borrowed;
    void *buffer = state_buffer(&borrowed);
    if (!buffer)
        return false;
    bool success = rg_storage_read_file(filename, &buffer, &buffer_len, RG_FILE_USER_BUFFER)
                    && gba_load_state(buffer);
    state_buffer_free(buffer, borrowed);
    return success;
}

static bool reset_handler(bool hard)
{
    reset_gba();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        rg_display_submit(currentUpdate, 0);
    }
}

int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    // RG_LOGI("%u, %u, %u, %u", port, device, index, id);
    uint32_t joystick = rg_input_read_gamepad();
    int16_t val = 0;
    if (joystick & RG_KEY_DOWN) val |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (joystick & RG_KEY_UP) val |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (joystick & RG_KEY_LEFT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (joystick & RG_KEY_RIGHT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (joystick & RG_KEY_START) val |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (joystick & RG_KEY_SELECT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (joystick & RG_KEY_B) val |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (joystick & RG_KEY_A) val |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (joystick & RG_KEY_L) val |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (joystick & RG_KEY_R) val |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    return val;
}

void set_fastforward_override(bool fastforward)
{
}

static rg_gui_event_t sound_toggle_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        sound_master_enable = !sound_master_enable;
        rg_settings_set_number(NS_APP, SETTING_SOUND_EMULATION, sound_master_enable);
    }

    strcpy(option->value, sound_master_enable ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Audio enable"), "-", RG_DIALOG_FLAG_NORMAL, &sound_toggle_cb};
    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .options = &options_handler,
    };
    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);
    rg_system_set_tick_rate(60);
    // rg_system_set_overclock(2);

    sound_master_enable = rg_settings_get_number(NS_APP, SETTING_SOUND_EMULATION, true);

    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    updates[0]->height = GBA_SCREEN_HEIGHT;
    // updates[1] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    // updates[1]->height = GBA_SCREEN_HEIGHT;
    currentUpdate = updates[0];

    gba_screen_pixels = currentUpdate->data;

    gbsp_memory = rg_alloc(sizeof(*gbsp_memory), MEM_ANY);
    RG_LOGI("gbsp_memory=%p", gbsp_memory);

    libretro_supports_bitmasks = true;
    retro_set_input_state(input_cb);
    init_gamepak_buffer();
    RG_LOGI("ROM cache: %u blocks of 1 MB", (unsigned)gamepak_buffer_count);
    init_sound();
    // load_bios(RG_BASE_PATH_BIOS "/gba_bios.bin");

    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    if (load_gamepak(NULL, app->romPath, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
    {
        RG_PANIC("Could not load the game file.");
    }

    RG_LOGI("reset_gba");
    reset_gba();

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        RG_LOGI("load_state");
        rg_emu_load_state(app->saveSlot);
    }

    RG_LOGI("emulation loop");

    rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH] = {0};

    while (true)
    {
        // RG_TIMER_INIT();
        const int64_t startTime = rg_system_timer();
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
            memset(&mixbuffer, 0, sizeof(mixbuffer));
            continue;
        }

        update_input();
        rumble_frame_reset();
        clear_gamepak_stickybits();
#ifdef GBAPROF
        const bool drawn = !skip_next_frame;
        const int64_t t_exec = rg_system_timer();
        gbaprof_render_us = 0;
#endif
        execute_arm(execute_cycles);
        // RG_TIMER_LAP("execute_arm");
#ifdef GBAPROF
        const int64_t t_disp = rg_system_timer();
#endif

        if (!skip_next_frame)
            rg_display_submit(currentUpdate, 0);
#ifdef GBAPROF
        const int64_t t_snd = rg_system_timer();
#endif

        size_t frames_count = sound_read_samples((s16 *)mixbuffer, AUDIO_BUFFER_LENGTH);
        // RG_TIMER_LAP("sound_read_samples");
#ifdef GBAPROF
        {
            /* cpu = execute_arm minus the scanline renderer inside it */
            static int64_t cpu_us, render_us, disp_us, snd_us, t_last, instr;
            static int frames, drawn_n;
            const int64_t now = rg_system_timer();
            cpu_us += (t_disp - t_exec) - gbaprof_render_us;
            render_us += gbaprof_render_us;
            disp_us += t_snd - t_disp;
            snd_us += now - t_snd;
            frames++;
            drawn_n += drawn;
            instr += gbaprof_instr;
            gbaprof_instr = 0;
            if (now - t_last >= 1000000)
            {
                static int seconds;
                if (++seconds == 4)
                {
                    samp = heap_caps_calloc(SAMP_N, sizeof(*samp), MALLOC_CAP_SPIRAM);
                    esp_register_freertos_tick_hook_for_cpu(samp_tick, 0);
                    samp_on = true;
                }
                else if (seconds == 24)
                    samp_dump();
                printf("GBAPROF %d frames (%d drawn): ms/frame cpu %.2f sound %.2f display %.2f | render %.2f per drawn frame | %d instr/frame, %.0f cycles/instr | %u ROM pages loaded\n",
                       frames, drawn_n, cpu_us / 1000.f / frames, snd_us / 1000.f / frames, disp_us / 1000.f / frames,
                       drawn_n ? render_us / 1000.f / drawn_n : 0.f, (int)(instr / frames), instr ? cpu_us * 240.0 / instr : 0.0, (unsigned)gbaprof_pageloads);
                gbaprof_pageloads = 0;
                cpu_us = render_us = disp_us = snd_us = instr = 0;
                frames = drawn_n = 0;
                t_last = now;
            }
        }
#endif

        rg_system_tick(rg_system_timer() - startTime);

        rg_audio_submit(mixbuffer, frames_count);
        // RG_TIMER_LAP("rg_audio_submit");

        if (skip_next_frame == 0)
            skip_next_frame = app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;
    }

    RG_PANIC("GBsP Ended");
}
