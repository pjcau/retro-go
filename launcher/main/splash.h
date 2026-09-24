#pragma once

#include <stdint.h>

// Boot splash: a procedural pixel-art intro played once on a cold boot.
// Everything is generated in code (no image or sound assets) so it costs a few KB of flash.

#define SPLASH_WIDTH   240 // logical canvas, drawn 2x on the 480x320 panel
#define SPLASH_HEIGHT  160
#define SPLASH_FPS     30
#define SPLASH_FRAMES  150 // 5 s
#define SPLASH_RATE    32000

// Palette (PICO-8 16 colours) as 0xRRGGBB.
extern const uint32_t splash_palette[16];

// Render frame `frame` into `out` (SPLASH_WIDTH * SPLASH_HEIGHT palette indices).
void splash_render(uint8_t *out, int frame);

// Mono sample (-1..1) at sample index `n` of the jingle (SPLASH_RATE Hz).
float splash_sample(int n);

// Play the splash on the display + speaker; returns early if a button is pressed.
void splash_play(void);
