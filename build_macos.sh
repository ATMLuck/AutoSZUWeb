#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT}/build/macos"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script builds the macOS .app and must run on macOS."
  exit 1
fi

PREFIXES=()
for formula in curl openssl@3 nlohmann-json; do
  if brew --prefix "${formula}" >/dev/null 2>&1; then
    PREFIXES+=("$(brew --prefix "${formula}")")
  else
    echo "Missing dependency: ${formula}. Install with: brew install ${formula}"
    exit 1
  fi
done

PREFIX_PATH="$(IFS=';'; echo "${PREFIXES[*]}")"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DAUTOSZUWEB_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH="${PREFIX_PATH}"
cmake --build "${BUILD_DIR}" --parallel
ctest --test-dir "${BUILD_DIR}" --output-on-failure
plutil -lint "${BUILD_DIR}/AutoSZUWeb.app/Contents/Info.plist"

echo "Built: ${BUILD_DIR}/AutoSZUWeb.app"
