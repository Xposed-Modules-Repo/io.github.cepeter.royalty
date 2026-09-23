# Telegram Chat Hider — Vector/Xposed Architecture

**Date:** 2026-09-24
**Status:** Approved

## Goal

Replace the unsafe custom ART entry-point patcher with a maintainable Android Xposed module APK that runs on JingMatrix Vector and legacy-compatible LSPosed. The first production release hides selected dialogs from Telegram's main dialog lists, suppresses new-message notifications for those dialogs, and provides an in-app five-tap reveal gesture.

## Supported environment

- Official Telegram package: `org.telegram.messenger`
- Android 8.1 or newer
- JingMatrix Vector at or after upstream commit `efb82883071643ca16128ecd588be7c40c1e45e6`, or a legacy-compatible LSPosed release
- Vector/LSPosed scope limited to Telegram; the framework also loads the module package itself to support the modern XSharedPreferences bridge

## Packaging

The deliverable is a signed Android APK, not an APatch flashable ZIP. It declares:

- `assets/xposed_init` with the Java entrypoint
- Xposed metadata with `xposedminversion > 92` and `xposedsharedprefs`
- recommended scope `org.telegram.messenger`
- an exported catalog Binder service whose methods verify the caller UID

The old MeowZygisk module, C hook engine, root WebUI, Unix socket, and shell scripts are removed. Git history remains the migration record.

## Hook behavior

### Dialog list

After `MessagesController.getDialogs(int)` returns:

1. Read the account index from the controller's inherited `currentAccount` field.
2. Submit a bounded catalog snapshot to the module service asynchronously.
3. If reveal mode is active, leave the result unchanged.
4. Otherwise return a new `ArrayList` excluding configured `account:dialogId` keys.

Never mutate Telegram's internal list.

### Notifications

Before `NotificationsController.processNewMessages(ArrayList, boolean, boolean, CountDownLatch)` runs:

1. Read the account index from `currentAccount`.
2. Copy the input list.
3. Remove messages whose `MessageObject.getDialogId()` key is hidden.
4. Replace only the method argument. Telegram's original method remains responsible for counting down its latch when the copy is empty.

### Reveal gesture

Hook `org.telegram.ui.ActionBar.ActionBar.dispatchTouchEvent(MotionEvent)`:

- accept `ACTION_DOWN` only;
- require `parentFragment` to be `DialogsActivity`;
- count five taps within a bounded interval;
- toggle process-memory reveal state;
- show a Toast and post Telegram's `dialogsNeedReload` event.

Reveal state resets when Telegram restarts.

## Configuration and IPC

### Configuration

The module Activity writes a single `config` SharedPreferences file using `MODE_WORLD_READABLE`. Vector and LSPosed redirect this operation to their SELinux-safe preference bridge for modules declaring `xposedminversion > 92` or `xposedsharedprefs`.

The Telegram process reads the same file through `XSharedPreferences`. A throttled immutable snapshot prevents disk I/O on every hook call.

Hidden keys are strings in the canonical form `<account>:<signed-dialog-id>`. Account scoping prevents collisions across Telegram accounts.

### Catalog service

The injected code binds explicitly to the module's `CatalogService` and submits bounded arrays of account IDs, dialog IDs, and display titles.

Every Binder call checks `Binder.getCallingUid()` against `PackageManager.getPackagesForUid()` and rejects callers that do not own `org.telegram.messenger`. Limits:

- at most 1,024 dialogs per submission;
- title length at most 256 Unicode code units;
- no message text or usernames are transmitted;
- catalog data remains in the module app's private storage.

## User interface

The Activity shows:

- framework/configuration status;
- hook health reported by Telegram;
- discovered dialogs grouped by account;
- selection controls;
- notification suppression toggle;
- explicit text that search, share picker, and new-group surfaces are unsupported.

Saving configuration reports success only when the framework-safe preference write commits.

## Error handling

Hooks fail open: Telegram continues normally if classes, methods, fields, preferences, or IPC are unavailable. Failures are logged through `XposedBridge.log` and reported to the catalog service as one of:

- `installed`
- `missing`
- `runtime_error`

The Activity must not show a healthy status unless both required hook points report `installed` for the running Telegram version.

## Security model

- No direct ART layout assumptions or native quick-ABI callbacks.
- No root shell execution.
- No world-readable chat catalog.
- Binder caller UID validation on every external service method.
- Bounded arrays and strings before persistence.
- Only IDs and titles are cataloged; no message content.
- Release dependencies and GitHub Actions are pinned.

## Verification and release gate

Automated verification:

- pure-Java unit tests for key parsing, list filtering, notification filtering, tap state, bounds, and immutable preference snapshots;
- service tests for UID and input validation;
- hook contract tests against pinned Telegram source signatures;
- release APK build and package inspection;
- no `de.robv.android.xposed` implementation classes packaged in the APK;
- pinned CI dependencies and successful GitHub Actions run.

Real-device verification on Vector is mandatory before a stable release:

1. install APK and enable Telegram scope;
2. verify hook status;
3. discover dialogs and save selections;
4. verify main, archive, and custom folders return filtered copies;
5. verify hidden dialog notifications are suppressed while visible ones remain;
6. toggle reveal and conceal with five taps;
7. restart Telegram and verify concealment returns;
8. verify two Telegram accounts do not collide;
9. disable the module and verify Telegram behavior returns to normal.

Without this device evidence, builds may be published only as prereleases.