# GBA: saves and the dynarec, made robust

Fork branch **`gba-robust`** (new, off master `6ecc9d9b`), submodule
**`60943394`** (master's — untouched). Play build **777920 B**. One flash.

**Separate from `gba-cpu`.** None of the frameskip or IRAM work is here, so this
build should behave like master except for the two fixes. Two commits:

| | |
|---|---|
| `2fa89241` | the dynarec guard: a power cut is not a crash |
| `43ddfb8d` | the battery save: off the frame loop, atomic, real size |

Host test already passing on the Mac: `gbsp/test/run.sh` — 25 checks over the
size table, round trips at all five real sizes, the 128 KB legacy files,
recovery from a lone `.tmp`, empty files, and a write that fails leaving the old
save byte-for-byte intact.

---

## 1. What changed, in one paragraph each

**The dynarec guard.** `DynarecState` TRYING only ever meant "the last start did
not finish", which is not "it crashed" — so a reset or power-off within two
minutes disabled the dynarec for that game for ever, and the alert was dismissed
unseen by a held button. Now `esp_reset_reason()` answers the real question:
PANIC / INT_WDT / TASK_WDT / WDT are the dynarec's fault; POWERON, SW, EXT and
BROWNOUT are not. retro-go switches apps with a *software* reset, so `ESP_RST_SW`
is how a game normally starts — that is precisely the case that was being
punished. A hang is still caught, because a watchdog is what ends one. On top of
that, `DynarecFails` counts consecutive crash-flagged starts and only the **third**
switches it off, so one freak crash costs nothing; two minutes of play spends the
count. `JIT_OFF_USER` separates the user's Off from the guard's, and a guard Off
— or a plain Off from an older build, which is what the four stuck games have —
is retried once with the count cleared. The alert now waits for every key to come
up before it appears.

**The battery save.** It was 128 KB to FAT over SPI from inside the frame loop,
whatever the cartridge actually had. Now the frame loop only takes a snapshot (a
memcpy) and a priority-1 task on core 0 writes it in 4 KB pieces with a yield
between them, so the SD lock is never held for a whole save while the ROM is
paged from the same card. Only the real size is written. The replace is atomic
(`.tmp` → fsync → rename), the dirty flag is cleared after the copy rather than
before the write, and an empty or unreadable file reads as "no save" instead of
wiping what is in memory.

## 2. Runs

```bash
python rg_tool.py --target esp32-emu-turbo build gbsp   # expect 777920 B
```

No card file is needed — there is no `gbaopt.txt` on this branch.

### A. The stall during a save (the headline)

Mario Kart, `load 1`, **drive a lap and finish it so the game records a lap
time** — that is what makes it write its save. Log for 90 s.

- **Pass: no frame longer than 50 ms** while it saves.
- In the log, confirm `battery save written: … (N bytes)` and **tell me N.**
  Mario Kart is flash, so expect 65536 or 131072, not always 131072.
- Also capture, from the launch lines, `battery save: 128 KB of PSRAM claimed
  before the ROM cache` **and the `ROM cache: N blocks of 1 MB` line.** The
  snapshot takes 128 KB that the ROM cache would have had; if the block count
  dropped and the game pages more, I want to know — that is the one cost of this
  design and it is measurable right there.
- How to see the frame time: the `[debug]` lines' `FPS:` dipping for a second is
  the symptom; if you have a cleaner way to catch a single long frame, use it.
  A dip from 60 to ~45 for one second is roughly a 300 ms frame; no visible dip
  at all is the pass.

### B. A save survives a round trip, byte for byte

1. Play until the game writes (a lap record), wait 2 s, **quit through the menu**.
2. Copy the `.sav` off the card, or note its size and a checksum.
3. Relaunch, check the record is there, quit again without playing.
4. **Byte-compare the file before and after.** It must be identical.
5. Check **no `.sav.tmp` is left** on the card at any point after a clean save.

### C. A reset during play leaves the dynarec on

1. Launch Mario Kart, play **30 s** (less than the two minutes that prove it).
2. **Reset the board** (or power-cycle it).
3. Relaunch the game. **Pass:** the log says `CPU core: dynarec`, no alert, and
   the line `the last start did not finish but did not crash (reset reason N):
   keeping the dynarec`. Tell me the reset reason number it prints.
4. Do it twice more. It must stay on the dynarec every time — the old build
   turned it off on the first try.

### D. The four stuck games heal themselves

**Do not edit any json for this.** Launch each of **Mario Kart, TMNT, NFS
Underground and the SM64 GBA demo** and capture the `CPU core:` line.

- **Pass:** each says `dynarec`, preceded by `the dynarec was switched off
  automatically before: trying it again`.
- And the speed should show it: Mario Kart was 45 fps on the interpreter.

### E. The menu toggle works in session

In Mario Kart's Options: set **Fast CPU → Off**, leave the menu, play 5 s —
speed should drop to interpreter level. Then **→ On**, leave, play 5 s — it must
come back **in the same session**, with no reset, and the row must read `On`
rather than `On (next start)`. Finally quit and relaunch: whatever was chosen
last must still be in force.

### F. A different save kind

If there is a game on the card with an **EEPROM** save (most Pokémon are flash;
many smaller titles are EEPROM) or any other non-flash save, play it until it
writes and report the `battery save written: … (N bytes)` size. A 512 or 8192
there is the clearest proof the size table is doing its job.

## 3. Pass criteria

1. **A: no frame over 50 ms while saving**, and the written size is the real one;
2. **B: the file is byte-identical** across a quit and a relaunch, with no
   `.sav.tmp` left behind;
3. **C: a reset at 30 s leaves the dynarec on**, three times out of three;
4. **D: all four stuck games start on the dynarec**, with no json editing;
5. **E: the menu toggle takes effect in session, both ways**;
6. no save lost anywhere, and no new alert the user has to dismiss.

## 4. What to do if something fails

- **If the ROM cache lost a block and the game pages noticeably more**, say so
  with both numbers — the snapshot can be made the real size and allocated after
  the ROM, at the cost of falling back to synchronous writes on big flash saves.
  I would rather know than guess.
- **If a save is ever lost or short**, stop and send me the `.sav`, its size and
  the launch lines. That is the one failure class I will not iterate on casually.
- **If the dynarec still turns off**, send the `the dynarec crashed on this game
  (reset reason N)` lines with their N — the reason number tells me whether the
  classification is wrong or the game genuinely crashes.

**Revert:** reflash a master build. Nothing here is behind a flag, because both
changes are bug fixes rather than experiments.

## 5. Not evidence

A single `[debug]` line, and "it looked fine" for the save round trip — B wants
a byte compare, because a save that is *nearly* right is the failure mode that
costs a player their progress.
