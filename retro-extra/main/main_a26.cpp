extern "C" {
#include "shared.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
}

// Atari 2600 — Stella (OtherCrashOverride's stella-odroid-go cut), esp32-emu-turbo 2026-09-14.
// The TIA renders 160 x (210..250) 8-bit palette frames; TIA audio at 31400 Hz.

#include "bspf.hxx"
#include "Console.hxx"
#include "Cart.hxx"
#include "Props.hxx"
#include "PropsSet.hxx"
#include "MD5.hxx"
#include "Sound.hxx"
#include "SoundSDL.hxx"
#include "OSystem.hxx"
#include "Settings.hxx"
#include "TIA.hxx"
#include "Event.hxx"
#include "EventHandler.hxx"
#include "Switches.hxx"
#include "Control.hxx"

#define A26_WIDTH 160
#define A26_MAX_HEIGHT 256
#define A26_SAMPLE_RATE 31400

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

static Console *console = 0;
static Cartridge *cartridge = 0;
static Settings *settings = 0;
static OSystem *osystem = 0;
static uint32_t tiaSamplesPerFrame;
static int videoHeight = 210;
bool RenderFlag = true; // TIA.cpp skips the pixel writes when false (frames not drawn)

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool reset_handler(bool hard)
{
    if (console)
        console->system().reset();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

extern "C" void a26_main(void)
{
    const rg_handlers_t handlers = {
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_reinit(A26_SAMPLE_RATE, &handlers, NULL);

    // 8-bit palette frame: the TIA writes its own buffer, we copy the visible height.
    updates[0] = rg_surface_create(A26_WIDTH, A26_MAX_HEIGHT, RG_PIXEL_PAL565_BE, MEM_FAST);
    currentUpdate = updates[0];

    void *rom_data;
    size_t rom_size;
    if (!rg_storage_read_file(app->romPath, &rom_data, &rom_size, 0))
        RG_PANIC("ROM load failed!");

    string cartMD5 = MD5((uInt8 *)rom_data, (uInt32)rom_size);
    osystem = new OSystem();
    Properties props;
    osystem->propSet().getMD5(cartMD5, props);
    string cartType = props.get(Cartridge_Type);
    string cartId;
    settings = new Settings(osystem);
    settings->setValue("romloadcount", false);
    cartridge = Cartridge::create((const uInt8 *)rom_data, (uInt32)rom_size, cartMD5, cartType, cartId, *osystem, *settings);
    if (!cartridge)
        RG_PANIC("Stella: failed to load cartridge");

    console = new Console(osystem, cartridge, props);
    osystem->myConsole = console;
    console->initializeVideo();
    console->initializeAudio();

    TIA &tia = console->tia();
    videoHeight = tia.height();
    if (videoHeight > A26_MAX_HEIGHT)
        videoHeight = A26_MAX_HEIGHT;
    currentUpdate->height = videoHeight;
    RG_LOGI("TIA %dx%d, %.2f fps", (int)tia.width(), (int)tia.height(), console->getFramerate());

    const uint32_t *palette = console->getPalette(0);
    for (int i = 0; i < 256; ++i)
    {
        uint32_t c = palette[i];
        uint16_t rgb565 = (((c >> 16) & 0xff) << 8 & 0xf800) | (((c >> 8) & 0xff) << 3 & 0x07e0) | ((c & 0xff) >> 3);
        currentUpdate->palette[i] = (rgb565 >> 8) | (rgb565 << 8);
    }

    tiaSamplesPerFrame = (uint32_t)(A26_SAMPLE_RATE / console->getFramerate());
    int16_t *mono = (int16_t *)malloc(tiaSamplesPerFrame * sizeof(int16_t) + 64);
    rg_audio_sample_t *stereo = (rg_audio_sample_t *)malloc(tiaSamplesPerFrame * sizeof(rg_audio_sample_t) + 64);

    rg_system_set_tick_rate((int)(console->getFramerate() + 0.5f));
    app->frameskip = 1;
    int skipFrames = 0;

    Event &ev = osystem->eventHandler().event();

    while (true)
    {
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & RG_KEY_MENU)
            rg_gui_game_menu();
        else if (joystick & RG_KEY_OPTION)
            rg_gui_options_menu();

        int64_t startTime = rg_system_timer();
        bool drawFrame = skipFrames == 0;
        bool slowFrame = false;

        ev.set(Event::JoystickZeroUp, (joystick & RG_KEY_UP) != 0);
        ev.set(Event::JoystickZeroDown, (joystick & RG_KEY_DOWN) != 0);
        ev.set(Event::JoystickZeroLeft, (joystick & RG_KEY_LEFT) != 0);
        ev.set(Event::JoystickZeroRight, (joystick & RG_KEY_RIGHT) != 0);
        ev.set(Event::JoystickZeroFire, (joystick & (RG_KEY_A | RG_KEY_B)) != 0);
        ev.set(Event::ConsoleSelect, (joystick & RG_KEY_SELECT) != 0);
        ev.set(Event::ConsoleReset, (joystick & RG_KEY_START) != 0);
        ev.set(Event::ConsoleLeftDiffA, (joystick & RG_KEY_L) != 0);
        ev.set(Event::ConsoleRightDiffA, (joystick & RG_KEY_R) != 0);
        console->controller(Controller::Left).update();
        console->controller(Controller::Right).update();
        console->switches().update();

        RenderFlag = drawFrame;
        tia.update();

        if (drawFrame)
        {
            memcpy(currentUpdate->data, tia.currentFrameBuffer(), A26_WIDTH * videoHeight);
            slowFrame = !rg_display_sync(false);
            rg_display_submit(currentUpdate, 0);
        }

        SoundSDL *sound = (SoundSDL *)&osystem->sound();
        sound->processFragment(mono, tiaSamplesPerFrame);
        for (uint32_t i = 0; i < tiaSamplesPerFrame; i++)
            stereo[i].left = stereo[i].right = mono[i];

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(stereo, tiaSamplesPerFrame);

        if (skipFrames == 0)
        {
            int elapsed = rg_system_timer() - startTime;
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
