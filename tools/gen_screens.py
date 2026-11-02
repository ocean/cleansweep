#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Drew Robinson
"""The screen's words, drawn once at build time: build/gen/screens.h (and a picture of each screen to look at).

  uv run tools/gen_screens.py --out build/gen [--png DIR]

Each text is white on black in Inter Tight (OFL, assets/fonts), anti-aliased to 16 levels and stored run-length
coded: a byte is a level (high nibble) and a run of 1..16 pixels (low nibble + 1). The firmware (switch_reset.c)
draws a text at its place and the progress bar between them; nothing else is on the screen.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets" / "fonts" / "InterTight[wght].ttf"
W = H = 240
MARGIN = 16

# name: (size, weight, top, lines). A line wraps to the screen's width; the firmware shows texts in groups
# (SCREENS below). The words are Drew's to settle.
TEXTS = {
    "title":    (25, 700, 22, ["SWITCH RESET"]),
    "clearing": (19, 500, 76, ["Clearing old settings…"]),
    "leave_on": (16, 400, 108, ["Please leave the FM-1 on. It only takes a few seconds."]),
    "done":     (19, 600, 70, ["Done: everything is fine."]),
    "next":     (16, 400, 104, ["You can install your next firmware now."]),
    "bluetooth": (14, 400, 164, ["Bluetooth devices will need pairing again: forget the FM-1 on them first, then pair."]),
    "wrong":    (19, 600, 70, ["Something went wrong."]),
    "again":    (16, 400, 104, ["Turn the FM-1 off and on again to try again."]),
    "installing": (19, 500, 76, ["Installing your next firmware…"]),
    "stopped":  (16, 400, 108, ["The install stopped. Nothing has changed: you can try it again."]),
    "digits":   (14, 500, 214, ["0123456789ABCDEF"]),
}
BAR_Y, BAR_H = 150, 6          # the progress bar (drawn by the firmware), for the pictures here
SCREENS = {                    # what the firmware shows together (for the pictures; the firmware says it itself)
    "clearing": ["title", "clearing", "leave_on", "BAR"],
    "done": ["title", "done", "next", "bluetooth"],
    "wrong": ["title", "wrong", "again"],
    "installing": ["title", "installing", "BAR"],
    "stopped": ["title", "wrong", "stopped"],
}


def font(size, weight):
    f = ImageFont.truetype(str(FONT), size)
    f.set_variation_by_axes([weight])
    return f


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
    size, weight, top, paras = TEXTS[name]
    f = font(size, weight)
    lines = [ln for p in paras for ln in wrap(f, p, W - 2 * MARGIN)]
    step = round(size * 1.3)
    img = Image.new("L", (W, step * len(lines) + size), 0)
    d = ImageDraw.Draw(img)
    for i, ln in enumerate(lines):
        d.text((W / 2, i * step), ln, font=f, fill=255, anchor="mt")
    box = img.getbbox()
    if name == "digits":      # one glyph per 16th of the width, so the firmware can pick them out
        img = Image.new("L", (16 * 10, size + 4), 0)
        d = ImageDraw.Draw(img)
        for i, ch in enumerate(TEXTS[name][3][0]):
            d.text((i * 10 + 5, 0), ch, font=f, fill=255, anchor="mt")
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
    ap.add_argument("--png", type=Path, help="also a picture of each screen, as the firmware draws it")
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
                    f"static const sr_text_t TX_{name.upper()} = {{{x}, {y}, {img.width}, {img.height}, "
                    f"TX_{name.upper()}_RLE, {len(data)}}};\n")
    for name, (x, y, img) in parts.items():
        for other, (ox, oy, oimg) in parts.items():
            if other == name or "digits" in (name, other):
                continue
            together = any(name in s and other in s for s in SCREENS.values())
            if together and y < oy + oimg.height and oy < y + img.height:
                raise SystemExit(f"gen_screens: {name} and {other} overlap")
    (a.out / "screens.h").write_text(
        "/* Generated by tools/gen_screens.py: the screen's words, Inter Tight (OFL), 16 levels, run-length coded. */\n"
        "typedef struct { uint16_t x, y, w, h; const uint8_t *rle; uint32_t n; } sr_text_t;\n"
        f"#define SR_BAR_Y {BAR_Y}u\n#define SR_BAR_H {BAR_H}u\n#define SR_DIGIT_W 10u\n" + "".join(defs))
    if a.png:
        a.png.mkdir(parents=True, exist_ok=True)
        for screen, names in SCREENS.items():
            shot = Image.new("L", (W, H), 0)
            for n in names:
                if n == "BAR":
                    ImageDraw.Draw(shot).rectangle((MARGIN, BAR_Y, W - MARGIN - 1, BAR_Y + BAR_H - 1), fill=40)
                    ImageDraw.Draw(shot).rectangle((MARGIN, BAR_Y, W // 2, BAR_Y + BAR_H - 1), fill=255)
                else:
                    x, y, img = parts[n]
                    shot.paste(Image.eval(img, lambda p: (p >> 4) * 17), (x, y))
            shot.resize((W * 2, H * 2), Image.NEAREST).save(a.png / f"{screen}.png")
    print(f"gen_screens: {len(TEXTS)} texts, {total} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
