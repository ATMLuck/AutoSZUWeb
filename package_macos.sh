#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/macos}"
DIST_DIR="${DIST_DIR:-${ROOT}/dist/macos}"
APP_SOURCE="${APP_SOURCE:-${BUILD_DIR}/AutoSZUWeb.app}"
APP_NAME="AutoSZUWeb"
VERSION="${VERSION:-1.3.0}"
ARCH_NAME="${ARCH_NAME:-$(uname -m)}"
PACKAGE_NAME="${APP_NAME}-${VERSION}-macOS-${ARCH_NAME}"
WORK_DIR="${DIST_DIR}/work"
APP_PATH="${WORK_DIR}/${APP_NAME}.app"
DMG_ROOT="${WORK_DIR}/dmg"
DMG_PATH="${DIST_DIR}/${PACKAGE_NAME}.dmg"
EXECUTABLE="${APP_PATH}/Contents/MacOS/${APP_NAME}"
FRAMEWORKS="${APP_PATH}/Contents/Frameworks"
ENTITLEMENTS="${ENTITLEMENTS:-${ROOT}/packaging/entitlements.plist}"
SIGN_IDENTITY="${MACOS_SIGN_IDENTITY:--}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This packaging script must run on macOS."
  exit 1
fi

for command in dylibbundler hdiutil codesign otool ditto; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "Missing command: ${command}"
    echo "Install packaging dependencies with: brew install dylibbundler"
    exit 1
  fi
done

if [[ ! -d "${APP_SOURCE}" ]]; then
  echo "Application bundle not found: ${APP_SOURCE}"
  echo "Build it first with ./build_macos.sh"
  exit 1
fi

rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}" "${DIST_DIR}"
ditto "${APP_SOURCE}" "${APP_PATH}"
mkdir -p "${FRAMEWORKS}"

# Copy all non-system dylibs recursively and rewrite references to the app bundle.
dylibbundler -od -b \
  -x "${EXECUTABLE}" \
  -d "${FRAMEWORKS}" \
  -p "@executable_path/../Frameworks"

# Refuse to publish a bundle that still points at Homebrew/MacPorts/local paths.
external_dependencies="$({
  otool -L "${EXECUTABLE}"
  find "${FRAMEWORKS}" -type f -print0 |
    while IFS= read -r -d '' library; do
      if file "${library}" | grep -q 'Mach-O'; then
        otool -L "${library}"
      fi
    done
} | awk '/^[[:space:]]+\// {print $1}' |
  grep -Ev '^(/System/Library/|/usr/lib/)' || true)"

if [[ -n "${external_dependencies}" ]]; then
  echo "Unbundled external dynamic libraries remain:"
  echo "${external_dependencies}"
  exit 1
fi

# Verify that every @loader_path/@rpath dependency resolves inside the copied bundle.
missing_relative_dependencies="$(python3 - "${EXECUTABLE}" "${FRAMEWORKS}" <<'PY'
import os
import subprocess
import sys

executable, frameworks = sys.argv[1:]
files = [executable]
for root, _, names in os.walk(frameworks):
    for name in names:
        path = os.path.join(root, name)
        if 'Mach-O' in subprocess.check_output(['file', path], text=True):
            files.append(path)

missing = []
for binary in files:
    lines = subprocess.check_output(['otool', '-L', binary], text=True).splitlines()[1:]
    for line in lines:
        dep = line.strip().split(' (', 1)[0]
        if dep.startswith('@loader_path/'):
            resolved = os.path.normpath(os.path.join(os.path.dirname(binary), dep[len('@loader_path/'):]))
        elif dep.startswith('@executable_path/'):
            resolved = os.path.normpath(os.path.join(os.path.dirname(executable), dep[len('@executable_path/'):]))
        elif dep.startswith('@rpath/'):
            resolved = os.path.join(frameworks, dep[len('@rpath/'):])
        else:
            continue
        if not os.path.exists(resolved):
            missing.append(f'{binary}: {dep} -> {resolved}')

print('\n'.join(missing))
PY
)"

if [[ -n "${missing_relative_dependencies}" ]]; then
  echo "Bundled dynamic libraries have unresolved relative dependencies:"
  echo "${missing_relative_dependencies}"
  exit 1
fi

# Sign nested code first, then the outer app. '-' means ad-hoc signing for test builds.
while IFS= read -r -d '' library; do
  if ! file "${library}" | grep -q 'Mach-O'; then
    continue
  fi
  if [[ "${SIGN_IDENTITY}" == "-" ]]; then
    codesign --force --sign - "${library}"
  else
    codesign --force --timestamp --options runtime \
      --sign "${SIGN_IDENTITY}" "${library}"
  fi
done < <(find "${FRAMEWORKS}" -type f -print0)

if [[ "${SIGN_IDENTITY}" == "-" ]]; then
  codesign --force --sign - --entitlements "${ENTITLEMENTS}" "${APP_PATH}"
else
  codesign --force --timestamp --options runtime \
    --entitlements "${ENTITLEMENTS}" \
    --sign "${SIGN_IDENTITY}" "${APP_PATH}"
fi

codesign --verify --deep --strict --verbose=2 "${APP_PATH}"

mkdir -p "${DMG_ROOT}"
ditto "${APP_PATH}" "${DMG_ROOT}/${APP_NAME}.app"
ln -s /Applications "${DMG_ROOT}/Applications"
rm -f "${DMG_PATH}"
hdiutil create \
  -volname "${APP_NAME}" \
  -srcfolder "${DMG_ROOT}" \
  -ov -format UDZO \
  "${DMG_PATH}"

if [[ "${SIGN_IDENTITY}" != "-" ]]; then
  codesign --force --timestamp --sign "${SIGN_IDENTITY}" "${DMG_PATH}"
  codesign --verify --verbose=2 "${DMG_PATH}"
fi

# Optional notarization. APPLE_ID must use an app-specific password.
if [[ -n "${APPLE_ID:-}" || -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" || -n "${APPLE_TEAM_ID:-}" ]]; then
  : "${APPLE_ID:?APPLE_ID is required for notarization}"
  : "${APPLE_APP_SPECIFIC_PASSWORD:?APPLE_APP_SPECIFIC_PASSWORD is required for notarization}"
  : "${APPLE_TEAM_ID:?APPLE_TEAM_ID is required for notarization}"
  if [[ "${SIGN_IDENTITY}" == "-" ]]; then
    echo "Notarization requires a Developer ID Application signature."
    exit 1
  fi

  xcrun notarytool submit "${DMG_PATH}" \
    --apple-id "${APPLE_ID}" \
    --password "${APPLE_APP_SPECIFIC_PASSWORD}" \
    --team-id "${APPLE_TEAM_ID}" \
    --wait
  xcrun stapler staple "${DMG_PATH}"
  xcrun stapler validate "${DMG_PATH}"
  spctl --assess --type open --context context:primary-signature --verbose=4 "${DMG_PATH}"
fi

rm -rf "${WORK_DIR}"
echo "Package created: ${DMG_PATH}"
if [[ "${SIGN_IDENTITY}" == "-" ]]; then
  echo "This is an ad-hoc signed test package. On another Mac, use Finder > right-click > Open."
else
  echo "Developer ID signed package created."
fi