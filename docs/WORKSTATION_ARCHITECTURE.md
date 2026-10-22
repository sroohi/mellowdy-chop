# Hardware console presentation (0.4)

The console presents the existing sampler and workstation through a new cabinet:
a vertical five-mode rail, permanent transport/recording dock, two banks of sixteen
square pads, a compact source deck and focused Trim/Tune/Envelope/Stems/Grid modules.
Song has an eight-track timeline and clip inspector; Pattern has a vertical
cartridge library and note editor; Effects uses horizontal processor cartridges
and a separate hosted insert chain; Perform is an eight-channel mixing console.

This version changes presentation and controller routing. The audio engine,
workstation domain, effect implementations, plugin host and session schema retain
their existing behavior. The sampler keeps all 32 cues across both banks; typing
keys follow the visible bank while MIDI 24–55 retains absolute cue addressing.
See [UI acceptance](UI_REDESIGN_ACCEPTANCE.md) for retained features and checks.

# Samplify workstation

The sampler remains the instrument: 32 cues address the currently loaded source
and its four stems. The workstation records cue events into 16 single-lane
patterns, places pattern or audio clips on eight tracks, and launches those clips
from four scenes. A pattern uses the current cue bank. Freeze a pattern to an
audio asset before replacing its source when its sound must remain fixed.

`WorkstationState` is the serializable domain model. It contains notes, clips,
asset references, mixer settings, effect parameters, and AU/VST3 descriptors and
state. PCM and plugin instances stay outside that model. Session schema 3 adds
this state to the existing sampler session; schemas 1 and 2 open with an empty
workstation. Asset paths are relative to the session when possible. Missing
plugins retain their saved state and pass audio through until restored.

`WorkstationComponent` owns message-thread edits. It publishes validated immutable
snapshots to `AudioEngine`; the callback owns transport, scheduling, voice and clip
playheads. Patterns have no hidden track assignment: arrangement clips, launch
slots, or the selected recording track supply the destination. Explicit Pattern,
Arrangement and Performance play modes avoid accidental playback on an empty
timeline. Fixed queues return recorded note events and meter snapshots to the UI.

Eight preallocated stereo buses feed four built-in inserts and four hosted
inserts per track, track controls, two built-in send returns, and the master
inserts/output. Plugin instances are created and retired outside the audio
callback. Third-party code can impose its own real-time limits. Master recording
copies the rendered output into a bounded queue; a writer thread writes the WAV
and reports overruns instead of silently losing audio. It records the app's
output, not a microphone input.

An outer studio shell owns navigation and session handoff. The original sampler
controller remains responsible for cue editing, analysis and stem processing;
workstation pages share its engine. A beige chassis, inset green displays, rubber
pads, dark knobs, screws and amber indicators are presentation, independent of
sampler behavior.

A single wrapper around multiple sampler engines would duplicate expensive
pitch/time processors and source state. Instead, one engine schedules the cue
voices and routes them into track buses. Frozen/imported audio has independent
asset ownership so it survives source replacement. Offline freezing has an
explicit processing scope; hosted plugin state is never borrowed concurrently
from live processing.

## Validation

The Linux Debug build passes all 11 CTest suites, including 43 workstation audio
checks and 38 schema-3 session checks. Five callback probes found zero intercepted
C++ or linked-C allocation/deallocation calls. The host has 40 lifecycle/state
checks; the native macOS variant additionally scans, instantiates, processes and
restores a real Apple Audio Unit. The GUI smoke exercises sampler editing, actual
neural separation, workstation controllers and every page. The release workflow
requires all suites and the native macOS smoke evidence before publishing.

These checks cover the app's host and built-in DSP. Third-party processors execute
in-process; their own resource use and stability remain vendor-dependent. Hosted
insert latency is not automatically aligned across parallel tracks.
