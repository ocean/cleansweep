# SWITCH_RESET

A tiny firmware for the M-VAVE FM-1 that clears what earlier firmwares left behind in its flash, so the next
firmware starts clean. It's for anyone switching firmwares with only a phone, or anything without a Mac: it installs
over USB-MIDI like any other firmware, does its job when it starts, then waits for the next firmware.

**Status: built and tested on the computer only. Not yet tried on an FM-1.**

## Why

Firmwares keep their settings in different places, and what one leaves can confuse the next. Installing V15 after
M-VAVE's V16, for instance, leaves V15 playing one note at a time: V16 writes over V15's settings record (0xFBD80 on).
Until now only a download-mode install from a Mac could clear that.

## What it does

Every time it starts:

1. The lights run once over the keys and buttons, and the screen says it's clearing old settings.
2. It brings the flash from **0x93000 to 0xFBFFF** to one picture: V15's factory data (voices and settings, as M-VAVE
   ship them) at 0xEA000, and everything else erased. It only erases a 4 KB sector that differs, and reads each one
   back. A start after that finds nothing to do.
3. The screen says it's done, that the next firmware can go on, and that Bluetooth devices will need pairing again.
   The lights run again.
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
| `firmware/switch_reset.c` | The firmware: the screen, the lights, USB and the update entry. Follows felucca-bsp's `examples/hwtest`. |
| `firmware/reset_core.c` | The reset itself, with no hardware in it. |
| `tests/reset_test.c` | The reset on a pretend flash: a power cut at every erase and write, then a restart, and more. |
| `tools/usr_data.py` | Takes V15's factory USR data out of M-VAVE's package at build time and checks it. |
| `tools/gen_screens.py` | Draws the screen's words (Inter Tight, white on black). |
| `tools/build.py` | Builds the update loader, the app and the package. |
| `bsp/` | [felucca-bsp](https://github.com/hugelton/felucca-bsp), pinned as a submodule. Its update check takes V15, V16, m-bootloader and Felucca-platform firmwares over any bootloader. |

The identity is **FM-1_379000** for development builds, and FM-1_379NNN for releases, so installers can tell it from
V15 and everything else.

## Build

    git submodule update --init
    uv sync --frozen
    tests/run_tests.sh
    uv run tools/build.py          # build/switch-reset.fwsc

The JieLi toolchain and AC79 SDK are the ones ESTAMPIE's `tools/setup_toolchain.sh` fetches and checks
(`tools/toolchain.lock` has the same pins); `JIELI_TOOLCHAIN` and `AC79_SDK` point elsewhere. On macOS the tools run in
the pinned distroless container, with no network, so Docker must be running.

## Licence

MPL-2.0 ([LICENSE](LICENSE)). V15's data in the firmware is M-VAVE's, Apache-2.0. Credits and details:
[NOTICE](NOTICE).
