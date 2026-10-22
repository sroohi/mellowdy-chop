#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
  echo "Packaging requires macOS tools: codesign, ditto, plutil, and hdiutil." >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
CONFIGURATION="${SAMPLIFY_CONFIGURATION:-Release}"
INPUT_APP="$BUILD_DIR/Samplify_artefacts/$CONFIGURATION/Samplify.app"
OUTPUT_DIR="${SAMPLIFY_DIST_DIR:-$ROOT_DIR/dist}"
[[ -x "$INPUT_APP/Contents/MacOS/Samplify" ]] || { echo "Build the Release app before packaging: $INPUT_APP" >&2; exit 1; }
if [[ -n "${KEYCHAIN_PROFILE:-}" && -z "${CODESIGN_IDENTITY:-}" ]]; then
  echo "KEYCHAIN_PROFILE requires a Developer ID CODESIGN_IDENTITY; ad-hoc builds cannot be notarized." >&2
  exit 1
fi

STAGING="$(mktemp -d "${TMPDIR:-/tmp}/samplify-package.XXXXXX")"
trap 'rm -rf "$STAGING"' EXIT
APP="$STAGING/Samplify.app"
/usr/bin/ditto "$INPUT_APP" "$APP"
/usr/bin/plutil -lint "$APP/Contents/Info.plist"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")" == audio.mpaudio.samplify ]] || {
  echo "Unexpected bundle identifier." >&2; exit 1;
}

ARCHS="$(/usr/bin/lipo -archs "$APP/Contents/MacOS/Samplify")"
LABEL="${ARCHS// /-}"
if [[ "$ARCHS" == *arm64* && "$ARCHS" == *x86_64* ]]; then LABEL=universal; fi
mkdir -p "$OUTPUT_DIR"
ZIP="$OUTPUT_DIR/Samplify-macOS-$LABEL.zip"
DMG="$OUTPUT_DIR/Samplify-macOS-$LABEL.dmg"

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
  /usr/bin/codesign --force --options runtime --timestamp --sign "$CODESIGN_IDENTITY" "$APP"
else
  /usr/bin/codesign --force --sign - --timestamp=none "$APP"
  echo "Packaging an ad-hoc signed development build. It is not Developer ID signed or notarized."
fi
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP"

if [[ -n "${KEYCHAIN_PROFILE:-}" ]]; then
  /usr/bin/ditto -c -k --sequesterRsrc --keepParent "$APP" "$STAGING/notarization.zip"
  xcrun notarytool submit "$STAGING/notarization.zip" --keychain-profile "$KEYCHAIN_PROFILE" --wait
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"
  /usr/sbin/spctl --assess --type execute --verbose=2 "$APP"
fi

/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
mkdir -p "$STAGING/disk"
/usr/bin/ditto "$APP" "$STAGING/disk/Samplify.app"
ln -s /Applications "$STAGING/disk/Applications"
/usr/bin/hdiutil create -volname Samplify -srcfolder "$STAGING/disk" -ov -format UDZO "$DMG"
/usr/bin/hdiutil verify "$DMG"
if [[ -n "${KEYCHAIN_PROFILE:-}" ]]; then
  /usr/bin/codesign --force --timestamp --sign "$CODESIGN_IDENTITY" "$DMG"
  xcrun notarytool submit "$DMG" --keychain-profile "$KEYCHAIN_PROFILE" --wait
  xcrun stapler staple "$DMG"
  xcrun stapler validate "$DMG"
fi
(
  cd "$OUTPUT_DIR"
  shasum -a 256 "$(basename "$ZIP")" "$(basename "$DMG")" > SHA256SUMS.txt
)
echo "Created $ZIP"
echo "Created $DMG"
