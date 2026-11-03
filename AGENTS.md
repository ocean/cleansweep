# AGENTS.md: working on CLEANSWEEP

CLEANSWEEP is a tiny FM-1 firmware that clears saved data left by earlier firmwares (README.md says what it is for; docs/HOW_IT_WORKS.md the detail and the build).
It's maintained by Drew Robinson (GitHub `ocean`), built by the ESTAMPIE agent session, and tracked in kata
(project `fm-1`, issue `magt`, labels `estampie`, `fm1kit`, `fm-switch`).

- **It erases flash.** Only [0x93000, 0xFC000) may ever be erased or programmed: `FL_RANGE_OK` in
  `firmware/cleansweep.c` makes the flash driver refuse anything else. Don't widen it without Drew.
- **Tests before every commit:** `tests/run_tests.sh`, and `uv run tools/build.py` must build.
- **The look:** `firmware/look.c` and `firmware/lights.c` hold no hardware; `uv run tools/preview.py` shows a start
  as the firmware draws it. The words are Cooper Hewitt italic, pinned by SHA-256 in `tools/gen_screens.py`.
- **Hardware** is tested by Drew only (HARDWARE_TESTING.md). Before he does, `uv run tools/hw_check.py` runs the
  package on emulated FM-1 hardware (ESTAMPIE's pinned emulator). Say plainly what has only been tested on the
  computer or the emulator.
- **Spelling:** Australian/British English. A one-line comment ends with a full stop.
- **Git:** `git --no-pager ...`; commits as drew.robinson@gmail.com; messages in prose. Creating the GitHub
  repository, publishing and releasing are Drew's call.
- **Python** through uv only (`uv sync --frozen`); anything new is pinned by hash and scanned with osv-scanner.
- **Scratch work** goes in `.artifacts/` (git-ignored).
- **bsp/** is felucca-bsp, a submodule pinned by commit: change it upstream, never here.
- **V15's data** comes from `assets/m-vave/FM-1_015.fwsc` at build time (`tools/usr_data.py`, pinned by SHA-256);
  never hand-copy bytes.
