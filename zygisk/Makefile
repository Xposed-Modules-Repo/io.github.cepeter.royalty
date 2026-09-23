# ============================================================ #
#  Telegram Chat Hider — Zygisk Library Build                 #
#  Cross-compile for Android ARM64 + ARMv7 using the        #
#  Android NDK standalone toolchain.                           #
# ============================================================ #

# ── Paths ─────────────────────────────────────────────────── #
NDK            ?= $(HOME)/Android/Sdk/ndk/26.1.10909125
TOOLCHAIN      := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64
API_LEVEL      := 21

# ── Target triplets ───────────────────────────────────────── #
TRIPLE_ARM64   := aarch64-linux-android$(API_LEVEL)
TRIPLE_ARM     := armv7a-linux-androideabi$(API_LEVEL)

# ── Compilers ─────────────────────────────────────────────── #
CC_ARM64       := $(TOOLCHAIN)/bin/$(TRIPLE_ARM64)-clang
CC_ARM         := $(TOOLCHAIN)/bin/$(TRIPLE_ARM)-clang
STRIP          := $(TOOLCHAIN)/bin/llvm-strip

# ── Flags ─────────────────────────────────────────────────── #
CFLAGS  := -std=c11 -Wall -Wextra -O2 -fPIC -shared
CFLAGS  += -I$(NDK)/sources/android/native_app_glue
CFLAGS  += -DREZYGISK_API_VERSION=5

LDFLAGS := -shared -Wl,--strip-all

# ── MeowZygisk loader include ─────────────────────────────── #
# Download from: https://github.com/MeowDump/MeowZygisk/releases
# Path to the MeowZygisk loader headers
MEOWZYSISK_HEADERS ?= /opt/meowzygisk-loader/include

# ── Source / output ───────────────────────────────────────── #
SRC      := src/telegram_hider.c
OUT_ARM64 := ../zygisk/arm64-v8a/libtelegram_hider.so
OUT_ARM   := ../zygisk/armeabi-v7a/libtelegram_hider.so

# ── Build rules ───────────────────────────────────────────── #
.PHONY: all clean

all: $(OUT_ARM64) $(OUT_ARM)

$(OUT_ARM64): $(SRC)
	mkdir -p $(dir $@)
	$(CC_ARM64) $(CFLAGS) $(LDFLAGS) $< -o $@

$(OUT_ARM): $(SRC)
	mkdir -p $(dir $@)
	$(CC_ARM) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(OUT_ARM64) $(OUT_ARM)

# ── Package into Magisk/Module ZIP ────────────────────────── #
.PHONY: package
package:
	@echo "Packaging module..."
	cd .. && zip -r telegram_chat_hider.zip \
	  module/module.prop \
	  module/sepolicy.rule \
	  module/service.sh \
	  module/post-fs-data.sh \
	  module/boot-completed.sh \
	  module/customize.sh \
	  module/uninstall.sh \
	  module/zygisk \
	  module/webroot \
	  module/service.d \
	  meta/
