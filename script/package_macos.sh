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
MODEL_NAME="ggml-model-htdemucs-4s-f16.bin"
MODEL="$APP/Contents/Resources/$MODEL_NAME"
[[ -f "$MODEL" ]] || { echo "The app bundle is missing its native HTDemucs model: $MODEL" >&2; exit 1; }
MODEL_CHECKSUM="$(shasum -a 256 "$MODEL")"
[[ "${MODEL_CHECKSUM%% *}" == 72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064 ]] || {
  echo "Bundled native model checksum mismatch; refusing to package this app." >&2; exit 1;
}
for NOTICE in Demucs-MIT.txt DemucsCpp-MIT.txt Eigen-MPL-2.0.txt; do
  [[ -s "$APP/Contents/Resources/Licenses/$NOTICE" ]] || { echo "Missing bundled license notice: $NOTICE" >&2; exit 1; }
done
for NOTICE in THIRD_PARTY_NOTICES.md LICENSE RubberBand-COPYING.txt; do
  [[ -s "$APP/Contents/Resources/$NOTICE" ]] || { echo "Missing bundled license resource: $NOTICE" >&2; exit 1; }
done

ARCHS="$(/usr/bin/lipo -archs "$APP/Contents/MacOS/Samplify")"
LABEL="${ARCHS// /-}"
if [[ "$ARCHS" == *arm64* && "$ARCHS" == *x86_64* ]]; then LABEL=universal; fi
mkdir -p "$OUTPUT_DIR"
ZIP="$OUTPUT_DIR/Samplify-macOS-$LABEL.zip"
DMG="$OUTPUT_DIR/Samplify-macOS-$LABEL.dmg"
/usr/bin/ditto "$MODEL" "$OUTPUT_DIR/$MODEL_NAME"
/usr/bin/ditto "$APP/Contents/Resources/Licenses/Demucs-MIT.txt" "$OUTPUT_DIR/HTDemucs-MIT.txt"
cat > "$OUTPUT_DIR/HTDemucs-model-provenance.json" <<'JSON'
{
  "model": "HTDemucs four-source float16, native demucs.cpp format",
  "model_license": "MIT; retain the accompanying HTDemucs-MIT.txt",
  "checkpoint_url": "https://dl.fbaipublicfiles.com/demucs/hybrid_transformer/955717e8-8726e21a.th",
  "checkpoint_sha256": "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4",
  "native_sha256": "72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064",
  "demucs_cpp_commit": "f1206e9adeea103aef4a636b9e62297cf1f8e34e",
  "runtime": "native C++; no Python runtime or Serato proprietary code"
}
JSON

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
  shasum -a 256 "$(basename "$ZIP")" "$(basename "$DMG")" "$MODEL_NAME" HTDemucs-model-provenance.json HTDemucs-MIT.txt > SHA256SUMS.txt
)
echo "Created $ZIP"
echo "Created $DMG"
