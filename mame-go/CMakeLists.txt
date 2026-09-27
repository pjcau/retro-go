cmake_minimum_required(VERSION 3.5)
# Arcade: MAME 0.37b5 (mame2000-libretro subset), non-commercial license, see
# components/mame2000/README_MAMEGO.md
set(COMPONENTS "main retro-go mame2000")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
# `NEOPROF=1 python rg_tool.py ... build mame-go`: ms per frame of the 68000,
# Z80, YM2610, video and mixer, printed every 60 frames (mamego_prof.c)
if(DEFINED ENV{NEOPROF} AND NOT "$ENV{NEOPROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DNEOPROF=1" APPEND)
endif()
project(mame-go)
