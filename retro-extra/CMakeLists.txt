cmake_minimum_required(VERSION 3.5)
# Cores whose static tables would eat the internal RAM of every retro-core
# emulator (Stella 30 KB, RACE 26 KB) live in their own app partition.
set(COMPONENTS "main retro-go race stella")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
project(retro-extra)
