#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Drew Robinson
"""Build CLEANSWEEP: build/cleansweep.bin (the app), build/loader/ota.bin (felucca-bsp's update loader) and
build/cleansweep.fwsc (the package any FM-1 installer takes).

  uv run tools/build.py [--release N]

--release N gives the package the identity FM-1_257NNN (dev builds FM-1_257000).

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it (default: ../estampie/.toolchain/jieli/
toolchain, which ESTAMPIE's tools/setup_toolchain.sh fetches and checks against the same pin as tools/toolchain.lock
here); on macOS each tool runs in the pinned distroless container (build-image in tools/toolchain.lock), with no
network. AC79_SDK points at JieLi's AC79 SDK (default ../estampie/.toolchain/ac79-sdk): fm1pkg.py takes three of its
files (Apache-2.0) for the package. The loader build and its wrapping follow fm1-base's tools/build.py (Charles
Vestal, MPL-2.0).
"""
import argparse
import os
import platform
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BSP = ROOT / "bsp"
OUT = ROOT / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(ROOT / "tools"))
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = 0x8DFBC                  # the app area, 0x4000..0x93000, less the package's own parts (fm1pkg.py)
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Werror", "-Wno-unused-function"]
INC = ["-Ibsp/chip", "-Ibsp/boards/fm1", "-Ibuild/gen"]
NAME = "cleansweep"


def lock(name):
    for line in (ROOT / "tools" / "toolchain.lock").read_text().splitlines():
        f = line.split()
        if f and f[0] == name:
            return f
    raise SystemExit(f"build: no {name} in tools/toolchain.lock")


def toolchain():
    tc = Path(os.environ.get("JIELI_TOOLCHAIN") or ROOT.parent / "estampie" / ".toolchain" / "jieli" / "toolchain")
    if not (tc / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain (the directory with pi32v2/ and "
                         "common/), pinned in tools/toolchain.lock")
    return tc.resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary with cwd ROOT; Path arguments relative to ROOT"""
    rel = [str(Path(a).resolve().relative_to(ROOT)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():                # as you, no network, no capabilities, read-only but for build/
        f = lock("build-image")
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "--network", "none", "--cap-drop", "ALL",
               "--security-opt", "no-new-privileges", "--pids-limit", "64", "--read-only", "--tmpfs", "/tmp",
               "--user", f"{os.getuid()}:{os.getgid()}", "-v", f"{ROOT}:/work:ro", "-v", f"{OUT}:/work/build",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", f"{f[4]}@sha256:{f[3]}", f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = BSP / "loader"
    tc("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o")
    tc("cc", *CFLAGS, "-Ibsp/chip", "-Ibsp/boards/fm1", "-c", src / "loader.c", "-o", LDR / "loader.o")
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    tc("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin")
    for ln in tc("common/bin/objdump", "-h", elf).splitlines():   # the loader rewrites the flash: all of it in RAM
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    return ota


def build_app(product):
    o = OUT / "app"
    o.mkdir(parents=True, exist_ok=True)
    objs = []
    for s in ("crt0.S", "fm1_vec.S", "fm1_isr.S"):
        tc("cc", "-c", BSP / "chip" / s, "-o", o / (s[:-2] + ".o"))
        objs.append(o / (s[:-2] + ".o"))
    tc("cc", *CFLAGS, *INC, f'-DFELUCCA_ID="{product}"', "-c", ROOT / "firmware" / "cleansweep.c",
       "-o", o / "cleansweep.o")
    elf = o / f"{NAME}.elf"
    tc("pi32v2/bin/ld", "-T", ROOT / "firmware" / "app.ld", *objs, o / "cleansweep.o", "-o", elf)
    for sec, f in ((".text", "text.bin"), (".data", "data.bin"), (".ram_text", "rt.bin")):
        tc("common/bin/objcopy", "-O", "binary", "-j", sec, elf, o / f)
    syms = tc("common/bin/objdump", "-t", elf)

    def sym(name):
        return int(re.search(rf"^([0-9a-f]+) .*\s{name}$", syms, re.M).group(1), 16)

    if sym("_start") != APP_XIP:
        raise SystemExit(f"app: _start at {sym('_start'):#x}, not {APP_XIP:#x}")
    img = bytearray((o / "text.bin").read_bytes())
    for at, part in sorted([(sym("_rt_load"), (o / "rt.bin").read_bytes()),
                            (sym("_data_load"), (o / "data.bin").read_bytes())]):
        if part:
            if at - APP_XIP < len(img):
                raise SystemExit("app: sections overlap")
            img += b"\xff" * (at - APP_XIP - len(img)) + part
    img += b"\xff" * (-len(img) % 4)
    if len(img) > APP_SLOT:
        raise SystemExit(f"app: {len(img)} B, more than the app area's {APP_SLOT} B")
    return bytes(img)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", type=int, metavar="N", help="identity FM-1_257NNN (default 000: a dev build)")
    a = ap.parse_args()
    product = f"FM-1_257{(a.release or 0):03d}"
    sdk = Path(os.environ.get("AC79_SDK") or ROOT.parent / "estampie" / ".toolchain" / "ac79-sdk")
    GEN.mkdir(parents=True, exist_ok=True)
    py = [sys.executable, "-I"]
    subprocess.run([*py, ROOT / "tools" / "usr_data.py", "--header", GEN / "usr_data.h"], check=True)
    subprocess.run([sys.executable, ROOT / "tools" / "gen_screens.py", "--out", GEN], check=True)
    ota = build_loader()
    img = build_app(product)
    (OUT / f"{NAME}.bin").write_bytes(img)
    pkg = OUT / f"{NAME}.fwsc"
    subprocess.run([*py, BSP / "tools" / "fm1pkg.py", OUT / f"{NAME}.bin", LDR / "ota.bin", pkg,
                    "--product", product, "--sdk", sdk], check=True)
    print(f"app      {OUT / f'{NAME}.bin'}  {len(img)} B ({APP_SLOT - len(img)} B spare)")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {pkg}  {pkg.stat().st_size} B, identity {product}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
