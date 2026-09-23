# 🌉 Telegram Chat Hider — APatch Module

[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

> **v1.3.1** · Hide selected Telegram chats behind a 5-tap header gesture. Runs as a Zygisk module via [MeowZygisk](https://github.com/MeowDump/MeowZygisk) inside [APatch](https://github.com/bmax121/APatch).

[Changelog](CHANGELOG.md)

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
| Notifications (heads-up + tray) | ✅ |

**5-tap reveal** temporarily shows hidden chats across *all* surfaces until Telegram is restarted.

---

## Architecture

```
Telegram Chat Hider (v1.3.0)
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
│   ├── telegram_hider.c       # Main module: hooks, socket, config
│   ├── art_hook.c             # ART entry-point replacement engine
│   ├── art_hook.h             # ArtMethod hooking API
│   ├── cJSON.c                # JSON parser (cJSON)
│   └── module.h               # MeowZygisk API stubs
├── zygisk/Makefile            # Standalone build
├── zygisk/stubs/              # Host-side stubs (jni.h, logging.h)
├── build.sh                   # Full build + zip script
├── scripts/check-syntax.sh    # Host-side C syntax verification
└── .github/workflows/         # CI: builds on release tags
```

### How It Works

```
APatch Manager ──serves──→ WebUI (reads socket catalog)
                                 │
MeowZygisk ──injects──→ libtelegram_hider.so (into Telegram process)
                                 │
                    ┌────────────┼─────────────┐
                    ↓            ↓             ↓
      ART entry-point   NotificationCenter    View.dispatchTouch
      replacement on    replacement on        replacement on
      getDialogs(I)     postNotificationName  (5-tap gesture)

Config:  JSON file ← WebUI / Root shell
Dialog catalog: Unix domain socket (SO_PEERCRED, root-only)
```

#### Hooking Method: ART Entry-Point Replacement (v1.3.0)

Unlike v1.0–v1.2 which used `hook_jni_native_methods()` (only intercepts JNI-native methods, **fails silently** on pure Java DEX methods like `getDialogs`), v1.3.0 resolves the `ArtMethod*` for each target method and replaces its `entry_point_from_compiled_code_` field. This is the same technique used by Pine and LSPosed, and is the only approach that reliably hooks Java methods compiled to ART bytecode.

**Version handling**: Android SDK level is detected at runtime via `ro.build.version.sdk`. The entry-point offset is selected from a table matching Pine's `art_method.h`:

| Android | API | arm64 offset | arm32 offset |
|---|---|---|---|
| 15 (V) / 14 (U) / 13 (T) | 34/33/33 | 24 | 20 |
| 12 (S) / 11 (R) | 31/30 | 24 (S) / 32 (R) | 20 (S) / 24 (R) |
| 10 (Q) / 9 (P) | 29/28 | 32 | 24 |
| 8.x (O/OMr1) | 26/27 | 40 | 32 |
| 7.x (N) | 24/25 | 48 | 28 |

On Android R+, the `ArtMethod*` is obtained by resolving the `Executable.artMethod` field via reflection (the `jmethodID` may differ from the actual method pointer). Methods are marked `K_ACC_COMPILE_DONT_BOTHER` to prevent JIT recompilation from resetting the hook.

1. **Zygisk module** loads into the Telegram process (`org.telegram.messenger`).
2. On `post_app_specialize`, it reads `chat_hider.json` config and starts the Unix socket server.
3. It hooks:
   - `MessagesController.getDialogs(int folderId)` — filters hidden dialogs from the returned `ArrayList`
   - `NotificationCenter.postNotificationName(int id, Object... args)` — suppresses notifications for hidden chats (resolves `didReceiveNewMessages` / `pushMessagesUpdated` IDs via reflection)
   - `View.dispatchTouchEvent(MotionEvent)` — detects 5 taps in the header region (y < 220px) to toggle reveal mode
4. The **WebUI** reads the dialog catalog via the Unix domain socket and lets you select chats to hide.

---

## Building from Source

### Prerequisites

- Android NDK r26+ (standalone or via Android Studio)
- `zip` utility

### Quick syntax check (no NDK required)

```bash
./scripts/check-syntax.sh
```

### Full build

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

After selecting chats, tap **Save** and restart Telegram.

### IPC: Unix Domain Socket

Instead of polling `dialogs.json` from disk, v1.3.0 serves the dialog catalog via a Unix domain socket at `/data/adb/modules/telegram_chat_hider/chat_hider.sock`. The socket uses `SO_PEERCRED` to enforce **root-only access** (`uid == 0`), preventing non-root processes from reading the dialog catalog.

---

## License

GPL-3.0 — see [LICENSE](LICENSE).