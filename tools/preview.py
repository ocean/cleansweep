#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Drew Robinson
"""A start of CLEANSWEEP as the firmware draws it, to look at without an FM-1: an animated picture of the screen
over a plain drawing of the panel's lights, and a picture of each other screen.

  uv run tools/preview.py [--out build/preview]

It runs tests/look_test.c, the real firmware/look.c and firmware/lights.c on a pretend screen and clock, so what it
shows is what the firmware draws (the panel is only a sketch: the keys' places, not the FM-1's looks).
"""
import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
W = H = 240
NLED = 41
GLOW, MID, LIT = 1, 2, 3
SCALE = 2
TOP = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"]           # their board ids (ESTAMPIE's panel.c)
TOP_ID = [2, 3, 4, 5, 6, 7]
BOTTOM = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
BOTTOM_ID = [8, 9, 10, 11, 12, 13]
COLOUR = {12: (0x8F, 0xE3, 0x6B), 13: (0xFF, 0x5A, 0x4F)}    # PLAY green, REC red, the rest white
BLACK_AFTER = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12}             # white keys from F (of a 7) followed by a black key


def screen(raw):
    px = memoryview(raw)[:W * H * 2].cast("H")
    img = Image.new("RGB", (W, H))
    img.putdata([((c >> 11) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31) for c in px])
    return img.resize((W * SCALE, H * SCALE), Image.NEAREST)


def led(d, box, level, colour=(255, 255, 255)):
    k = {0: 0.0, GLOW: 0.12, MID: 0.4, LIT: 1.0}[level]
    fill = tuple(int(30 + (c - 30) * k) for c in colour)
    d.rounded_rectangle(box, radius=4, fill=fill, outline=(70, 70, 76))


def panel(levels):
    img = Image.new("RGB", (W * SCALE, 150), (24, 24, 28))
    d = ImageDraw.Draw(img)
    for row, (names, ids) in enumerate(((TOP, TOP_ID), (BOTTOM, BOTTOM_ID))):
        for i, (name, n) in enumerate(zip(names, ids)):
            x, y = 120 + i * 58, 8 + row * 30
            led(d, (x, y, x + 50, y + 22), levels[n], COLOUR.get(n, (255, 255, 255)))
            d.text((x + 25, y + 11), name, fill=(120, 120, 130), anchor="mm")
    for i, (name, n) in enumerate((("OCT-", 0), ("OCT+", 1))):
        x = 8 + i * 52
        led(d, (x, 38, x + 46, 60), levels[n])
        d.text((x + 23, 49), name, fill=(120, 120, 130), anchor="mm")
    whites, blacks, key = [], [], 14                       # F3..G5: 16 white keys, 11 black
    for w in range(16):
        whites.append((w, key))
        key += 1
        if w % 7 in BLACK_AFTER and w < 15 and key <= 40:
            blacks.append((w, key))
            key += 1
    kw = (W * SCALE - 16) / 16
    for w, n in whites:
        x = 8 + w * kw
        led(d, (x, 72, x + kw - 3, 142), levels[n])
    for w, n in blacks:
        x = 8 + (w + 1) * kw - kw * 0.32
        led(d, (x, 72, x + kw * 0.6, 112), levels[n], (230, 230, 255))
    return img


def frame(raw):
    img = Image.new("RGB", (W * SCALE, H * SCALE + 150))
    img.paste(screen(raw), (0, 0))
    img.paste(panel(raw[W * H * 2:W * H * 2 + NLED]), (0, H * SCALE))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "preview")
    a = ap.parse_args()
    raw = a.out / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    for f in raw.glob("*.raw"):
        f.unlink()
    gen = ROOT / "build" / "gen"
    subprocess.run([sys.executable, ROOT / "tools" / "gen_screens.py", "--out", gen], check=True)
    exe = a.out / "look_test"
    subprocess.run(["cc", "-std=c99", "-O1", f"-I{gen}", ROOT / "tests" / "look_test.c", "-o", exe], check=True)
    subprocess.run([exe, "--frames", raw], check=True)
    frames = [frame(f.read_bytes()) for f in sorted(raw.glob("f*.raw"))]
    frames[0].save(a.out / "start.gif", save_all=True, append_images=frames[1:], duration=40, loop=0)
    for name in ("wrong", "installing", "stopped"):
        screen((raw / f"{name}.raw").read_bytes()).save(a.out / f"{name}.png")
    frames[-1].save(a.out / "done.png")
    print(f"preview: {a.out / 'start.gif'} ({len(frames)} frames), done.png, wrong.png, installing.png, stopped.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
