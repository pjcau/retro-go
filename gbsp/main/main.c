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
#ifdef HAVE_DYNAREC
/* the Xtensa dynarec (gbsp-libretro/xtensa): translation caches in PSRAM
   mapped executable, see components/xjit */
#include "xjit_exec.h"
#include "soc/soc.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
int dynarec_enable = 1;
extern u32 xt_exec_delta;
u32 execute_arm_translate(u32 cycles);
#endif
/* scanline renderer on core 1 (gbsp-libretro/video.cpp) */
extern int gbsp_render_core1;
void gbsp_render_start(void);
void gbsp_render_wait(void);
#ifdef GBAPROF
extern int64_t gbaprof_render_us, gbaprof_wait_us;
extern u32 gbaprof_lag159, gbaprof_syncs, gbaprof_lag80, gbaprof_wakes;
extern u32 gbaprof_instr;
u32 gbaprof_pageloads;
static int64_t gbaprof_sync_us;
#endif
extern u32 gamepak_buffer_count;

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
#include <esp_debug_helpers.h>
#include <esp_memory_utils.h>
#define SAMP_N 8192
static struct samp_s { uint32_t pc, n, a0, ps; } *samp;   /* PSRAM: internal RAM is full */
static volatile bool samp_on;
static uint32_t samp_up2, samp_all;
uint32_t samp_jit_lo = 0x42400000, samp_jit_len = 0x1C00000;   /* the translated code */
static void IRAM_ATTR samp_tick(void)
{
    if (!samp_on || !samp)
        return;
    TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(0);
    if (!t)
        return;
    uint32_t *f = *(uint32_t **)t, pc = f[1], up = f[3];
    /* in _xtos_set_intlevel (ROM, the end of a critical section, where the
       ticks it held back land): key on the caller of vPortExitCritical */
    if (pc - 0x400559d0u < 0x40)
    {
        esp_backtrace_frame_t fr = {.pc = f[1], .sp = f[4], .next_pc = f[3]};
        for (int d = 0; d < 5 && esp_stack_ptr_is_sane(fr.sp); d++)
        {
            if (!esp_backtrace_get_next_frame(&fr))
                break;
            if (d == 1) pc = fr.pc | 1;   /* odd: marks a critical-section caller */
            if (d == 3) up = fr.pc;
            if (d == 4) samp_up2 = fr.pc;
        }
    }
    if (pc - samp_jit_lo < samp_jit_len)
        pc &= ~0xFFu;   /* translated code (PSRAM mapped executable): 256-byte buckets */
    samp_all++;
    uint32_t h = (pc >> 2) & (SAMP_N - 1);
    for (int i = 0; i < 16; i++, h = (h + 1) & (SAMP_N - 1))
        if (samp[h].pc == pc || samp[h].n == 0)
        {
            samp[h].pc = pc;
            samp[h].n++;
            samp[h].a0 = up;   /* the last caller seen */
            samp[h].ps = (*(uint32_t **)t)[2];
            return;
        }
}
/* core 1: which task runs at each tick */
#define C1_N 12
typedef struct { TaskHandle_t t; uint32_t n; } task_ticks_t;
static task_ticks_t c1[C1_N], c0[C1_N];
static void IRAM_ATTR c1_tick(void)
{
    TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(1);
    for (int i = 0; i < C1_N; i++)
        if (c1[i].t == t || !c1[i].t) { c1[i].t = t; c1[i].n++; break; }
    t = xTaskGetCurrentTaskHandleForCore(0);
    for (int i = 0; i < C1_N; i++)
        if (c0[i].t == t || !c0[i].t) { c0[i].t = t; c0[i].n++; break; }
}
static void c1_dump(void)
{
    for (int c = 1; c >= 0; c--)
    {
        task_ticks_t *a = c ? c1 : c0;
        uint32_t tot = 0;
        for (int i = 0; i < C1_N; i++) tot += a[i].n;
        printf("CORE%d", c);
        for (int i = 0; i < C1_N && a[i].t; i++)
            printf(" %s:%.0f%%", pcTaskGetName(a[i].t), 100.0 * a[i].n / (tot ? tot : 1));
        printf("\n");
        memset(a, 0, sizeof(c1));
    }
}
static void samp_dump(void)
{
    uint32_t total = 0;
    if (!samp)
        return;
    samp_on = false;
    uint32_t jit = 0, jit_ram = 0;
#ifdef HAVE_DYNAREC
    extern u8 *ram_translation_cache;
    const uint32_t ram_code = (uint32_t)(uintptr_t)ram_translation_cache + xt_exec_delta;
#else
    const uint32_t ram_code = 0;
#endif
    for (int i = 0; i < SAMP_N; i++)
    {
        total += samp[i].n;
        if (samp[i].pc - samp_jit_lo < samp_jit_len) jit += samp[i].n;
        if (samp[i].pc >= (ram_code & ~0xFFu) && samp[i].pc < ram_code + RAM_TRANSLATION_CACHE_SIZE) jit_ram += samp[i].n;
    }
    for (int k = 0; k < 300; k++)
    {
        int best = -1;
        for (int i = 0; i < SAMP_N; i++)
            if (samp[i].n && !(samp[i].pc - samp_jit_lo < samp_jit_len) && (best < 0 || samp[i].n > samp[best].n))
                best = i;
        if (best < 0)
            break;
        printf("GBASAMPLE %08x %u %.2f a0 %08x ps %08x\n", (unsigned)samp[best].pc, (unsigned)samp[best].n, 100.0 * samp[best].n / total,
               (unsigned)samp[best].a0, (unsigned)samp[best].ps);
        samp[best].n = 0;
    }
    printf("GBASAMPLE total %u of %u ticks (last depth-4 caller %08x)\n", (unsigned)total, (unsigned)samp_all, (unsigned)samp_up2);
    {
        printf("GBASAMPLE translated code %.1f%% (RAM cache code %.1f%%)\n", 100.0 * jit / (total ? total : 1), 100.0 * jit_ram / (total ? total : 1));
    }
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

#ifdef HAVE_DYNAREC
/* the dynarec translates a block's branch targets recursively: deeper than
   the main task's stack, so the emulator runs on its own task */
static void gbsp_main(void);
static void gbsp_task(void *arg)
{
    gbsp_main();
}
void app_main(void)
{
    if (xTaskCreatePinnedToCore(gbsp_task, "gbsp", 20 * 1024, NULL, uxTaskPriorityGet(NULL), NULL, 0) != pdPASS)
        gbsp_main();   /* no memory for the stack: try on the main task */
    vTaskDelete(NULL);
}
static void gbsp_main(void)
#else
void app_main(void)
#endif
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

#ifdef HAVE_DYNAREC
    /* the dynarec's IWRAM (64 KB with its SMC tags) takes the internal RAM */
    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_SLOW);
#else
    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
#endif
    updates[0]->height = GBA_SCREEN_HEIGHT;
    /* second buffer (PSRAM: internal RAM is full): the display task on core 1
       sends one frame while the next is drawn into the other, otherwise the
       top of the next frame shows up in the one being sent */
    updates[1] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_SLOW);
    if (updates[1])
        updates[1]->height = GBA_SCREEN_HEIGHT;
    currentUpdate = updates[0];

    gba_screen_pixels = currentUpdate->data;

    gbsp_memory = rg_alloc(sizeof(*gbsp_memory), MEM_ANY);
    RG_LOGI("gbsp_memory=%p", gbsp_memory);

    libretro_supports_bitmasks = true;
    retro_set_input_state(input_cb);
#ifdef HAVE_DYNAREC
    {
        static xj_exec_t jit;   /* before the ROM cache takes the rest of PSRAM */
#ifdef XT_IRAM_CACHE
        /* internal RAM: written through the data bus alias (byte stores), run
           through the instruction bus */
        /* the exec heap is tiny: take plain internal RAM in SRAM1, which is
           also mapped on the instruction bus (memory protection off) */
        jit.size = ROM_TRANSLATION_CACHE_SIZE + RAM_TRANSLATION_CACHE_SIZE;
        jit.data = heap_caps_aligned_alloc(4, jit.size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (jit.data && (uintptr_t)jit.data >= SOC_DIRAM_DRAM_LOW && (uintptr_t)jit.data + jit.size <= SOC_DIRAM_DRAM_HIGH)
            jit.exec = MAP_DRAM_TO_IRAM((uint32_t)(uintptr_t)jit.data);
        else
#else
        if (!xj_exec_alloc_psram(&jit, ROM_TRANSLATION_CACHE_SIZE + RAM_TRANSLATION_CACHE_SIZE))
#endif
        {
            static char msg[128];
            snprintf(msg, sizeof(msg), "No memory for the translation caches (exec largest %u, exec free %u, internal free %u)",
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC), (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            RG_PANIC(msg);
        }
        rom_translation_cache = jit.data;
        ram_translation_cache = jit.data + ROM_TRANSLATION_CACHE_SIZE;
        rom_translation_ptr = rom_translation_cache;
        ram_translation_ptr = ram_translation_cache;
        xt_exec_delta = jit.exec - (u32)(uintptr_t)jit.data;
#ifdef GBAPROF
        {
            extern uint32_t samp_jit_lo, samp_jit_len;
            samp_jit_lo = jit.exec;
            samp_jit_len = ROM_TRANSLATION_CACHE_SIZE + RAM_TRANSLATION_CACHE_SIZE;
        }
#endif
        RG_LOGI("dynarec: translation caches %u KB at %p (exec %08lx)",
                (unsigned)((ROM_TRANSLATION_CACHE_SIZE + RAM_TRANSLATION_CACHE_SIZE) / 1024), jit.data, (unsigned long)jit.exec);
    }
#endif
    init_gamepak_buffer();
    RG_LOGI("ROM cache: %u blocks of 1 MB", (unsigned)gamepak_buffer_count);
    init_sound();
    // load_bios(RG_BASE_PATH_BIOS "/gba_bios.bin");

    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    if (load_gamepak(NULL, app->romPath, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
    {
        RG_PANIC("Could not load the game file.");
    }

    gbsp_render_start();
    RG_LOGI("line renderer on core 1: %s", gbsp_render_core1 ? "yes" : "no");
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
#ifdef HAVE_DYNAREC
        execute_arm_translate(execute_cycles);
#else
        execute_arm(execute_cycles);
#endif
        // RG_TIMER_LAP("execute_arm");
#ifdef GBAPROF
        const int64_t t_disp = rg_system_timer();
#endif

        if (!skip_next_frame)
        {
            gbsp_render_wait();   /* core 1 finishes the frame's last lines */
            if (updates[1])
            {
#ifdef GBAPROF
                const int64_t t_sync = rg_system_timer();
#endif
                rg_display_sync(true);   /* the other buffer has been sent */
#ifdef GBAPROF
                gbaprof_sync_us += rg_system_timer() - t_sync;
#endif
                rg_display_submit(currentUpdate, 0);
                currentUpdate = updates[currentUpdate == updates[0]];
                gba_screen_pixels = currentUpdate->data;
            }
            else
                rg_display_submit(currentUpdate, 0);
        }
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
            cpu_us += (t_disp - t_exec) - (gbsp_render_core1 ? 0 : gbaprof_render_us);
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
                    esp_register_freertos_tick_hook_for_cpu(c1_tick, 1);
                    samp_on = true;
                }
                else if (seconds == 24)
                    samp_dump();
                printf("GBAWAIT ms/frame: wait for core 1 %.2f (%.1f syncs), display sync %.2f | core 1 lines behind at line 80: %.1f, 159: %.1f, wakes %.1f\n",
                       gbaprof_wait_us / 1000.f / frames, (float)gbaprof_syncs / frames, gbaprof_sync_us / 1000.f / frames, (float)gbaprof_lag80 / frames, (float)gbaprof_lag159 / frames, (float)gbaprof_wakes / frames);
                gbaprof_wait_us = gbaprof_sync_us = 0;
                gbaprof_lag159 = gbaprof_syncs = gbaprof_lag80 = gbaprof_wakes = 0;
                c1_dump();
#ifdef HAVE_DYNAREC
                {
                    extern u32 xt_prof_syncs, xt_prof_sync_cycles, flush_ram_count;
                    static u32 last_flush;
                    extern u32 gbaprof_notify_cycles, gbaprof_notifies;
                    printf("GBAJIT per second: %u cache syncs (%.2f ms/frame), %u RAM flushes, %u notifies (%.2f ms/frame)\n", (unsigned)xt_prof_syncs,
                           xt_prof_sync_cycles / 240000.f / frames, (unsigned)(flush_ram_count - last_flush),
                           (unsigned)gbaprof_notifies, gbaprof_notify_cycles / 240000.f / frames);
                    gbaprof_notify_cycles = gbaprof_notifies = 0;
                    printf("GBAJIT code: ROM cache %u KB, RAM cache %u KB\n",
                           (unsigned)((rom_translation_ptr - rom_translation_cache) / 1024), (unsigned)((ram_translation_ptr - ram_translation_cache) / 1024));
                    xt_prof_syncs = xt_prof_sync_cycles = 0;
                    last_flush = flush_ram_count;
                }
#endif
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

        /* with the lines drawn on core 1 a skipped frame saves core 0
           nothing: draw them all (retro-go's auto frameskip still raises
           app->frameskip when the game runs below full speed) */
        if (gbsp_render_core1)
            skip_next_frame = 0;
        else if (skip_next_frame == 0)
            skip_next_frame = app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;
    }

    RG_PANIC("GBsP Ended");
}
