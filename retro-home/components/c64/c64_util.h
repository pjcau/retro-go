/*
    Commodore 64 core on retro-go: what is left of MCUME's util.h/util.cpp.

    The Teensy audio-clock and interrupt-list helpers are gone; only the cycle
    counter the VIC uses to measure how long a frame took survives (ccount is
    an LX7 register too, so the asm is unchanged).

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_UTIL_H_
#define C64_UTIL_H_

#include "c64_machine.h"

static inline unsigned get_ccount(void)
{
    unsigned r;
    asm volatile("rsr %0, ccount" : "=r"(r));
    return r;
}

static inline unsigned fbmillis(void) { return (unsigned)(get_ccount() * (1000.0 / F_CPU)); }
static inline unsigned fbmicros(void) { return (unsigned)(get_ccount() * (1000000.0 / F_CPU)); }
static inline unsigned fbnanos(void) { return (unsigned)(get_ccount() * (1000000000.0 / F_CPU)); }

/* No-ops kept so cpu.cpp / vic.cpp need no edit: retro-go owns the audio
   stream and there is no event responder to disable. */
static inline void enableCycleCounter(void) {}
static inline void disableEventResponder(void) {}
static inline void setAudioOff(void) {}
static inline void setAudioOn(void) {}

#endif
