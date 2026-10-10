# GBA: Auto frameskip that comes back down, and must never be worse

Fork `gba-cpu` **`ffb457bb`**, submodule **`2b87220`**, on master `6ecc9d9b`.
Play build **777120 B**, GBAPROF build **787408 B**. One flash.

Auto now raises *and* lowers, at every level. CPU6 showed why that matters,
twice over:

- on the **real road** skip 1 is not enough (FPS 55-58, BUSY 98-100), Auto
  reaches 2, and `rg_system` never brings it back because its reducer wants BUSY
  under 85 — so a player got 20 drawn for most of a race;
- on **Sonic**, a one-second stall at `load 0` raised Auto to 1 at 281.4 s and it
  **stayed there for the rest of the session** — 30 drawn instead of 60, at BUSY
  57-61 %. Half the frames thrown away with 40 % of the core idle, because
  `rg_system` only ever reduces from above 1.

The second one is the cleanest demonstration of the bug and the cleanest test of
the fix, on a game that is not Mario Kart.

**This run is a three-way A/B, and the rule is that dynamic Auto must not be
worse than the best fixed setting.** Section 5 says what happens if it is.

---

## 1. The prediction, checked before it was written

Solving the three CPU5 runs for the two per-frame costs — skip 0 (54 drawn,
18.29), skip 1 (31 of 61 drawn, 15.87), skip 2 (20 of 60 drawn, 14.72):

| | |
|---|---|
| a **drawn** frame (core 1 drawing) | **19.10 ms** |
| a **skipped** frame (core 1 idle) | **12.53 ms** |

`(drawn + k·skipped)/(k+1)` then reproduces your measured averages at **skip 1
to 0.05 ms** and **skip 2 exactly**, so the two costs barely depend on the level.
Two consequences:

- **S = 12.53 is 1.11 ms below the 13.64 `core1_idle` floor** — as it must be,
  since a real skipped frame also drops the rline/OAM/palette snapshot and
  `gbsp_render_wait`, which `core1_idle=1` still ran. An independent check on
  the model, not a fitted constant.
- **A drawn frame at skip ≥ 1 costs 0.81 ms *more* than a frame at skip 0**
  (19.10 vs 18.29): at skip 0 core 1 lags by up to ~77 lines and spreads its
  work over every frame, while a drawn frame ends in `gbsp_render_wait` and
  carries a whole frame of core-1 work alone.

So the predictor is **pessimistic by ~0.8 ms** — the safe direction. With the
15.8 ms threshold the true margin under 16.67 is about **1.67 ms**.

## 2. The controller

- **Lower** `k → k−1` when `(drawn + (k−1)·skipped)/k` is under **15.8 ms** and
  has held for the current hold time. At k=1 that is the drawn time; at k=2 it
  is the average skip 1 would give. **Every level, not just 1→0.**

  **The 2→1 decision is the delicate one.** On the CPU5 grass numbers the
  prediction at k=2 is (19.10 + 12.53)/2 = **15.815 ms against the 15.800
  threshold** -- 15 us from dropping. It therefore stays at 2, which is right,
  but only just; on the road `drawn` is higher than 19.10, so it stays at 2 more
  firmly. The backoff below is what stops that margin becoming a 1<->2 flap, and
  the drawn/skipped figures in every log line are what let me move the threshold
  with data if it does.
- **Raise** on `SPEED < 96 %` and `BUSY > 85 %`, capped at **2**, and **never
  delayed** — losing speed is the one outcome that must not happen.
- **Anti-flapping backoff**, replacing the fixed 2 s cooldown: a raise within
  **3 s** of a drop means the drop was wrong, so the next must hold twice as
  long — **3 → 6 → 12 → 24 → 30 s** (one doubling step more than 3→6→12→30, same
  cap) — and **30 s with no raise** forgets it. A borderline scene therefore settles at the
  higher skip rather than flipping, and the worst case is one drop attempt per
  30 s.
- `frameskipMax` is **0**, so `rg_system` never joins in.

**Cost to core 0**: the frame path is two `rsr CCOUNT` instructions
(`esp_cpu_get_cycle_count`) and one add plus one increment — about **four cycles
of the four million in a frame**. No divide, no float, no log inside the frame;
the division, the single float compare and every log line are in the
once-a-second tick. Two reads is the minimum for a span: one read per frame
would only give the paced wall time, which is 16.67 ms whenever the game keeps
up and so says nothing about what a frame costs.

**Lines to expect**:

```
frameskip 0 -> 1: speed 90%, drawn 18.29 ms, skipped 0.00 ms, hold now 3s
frameskip 1 -> 0: 12.80 ms predicted, drawn 12.80, skipped 11.90, held 3s
frameskip residency: skip0 12s, skip1 46s, skip2 2s of 60s, 38.4 drawn fps, hold 3s
```

The residency line comes every ten seconds and is what answers "how long at
each level" and "average drawn fps" without arithmetic on your side.

## 3. Runs — the three-way A/B

```bash
python rg_tool.py --target esp32-emu-turbo build gbsp              # 777120 B, play
GBAPROF=1 python rg_tool.py --target esp32-emu-turbo build gbsp    # 787408 B
```

`/sd/retro-go/config/gbaopt.txt`. New key **`autodyn=0`** restores exactly
pre-CPU7 Auto (raise only, `rg_system`'s rule) — that is the control and, if
this fails, the fallback.

**Mario Kart must be driven on the real road, not A held on the grass.** That is
where CPU6 found the problem, so: `load 1`, then **drive a full lap or two,
steering, for 60 s**. Same route in all three Mario Kart runs, as closely as you
can — if the route differs the comparison is worth little, so say so if it did.

| # | game | card | 60 s |
|---|---|---|---|
| 1 | Mario Kart | `perf=1` (dynamic Auto) | the candidate |
| 2 | Mario Kart | `perf=1 skip=0` | fixed 0 |
| 3 | Mario Kart | `perf=1 skip=1` | fixed 1 |
| 4 | Mario Kart | `perf=1 autodyn=0` | **today's Auto**, for the "never worse" comparison — expect it to reach 2 and stay |
| 5 | Sonic Advance | `perf=1` (dynamic Auto) | must stay at 0, or recover to it — see below |
| 6 | Sonic Advance | `perf=1 skip=0` | fixed 0 |
| 7 | Metal Slug Advance | `perf=1` (dynamic Auto) | must stay at 0 |
| 8 | Metal Slug Advance | `perf=1 skip=0` | fixed 0 |

### Run 5 in particular: the CPU6 Sonic bug, cured

Start Sonic with **`load 0`**, the same way that produced the stall, and let it
play at least 30 s. If the stall raises Auto to 1, the controller should bring it
straight back:

- Sonic's drawn frame is ~10.0 ms at skip 0, so at skip 1 the prediction for 0
  is ~10.9 ms at worst against the 15.8 ms threshold — **4.95 ms of margin**, not
  a close call;
- the hold at that moment is **3 s** (no prior drop within 3 s, so the backoff
  has not armed);
- so expect **one raise, then one drop about 3 s later**, and 60/60 for the rest
  of the run.

If it raises and does *not* come back, that is the headline failure of this
build and worth reporting before anything else in the table.

**One thing I deliberately did not change**: the raise is still immediate rather
than requiring two seconds of slow speed, which would have suppressed the
spurious raise altogether. Delaying it would break the guard that matters more —
never lose speed when a game genuinely cannot keep up — and the drop recovers on
its own in 3 s. If the round trip turns out to be visible on the webcam, that is
the evidence that would change my mind.

### The table I need

Per run:

| column | where from |
|---|---|
| **average SPEED %** | mean of the per-second `SPEED:`/stats values over the run |
| **minimum SPEED % in a 1 s window** | the worst single second — this is the one that catches a stall a mean would hide |
| **drawn fps** | `R:` full+partial per second, or the residency line's figure on the Auto runs |
| **transitions** | count of `frameskip N -> M` lines (0 for every fixed and `autodyn=0` run) |
| **time at each level** | the residency line, Auto runs only |
| `cpu` ms | median of the last 6 `GBAPROF` lines |

Plus, for the Auto runs, **every `frameskip N -> M` line in order with its
timestamp** — that is how I see the mechanism rather than the result.

## 4. Pass criteria

All of these, together:

1. **dynamic Auto's average SPEED is never more than 1 % below the best fixed
   setting** for that game;
2. **drawn fps ≥ 20 on the road**, and **≥ fixed skip=1 only where skip=1
   actually holds full speed**. Not unconditionally: CPU6 showed skip=1 does not
   hold on the road (55-58 fps), so fixed skip=1 there draws ~56 frames while
   running *slow*, and Auto correctly sitting at 2 with 20 drawn at full speed
   must not be scored against it. Where skip=1 does hold — the grass, the
   lighter stretches — Auto must match its 30. On Sonic and Metal Slug: **60**;
3. **drawn fps on Auto ≥ what today's Auto gives** (run 4 — about 20 on the
   road);
4. **at most one transition per 10 s during the race** (so ≤ 6 in a 60 s run).
   On Sonic and Metal Slug: **no transitions during steady play, and at most one
   round trip — one raise then one drop — after a loading stall.** I had written
   "exactly 0" here, and that was wrong: a `load` can stall a second, which
   legitimately raises frameskip, and the drop back is the controller doing its
   job. What would be a fail is a *second* round trip once play is steady, or a
   raise that never comes back;
5. **no visible stutter at a transition on the webcam.** Watch runs 1, 5 and 7
   at the moments the log shows a change: a change of smoothness is expected and
   fine, a hitch, a jump in game speed or a torn frame is **a fail**. This is the
   one criterion I cannot evaluate from here.

Expected if it works: menus at 0, most of the road at 1, 2 only on the heavy
stretches — so drawn fps between 20 and 30, above run 4's 20.

## 5. The rule on a fail

**Not an option, a rule.** If any criterion in section 4 fails, the play build's
default goes back to today's Auto — raise only — and the dynamic controller
stays opt-in behind `autodyn=1`. That is one line in `frameskip_apply`
(`fs_dynamic` starts at 0 instead of 1); the code for both paths is already in
this build, which is why `autodyn=0` is run 4 rather than a separate flash.

Tell me **which** criterion failed and with what numbers, and I will either make
that flip or fix the controller with the drawn/skipped figures from the log —
which is why every line carries them.

**Revert on the board, immediately**: `autodyn=0` in the card file, or Frameskip
→ 0 or 1 in the menu (a pin disables the controller entirely).

## 6. Play check

Reinstall the **play build** (777120 B) with `board_install.sh`, delete
`gbaopt.txt`, then:

1. **Mario Kart: menus → drive a lap → pause → drive → back to the menus**, on
   Auto. Watch the transitions on the webcam (criterion 5) and the log lines
   around them.
2. **Sonic Advance** — every frame drawn, 60/60, no transition lines.
3. **Buttons with real presses**: SELECT / START / A / START / RIGHT, confirming
   on the webcam that RIGHT steers, A accelerates and START pauses.
4. **Menu → Frameskip** reads `Auto`, cycles `Auto → 0 → 1 → 2` and no further,
   and the choice survives leaving and re-entering the game.

## 7. Not evidence

Instruction counts, the frame hash, `instr/frame` and `cycles/instr`, host
timings. For this run the **ordered log lines and the minimum 1 s SPEED** matter
more than any average.
