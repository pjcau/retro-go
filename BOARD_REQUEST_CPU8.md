# GBA: one flash — saves, the dynarec, and Auto frameskip that settles

Fork `gba-cpu` **`317f097d`**, rebased onto `gba-robust`, submodule
**`2b87220`**, all on master `6ecc9d9b`.
Play build **779408 B**, GBAPROF build **789632 B**. **One flash, everything.**

This replaces BOARD_REQUEST_ROBUST1 and CPU7 — both are superseded, don't run
them. Four things are in this build:

| | |
|---|---|
| `2fa89241` | the dynarec guard: a power cut is no longer read as a crash |
| `43ddfb8d` | the battery save: off the frame loop, atomic, real size |
| (CPU5/6 work) | core-1 renderer in IRAM, L1 2048, frameskip honoured, the Frameskip menu |
| `317f097d` | **new**: Auto drops a level on what the level below really cost |

Already green on the Mac: the save host test (`gbsp/test/run.sh`, 25/25) and
`videobench fuzz 400` unchanged (sha256 `81447d21…`).

---

## 1. Why Auto kept flapping, and what replaced the rule

CPU7 measured what the controller had been guessing: **at skip 1 a drawn frame
reads 15.2–15.5 ms, while a real skip-0 frame costs 17.5–18.3.** The drawn time
underestimates the level below by 2.5–3 ms — core 1 lags, so at skip 1 its work
for a drawn frame spills into the following skipped frame and a drawn frame never
carries a full frame of contention. (My earlier reading of this had the sign the
other way round; it came from a fitted model, not a measurement.)

So a 15.8 ms prediction dropped into an 18 ms frame every time: five round trips
in 140 s on the road, each costing a second at 91–94 %.

The prediction is now only a fallback. The first question is **what the level
below actually cost when this game was last on it**:

- never measured → the prediction, with a **13.0 ms** gate instead of 15.8;
- cost **≤ 16.2 ms** → it held, so go;
- cost **> 16.2 ms** → it could not hold, and no prediction changes that. Go
  only once the scene is measurably lighter than on arriving here: **2 ms** off
  the drawn time.

A raise records what the level it is leaving cost, so the memory is always the
freshest measurement of the level in question. **And a stall is never allowed
into that memory**: a second whose worst frame exceeds 100 ms updates nothing,
because remembering "skip 0 costs 140 ms" from one state load would strand the
game a level up for the rest of the session — which is precisely how Sonic got
stuck, the bug this is meant to cure. Raising stays immediate.

Traced against five cases before building: the road holds 1, the menus drop to 0,
Sonic returns to 0 after a load, and a stall in the very first second falls back
to the prediction — which lets Sonic down and keeps Mario Kart at 1.

**New log lines**: the drop line names which of the three reasons applied, and
the ten-second residency line now carries all three measured costs:

```
frameskip residency: skip0 12s, skip1 106s, skip2 22s of 140s, 30.0 drawn fps,
                     measured 18.05 / 15.82 / 14.70 ms, hold 3s
frameskip 1 -> 0: skip0 last cost 10.04 ms, drawn 10.90 (ref 10.90), ...
```

## 2. Runs

```bash
python rg_tool.py --target esp32-emu-turbo build gbsp              # 779408 B, play
GBAPROF=1 python rg_tool.py --target esp32-emu-turbo build gbsp    # 789632 B
```

No card file is needed for any run below; `gbaopt.txt` keys still exist for an
A/B if you want one (`skip=`, `autodyn=0`, `perf=1`).

### A. Mario Kart on the road, Auto — the headline

`load 1`, then **drive, steering, for 2 minutes**, through whatever the course
gives you. Then back out to the menus for 20 s.

- **Pass: at most one `0 -> 1` line in the whole 2 minutes**, and the minimum
  1 s `SPEED` **≥ 97 %** outside load stalls;
- the residency line's level split and average drawn fps;
- every `frameskip N -> M` line in order with its timestamp and its reason;
- and the drop back to 0 when you reach the menus — that is the other half
  working.

### B. Sonic and Metal Slug Advance, Auto

Each with a `load`, then 60 s of steady play.

- **Pass: back to 0 after the load stall** (at most one round trip: one raise,
  one drop), and **0 in steady play** with no further transitions;
- 60 fps, 60 drawn.

### C. The save never stalls a frame

Mario Kart, `load 1`, **finish a lap so the game records a time** — that is what
makes it write. 90 s.

- **Pass: no frame over 50 ms** while it saves. A one-second dip from 60 to ~45
  in the `[debug]` `FPS:` is roughly a 300 ms frame; no dip at all is the pass;
- report the `battery save written: … (N bytes)` size — Mario Kart is flash, so
  65536 or 131072, **not always 131072** as before;
- report the launch lines `battery save: 128 KB of PSRAM claimed before the ROM
  cache` **and `ROM cache: N blocks of 1 MB`.** The snapshot takes 128 KB the
  ROM cache would have had; if the block count dropped, I want the number —
  that is the one cost of this design and §5 has the fallback.

### D. A save survives a round trip, byte for byte

1. Play until it writes, wait 2 s, **quit through the menu**;
2. copy the `.sav` off the card (or note size + checksum);
3. relaunch, confirm the record is there, quit without playing;
4. **byte-compare the file before and after — it must be identical**;
5. **no `.sav.tmp` may be left** on the card after a clean save.

### E. A reset during play leaves the dynarec on

Launch Mario Kart, play **30 s** (under the two minutes that prove it), **reset
the board**, relaunch. **Pass:** `CPU core: dynarec`, no alert, and the line
`the last start did not finish but did not crash (reset reason N): keeping the
dynarec`. Tell me the N. **Do it three times** — the old build turned the
dynarec off on the first.

### F. The four stuck games heal themselves

**Edit no json.** Launch **Mario Kart, TMNT, NFS Underground, the SM64 GBA
demo** and capture each `CPU core:` line. **Pass:** each says `dynarec`,
preceded by `the dynarec was switched off automatically before: trying it
again`. Mario Kart was 45 fps on the interpreter, so the speed should show it.

### G. The menu toggle works in session

Mario Kart → Options → **Fast CPU → Off**, leave, play 5 s (speed should drop),
then **→ On**, leave, play 5 s — it must come back **in the same session**, with
the row reading `On`, not `On (next start)`. Quit and relaunch: the last choice
must still hold. Also check **Frameskip** reads `Auto` and cycles
`Auto → 0 → 1 → 2`, no further.

### H. A non-flash save, if one is on the card

Any game with an **EEPROM** save: play until it writes and report the
`battery save written: … (N bytes)`. A 512 or 8192 there is the clearest proof
the size table works.

## 3. Pass criteria

1. **A:** ≤ 1 `0 -> 1` line in 2 minutes, minimum 1 s SPEED ≥ 97 % outside
   loads, and it comes back to 0 in the menus;
2. **B:** Sonic and MSA back to 0 after the load, 0 in steady play, 60/60;
3. **C:** no frame over 50 ms while saving, and the real size written;
4. **D:** the `.sav` byte-identical across quit and relaunch, no `.tmp` left;
5. **E:** dynarec still on after a 30 s reset, three times out of three;
6. **F:** all four games start on the dynarec, no json edited;
7. **G:** the toggle takes effect in session, both ways;
8. no save lost anywhere, and no visible stutter at a frameskip transition on
   the webcam.

## 4. If something fails

- **Still flapping on the road** (more than one `0 -> 1`): send the drop lines
  with their `skip0 last cost` and `ref` figures. Those are exactly the inputs
  to the decision, so the fix is a number, not a guess.
- **Sonic does not come back to 0**: send its drop/raise lines and the residency
  line's `measured` field — it will show whether a stall got into the memory
  after all.
- **The ROM cache lost a block** and the game pages more: say so with both
  numbers. The snapshot can be made the real size and allocated after the ROM
  instead, at the cost of falling back to synchronous writes on big flash saves.
- **A save is ever lost or short**: stop, and send me the `.sav`, its size and
  the launch lines. That is the one failure class I will not iterate on
  casually.
- **The dynarec still turns itself off**: send the `the dynarec crashed on this
  game (reset reason N)` lines with their N — the number says whether the
  classification is wrong or the game genuinely crashes.

## 5. Revert

Frameskip → 0 or 1 in the menu pins it and switches the controller off;
`autodyn=0` on the card restores raise-only Auto. The save and dynarec fixes are
not behind a flag, because they are bug fixes rather than experiments — to drop
those, reflash a master build.

## 6. Not evidence

A single `[debug]` line; an average SPEED where the **minimum 1 s** is what
matters; "it looked fine" for D, which wants a byte compare, because a save that
is *nearly* right is the failure that costs a player their progress.
