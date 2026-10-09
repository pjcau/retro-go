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
# `DOOMWARP=40 DOOMMEM=1 python rg_tool.py ... build prboom-go`: a multi-level
# bench needing no pad and no level exit - E1M1 starts by itself, the next map
# every <seconds>, E1M1..E1M9 twice, then it stops. The value is the seconds
# spent in each map, so 40 is an 18-load run of twelve minutes (main/main.c).
if(DEFINED ENV{DOOMWARP} AND NOT "$ENV{DOOMWARP}" STREQUAL "0")
    idf_build_set_property(COMPILE_DEFINITIONS "-DDOOMWARP=$ENV{DOOMWARP}" APPEND)
endif()
project(prboom-go)
