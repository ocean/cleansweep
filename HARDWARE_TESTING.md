# Hardware testing

Drew runs every step that changes a device. Take a flash backup first (FM1Kit `fm1probe flash-backup`), and keep
download mode within reach: it's the way back from anything.

Install with the usual installer, e.g. `uv run bsp/tools/fm1_install.py build/cleansweep.fwsc`, FM-SWITCH or the
FM1Kit app.

## S1: the mono V15 (the reason for it)

1. V15 → V16 → V15. Check V15 plays one note at a time (as stage R found).
2. Install CLEANSWEEP over that V15. Expect CLEAN and SWEEP to zip in on a violet screen while a light runs round:
   OCT+, OCT-, the keys left to right, the top row of buttons right to left, the bottom row left to right, then PLAY
   flashes three times. Check the order: the buttons' places come from ESTAMPIE's panel table, measured only for
   PLAY and REC. Then "Clearing old settings…" with a fizzing bar, "Done: everything is fine." and PLAY's flashes
   again. No sound at any point, including a pop at power-on. Note whether the animations look smooth or jerky.
3. On the serial console (`p`): `err=0`, `erased=` some tens of sectors.
4. Turn it off and on. Expect the same screens, and `p` gives `erased=0 pages_written=0`.
5. Install V15 over it. Expect chords (polyphony) and the factory voices, without restoring voices.
6. Optional: a flash backup now, and compare 0x93000–0xFBFFF with a backup of a V15 after a download-mode erase.

## S2: from other firmwares

- From Felucca 1.6.1 (or ESTAMPIE) and from V16: CLEANSWEEP installs, resets, and V15 installs over it.
- From CLEANSWEEP: V16, ESTAMPIE and MIDIMON or LOOM install over it too.

## S3: power cuts

- Pull the power during "Clearing old settings…" (a few times, at different points). Each next start should finish
  with "Done". Two cuts in quick succession still end in "Done" (a cut isn't a crash: RAM forgets it).

## S4: Bluetooth

- After S1, pair the FM-1 (V15) with a phone again after forgetting it there. Expect it to work, under a new address.

## What to report

The screen at each step, the console's `p` line, and anything odd from the lights or the speaker.
