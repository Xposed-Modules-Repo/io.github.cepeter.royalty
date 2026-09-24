# Changelog

All notable changes are documented here. The project follows [Keep a Changelog](https://keepachangelog.com/) and semantic versioning.

## [Unreleased]

### Added

- Android Xposed module APK compatible with JingMatrix Vector and legacy-compatible LSPosed.
- Account-aware dialog keys and bounded catalog collection.
- Package-visibility-safe catalog refresh using a module request and nonce-validated `PendingIntent` callback.
- Configuration app with hook status, dialog selection, and notification controls.
- Five-tap reveal on Telegram’s `ActionBar`.
- Unit and packaging/security contract tests.
- Gradle dependency verification and complete security/licensing documents.
- SHA-pinned CI, secret-backed release signing, APK inspection, payload reproducibility checks, and Dependabot configuration.
- Sanitized Vector device-acceptance record.

### Changed

- Main dialog filtering now returns a copy instead of mutating Telegram’s internal list.
- Notification suppression hooks `NotificationsController.processNewMessages` while preserving empty-list countdown behavior.
- Configuration moved from a root JSON file to Vector/LSPosed XSharedPreferences safe-zone storage.
- Supported surfaces are now documented accurately: dialog lists and new-message notifications only.
- Notification suppression is opt-in; catalog responses follow the documented 1,024-entry and 256-code-unit bounds.
- The reveal gesture supports Telegram 12.8.3’s APK-verified obfuscated `ActionBar` and `DialogsActivity` aliases, hooks `onInterceptTouchEvent(MotionEvent)`, and verifies the active fragment through `ActionBarLayout`.
- Telegram hook classes are resolved only after `ApplicationLoader.onCreate`, preventing early static initialization from crashing Telegram under Vector.
- Catalog requests now rely on the signature-level sender permission instead of package-visibility-sensitive `PendingIntent` identity metadata.

### Removed

- Custom ART entry-point offsets and Quick-ABI callbacks.
- APatch/MeowZygisk packaging, native C library, root WebUI, and Unix catalog socket.
- Exported Binder/AIDL catalog service that depended on Telegram being able to discover the module package.
- Unsupported search, share-picker, and new-group claims.

### Security

- Version 1.x is unsupported because its native hook engine was not ABI-safe and its module lifecycle prevented reliable activation.

## [v1.3.1] — 2026-09-23

Legacy APatch release. Superseded by the version 2 architecture and no longer supported.

[Unreleased]: https://github.com/cepeter/telegram-apatch-chat-hider/compare/v1.3.1...HEAD
[v1.3.1]: https://github.com/cepeter/telegram-apatch-chat-hider/releases/tag/v1.3.1
