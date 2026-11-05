# How CLEANSWEEP is built, and how to check a release

CLEANSWEEP writes to an FM-1's flash, so what it does should be exactly what's in this repository. It's built the same
way as [ESTAMPIE](https://github.com/ocean/estampie) (whose `docs/SECURITY.md` has the longer reasoning).

## What the build runs

| What | Pinned in | How | Where it runs |
| --- | --- | --- | --- |
| JieLi toolchain (clang 4.0.1, pi32v2) | `tools/toolchain.lock` | SHA-256 | natively on Linux x86-64; elsewhere in the build container |
| JieLi AC79 SDK (three files a package carries) | `tools/toolchain.lock` | commit | read only |
| felucca-bsp (the platform, the update loader) | the `bsp` submodule | commit | compiled here, with `patches/` applied to copies |
| V15's factory data | `tools/usr_data.py` | SHA-256 of M-VAVE's package | read at build time |
| Python 3.14 and Pillow (the screen's words) | `uv.lock` | hashes | `uv sync --frozen` |
| Build container `cc-debian13:nonroot` | `tools/toolchain.lock` | image digest | no network, no shell, no capabilities, read-only |
| uv, osv-scanner | `tools/toolchain.lock` | SHA-256 | the tools themselves |
| GitHub Actions | the workflows | commit | `permissions: {}` by default, no stored credentials |

`tools/setup_toolchain.sh` fetches everything into `.toolchain/` and checks each against its pin before unpacking it.
Nothing new is pinned until it has been out about two weeks, and anything new is scanned first.

**Scanning:** `tools/osv_scan.sh` checks `uv.lock` and the build container against osv.dev, on every push and weekly
(`.github/workflows/supply-chain.yml`); zizmor checks the workflows. Known findings in the container with no Debian
fix yet are accepted in `tools/osv-build-image.toml`, each with a reason and an expiry. The scan also checks the
container's newer image, and fails once that image fixes an accepted finding and is two days old, so the pin moves
then rather than at the expiry.

## Releases

A release is built, signed and published by GitHub, not on anyone's computer. A pushed tag `vX.Y.Z` runs
`.github/workflows/release.yml`, which uses the FM-1 Firmware Hub's shared workflow,
[zvenson/fm1-build](https://github.com/zvenson/fm1-build) `@v1`. It runs `tools/ci_release.sh` with the pins above,
runs the tests, and signs a build attestation for each file: the package `cleansweep-X.Y.Z.fwsc`, the app alone, and
the package's `.sha256`. A release's identity is FM-1_257NNN, its number in `RELEASES` (`tools/build.py`).

A run started by hand (Actions › release › Run workflow) is a dry run: built and signed, but not released.

## Checking a release

To check that a file came from this repository's workflow, at that tag:

    gh attestation verify cleansweep-X.Y.Z.fwsc --repo ocean/cleansweep \
        --signer-workflow zvenson/fm1-build/.github/workflows/build.yml

The build is reproducible, so you can also rebuild it yourself and compare:

    git checkout vX.Y.Z && git submodule update --init
    tools/setup_toolchain.sh
    tools/release.sh X.Y.Z          # build/dist/
    tools/verify_release.sh X.Y.Z   # each file signed, and the same as this build

## Updating a pin

Check the new release's signature or provenance where its project publishes one, wait out the cooldown, record the
hash in `tools/toolchain.lock`, then run `tools/setup_toolchain.sh`, `tools/osv_scan.sh` and `tests/run_tests.sh`.
For the build container, `docker buildx imagetools inspect gcr.io/distroless/cc-debian13:nonroot` gives the digest; a
rebuild must give the same package byte for byte, then delete accepted findings the new image fixes.
