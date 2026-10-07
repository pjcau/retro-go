# License — retro-home `c64` component

## Teensy64 (the C64 machine: CPU, VIC-II, CIA, PLA, KERNAL patches)

Copyright Frank Bösing, 2017 — <https://github.com/FrankBoesing/Teensy64>

Ported to the ESP32 by Jean-Marc Harvengt in MCUME
(<https://github.com/Jean-MarcHarvengt/MCUME>, `MCUME_esp32/esp64/main`,
commit `27f6b906`), and from there to retro-go in this component.

**GNU General Public License, version 3 or (at your option) any later
version.** Every ported file keeps Frank Bösing's notice; the full text is at
<https://www.gnu.org/licenses/gpl-3.0.html>.

The 6502 core inside `c64_cpu.cpp` is Mike Chambers' Fake6502 v1.1, released
by its author into the public domain.

## reSID (the SID) — `reSID/`

Copyright (C) 2004 Dag Lem <resid@nimrod.no>, reSID 0.16, as shipped by MCUME
in `MCUME_esp32/esp64/main/reSID/reSID`. **GNU General Public License,
version 2 or (at your option) any later version** — see `reSID/COPYING`.

Only the 6581 wave tables are kept (`wave6581_*.cc`); the 8580 tables are not
used by this build.

## Consequence for the app

GPL v2-or-later and GPL v3-or-later combine as GPL v3-or-later, which is what
this component as a whole is under. retro-go itself is GPL v2 or later
(`components/retro-go/COPYING`), so linking it with this component makes the
**`retro-home` binary GPL v3 or later**. The other two cores in `retro-home`
(prosystem, atari5200) are GPL v2 or later and are compatible with that.

## ROMs

None. The C64's KERNAL, BASIC and character generator ROMs are Commodore's
copyrighted code and are **not** in this repository: MCUME's `roms.cpp` was
deliberately left out of the port. The user supplies them on the SD card as
`/retro-go/bios/c64/kernal.rom`, `basic.rom` and `chargen.rom`.
