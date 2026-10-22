#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-run}"
case "$MODE" in
  run|--debug|debug|--logs|logs|--telemetry|telemetry|--verify|verify) ;;
  *) echo "usage: $0 [run|--debug|--logs|--telemetry|--verify]" >&2; exit 2 ;;
esac

if [[ "$(uname -s)" != Darwin ]]; then
  echo "Samplify.app must be built and launched on macOS. Use GitHub Actions for a downloadable macOS build." >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SAMPLIFY_BUILD_DIR:-$ROOT_DIR/build}"
APP_NAME="Samplify"
BUNDLE_ID="audio.mpaudio.samplify"
CONFIGURATION="${SAMPLIFY_CONFIGURATION:-Debug}"
APP_BUNDLE="$BUILD_DIR/Samplify_artefacts/$CONFIGURATION/$APP_NAME.app"

command -v cmake >/dev/null || { echo "Install CMake first: brew install cmake" >&2; exit 1; }
xcrun --find clang >/dev/null
pkill -x "$APP_NAME" >/dev/null 2>&1 || true
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --target Samplify --parallel "${SAMPLIFY_JOBS:-4}"
[[ -x "$APP_BUNDLE/Contents/MacOS/$APP_NAME" ]] || { echo "App bundle not found: $APP_BUNDLE" >&2; exit 1; }

open_app() { /usr/bin/open -n "$APP_BUNDLE"; }

case "$MODE" in
  run) open_app ;;
  --debug|debug) lldb -- "$APP_BUNDLE/Contents/MacOS/$APP_NAME" ;;
  --logs|logs)
    open_app
    /usr/bin/log stream --info --style compact --predicate "process == \"$APP_NAME\""
    ;;
  --telemetry|telemetry)
    open_app
    /usr/bin/log stream --info --style compact --predicate "subsystem == \"$BUNDLE_ID\""
    ;;
  --verify|verify)
    open_app
    for attempt in {1..20}; do
      if pgrep -x "$APP_NAME" >/dev/null; then
        echo "Samplify launched. Process presence is verified; audio and UI behavior require a functional check."
        exit 0
      fi
      sleep 0.25
    done
    echo "Samplify did not remain running after launch." >&2
    exit 1
    ;;
esac
