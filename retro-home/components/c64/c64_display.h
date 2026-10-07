/*
    Commodore 64 core on retro-go: what is left of MCUME's ili9341_t3dma.h.

    The VIC writes 16-bit pixels straight into the frame it is given. On the
    Teensy that was the display driver's DMA line buffer; here it is a
    retro-go surface (RG_PIXEL_565_BE, so the colours are byte-swapped once
    when the palette is installed -- see C64_SWAP16 / c64_vic_palette.h).

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_DISPLAY_H_
#define C64_DISPLAY_H_

#include <stdint.h>

/* The C64 picture: 320 px wide, 200 px of screen plus 20 px of border on top
   and bottom. Exactly the 320x240 the handheld's display wants. */
#define ILI9341_TFTWIDTH  320
#define ILI9341_TFTHEIGHT 240

#define C64_SCREEN_WIDTH  320
#define C64_SCREEN_HEIGHT 240

/* 200 lines of screen, the rest border, centred -- and the raster lines they
   correspond to. vic.cpp derives its own FIRSTDISPLAYLINE/LASTDISPLAYLINE
   from these, and so does the per-line copy in c64_machine.cpp. */
#define C64_BORDER_HEIGHT      ((C64_SCREEN_HEIGHT - 200) / 2)
#define C64_FIRST_DISPLAY_LINE (51 - C64_BORDER_HEIGHT)
#define C64_LAST_DISPLAY_LINE  (250 + C64_BORDER_HEIGHT)

#define RGBVAL16(r, g, b) ((((r) >> 3) & 0x1f) << 11 | (((g) >> 2) & 0x3f) << 5 | (((b) >> 3) & 0x1f))

/* RGB565 in the display's byte order (retro-go's RG_PIXEL_565_BE) */
#define C64_SWAP16(x) ((uint16_t)((((uint16_t)(x)) >> 8) | (((uint16_t)(x)) << 8)))

#ifdef __cplusplus
extern "C" {
#endif

/* Set by the front end to the retro-go surface's pixel data, 320x240x16bpp.
   The surface lives in PSRAM: 153 KB of internal RAM is not available once
   the three cores' static buffers are in (the C64 alone keeps its 64 KB of
   RAM there). */
extern uint16_t *c64_framebuffer;

/* The VIC draws a whole raster line, several times over in places (border,
   then text, then border again), so it draws into this one internal-RAM line
   and c64_run_line() copies the finished line into the surface -- one
   sequential burst instead of 320 scattered writes into PSRAM. */
extern uint16_t c64_line_buffer[C64_SCREEN_WIDTH];

#ifdef __cplusplus
}
#endif

/* The line the VIC draws into. Replaces tft.getLineBuffer(); which line it is
   does not matter, c64_run_line() knows where to put it. */
#define C64_LINE_BUFFER(line) (c64_line_buffer)

#endif
