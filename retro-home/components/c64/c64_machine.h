/*
    Commodore 64 core: Frank Bösing's Teensy64 (Teensy64.h), as cut down for
    the ESP32 by Jean-Marc Harvengt in MCUME (MCUME_esp32/esp64, commit
    27f6b906). GPL v3 or later -- see LICENSE.md.

    Ported to retro-go: the Teensy / ILI9341_t3DMA / Arduino dependencies are
    gone; the platform calls live in retro-home/main/main_c64.cpp.
*/
#ifndef C64_MACHINE_H_
#define C64_MACHINE_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define F_CPU 240000000.0
#define F_BUS 240000000.0

#include "c64_settings.h"

#define VERSION "09"

#include <esp_timer.h>

static inline unsigned long millis(void) { return (unsigned long)(esp_timer_get_time() / 1000); }

#include "c64_display.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "c64_emuapi.h"
#ifdef __cplusplus
}
#endif

/* MCUME picked the video standard with a #if; here it is c64_set_region()
   (c64.h), so the numbers the VIC and the CPU run on are variables. The
   macro names are MCUME's, so that vic.cpp and cpu.cpp read as they did.

                      PAL 6569      NTSC 6567R8
     crystal          17.734475 MHz 14.318180 MHz
     CPU/VIC clock    /18 = 985249  /14 = 1022727 Hz
     cycles per line  63            65
     raster lines     312           263
     refresh          50.125 Hz     59.826 Hz          */
#ifdef __cplusplus
extern "C" {
#endif
extern int c64_rg_lines;        // raster lines per frame: 312 / 263
extern int c64_rg_line_cycles;  // CPU cycles per raster line: 63 / 65
extern int c64_rg_right_hblank; // of those, the right-hand blanking: 2 / 4
extern float c64_rg_clock;      // CPU/VIC clock in Hz
#ifdef __cplusplus
}
#endif

#define CLOCKSPEED          c64_rg_clock
#define CYCLESPERRASTERLINE c64_rg_line_cycles
#define RIGHTHBLANK         c64_rg_right_hblank
#define LINECNT             c64_rg_lines

/* Which of the 240 rows the handheld shows a raster line lands on, or -1 for
   a line that is not shown. The text window is raster 51..250 on both
   standards (the VIC compares RSEL against fixed line numbers), so the row
   is simply the line less C64_FIRST_DISPLAY_LINE -- except that NTSC does
   not have 240 lines below 51 - 20: its last 7 rows are raster 0..6, which
   is where its bottom border carries on after line 263.

   The counter runs 0..LINECNT, one more value than LINECNT, which is why the
   wrap adds LINECNT + 1. */
static inline int c64_display_row(int raster_line)
{
	int row = raster_line - C64_FIRST_DISPLAY_LINE;
	if (row < 0)
		row += LINECNT + 1;
	return ((unsigned)row < C64_SCREEN_HEIGHT) ? row : -1;
}

#define LINEFREQ        (CLOCKSPEED / CYCLESPERRASTERLINE) // Hz
#define REFRESHRATE     (LINEFREQ / LINECNT) // Hz
#define LINETIMER_DEFAULT_FREQ (1000000.0f / LINEFREQ)

#define MCU_C64_RATIO   ((float)F_CPU / CLOCKSPEED) // MCU cycles per C64 cycle
#define US_C64_CYCLE    (1000000.0f / CLOCKSPEED)   // Duration (us) of a C64 cycle

#define ISR_PRIORITY_RASTERLINE 255

/* The IEC bus is not emulated: no 1541, no serial lines. */
#define WRITE_ATN_CLK_DATA(value) {}
#define READ_CLK_DATA() (0)

#include "c64_sid.h"
#include "c64_cpu.h"

#endif
