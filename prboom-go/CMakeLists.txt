cmake_minimum_required(VERSION 3.5)
set(COMPONENTS "main retro-go prboom")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
# `DOOMMEM=1 python rg_tool.py ... build prboom-go`: zone accounting - live
# bytes and blocks per PU_* tag, the call sites holding the most, the cache
# purges, next to both heaps every 10 s (main/main.c, components/prboom/
# z_zone.c). Four bytes per block and two adds per allocation, so it stays out
# of a release build. Written to tell the lump cache filling the heap by
# design from a leak; see tools/host/README.md.
if(DEFINED ENV{DOOMMEM} AND NOT "$ENV{DOOMMEM}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DDOOMMEM=1" APPEND)
endif()
project(prboom-go)
