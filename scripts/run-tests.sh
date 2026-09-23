#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/.test-build"
trap 'rm -rf "$BUILD_DIR"' EXIT
mkdir -p "$BUILD_DIR"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/zygisk/src" \
    "$ROOT/tests/test_runtime_utils.c" \
    "$ROOT/zygisk/src/runtime_utils.c" \
    -lm -o "$BUILD_DIR/test_runtime_utils"

"$BUILD_DIR/test_runtime_utils"
python3 -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
