/*
	Copyright Frank Bösing, 2017

	This file is part of Teensy64.

    Teensy64 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Teensy64 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Teensy64.  If not, see <http://www.gnu.org/licenses/>.

    ---

    retro-go port of MCUME's c64.cpp (MCUME_esp32/esp64, commit 27f6b906):
    the machine's glue -- the CIA1 key matrix and joystick ports, the raster
    line step, and the bits of the C64 the front end needs to poke (keyboard
    buffer, program injection, save state).

    Gone with the Teensy: the USB-host keyboard, the OSD menu, the ILI9341
    DMA driver, the audio ISR and the "exact timing" IEC mode (nothing drives
    the IEC bus here, so it never engaged anyway).
*/

#include "c64.h"
#include "c64_cpu.h"
#include "c64_sid.h"

#include <stdlib.h>
#include <string.h>

/* The ROMs. MCUME compiled them in (roms.cpp); here they are RAM, filled by
   the front end from the SD card. See c64_roms.h. */
unsigned char rom_basic[C64_ROM_BASIC_SIZE];
unsigned char rom_kernal[C64_ROM_KERNAL_SIZE];
unsigned char rom_characters[C64_ROM_CHARGEN_SIZE];

/* Where the VIC draws. The front end points c64_framebuffer at its retro-go
   surface (in PSRAM); the VIC itself only ever touches the internal-RAM
   c64_line_buffer, which c64_run_line() flushes one line at a time. */
uint16_t *c64_framebuffer;
uint16_t c64_line_buffer[C64_SCREEN_WIDTH];

extern CONSTROM rarray_t PLA_READ[8];
extern CONSTROM warray_t PLA_WRITE[8];

/* ------------------------------------------------------------------------ */
/* Keyboard matrix                                                          */
/* ------------------------------------------------------------------------ */
/*
  MCUME's table, unchanged: keymatrixmap[0][hid] is the CIA1 PORT A mask and
  keymatrixmap[1][hid] the PORT B mask of the C64 key that sits where the USB
  HID usage code `hid` would be on a PC keyboard.
*/
static const uint8_t keymatrixmap[2][256] = {
  //Rows:
  // 0    1     2     3    4     5     6      7     8      9     A     B     C     D     E     F
  { 0x00, 0x00, 0x00, 0x00, 0x02, 0x08, 0x04, 0x04, 0x02, 0x04, 0x08, 0x08, 0x10, 0x10, 0x10, 0x20, //0x00
    0x10, 0x10, 0x10, 0x20, 0x80, 0x04, 0x02, 0x04, 0x08, 0x08, 0x02, 0x04, 0x08, 0x02, 0x80, 0x80, //0x10
    0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x01, 0x80, 0x01, 0x00, 0x80, 0x00, 0x00, 0x20, //0x20
    0x00, 0x00, 0x40, 0x20, 0x40, 0x00, 0x20, 0x20, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, //0x30
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x80, 0x01, //0x40
    0x00, 0x01, 0x00, 0x00, 0x40, 0x40, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x50
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x60
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x70
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x80
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x90
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xA0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xB0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xC0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xD0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xE0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x40, 0x02
  }, //0xF0
  //Columns:
  // 0    1     2     3    4     5     6      7     8      9     A     B     C     D     E     F
  { 0x00, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10, 0x04, 0x40, 0x20, 0x04, 0x20, 0x02, 0x04, 0x20, 0x04, //0x00
    0x10, 0x80, 0x40, 0x02, 0x40, 0x02, 0x20, 0x40, 0x40, 0x80, 0x02, 0x80, 0x02, 0x10, 0x01, 0x08, //0x10
    0x01, 0x08, 0x01, 0x08, 0x01, 0x08, 0x01, 0x08, 0x02, 0x80, 0x01, 0x00, 0x10, 0x00, 0x00, 0x40, //0x20
    0x00, 0x00, 0x20, 0x20, 0x04, 0x00, 0x80, 0x10, 0x00, 0x00, 0x10, 0x00, 0x20, 0x00, 0x40, 0x00, //0x30
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x40, 0x00, 0x00, 0x02, 0x04, //0x40
    0x00, 0x80, 0x00, 0x00, 0x80, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x50
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x60
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x70
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x80
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0x90
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xA0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xB0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xC0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xD0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //0xE0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x04, 0x10, 0x80
  }
}; //0xF0 

/* HID codes of the keys that are not in the table by position. */
#define HID_LSHIFT 0xFF
#define HID_RSHIFT 0xFE
#define HID_CTRL   0xFD
#define HID_CBM    0xFC

static uint8_t kbd_key;   /* HID code currently held, 0 = none */
static uint8_t kbd_mods;  /* C64_MOD_* */
static int joystick_port = 2;

void c64_key_set(uint8_t hid_code) { kbd_key = hid_code; }
void c64_key_clear(void) { kbd_key = 0; kbd_mods = 0; }
void c64_mod_set(uint8_t mods) { kbd_mods = mods; }

void c64_joystick_port(int port)
{
	joystick_port = (port == 1) ? 1 : 2;
	/* cia1PORTA reads the C64_JOY2_* bits when swapJoysticks is clear, which
	   is the C64's port 2 -- what most games use. */
	cpu.swapJoysticks = (joystick_port == 1) ? 1 : 0;
}

int c64_joystick_port_get(void) { return joystick_port; }

/* The modifier keys, applied to whichever port is being read. */
static inline uint8_t kbd_mod_mask(int table, uint8_t filter)
{
	const int other = table ^ 1;
	uint8_t v = 0;
	if ((kbd_mods & C64_MOD_LSHIFT) && (keymatrixmap[other][HID_LSHIFT] & filter))
		v |= keymatrixmap[table][HID_LSHIFT];
	if ((kbd_mods & C64_MOD_RSHIFT) && (keymatrixmap[other][HID_RSHIFT] & filter))
		v |= keymatrixmap[table][HID_RSHIFT];
	if ((kbd_mods & C64_MOD_CTRL) && (keymatrixmap[other][HID_CTRL] & filter))
		v |= keymatrixmap[table][HID_CTRL];
	if ((kbd_mods & C64_MOD_COMMODORE) && (keymatrixmap[other][HID_CBM] & filter))
		v |= keymatrixmap[table][HID_CBM];
	return v;
}

/*
  CIA1 $DC00 / $DC01. The joystick bits of a C64 control port are
  bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire, all active low.
  MCUME had left and right the other way round (it compensated in its own
  front end); they are correct here, so the front end maps the d-pad
  straight.
*/
uint8_t cia1PORTA(void)
{
	uint8_t v = ~cpu.cia1.R[0x02] | (cpu.cia1.R[0x00] & cpu.cia1.R[0x02]);

	const int keys = c64_emu_read_joysticks();
	if (!cpu.swapJoysticks) {
		if (keys & C64_JOY2_FIRE) v &= 0xEF;
		if (keys & C64_JOY2_UP) v &= 0xFE;
		if (keys & C64_JOY2_DOWN) v &= 0xFD;
		if (keys & C64_JOY2_LEFT) v &= 0xFB;
		if (keys & C64_JOY2_RIGHT) v &= 0xF7;
	} else {
		if (keys & C64_JOY1_FIRE) v &= 0xEF;
		if (keys & C64_JOY1_UP) v &= 0xFE;
		if (keys & C64_JOY1_DOWN) v &= 0xFD;
		if (keys & C64_JOY1_LEFT) v &= 0xFB;
		if (keys & C64_JOY1_RIGHT) v &= 0xF7;
	}

	if (!kbd_key && !kbd_mods)
		return v;

	const uint8_t filter = ~cpu.cia1.R[0x01] & cpu.cia1.R[0x03];

	if (kbd_key && (keymatrixmap[1][kbd_key] & filter))
		v &= ~keymatrixmap[0][kbd_key];
	v &= ~kbd_mod_mask(0, filter);

	return v;
}

uint8_t cia1PORTB(void)
{
	/* MCUME built PORT B's idle value out of PORT A's registers ($DC00/$DC02);
	   it is $FF either way in the usual scan direction, but wrong as soon as a
	   program drives PORT B as output. Here it is PORT B's own ($DC01/$DC03). */
	uint8_t v = ~cpu.cia1.R[0x03] | (cpu.cia1.R[0x01] & cpu.cia1.R[0x03]);

	const int keys = c64_emu_read_joysticks();
	if (!cpu.swapJoysticks) {
		if (keys & C64_JOY1_FIRE) v &= 0xEF;
		if (keys & C64_JOY1_UP) v &= 0xFE;
		if (keys & C64_JOY1_DOWN) v &= 0xFD;
		if (keys & C64_JOY1_LEFT) v &= 0xFB;
		if (keys & C64_JOY1_RIGHT) v &= 0xF7;
	} else {
		if (keys & C64_JOY2_FIRE) v &= 0xEF;
		if (keys & C64_JOY2_UP) v &= 0xFE;
		if (keys & C64_JOY2_DOWN) v &= 0xFD;
		if (keys & C64_JOY2_LEFT) v &= 0xFB;
		if (keys & C64_JOY2_RIGHT) v &= 0xF7;
	}

	if (!kbd_key && !kbd_mods)
		return v;

	const uint8_t filter = ~cpu.cia1.R[0x00] & cpu.cia1.R[0x02];

	if (kbd_key && (keymatrixmap[0][kbd_key] & filter))
		v &= ~keymatrixmap[1][kbd_key];
	v &= ~kbd_mod_mask(1, filter);

	return v;
}

/* ------------------------------------------------------------------------ */
/* Machine                                                                  */
/* ------------------------------------------------------------------------ */

bool c64_patch_kernal(void)
{
	/* $FFD5 JMP $F49E (LOAD) and $FFD8 JMP $F5DD (SAVE) in the KERNAL's jump
	   table. Replacing the JMP opcode leaves the operand bytes in place, which
	   is how patchLOAD finds the original entry point again when it decides to
	   let the real KERNAL handle the call. */
	const int load_vector = 0xFFD5 - 0xE000;
	const int save_vector = 0xFFD8 - 0xE000;

	if (rom_kernal[load_vector] != 0x4C || rom_kernal[save_vector] != 0x4C)
		return false;

	rom_kernal[load_vector] = 0xD2; /* -> opPATCHD2 -> patchLOAD() */
	rom_kernal[save_vector] = 0xF2; /* -> opPATCHF2 -> patchSAVE() */
	return true;
}

void c64_init(void)
{
	resetPLA();
	resetCia1();
	resetCia2();
	resetVic();
	cpu_reset();
	c64_joystick_port(joystick_port);
}

void c64_run_line(void)
{
	static unsigned short lc = 1;

	cpu.lineStartTime = get_ccount();
	cpu.lineCycles = cpu.lineCyclesAbs = 0;

	vic_do();

	/* The line the VIC has just drawn, if it was a visible one. */
	const int display_line = cpu.vic.rasterLine - C64_FIRST_DISPLAY_LINE;
	if (c64_framebuffer && display_line >= 0 && display_line < C64_SCREEN_HEIGHT)
		memcpy(c64_framebuffer + (unsigned)display_line * C64_SCREEN_WIDTH, c64_line_buffer,
		       sizeof(c64_line_buffer));

	if (--lc == 0) {
		lc = LINEFREQ / 10; // 10 Hz
		cia1_checkRTCAlarm();
		cia2_checkRTCAlarm();
	}
}

int c64_lines_per_frame(void) { return LINECNT; }
int c64_cycles_per_frame(void) { return LINECNT * CYCLESPERRASTERLINE; }
int c64_raster_line(void) { return cpu.vic.rasterLine; }

/* ------------------------------------------------------------------------ */
/* Starting a program without the user typing                               */
/* ------------------------------------------------------------------------ */
/*
  The KERNAL keyboard buffer: $0277..$0280 holds up to ten PETSCII codes and
  $00C6 their count. BASIC's idle loop takes them as if they had been typed,
  which is far more dependable than driving the key matrix with timed presses.
*/
void c64_type_petscii(const char *text)
{
	int n = 0;
	while (text[n] && n < 10) {
		char c = text[n];
		if (c == '\n')
			c = '\r';
		cpu.RAM[0x0277 + n] = (uint8_t)c;
		n++;
	}
	cpu.RAM[0x00C6] = (uint8_t)n;
}

bool c64_basic_ready(void)
{
	/* "READY." in screen codes, somewhere in the 1000 bytes of screen RAM.
	   The KERNAL has cleared the screen and run its RAM test by then. */
	static const uint8_t ready[] = {18, 5, 1, 4, 25, 46};
	const uint8_t *screen = &cpu.RAM[0x0400];
	for (int i = 0; i <= 1000 - (int)sizeof(ready); i++) {
		if (memcmp(screen + i, ready, sizeof(ready)) == 0)
			return true;
	}
	return false;
}

uint16_t c64_inject_program(const uint8_t *image, size_t size)
{
	if (!image || size < 3)
		return 0;

	const uint16_t addr = (uint16_t)(image[0] | (image[1] << 8));
	size_t len = size - 2;
	if ((size_t)addr + len > 0x10000)
		len = 0x10000 - addr;
	memcpy(&cpu.RAM[addr], image + 2, len);

	const uint16_t end = (uint16_t)(addr + len);

	/* If it loaded where BASIC programs live, tell BASIC how long it is:
	   VARTAB ($2D/$2E), ARYTAB ($2F/$30) and STREND ($31/$32) all point just
	   past the program. Without this, RUN runs nothing. */
	const uint16_t txttab = (uint16_t)(cpu.RAM[0x2B] | (cpu.RAM[0x2C] << 8));
	if (addr == txttab) {
		for (int p = 0x2D; p <= 0x31; p += 2) {
			cpu.RAM[p] = end & 0xff;
			cpu.RAM[p + 1] = end >> 8;
		}
	}

	/* The LOAD end address, where the KERNAL would have left it. */
	cpu.RAM[0xAE] = end & 0xff;
	cpu.RAM[0xAF] = end >> 8;

	return addr;
}

/* ------------------------------------------------------------------------ */
/* Save states                                                              */
/* ------------------------------------------------------------------------ */

#define C64_STATE_MAGIC 0x34364353u /* "SC64" */

struct c64_state_header {
	uint32_t magic;
	uint32_t version;
	uint32_t cpu_size;
	uint32_t sid_size;
};

size_t c64_state_size(void)
{
	return sizeof(struct c64_state_header) + sizeof(cpu) + (size_t)c64_sid_state_size();
}

bool c64_state_save(void *dest, size_t size)
{
	if (!dest || size < c64_state_size())
		return false;

	struct c64_state_header header = {
		C64_STATE_MAGIC, 1, (uint32_t)sizeof(cpu), (uint32_t)c64_sid_state_size(),
	};
	uint8_t *p = (uint8_t *)dest;
	memcpy(p, &header, sizeof(header));
	p += sizeof(header);
	memcpy(p, &cpu, sizeof(cpu));
	p += sizeof(cpu);
	c64_sid_state_save(p);
	return true;
}

bool c64_state_load(const void *src, size_t size)
{
	if (!src || size < c64_state_size())
		return false;

	struct c64_state_header header;
	const uint8_t *p = (const uint8_t *)src;
	memcpy(&header, p, sizeof(header));
	if (header.magic != C64_STATE_MAGIC || header.version != 1 ||
	    header.cpu_size != sizeof(cpu) || header.sid_size != (uint32_t)c64_sid_state_size())
		return false;
	p += sizeof(header);

	memcpy(&cpu, p, sizeof(cpu));
	p += sizeof(cpu);
	c64_sid_state_load(p);

	/* The state holds pointers, which are only meaningful in the binary that
	   wrote them. Derive them again from the registers instead of trusting
	   them: the PLA mapping from the 6510 port ($01), the VIC's character /
	   bitmap / video-matrix pointers from $D011 and $D018. */
	const uint8_t port = cpu.RAM[1] & 0x07;
	cpu.plamap_r = (rarray_t *)&PLA_READ[port];
	cpu.plamap_w = (warray_t *)&PLA_WRITE[port];
	vic_adrchange();

	return true;
}
