#!/bin/sh
# Headless host build of prboom-go's engine, with the zone accounting on.
#
#   ./build.sh && build/doomhost <iwad> [tics] [report_every] [attract]
#
# The engine already has the #ifdef RETRO_GO guards for a non-retro-go build,
# so only the I_* hooks and three retro-go helpers are stubbed (host_main.c).
set -e

here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../components/prboom
out=$here/build

mkdir -p "$out/obj"
CFLAGS="-O1 -g -w -DHAVE_CONFIG_H -DDOOMMEM=1 -I $src -include $here/host_decls.h"

# d_server.c is the standalone server and has its own main()
for f in $(find "$src" -maxdepth 1 -name '*.c' ! -name 'd_server.c' | sort); do
    cc -c $CFLAGS -o "$out/obj/$(basename "$f" .c).o" "$f"
done
cc -c $CFLAGS -o "$out/obj/host_main.o" "$here/host_main.c"
cc -o "$out/doomhost" "$out"/obj/*.o -lm

cp -f "$src/data/doom1.wad" "$out/doom1.wad"
echo "built $out/doomhost (and copied doom1.wad next to it)"
