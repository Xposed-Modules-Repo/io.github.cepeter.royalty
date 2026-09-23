# Changelog

All notable changes to this project are documented here.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [v1.3.0] — 2026-09-23

### Summary

Replaces the broken JNI-native hook architecture with ART entry-point replacement,
making chat hiding actually work on real Telegram. Adds NotificationCenter
notification suppression and a 5-tap gesture hook. See the
[security audit summary](https://github.com/cepeter/telegram-apatch-chat-hider)
for the original findings this release addresses.

### Added

- **`art_hook.c` / `art_hook.h`** — new module that resolves `ArtMethod*`
  from `jmethodID` (via JNI reflection on Android R+, direct cast pre-R) and
  replaces the method's `entry_point_from_compiled_code_`. Includes
  version-specific offset table (API 24–34) sourced from Pine's `art_method.h`,
  and `K_ACC_COMPILE_DONT_BOTHER` flagging to prevent JIT overwrite.
- **NotificationCenter hook** — `postNotificationName(int, Object[])` is
  intercepted. Notification constants (`didReceiveNewMessages`,
  `pushMessagesUpdated`) are resolved at runtime via reflection. Messages
  referencing hidden-chat dialogs are suppressed before reaching the
  Android NotificationManager.
- **5-tap gesture** — `View.dispatchTouchEvent(MotionEvent)` is hooked directly
  as a Java method (was `plt_hook_register` which never worked on `android.view.View`).
  Detects ACTION_DOWN in the header region (y < 220px) and toggles reveal
  mode after 5 rapid taps.

### Changed

- **Hook engine**: replaced `api->hook_jni_native_methods()` with
  `art_hook_method()`. This is the critical fix — `getDialogs()` and
  `postNotificationName()` are pure Java DEX methods (no `native` modifier),
  so `RegisterNatives` silently failed and hidden chats were never filtered.
  Entry-point replacement works on **any** Java method.
- **cJSON** is now included as a vendored dependency (`zygisk/src/cJSON.c`).
  Replaces the hand-rolled parser that had buffer-overflow risks.
- **IPC**: replaced `dialogs.json` file polling with a Unix domain socket
  (`/data/adb/modules/telegram_chat_hider/chat_hider.sock`) using `SO_PEERCRED`
  for root-only authentication. Eliminates TOCTOU race between WebUI reader
  and Zygisk writer.
- **Makefile**: fixed `$<` → `$(SRC)` in build rules so all source files are
  compiled (previously only the first file was compiled; subsequent files were
  silently ignored).
- **Host stubs**: added `JNIInvokeInterface`, `JavaVM`, `GetJavaVM`, and
  `stdbool.h` includes to `jni.h` and `module.h` stubs for host-side syntax
  verification.
- **Logging**: wrapped `LOGE`/`LOGW`/`LOGI`/`LOGD` macros in `do { } while(0)`
  to fix dangling-else bugs when used after `if` without braces.

### Deprecated

- `MessagesStorage.loadDialogs()` hook — no longer needed; `getDialogs()`
  alone covers the main chat list use case on modern Telegram.

## [v1.2.0] — 2025-03-XX

### Added

- cJSON dependency (vendored) for JSON config parsing.
- Unix domain socket IPC with `SO_PEERCRED` authentication for WebUI catalog.
- `pthread_mutex_t` mutex around all shared config state.

### Fixed

- GitHub release cleanup: only `tagName` field is valid; delete all prior
  releases on each new tag push.

## [v1.1.0] — 2025-03-XX

### Fixed

- `gh release list --json` field specification — `url` is not a valid
  return field; use `tagName` only.

## [v1.0.0] — 2025-03-XX

### Added

- Initial release: chat list hiding via `hook_jni_native_methods`.
- WebUI for selecting chats to hide.
- 5-tap header gesture (broken — `plt_hook_register` does not work on `View`).

### Known Issues (fixed in v1.3.0)

- `hook_jni_native_methods` silently fails on pure Java methods → chats visible.
- `plt_hook_register` on `android.view.View.dispatchTouchEvent` never installed.
- Hand-rolled JSON parser has no bounds checking.
- `dialogs.json` IPC has TOCTOU race between root shell writer and Zygote reader.

---

[Unreleased]: https://github.com/cepeter/telegram-apatch-chat-hider/compare/v1.3.0...HEAD
[v1.3.0]: https://github.com/cepeter/telegram-apatch-chat-hider/releases/tag/v1.3.0
[v1.2.0]: https://github.com/cepeter/telegram-apatch-chat-hider/releases/tag/v1.2.0
[v1.1.0]: https://github.com/cepeter/telegram-apatch-chat-hider/releases/tag/v1.1.0
[v1.0.0]: https://github.com/cepeter/telegram-apatch-chat-hider/releases/tag/v1.0.0