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

    retro-go port: the KERNAL LOAD patch. MCUME's version read the file
    through emuapi and could also fake a directory listing off the SD card;
    here there is exactly one program image in memory -- the file the launcher
    picked -- and the front end provides it through c64_emu_file_* (c64_emuapi.h).

    The directory listing and SAVE are not ported: there is nothing to write
    to and no disk to list. patchSAVE returns the KERNAL's "device not
    present" error so a program trying to save fails cleanly instead of
    hanging.
*/

#include "c64_patches.h"

static char filename[64];

void patchLOAD(void)
{
	int device;
	int secondaryAddress;
	uint16_t addr, size;

	device = cpu.RAM[0xBA];
	if (device != 1 && device != 8 && device != 9)
	{
		/* Not a disk/tape LOAD: run the original KERNAL code. */
		cpu.pc = rom_kernal[cpu.pc - 0xe000 + 1] * 256 + rom_kernal[cpu.pc - 0xe000];
		return;
	}

	memset(filename, 0, sizeof(filename));
	if (cpu.RAM[0xB7] == 0)
	{
		/* LOAD with no name: the file the launcher picked. */
		const char *name = c64_emu_file_name();
		if (name)
			strncpy(filename, name, sizeof(filename) - 1);
	}
	else
	{
		int len = cpu.RAM[0xB7];
		if (len > (int)sizeof(filename) - 1)
			len = sizeof(filename) - 1;
		strncpy(filename, (char *)&cpu.RAM[cpu.RAM[0xBC] * 256 + cpu.RAM[0xBB]], len);
	}
	secondaryAddress = cpu.RAM[0xB9];

	printf("C64: LOAD \"%s\",%d,%d\n", filename, device, secondaryAddress);

	if (c64_emu_file_open(filename) == 0)
	{
		printf("C64: not found\n");
		cpu.pc = 0xf530; // FILE NOT FOUND
		return;
	}

	size = (uint16_t)c64_emu_file_size(filename);
	if (size < 3)
	{
		c64_emu_file_close();
		cpu.pc = 0xf530;
		return;
	}

	{
		uint8_t header[2];
		c64_emu_file_read(header, 2);
		addr = header[1] * 256 + header[0];
	}

	if (secondaryAddress == 0)
	{
		/* LOAD"...",8 relocates to the start of BASIC. */
		addr = cpu.RAM[0x2B] | (cpu.RAM[0x2C] << 8);
	}

	uint32_t len = size - 2;
	if (addr + len > 0x10000)
		len = 0x10000 - addr;
	c64_emu_file_read(&cpu.RAM[addr], (int)len);
	c64_emu_file_close();

	/* End-of-load address. MCUME wrote the two halves the wrong way round
	   ($AF = low); the C64 keeps the low byte in $AE. */
	const uint16_t end = (uint16_t)(addr + len);
	cpu.RAM[0xAE] = end & 0xff;
	cpu.RAM[0xAF] = end >> 8;

	cpu.y = 0x49; // Offset for "LOADING"
	cpu.pc = 0xF12B; // Print and return
	printf("C64: loaded %u bytes at $%04X\n", (unsigned)len, addr);
}

void patchSAVE(void)
{
	/* No storage to save to. */
	printf("C64: SAVE is not supported\n");
	cpu.pc = 0xF707; // DEVICE NOT PRESENT
}
