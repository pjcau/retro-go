# Commodore 64: Frank Bösing's Teensy64 (github.com/FrankBoesing/Teensy64,
# GPL v3 or later) as cut down for the ESP32 by Jean-Marc Harvengt in MCUME
# (github.com/Jean-MarcHarvengt/MCUME, MCUME_esp32/esp64 at commit 27f6b906),
# plus reSID 0.16 for the SID (Dag Lem, GPL v2 or later) in reSID/, unchanged.
#
# Ported files carry the c64_ prefix because this app's include path also
# holds atari5200/ (cpu.h, memory.h, pokey.h...) and prosystem/ (Memory.h,
# Cartridge.h...), and the checkout is case-insensitive.
#
# NOT ported: roms.cpp (Commodore's KERNAL/BASIC/CHARGEN -- read from the SD
# card instead, see c64_roms.h), emuapi.cpp, go.cpp, ili9341_t3dma.*,
# AudioPlaySystem.*, IntervalTimer.h, the bitmap/font/OSD headers and
# iopins.h. The platform calls of c64_emuapi.h are main_c64.cpp's.
#
# License: GPL v3 or later. See LICENSE.md and reSID/COPYING.
set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()
rg_setup_compile_options(-O2 -Wno-error=unused-function -Wno-unused-function -Wno-error=unused-variable -Wno-unused-variable -Wno-error=unused-but-set-variable -Wno-error=maybe-uninitialized -Wno-error=implicit-fallthrough -Wno-format -Wno-error=unused-value -Wno-unused-value -Wno-error=sequence-point -Wno-error=char-subscripts -Wno-error=attributes -Wno-error=parentheses -Wno-error=misleading-indentation -Wno-error=dangling-else -Wno-error=unused-label -Wno-error=array-bounds -Wno-error=delete-non-virtual-dtor -Wno-error=reorder -Wno-error=write-strings -Wno-error=deprecated-declarations -Wno-error=strict-aliasing -Wno-error=narrowing)
