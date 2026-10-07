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
#define NTSC (!PAL)

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

#if PAL == 1
#define CRYSTAL         17734475.0f
#define CLOCKSPEED      (CRYSTAL / 18.0f) // 985248.61 Hz
#define CYCLESPERRASTERLINE 63
#define LINECNT         312 // Rasterlines
#define VBLANK_FIRST    300
#define VBLANK_LAST     15
#else
#define CRYSTAL         14318180.0f
#define CLOCKSPEED      (CRYSTAL / 14.0f) // 1022727.14 Hz
#define CYCLESPERRASTERLINE 64
#define LINECNT         263 // Rasterlines
#define VBLANK_FIRST    13
#define VBLANK_LAST     40
#endif

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
