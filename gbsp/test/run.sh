#!/bin/sh
# Host test for the battery-save file logic (gbsp/main/sram_file.h).
# No ESP-IDF, no board: cc and a /tmp directory.
set -e
dir=$(dirname "$0")
out=${TMPDIR:-/tmp}/sram_file_test
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$out" "$dir/sram_file_test.c"
exec "$out"
