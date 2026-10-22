# Build and distribute Samplify 0.3

Samplify is a standalone JUCE/C++ app for macOS 12 and later, built for Apple
Silicon and Intel. Its sampling workflow follows the public Serato Sample 2.x
manual; the [acceptance contract](SERATO_PARITY.md) tracks the individual reference
behaviors and remaining comparisons. Native HTDemucs supplies four-stem
separation, and Rubber Band supplies independent pitch/time processing. These are
independent engines, not Serato's proprietary processing.

Version 0.3 adds beige panels and green displays, a pattern editor, an eight-track
arranger, built-in effects presets, and performance/output-recording controls.
Track and master inserts host installed stereo Audio Unit and VST3 effects.
No third-party plugins are bundled; plugin vendors' licenses and activation
requirements still apply.

The approximately 84 MB native model is bundled inside the app. Running the app
does not need Python, a DAW, a separate model download, or an online account.
Python and PyTorch are used only when preparing the model during a source build.

## Download a development preview

A push to `codex/samplify-native`, or a manual GitHub Actions workflow run,
publishes a prerelease after the universal macOS build, all required tests,
native demo smoke test, and packaging succeed. Its tag is
`samplify-v0.3.0-<first-seven-commit-characters>`. For commit `abc1234…`, the URL is:

```text
https://github.com/sroohi/mellowdy-chop/releases/tag/samplify-v0.3.0-abc1234
```

Download the DMG and drag Samplify to Applications, or extract the app ZIP and
move the app there. The app bundle contains the model and its license notices.
`SHA256SUMS.txt` covers the ZIP, DMG, standalone model, and model notices. Keep the
downloaded files in one directory and run `shasum -a 256 -c SHA256SUMS.txt` to
verify them; missing optional downloads will be reported as missing files.

The release also includes `Samplify.png`, `Samplify-empty.png`,
`Samplify-compact.png`, `Samplify-arrange.png`, `Samplify-patterns.png`,
`Samplify-fx.png`, `Samplify-perform.png`, and `smoke.json` from the native macOS
sampler, neural-separation, and workstation controller checks. The separate
`ggml-model-htdemucs-4s-f16.bin` asset is for source
builders; ordinary app users already have that file inside the bundle. Preserve
the model's `HTDemucs-model-provenance.json` and full `HTDemucs-MIT.txt` notice when
copying or redistributing the standalone model.

Default CI packages are **ad-hoc signed, not Developer ID signed or notarized**.
macOS may require explicit approval through **Privacy & Security → Open Anyway**
for a downloaded development build you trust. Public distribution with normal
first-launch trust requires the signing and notarization steps below.

Every push and pull request also produces **Samplify-macOS-universal** and
**Samplify-native-macOS-smoke** artifacts in the workflow's Actions page. GitHub
may require sign-in to download Actions artifacts; their retention is 30 days.
The corresponding prerelease assets persist until removed. Pull requests and
ordinary branch pushes never publish releases. Only the release job has
`contents: write`; repository settings must allow its scoped Actions token to
create releases. If release publication is denied, successfully uploaded build
artifacts remain available.

## Build and run on a Mac

Install Xcode Command Line Tools (`xcode-select --install`), Git, and CMake 3.28
or later (`brew install cmake` if using Homebrew). Then run:

```sh
./script/build_and_run.sh
```

The script verifies or prepares the native model, builds the app, and opens its
bundle. It backs the Codex **Run** action. Modes `--debug`, `--logs`, `--telemetry`,
and `--verify` are available. `--verify` checks process presence only, not audio
or interaction behavior. `SAMPLIFY_JOBS` changes build parallelism (default 2).

On Apple Silicon, first-time model preparation requires Python 3.12
(`brew install python@3.12`). If the model is missing, the script creates an
isolated `.cache/model-python` environment, installs build-time `torch==2.6.0`
and `numpy==2.2.6`, downloads the official hash-pinned Meta checkpoint, and
converts it. Existing verified model bytes are reused without reinstalling
PyTorch or converting again. `SAMPLIFY_MODEL_PYTHON` can select the Python 3.12
interpreter used to create this isolated environment.

PyTorch 2.6 macOS conversion wheels support Apple Silicon. For an Intel source
build, download the converted model from a Samplify 0.3 release, or use the model
inside a released app:

```sh
export SAMPLIFY_STEM_MODEL_FILE='/Applications/Samplify.app/Contents/Resources/ggml-model-htdemucs-4s-f16.bin'
./script/build_and_run.sh
```

The same override works on Apple Silicon and avoids any Python dependency for
that build. The script and CMake both verify the native model SHA-256:

```text
72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064
```

The source helper can also verify an existing model using only Python's standard
library: `python3 script/prepare_stem_model.py --verify-only --output <model>`.
Model and checkpoint hashes are checked without disabling TLS or changing
expected checksums.

## Universal Release build

With `SAMPLIFY_STEM_MODEL_FILE` pointing to an already prepared model:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DSAMPLIFY_STEM_MODEL_FILE="$SAMPLIFY_STEM_MODEL_FILE" -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build --build-config Release --timeout 900 --output-on-failure
./script/package_macos.sh build
```

The Actions workflow performs this sequence on an Apple Silicon macOS runner.
It caches converted model bytes by their pinned hash and converter source hash,
installs Python conversion dependencies only on cache misses, and rechecks the
model hash on every run. It verifies that all eleven required suites are
registered, then runs all of them: core DSP, cue editing, metadata, Rubber Band,
neural stem inference, audio/session integration, workstation model, built-in
effects, workstation audio, workstation session, and plugin hosting. On macOS,
the plugin-host suite also loads, processes, and restores a real Apple stereo
Audio Unit. Its deterministic fixture tests exercise insert routing, bypass,
state, missing-plugin recovery, and safe instance retirement. The
actual-inference stem test has a 900-second timeout;
it is not replaced by an availability or zero-test check.

JUCE, Rubber Band, Eigen, and native Demucs source archives are pinned in CMake.
Initial configuration and model preparation need network access to their
upstreams. Compiling the universal native inference backend takes more memory
than the earlier sampler; two parallel build jobs balance the hosted runner's
resources. Generated files stay in `build/`, `dist/`, and ignored `.cache/`.
`SAMPLIFY_BUILD_DIR`, `SAMPLIFY_CONFIGURATION`, and `SAMPLIFY_DIST_DIR` override
script defaults. Reuse a build directory with the same CMake generator.

## Bundle checks and licenses

Before signing, the package script verifies bundle metadata, the bundled model
hash, and required license resources. It checks the resulting app signature and
disk image, then creates ZIP/DMG/model checksums. The bundle includes:

- `Contents/Resources/ggml-model-htdemucs-4s-f16.bin`.
- `Contents/Resources/THIRD_PARTY_NOTICES.md`, `LICENSE`, `RubberBand-COPYING.txt`.
- `Contents/Resources/Licenses/Demucs-MIT.txt`, `DemucsCpp-MIT.txt`, `Eigen-MPL-2.0.txt`, and `VST3-SDK-LICENSE.txt`.

The MIT model/runtime notices and Eigen MPL 2.0 notice accompany their respective
components. The app's GPL license and the JUCE/Rubber Band distribution notices
remain in the bundle; packaging does not remove or replace them.

## Developer ID signing and notarization

Public distribution requires an Apple Developer Program membership, a Developer
ID Application identity with its private key in the signing Mac's keychain,
and notarization credentials. None are shipped with this repository, and the
default Actions workflow does not import them or submit to Apple.

On a trusted signing Mac, store notarization credentials in a keychain profile
using `xcrun notarytool store-credentials` and Apple's secure prompts. Then run:

```sh
CODESIGN_IDENTITY='Developer ID Application: Your Organization (TEAMID)' \
KEYCHAIN_PROFILE='samplify-notary' \
./script/package_macos.sh build
```

The script signs with hardened runtime, verifies the app, submits it to Apple's
notary service, staples its ticket, recreates the ZIP and DMG, and notarizes and
staples the DMG. Signature or notarization failure stops packaging. A
`KEYCHAIN_PROFILE` without `CODESIGN_IDENTITY` is rejected. Leaving both unset
produces the marked ad-hoc development package.

For Developer ID signing, `script/Samplify.entitlements` adds only
`com.apple.security.cs.disable-library-validation=true`. This permits the
in-process host to load effects signed by other developers rather than only
Apple or Samplify's own signing team. The package script reads the actual signed
entitlements back and verifies this flag; it rejects JIT, unsigned executable
memory, or App Sandbox exceptions. The default ad-hoc CI path remains unchanged
and does not enable hardened runtime. No third-party plugin is re-signed or
bundled by this process, and the entitlement does not provide plugin crash
isolation or bypass a plugin's own installation/security requirements.

## Native evidence and functional acceptance

Normal startup presents an empty load screen. The CI-only `--smoke-test` path
captures that state, explicitly loads the demo, exercises the actual controller
workflows, completes native HTDemucs separation through the UI, exercises the
workstation, captures seven PNGs plus a JSON report, and exits. CI opens the real `.app`
through `open --args` and allows 270 seconds for startup, inference, and rendering
(the app's internal workflow timeout is 240 seconds). It
requires the macOS/demo/rendering, blank-screenshot, stem-completion, and
workstation-screenshot flags to
be true, at least ten named `workflowChecks` booleans to be true, and every named
`stemWorkflowChecks` boolean to be true. `workstationChecks` must contain at least
ten named booleans, all true. Missing or undersized check groups fail. CI also
verifies all seven PNGs and matching full-image dimensions. A missing runner
GUI or absent valid report fails the workflow. The separate native smoke artifact
and release assets contain the evidence.

These tests establish specific build and behavior checks, not exact Serato visual
or sound parity. Use the [acceptance contract](SERATO_PARITY.md) for comparison
against a specified running Serato version. On physical Macs, check audio output,
MIDI input, 32-voice operation, cue handles and multi-selection, Hold/Trigger,
keyboard mode, tempo/pitch control, undo/redo, session restore, stem separation
quality, and WAV export. Test Intel and Apple Silicon hardware before shipping
a universal public release. Gatekeeper and notarization outcomes also require
their own signing and trust checks.

For the workstation, check pattern editing and playback, eight-track arrangement,
track/master effects, presets, performance controls, and recording the actual
output. Test installed stereo AU and VST3 effects for scanning, loading, editor
interaction, bypass, state restore, and live audio. A universal app runs one
architecture at a time: an Apple Silicon process cannot load an Intel-only
in-process plugin, and an Intel process cannot load an ARM-only plugin. Hosted
plugins execute in the app process; the automated Apple Audio Unit test does not
prove every third-party plugin is compatible or crash-isolated.

The Linux cloud machine validates portable DSP and native inference code. Native
macOS UI evidence comes from a successful macOS workflow run, not Linux testing.
