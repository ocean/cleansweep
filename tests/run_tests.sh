#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The host tests: the reset on a pretend flash (reset_test.c), with V15's data from the package (tools/usr_data.py).
#   tests/run_tests.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/test"
mkdir -p "$OUT" "$ROOT/build/gen"
python3 -I "$ROOT/tools/usr_data.py" --header "$ROOT/build/gen/usr_data.h" >/dev/null
cc -std=c99 -O1 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
    -I"$ROOT/build/gen" "$ROOT/tests/reset_test.c" -o "$OUT/reset_test"
"$OUT/reset_test"
