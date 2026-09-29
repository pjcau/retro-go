cmake_minimum_required(VERSION 3.5)
set(COMPONENTS "main retro-go gbsp-libretro app_trace bootloader esptool_py")
if(DEFINED ENV{GBAJIT} AND NOT "$ENV{GBAJIT}" STREQUAL "0")
    list(APPEND COMPONENTS xjit esp_mm)
endif()
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
project(gbsp)
# `GBAPROF=1 python rg_tool.py ... build gbsp`: ms per frame of the ARM
# interpreter, the scanline renderer, the sound mix and the display submit,
# printed once a second as a "GBAPROF" console line
if(DEFINED ENV{GBAPROF} AND NOT "$ENV{GBAPROF}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DGBAPROF=1" APPEND)
endif()
