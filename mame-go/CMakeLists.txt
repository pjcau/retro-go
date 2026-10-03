cmake_minimum_required(VERSION 3.5)
# Arcade: MAME 0.37b5 (mame2000-libretro subset), non-commercial license, see
# components/mame2000/README_MAMEGO.md
set(COMPONENTS "main retro-go mame2000")
# `M68KJIT=1 python rg_tool.py ... build mame-go`: the 68000 dynarec (submodule
# ../xtensa-68000-dynarec, components m68kjit and xjit)
if(DEFINED ENV{M68KJIT} AND NOT "$ENV{M68KJIT}" STREQUAL "0")
    list(APPEND COMPONENTS m68kjit xjit esp_mm)
endif()
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
if(DEFINED ENV{M68KJIT} AND NOT "$ENV{M68KJIT}" STREQUAL "0")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../xtensa-68000-dynarec/components")
endif()
# `NEOPROF=1 python rg_tool.py ... build mame-go`: ms per frame of the 68000,
# Z80, YM2610, video and mixer, printed every 60 frames (mamego_prof.c)
# Neo Geo sprites are drawn on core 0: on core 1 they measured the same fps
# (43 in Metal Slug 2), the 68000 slowing down from the memory contention
# (11.8 -> 14 ms). `NEODEFER=1` builds the core-1 renderer for experiments.
if(NOT DEFINED ENV{NEODEFER} OR NOT "$ENV{NEODEFER}" STREQUAL "1")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNEO_NO_DEFER=1" APPEND)
endif()
# `LCD_MHZ=25`: the i80 LCD write clock (default 20 MHz, see the target's config.h);
# B1 of the Arcade 60 fps plan - the bus is 13.9 ms a full frame at 20 MHz
if(DEFINED ENV{LCD_MHZ} AND NOT "$ENV{LCD_MHZ}" STREQUAL "")
    idf_build_set_property(COMPILE_DEFINITIONS "-DRG_LCD_I80_CLK_HZ=($ENV{LCD_MHZ}*1000*1000)" APPEND)
endif()
# `LCD_BUFS=3`: the display driver's DMA buffers (default 5, 3.75 KB of internal RAM each)
if(DEFINED ENV{LCD_BUFS} AND NOT "$ENV{LCD_BUFS}" STREQUAL "")
    idf_build_set_property(COMPILE_DEFINITIONS "-DI80_BUF_COUNT=$ENV{LCD_BUFS}" APPEND)
endif()
# `BAND_INTERNAL=3`: Neo Geo band buffers in internal RAM (default 2 at 16 lines, 4 at 8)
if(DEFINED ENV{BAND_INTERNAL} AND NOT "$ENV{BAND_INTERNAL}" STREQUAL "")
    idf_build_set_property(COMPILE_DEFINITIONS "-DBAND_INTERNAL=$ENV{BAND_INTERNAL}" APPEND)
endif()
# `AUDIO_MIX_HZ=16000`: the chips render at 16 kHz, doubled to the 32 kHz the speaker
# always gets (default 32000, main/main.c)
if(DEFINED ENV{AUDIO_MIX_HZ} AND NOT "$ENV{AUDIO_MIX_HZ}" STREQUAL "")
    idf_build_set_property(COMPILE_DEFINITIONS "-DAUDIO_MIX_HZ=$ENV{AUDIO_MIX_HZ}" APPEND)
endif()
# `SD_HOLD_CS=1`: the SD card's chip select held low for good (rg_storage.c
# RG_STORAGE_SDSPI_HOLD_CS, the work-around of esp-idf issue 10493 for slow
# SDSPI accesses when the card is alone on its SPI bus). A measurement.
if(DEFINED ENV{SD_HOLD_CS} AND NOT "$ENV{SD_HOLD_CS}" STREQUAL "" AND NOT "$ENV{SD_HOLD_CS}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DRG_STORAGE_SDSPI_HOLD_CS=1" APPEND)
endif()
# `NEOSND_PRIO=7`: priority of the Neo Geo sound board's task on core 1 (default 5;
# the display task is 6, drivers/neogeo.c)
if(DEFINED ENV{NEOSND_PRIO} AND NOT "$ENV{NEOSND_PRIO}" STREQUAL "")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNEOSND_PRIO=$ENV{NEOSND_PRIO}" APPEND)
endif()
# `NB_LINES=16`: lines per Neo Geo band (default 8, vidhrdw/neogeo_band.c)
if(DEFINED ENV{NB_LINES} AND NOT "$ENV{NB_LINES}" STREQUAL "")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNB_LINES=$ENV{NB_LINES}" APPEND)
endif()
# `M68KJIT_MEMIRQ=1`: the dynarec keeps the flags current before every memory
# access (interrupts taken inside a handler); off since F2 of the codegen plan
if(DEFINED ENV{M68KJIT_MEMIRQ} AND NOT "$ENV{M68KJIT_MEMIRQ}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DM68KJIT_MEMIRQ=1" APPEND)
endif()
if(DEFINED ENV{NEOPROF} AND NOT "$ENV{NEOPROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNEOPROF=1" APPEND)
endif()
# `NEOBAND=1`: Neo Geo frames drawn in 16-line bands in internal RAM (V1 of the
# Arcade 60 fps plan, vidhrdw/neogeo_band.c); off until it is proven faster
if(DEFINED ENV{NEOBAND} AND NOT "$ENV{NEOBAND}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNEOBAND=$ENV{NEOBAND}" APPEND) # 1: bands copied to the bitmap (V1), 2: handed to the display (V2)
endif()
# `MAMEPROF=1` (with MAMEBENCH=1): sampling profiler of core 0 (main/main.c)
if(DEFINED ENV{MAMEPROF} AND NOT "$ENV{MAMEPROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DMAMEPROF=1" APPEND)
    if("$ENV{MAMEPROF}" STREQUAL "core1")
        idf_build_set_property(COMPILE_DEFINITIONS "-DMAMEPROF_CORE1=1" APPEND)
    endif()
endif()
# `MAMEBENCH=1`: deterministic benchmark, scripted input from the first frame (the
# attract loop on a cold boot). `MAMEBENCH=2`: the script first inserts a coin and
# presses START, so the run measures the game being played. `MAMEBENCH=3`: the same
# with every other frame not drawn (main/main.c)
if(DEFINED ENV{MAMEBENCH} AND NOT "$ENV{MAMEBENCH}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DMAMEBENCH=$ENV{MAMEBENCH}" APPEND)
endif()
project(mame-go)
