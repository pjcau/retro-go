# GBA: the two frameskip findings from CPU5

Fork `gba-cpu` **`137c9e8d`**, submodule **`2b87220`**, on master `6ecc9d9b`.
Play build **775840 B**, GBAPROF build **785984 B**. One flash.

CPU5 passed — Mario Kart 15.87 ms / 60 fps / 30 drawn / SPEED 100 %, Sonic
60/60 on both `skip=0` and Auto. This build fixes only the two things you
found, and **nothing in it should change a single one of those numbers**. That
is the point: if any of them move, something went wrong.

---

## What changed, and why it was one bug

Both findings had the same cause: gbsp and `rg_system`'s auto-frameskip were
both writing `app->frameskip` — gbsp once a frame, auto once a second. The
frameskip in force was always correct; the log was honestly reporting a fight.

`app->frameskipMax` is now the ceiling auto may raise frameskip to, and **0
means "do not adjust it at all"**. gbsp sets **2 for Auto** and **0 when
pinned**, and the per-frame write-back is gone — the frameskip variables are
touched only at startup, when the menu changes, and when the card is read.

**This touches shared `rg_system`**, which you asked me to avoid. It is
unavoidable: both the raise and the log line happen *inside* `rg_system`, once
a second, to a variable gbsp does not own, so nothing an app can do from
outside stops it. The two app-only alternatives are worse — keep fighting it
(the bug), or stop reading `app->frameskip` and let auto climb to 5 unchecked,
which leaves the game-menu header showing 5 while the game skips 0. The change
is one field, one condition and one default; `.frameskipMax = 5` keeps every app
that ignores it behaving exactly as today, and I built the **launcher**
(1134944 B) to prove the shared edit does not disturb another app.

Also hardened while I was there: a stored Frameskip setting outside `-1..2` is
distrusted and reset to Auto, and `skip=` above 2 is **refused with an error**
instead of quietly pinning a value the menu cannot display.

## Runs

```bash
python rg_tool.py --target esp32-emu-turbo build gbsp              # 775840 B, play
GBAPROF=1 python rg_tool.py --target esp32-emu-turbo build gbsp    # 785984 B
```

Protocol as before: `load 1` right after "Retro-Go ready", A held, 60 s
unfiltered log, medians of the last 6 `GBAPROF` lines.

| # | game | `gbaopt.txt` | the check |
|---|---|---|---|
| 1 | Mario Kart | `perf=1 skip=0` | **finding (a)**: `cpu` ≈ 18.4 ms, 54 fps, 54 drawn — and **zero `Raised frameskip` lines** in the whole log, against 52 last time |
| 2 | Mario Kart | `perf=1 skip=1` | **the regression check**: must still be ≈ 15.9 ms, 60 fps, **30 drawn**, SPEED 100 %. And **no `Raised frameskip to 2`** after the GBASAMPLE dump stall — that was finding (b) |
| 3 | Mario Kart | `perf=1 skip=2` | 20 drawn, and **no `Raised frameskip to 3`** |
| 4 | Mario Kart | `perf=1` (Auto) | auto should raise to 1 once, early, then hold 60 — as it did at 10.4 s last time. **It must never log a raise past 2** |
| 5 | Sonic Advance | `perf=1 skip=0` | still 60 fps / 60 drawn, no raise lines |
| 6 | Mario Kart | `perf=1 skip=3` | the new guard: the log must say **`gbaopt skip=3 is not 0, 1 or 2`** and fall back to the menu's choice (Auto), not pin anything |

Capture per run: `cpu` ms, the `[debug]` `FPS:` with its `S:` / `R:` split and
`BUSY:`, **every `Raised frameskip` / `Reduced frameskip` line with its
timestamp** (that is the finding under test — please give the count, not just
"none"), and the `gbaopt from …` line.

## Play check

Reinstall the **play build** (775840 B) with `board_install.sh`, delete
`/sd/retro-go/config/gbaopt.txt`, then:

1. **Mario Kart on Auto** — expect one `Raised frameskip to 1` early and then
   60 fps held, with **no further raise lines for the rest of the session**.
2. **Sonic Advance** — every frame drawn, 60/60, no raise lines.
3. **Buttons with real presses**: SELECT / START / A / START / RIGHT, confirming
   on the webcam that RIGHT steers, A accelerates and START pauses.
4. **Menu → Frameskip**: reads `Auto`, cycles `Auto → 0 → 1 → 2` (and no
   further), and the choice survives leaving and re-entering the game.
5. Set it to **2**, leave the menu, and check the log stays free of raise lines
   — that is finding (a) through the menu rather than the card.

## Pass criterion

**Run 2 unchanged from CPU5 (≈15.9 ms, 60 fps, 30 drawn), and not one
`Raised frameskip` line anywhere a pin is in force.** Any movement in the CPU5
numbers is a regression, because nothing here was meant to affect speed.

**Revert**: Frameskip → Auto in the menu, or delete `gbaopt.txt`. The shared
default (`frameskipMax = 5`) means no other app's behaviour depends on this.
