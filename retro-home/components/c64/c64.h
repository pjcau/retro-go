/*
    Commodore 64 core on retro-go: the interface the front end
    (retro-home/main/main_c64.cpp) uses. Replaces MCUME's c64.h.

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_H_
#define C64_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "c64_display.h"
#include "c64_roms.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Put the two trap opcodes into the KERNAL that the LOAD/SAVE patches of
   c64_patches.cpp hook: $FFD5 and $FFD8, whose JMP ($4C) becomes an illegal
   opcode the 6502 core traps. MCUME shipped a KERNAL with these already in
   it; ours comes from the SD card, so it has to be patched after loading.
   Returns false if the image does not have a JMP at both vectors -- then
   LOAD from inside a program will not work, but nothing else changes. */
bool c64_patch_kernal(void);

/* Reset the machine. The three ROM arrays of c64_roms.h must be filled,
   c64_patch_kernal() called and c64_sid_init() done first. */
void c64_init(void);

/* Run one raster line. One frame is C64_LINES_PER_FRAME of these. */
void c64_run_line(void);

/* Raster lines per frame (312 for PAL) and the emulated CPU cycles they take,
   so the front end can charge the right number of cycles to the SID. */
int c64_lines_per_frame(void);
int c64_cycles_per_frame(void);

/* The raster line the VIC is on, 0 .. c64_lines_per_frame()-1. */
int c64_raster_line(void);

/* Keyboard. The core reads the C64's key matrix through CIA1, so a key is
   "held" until it is cleared. Keys are named by USB HID usage code, which is
   what MCUME's matrix table is indexed by (see C64_KEY_* below). */
void c64_key_set(uint8_t hid_code);
void c64_key_clear(void);
/* Modifiers, OR-ed: */
#define C64_MOD_LSHIFT   0x01
#define C64_MOD_RSHIFT   0x02
#define C64_MOD_CTRL     0x04
#define C64_MOD_COMMODORE 0x08
void c64_mod_set(uint8_t mods);

/* USB HID usage codes for the keys the front end needs by name. */
#define C64_KEY_A        0x04
#define C64_KEY_Z        0x1D
#define C64_KEY_1        0x1E
#define C64_KEY_0        0x27
#define C64_KEY_RETURN   0x28
#define C64_KEY_RUNSTOP  0x29 /* HID Escape */
#define C64_KEY_SPACE    0x2C
#define C64_KEY_F1       0x3A
#define C64_KEY_F3       0x3C
#define C64_KEY_F5       0x3E
#define C64_KEY_F7       0x40

/* Joystick, as the front end hands it to the core every CIA1 port read.
   Bits are the C64_JOY2_* of c64_emuapi.h; which physical port they land on
   depends on c64_joystick_port(). */
void c64_joystick_port(int port); /* 1 or 2, default 2 */
int c64_joystick_port_get(void);

/* Put a string into the KERNAL's keyboard buffer ($0277, max 10 chars).
   PETSCII, '\r' for RETURN. This is how a program is started without the
   user typing: BASIC's idle loop reads it as if it had been typed. */
void c64_type_petscii(const char *text);

/* Load a program image (a .prg: 2 bytes of load address, then the data)
   straight into RAM and fix BASIC's pointers, so that RUN works. Returns the
   load address, or 0 if the image is unusable. */
uint16_t c64_inject_program(const uint8_t *image, size_t size);

/* True once the KERNAL has printed READY. and BASIC is waiting for input. */
bool c64_basic_ready(void);

/* Save states */
size_t c64_state_size(void);
bool c64_state_save(void *dest, size_t size);
bool c64_state_load(const void *src, size_t size);

#ifdef __cplusplus
}
#endif

#endif
