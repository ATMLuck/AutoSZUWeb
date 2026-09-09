#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT}/build/linux-test"

cmake -S "${ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAUTOSZUWEB_BUILD_TESTS=ON
cmake --build "${BUILD_DIR}" --parallel
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "Linux platform-stub tests complete. No Linux release binary is produced."
