/*
    Commodore 64 core on retro-go: the three ROMs.

    MCUME shipped roms.cpp with the KERNAL, BASIC and CHARGEN images compiled
    in. They are Commodore's copyrighted code, and this project ships no ROM
    in any repository, so roms.cpp is NOT ported: the arrays are plain RAM and
    the front end fills them from the SD card before the machine is reset
    (/retro-go/bios/c64/kernal.rom, basic.rom, chargen.rom).

    They stay non-const for that reason; the core only ever reads them, and
    pla.cpp's sizeof(rom_*) still gives the right mask.

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_ROMS_H_
#define C64_ROMS_H_

#include <stdint.h>

#define APPLY_PATCHES 1

#define C64_ROM_BASIC_SIZE   8192
#define C64_ROM_KERNAL_SIZE  8192
#define C64_ROM_CHARGEN_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char rom_basic[C64_ROM_BASIC_SIZE];
extern unsigned char rom_kernal[C64_ROM_KERNAL_SIZE];
extern unsigned char rom_characters[C64_ROM_CHARGEN_SIZE];

#ifdef __cplusplus
}
#endif

#endif
