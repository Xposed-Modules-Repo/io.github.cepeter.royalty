#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Host-side syntax check..."

# Use stub jni.h from zygisk/stubs/ + MeowZygisk stubs from zygisk/src/
gcc -fsyntax-only -Wall -Wextra -std=c11 -D__LP64__ -DNDEBUG -D_GNU_SOURCE \
    -I"$SCRIPT_DIR/zygisk/stubs" -I"$SCRIPT_DIR/zygisk/src" \
    "$SCRIPT_DIR/zygisk/src/telegram_hider.c" "$SCRIPT_DIR/zygisk/src/cJSON.c"

echo "✓ No errors or warnings"
