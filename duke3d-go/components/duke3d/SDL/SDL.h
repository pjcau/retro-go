#ifndef SDL_h_
#define SDL_h_
#include <rg_system.h>
#include <stdint.h>
#include <stdio.h>
#include "SDL_input.h"
#include "SDL_stdinc.h"
#include "SDL_endian.h"
#include "SDL_audio.h"
#include "SDL_video.h"
#include "SDL_event.h"
#include "SDL_system.h"
#include "SDL_scancode.h"

typedef int SDLMod;
//typedef int SDL_Joystick;

#define SDLK_FIRST 0
#define SDLK_LAST 1024

/* Keys the gamepad never sends, on free slots at the top of display.c's
 * scancodes[SDLK_LAST] like NUMLOCK/SCROLLOCK below. The keypad names used
 * to alias the arrows, RETURN and BACKSLASH, so "scancodes[SDLK_KP0] = 0x52"
 * overwrote RETURN: START/Enter reached the game as Insert and no menu
 * entry could be selected. */
#define SDLK_KP0 1001
#define SDLK_KP1 1002
#define SDLK_KP2 1003
#define SDLK_KP3 1004
#define SDLK_KP4 1005
#define SDLK_KP5 1006
#define SDLK_KP6 1007
#define SDLK_KP7 1008
#define SDLK_KP8 1009
#define SDLK_KP9 1010
#define SDLK_PRINT 1011

#define AUDIO_S16SYS 16
#define AUDIO_S8 8

#define SDL_BUTTON_LEFT 0
#define SDL_BUTTON_RIGHT 1
#define SDL_BUTTON_MIDDLE 2
#define SDLK_NUMLOCK 999
#define SDLK_SCROLLOCK 1000

#define SDL_strlcpy strlcpy

#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u  /**< SDL_INIT_VIDEO implies SDL_INIT_EVENTS */
#define SDL_INIT_JOYSTICK       0x00000200u  /**< SDL_INIT_JOYSTICK implies SDL_INIT_EVENTS */
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u  /**< SDL_INIT_GAMECONTROLLER implies SDL_INIT_JOYSTICK */
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_SENSOR         0x00008000u
#define SDL_INIT_NOPARACHUTE    0x00100000u  /**< compatibility; this flag is ignored. */
#define SDL_INIT_EVERYTHING ( \
                SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS | \
                SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR \
            )

#endif
