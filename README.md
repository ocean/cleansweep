# CLEANSWEEP

A repair tool for the M-VAVE FM-1, not a firmware to play. It wipes what earlier firmwares saved in the FM-1's
flash, so the next firmware you install starts clean.

**Only use it if a firmware misbehaves after you've switched from another one**, for example V15 playing one note at
a time after M-VAVE's V16. If your FM-1 works, you don't need it.

It erases most of what firmwares have saved, such as sounds, patterns and V15's Bluetooth pairings. There is no
undo. V15's factory voices are put back. When it says it's done, install the firmware you want next. CLEANSWEEP does nothing
else: it makes no sound.

**Status: tested on a computer only, not yet on an FM-1.**

How it works and how to build it: [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).

## Licence

MPL-2.0 ([LICENSE](LICENSE)). V15's data in the firmware is M-VAVE's, Apache-2.0. Credits and details:
[NOTICE](NOTICE).
