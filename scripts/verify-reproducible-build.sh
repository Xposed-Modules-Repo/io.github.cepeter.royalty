#!/usr/bin/env bash
set -euo pipefail
output=${1:-app/build/outputs/apk/release/app-release.apk}
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1789948800}
./gradlew --no-daemon clean assembleRelease
cp "$output" "$workdir/first.apk"
./gradlew --no-daemon clean assembleRelease
python3 scripts/compare_apk_payloads.py \
  "$workdir/first.apk" \
  "$output" \
  "$output.reproducibility.sha256"
sha256sum "$output"

