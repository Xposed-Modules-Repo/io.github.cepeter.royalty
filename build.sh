#!/usr/bin/env bash
# ============================================================ #
#  build.sh — Build the Zygisk shared library + module zip     #
#  Requires: Android NDK r26+ (or standalone toolchain)       #
# ============================================================ #
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZIGISK_DIR="$SCRIPT_DIR/zygisk"
MODULE_DIR="$SCRIPT_DIR/module"
MOD_ID=$(grep '^id=' "$MODULE_DIR/module.prop" | cut -d= -f2)

# --- Resolve NDK -----------------------------------------------------------
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    ANDROID_NDK_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}/ndk"
    # Pick the latest installed NDK
    if [ -d "$ANDROID_NDK_HOME" ]; then
        ANDROID_NDK_HOME=$(ls -d "$ANDROID_NDK_HOME"/*/ 2>/dev/null | sort -V | tail -1)
    fi
fi

if [ -z "${ANDROID_NDK_HOME:-}" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "ERROR: ANDROID_NDK_HOME not set or invalid."
    echo "Install the Android NDK or set ANDROID_NDK_HOME."
    exit 1
fi

TC_TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt"
if [ ! -d "$TC_TOOLCHAIN" ]; then
    echo "ERROR: Prebuilt toolchain not found in $TC_TOOLCHAIN"
    exit 1
fi

# Pick the host platform
HOST_TAG=$(ls "$TC_TOOLCHAIN" | head -1)
TC_PATH="$TC_TOOLCHAIN/$HOST_TAG"
CLANG="$TC_PATH/bin/clang"

echo "NDK: $ANDROID_NDK_HOME"
echo "Clang: $CLANG"

# --- Build for arm64-v8a ----------------------------------------------------
echo "Building arm64-v8a..."
mkdir -p "$ZIGISK_DIR/lib/arm64-v8a"
"$CLANG" --target=aarch64-linux-android26 \
    -shared -fPIC -O2 -Wall -Wextra \
    -I "$ZIGISK_DIR/src" \
    -o "$ZIGISK_DIR/lib/arm64-v8a/libtelegram_hider.so" \
    "$ZIGISK_DIR/src/telegram_hider.c"

# --- Build for armeabi-v7a --------------------------------------------------
echo "Building armeabi-v7a..."
mkdir -p "$ZIGISK_DIR/lib/armeabi-v7a"
"$CLANG" --target=armv7a-linux-androideabi26 \
    -shared -fPIC -O2 -Wall -Wextra -marm \
    -I "$ZIGISK_DIR/src" \
    -o "$ZIGISK_DIR/lib/armeabi-v7a/libtelegram_hider.so" \
    "$ZIGISK_DIR/src/telegram_hider.c"

# --- Copy .so files into module/zygisk/ ------------------------------------
echo "Assembling module..."
mkdir -p "$MODULE_DIR/zygisk/arm64-v8a" "$MODULE_DIR/zygisk/armeabi-v7a"
cp "$ZIGISK_DIR/lib/arm64-v8a/libtelegram_hider.so" "$MODULE_DIR/zygisk/arm64-v8a/"
cp "$ZIGISK_DIR/lib/armeabi-v7a/libtelegram_hider.so" "$MODULE_DIR/zygisk/armeabi-v7a/"

# --- Create ZIP ------------------------------------------------------------
VERSION=$(grep '^versionCode=' "$MODULE_DIR/module.prop" | cut -d= -f2)
ZIP_NAME="$SCRIPT_DIR/${MOD_ID}-v${VERSION}.zip"
rm -f "$ZIP_NAME"
(cd "$MODULE_DIR/.." && zip -r "$ZIP_NAME" module/)

echo ""
echo "✓ Module zip built: $ZIP_NAME"
echo "  Flash via APatch Manager → Modules → Install from storage"
