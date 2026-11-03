# How CLEANSWEEP works

The detail behind the [README](../README.md), for anyone working on it.

## Why

Firmwares keep their settings in different places, and what one leaves can confuse the next. Installing V15 after
M-VAVE's V16, for instance, leaves V15 playing one note at a time: V16 writes over V15's settings record (0xFBD80 on).
Until now only a download-mode install from a Mac could clear that.

## What it does

Every time it starts:

1. CLEAN zips onto the deep violet screen from the left and SWEEP from the right, while a light runs once round the
   FM-1: OCT+, OCT-, the keys from left to right, the top row of buttons from the right, the bottom row from the left,
   and PLAY flashes green. The screen says it's clearing old settings.
2. It brings the flash from **0x93000 to 0xFBFFF** to one picture: V15's factory data (voices and settings, as M-VAVE
   ship them) at 0xEA000, and everything else erased. It only erases a 4 KB sector that differs, and reads each one
   back. A bar fizzing with sparks shows how far it is. A start after that finds nothing to do.
3. The screen says it's done, that the next firmware can go on, and that Bluetooth devices will need pairing again.
   PLAY flashes again.
4. USB comes up and the installer (FM-SWITCH, FM1Kit, a web installer, `fm1_install.py`) can install the next
   firmware: V15 (its voices are already back), V16, or anything else.

What it never touches: the boot area and its own image (below 0x93000), Felucca's store at 0xFC000–0xFEFFF (where
Melodee and LOOM keep their Bluetooth details) and 0xFF000. The flash driver itself refuses anything outside the range.
It makes no sound at all: the audio is never started.

**Power cuts** are safe. Nothing records that the reset was done: each start compares again and finishes whatever a
cut left half done. If a sector won't take its data after three tries, the screen says something went wrong and
shows where; the next firmware can still be installed.

**Bluetooth:** the range holds V15's Bluetooth calibration and pairing records and the address at 0xE9000. V15
measures again and makes a new address when they're missing, so devices paired before need pairing again: forget the
FM-1 on them first.

## How it works

| Where | What |
| --- | --- |
| `firmware/cleansweep.c` | The firmware: the screen, the lights, USB and the update entry. Follows felucca-bsp's `examples/hwtest`. |
| `firmware/reset_core.c` | The reset itself, with no hardware in it. |
| `firmware/look.c` | The screen: the violet, the words, CLEAN and SWEEP zipping in, the bar's fizz. No hardware in it. |
| `firmware/lights.c` | The light show round the keys and buttons, and PLAY's flashes. No hardware in it. |
| `tests/reset_test.c` | The reset on a pretend flash: a power cut at every erase and write, then a restart, and more. |
| `tests/look_test.c` | The look and the lights on a pretend screen and clock. |
| `tools/preview.py` | An animated picture of a start, from `look_test.c`: the screen and a sketch of the panel's lights. |
| `tools/hw_check.py` | The real package on emulated FM-1 hardware: a whole reset, ending on the done screen. |
| `tools/usr_data.py` | Takes V15's factory USR data out of M-VAVE's package at build time and checks it. |
| `tools/gen_screens.py` | Draws the screen's words in Cooper Hewitt italic ([assets/fonts](../assets/fonts/README.md)). |
| `tools/build.py` | Builds the update loader, the app and the package. |
| `bsp/` | [felucca-bsp](https://github.com/hugelton/felucca-bsp), pinned as a submodule. Its update check takes V15, V16, m-bootloader and Felucca-platform firmwares over any bootloader. |

The identity is **FM-1_257000** for development builds, and FM-1_257NNN for releases, so installers can tell it from
V15 and everything else. 257 spells CLS on a phone keypad (ESTAMPIE's 378 spells EST).

## Build

    git submodule update --init
    uv sync --frozen
    tests/run_tests.sh
    uv run tools/build.py          # build/cleansweep.fwsc
    uv run tools/preview.py        # build/preview/start.gif: what a start looks like
    uv run tools/hw_check.py       # the package on emulated FM-1 hardware, about a minute

The JieLi toolchain and AC79 SDK are the ones ESTAMPIE's `tools/setup_toolchain.sh` fetches and checks
(`tools/toolchain.lock` has the same pins); `JIELI_TOOLCHAIN` and `AC79_SDK` point elsewhere. On macOS the tools run in
the pinned distroless container, with no network, so Docker must be running.

`tools/hw_check.py` boots the built package on [shcherbakov7's emulator of the FM-1's
hardware](https://github.com/shcherbakov7/m-wave-fm1-browser-emulator) (GPL-3.0, run, not included), the copy
ESTAMPIE pins and builds (`tools/setup_toolchain.sh --only hw` there), or the one `FM1_HW_EMU` points at. It checks
the reset runs to the end without stopping or starting the audio, USB comes up after it, and the last screen is the
done screen exactly as `look_test.c` draws it. It can't see the LEDs, doesn't run at the FM-1's speed and starts from
its own flash, so the device still has the last word.
