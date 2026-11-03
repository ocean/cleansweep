# Hardware testing: stage C

Drew runs every step that changes a device. Take a flash backup first, and keep download mode within reach: it's
the way back from anything, with a `flash-restore` of the C0 backup, or an erasing `flash-install` of V15
(FM1Kit's stage R11).

Before installing, `uv run tools/hw_check.py` should pass: the same package, run to the done screen on emulated
hardware. The package to test is HEAD 1cac418's build: `build/cleansweep.fwsc`, `FM-1_257000`, SHA-256
`b0d65cb12f35a93adb91fd7fa7fee2382aee670a4b1e38001569fbb34fb596dc`. Check it with FM1Kit's `swift run fm1probe
inspect ../cleansweep/build/cleansweep.fwsc`.

Run the commands from FM1Kit's folder (`../FM1Kit` from here). The packages:
- CLEANSWEEP: `../cleansweep/build/cleansweep.fwsc`
- V15: `../FM-SWITCH/App/Resources/Firmware/FM-1_015.fwsc`
- V16: `.artifacts/tmp/v16/FM-1-v16.fwsc`, downloaded from M-VAVE
- MIDIMON 0.4: `.artifacts/tmp/hops/ignis32_midimonitor_fm1/midimon-0.4.fwsc`

**USB is off until the reset is done.** From the restart after an install until the done (or error) screen, the
Mac sees no FM-1 at all. That's expected. fm1probe waits about 43 s for the FM-1 to come back after an install,
which should cover it; if it reports "the FM-1 didn't come back", check the screen before anything else. The
identity request, download mode and installs all work only once the done screen is up.

**Checking the bytes.** Backups go in `.artifacts/tmp/c-backups/`.
`python3 -I .artifacts/scripts/check_cleansweep.py AFTER.bin .artifacts/tmp/r3-diag/v15-usr.bin BEFORE.bin`
checks a backup against what CLEANSWEEP should leave: 0x93000–0xFBFFF is V15's factory data (73,226 bytes) at
0xEA000 and erased everywhere else, 105 of 105 sectors; the boot area and 0xFC000–0xFFFFF are unchanged. On a
V15 that has run since, 5 sectors differ (0x93000, 0xE9000, 0xF2000, 0xF3000, 0xFB000): V15's own settings and
Bluetooth records. If 0xFC000 onwards ever differs, note which bytes: CLEANSWEEP can't write there, so it would
come from the install.

**The console (optional).** CLEANSWEEP's USB console is a `/dev/cu.usbmodem*` port, once the done screen is up:
`screen /dev/cu.usbmodemXXXX`, then `p` for the result (`range=00093000 ..000FC000 flash_ok=1 err=0 bad_at=…
erased=N pages_written=N`), `h` for help. The backups are the stronger check.

## Part 1: the mono V15, the reason for CLEANSWEEP

Start on V15 with M-VAVE's bootloader.

| Step | Command | What to look for |
|---|---|---|
| C0 | `swift run fm1probe download-mode`, then `sudo .build/debug/fm1probe flash-backup .artifacts/tmp/c-backups`; off and on | A backup of the FM-1 as it is now, to go back to. |
| C1 | `swift run fm1probe install .artifacts/tmp/v16/FM-1-v16.fwsc --log .artifacts/tmp/c1.csv` | V15 takes V16. Play a few notes, so V16 has run. |
| C2 | `swift run fm1probe install ../FM-SWITCH/App/Resources/Firmware/FM-1_015.fwsc --log .artifacts/tmp/c2.csv`, then `swift run fm1probe restore-voices ../FM-SWITCH/App/Resources/Firmware/FM-1_015.fwsc` | V15 is back with `PIANO 1`, but plays one note at a time (as in FM1Kit's stage R). |
| C3 | As C0 | The "before" backup: the mono V15. |
| C4 | `swift run fm1probe install ../cleansweep/build/cleansweep.fwsc --log .artifacts/tmp/c4.csv` | V15 takes CLEANSWEEP. Watch the FM-1 from the restart: CLEAN and SWEEP zip in on violet; a light runs OCT+, OCT-, the keys left to right, the top row of buttons right to left, the bottom row left to right; PLAY flashes green three times. REC (red) only glows as the light passes, and never lights brightly. Then "Clearing old settings…" with a fizzing bar, "Done: everything is fine." and PLAY's flashes again. **No sound at any point**, not even a pop at power-on. Note whether the animation is smooth or jerky, and write down the light order if it's wrong: only PLAY and REC have been placed on hardware. |
| C5 | Once the done screen is up: `swift run fm1probe identify` | `FM-1_257000`. Optional: the console's `p` gives `err=0` and `erased=` some tens of sectors. |
| C6 | Switch off and on | The same screens. The name and the light show take about 2 s, then the bar sweeps through in about 1.7 s with nothing to erase: about 4 s from power-on to "Done". Optional: `p` gives `erased=0 pages_written=0`. |
| C7 | As C0, then `python3 -I .artifacts/scripts/check_cleansweep.py <C7 backup> .artifacts/tmp/r3-diag/v15-usr.bin <C3 backup>` | 105 of 105 sectors as expected; the boot area and 0xFC000–0xFFFFF unchanged. |
| C8 | `swift run fm1probe install ../FM-SWITCH/App/Resources/Firmware/FM-1_015.fwsc --log .artifacts/tmp/c8.csv` | CLEANSWEEP takes V15. **Chords play**, with `PIANO 1` and every bank, without restoring the voices. |
| C9 | On a phone, forget the FM-1, then pair it again | Pairing works, under a new Bluetooth address. |

## Part 2: other routes

| Step | Command | What to look for |
|---|---|---|
| C10 | As C1, then as C4 | V16 takes CLEANSWEEP, and the reset runs to "Done". |
| C11 | `swift run fm1probe install .artifacts/tmp/v16/FM-1-v16.fwsc --log .artifacts/tmp/c11.csv` | CLEANSWEEP takes V16. V16 plays its own voices (`PIANO 3` first). |
| C12 | As C4, then `swift run fm1probe install .artifacts/tmp/hops/ignis32_midimonitor_fm1/midimon-0.4.fwsc --log .artifacts/tmp/c12.csv` | V16 takes CLEANSWEEP again; CLEANSWEEP takes MIDIMON. |
| C13 | As C4, then as C8 | MIDIMON takes CLEANSWEEP; CLEANSWEEP takes V15, which plays chords. |
| C14 | From FM-SWITCH on an iPhone: CLEANSWEEP, then V15 | **Waiting:** FM-SWITCH doesn't offer CLEANSWEEP's package yet. Run this once it does. |

Later, if wanted: CLEANSWEEP over Felucca 1.6.1 or ESTAMPIE, and LOOM over CLEANSWEEP.

## Part 3: power cuts

| Step | Command | What to look for |
|---|---|---|
| C15 | As C1 and C2, then as C4; pull the power during "Clearing old settings…", a few times at different points, including twice in a row | Each next start carries on and ends with "Done". Then as C7: 105 of 105. If the FM-1 ever comes up in download mode instead of the screens, report it: it's a bug in the crash guard. |

## What to report

For each step: the commands' output, what the screen and lights did, anything from the speaker, and the check's
result for C7 and C15.
