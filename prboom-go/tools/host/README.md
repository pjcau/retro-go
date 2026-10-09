# prboom-go on the host

A headless build of the engine for a PC: no video, no input, a silent audio
sink, and a clock that advances one tic per call, so a run goes as fast as the
CPU (19 minutes of game time in about 2 seconds). It exists to answer memory
and behaviour questions without a board.

    cd prboom-go/tools/host
    ./build.sh
    build/doomhost doom1.wad 40000 2000 attract

Arguments: the IWAD, how many tics to run, how often to report, and `attract`
for the title screen's demo loop (which reloads a level every couple of
minutes) instead of a single demo on one map.

The engine is built with `DOOMMEM=1`, so every report is the same
`Z_LogStats()` line the firmware prints (see `prboom-go/CMakeLists.txt`):
live zone bytes and blocks per `PU_*` tag, then the eight allocation sites
holding the most with what each gained since the previous report.

Resolving the site addresses on macOS (the harness prints the image slide
on its first line, because the binary is position independent):

    python3 - <<'EOF'
    import re, subprocess
    out = open('out.txt').read()
    slide = re.search(r'slide: (0x[0-9a-f]+)', out).group(1)
    for a in dict.fromkeys(re.findall(r'site (0x[0-9a-f]+)', out)):
        sym = subprocess.run(['atos', '-o', 'build/doomhost', '-s', slide, a],
                             capture_output=True, text=True).stdout.strip()
        print(a, sym)
    EOF

## What it measured (2026-10-09)

40000 tics of the attract loop (19 minutes of game time, maps E1M5/E1M3/E1M7
three times over, with music registered and unregistered at every change):

| tag | start | end |
|:---|---:|---:|
| static | 1301600 B / 327 blk | 1346936 B / 337 blk |
| level | 421060 B / 220 blk | 419580 B / 215 blk |
| levspec | 1264 B / 16 blk | 1056 B / 14 blk |
| cache | 2359460 B / 803 blk | 2493648 B / 856 blk |

Compared at the *same point of the loop* (E1M5 after its demo), `static` is
1301600, 1344320, 1344320, 1346936 — it converges instead of growing, and the
non-purgeable tags are flat. Everything that grows is `PU_CACHE`, held by
`W_CacheLumpNum` (w_wad.c) and `createPatch` (r_patch.c): the lump cache
filling the heap on purpose. On the host nothing ever forces a purge, which is
why it keeps growing here; on the board `Z_Malloc` frees cache blocks to keep
the PSRAM reserve.

Not covered by the harness: everything inside `#ifdef ESP_PLATFORM` (the
reserve itself), and all of retro-go (display, audio, SD, the menus). A heap
that falls on the board while these numbers are flat is a leak outside the
zone.
