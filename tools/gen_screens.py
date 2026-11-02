#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Drew Robinson
"""The screen's words, drawn once at build time: build/gen/screens.h.

  uv run tools/gen_screens.py --out build/gen

Each text is Cooper Hewitt italic (OFL, assets/fonts), leaning forward as if it's zooming along, anti-aliased to 16
levels and stored run-length coded: a byte is a level (high nibble) and a run of 1..16 pixels (low nibble + 1).
firmware/look.c draws them white over the violet; tools/preview.py shows the screens as the firmware draws them.
"""
import argparse
import hashlib
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "assets" / "fonts"
FONT_SHA256 = {                # the museum's own files, unmodified (assets/fonts/README.md)
    "Heavy": "d09f1625b5621dcfd15b223a9356ef8c1c4bb7860f37dad6dcf4e77d4a35de43",
    "Semibold": "d8ab09c6d0cc436c1503b6c44945a117581456e2dbfbbad779d471a4e3aa7cba",
    "Medium": "cacf1a9af4e769fe51215c02e12d773937877c1645c1adb08b85ee39efbc4289",
    "Book": "54033f47585fa4c33bed67a9fb0752ca5e273769ce9ff65a69c42144964bb855",
}
W = H = 240
MARGIN = 16

# name: (size, style, top, lines). A line wraps to the screen's width; the firmware shows texts in groups
# (SCREENS below). The words are Drew's to settle.
TEXTS = {
    "clean":    (46, "Heavy", 10, ["CLEAN"]),
    "sweep":    (46, "Heavy", 52, ["SWEEP"]),
    "clearing": (19, "Semibold", 102, ["Clearing old settings…"]),
    "leave_on": (16, "Medium", 130, ["Please leave the FM-1 on. It only takes a few seconds."]),
    "done":     (19, "Semibold", 102, ["Done: everything is fine."]),
    "next":     (16, "Medium", 130, ["You can install your next firmware now."]),
    "bluetooth": (14, "Book", 180, ["Bluetooth devices will need pairing again: forget the FM-1 on them first, then pair."]),
    "wrong":    (19, "Semibold", 102, ["Something went wrong."]),
    "again":    (16, "Medium", 130, ["Turn the FM-1 off and on again to try again."]),
    "installing": (19, "Semibold", 102, ["Installing your next firmware…"]),
    "stopped":  (16, "Medium", 130, ["The install stopped. Nothing has changed: you can try it again."]),
    "digits":   (16, "Semibold", 214, ["0123456789ABCDEF"]),
}
BAR_Y, BAR_H = 196, 6          # the progress bar (firmware/look.c draws it)
FIZZ = 16                      # the rows above and below it that its sparks may reach
SCREENS = {                    # what the firmware shows together, for the overlap check
    "clearing": ["clean", "sweep", "clearing", "leave_on", "BAR"],
    "done": ["clean", "sweep", "done", "next", "bluetooth"],
    "wrong": ["clean", "sweep", "wrong", "again", "digits"],
    "installing": ["clean", "sweep", "installing", "BAR"],
    "stopped": ["clean", "sweep", "wrong", "stopped"],
}


def font(size, style):
    path = FONTS / f"CooperHewitt-{style}Italic.otf"
    if hashlib.sha256(path.read_bytes()).hexdigest() != FONT_SHA256[style]:
        raise SystemExit(f"gen_screens: {path.name} is not the museum's file")
    return ImageFont.truetype(str(path), size)


def wrap(f, text, width):
    lines, line = [], ""
    for word in text.split(" "):
        test = f"{line} {word}".strip()
        if f.getlength(test) <= width or not line:
            line = test
        else:
            lines.append(line)
            line = word
    return lines + [line]


def render(name):
    size, style, top, paras = TEXTS[name]
    f = font(size, style)
    lines = [ln for p in paras for ln in wrap(f, p, W - 2 * MARGIN)]
    step = round(size * 1.3)
    img = Image.new("L", (W + 2 * MARGIN, step * len(lines) + size), 0)
    d = ImageDraw.Draw(img)
    for i, ln in enumerate(lines):
        d.text((W / 2 + MARGIN, i * step), ln, font=f, fill=255, anchor="mt")
    box = img.getbbox()
    if name == "digits":      # one glyph per 16th of the width, so the firmware can pick them out
        img = Image.new("L", (16 * 11, size + 4), 0)
        d = ImageDraw.Draw(img)
        for i, ch in enumerate(TEXTS[name][3][0]):
            d.text((i * 11 + 5, 0), ch, font=f, fill=255, anchor="mt")
        box = (0, img.getbbox()[1], img.width, img.getbbox()[3])
    img = img.crop(box)
    x = (W - img.width) // 2 if name != "digits" else 0
    return x, top + box[1], img


def rle(img):
    out = bytearray()
    for y in range(img.height):          # runs never cross a row: the firmware draws a row at a time
        row = [p >> 4 for p in img.tobytes()[y * img.width:(y + 1) * img.width]]
        i = 0
        while i < len(row):
            n = 1
            while i + n < len(row) and n < 16 and row[i + n] == row[i]:
                n += 1
            out.append(row[i] << 4 | (n - 1))
            i += n
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    parts, defs, total = {}, [], 0
    for name in TEXTS:
        x, y, img = render(name)
        if y + img.height > H or img.width > W:
            raise SystemExit(f"gen_screens: {name} doesn't fit the screen")
        data = rle(img)
        parts[name] = (x, y, img)
        total += len(data)
        body = ",".join(f"0x{b:02X}" for b in data)
        defs.append(f"static const uint8_t TX_{name.upper()}_RLE[{len(data)}] = {{{body}}};\n"
                    f"static const cs_text_t TX_{name.upper()} = {{{x}, {y}, {img.width}, {img.height}, "
                    f"TX_{name.upper()}_RLE, {len(data)}}};\n")
    parts["BAR"] = (0, BAR_Y - FIZZ, Image.new("L", (W, BAR_H + 2 * FIZZ)))   # the bar, with its sparks' room
    for name, (x, y, img) in parts.items():
        for other, (ox, oy, oimg) in parts.items():
            if other == name or "digits" in (name, other):
                continue
            together = any(name in s and other in s for s in SCREENS.values())
            if together and y < oy + oimg.height and oy < y + img.height:
                raise SystemExit(f"gen_screens: {name} and {other} overlap")
    (a.out / "screens.h").write_text(
        "/* Generated by tools/gen_screens.py: the screen's words, Cooper Hewitt italic (OFL), 16 levels, run-length coded. */\n"
        "typedef struct { uint16_t x, y, w, h; const uint8_t *rle; uint32_t n; } cs_text_t;\n"
        f"#define CS_BAR_Y {BAR_Y}\n#define CS_BAR_H {BAR_H}\n#define CS_FIZZ {FIZZ}\n#define CS_DIGIT_W 11u\n" + "".join(defs))
    print(f"gen_screens: {len(TEXTS)} texts, {total} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
