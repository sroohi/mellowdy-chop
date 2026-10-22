# Serato Sample 2.x reference and acceptance contract

Version 0.4 retains the sampler behavior described here and replaces the former
Serato-shaped presentation with a hardware console. The manual is a feature
reference; the current visual layout follows the
[hardware UI acceptance checklist](UI_REDESIGN_ACCEPTANCE.md).

This document specifies the rebuild against Serato's current public Sample
manual and annotated interface. It is an acceptance contract, not a declaration
that Samplify has achieved feature, visual, or audio equivalence. A passing
existing sampler test does not establish reference parity.

Reference reviewed on 22 October 2026. Requirements below paraphrase these
official sources; numeric values are only asserted where the manual provides
them.

| ID | Official reference |
| --- | --- |
| O | [Overview and annotated main screen](https://support.serato.com/hc/en-us/articles/115000487673-Overview) |
| P | [Source Player](https://support.serato.com/hc/en-us/articles/115000489334-Source-Player) |
| B | [Beatgrids](https://support.serato.com/hc/en-us/articles/360002025835-Beatgrids) |
| A | [Autoset](https://support.serato.com/hc/en-us/articles/115000489113-Autoset) |
| C | [Cues](https://support.serato.com/hc/en-us/articles/115000501614-Cues) |
| Q | [Cue parameters](https://support.serato.com/hc/en-us/articles/115000500413-Cue-parameters) |
| S | [Stems](https://support.serato.com/hc/en-us/articles/7645256113551-Stems) |
| M | [Miscellaneous: velocity, automation, undo/redo](https://support.serato.com/hc/en-us/articles/115003945934-Miscellaneous) |
| L | [Loading a file](https://support.serato.com/hc/en-us/articles/115000489094-Loading-a-file) |

## Version 0.3 presentation and workstation scope

The user subsequently requested a beige MPC-style hardware interface with green
LCDs, plus pattern recording, arrangement, effects and performance layers.
Version 0.3 deliberately changes the visual presentation while retaining the
source/cue interaction hierarchy below. Rubber pads use cue-color indicators;
neutral parameter text follows the hardware palette. Studio routing replaces
legacy device-pair routing in the app: live pads and source audition feed the
selected mixer track, and scheduled clips carry an explicit destination. Options
labels that control “Live sampler track.” Cue-level legacy output data remains
readable for session compatibility and engine callers without the workstation.

Sixteen single-lane patterns, eight arrangement tracks, four scenes, built-in
and hosted effects, frozen audio assets, mixer controls and master-output
recording extend the sampler beyond the reference. These features are described
in [the workstation architecture](WORKSTATION_ARCHITECTURE.md) and README.

## Reference visual and interaction structure

Use the [annotated official screen](https://support.serato.com/hc/article_attachments/7728847814031)
as the layout reference, rather than the previous sidebar/inspector design.

The hierarchy is: title/header tools; a source-transport strip containing mode,
stems, track metadata, global key/detune and tempo; thin full-track overview;
large detailed waveform; horizontal selected-cue parameter strip; Autoset,
selection/delete and keyboard tools; a four-row, eight-column pad grid.
Controls and cue handles use the selected cue's color; multi-selection parameters
use white. Empty pads are dark grey. The detailed view and overview both show
cue positions and the source playhead. [O, C]

The official pad image shows typing keys `A W S E D F T G` on the bottom row
and `Y H U J K O L P` on the row above. The official
[Options screenshot](https://support.serato.com/hc/article_attachments/7728957926543)
shows `Pad 10 / A1`. These establish visible labels, not a numeric MIDI octave
convention. Numeric MIDI-note defaults still need confirmation.

## Playback, transport and cue acceptance

| Test | Action and expected result | Source |
| --- | --- | --- |
| P01 | Load a track; play/pause auditions the source independently of cue assignment. Elapsed position updates. | O, P |
| P02 | Drag the main waveform to scrub source position; click/release in the overview to seek and change the detailed selection. | P |
| P03 | Zoom buttons, wheel-over-waveform and documented keyboard shortcuts change detailed zoom without changing cue positions. | P |
| P04 | Enable Freeze during playback: the editing view remains stationary so cue lengths/parameters can be edited. Verify exact audio/playhead behavior against the running reference; manual wording is ambiguous. | P |
| C01 | Click or send the mapped MIDI note to an empty pad: create its cue at the current source playhead. Existing pads trigger their stored cues. | C |
| C02 | Trigger a cue: its pad lights, selection follows it, and the source playhead jumps to its cue position and plays. | C |
| C03 | A cue may have no explicit end; do not conflate that state with an unassigned pad. Without a set length it can play to source end. | C |
| C04 | Drag the top start handle to change start; drag the bottom end handle to set length. Show the colored region only when length is explicitly set. | C |
| C05 | With Quantize on, newly set starts and endpoints snap to **1/8 beat** positions. With it off they do not snap. Quantize is not a request to delay incoming notes until a future beat. | O |
| C06 | **Hold is the default.** While held, a cue plays. Note-off starts its release envelope; after release the playhead returns to cue start. | C |
| C07 | In Hold, reaching an explicit cue end while still held leaves the playhead stationary there until release, then returns it to cue start. | C |
| C08 | In Trigger, note-off does not stop the cue. Its release envelope starts **after attack finishes**, and playback ends when that release completes or the cue/file end is reached. The playhead stays at the stopped position. | C |
| C09 | In Mono, subsequent cues use the reference's monophonic behavior. In Poly, demonstrate **32 simultaneous voices**, not the previous 16-voice cap. | C |
| C10 | Keyboard mode plays one selected cue chromatically across a piano keyboard. Validate independent pitch/time behavior against the reference; the previous playback-rate shortcut changes duration. | C, Q |
| C11 | With Velocity on, otherwise identical MIDI notes of different velocities have correspondingly different levels; with it off velocity is ignored. Do not invent a response curve without comparison. | O, M |

## Cue editing and parameter acceptance

| Test | Action and expected result | Source |
| --- | --- | --- |
| E01 | Command/Control-click adds/removes cues from selection. Select All selects assigned cues. Parameter controls are cue-colored for a single selection and white for multiple selections. | C, Q |
| E02 | A parameter edit affects every selected cue. If all selected values agree, display that value; otherwise display the parameter default. Unselected cues stay unchanged. | C |
| E03 | Shift-click a parameter restores its default. Command/Control-drag makes finer changes. | Q |
| E04 | Delete removes selected cues and their parameters. Shift-click a cue deletes it, including a favorite. Select All then Delete clears all assigned cues. | C |
| E05 | Right-click Rename changes the cue name. Dragging onto an empty pad moves the cue; dragging onto an occupied pad swaps them. | C |
| E06 | Option/Alt-drag duplicates a cue, including color/parameters, and overwrites an occupied destination. Output routing follows moved cues. | C |
| E07 | Favorite protects a cue from repeated Autoset, but does not protect it from explicit deletion. | A, Q |
| E08 | Undo/Redo remembers up to **100 actions** per project/session, including cue parameter edits and supported Autoset/delete operations. | O, M |
| E09 | Cue Level accepts **-12 to +12 dB**. | Q |
| E10 | Cue Filter exposes the documented **80 Hz to 17 kHz** range. Verify neutral position, LP/HP behavior and taper against the reference before declaring audio equivalence. | Q |
| E11 | Attack and Release each accept **0 to 10 seconds** and obey the Hold/Trigger rules above. | Q, C |
| E12 | Reverse changes cue playback direction. Cue Key Shift accepts **-24 to +24 semitones**, relative to the source/global key so global key changes affect displayed cue shifts. | Q |
| E13 | Cue Time Stretch represents **speed**, from **0.25x to 4x**, displayed as **-75% to +300%** relative to original speed. Cue speed combines with source tempo. Its 2x/half buttons change speed, not duration. | Q |
| E14 | In Keyboard mode Favorite is replaced by Glide. Glide is available only in Mono, with off or **0.01 ms to 5 seconds** transition time. | Q |
| E15 | Glide Trigger retriggers voice/attack/release for each new note; Glide Sustain retunes the existing voice and only the first note retriggers its envelope. | Q |
| E16 | Options reveals cue/pad number, MIDI note and output channel. Standalone output adaptation must be explicit; a channel label without corresponding audio routing is not parity. | C |

The manual explicitly documents Hold and Find Samples as defaults. The parameter
strip screenshot shows zero semitone shift and 0% time stretch. It does not
establish every numeric default, toggle default, filter taper or velocity curve;
record those from a specified running Serato version before asserting them.

## Autoset acceptance

| Test | Action and expected result | Source |
| --- | --- | --- |
| A01 | Provide all **five** modes. Find Samples is initially selected. Repeated Autoset replaces non-favorite cues and retains favorites. | A |
| A02 | Find Samples searches suitable source regions and places randomized, on-beat cues within them. Taking only the earliest N transients does not satisfy that behavior. | A |
| A03 | Set Slicer creates **32 consecutive cues starting at the current playhead**, with lengths selectable from **1/16 through 16 beats**. Domain-shift controls move the region; changing slice length immediately resets cues accordingly. | A |
| A04 | Set Random generates 32 random source locations and changes them on subsequent presses. The manual does not independently require on-beat placement in this mode; test Quantize-on/off behavior separately. | A, O |
| A05 | Key Shift Pad duplicates the chosen cue across pads using its key as root, with the documented 1- or 2-semitone increments spanning **-12 to +12**. The exact 32-slot sequence/duplication policy needs running-reference confirmation. | A |
| A06 | Serato DJ mode imports file-associated Serato DJ cues and colors; disable that mode when no such cues exist. It must not silently substitute another chop algorithm. | A |

## Global source and beatgrid acceptance

| Test | Action and expected result | Source |
| --- | --- | --- |
| G01 | Display artist/title, original key, original BPM and elapsed time in source metadata. Keep current global key/detune and current BPM controls distinct. Key/Detune switches display; Alt-click cycles key display format. | O |
| G02 | Current BPM supports 2x/half. Sync follows the host tempo in the reference plugin. Standalone must define an internal/external clock adaptation instead of claiming host synchronization. | O |
| G03 | Pitch/time processing can be enabled/disabled; Sync is unavailable when it is disabled. The reference disables this processing on load for clips under 3 seconds, or under 10 seconds when the filename contains `808`. | O |
| B01 | Grid mode displays beat/bar/tempo markers. Support drag adjustment and Control-drag slip of the whole grid. Tempo markers bound local tempo regions. | B |
| B02 | Grid Save commits edits; Cancel restores the pre-edit grid. Save is only shown once changed. BPM edits change the grid, with coarse/fine adjustment, direct entry, 2x/half and tap tempo. | B |
| B03 | Metronome is audible only while auditioning the source in Grid mode, not while triggering a pad. | B |
| B04 | Set Grid Start replaces markers at playhead; Set adds a tempo marker, up to **128**. Clear removes the nearest marker; Clear All removes the grid. Adjust arrows stretch/contract the relevant tempo region. | B |

## Stems, loading and persistence acceptance

| Test | Action and expected result | Source |
| --- | --- | --- |
| S01 | Four stem controls operate on vocals, melody, bass and drums. Click mutes; Shift-click solos. Changing stem selection changes audio and the main waveform's color/content together. | S |
| S02 | Current reference separation is initiated from a mixed source; importing pre-separated files is a different workflow. The earlier user-approved imported-stem scope must not be described as exact native-separation parity. | S |
| L01 | Start/eject presents Load Audio File. Drop or choose a supported file, show cancellable analysis, then show the loaded source. Eject discards existing cues. | L |
| L02 | If referenced audio moves, expose Locate Audio File and restore the saved cue state after relocation. Do not silently load another source or discard saved cues. | L |
| L03 | Restore cue identity/assignment, explicit or absent end, colors/names, favorites, selections where supported, modes, processing, grid, stem selection and routing. Preserve older Samplify sessions through a deliberate migration. | C, Q, B, L |

## Model/API implications

The rebuilt model distinguishes a point cue from an explicit region using
`hasEnd`: an assigned point cue stores file end in `end` with `hasEnd == false`;
an unassigned pad has `end <= start`. Favorite, name/color and output destination
belong to the cue so moves/copies preserve them. Hold/Trigger is independent of
Mono/Poly. The model includes velocity behavior, independent global
key/detune/tempo processing, a cue speed multiplier, Glide mode/time, and a
piecewise beatgrid with up to 128 tempo anchors. `beatgridEnabled` distinguishes
a cleared grid from an empty marker vector that could otherwise imply a default
grid.

Source transport, cue voices and waveform viewport need separate ownership.
Audio snapshots must report source position, audition state and active cue IDs;
incoming MIDI selection must reach the UI. Multi-selection and edit operations
need model-level commands suitable for the 100-action undo history.

Migrate the former duration multiplier with `newSpeed = 1 / oldTimeRatio` so
existing saved audio behavior is preserved. Quantize migration must not retain
the old delayed-note behavior. Reuse validated audio file I/O and realtime-safe
ownership, but do not require full-bank rendering or disabled UI for every cheap
parameter edit.

## Still requiring direct comparison

The public articles do not establish numeric MIDI-note numbering, all parameter
defaults, source/global pitch and detune limits, velocity response curve, exact
filter transfer function, every keyboard shortcut, retrigger/choke details,
Freeze's precise audio/playhead behavior, or proprietary Find Samples and
Pitch 'n Time results. Supported-format coverage needs the separate format list.
Embedded Serato cue-tag parsing now has dedicated fixture tests; the exact Key
Shift Pad distribution still requires direct-reference fixtures. Parser fixture
coverage does not establish every historical Serato tag variant. Keep remaining
items unverified until evidence exists; a control's presence or a passing sampler
unit test does not prove matching behavior or DSP quality.

Samplify currently uses numeric MIDI notes 24 through 55 for its 32 cue slots,
with the display convention C1 = 24. Pad 10 therefore displays A1, consistent
with the official Options image. This is an explicit implementation convention;
the reference's numeric MIDI octave default has not been independently verified.

The original target remains a standalone macOS app. Host automation, DAW bus
routing and vendor account/licensing UI are not directly transferable. Record
explicit standalone adaptations instead of displaying nonfunctional substitutes.

## Version 0.2 sampler baseline and verification history

The version 0.2 rebuild follows the documented source/cue workflow and replaces the
earlier sidebar sampler layout. It includes source audition/seeking, the overview
and detailed waveform, manual point/region cues, Hold/Trigger, 32-voice Poly,
velocity, multi-selection, favorites, cue moves/copies, undo/redo, grid editing,
the five Autoset entries, and mixed-source neural separation. Time/pitch uses
Rubber Band and stems use an independent Demucs model; these are not Serato's
proprietary engines.

Completed receipts below are from the **Linux validation environment** for this
rebuild. A previous macOS release of the earlier app does not validate this
implementation.

| Completed validation | Observed result and scope |
| --- | --- |
| Latest registered CTest run | **5/5 suites passed**: portable core **61** checks, cue editing **62**, stretch **25**, native stems **17**, Serato metadata **40**. |
| Device-free audio/session/MIDI integration | **123 checks passed, 0 failed**. C++ and linked static-C allocator hooks observed zero allocation/deallocation on the exercised callback paths, including +/-48-semitone, Glide and tempo-edge cases. This does not certify every hardware deadline or every possible call path. |
| Stretch capacity repair | Three targeted capacity-patch cases also passed sanitizer validation; the current stretch suite passed 25/25. |
| Embedded Serato cue metadata | **40 fixture checks**, including sanitizer validation, cover supported ID3/GEOB and FLAC comment paths, legacy/modern markers, cue names/colors and malformed inputs. |
| Native model execution | **17 checks** used real inference and verified frame alignment, distinct stem outputs, reconstruction behavior and cancellation. These are functional checks, not comparison against Serato's separated audio. |
| Latest real GUI run | **21 controller workflow checks and 6 stem workflow checks passed** with real UI/model execution and an empty error field. Normal **1280 x 960**, compact **960 x 720**, and empty-state screenshots were saved. The report correctly identifies this as a Linux run, not macOS. |

The first rebuild passed native macOS compilation, all six test suites, and
universal architecture checks in [run 39779683282](https://github.com/sroohi/mellowdy-chop/actions/runs/39779683282).
Its native GUI gate blocked packaging. The follow-up fixes stale cue focus after
Stop and preserves unclipped audio in 32-bit float stem caches; a fresh native
GUI and packaging run is required for these changes. Physical audio/MIDI devices, multichannel
hardware routing, performance under a representative production workload, and
direct comparison with a specified running Serato Sample version remain to be
verified.

Global key display supports **Alt-click key-format cycling** between traditional,
Camelot and Open Key notation. The source player extracts embedded artist/title
tags, with native AudioToolbox metadata fallback on macOS, and uses the filename
when those tags are absent. The exact reference format order still requires a
running-version comparison. Current parameter defaults, key/velocity response,
filter curves, Freeze details, Key Shift distribution and proprietary analysis/DSP
results retain the comparison limits stated above. Presence of a similarly
labeled control is not evidence of complete parity.
