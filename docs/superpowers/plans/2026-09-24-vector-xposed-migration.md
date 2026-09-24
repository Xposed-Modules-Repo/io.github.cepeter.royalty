# Vector/Xposed Migration Implementation Plan

> **Superseded transport note:** The Binder/AIDL catalog steps below document the first implementation. They were replaced by the package-visibility-safe callback design in `2026-09-24-package-visibility-safe-catalog-ipc.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan.

**Goal:** Replace the unsafe APatch/MeowZygisk native module with a tested Android Xposed APK for Vector and LSPosed.

**Architecture:** A legacy-compatible Xposed entrypoint installs three Java hooks in Telegram. A module Activity owns configuration and a UID-validated Binder catalog service. Telegram reads immutable configuration snapshots through XSharedPreferences.

**Tech Stack:** Java 17, Android Gradle Plugin, Android SDK, JUnit 4, legacy Xposed API compile-only, AIDL, GitHub Actions.

---

### Task 1: Bootstrap a deterministic Android APK project

**Files:**
- Create: `settings.gradle.kts`
- Create: `build.gradle.kts`
- Create: `gradle.properties`
- Create: `app/build.gradle.kts`
- Create: `app/src/main/AndroidManifest.xml`
- Create: `app/src/main/assets/xposed_init`
- Create: `app/src/main/res/values/strings.xml`
- Create: `gradle/wrapper/*`
- Remove at Task 6: native/APatch implementation files

- [x] Write package-contract tests for manifest metadata, entrypoint asset, Telegram recommended scope, min SDK, and compile-only Xposed API.
- [x] Run tests and confirm failure before project files exist.
- [x] Pin Gradle, AGP, Android SDK, JDK, and Xposed API artifact/checksum.
- [x] Build a minimal debug APK.
- [x] Inspect the APK manifest and assets.

### Task 2: Implement and test the pure Java core

**Files:**
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/DialogKey.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/HiddenConfig.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/DialogFilter.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/TapSequence.java`
- Create: corresponding tests under `app/src/test/java/...`

- [x] Write failing tests for signed ID/account parsing, immutable snapshots, list copy semantics, notification filtering, and five-tap timing/reset.
- [x] Implement minimal core classes without Android/Xposed dependencies.
- [x] Run all unit tests.

### Task 3: Implement secure catalog Binder service

**Files:**
- Create: `app/src/main/aidl/io/github/cepeter/telegramhider/ICatalogService.aidl`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/catalog/CatalogService.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/catalog/CatalogStore.java`
- Create: service tests

- [x] Write failing tests for Telegram UID authorization and all bounds.
- [x] Implement direct Binder caller verification on every method.
- [x] Persist only account, dialog ID, bounded title, and hook status in app-private storage.
- [x] Run service and unit tests.

### Task 4: Implement resilient Vector/LSPosed hooks

**Files:**
- Create: `app/src/main/java/io/github/cepeter/telegramhider/xposed/TelegramHook.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/xposed/PreferenceSnapshot.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/xposed/CatalogClient.java`
- Create: hook adapter and contract tests

- [x] Write failing tests around hook adapter behavior using plain Java facades.
- [x] Install hooks only for exact package and process names.
- [x] Return filtered copies from `getDialogs`.
- [x] Replace `processNewMessages` argument with a filtered copy.
- [x] Implement DialogsActivity-only reveal gesture and reload notification.
- [x] Report install/runtime status through the service.
- [x] Ensure every reflection failure logs and fails open.

### Task 5: Implement configuration Activity

**Files:**
- Create: `app/src/main/java/io/github/cepeter/telegramhider/MainActivity.java`
- Create: minimal Android resources/layouts
- Create: UI/controller tests

- [x] Show framework and per-hook health.
- [x] Render catalog grouped by account with stable checked states.
- [x] Save canonical hidden keys and notification toggle through framework-safe SharedPreferences.
- [x] Display unsupported surfaces and restart/reload guidance.
- [x] Verify Activity lifecycle and empty/error states.

### Task 6: Remove unsafe implementation and update project documentation

**Files:**
- Remove: `zygisk/`, `module/`, `meta/`, native `build.sh`, and native scripts/tests
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Create/modify: `SECURITY.md`, `NOTICE.md`, canonical `LICENSE`

- [x] Remove all direct ART offset/Quick ABI code and APatch packaging.
- [x] Document Vector/LSPosed installation, scope, supported surfaces, privacy model, and troubleshooting.
- [x] Add migration notes and accurate third-party notices.
- [x] Verify repository search finds no obsolete feature claims or native hook code.

### Task 7: CI, release signing, and artifact verification

**Files:**
- Replace: `.github/workflows/build.yml`
- Create: `.github/dependabot.yml`
- Add: release verification scripts

- [x] Pin every GitHub Action by full SHA.
- [x] Build/test with read-only permissions; release in a separate write-permission job.
- [x] Configure release signing from GitHub secrets without storing keys in the repository.
- [x] Verify APK signature, manifest, assets, reproducibility metadata, and absence of packaged Xposed API classes.
- [ ] Push branch and require green GitHub Actions before tagging.

### Task 8: Vector device acceptance and release

- [x] Connect a Vector-enabled Android device.
- [ ] Execute every real-device scenario from the approved architecture spec.
- [x] Save versions, logs, and test results without chat-sensitive data.
- [ ] Fix any runtime incompatibilities and rerun the full matrix.
- [ ] Tag and publish a stable GitHub release only after all scenarios pass.
- [ ] Delete scratch build/download files and verify the remote release and asset.
