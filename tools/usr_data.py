#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Drew Robinson
"""V15's factory USR data, taken out of M-VAVE's own package at build time.

  tools/usr_data.py [--package assets/m-vave/FM-1_015.fwsc] [--header OUT.h] [--bin OUT.bin]

The USR part of FM-1_015.fwsc (Apache-2.0, m-vave/fm1_open) is V15's voices and settings as they sit in flash from
0xEA000: four 4 KB voice banks, then the rest of its factory data, settings record and all. Every 32-byte block of
the part is enciphered with a key made from its offset in the package's logical image (seed 0x980F). This follows
FM1Kit's FwscPackage.factoryUserData() (FactoryVoices.swift, FirmwareContainer.swift): the same checks, the same
answer. Nothing is copied by hand: a different package, or a different answer, stops the build.
"""
import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "assets" / "m-vave" / "FM-1_015.fwsc"
PACKAGE_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
VOICES_SHA256 = "ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25"   # M-VAVE's updater pins it
USR_SHA256 = "2a76d6abfe229c20ba4e884194d466e6d4bcab5a030d73fcfb74bec2db058479"      # the whole part, deciphered
USR_LEN = 73226
USR_BASE = 0xEA000            # where V15 keeps it in flash
BLOCKS, BLOCK, IMAGE_PER_BLOCK, UNUSED = 20, 0x30, 0x2F, 0x7D   # the .fwsc identity markers
KEY_SEED = 0x980F


def crc16(data, c=0):
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def decipher(data, key=0xFFFF):
    out = bytearray(data)
    for i in range(len(out)):
        out[i] ^= key & 0xFF
        key = ((key << 1) ^ 0x1021) & 0xFFFF if key & 0x8000 else (key << 1) & 0xFFFF
    return bytes(out)


def logical_image(fwsc):
    """the package without its identity markers: the last byte of each of the first 20 blocks of 0x30"""
    return b"".join(fwsc[i * BLOCK:i * BLOCK + IMAGE_PER_BLOCK] for i in range(BLOCKS)) + fwsc[BLOCKS * BLOCK:]


def le(b, at, n):
    return int.from_bytes(b[at:at + n], "little")


def user_data(fwsc):
    image = logical_image(fwsc)
    head = decipher(image[:64])
    count = le(head, 8, 2)
    if le(head, 0, 2) != crc16(head[2:]) or le(head, 4, 4) != len(image) or head[16:22] != b"AC791N":
        raise SystemExit("usr_data: the package's header is damaged")
    if crc16(image[64:64 + count * 80]) != le(head, 2, 2):
        raise SystemExit("usr_data: the package's table of parts is damaged")
    for i in range(count):
        e = decipher(image[64 + i * 80:64 + (i + 1) * 80])
        name = e[64:].split(b"\0")[0]
        if le(e, 0, 2) != 50 or name != b"USR":
            continue
        off, size, stored, enc, check = le(e, 8, 4), le(e, 12, 4), le(e, 16, 4), le(e, 24, 4), le(e, 4, 2)
        if enc != stored or off + stored > len(image):
            raise SystemExit("usr_data: the USR part is not stored as V15 stores it")
        plain = b"".join(decipher(image[s:min(s + 32, off + stored)], KEY_SEED ^ ((s >> 2) & 0xFFFF))
                         for s in range(off, off + stored, 32))[:size]
        if crc16(plain) != check:
            raise SystemExit("usr_data: the USR part does not decipher to its checksum")
        return plain
    raise SystemExit("usr_data: the package has no USR part")


def records_in(data):
    """offsets (in flash) where the bytes would pass for an update record, which a loader clears (felucca-bsp
    fm1_flash.h: 'AT' at bytes 6..7 of a 4 KiB boundary - 256, and a valid CRC)"""
    found = []
    for sector in range(USR_BASE, USR_BASE + len(data), 0x1000):
        at = sector + 0xF00 - USR_BASE
        r = data[at:at + 80]
        if len(r) == 80 and r[6:8] == b"AT" and crc16(r[2:80]) == le(r, 0, 2):
            found.append(sector + 0xF00)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", type=Path, default=PACKAGE)
    ap.add_argument("--header", type=Path, help="write a C header with the bytes (usr_data[], USR_DATA_LEN)")
    ap.add_argument("--bin", type=Path, help="write the bytes as they are")
    a = ap.parse_args()
    fwsc = a.package.read_bytes()
    if hashlib.sha256(fwsc).hexdigest() != PACKAGE_SHA256:
        raise SystemExit(f"usr_data: {a.package} is not M-VAVE's V15 (SHA-256 {PACKAGE_SHA256[:16]}…)")
    data = user_data(fwsc)
    if hashlib.sha256(data[:16384]).hexdigest() != VOICES_SHA256:
        raise SystemExit("usr_data: the voices are not V15's factory voices")
    if len(data) != USR_LEN or hashlib.sha256(data).hexdigest() != USR_SHA256:
        raise SystemExit(f"usr_data: the USR part changed: {len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()}")
    if records_in(data):
        raise SystemExit(f"usr_data: the data holds what a loader takes for an update record at {records_in(data)}")
    if a.bin:
        a.bin.write_bytes(data)
    if a.header:
        rows = ",\n".join("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16))
        a.header.write_text(
            "/* Generated by tools/usr_data.py from M-VAVE's FM-1_015.fwsc (Apache-2.0, LICENSES/Apache-2.0.txt): V15's\n"
            f" * factory voices and data as V15 keeps them in flash from 0x{USR_BASE:X}. SHA-256 {USR_SHA256}. */\n"
            f"#define USR_DATA_BASE 0x{USR_BASE:X}u\n#define USR_DATA_LEN {len(data)}u\n"
            f"static const uint8_t usr_data[USR_DATA_LEN] = {{\n{rows}\n}};\n")
    print(f"usr_data: {len(data)} bytes for 0x{USR_BASE:X}..0x{USR_BASE + len(data):X}, SHA-256 {USR_SHA256[:16]}…")
    return 0


if __name__ == "__main__":
    sys.exit(main())
