#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# CLEANSWEEP: the release build on GitHub's runner (.github/workflows/release.yml, through zvenson/fm1-build). The
# shared workflow installs a JieLi toolchain and Python of its own; this uses CLEANSWEEP's, pinned and checked in
# tools/toolchain.lock (tools/setup_toolchain.sh), and felucca-bsp at the commit the submodule pins, so the release is
# built exactly as it is here and anyone can rebuild it byte for byte. Then tools/release.sh, which leaves the files to
# sign and release in build/dist/.
#   tools/ci_release.sh REF     REF: the run's ref name (GITHUB_REF_NAME). A tag vX.Y.Z builds X.Y.Z, which must have
#                               its number in RELEASES (tools/build.py); a branch (a dry run) builds the newest
#                               version there, signed but not released (fm1-build releases tags only)
set -eu
cd "$(dirname "$0")/.."
REF=${1:?usage: tools/ci_release.sh REF}
HAS=$(python3 -I -c 'import ast, re, sys
m = re.search(r"^RELEASES = (\{.*\})", open("tools/build.py").read(), re.M)
r = ast.literal_eval(m.group(1))
print(max(r, key=r.get))')
case "$REF" in
    v[0-9]*) VER=${REF#v}
             [ "$VER" = "$HAS" ] || { echo "ci_release: tag $REF, but the newest in RELEASES is $HAS: tag the commit that adds it"; exit 1; } ;;
    *)       VER=$HAS; echo "ci_release: $REF is not a version tag: a dry run of $VER (signed, not released)" ;;
esac
git submodule update --init bsp
[ "$(git -C bsp rev-parse HEAD)" = "$(git ls-tree HEAD bsp | awk '{ print $3 }')" ] || { echo "ci_release: bsp is not at its pinned commit"; exit 1; }
# (Python: the build's packages only; not the device group, whose MIDI library needs ALSA's headers)
UV_SYNC_ARGS="--no-default-groups" tools/setup_toolchain.sh --only uv,jieli,ac79-sdk,python
export JIELI_TOOLCHAIN="$PWD/.toolchain/jieli/toolchain" AC79_SDK="$PWD/.toolchain/ac79-sdk"   # (not the workflow's)
# the text rasteriser's libraries (the screen's words are drawn with them: tools/gen_screens.py), so a build here and
# one elsewhere can be compared
.venv/bin/python3 -c 'from PIL import features as f, __version__ as v
print("rasteriser: Pillow", v, *(k + " " + str(f.version(k)) for k in ("freetype2", "raqm", "fribidi", "harfbuzz")))'
tools/release.sh "$VER"
sha256sum build/gen/*.h | sed 's/^/generated: /'
