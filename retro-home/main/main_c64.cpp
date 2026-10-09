/*
    Commodore 64 front end for retro-home.

    The core is Frank Bösing's Teensy64 by way of MCUME, in
    components/c64/ (GPL v3 or later -- see components/c64/LICENSE.md). This
    file is everything the core needs from the platform: the frame loop, the
    display surface the VIC draws into, the SID's samples, the buttons, and
    getting the program the launcher picked to start on its own.

    ROMs -- no ROM is in this repository, they go on the SD card:
        /retro-go/bios/c64/kernal.rom    8192 bytes
        /retro-go/bios/c64/basic.rom     8192 bytes
        /retro-go/bios/c64/chargen.rom   4096 bytes
    A missing one is named in an alert, then the app exits.

    PAL or NTSC -- the machine is one or the other, and a game written for
    the wrong one plays a fifth too slow or too fast. "Video" in the options
    menu is Auto, PAL or NTSC; Auto reads the dump's file name ("(USA)",
    "(NTSC)" -> the NTSC machine, anything else PAL). Changing it restarts
    the program, because the raster counter and the CIA timers are in the
    machine's own clock.

    Games -- .prg, .d64 or a .zip holding either. A .prg is put in RAM at its
    own load address; of a .d64 the FIRST program file of the directory is
    loaded the same way. Nothing has to be typed: once the KERNAL has printed
    READY., the program is injected and RUN (or SYS <address>, for a program
    that does not load at the start of BASIC) is pushed into the KERNAL's
    keyboard buffer. Multi-load games -- the ones that come back to the disk
    for the next part -- will not get past their first part: there is no 1541
    here, only the one program image.

    Buttons
        D-pad       joystick, in port 2 (change it with L, or in the options)
        A           fire
        B           fire as well
        L           switch the joystick between port 2 and port 1
        START       SPACE      (what most title screens want)
        SELECT      RUN/STOP
        X           F1         } the keys games use to pick
        Y           F3         } players or options
        R           RETURN
        MENU        retro-go's game menu
        OPTION      retro-go's options menu, which holds:
                      "Video"          Auto, PAL or NTSC
                      "Joystick port"  2 or 1
                      "Type key"       pick a key with left/right, A sends it
                                       (A-Z, 0-9, SPACE, RETURN, RUN/STOP,
                                       F1/F3/F5/F7); several can be queued,
                                       they are typed once the menu closes.
*/

#include "shared.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c64.h"
#include "c64_emuapi.h"
#include "c64_sid.h"

/* The audio output rate never changes; what changes with the region is how
   many of its samples one frame is worth. The buffers are sized for PAL,
   which is the slower frame rate and so the bigger frame. */
#define C64_RATE AUDIO_SAMPLE_RATE
#define C64_PAL_FPS 50
#define C64_NTSC_FPS 60
#define C64_MAX_SAMPLES (C64_RATE / C64_PAL_FPS + 2)

static rg_app_t *app;
static rg_surface_t *update;

/* Where the autostart has got to. A reset puts it back to BOOTING so the
   program is injected again once the KERNAL has printed READY. */
enum autostart_phase
{
    BOOTING,
    STARTING,
    PLAYING
};
static enum autostart_phase phase = BOOTING;
static int phase_frames;

static uint32_t joystick;
static int joy_bits; /* C64_JOY*_* for the core, refreshed once a frame */

/* ---------------------------------------------------------------------- */
/* PAL or NTSC                                                            */
/* ---------------------------------------------------------------------- */
/*
  A C64 program was written for one machine or the other, and the two differ
  by 20 % in frame rate, so running an NTSC game on the PAL machine is not a
  matter of taste -- it plays a fifth too slow, with its music to match.

  The setting is per app (not per file): "Video" in the options menu, Auto by
  default, and Auto reads the file name. It is applied at c64_init() time, so
  changing it restarts the program.
*/
enum video_choice
{
    VIDEO_AUTO = 0,
    VIDEO_PAL = 1,
    VIDEO_NTSC = 2
};
#define SETTING_VIDEO "video"

static enum video_choice video;
static int fps = C64_PAL_FPS;
static bool region_change_pending;

/* The name Auto reads: the file the launcher picked, not prg_name -- that is
   the program's name inside a .d64, and the tag is on the file. */
static const char *rom_file_name = "";

static bool name_has(const char *name, const char *tag)
{
    const size_t n = strlen(tag);
    for (const char *p = name; *p; p++)
    {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tag[i])
            i++;
        if (i == n)
            return true;
    }
    return false;
}

/* What Auto makes of the file name. The dump sets tag the machine in the
   name, so "(NTSC)", "[NTSC]" or "(USA)" is a 6567; a European tag, and
   anything untagged, is the PAL machine the C64 sold most of. */
static c64_region_t region_from_name(const char *name)
{
    static const char *pal_tags[] = {"(pal", "[pal", "(europe", "(e)", "(uk", "(germany", "(france"};
    static const char *ntsc_tags[] = {"(ntsc", "[ntsc", "(usa", "[usa", "(us)", "(u)", "(japan", "(canada"};

    for (size_t i = 0; i < RG_COUNT(pal_tags); i++)
        if (name_has(name, pal_tags[i]))
            return C64_REGION_PAL;
    for (size_t i = 0; i < RG_COUNT(ntsc_tags); i++)
        if (name_has(name, ntsc_tags[i]))
            return C64_REGION_NTSC;
    return C64_REGION_PAL;
}

static c64_region_t chosen_region(void)
{
    if (video == VIDEO_PAL)
        return C64_REGION_PAL;
    if (video == VIDEO_NTSC)
        return C64_REGION_NTSC;
    return region_from_name(rom_file_name);
}

/* The machine, the SID's clock and retro-go's frame pacing all move together.
   c64_init() has to follow (reset_handler does it), because the raster
   counter and the CIA timers are in the old clock. */
static void apply_region(c64_region_t r)
{
    fps = (r == C64_REGION_NTSC) ? C64_NTSC_FPS : C64_PAL_FPS;
    c64_set_region(r);
    c64_sid_init(c64_clock_speed(), (float)C64_RATE);
    rg_system_set_tick_rate(fps);
}

/* ---------------------------------------------------------------------- */
/* The program image, and the file calls the patched KERNAL LOAD uses      */
/* ---------------------------------------------------------------------- */

static uint8_t *prg_image;
static size_t prg_size;
static size_t prg_pos;
static char prg_name[20];

extern "C" int c64_emu_read_joysticks(void)
{
    return joy_bits;
}

extern "C" const char *c64_emu_file_name(void)
{
    return prg_name;
}

extern "C" int c64_emu_file_open(const char *filename)
{
    (void)filename;
    prg_pos = 0;
    return prg_image ? 1 : 0;
}

extern "C" int c64_emu_file_size(const char *filename)
{
    (void)filename;
    return (int)prg_size;
}

extern "C" int c64_emu_file_read(void *buffer, int size)
{
    if (!prg_image || size <= 0)
        return 0;
    size_t len = (size_t)size;
    if (prg_pos + len > prg_size)
        len = prg_size - prg_pos;
    memcpy(buffer, prg_image + prg_pos, len);
    prg_pos += len;
    return (int)len;
}

extern "C" void c64_emu_file_close(void)
{
    prg_pos = 0;
}

/* ---------------------------------------------------------------------- */
/* Keys sent from the options menu                                        */
/* ---------------------------------------------------------------------- */

typedef struct
{
    uint8_t hid;
    const char *name;
} c64_named_key_t;

static c64_named_key_t named_keys[26 + 10 + 7];
static int named_keys_count;
static int named_key_sel;

static void build_named_keys(void)
{
    static char letters[26][2];
    static char digits[10][2];
    int n = 0;
    for (int i = 0; i < 26; i++)
    {
        letters[i][0] = (char)('A' + i);
        letters[i][1] = 0;
        named_keys[n].hid = (uint8_t)(C64_KEY_A + i);
        named_keys[n].name = letters[i];
        n++;
    }
    for (int i = 0; i < 10; i++)
    {
        digits[i][0] = (char)('0' + i);
        digits[i][1] = 0;
        /* HID puts 1..9 at 0x1E..0x26 and 0 after them, at 0x27. */
        named_keys[n].hid = (uint8_t)(i == 0 ? C64_KEY_0 : C64_KEY_1 + i - 1);
        named_keys[n].name = digits[i];
        n++;
    }
    named_keys[n++] = c64_named_key_t{C64_KEY_SPACE, "SPACE"};
    named_keys[n++] = c64_named_key_t{C64_KEY_RETURN, "RETURN"};
    named_keys[n++] = c64_named_key_t{C64_KEY_RUNSTOP, "RUN/STOP"};
    named_keys[n++] = c64_named_key_t{C64_KEY_F1, "F1"};
    named_keys[n++] = c64_named_key_t{C64_KEY_F3, "F3"};
    named_keys[n++] = c64_named_key_t{C64_KEY_F5, "F5"};
    named_keys[n++] = c64_named_key_t{C64_KEY_F7, "F7"};
    named_keys_count = n;
}

/* A key picked in the menu is typed by the frame loop once the menu is gone:
   held for a few frames, then released, so the KERNAL's matrix scan sees it
   exactly as it would see a finger. */
#define KEY_QUEUE_SIZE 16
#define KEY_DOWN_FRAMES 4
#define KEY_UP_FRAMES 3

static uint8_t key_queue[KEY_QUEUE_SIZE];
static int key_queue_head, key_queue_tail;
static uint8_t key_held;
static int key_frames;
static bool key_is_down;

static void key_queue_push(uint8_t hid)
{
    const int next = (key_queue_tail + 1) % KEY_QUEUE_SIZE;
    if (next == key_queue_head)
        return; /* full: drop it rather than overwrite what is waiting */
    key_queue[key_queue_tail] = hid;
    key_queue_tail = next;
}

static uint8_t key_queue_tick(void)
{
    if (key_frames > 0)
    {
        key_frames--;
        return key_is_down ? key_held : 0;
    }
    if (key_is_down)
    {
        key_is_down = false;
        key_frames = KEY_UP_FRAMES;
        return 0;
    }
    if (key_queue_head != key_queue_tail)
    {
        key_held = key_queue[key_queue_head];
        key_queue_head = (key_queue_head + 1) % KEY_QUEUE_SIZE;
        key_is_down = true;
        key_frames = KEY_DOWN_FRAMES;
        return key_held;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Options menu                                                           */
/* ---------------------------------------------------------------------- */

static rg_gui_event_t joystick_port_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT || event == RG_DIALOG_ENTER)
        c64_joystick_port(c64_joystick_port_get() == 2 ? 1 : 2);
    sprintf(option->value, "%d", c64_joystick_port_get());
    return RG_DIALOG_VOID;
}

static rg_gui_event_t type_key_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV)
        named_key_sel = (named_key_sel + named_keys_count - 1) % named_keys_count;
    else if (event == RG_DIALOG_NEXT)
        named_key_sel = (named_key_sel + 1) % named_keys_count;
    else if (event == RG_DIALOG_ENTER)
        key_queue_push(named_keys[named_key_sel].hid);
    sprintf(option->value, "%s", named_keys[named_key_sel].name);
    return RG_DIALOG_VOID;
}

static rg_gui_event_t video_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT || event == RG_DIALOG_ENTER)
    {
        const int step = (event == RG_DIALOG_PREV) ? 2 : 1;
        video = (enum video_choice)((video + step) % 3);
        rg_settings_set_number(NS_APP, SETTING_VIDEO, video);
        /* Done at the top of the frame loop: it restarts the machine. */
        region_change_pending = true;
    }

    if (video == VIDEO_PAL)
        strcpy(option->value, "PAL");
    else if (video == VIDEO_NTSC)
        strcpy(option->value, "NTSC");
    else
        sprintf(option->value, "Auto (%s)", chosen_region() == C64_REGION_NTSC ? "NTSC" : "PAL");
    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = rg_gui_option_t{0, _("Video"), (char *)"-", RG_DIALOG_FLAG_NORMAL, &video_cb};
    *dest++ = rg_gui_option_t{0, _("Joystick port"), (char *)"-", RG_DIALOG_FLAG_NORMAL, &joystick_port_cb};
    *dest++ = rg_gui_option_t{0, _("Type key"), (char *)"-", RG_DIALOG_FLAG_NORMAL, &type_key_cb};
    *dest++ = rg_gui_option_t RG_DIALOG_END;
}

/* ---------------------------------------------------------------------- */
/* retro-go handlers                                                      */
/* ---------------------------------------------------------------------- */

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(update, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    const size_t size = c64_state_size();
    uint8_t *buf = (uint8_t *)rg_alloc(size, MEM_SLOW);
    bool ok = buf && c64_state_save(buf, size);
    FILE *fp = ok ? fopen(filename, "wb") : NULL;
    ok = fp && fwrite(buf, 1, size, fp) == size;
    if (fp)
        fclose(fp);
    free(buf);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    const size_t size = c64_state_size();
    uint8_t *buf = (uint8_t *)rg_alloc(size, MEM_SLOW);
    FILE *fp = fopen(filename, "rb");
    bool ok = buf && fp && fread(buf, 1, size, fp) == size && c64_state_load(buf, size);
    if (fp)
        fclose(fp);
    free(buf);
    return ok;
}

static void event_handler(int event, void *arg)
{
    (void)arg;
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(update, 0);
}

/* ---------------------------------------------------------------------- */
/* ROMs                                                                   */
/* ---------------------------------------------------------------------- */

static void load_rom_or_exit(const char *name, unsigned char *dest, size_t size)
{
    char path[RG_PATH_MAX];
    snprintf(path, sizeof(path), RG_BASE_PATH_BIOS "/c64/%s", name);

    FILE *fp = fopen(path, "rb");
    const bool ok = fp && fread(dest, 1, size, fp) == size;
    if (fp)
        fclose(fp);
    if (!ok)
    {
        RG_LOGE("C64 ROM missing or too short: %s (%d bytes expected)", path, (int)size);
        rg_gui_alert(_("ROM missing"), path);
        rg_system_exit();
    }
    RG_LOGI("C64 ROM %s loaded (%d bytes)", path, (int)size);
}

/* ---------------------------------------------------------------------- */
/* .d64: the first program file of the disk                               */
/* ---------------------------------------------------------------------- */

#define D64_SIZE_35 174848
#define D64_SIZE_35_ERR 175531
#define D64_SIZE_40 196608
#define D64_SIZE_40_ERR 197376

static bool is_d64_size(size_t size)
{
    return size == D64_SIZE_35 || size == D64_SIZE_35_ERR || size == D64_SIZE_40 || size == D64_SIZE_40_ERR;
}

/* Byte offset of a sector in the image. Tracks are numbered from 1, sectors
   from 0, and the number of sectors per track falls off in four zones. */
static long d64_offset(int track, int sector)
{
    static const struct
    {
        int first, last, sectors;
    } zones[] = {{1, 17, 21}, {18, 24, 19}, {25, 30, 18}, {31, 40, 17}};

    if (track < 1 || track > 40)
        return -1;
    long offset = 0;
    for (int t = 1; t < track; t++)
    {
        for (size_t z = 0; z < RG_COUNT(zones); z++)
            if (t >= zones[z].first && t <= zones[z].last)
                offset += zones[z].sectors;
    }
    int sectors_here = 0;
    for (size_t z = 0; z < RG_COUNT(zones); z++)
        if (track >= zones[z].first && track <= zones[z].last)
            sectors_here = zones[z].sectors;
    if (sector < 0 || sector >= sectors_here)
        return -1;
    return (offset + sector) * 256;
}

/* Walk the sector chain of a file and return it as a .prg image (load address
   first, as the two first data bytes already are). */
static uint8_t *d64_read_chain(const uint8_t *image, size_t image_size, int track, int sector, size_t *out_size)
{
    const size_t max_size = 256 * 1024;
    uint8_t *out = (uint8_t *)rg_alloc(max_size, MEM_SLOW);
    if (!out)
        return NULL;

    size_t len = 0;
    int guard = 0;
    while (track >= 1 && guard++ < 1000)
    {
        const long offset = d64_offset(track, sector);
        if (offset < 0 || (size_t)offset + 256 > image_size)
            break;
        const uint8_t *s = image + offset;
        const int next_track = s[0];
        const int next_sector = s[1];
        /* On the last sector, s[1] is the number of bytes used, counting the
           two link bytes. */
        const size_t chunk = next_track ? 254 : (next_sector >= 2 ? (size_t)next_sector - 1 : 0);
        if (len + chunk > max_size)
            break;
        memcpy(out + len, s + 2, chunk);
        len += chunk;
        if (!next_track)
            break;
        track = next_track;
        sector = next_sector;
    }

    if (len < 3)
    {
        free(out);
        return NULL;
    }
    *out_size = len;
    return out;
}

static uint8_t *d64_first_program(const uint8_t *image, size_t image_size, size_t *out_size, char *name,
                                  size_t name_size)
{
    int track = 18, sector = 1;
    int guard = 0;

    while (track >= 1 && guard++ < 64)
    {
        const long offset = d64_offset(track, sector);
        if (offset < 0 || (size_t)offset + 256 > image_size)
            return NULL;
        const uint8_t *s = image + offset;

        for (int i = 0; i < 8; i++)
        {
            const uint8_t *e = s + 2 + i * 32;
            const uint8_t type = e[0];
            if ((type & 0x0F) != 0x02) /* not PRG */
                continue;
            if (!(type & 0x80)) /* not properly closed */
                continue;

            size_t n = 0;
            for (int c = 0; c < 16 && n + 1 < name_size; c++)
            {
                const uint8_t ch = e[3 + c];
                if (ch == 0xA0 || ch == 0)
                    break;
                name[n++] = (char)((ch >= 0x41 && ch <= 0x5A) ? ch : (ch >= 0xC1 && ch <= 0xDA) ? ch - 0x80 : ch);
            }
            name[n] = 0;

            RG_LOGI("D64: first program \"%s\" at track %d sector %d", name, e[1], e[2]);
            return d64_read_chain(image, image_size, e[1], e[2], out_size);
        }

        track = s[0];
        sector = s[1];
    }
    return NULL;
}

/* ---------------------------------------------------------------------- */
/* Loading what the launcher picked                                       */
/* ---------------------------------------------------------------------- */

static void load_game(const char *rom_path)
{
    void *data;
    size_t size;

    if (rg_extension_match(rom_path, "zip"))
    {
        if (!rg_storage_unzip_file(rom_path, NULL, &data, &size, 0))
            RG_PANIC("ROM file unzipping failed!");
    }
    else if (!rg_storage_read_file(rom_path, &data, &size, 0))
        RG_PANIC("ROM load failed!");

    const char *basename = strrchr(rom_path, '/');
    basename = basename ? basename + 1 : rom_path;
    snprintf(prg_name, sizeof(prg_name), "%s", basename);

    /* Inside a .zip we cannot ask for the extension (retro-go hands us the
       first file whatever it is called), so a disk image is recognised by its
       size -- the only four a .d64 ever has. */
    if (rg_extension_match(rom_path, "d64") || is_d64_size(size))
    {
        char name[20] = "";
        size_t prog_size = 0;
        uint8_t *prog = d64_first_program((const uint8_t *)data, size, &prog_size, name, sizeof(name));
        free(data);
        if (!prog)
        {
            rg_gui_alert(_("Not supported"), _("No program file found in this disk image."));
            rg_system_exit();
        }
        if (name[0])
            snprintf(prg_name, sizeof(prg_name), "%s", name);
        prg_image = prog;
        prg_size = prog_size;
    }
    else
    {
        if (size < 3)
        {
            rg_gui_alert(_("Not supported"), _("This file is too short to be a C64 program."));
            rg_system_exit();
        }
        /* rg_storage_read_file's buffer may be internal RAM, which is scarce
           here: move the image to PSRAM, it is only read at start-up. */
        prg_image = (uint8_t *)rg_alloc(size, MEM_SLOW);
        if (prg_image)
        {
            memcpy(prg_image, data, size);
            prg_size = size;
            free(data);
        }
        else
        {
            prg_image = (uint8_t *)data;
            prg_size = size;
        }
    }

    RG_LOGI("C64 program \"%s\": %d bytes, load address $%04X", prg_name, (int)prg_size,
            prg_image[0] | (prg_image[1] << 8));
}

/* ---------------------------------------------------------------------- */
/* Main                                                                   */
/* ---------------------------------------------------------------------- */

static bool reset_handler(bool hard)
{
    (void)hard;
    c64_sid_reset();
    c64_init();
    phase = BOOTING;
    phase_frames = 0;
    return true;
}

extern "C" void c64_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .options = &options_handler,
    };

    app = rg_system_reinit(C64_RATE, &handlers, NULL);

    /* 150 KB: internal RAM has nothing like that left once the three cores'
       static buffers are in, and the VIC never writes here directly anyway
       (it draws into c64_line_buffer, see c64_display.h). */
    update = rg_surface_create(C64_SCREEN_WIDTH, C64_SCREEN_HEIGHT, RG_PIXEL_565_BE, MEM_SLOW);
    if (!update)
        RG_PANIC("Display surface allocation failed!");
    c64_framebuffer = (uint16_t *)update->data;

    load_rom_or_exit("kernal.rom", rom_kernal, C64_ROM_KERNAL_SIZE);
    load_rom_or_exit("basic.rom", rom_basic, C64_ROM_BASIC_SIZE);
    load_rom_or_exit("chargen.rom", rom_characters, C64_ROM_CHARGEN_SIZE);

    if (!c64_patch_kernal())
        RG_LOGW("kernal.rom has no JMP at $FFD5/$FFD8: LOAD from inside a program will not work");

    load_game(app->romPath);

    build_named_keys();

    rom_file_name = rg_basename(app->romPath);
    video = (enum video_choice)rg_settings_get_number(NS_APP, SETTING_VIDEO, VIDEO_AUTO);
    apply_region(chosen_region());
    c64_joystick_port(2);
    c64_init();

    RG_LOGI("C64 started: %s, %d raster lines, %d cycles/frame, %d fps, audio %d Hz",
            c64_region_name(), c64_lines_per_frame(), c64_cycles_per_frame(), fps, C64_RATE);

    app->frameskip = -1;

    static int16_t mono[C64_MAX_SAMPLES];
    static rg_audio_sample_t stereo[C64_MAX_SAMPLES];
    int sample_frac = 0;
    int skipFrames = 0;

    /* Autostart: wait for READY., then put the program in RAM and push RUN
       (or SYS, for a program that does not live at the start of BASIC) into
       the KERNAL's keyboard buffer. */
    uint32_t previous = 0;

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        /* Resuming a save state: the program is already in the machine's RAM,
           so the autostart must not run over it. */
        if (rg_emu_load_state(app->saveSlot))
            phase = PLAYING;
    }

    while (true)
    {
        if (region_change_pending)
        {
            region_change_pending = false;
            apply_region(chosen_region());
            sample_frac = 0;
            reset_handler(true);
            RG_LOGI("C64 now %s: %d raster lines, %d cycles/frame, %d fps", c64_region_name(),
                    c64_lines_per_frame(), c64_cycles_per_frame(), fps);
        }

        joystick = rg_input_read_gamepad();

        if (joystick & RG_KEY_MENU)
            rg_gui_game_menu();
        else if (joystick & RG_KEY_OPTION)
            rg_gui_options_menu();

        const uint32_t pressed = joystick & ~previous;
        previous = joystick;

        if (pressed & RG_KEY_L)
        {
            c64_joystick_port(c64_joystick_port_get() == 2 ? 1 : 2);
            RG_LOGI("C64 joystick now in port %d", c64_joystick_port_get());
            rg_gui_alert(_("Joystick"), c64_joystick_port_get() == 2 ? "Port 2" : "Port 1");
        }

        /* The d-pad and the fire buttons always speak as port 2; which
           physical port the core puts them on is c64_joystick_port()'s job. */
        joy_bits = 0;
        if (joystick & RG_KEY_UP)
            joy_bits |= C64_JOY2_UP;
        if (joystick & RG_KEY_DOWN)
            joy_bits |= C64_JOY2_DOWN;
        if (joystick & RG_KEY_LEFT)
            joy_bits |= C64_JOY2_LEFT;
        if (joystick & RG_KEY_RIGHT)
            joy_bits |= C64_JOY2_RIGHT;
        if (joystick & (RG_KEY_A | RG_KEY_B))
            joy_bits |= C64_JOY2_FIRE;

        uint8_t key = 0;
        if (joystick & RG_KEY_START)
            key = C64_KEY_SPACE;
        else if (joystick & RG_KEY_SELECT)
            key = C64_KEY_RUNSTOP;
        else if (joystick & RG_KEY_X)
            key = C64_KEY_F1;
        else if (joystick & RG_KEY_Y)
            key = C64_KEY_F3;
        else if (joystick & RG_KEY_R)
            key = C64_KEY_RETURN;
        if (!key)
            key = key_queue_tick();
        c64_key_set(key);

        const int64_t startTime = rg_system_timer();
        const bool drawFrame = skipFrames == 0;
        bool slowFrame = false;

        const int lines = c64_lines_per_frame();
        for (int i = 0; i < lines; i++)
            c64_run_line();

        phase_frames++;
        if (phase == BOOTING && phase_frames > 40 && (c64_basic_ready() || phase_frames > 400))
        {
            const uint16_t addr = c64_inject_program(prg_image, prg_size);
            char command[12];
            if (addr == 0x0801)
                snprintf(command, sizeof(command), "RUN\r");
            else
                snprintf(command, sizeof(command), "SYS%u\r", (unsigned)addr);
            c64_type_petscii(command);
            RG_LOGI("C64 autostart: injected at $%04X, typed \"%s\"", addr, addr == 0x0801 ? "RUN" : command);
            phase = STARTING;
            phase_frames = 0;
        }
        else if (phase == STARTING && phase_frames > 10)
        {
            phase = PLAYING;
            /* The image is only needed by a LOAD from inside the program; we
               keep it, it lives in PSRAM. */
        }

        if (drawFrame)
        {
            slowFrame = !rg_display_sync(false);
            rg_display_submit(update, 0);
        }

        /* 32000 Hz out whatever the region: only how many samples a frame is
           worth changes, 640 on PAL and 533 or 534 on NTSC. */
        sample_frac += C64_RATE % fps;
        const int samples = C64_RATE / fps + (sample_frac >= fps ? 1 : 0);
        if (sample_frac >= fps)
            sample_frac -= fps;
        c64_sid_render(mono, samples, c64_cycles_per_frame());
        for (int i = 0; i < samples; i++)
            stereo[i].left = stereo[i].right = mono[i];

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(stereo, samples);

        if (skipFrames == 0)
        {
            const int elapsed = rg_system_timer() - startTime;
            if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > app->frameTime + 1500)
                skipFrames = 1;
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
    }
}
