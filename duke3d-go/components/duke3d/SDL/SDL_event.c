#include "SDL_event.h"
#include <rg_system.h>
#define NELEMS(x)  (sizeof(x) / sizeof((x)[0]))
int keyMode = 1;

//Mappings from buttons to keys
// static const GPIOKeyMap keymap[2][6]={{
// // Game
// 	{CONFIG_HW_BUTTON_PIN_NUM_BUTTON1, SDL_SCANCODE_LCTRL, SDLK_LCTRL},
// 	{CONFIG_HW_BUTTON_PIN_NUM_SELECT, SDL_SCANCODE_SPACE, SDLK_SPACE},
// 	{CONFIG_HW_BUTTON_PIN_NUM_VOL, SDL_SCANCODE_CAPSLOCK, SDLK_CAPSLOCK},
// 	{CONFIG_HW_BUTTON_PIN_NUM_MENU, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
// 	{CONFIG_HW_BUTTON_PIN_NUM_START, SDL_SCANCODE_A, SDLK_a},
// 	{CONFIG_HW_BUTTON_PIN_NUM_BUTTON2, SDL_SCANCODE_LALT, SDLK_LALT},
// },
// // Menu
// {
// 	{CONFIG_HW_BUTTON_PIN_NUM_BUTTON1, SDL_SCANCODE_SPACE, SDLK_SPACE},
// 	{CONFIG_HW_BUTTON_PIN_NUM_BUTTON2, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
// 	{CONFIG_HW_BUTTON_PIN_NUM_VOL, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
// 	{CONFIG_HW_BUTTON_PIN_NUM_MENU, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
// 	{CONFIG_HW_BUTTON_PIN_NUM_START, SDL_SCANCODE_A, SDLK_a},
// 	{CONFIG_HW_BUTTON_PIN_NUM_SELECT, SDL_SCANCODE_LALT, SDLK_LALT},
// }};

/* retro-go gamepad -> SDL key events. Duke3D reads keyboard scancodes
 * (Game/config.c defaults): arrows move/turn, LCTRL fire, SPACE open,
 * LALT strafe, LSHIFT run, ENTER/ESC menus, ',' '.' weapon prev/next. */
static const struct { uint32_t key; SDL_Scancode sc; SDL_Keycode sym; } keymap[] = {
    {RG_KEY_UP,     SDL_SCANCODE_UP,     SDLK_UP},
    {RG_KEY_DOWN,   SDL_SCANCODE_DOWN,   SDLK_DOWN},
    {RG_KEY_LEFT,   SDL_SCANCODE_LEFT,   SDLK_LEFT},
    {RG_KEY_RIGHT,  SDL_SCANCODE_RIGHT,  SDLK_RIGHT},
    {RG_KEY_A,      SDL_SCANCODE_LCTRL,  SDLK_LCTRL},   /* fire */
    {RG_KEY_B,      SDL_SCANCODE_SPACE,  SDLK_SPACE},   /* open / use */
    {RG_KEY_X,      SDL_SCANCODE_LALT,   SDLK_LALT},    /* strafe modifier */
    {RG_KEY_Y,      SDL_SCANCODE_LSHIFT, SDLK_LSHIFT},  /* run */
    {RG_KEY_L,      SDL_SCANCODE_COMMA,  SDLK_COMMA},   /* previous weapon */
    {RG_KEY_R,      SDL_SCANCODE_PERIOD, SDLK_PERIOD},  /* next weapon */
    {RG_KEY_START,  SDL_SCANCODE_RETURN, SDLK_RETURN},
    {RG_KEY_SELECT, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
};
static uint32_t last_keys = 0;
static int pending = 0;   /* index of the next mapping to check for a change */

int SDL_PollEvent(SDL_Event * event)
{
    uint32_t keys = rg_input_read_gamepad();
    if (keys & RG_KEY_MENU) /* START+SELECT: retro-go menu */
    {
        rg_gui_game_menu();
        keys = rg_input_read_gamepad();
    }
    uint32_t changed = keys ^ last_keys;
    if (!changed)
        return 0;
    for (int n = 0; n < (int)(sizeof(keymap) / sizeof(keymap[0])); n++)
    {
        int i = (pending + n) % (int)(sizeof(keymap) / sizeof(keymap[0]));
        if (changed & keymap[i].key)
        {
            int down = keys & keymap[i].key;
            memset(event, 0, sizeof(*event));
            event->type = down ? SDL_KEYDOWN : SDL_KEYUP;
            event->key.type = event->type;
            event->key.state = down ? SDL_PRESSED : SDL_RELEASED;
            event->key.keysym.scancode = keymap[i].sc;
            event->key.keysym.sym = keymap[i].sym;
            event->key.keysym.mod = 0;
            if (down) last_keys |= keymap[i].key; else last_keys &= ~keymap[i].key;
            pending = i + 1;
            return 1;
        }
    }
    last_keys = keys;
    return 0;
}

void inputInit(void)
{
}
