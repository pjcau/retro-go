#include <rg_system.h>
#include <esp_system.h>   /* esp_reset_reason: a crash is not a power cut */
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../xtensa-68000-dynarec/components/gbsp-libretro/common.h"
#include "../../xtensa-68000-dynarec/components/gbsp-libretro/memmap.h"
#include "../../xtensa-68000-dynarec/components/gbsp-libretro/sound.h"
#include "../../xtensa-68000-dynarec/components/gbsp-libretro/gba_memory.h"
#include "../../xtensa-68000-dynarec/components/gbsp-libretro/gba_cc_lut.h"

/* the atomic battery-save file (gbsp/test/sram_file_test.c tests this header
   on the host): yield between chunks so the card is never held for a whole
   save while the ROM is paged from it */
#define SRAM_FILE_LOGE(...) RG_LOGE(__VA_ARGS__)
#define SRAM_FILE_LOGI(...) RG_LOGI(__VA_ARGS__)
#define SRAM_FILE_YIELD() vTaskDelay(1)
#include "sram_file.h"

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

/* three frame buffers: core 1 draws into currentUpdate while the display task
   sends the last submitted one; when the display is still busy at the end of
   a frame, that frame is not shown and emulation goes on in the third buffer
   instead of waiting (a full-screen 2x update is ~16-17 ms on the 20 MHz bus) */
static rg_surface_t *updates[3];
static rg_surface_t *displaying;   /* last submitted */
static rg_surface_t *pending;      /* finished while the display was busy: sent as soon as it is free */
static rg_surface_t *frame_done;   /* the frame just finished (GBABENCH hash) */
static uint32_t frames_not_shown;

/* called by the scanline code every 32 lines (core 0): send the pending frame
   as soon as the display task has taken the previous one */
void gbsp_display_poll(void)
{
    if (pending && rg_display_sync(false))
    {
        rg_display_submit(pending, 0);
        displaying = pending;
        pending = NULL;
    }
}
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
/* core 1 PCs (renderer, display): which code shares the instruction cache */
#define SAMP1_N 2048
static struct { uint32_t pc, n; } *samp1;
static void IRAM_ATTR c1_tick(void)
{
    TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(1);
    if (samp1 && samp_on && t)
    {
        uint32_t pc = (*(uint32_t **)t)[1], h = (pc >> 2) & (SAMP1_N - 1);
        for (int i = 0; i < 16; i++, h = (h + 1) & (SAMP1_N - 1))
            if (samp1[h].pc == pc || samp1[h].n == 0) { samp1[h].pc = pc; samp1[h].n++; break; }
    }
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
/* The tables are claimed before the ROM cache takes PSRAM: during a game only
   ~17 KB of it is free, heap_caps_calloc returned NULL, and the sampler then
   did nothing at all without saying so (the 2026-10-09 GBA runs produced no
   GBASAMPLE line for that reason). */
static void samp_alloc(void)
{
    samp = heap_caps_calloc(SAMP_N, sizeof(*samp), MALLOC_CAP_SPIRAM);
    samp1 = heap_caps_calloc(SAMP1_N, sizeof(*samp1), MALLOC_CAP_SPIRAM);
    if (samp && samp1)
        RG_LOGI("sampling profiler: %u KB of PSRAM claimed before the ROM cache",
                (unsigned)((SAMP_N * sizeof(*samp) + SAMP1_N * sizeof(*samp1)) / 1024));
    else
        RG_LOGE("sampling profiler: %u KB of PSRAM REFUSED, there will be no GBASAMPLE",
                (unsigned)((SAMP_N * sizeof(*samp) + SAMP1_N * sizeof(*samp1)) / 1024));
}
static void samp_dump(void)
{
    uint32_t total = 0;
    if (!samp)
    {
        printf("GBASAMPLE unavailable: the tables were refused at startup\n");
        return;
    }
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
    for (int k = 0; samp1 && k < 150; k++)
    {
        int best = -1;
        for (int i = 0; i < SAMP1_N; i++)
            if (samp1[i].n && (best < 0 || samp1[i].n > samp1[best].n))
                best = i;
        if (best < 0)
            break;
        printf("GBASAMPLE1 %08x %u\n", (unsigned)samp1[best].pc, (unsigned)samp1[best].n);
        samp1[best].n = 0;
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

/* the cartridge's battery memory (SRAM / flash / EEPROM, 128 KB buffer):
   <saves>/gba/<rom>.gba.sram, read at start, written ~1-2 s after the game
   stops writing it and when leaving */

extern u8 gamepak_backup_dirty;
static char *sram_path;
static void sram_save_sync(void);

/* the table itself is in sram_file.h, where the host test can reach it.
   gba_memory.h declares sram_bankcount but nothing defines it, so GBA SRAM is
   the fixed 32 KB it is on the hardware. */
static size_t sram_real_size(void)
{
    return sram_backup_size(backup_type, flash_bank_cnt, eeprom_size, sizeof(gamepak_backup));
}

/* The write used to happen in the frame loop: 128 KB to FAT over SPI, one
   second after the game touched its save, which is hundreds of milliseconds in
   a single frame -- the stall the user saw during a race. Now the frame loop
   only copies the save into a snapshot (microseconds) and a low-priority task
   does the file work. The snapshot is claimed before the ROM cache takes the
   rest of PSRAM, the way the renderer's VRAM copy and the sampler's tables
   are; if it cannot be had, the write stays synchronous rather than silently
   not happening. */
static u8 *sram_snap;
static size_t sram_snap_len;
static volatile bool sram_snap_pending;
static TaskHandle_t sram_task_h;
static SemaphoreHandle_t sram_file_lock;   /* the task and a synchronous save */

static void sram_alloc(void)
{
    sram_snap = heap_caps_malloc(sizeof(gamepak_backup), MALLOC_CAP_SPIRAM);
    sram_file_lock = xSemaphoreCreateMutex();
    if (sram_snap && sram_file_lock)
        RG_LOGI("battery save: %u KB of PSRAM claimed before the ROM cache",
                (unsigned)(sizeof(gamepak_backup) / 1024));
    else
        RG_LOGE("battery save: no snapshot buffer, writes stay in the frame loop");
}

static void sram_write_locked(const void *data, size_t len)
{
    if (sram_file_lock)
        xSemaphoreTake(sram_file_lock, portMAX_DELAY);
    rg_storage_mkdir(rg_dirname(sram_path));
    sram_file_write(sram_path, data, len);
    if (sram_file_lock)
        xSemaphoreGive(sram_file_lock);
}

static void sram_task(void *arg)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (sram_snap_pending)
        {
            sram_write_locked(sram_snap, sram_snap_len);
            sram_snap_pending = false;
        }
    }
}

static void sram_load(void)
{
    if (!sram_path)
        return;
    /* up to the whole buffer, so the 128 KB files older builds wrote still
       load in full whatever the cartridge turns out to be */
    size_t n = sram_file_read(sram_path, gamepak_backup, sizeof(gamepak_backup));
    if (n)
        RG_LOGI("battery save loaded: %s (%u bytes)", sram_path, (unsigned)n);
}

/* the frame loop: copy and go */
static void sram_schedule(void)
{
    if (!sram_path || !gamepak_backup_dirty)
        return;
    if (!sram_snap || !sram_task_h)
    {
        sram_save_sync();   /* no buffer and no task: as it was before */
        return;
    }
    if (sram_snap_pending)
        return;   /* a write is in flight; the dirty flag stays set for the next one */
    sram_snap_len = sram_real_size();
    memcpy(sram_snap, gamepak_backup, sram_snap_len);
    /* only now: a write landing during the copy above leaves the flag set and
       earns another snapshot, with the newer data */
    gamepak_backup_dirty = 0;
    sram_snap_pending = true;
    xTaskNotifyGive(sram_task_h);
}

/* the menu, a quit and a shutdown: nothing may be left unwritten */
static void sram_save_sync(void)
{
    if (!sram_path)
        return;
    if (sram_snap_pending)
    {
        sram_snap_pending = false;
        sram_write_locked(sram_snap, sram_snap_len);
    }
    if (gamepak_backup_dirty)
    {
        gamepak_backup_dirty = 0;
        sram_write_locked(gamepak_backup, sram_real_size());
    }
}

#ifdef HAVE_DYNAREC
/* per game (NS_FILE): whether the dynarec may run it. A launch sets TRYING,
   two minutes of play (or a clean exit) set OK; a launch that finds TRYING
   means the last one crashed or hung on the dynarec, and the game uses the
   interpreter from then on (OFF), as it does after a self-modifying-code
   storm (xt_give_up). Save states and .sram files are the same for both. */
#define JIT_KEY   "DynarecState"
#define JIT_FAILS_KEY "DynarecFails"
#define JIT_FAILS_MAX 3   /* observed, repeated crashes before giving up */
/* JIT_OFF is what the guard (or an older build) wrote, and is retried on the
   next start; JIT_OFF_USER is the user's own choice and is never second-guessed.
   JIT_OK stays 0 so an existing card and a hand-written {"DynarecState":0}
   still mean "fine". */
enum { JIT_OK = 0, JIT_TRYING = 1, JIT_OFF = 2, JIT_OFF_USER = 3 };
static int jit_state = JIT_OK;
static int jit_fails;
static void jit_state_set(int v)
{
    jit_state = v;
    rg_settings_set_number(NS_FILE, JIT_KEY, v);
    rg_settings_commit();
}
static void jit_fails_set(int v)
{
    jit_fails = v;
    rg_settings_set_number(NS_FILE, JIT_FAILS_KEY, v);
    rg_settings_commit();
}
/* Did the machine come back from a crash, or just from being switched off?
   This is the whole fix: a TRYING left behind says only "the last start did
   not finish", and before this it was read as a crash -- so a power cut or a
   reset inside two minutes disabled the dynarec for the game for ever. Only
   the resets below are the dynarec's fault. retro-go switches apps with a
   software reset, so ESP_RST_SW is the normal way a game starts and must not
   count. A hang is caught, because a watchdog is what ends it. */
static bool jit_reset_was_a_crash(void)
{
    switch (esp_reset_reason())
    {
    case ESP_RST_PANIC:    return true;   /* abort(), Guru Meditation */
    case ESP_RST_INT_WDT:  return true;
    case ESP_RST_TASK_WDT: return true;
    case ESP_RST_WDT:      return true;
    default:               return false;  /* POWERON, SW, EXT, BROWNOUT, ... */
    }
}
#endif

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_SHUTDOWN)
    {
        sram_save_sync();
#ifdef HAVE_DYNAREC
        if (jit_state == JIT_TRYING)
            jit_state_set(JIT_OK);   /* a clean exit proves it as well as two minutes */
#endif
    }
    if (event == RG_EVENT_REDRAW)
    {
        rg_display_submit(displaying ? displaying : currentUpdate, 0);
    }
}

#ifdef GBABENCH
#include "xtensa_perfmon_access.h"
#include "xtensa/xt_perf_consts.h"
static int bench_frame;
/* core-0 LX7 counters around the CPU emulation; 2 counters, 3 pairs taken in
   turn frame by frame (each total is scaled by 3 when printed) */
static const uint16_t perf_sel[3][2][2] = {
    {{XTPERF_CNT_CYCLES, XTPERF_MASK_CYCLES}, {XTPERF_CNT_INSN, XTPERF_MASK_INSN_ALL}},
    {{XTPERF_CNT_I_STALL, XTPERF_MASK_I_STALL_ALL}, {XTPERF_CNT_D_STALL, XTPERF_MASK_D_STALL_ALL}},
    {{XTPERF_CNT_I_STALL, XTPERF_MASK_I_STALL_CACHE_MISS}, {XTPERF_CNT_I_STALL, XTPERF_MASK_I_STALL_BUSY | XTPERF_MASK_I_STALL_IN_PIF}},
};
static uint64_t perf_sum[3][2];
static void perf_begin(int f)
{
    const int p = f % 3;
    xtensa_perfmon_stop();
    for (int i = 0; i < 2; i++)
    {
        xtensa_perfmon_init(i, perf_sel[p][i][0], perf_sel[p][i][1], 0, -1);
        xtensa_perfmon_reset(i);
    }
    xtensa_perfmon_start();
}
static void perf_end(int f)
{
    xtensa_perfmon_stop();
    for (int i = 0; i < 2; i++)
        perf_sum[f % 3][i] += xtensa_perfmon_value(i);
}
/* right held, B 4 frames in 16, A 10 frames in 120 (libretro bits) */
static int16_t bench_keys(int f)
{
    int16_t m = 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if ((f & 15) < 4) m |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
    if (f % 120 < 10) m |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
    return m;
}
#endif

int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    // RG_LOGI("%u, %u, %u, %u", port, device, index, id);
#ifdef GBABENCH
    return bench_keys(bench_frame);
#endif
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

#ifdef HAVE_DYNAREC
/* The per-game engine switch, and it takes effect at once in both directions.
   On used to mean "from the next start": it set the stored state but left
   xt_give_up at 1, so the frame loop below wrote OFF straight back and the
   choice was undone before the user left the menu. */
static rg_gui_event_t dynarec_toggle_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    extern int xt_give_up;
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        if (xt_give_up)
        {
            jit_fails_set(0);          /* the user overrules the guard's count */
            jit_state_set(JIT_TRYING); /* unproven again, and proven by playing */
            xt_give_up = 0;            /* the translation caches are already there */
        }
        else
        {
            jit_state_set(JIT_OFF_USER);
            xt_give_up = 1;
        }
    }
    strcpy(option->value, xt_give_up ? _("Off") : _("On"));
    return RG_DIALOG_VOID;
}
#endif

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Audio enable"), "-", RG_DIALOG_FLAG_NORMAL, &sound_toggle_cb};
#ifdef HAVE_DYNAREC
    *dest++ = (rg_gui_option_t){0, _("Fast CPU (dynarec)"), "-", RG_DIALOG_FLAG_NORMAL, &dynarec_toggle_cb};
#endif
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
    updates[2] = updates[1] ? rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_SLOW) : NULL;
    if (updates[2])
        updates[2]->height = GBA_SCREEN_HEIGHT;
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
        static uint8_t iram_cache[ROM_TRANSLATION_CACHE_SIZE + RAM_TRANSLATION_CACHE_SIZE] __attribute__((aligned(64)));   /* .bss: not fragmented */
        jit.size = sizeof(iram_cache);
        jit.data = iram_cache;
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
#ifdef HAVE_DYNAREC
    {
        extern void gbsp_rvram_alloc(void);   /* the renderer's VRAM copy, before the ROM cache takes PSRAM */
        gbsp_rvram_alloc();
    }
#endif
    sram_alloc();   /* before the ROM cache: during a game PSRAM is full */
#ifdef GBAPROF
    samp_alloc();   /* the same, and for the same reason */
#endif
    init_gamepak_buffer();
    RG_LOGI("ROM cache: %u blocks of 1 MB", (unsigned)gamepak_buffer_count);
    init_sound();
    // load_bios(RG_BASE_PATH_BIOS "/gba_bios.bin");

    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    {
        /* the loading percentage under the hourglass: the ROM is read 1 MB at a time */
        extern void (*gamepak_load_progress)(int percent);
        extern size_t (*gamepak_fread)(void *buffer, size_t length, FILE *fp);
        gamepak_load_progress = rg_gui_draw_loading;
        gamepak_fread = rg_storage_fread_raw; /* also for the pages read on demand in play */
    }
    if (load_gamepak(NULL, app->romPath, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
    {
        RG_PANIC("Could not load the game file.");
    }
    {
        extern void (*gamepak_load_progress)(int percent);
        gamepak_load_progress = NULL;
    }

    gbsp_render_start();
    RG_LOGI("line renderer on core 1: %s", gbsp_render_core1 ? "yes" : "no");
    RG_LOGI("reset_gba");
    reset_gba();

    sram_path = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    sram_load();
    if (sram_snap && xTaskCreatePinnedToCore(sram_task, "gba_sram", 3072, NULL, 1, &sram_task_h, 0) != pdPASS)
    {
        sram_task_h = NULL;   /* no task: sram_schedule falls back to writing here */
        RG_LOGE("battery save: no task, writes stay in the frame loop");
    }
    gamepak_backup_dirty = 0;

#ifdef HAVE_DYNAREC
    {
        extern int xt_give_up;
        const bool crashed = jit_reset_was_a_crash();
        jit_state = (int)rg_settings_get_number(NS_FILE, JIT_KEY, JIT_OK);
        jit_fails = (int)rg_settings_get_number(NS_FILE, JIT_FAILS_KEY, 0);

        if (jit_state == JIT_OFF_USER)
        {
            xt_give_up = 1;   /* the user's own choice: left alone */
        }
        else
        {
            if (jit_state == JIT_TRYING && crashed)
            {
                jit_fails_set(jit_fails + 1);
                RG_LOGW("the dynarec crashed on this game (reset reason %d), %d of %d",
                        (int)esp_reset_reason(), jit_fails, JIT_FAILS_MAX);
            }
            else if (jit_state == JIT_TRYING)
            {
                /* the last start did not finish, but nothing crashed: a power
                   cut, a reset or a switch of app. Not the dynarec's fault. */
                RG_LOGI("the last start did not finish but did not crash (reset reason %d): "
                        "keeping the dynarec", (int)esp_reset_reason());
            }
            else if (jit_state == JIT_OFF)
            {
                /* switched off automatically, here or by an older build that
                   could not tell a crash from a power cut: give it a clean
                   chance rather than leaving the game on the interpreter. */
                RG_LOGW("the dynarec was switched off automatically before: trying it again");
                jit_fails_set(0);
            }

            if (jit_fails >= JIT_FAILS_MAX)
            {
                jit_state_set(JIT_OFF);
                xt_give_up = 1;
                /* An alert a held button dismisses is an alert nobody reads,
                   and this one changes how the game runs: wait for every key
                   to come up first, so it takes a fresh press. */
                rg_input_wait_for_key(RG_KEY_ALL, false, 2000);
                rg_gui_alert("GBA", "This game crashed repeatedly with the fast CPU core (dynarec): "
                                    "it now runs with the interpreter. Options > Fast CPU turns it back on.");
            }
            else
            {
                xt_give_up = 0;
                jit_state_set(JIT_TRYING);
            }
        }
        RG_LOGI("CPU core: %s (DynarecState %d, fails %d)",
                xt_give_up ? "interpreter" : "dynarec", jit_state, jit_fails);
    }
#endif

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
            sram_save_sync();   /* the menu can quit the game */
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
#ifdef GBABENCH
        const int64_t tb_exec = rg_system_timer();
        perf_begin(bench_frame);
#endif
#ifdef HAVE_DYNAREC
        {
            extern int xt_give_up;   /* xtensa_stub.c: self-modifying code storm, or DynarecState */
            static int jit_frames;
            if (xt_give_up)
            {
                execute_arm(execute_cycles);
                /* Only a give-up the emulator decided (a self-modifying-code
                   storm) is worth remembering. JIT_OFF_USER is already the
                   user's choice, and overwriting JIT_TRYING here is what used
                   to undo the menu's On a frame after it was chosen. */
                if (jit_state == JIT_OK)
                    jit_state_set(JIT_OFF);
            }
            else
            {
                execute_arm_translate(execute_cycles);
                if (jit_state == JIT_TRYING && ++jit_frames == 60 * 120)
                {
                    jit_state_set(JIT_OK);   /* two minutes without trouble */
                    if (jit_fails)
                        jit_fails_set(0);    /* and the crash count is spent */
                }
            }
        }
#else
        execute_arm(execute_cycles);
#endif
#ifdef GBABENCH
        perf_end(bench_frame);
#endif
#ifdef GBABENCH
        const int64_t tb_render = rg_system_timer();
        int64_t tb_display = tb_render;
#endif
        // RG_TIMER_LAP("execute_arm");
#ifdef GBAPROF
        const int64_t t_disp = rg_system_timer();
#endif

        if (!skip_next_frame)
        {
            gbsp_render_wait();   /* core 1 finishes the frame's last lines */
#ifdef GBABENCH
            tb_display = rg_system_timer();
#endif
            frame_done = currentUpdate;
            if (updates[2])
            {
                if (pending)            /* never sent: this newer frame replaces it */
                    frames_not_shown++;
                pending = currentUpdate;
                gbsp_display_poll();
                for (int i = 0; i < 3; i++)
                    if (updates[i] != displaying && updates[i] != pending && updates[i] != frame_done)
                    {
                        currentUpdate = updates[i];
                        break;
                    }
                gba_screen_pixels = currentUpdate->data;
            }
            else if (updates[1])
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

#ifdef GBABENCH
        const int64_t tb_sound = rg_system_timer();
#endif
        size_t frames_count = sound_read_samples((s16 *)mixbuffer, AUDIO_BUFFER_LENGTH);
        // RG_TIMER_LAP("sound_read_samples");
#ifdef GBABENCH
        {
            static int64_t work_us, exec_us, rwait_us, disp_us, snd_us;
            exec_us += tb_render - tb_exec;
            rwait_us += tb_display - tb_render;
            disp_us += tb_sound - tb_display;
            snd_us += rg_system_timer() - tb_sound;
            static uint32_t acc;
            work_us += rg_system_timer() - startTime;
            /* the frame just submitted (the buffers were swapped) */
            const rg_surface_t *shown = frame_done ? frame_done : currentUpdate;
            uint32_t h = 2166136261u;
            for (int y = 0; y < GBA_SCREEN_HEIGHT; y++)
            {
                const uint16_t *line = (const uint16_t *)((const uint8_t *)shown->data + y * shown->stride);
                for (int x = 0; x < GBA_SCREEN_WIDTH; x++)
                    h = (h ^ line[x]) * 16777619u;
            }
            acc = acc * 31 + h;
            if (++bench_frame % 300 == 0)
            {
                printf("GBABENCH frames %d work %.2f ms/frame hash %08lx | exec %.2f render-wait %.2f display %.2f sound %.2f | not shown %u\n",
                       bench_frame, work_us / 1000.0 / 300, (unsigned long)acc, exec_us / 300000.0, rwait_us / 300000.0,
                       disp_us / 300000.0, snd_us / 300000.0, (unsigned)frames_not_shown);
                frames_not_shown = 0;
                {
                    /* per frame, in thousands (Mcycles/1000); each pair ran one frame in three */
                    const double k = 3.0 / 300 / 1000;
                    printf("GBABENCH perf: kcycles %.0f kinstr %.0f (%.2f cyc/instr) | I-stall %.0f (cache-miss %.0f, busy/PIF %.0f) D-stall %.0f\n",
                           perf_sum[0][0] * k, perf_sum[0][1] * k, perf_sum[0][1] ? (double)perf_sum[0][0] / perf_sum[0][1] : 0.0,
                           perf_sum[1][0] * k, perf_sum[2][0] * k, perf_sum[2][1] * k, perf_sum[1][1] * k);
                    memset(perf_sum, 0, sizeof(perf_sum));
                }
                {
                    static rg_display_counters_t last;
                    rg_display_counters_t c = rg_display_get_counters();
                    int shown = (int)((c.fullFrames + c.partFrames) - (last.fullFrames + last.partFrames));
                    printf("GBABENCH display: %d frames sent (%d full), %.2f ms each\n", shown, (int)(c.fullFrames - last.fullFrames),
                           shown ? (c.busyTime - last.busyTime) / 1000.0 / shown : 0.0);
                    last = c;
                }
                work_us = exec_us = rwait_us = disp_us = snd_us = 0;
            }
        }
#endif
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
                {
                    extern int64_t gbaprof_wait_by[8];
                    printf("GBAWAIT by cause ms/frame: other %.2f cpuBG %.2f cpuOBJ %.2f dmaBG %.2f dmaOBJ %.2f oam %.2f pal %.2f end %.2f\n",
                           gbaprof_wait_by[0] / 1000.f / frames, gbaprof_wait_by[1] / 1000.f / frames, gbaprof_wait_by[2] / 1000.f / frames,
                           gbaprof_wait_by[3] / 1000.f / frames, gbaprof_wait_by[4] / 1000.f / frames, gbaprof_wait_by[5] / 1000.f / frames,
                           gbaprof_wait_by[6] / 1000.f / frames, gbaprof_wait_by[7] / 1000.f / frames);
                    memset(gbaprof_wait_by, 0, sizeof(gbaprof_wait_by));
                }
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
                    extern u32 xt_prof_translate_cycles, xt_prof_translate_blocks;
                    printf("GBAJIT translate: %u blocks, %.2f ms/frame\n", (unsigned)xt_prof_translate_blocks,
                           xt_prof_translate_cycles / 240000.f / frames);
                    {
                        extern u32 xt_prof_tr_region[16], xt_prof_tr_pc[4], xt_prof_rom_flush;
                        printf("GBAJIT translate by region: bios %u ewram %u iwram %u rom %u | ROM flushes %u | last pcs %08x %08x %08x %08x\n",
                               (unsigned)xt_prof_tr_region[0], (unsigned)xt_prof_tr_region[2], (unsigned)xt_prof_tr_region[3],
                               (unsigned)(xt_prof_tr_region[8] + xt_prof_tr_region[9] + xt_prof_tr_region[10] + xt_prof_tr_region[11]),
                               (unsigned)xt_prof_rom_flush, (unsigned)xt_prof_tr_pc[0], (unsigned)xt_prof_tr_pc[1], (unsigned)xt_prof_tr_pc[2], (unsigned)xt_prof_tr_pc[3]);
                        memset(xt_prof_tr_region, 0, sizeof(xt_prof_tr_region));
                        xt_prof_rom_flush = 0;
                    }
                    xt_prof_translate_cycles = xt_prof_translate_blocks = 0;
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

        /* battery save: one second after the game started writing it (a flash
           save takes several frames), and again if it goes on. Only the
           snapshot happens here; the card work is on sram_task. */
        {
            static int sram_timer;
            if (gamepak_backup_dirty && !sram_timer)
                sram_timer = 60;
            else if (sram_timer && --sram_timer == 0)
                sram_schedule();
        }

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
