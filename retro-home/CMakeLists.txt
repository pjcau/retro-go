cmake_minimum_required(VERSION 3.5)
# retro-home: the home consoles and computers that came after the user's
# request of 2026-10-07 (Atari 7800, Atari 5200, Commodore 64), one app
# partition for all of them, each core a component.
set(COMPONENTS "main retro-go prosystem atari5200 c64")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
project(retro-home)
