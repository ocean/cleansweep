#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Drew Robinson
"""The real package on emulated FM-1 hardware: boots build/cleansweep.fwsc on shcherbakov7's emulator of the FM-1
(its pi32v2 cores, screen, flash and USB) and checks it runs a whole reset: nothing stops it, no sound is started, USB
comes up after it, and the screen it ends on is the done screen, pixel for pixel as tests/look_test.c draws it.

  uv run tools/hw_check.py [--package build/cleansweep.fwsc] [--instructions 2500000000]

The emulator is the one ESTAMPIE pins and builds (its tools/setup_toolchain.sh --only hw), found next to this repo
or at $FM1_HW_EMU. It starts from its own flash, not one a firmware has used, and doesn't show the LEDs or run at the
FM-1's speed: the light show, smoothness and a real V16 leftover are still for the device. Takes about a minute.
Writes build/hw/screen.png.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
EMU = ROOT.parent / "estampie/.toolchain/emulators/m-wave-fm1-browser-emulator/emulator/target/release/examples/diagnose"
W = H = 240
TOLERANCE = 2                                         # the emulator rounds RGB565 to 8 bits a level apart


def emulator():
    path = Path(os.environ.get("FM1_HW_EMU", EMU))
    if not os.access(path, os.X_OK):
        sys.exit(f"hw_check: no emulator at {path}: build it with ESTAMPIE's tools/setup_toolchain.sh --only hw, "
                 "or point FM1_HW_EMU at its examples/diagnose")
    return path


def done_screen(out):
    """The done screen as look.c draws it (look_test --frames), RGB565."""
    gen, raw = ROOT / "build" / "gen", out / "look"
    raw.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, ROOT / "tools" / "gen_screens.py", "--out", gen], check=True, stdout=subprocess.DEVNULL)
    exe = out / "look_test"
    subprocess.run(["cc", "-std=c99", "-O1", f"-I{gen}", ROOT / "tests" / "look_test.c", "-o", exe], check=True)
    subprocess.run([exe, "--frames", raw], check=True, stdout=subprocess.DEVNULL)
    return memoryview((raw / "done.raw").read_bytes())[:W * H * 2].cast("H")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", type=Path, default=ROOT / "build" / "cleansweep.fwsc")
    ap.add_argument("--instructions", type=int, default=2_500_000_000, help="the done screen is up by about 2e9")
    a = ap.parse_args()
    out = ROOT / "build" / "hw"
    out.mkdir(parents=True, exist_ok=True)
    ppm = out / "screen.ppm"
    ppm.unlink(missing_ok=True)
    run = subprocess.run([emulator(), a.package, str(a.instructions)], capture_output=True, text=True,
                         env={**os.environ, "FM1_LCD_PPM": str(ppm), "DIAG_FAST": "1"})
    log = run.stdout + run.stderr
    stop = re.search(r"^diagnose: (.*)$", log, re.M)
    usb = re.search(r"USB: (\d+) host setups", log)
    audio = re.search(r"audio: (\d+) stereo frames", log)
    problems = []
    if not stop or not stop.group(1).startswith("instruction limit"):
        problems.append(f"it stopped: {stop.group(1) if stop else 'no word from the emulator'}")
    if not usb or int(usb.group(1)) == 0:
        problems.append("USB never came up (it comes up after the reset)")
    if not audio or int(audio.group(1)) != 0:
        problems.append("it started the audio")
    if ppm.exists():
        Image.open(ppm).save(out / "screen.png")
        want = done_screen(out)
        got = Image.open(ppm).convert("RGB").get_flattened_data()
        off = sum(max(abs((c >> 11) * 255 // 31 - r), abs((c >> 5 & 63) * 255 // 63 - g),
                      abs((c & 31) * 255 // 31 - b)) > TOLERANCE for c, (r, g, b) in zip(want, got))
        if off:
            problems.append(f"the screen isn't the done screen: {off} pixels differ (see build/hw/screen.png)")
    else:
        problems.append("nothing drawn")
    if problems:
        print(log[-2000:])
        for p in problems:
            print(f"hw_check: FAIL: {p}")
        return 1
    print(f"hw_check: ok: {a.package.name} ran a whole reset to the done screen, USB up after it "
          f"({a.instructions:,} instructions); screen in build/hw/screen.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
