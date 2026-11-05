#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# CLEANSWEEP: everything a release ships. GitHub builds the release itself: a pushed tag vX.Y.Z runs
# .github/workflows/release.yml, which builds with this script on GitHub's runner (tools/ci_release.sh) through the
# FM-1 Firmware Hub's shared workflow (zvenson/fm1-build), signs a build attestation for every file in build/dist/
# and releases them. The build is reproducible: the same tag built here gives the same files, byte for byte
# (tools/verify_release.sh checks a release against this machine's build).
#
#   tools/release.sh X.Y.Z            build it: the package and the app, in build/dist/
#   tools/release.sh X.Y.Z --publish  build it here first (it must work), then tag vX.Y.Z and push the tag: GitHub
#                                     builds, signs and releases it. Publishing is Drew's call.
#
# X.Y.Z must have its number in RELEASES (tools/build.py): build.py stops otherwise and names the next one.
# Out, in build/dist/: cleansweep-X.Y.Z.fwsc (the package any installer takes), cleansweep-X.Y.Z-app.bin (the app
# alone) and cleansweep-X.Y.Z.fwsc.sha256 ("HASH  NAME").
set -e
cd "$(dirname "$0")/.."
VER="$1"
[ -n "$VER" ] || { echo "usage: tools/release.sh X.Y.Z [--publish]"; exit 2; }
PUBLISH=0
[ "$2" = "--publish" ] && PUBLISH=1
[ -x .venv/bin/python3 ] && PY="$PWD/.venv/bin/python3" || PY=python3

if [ "$PUBLISH" = 1 ]; then
    [ -z "$(git status --porcelain)" ] || { echo "release: the tree has changes: commit them first"; exit 1; }
    ! git rev-parse -q --verify "refs/tags/v$VER" >/dev/null || { echo "release: v$VER is already tagged"; exit 1; }
fi

[ -f bsp/chip/usb.c ] || git submodule update --init bsp   # (felucca-bsp at the commit this one pins)
"$PY" tools/build.py --release "$VER"

rm -rf build/dist && mkdir -p build/dist           # what a release ships (fm1-build signs and releases build/dist/*)
cp build/cleansweep.fwsc "build/dist/cleansweep-$VER.fwsc"
cp build/cleansweep.bin "build/dist/cleansweep-$VER-app.bin"
(cd build/dist && shasum -a 256 "cleansweep-$VER.fwsc" > "cleansweep-$VER.fwsc.sha256" && shasum -a 256 * | sed 's/^/release: /')

if [ "$PUBLISH" = 1 ]; then
    git tag -a "v$VER" -m "CLEANSWEEP $VER"
    git push origin "v$VER"
    echo "release: v$VER pushed. GitHub builds, signs and releases it (Actions > release); then"
    echo "         tools/verify_release.sh $VER checks it against this build. Next version: its number in RELEASES."
else
    echo "release: not published (tools/release.sh $VER --publish tags v$VER: GitHub builds, signs and releases it)"
fi
