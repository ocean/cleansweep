#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The host tests: the reset on a pretend flash (reset_test.c), with V15's data from the package (tools/usr_data.py),
# and the look and the lights on a pretend screen (look_test.c), with the words from tools/gen_screens.py.
#   tests/run_tests.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/test"
mkdir -p "$OUT" "$ROOT/build/gen"
python3 -I "$ROOT/tools/usr_data.py" --header "$ROOT/build/gen/usr_data.h" >/dev/null
cc -std=c99 -O1 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
    -I"$ROOT/build/gen" "$ROOT/tests/reset_test.c" -o "$OUT/reset_test"
"$OUT/reset_test"
UV="$ROOT/.toolchain/uv/uv"                     # (the pinned uv where setup_toolchain.sh fetched one: a CI runner)
[ -x "$UV" ] || UV=uv
"$UV" run --frozen --no-default-groups --project "$ROOT" "$ROOT/tools/gen_screens.py" --out "$ROOT/build/gen" >/dev/null
cc -std=c99 -O1 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
    -I"$ROOT/build/gen" "$ROOT/tests/look_test.c" -o "$OUT/look_test"
"$OUT/look_test"
