# ============================================================ #
#  Telegram Chat Hider — Zygisk Library Build                 #
#  Cross-compile for Android ARM64 + ARMv7 using the        #
#  Android NDK.                                                #
# ============================================================ #

# ── Paths ─────────────────────────────────────────────────── #
NDK            ?= $(HOME)/Android/Sdk/ndk/26.1.10909125
TOOLCHAIN      := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64
API_LEVEL      := 26

# ── Target triplets ───────────────────────────────────────── #
TRIPLE_ARM64   := aarch64-linux-android$(API_LEVEL)
TRIPLE_ARM     := armv7a-linux-androideabi$(API_LEVEL)

# ── Compilers ─────────────────────────────────────────────── #
CC_ARM64       := $(TOOLCHAIN)/bin/$(TRIPLE_ARM64)-clang
CC_ARM         := $(TOOLCHAIN)/bin/$(TRIPLE_ARM)-clang

# ── Flags ─────────────────────────────────────────────────── #
CFLAGS  := -std=c11 -Wall -Wextra -O2 -fPIC
# Include local stubs for logging.h / module.h / misc.h (MeowZygisk loader API)
CFLAGS  += -I$(CURDIR)/src
CFLAGS  += -I$(NDK)/sources/android/native_app_glue
CFLAGS  += -DREZYGISK_API_VERSION=5
CFLAGS  += -D_GNU_SOURCE

LDFLAGS := -shared -Wl,--strip-all

# ── Source / output ───────────────────────────────────────── #
SRC      := src/telegram_hider.c
OUT_ARM64 := ../zygisk/lib/arm64-v8a/libtelegram_hider.so
OUT_ARM   := ../zygisk/lib/armeabi-v7a/libtelegram_hider.so

# ── Build rules ───────────────────────────────────────────── #
.PHONY: all clean package

all: $(OUT_ARM64) $(OUT_ARM)

$(OUT_ARM64): $(SRC)
	mkdir -p $(dir $@)
	$(CC_ARM64) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(OUT_ARM): $(SRC)
	mkdir -p $(dir $@)
	$(CC_ARM) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(OUT_ARM64) $(OUT_ARM)

# ── Package into APatch module ZIP ────────────────────────── #
package: all
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
	  meta/icon.png \
	  meta/update-json.json \
	  README.md \
	  LICENSE
	@echo "✓ Package built: telegram_chat_hider.zip"
