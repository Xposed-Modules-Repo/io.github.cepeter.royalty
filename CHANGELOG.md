# Changelog

All notable changes are documented here. The project follows [Keep a Changelog](https://keepachangelog.com/) and semantic versioning.

## [Unreleased]

### Added

- Android Xposed module APK compatible with JingMatrix Vector and legacy-compatible LSPosed.
- Account-aware dialog keys and bounded catalog collection.
- UID-authenticated Binder service for Telegram-to-module catalog/status updates.
- Configuration app with hook status, dialog selection, and notification controls.
- Five-tap reveal on Telegram’s `ActionBar`.
- Unit and packaging/security contract tests.
- Gradle dependency verification and complete security/licensing documents.

### Changed

- Main dialog filtering now returns a copy instead of mutating Telegram’s internal list.
- Notification suppression hooks `NotificationsController.processNewMessages` while preserving empty-list countdown behavior.
- Configuration moved from a root JSON file to Vector/LSPosed XSharedPreferences safe-zone storage.
- Supported surfaces are now documented accurately: dialog lists and new-message notifications only.

### Removed

- Custom ART entry-point offsets and Quick-ABI callbacks.
- APatch/MeowZygisk packaging, native C library, root WebUI, and Unix catalog socket.
- Unsupported search, share-picker, and new-group claims.

### Security

- Version 1.x is unsupported because its native hook engine was not ABI-safe and its module lifecycle prevented reliable activation.

## [v1.3.1] — 2026-09-23

Legacy APatch release. Superseded by the version 2 architecture and no longer supported.

[Unreleased]: https://github.com/cepeter/telegram-apatch-chat-hider/compare/v1.3.1...HEAD
[v1.3.1]: https://github.com/cepeter/telegram-apatch-chat-hider/releases/tag/v1.3.1
