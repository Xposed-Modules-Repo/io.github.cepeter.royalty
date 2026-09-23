# Vector/Xposed Migration Implementation Plan

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

- [ ] Write package-contract tests for manifest metadata, entrypoint asset, Telegram recommended scope, min SDK, and compile-only Xposed API.
- [ ] Run tests and confirm failure before project files exist.
- [ ] Pin Gradle, AGP, Android SDK, JDK, and Xposed API artifact/checksum.
- [ ] Build a minimal debug APK.
- [ ] Inspect the APK manifest and assets.

### Task 2: Implement and test the pure Java core

**Files:**
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/DialogKey.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/HiddenConfig.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/DialogFilter.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/core/TapSequence.java`
- Create: corresponding tests under `app/src/test/java/...`

- [ ] Write failing tests for signed ID/account parsing, immutable snapshots, list copy semantics, notification filtering, and five-tap timing/reset.
- [ ] Implement minimal core classes without Android/Xposed dependencies.
- [ ] Run all unit tests.

### Task 3: Implement secure catalog Binder service

**Files:**
- Create: `app/src/main/aidl/io/github/cepeter/telegramhider/ICatalogService.aidl`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/catalog/CatalogService.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/catalog/CatalogStore.java`
- Create: service tests

- [ ] Write failing tests for Telegram UID authorization and all bounds.
- [ ] Implement direct Binder caller verification on every method.
- [ ] Persist only account, dialog ID, bounded title, and hook status in app-private storage.
- [ ] Run service and unit tests.

### Task 4: Implement resilient Vector/LSPosed hooks

**Files:**
- Create: `app/src/main/java/io/github/cepeter/telegramhider/xposed/TelegramHook.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/xposed/PreferenceSnapshot.java`
- Create: `app/src/main/java/io/github/cepeter/telegramhider/xposed/CatalogClient.java`
- Create: hook adapter and contract tests

- [ ] Write failing tests around hook adapter behavior using plain Java facades.
- [ ] Install hooks only for exact package and process names.
- [ ] Return filtered copies from `getDialogs`.
- [ ] Replace `processNewMessages` argument with a filtered copy.
- [ ] Implement DialogsActivity-only reveal gesture and reload notification.
- [ ] Report install/runtime status through the service.
- [ ] Ensure every reflection failure logs and fails open.

### Task 5: Implement configuration Activity

**Files:**
- Create: `app/src/main/java/io/github/cepeter/telegramhider/MainActivity.java`
- Create: minimal Android resources/layouts
- Create: UI/controller tests

- [ ] Show framework and per-hook health.
- [ ] Render catalog grouped by account with stable checked states.
- [ ] Save canonical hidden keys and notification toggle through framework-safe SharedPreferences.
- [ ] Display unsupported surfaces and restart/reload guidance.
- [ ] Verify Activity lifecycle and empty/error states.

### Task 6: Remove unsafe implementation and update project documentation

**Files:**
- Remove: `zygisk/`, `module/`, `meta/`, native `build.sh`, and native scripts/tests
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Create/modify: `SECURITY.md`, `NOTICE.md`, canonical `LICENSE`

- [ ] Remove all direct ART offset/Quick ABI code and APatch packaging.
- [ ] Document Vector/LSPosed installation, scope, supported surfaces, privacy model, and troubleshooting.
- [ ] Add migration notes and accurate third-party notices.
- [ ] Verify repository search finds no obsolete feature claims or native hook code.

### Task 7: CI, release signing, and artifact verification

**Files:**
- Replace: `.github/workflows/build.yml`
- Create: `.github/dependabot.yml`
- Add: release verification scripts

- [ ] Pin every GitHub Action by full SHA.
- [ ] Build/test with read-only permissions; release in a separate write-permission job.
- [ ] Configure release signing from GitHub secrets without storing keys in the repository.
- [ ] Verify APK signature, manifest, assets, reproducibility metadata, and absence of packaged Xposed API classes.
- [ ] Push branch and require green GitHub Actions before tagging.

### Task 8: Vector device acceptance and release

- [ ] Connect a Vector-enabled Android device.
- [ ] Execute every real-device scenario from the approved architecture spec.
- [ ] Save versions, logs, and test results without chat-sensitive data.
- [ ] Fix any runtime incompatibilities and rerun the full matrix.
- [ ] Tag and publish a stable GitHub release only after all scenarios pass.
- [ ] Delete scratch build/download files and verify the remote release and asset.