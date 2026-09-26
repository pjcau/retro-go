cmake_minimum_required(VERSION 3.5)
# Arcade: MAME 0.37b5 (mame2000-libretro subset), non-commercial license, see
# components/mame2000/README_MAMEGO.md
set(COMPONENTS "main retro-go mame2000")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
project(mame-go)
