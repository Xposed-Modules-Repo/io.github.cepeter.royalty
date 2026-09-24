# Vector device acceptance — 2.0.0 release candidate

Date: 2026-09-24

## Environment

- Device: Motorola Moto G45 5G
- Android: 15 (API 35)
- Telegram: 12.8.3 (`versionCode 69222`)
- Vector: 2.2
- Module: 2.0.0 release candidate

No dialog IDs, titles, message text, usernames, device serials, or raw Telegram logs are retained here.

## Results

| Scenario | Result |
|---|---|
| Vector recognizes and activates the module | Pass on device; rerun after final APK install |
| XSharedPreferences safe-zone redirection | Pass |
| Package-visibility-safe catalog callback | Pending final APK test |
| Telegram 12.8.3 hook signatures and APK-verified reveal aliases | Pending final APK test |
| Main dialog list hides selected dialogs without mutating Telegram's list | Pending final APK regression test |
| Hidden-chat notification suppression preserves countdown handling | Pending final APK regression test |
| Three-second ActionBar hold reveals/conceals and redraws | Pending final APK test |
| Telegram restarts without Java/native crash; concealment resets | Pending final APK regression test |
| Archive and custom folders | Pending manual confirmation |
| Two-account isolation | Pending manual confirmation |
| Disabling the module restores normal behavior | Pending manual confirmation |

A stable tag remains blocked until every pending row passes on the final signed APK.
