# Samplify by MP Audio

A standalone native macOS sampler built in C++20 with JUCE 8. Import a track,
find chops, play a bank of 16 or 32 pads, and export the processed slices.

![Samplify workstation with demo audio](docs/Samplify.png)

*Interface captured from the Linux validation build. Native macOS screenshot
evidence is produced by the macOS Actions workflow.*

## Build and run

Requires macOS 12+, Xcode Command Line Tools, CMake 3.28+, and Git. There are no
runtime accounts, servers, Python packages, or model downloads.

```sh
./script/build_and_run.sh
```

The Codex **Run** action calls this same script. See [deployment instructions](docs/DEPLOYMENT.md)
for universal Apple Silicon/Intel ZIP and DMG builds, GitHub Actions downloads,
and optional Developer ID signing/notarization.

## Workflow

1. Start with **Load demo**, import WAV/AIFF/FLAC/MP3, or drop an audio file on the window.
2. Choose **Find samples** for transient cuts, **Set slicer** for beat-grid cuts,
   or **Set random** for repeatable cuts on detected beats. Choose 16 or 32 pad slots.
   Sparse beat grids leave remaining pads empty; without detected beats, random
   slicing uses the source BPM grid (sample cuts for clips shorter than one beat).
3. Click a pad or play its computer/MIDI key. Edit its cue range, level, pitch,
   detune, stretch, filter, attack/release, and reverse settings in the inspector.
4. Set source/project BPM and key; enable tempo/key sync as needed. Tempo estimates
  and key estimates are editable. Quantize schedules notes on an internal beat clock.
5. Export the selected pad or the entire bank as 24-bit WAV. Save a `.samplify`
   JSON session to retain parameters and audio file references.

The first 16 pads use `1234 / QWER / ASDF / ZXCV`; MIDI notes 36–67 address all
32 slots. Keyboard mode transposes the selected pad from a piano-style key layout.
**Space** stops all voices. Use **Audio / MIDI** to select an output device and
**Learn MIDI note** to map the next MIDI note to the selected pad.

## Imported stems

Import aligned **Vocals, Melody, Bass, Drums** files into the stem slots, then mute
individual stems per pad. They must exactly match the source sample rate, channel
count, and frame count. Once any stem is imported, playback uses the imported stem
mix; unimported slots are silent. Clear the stems to return to the original mix.

Neural stem separation is not included: the supplied blueprint has no trained
weights or model contract. The app never substitutes frequency filters for source
separation. Stem import is the explicitly selected workflow for this version.

## Processing and limits

- Colored waveforms use three frequency bands. Analysis uses spectral-flux onset
  detection, tempo autocorrelation, and chroma/key-profile matching. Estimates can
  be ambiguous with syncopation, silence, or changing tempo; review the results.
  Rhythm/key analysis uses the first 120 seconds; the waveform covers the full file.
- Independent pitch/tempo processing uses offline WSOLA and windowed-sinc
  resampling, rendered in the background into a playback cache. Extreme ratios
  can introduce artifacts. This is not the proprietary Pitch 'n Time engine or a
  transient-locked phase-vocoder implementation.
- `timeRatio` multiplies pad duration; tempo sync divides duration by
  `projectBpm / sourceBpm`. Pitch and cents do not change the target duration.
  Key sync transposes pitch roots; it does not reharmonize major/minor qualities.
- Keyboard-mode extra transposition uses playback-rate scaling, so its duration
  also changes. The selected pad's inspector pitch control preserves duration.
- Playback supports up to 16 simultaneous voices, click-reducing note-off fades,
  and stereo output. Standalone stereo output replaces the blueprint's DAW bus
  routing. Plugin formats (AU/VST3/AAX) are outside this standalone target.
- Imports are mono/stereo, 8–384 kHz, up to 32 million frames. Rendered banks are
  limited to 128 MiB. WAV exports clip values outside full scale.
- Sessions reference audio files rather than embedding them. Keep those files
  in place when reopening a session. MIDI-learn mappings are per launch.
- MP3 uses CoreAudio on macOS; the Linux validation build tests WAV/FLAC/AIFF.

## Validate

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
```

Portable DSP tests do not require JUCE or a desktop:

```sh
cmake -S . -B build-core -DSAMPLIFY_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel 3
ctest --test-dir build-core --output-on-failure
```

Tests exercise synthesized beat/key fixtures, actual pitch/duration behavior,
slice boundaries, WAV round trips, sessions, imported-stem mixing, and the
playback/MIDI callback without requiring an audio device. Native macOS device,
window, and distribution checks are described in the deployment guide.

## Structure and license

`src/core` contains portable analysis/slicing/DSP; `src/audio` owns decoding,
immutable playback banks, device/MIDI I/O and sessions; `src/ui` contains the
desktop workstation. JUCE is pinned to an exact commit and archive checksum.
The original [blueprint](docs/BLUEPRINT.md) is retained for reference.

GPL-3.0, including JUCE's GPL distribution requirements. See [LICENSE](LICENSE).
Commercial closed-source redistribution needs an appropriate JUCE license.
