// "Arcade 3D": davidmonterocrespo24/esp32s3-arcade-3d, an OutRun-style racing
// game for Arduino + TFT_eSPI, run here through components/arcade3d/shim.
extern "C" {
#include "shared.h"
}
#include <Arduino.h>

static rg_app_t *app;
static rg_surface_t *frames[2];
static int frame_index;
static uint32_t buttons;
static int64_t frame_start;

void *a3d_alloc(size_t bytes)
{
    return rg_alloc(bytes, MEM_SLOW);
}

int64_t a3d_micros(void)
{
    return rg_system_timer();
}

uint32_t a3d_buttons(void)
{
    return buttons;
}

// One finished frame: copied to the surface the display is not showing, so
// the game can draw the next one straight away.
void a3d_present(const uint16_t *pixels, int width, int height)
{
    rg_surface_t *frame = frames[frame_index];
    memcpy(frame->data, pixels, (size_t)width * height * 2);
    rg_system_tick(rg_system_timer() - frame_start);
    rg_display_submit(frame, 0);
    frame_index ^= 1;

    uint32_t joystick = rg_input_read_gamepad();
    if (joystick & RG_KEY_MENU)
        rg_gui_game_menu();
    else if (joystick & RG_KEY_OPTION)
        rg_gui_options_menu();
    buttons = ((joystick & RG_KEY_LEFT) ? A3D_BTN_LEFT : 0) | ((joystick & RG_KEY_RIGHT) ? A3D_BTN_RIGHT : 0)
            | ((joystick & RG_KEY_B) ? A3D_BTN_BRAKE : 0) | ((joystick & RG_KEY_A) ? A3D_BTN_GAS : 0);
    frame_start = rg_system_timer();
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(frames[frame_index ^ 1], filename, width, height);
}

static bool reset_handler(bool hard)
{
    rg_system_restart();
    return true;
}

extern "C" void arcade3d_main(void)
{
    static const rg_handlers_t handlers = {
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
    };
    app = rg_system_reinit(AUDIO_SAMPLE_RATE, &handlers, NULL);
    app->tickRate = 60;

    frames[0] = rg_surface_create(320, 240, RG_PIXEL_565_LE, MEM_SLOW);
    frames[1] = rg_surface_create(320, 240, RG_PIXEL_565_LE, MEM_SLOW);
    if (!frames[0] || !frames[1])
        RG_PANIC("No memory for the frame buffers");

    frame_start = rg_system_timer();
    setup();
    while (true)
        loop();
}
