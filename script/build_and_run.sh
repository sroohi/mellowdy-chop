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
MODEL_FILE="${SAMPLIFY_STEM_MODEL_FILE:-$ROOT_DIR/.cache/models/ggml-model-htdemucs-4s-f16.bin}"
case "$MODEL_FILE" in /*) ;; *) MODEL_FILE="$PWD/$MODEL_FILE" ;; esac
if [[ ! -f "$MODEL_FILE" ]]; then
  if [[ "$(uname -m)" != arm64 ]]; then
    echo "The native HTDemucs model is missing. PyTorch 2.6 macOS conversion wheels require Apple Silicon." >&2
    echo "Download ggml-model-htdemucs-4s-f16.bin from a Samplify 0.2 release, or use Contents/Resources/ggml-model-htdemucs-4s-f16.bin from the released app." >&2
    echo "Set SAMPLIFY_STEM_MODEL_FILE to that file and run this script again. The model checksum will be verified." >&2
    exit 1
  fi
  MODEL_PYTHON="${SAMPLIFY_MODEL_PYTHON:-python3.12}"
  command -v "$MODEL_PYTHON" >/dev/null || {
    echo "First-time model preparation requires Python 3.12: brew install python@3.12" >&2
    echo "Alternatively set SAMPLIFY_STEM_MODEL_FILE to the verified model from a release. Python is not an app runtime dependency." >&2
    exit 1
  }
  "$MODEL_PYTHON" -c 'import sys; sys.exit(0 if sys.version_info[:2] == (3, 12) else "Model conversion requires Python 3.12.")'
  MODEL_VENV="$ROOT_DIR/.cache/model-python"
  if [[ ! -x "$MODEL_VENV/bin/python" ]]; then
    "$MODEL_PYTHON" -m venv "$MODEL_VENV"
  fi
  echo "Preparing the bundled model with isolated build-time Python tools. The shipped app runs native C++ without Python."
  "$MODEL_VENV/bin/python" -m pip install --disable-pip-version-check --only-binary=:all: torch==2.6.0 numpy==2.2.6
  "$MODEL_VENV/bin/python" "$ROOT_DIR/script/prepare_stem_model.py" --output "$MODEL_FILE"
fi
MODEL_CHECKSUM="$(shasum -a 256 "$MODEL_FILE")"
if [[ "${MODEL_CHECKSUM%% *}" != 72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064 ]]; then
  echo "Native model checksum mismatch: $MODEL_FILE. Replace it with the pinned release model or regenerate it with script/prepare_stem_model.py." >&2
  exit 1
fi
pkill -x "$APP_NAME" >/dev/null 2>&1 || true
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DSAMPLIFY_STEM_MODEL_FILE="$MODEL_FILE"
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --target Samplify --parallel "${SAMPLIFY_JOBS:-2}"
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
