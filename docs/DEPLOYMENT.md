# Build and distribute Samplify

Samplify is a standalone JUCE/C++ macOS app. It does not need a DAW, web server,
Python installation, model download, or online account to run. macOS 12 or later
is the deployment target.

## Download the development preview

A push to the deployment branch `codex/samplify-native`, or a manual workflow run,
publishes a GitHub **prerelease** after the macOS build, tests, native demo smoke
test, and packaging all succeed. The release tag is
`samplify-v0.1.0-<first-seven-commit-characters>`.
For example, the release for commit `abc1234…` appears at:

```text
https://github.com/sroohi/mellowdy-chop/releases/tag/samplify-v0.1.0-abc1234
```

Use the ZIP or DMG attached to that release. The release is a development preview;
it is not a notarized public release. Repository settings must permit Actions to
create releases with the workflow's scoped `contents: write` token. If release
publication is denied, the successful build's Actions artifact remains available.
The workflow never publishes releases from pull requests or ordinary branch pushes.

## Download from GitHub Actions

Every push and pull request runs **Build Samplify for macOS**. The workflow also
supports **Run workflow** from GitHub's Actions tab. After a successful run, open
that run and download **Samplify-macOS-universal** from its Artifacts section.
GitHub may require sign-in to download Actions artifacts. Artifact retention is
30 days; the matching prerelease assets remain available until the release is
removed. Pushes to other branches and pull requests produce Actions artifacts only.

The artifact contains:

- `Samplify-macOS-universal.zip`: the native `.app` bundle.
- `Samplify-macOS-universal.dmg`: a disk image with the app and an Applications shortcut.
- `SHA256SUMS.txt`: SHA-256 checksums for both files.

The binary contains Apple Silicon (`arm64`) and Intel (`x86_64`) architectures.
Open the disk image and drag Samplify into Applications, or extract the app ZIP
and move the app there. No installer service is required.

The default CI artifact is **ad-hoc signed, not Developer ID signed or notarized**.
macOS Gatekeeper can block downloaded development builds. For a build you trust,
use the system's **Privacy & Security → Open Anyway** approval flow if offered.
Do not disable Gatekeeper globally. Public distribution with normal first-launch
trust requires the signing and notarization steps below.

## Build and run on a Mac

Install Xcode Command Line Tools (`xcode-select --install`), Git, and CMake
(`brew install cmake` if Homebrew is your package manager). From the repository:

```sh
./script/build_and_run.sh
```

The script builds the native app and opens its bundle. It also backs the Codex
**Run** action. Optional modes are `--debug`, `--logs`, `--telemetry`, and `--verify`.
`--verify` checks process presence only; it does not validate playback or UI state.
Use `SAMPLIFY_JOBS` to change build parallelism (default 4).

To create a universal Release package locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 3
ctest --test-dir build --build-config Release --output-on-failure
./script/package_macos.sh build
```

Dependencies are fetched from the pinned sources in `CMakeLists.txt`; Internet
access is needed for the initial configure. Generated outputs stay in `build/`
and `dist/`. `SAMPLIFY_BUILD_DIR`, `SAMPLIFY_CONFIGURATION`, and
`SAMPLIFY_DIST_DIR` can override script defaults. Keep the same CMake generator
when reusing a build directory.

## Developer ID signing and notarization

Public distribution requires your own Apple Developer Program membership,
Developer ID Application signing identity with its private key in the Mac's
keychain, and notarization credentials. No credentials are shipped with this
repository. The default Actions workflow does not import signing identities or
submit to Apple.

On a trusted signing Mac, store notarization credentials in a keychain profile
using `xcrun notarytool store-credentials` and Apple's documented secure prompts.
Then run:

```sh
CODESIGN_IDENTITY='Developer ID Application: Your Organization (TEAMID)' \
KEYCHAIN_PROFILE='samplify-notary' \
./script/package_macos.sh build
```

The script signs with hardened runtime, verifies the app signature, submits the
app ZIP to Apple's notary service, staples the app ticket, rebuilds the ZIP and
disk image, and notarizes/staples the disk image. It fails on signature or
notarization errors. A `KEYCHAIN_PROFILE` without `CODESIGN_IDENTITY` is rejected.
Leaving both unset produces the explicitly marked ad-hoc development package.

## Functional acceptance on macOS

The workflow launches the actual `.app` bundle on its macOS runner using
`open --args --smoke-test`. The app opens its normal window, analyses the demo,
renders its pads, saves `Samplify.png` and `smoke.json`, and exits. The workflow
checks the report's macOS/demo/rendering flags and matching screenshot dimensions;
it fails if no valid result arrives within 75 seconds. The separate
**Samplify-native-macOS-smoke** Actions artifact contains the native screenshot and
report. Missing runner GUI capability is a failing check, not a skipped success.

Automated DSP checks, demo startup, and packaging do not prove hardware audio or
MIDI behavior. Before distributing a build, launch the app on a Mac, import an audio file,
generate slices, play pads from the pointer and keyboard, connect a MIDI input,
check mono/poly and tempo/pitch controls, save/reopen a project, and export a WAV.
Test imported stems with matching duration and alignment. Test both Intel and
Apple Silicon hardware when shipping a universal app.

The Linux cloud workspace can validate portable DSP code and Linux app behavior.
It cannot compile Apple SDKs, run a native macOS app, or validate
Gatekeeper/notarization outcomes. Native demo smoke evidence comes from a
successful macOS Actions run, not the Linux cloud machine.
