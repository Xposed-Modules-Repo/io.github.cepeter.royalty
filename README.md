# 🌉 Telegram Chat Hider — APatch Module

[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

> **v1.0.0** · Hide selected Telegram chats behind a 5-tap header gesture. Runs as a Zygisk module via [MeowZygisk](https://github.com/MeowDump/MeowZygisk) inside [APatch](https://github.com/bmax121/APatch).

---

## Quick Start

1. **Install [MeowZygisk](https://github.com/MeowDump/MeowZygisk)** via APatch Manager.
2. **Flash this module**: APatch Manager → Modules → **Install from storage** → select the `.zip`.
3. **Reboot**.
4. Open **APatch Manager → Modules → Telegram Chat Hider → WebUI** to select chats to hide.
5. In Telegram, **tap the header title 5 times** to toggle reveal mode.

---

## What It Does

| Surface | Hidden? |
|---|---|
| Chat list (main dialog list) | ✅ |
| Chat search / global search | ✅ |
| Share / contact picker | ✅ |
| New group / contact invite | ✅ |
| Notifications | ✅ |

**5-tap reveal** temporarily shows hidden chats across *all* surfaces until Telegram is restarted.

---

## Architecture

```
Telegram Chat Hider
├── module/                    # APatch module root (flashed as .zip)
│   ├── module.prop            # Module metadata
│   ├── customize.sh           # Installation script
│   ├── service.sh / boot...sh # Runtime scripts
│   ├── sepolicy.rule          # SELinux policy
│   ├── webroot/               # APatch WebUI (served by APatch Manager)
│   │   ├── index.html
│   │   ├── style.css
│   │   └── js/app.js
│   └── zygisk/                # Native Zygisk libraries
│       ├── arm64-v8a/libtelegram_hider.so
│       └── armeabi-v7a/libtelegram_hider.so
├── zygisk/src/                # Zygisk library source (C)
│   └── telegram_hider.c
├── zygisk/Makefile            # Standalone build (NDK not found → use build.sh)
├── build.sh                   # Full build + zip script
└── .github/workflows/         # CI build on release tags
```

### How It Works

```
APatch Manager ──serves──→ WebUI (window.android.exec)
                                 │ read/write JSON config
                                 ↓
MeowZygisk ──injects──→ libtelegram_hider.so (into Telegram process)
                                 │
                         ┌───────┴───────┐
                         ↓               ↓
               Hook dialogs         5-tap gesture
               (getDialogs,          detector
                loadDialogs)
```

1. **Zygisk module** loads into the Telegram process (`org.telegram.messenger`).
2. On process start, it reads `chat_hider.json` from the module config directory.
3. It hooks `MessagesController.getDialogs()` and `MessagesStorage.loadDialogs()` to filter hidden dialogs.
4. For the **5-tap gesture**, it hooks `View.dispatchTouchEvent` to detect rapid taps on the header area.
5. The **WebUI** reads the dialog catalog (exported by the Zygisk module) and lets you select chats.
6. Config is stored as a JSON file that both the WebUI (root) and the Zygisk module (Telegram process) read.

---

## Building from Source

### Prerequisites

- Android NDK r26+ (standalone or via Android Studio)
- `zip` utility

### Build

```bash
# Auto-detects NDK from $ANDROID_NDK_HOME or $ANDROID_HOME
./build.sh

# Or specify NDK path:
ANDROID_NDK_HOME=/path/to/ndk ./build.sh
```

The output is `telegram_chat_hider-<versionCode>.zip` in the repository root.

### Cross-compile manually

```bash
cd zygisk
make NDK=/path/to/android-ndk
```

---

## WebUI Configuration

The WebUI is accessible from **APatch Manager → Modules → Telegram Chat Hider → WebUI**.

| Setting | Description |
|---|---|
| Chat List | Toggle hiding in the main chat list |
| Search Hidden | Hide chats from global search |
| Share Picker Hidden | Hide from share/contact picker |
| Notifications Suppressed | Block notifications from hidden chats |
| Auto-reveal Timeout | Seconds before hidden chats auto-re-hide (default: 60) |

After selecting chats, tap **Save** and restart Telegram.

---

## License

GPL-3.0 — see [LICENSE](LICENSE).
