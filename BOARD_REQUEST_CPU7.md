# GBA: Auto frameskip that goes back down

Fork `gba-cpu` **`31b6dd7e`**, submodule **`2b87220`**, on master `6ecc9d9b`.
Play build **777008 B**, GBAPROF build **787136 B**. One flash.

Auto now raises *and* lowers: skip a frame only while the game cannot reach
full speed, and draw every frame again as soon as it can. Mario Kart's menus at
60 drawn, the race at skip 1, and back on the way out.

---

## 1. The prediction, checked before it was written

I was asked to confirm that a drawn frame's core-0 time at skip 1 really
predicts the skip-0 cost. It does, with a known bias, and here is the working
rather than an assertion. Solving the three CPU5 runs for the two per-frame
costs — skip 0 (all 54 drawn, 18.29), skip 1 (31 of 61 drawn, 15.87), skip 2
(20 of 60 drawn, 14.72):

| | |
|---|---|
| a **drawn** frame (core 1 drawing) | **19.10 ms** |
| a **skipped** frame (core 1 idle) | **12.53 ms** |

and then `(drawn + k·skipped)/(k+1)` reproduces your measured averages at
**skip 1 to 0.05 ms** and **skip 2 exactly** — so the two costs barely depend on
the level, which is what makes the prediction usable at all.

Two things fall out, and the second is the answer to your question:

- **S = 12.53 is 1.11 ms below the 13.64 core1_idle floor**, exactly as it
  should be: a real skipped frame also drops the rline/OAM/palette snapshot and
  `gbsp_render_wait`, which `core1_idle=1` still ran. That is an independent
  check on the model, not a fitted constant.
- **A drawn frame at skip ≥ 1 costs 0.81 ms *more* than a frame at skip 0**
  (19.10 against 18.29). At skip 0 core 1 lags by up to ~77 lines and its work
  spreads over every frame; a drawn frame ends in `gbsp_render_wait`, so it
  carries a whole frame of core-1 work by itself.

So the predictor is **pessimistic by ~0.8 ms**, which is the safe direction: Auto
holds a skip slightly longer than it strictly must and will not drop to 0 onto a
scene that cannot carry it. With the 15.8 ms threshold the true margin under the
16.67 ms budget is about **1.67 ms**, not 0.87. No alternative mechanism is
needed — the bias also shrinks exactly where it matters least, because it comes
from core 1's load, which is small in the light scenes where lowering happens.

## 2. The controller

- **Lower** `k → k−1` when `(drawn + (k−1)·skipped)/k` is under **15.8 ms** and
  has stayed there **2 s**. At k=1 that is just the drawn time; at k=2 it is the
  average skip 1 would give.
- **Raise** on the same test as before — `SPEED < 96 %` and `BUSY > 85 %` —
  capped at **2**.
- **2 s cooldown** after either, so a wrong call cannot oscillate faster than
  once every four seconds.
- `frameskipMax` stays **0**, so `rg_system`'s auto never joins in: gbsp owns
  the whole decision and there is nobody to fight.

**Every change logs one line** with the numbers behind it:

```
frameskip 0 -> 1: speed 90%, drawn 18.29 ms, skipped 0.00 ms
frameskip 1 -> 0: 12.80 ms predicted a frame, drawn 12.80, skipped 11.90
```

That is the point of this run: **the log should show the mechanism, not just the
result.** The two timing points around the emulation are no longer GBAPROF-only
(Auto needs them in a play build) — two `esp_timer` reads a frame, ~0.4 µs of
16 ms.

## 3. Runs

```bash
python rg_tool.py --target esp32-emu-turbo build gbsp              # 777008 B, play
GBAPROF=1 python rg_tool.py --target esp32-emu-turbo build gbsp    # 787136 B
```

`gbaopt.txt` keys unchanged (`perf`, `core1_idle`, `nohash`, `rint`, `l1`,
`skip`, and the idle-loop keys). **Leave `skip=` out for every run below** —
a pin switches the controller off, which is the whole thing under test. `perf=1`
is fine and does not affect it.

### Run 1 — Mario Kart, the full cycle (the headline)

**No `load`.** Start from the launcher so the menus come first, and please log
continuously through all of it, ~2 minutes:

1. the **title and menus** — let it sit 10 s;
2. pick a cup and **start the race**, hold A, 30 s;
3. **pause** (START) and sit in the pause menu 10 s;
4. **resume** the race, 20 s;
5. back out to the **menus**, 15 s.

What I want, with timestamps:

- **every `frameskip N -> M` line**, in order, with its drawn/skipped/predicted
  milliseconds — this is the deliverable;
- **the count of transitions**. Expect roughly four: 0→1 entering the race, 1→0
  on the pause menu, 0→1 resuming, 1→0 backing out. **More than ~8 is
  flapping** and a finding;
- `FPS:` with `S:` / `R:` and `BUSY:` in each phase, so I can see 60 drawn in
  the menus and 30 in the race;
- `SPEED:` ~100 % in the race.

### Runs 2 and 3 — the games that must not change

| # | game | expect |
|---|---|---|
| 2 | **Sonic Advance**, Auto, 60 s | **frameskip 0 the whole time, 60/60, and not one `frameskip N -> M` line** |
| 3 | **Metal Slug Advance**, Auto, 60 s | the same: 0 throughout, 60/60, no transition lines |

These are the regression: both were 60/60 already, so the controller must never
touch them. One transition line in either is a bug, even if the fps looks fine.

### Run 4 — the pin still wins

`skip=1` on Mario Kart, 30 s: 30 drawn, ≈15.9 ms, and **no `frameskip N -> M`
lines at all** (a pin disables the controller). This also re-checks CPU6's
finding (a) — no `Raised frameskip` spam either.

## 4. Play check

Reinstall the **play build** (777008 B) with `board_install.sh`, delete
`/sd/retro-go/config/gbaopt.txt`, then:

1. **Mario Kart, menus → race → menus** on Auto (the default), watched on the
   webcam. The question is whether the *transitions* are acceptable: the race
   dropping to 30 drawn and the menus coming back to 60 should look like a
   change of smoothness, **not a stutter, a hitch or a jump in game speed**. If
   a transition is visible as a glitch, say so — that is the one thing I cannot
   judge from here and it decides whether Auto should be the default.
2. **Sonic Advance** — every frame drawn, 60/60.
3. **Buttons with real presses**: SELECT / START / A / START / RIGHT, confirming
   on the webcam that RIGHT steers, A accelerates and START pauses.
4. **Menu → Frameskip** reads `Auto`, cycles `Auto → 0 → 1 → 2` and no further,
   and the choice survives leaving and re-entering the game.

## 5. Pass criterion

**Mario Kart on Auto: 60 drawn in the menus, 30 drawn in the race at SPEED
100 %, and the transitions both ways within about four changes over the two
minutes** — with Sonic and Metal Slug untouched at 0.

If it **flaps** (many transitions, or one every few seconds in a steady scene),
the threshold is too close to the real cost and I will lower `FS_BUDGET_US` or
lengthen the hold using the drawn/skipped numbers from the log — which is why
every line carries them.

**Revert**: Frameskip → 0 or 1 in the menu pins it and switches the controller
off entirely; or reflash CPU6's build (`137c9e8d`).

## 6. Not evidence

Instruction counts, the frame hash, `instr/frame` and `cycles/instr`, host
timings, and a single `GBAPROF` line. Medians of the last six where a number is
wanted — but for this run the **log lines in order** matter more than any median.
