/*
    Commodore 64 core on retro-go: the shim that replaces MCUME's emuapi.h.

    Everything declared here is implemented by the front end,
    retro-home/main/main_c64.cpp. Only what the C64 core actually calls is
    kept: the two joysticks, and the file reads the patched KERNAL LOAD needs.

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_EMUAPI_H_
#define C64_EMUAPI_H_

#include <stdint.h>

#define HAS_SND 1

/* Joystick bits returned by c64_emu_read_joysticks(), read by cia1PORTA/cia1PORTB. */
#define C64_JOY1_UP    0x0001
#define C64_JOY1_DOWN  0x0002
#define C64_JOY1_LEFT  0x0004
#define C64_JOY1_RIGHT 0x0008
#define C64_JOY1_FIRE  0x0010
#define C64_JOY2_UP    0x0020
#define C64_JOY2_DOWN  0x0040
#define C64_JOY2_LEFT  0x0080
#define C64_JOY2_RIGHT 0x0100
#define C64_JOY2_FIRE  0x0200

#ifdef __cplusplus
extern "C" {
#endif

/* Joystick state, polled by the CIA1 port handlers every read. */
int c64_emu_read_joysticks(void);

/* The program image the launcher picked: a .prg, or the first file of a .d64,
   already unpacked in memory. These back the patched KERNAL LOAD so that a
   program doing LOAD"...",8,1 gets the same image again. */
int c64_emu_file_open(const char *filename);
int c64_emu_file_size(const char *filename);
int c64_emu_file_read(void *buffer, int size);
void c64_emu_file_close(void);

/* The name shown for that image (and used when LOAD is given no filename). */
const char *c64_emu_file_name(void);

#ifdef __cplusplus
}
#endif

#endif
