/*
    Commodore 64 core on retro-go: the SID.

    Replaces MCUME's reSID.h/reSID.cpp wrapper and output_dac.h. reSID itself
    (Dag Lem, GPL v2 or later) is in reSID/ unchanged, compiled through
    c64_sid.cpp; only the 6581 wave tables are linked in (the 8580 ones are
    not used by this build and would cost another 32 KB of flash).

    The reSID class stays out of this header on purpose: pla.cpp and the front
    end see plain C functions, so no core file has to parse reSID's headers.

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_SID_H_
#define C64_SID_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called once before the machine is reset. sample_rate is retro-go's audio
   rate, clock_rate the emulated C64's (CLOCKSPEED). */
void c64_sid_init(float clock_rate, float sample_rate);
void c64_sid_reset(void);

/* $D400-$D41F, from pla.cpp's r_sid/w_sid. */
uint8_t c64_sid_read(uint8_t reg);
void c64_sid_write(uint8_t reg, uint8_t value);

/* Render exactly `samples` mono 16-bit samples, charging `cycles` emulated
   C64 cycles to the SID. Any mismatch between the two is carried over to the
   next call, so the pitch does not drift. Always returns `samples`. */
int c64_sid_render(int16_t *buffer, int samples, int cycles);

/* Save states */
int c64_sid_state_size(void);
void c64_sid_state_save(void *dest);
void c64_sid_state_load(const void *src);

#ifdef __cplusplus
}
#endif

#endif
