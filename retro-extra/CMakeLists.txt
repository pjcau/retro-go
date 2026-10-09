cmake_minimum_required(VERSION 3.5)
# Cores whose static tables would eat the internal RAM of every retro-core
# emulator (Stella 30 KB, RACE 26 KB) live in their own app partition.
set(COMPONENTS "main retro-go race stella arcade3d")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
# `NGPPROF=1 python rg_tool.py ... build retro-extra`: ms per frame of the
# TLCS-900H, the Z80, the line renderer, the sound chip, the sample mix and the
# frame send, printed every 60 frames (components/race/src/race_prof.c).
# `NGPPROF=gfx` adds the split inside the line renderer, at the cost of five
# more timer reads per scanline.
if(DEFINED ENV{NGPPROF} AND NOT "$ENV{NGPPROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNGPPROF=1" APPEND)
    if("$ENV{NGPPROF}" STREQUAL "gfx")
        idf_build_set_property(COMPILE_DEFINITIONS "-DNGPPROF_GFX=1" APPEND)
    endif()
endif()
project(retro-extra)
