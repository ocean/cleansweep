#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# CLEANSWEEP: check everything the build runs against the OSV vulnerability database (osv.dev), with osv-scanner
# (after ESTAMPIE's tools/osv_scan.sh, same author).
#   python       uv.lock (the screen generator, the installer tools, the workflow linter)
#   build-image  the pinned container the JieLi tools run in (needs Docker); accepted findings, each with a reason and
#                an expiry, are in tools/osv-build-image.toml. A newer image of the tag is scanned too: once it fixes an
#                accepted finding and is two days old, the scan fails, so the pin moves before the expiry
# The JieLi toolchain is closed source (nothing to scan); felucca-bsp has no dependencies. Reports (JSON) in
# build/osv/. Exits 1 on any finding.
#   tools/osv_scan.sh [part ...]       (default: every part)
set -u
cd "$(dirname "$0")/.."
OSV=.toolchain/bin/osv-scanner
[ -x "$OSV" ] || OSV=$(command -v osv-scanner) || { echo "osv_scan: no osv-scanner (tools/setup_toolchain.sh)"; exit 1; }
mkdir -p build/osv
PARTS=${*:-python build-image}
fail=0

# Scan with the given arguments; the report goes to build/osv/NAME.json, a summary to the terminal. A scan that sees
# no packages at all fails too: it would otherwise pass without checking anything.
scan() {
    name=$1; shift
    "$OSV" scan "$@" --format json --all-packages > "build/osv/$name.json" 2> "build/osv/$name.log"
    python3 -I - "build/osv/$name.json" "$name" <<'PY' || fail=1
import json, sys
path, name = sys.argv[1], sys.argv[2]
try:
    results = json.load(open(path)).get("results") or []
except ValueError:
    sys.exit(f"osv_scan: {name}: no report (build/osv/{name}.log)")
packages = [p for r in results for p in r["packages"]]
found = [(v["id"], p["package"]["name"], p["package"].get("version", "?")) for p in packages for v in p.get("vulnerabilities", [])]
if not packages:
    sys.exit(f"osv_scan: {name}: FAILED, no packages seen (build/osv/{name}.log)")
if found:
    print(f"osv_scan: {name}: {len(found)} FINDINGS in {len(packages)} packages (build/osv/{name}.json):")
    for f in sorted(set(found)):
        print("    %s  %s %s" % f)
    sys.exit(1)
print(f"osv_scan: {name}: {len(packages)} packages, no known vulnerabilities")
PY
}
skip() { echo "osv_scan: $1: skipped ($2)"; }

for part in $PARTS; do
    case $part in
    python)
        scan python source --lockfile uv.lock ;;
    build-image)
        if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
            IMG=$(awk '$1 == "build-image" { print $5 "@sha256:" $4 }' tools/toolchain.lock)
            docker pull -q --platform linux/amd64 "$IMG" >/dev/null &&
                docker save --platform linux/amd64 -o build/osv/build-image.tar "$IMG" &&
                scan build-image image --config tools/osv-build-image.toml --archive build/osv/build-image.tar &&
                "$OSV" scan image --archive build/osv/build-image.tar --format json --all-packages \
                    > build/osv/build-image-all.json 2> /dev/null
            rm -f build/osv/build-image.tar
            latest=$(docker buildx imagetools inspect "${IMG%@*}" 2>/dev/null | awk '/^Digest:/ { print $2; exit }')
            if [ -n "$latest" ] && [ "${IMG#*@}" != "$latest" ]; then
                # A newer image: scan it too, without the accepted list, and fail once it fixes an accepted finding
                # and is two days old (its upload time from the registry; distroless dates its images 1970), so a
                # fix is taken soon, not at the accepted list's expiry. One that fixes none is only reported.
                NEW="${IMG%@*}@$latest"
                docker pull -q --platform linux/amd64 "$NEW" >/dev/null &&
                    docker save --platform linux/amd64 -o build/osv/build-image-newer.tar "$NEW" &&
                    "$OSV" scan image --archive build/osv/build-image-newer.tar --format json --all-packages \
                        > build/osv/build-image-newer.json 2> /dev/null
                rm -f build/osv/build-image-newer.tar
                python3 -I - "$NEW" tools/osv-build-image.toml build/osv/build-image-all.json \
                    build/osv/build-image-newer.json <<'PY' || fail=1
import json, re, sys, time, urllib.request
new, accepted_file, cur_file, newer_file = sys.argv[1:]
def ids(path):
    try:
        results = json.load(open(path)).get("results") or []
    except (OSError, ValueError):
        return None
    return {v["id"] for r in results for p in r["packages"] for v in p.get("vulnerabilities", [])}
accepted = set(re.findall(r'^id = "([^"]+)"', open(accepted_file).read(), re.M))
cur, newer = ids(cur_file), ids(newer_file)
name, digest = new.split("@")
print(f"osv_scan: build-image: {name} has a newer digest, {digest} (update tools/toolchain.lock)")
if cur is None or newer is None:
    sys.exit("osv_scan: build-image: FAILED, the newer image could not be scanned (build/osv/build-image-newer.json)")
fixed = sorted(accepted & cur - newer)
if not fixed:
    print("osv_scan: build-image: the newer image fixes none of the accepted findings")
    sys.exit(0)
age = None
m = re.match(r"gcr\.io/(.+):", name)                 # (the registry's upload time: gcr.io's tag list has it)
if m:
    try:
        with urllib.request.urlopen(f"https://gcr.io/v2/{m.group(1)}/tags/list", timeout=30) as r:
            age = (time.time() - int(json.load(r)["manifest"][digest]["timeUploadedMs"]) / 1000) / 86400
    except (OSError, ValueError, KeyError):
        pass
print(f"osv_scan: build-image: the newer image fixes {len(fixed)} accepted: {' '.join(fixed)}")
if age is not None and age < 2:
    print(f"osv_scan: build-image: it is {age:.1f} days old; it fails this scan from 2 days")
    sys.exit(0)
sys.exit("osv_scan: build-image: FAILED, take the newer image (" + (f"{age:.0f} days old" if age is not None else
         "age unknown") + "), check the build is byte-identical, and remove the fixed ids from " + accepted_file)
PY
            fi
            python3 -I - tools/osv-build-image.toml build/osv/build-image-all.json <<'PY'
import json, re, sys
accepted = re.findall(r'^id = "([^"]+)"', open(sys.argv[1]).read(), re.M)
try:                                                  # accepted ids the pinned image no longer has: report only
    results = json.load(open(sys.argv[2])).get("results") or []
except (OSError, ValueError):
    sys.exit(0)
have = {v["id"] for r in results for p in r["packages"] for v in p.get("vulnerabilities", [])}
gone = [i for i in accepted if i not in have]
if have and gone:
    print(f"osv_scan: build-image: {len(gone)} accepted ids no longer found, can go from {sys.argv[1]}: {' '.join(gone)}")
PY
        else skip build-image "no Docker"; fi ;;
    *) echo "osv_scan: no part $part"; exit 2 ;;
    esac
done
[ $fail = 0 ] && echo "osv_scan: clean" || echo "osv_scan: FAILED"
exit $fail
