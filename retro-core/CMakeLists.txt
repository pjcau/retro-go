cmake_minimum_required(VERSION 3.5)
set(COMPONENTS "main retro-go nofrendo gnuboy pce-go gw-emulator handy smsplus snes9x race")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
# SNES renderer counters + on-screen HUD (esp32-emu-turbo Phase 4 step 4.0).
# `SNES_PROF=1 python rg_tool.py ... build retro-core` — counters and timer
# reads only, no -finstrument-functions (unlike the `profile` command).
if(DEFINED ENV{SNES_PROF} AND NOT "$ENV{SNES_PROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DSNES_PROF=1" APPEND)
    message(STATUS "SNES_PROF=1: snes9x renderer profiling enabled")
endif()
project(retro-core)
