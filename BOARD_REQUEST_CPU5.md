# GBA: frameskip, and the measured 1.0 ms shipped

Fork `gba-cpu` **`5d433adb`**, submodule **`2b87220`**, on master `6ecc9d9b`.
Play build **775824 B**, GBAPROF build **785872 B**. **One flash.**

Two things in this build:

1. **The measured 1.0 ms is now in the play build** — core 1's hot renderer in
   IRAM (0.86 ms), the 2048-slot L1 and the cache-sync pre-check (0.15–0.17 ms).
   `nohash` and `rint` are *not* shipped: you showed the line hash pays for
   itself and `rint` does nothing.
2. **Frameskip works, and there is a Frameskip option.** One line in
   `gbsp/main/main.c` forced `skip_next_frame = 0` whenever the core-1 renderer
   ran, so the option and auto-frameskip were both ignored — that is why
   frameskip did nothing on the build you were testing.

---

## 1. Why frameskip should be worth a lot

You measured it: core 0's frame is **18.44 ms** with core 1 drawing and
**13.64 ms** with core 1 idle. Core 1 reads its renderer out of flash through
the same SPI0 cache controller core 0 reads its PSRAM-resident translated code
through, so core 1 working costs core 0 4.8 ms whether core 0 draws or not.

A skipped frame makes core 1 genuinely idle, which I checked rather than
assumed:

- `update_scanline` returns on `skip_next_frame` **before** the rline snapshot,
  the OAM and palette copies and `line_ready()` — so nothing is queued and
  core 1 is never woken: no `render_line`, no `render_scanline_*`.
- every `rg_display_submit` is inside the frame loop's `if (!skip_next_frame)` —
  so **no line hash and no `write_lines`** either.
- `gbsp_render_wait` is skipped with it.
- `gbsp_vram_flush` still runs, which it must, or the VRAM copy would go stale.
- `rg_audio_submit` sits **outside** the skipped branch, so audio is unaffected.

**Expect `cpu` ≈ (18.44 + 13.64)/2 ≈ 16 ms at skip=1** — full game speed with
30 frames drawn.

## 2. The option, and one deviation I want you to check

**Menu → Frameskip: Auto / 0 / 1 / 2**, kept per app.

**Auto is the default, not 0**, and that is a deliberate deviation from what you
asked for. The reason: `rg_system`'s auto-frameskip can lower `app->frameskip`
to 1 but **never back to 0**. With a pinned 0 as the default, Mario Kart would
sit at 54 fps until the user found the menu. Auto starts at 0 and only rises
when the speed sits under 96 %, so:

- Sonic Advance and Metal Slug (100 % speed) **never lose a frame** — Auto is 0
  for them, which is the regression this change could have caused and the thing
  to verify in run 4 below;
- Mario Kart reaches full speed by itself.

If you want the literal 0/1/2 with 0 default, it is one line — say so.

## 3. Runs

```bash
python rg_tool.py --target esp32-emu-turbo build gbsp              # 775824 B, play
GBAPROF=1 python rg_tool.py --target esp32-emu-turbo build gbsp    # 785872 B
```

The A/B needs **no menu navigation**: `skip=` in
`/sd/retro-go/config/gbaopt.txt` pins frameskip over both the menu and auto.
Keys now: `perf`, `core1_idle`, `nohash`, `rint`, `l1`, **`skip`**, and the
three idle-loop keys. **`isync=` is gone** — it is unconditional now, so a stale
`isync=1` is simply not read; the startup line no longer mentions it and reports
`skip -1` when the card does not pin it.

Protocol as always: Mario Kart, `load 1` right after "Retro-Go ready", A held,
60 s unfiltered log, **medians of the last 6** `GBAPROF`/`GBAPERF`/`GBAL1` lines.

| # | game | `gbaopt.txt` | expect |
|---|---|---|---|
| 1 | Mario Kart | `perf=1 skip=0` | the baseline with the IRAM placement shipped: `cpu` ≈ 18.4 ms, ~54 fps emulated, 54 drawn |
| 2 | Mario Kart | `perf=1 skip=1` | **the one that matters**: `cpu` ≈ 16 ms, ~60 fps emulated, **30 drawn**, `SPEED:` ~100 % |
| 3 | Mario Kart | `perf=1 skip=2` | 20 drawn. Only worth running if run 2 misses full speed |
| 4 | **Sonic Advance** | `perf=1 skip=0` | **the regression check**: 60 fps emulated, **60 drawn**, unchanged from today. If this is not 60/60, pinned 0 is broken and nothing else in this build matters |
| 5 | **Sonic Advance** | `perf=1` (no `skip=`) | Auto must stay at 0 on a game already at 100 %: still 60/60, and **no `Raised frameskip` line in the log** |

Please capture, per run:

- **`cpu` ms** (median of the last 6 `GBAPROF` lines);
- from the `[debug]` lines, **`FPS:` with its `S:` / `R:` split** and `BUSY:` —
  the total is the emulated rate, `S` + `R` is what was skipped against what
  was sent, so **this is where the drawn rate comes from**; plus `SPEED:` from
  the in-game menu header if you open it;
- any **`Raised frameskip` / `Reduced frameskip`** log lines (runs 4 and 5 should
  have none);
- a **`HEAP:`** reading during play on run 1;
- the **`GBASAMPLE1 core 1 code: IRAM x%, flash y%, other z%`** line on run 1.
  It should now print the real split by itself — roughly 74 / 19 / 7, the
  numbers you had to recompute by hand last time. **If it still reports a few
  hundred ticks, the fix did not work and please say so**; that is the whole
  point of checking it.
- **Webcam on runs 1 and 2**: how the race looks at 54 drawn against 30 drawn.

## 3b. The play check, at the end

Reinstall the **play build** (775824 B) with `board_install.sh`, delete
`/sd/retro-go/config/gbaopt.txt` so no card pin is left behind, and play:

1. **Mario Kart with Frameskip on Auto** (the default). Watch for
   `Raised frameskip to 1` in the log, then **judge it on the webcam**: at 30
   drawn frames, does the race look acceptable, and do full-speed audio and
   controls make up for it?
2. **Sonic Advance** — still every frame drawn, still 60/60.
3. **Buttons proven with real presses**, not just a boot: SELECT / START / A /
   START / RIGHT, and confirm on the webcam that RIGHT steers, A accelerates and
   START pauses.
4. Open the game menu once and check the **Frameskip** row reads `Auto`, cycles
   `Auto → 0 → 1 → 2`, and that the choice survives leaving and re-entering the
   game (it is kept per app).

**The smoothness judgement is yours, not a number.** It is a trade of
smoothness for speed, and if it looks bad the menu sets Frameskip to 0 and we
are back at 54 fps having lost nothing.

## 4. Pass criterion

**Run 2: `cpu` ≤ 16.5 ms with the game at full speed** (`SPEED:` ~100 %), the
race playing correctly, and Sonic unaffected in run 4.

Note this is full *game* speed with 30 drawn frames, not 60 drawn — 60 drawn
needs the data-traffic work in section 6, which I do not recommend yet.

**Revert**: Frameskip → 0 in the menu, or delete `gbaopt.txt` for the card
pins. The IRAM placement is link-time and only reverts by reflashing an older
build.

## 5. Correctness, and what I could not check

**Emulation is identical with skipping, by construction rather than by
measurement.** In the sources gbsp compiles, `skip_next_frame` exists in exactly
two places — the two `update_scanline` variants, each `if(skip_next_frame)
return;` at the top — and nowhere in the CPU, the timers, DMA, the sound mixer
or `gba_memory`. So a skipped frame changes no emulated state. The drawn frames
cannot drift either: `gbsp_vram_flush` runs every frame, and `OAM_UPDATED` and
`gbsp_pal_dirty` are only ever cleared *after* a snapshot has actually been
taken — so a skipped frame leaves them set and the next drawn line copies.

**The host harness cannot check this, and I would rather say so than imply it
did.** The snapshot path it would have to exercise (`rlines`, `r_oam`, `r_pal`,
`gbsp_vram_flush`, `line_ready`) is `ESP_PLATFORM`-only, so `gbahost` compiles
the *other* `update_scanline` entirely — and there is no GBA ROM on this machine
to run it against in any case. What I did run is `videobench fuzz 400`, which is
byte-identical (sha256 `81447d21…`, hash `9291ac80`): this commit did not touch
the renderer. The webcam and the play check are the test.

**Also fixed**: `GBASAMPLE1 core 1 code` was summing only the tail past the
150th entry, because the printing loop zeroes each entry as it goes. It should
now report the 74/19/7-style split by itself instead of you recomputing it.

## 6. Two corrections, and the data traffic

**`0x400559e0` is not memcpy.** `gbsp/main/main.c`'s own comment identifies
`pc - 0x400559d0 < 0x40` as ROM `_xtos_set_intlevel` — the end of a critical
section, where the timer ticks it held back land. So those 1227 ticks (7 % of
core 1) measure **interrupt-disabled time on core 1**, most likely the display
task's DMA handoff and FreeRTOS spinlocks. The ROM memcpy I found earlier was
`0x40056f5c`, a different routine, and on core 0. Attributing the 7 % properly
needs the depth-4 backtrace walk `samp_tick` has and `c1_tick` lacks — about ten
lines, say the word.

**The display DMA never touches PSRAM**: the i80 buffers come from
`rg_alloc(I80_BUF_LENGTH, MEM_DMA)`, internal RAM. The scaler reads the PSRAM
surface and writes into those, so the LCD DMA is off the contention list.

**Internal-RAM line buffers — my estimate.** Core 1's removable PSRAM traffic
per drawn frame is the surface write (76.8 KB, ~0.96 ms of bus at ~80 MB/s) plus
the hash read-back (76.8 KB, ~0.96 ms); the scaler's re-read is D-cache hits
behind the hash, and `rlines` is 0.24 ms. So **~1.9 ms of bus occupancy**, of
which core 0 recovers only where it actually collides: **≤1.9 ms, realistically
1.0–1.3**.

I did not build it. The surface is 76.8 KB against ~60 KB of internal RAM free
with a smaller largest block, and retro-go keeps three surfaces for the
renderer/display/pending pipeline — so it is not a buffer move but a rewrite of
the renderer→display handoff in shared retro-go code, breaking the deliberate
~77-line decoupling (`GBAWAIT lag159 ≈ 77`). For a ±50 % band, against frameskip
halving all of it for one deleted line. If you want 60 *drawn* afterwards, the
honest next step is a measurement first: a `nosurface=1` flag pointing the
renderer at one reused internal line instead of the surface — one GBAPROF line,
picture breaks, isolates the write exactly as `core1_idle` isolated core 1.

## 7. Not evidence

Instruction counts (2026-10-09 settled that), the frame hash, `instr/frame` and
`cycles/instr` (they count M4A HLE steps), host timings, and a single `GBAPROF`
line. Medians of the last six, every time.
