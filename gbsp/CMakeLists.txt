cmake_minimum_required(VERSION 3.5)
set(COMPONENTS "main retro-go gbsp-libretro app_trace bootloader esptool_py")
if(NOT "$ENV{GBAJIT}" STREQUAL "0")   # dynarec by default, GBAJIT=0: interpreter only
    list(APPEND COMPONENTS xjit esp_mm)
endif()
if((DEFINED ENV{GBABENCH} AND NOT "$ENV{GBABENCH}" STREQUAL "0") OR
   (DEFINED ENV{GBAPROF} AND NOT "$ENV{GBAPROF}" STREQUAL "0"))
    list(APPEND COMPONENTS perfmon)   # LX7 performance counters: GBABENCH and GBAPROF lines
endif()
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
# gpSP and the Xtensa dynarec (gbsp-libretro, xjit) live in the xtensa-68000-dynarec submodule
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../xtensa-68000-dynarec/components")
project(gbsp)
# retro-go's write_lines was 20.2% of core 1's ticks, every one fetched from
# flash through the same SPI0 cache controller core 0 reads its PSRAM-resident
# translated code through. 7.4 KB of it in IRAM, for this app only: other apps'
# internal-RAM budgets are not this one's. Board 2026-10-10: with this and the
# renderer's hot functions moved, core 0's Mario Kart frame went 19.30 -> 18.44.
idf_build_set_property(COMPILE_DEFINITIONS "-DRG_DISPLAY_WRITE_LINES_IRAM=1" APPEND)
# `GBAPROF=1 python rg_tool.py ... build gbsp`: ms per frame of the ARM
# interpreter, the scanline renderer, the sound mix and the display submit,
# printed once a second as a "GBAPROF" console line
if(DEFINED ENV{GBAPROF} AND NOT "$ENV{GBAPROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DGBAPROF=1" APPEND)
endif()
# `GBABENCH=1`: deterministic benchmark: after the resume the input comes from
# a frame-numbered script (right held, B and A tapped), and every 300 frames a
# "GBABENCH" console line gives the work ms per frame (emulation + render wait
# + display + sound mix, without the audio/frame pacing) and a screen hash
if(DEFINED ENV{GBABENCH} AND NOT "$ENV{GBABENCH}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DGBABENCH=1" APPEND)
endif()
