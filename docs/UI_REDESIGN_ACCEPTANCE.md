# Workstation interface redesign acceptance

The October 2026 redesign changes the complete interface structure while
retaining the shipped 0.3 sampler and workstation behavior. This document is a
review contract, not a claim that its checks have passed. It supersedes the
Serato-shaped **visual hierarchy** in [SERATO_PARITY.md](SERATO_PARITY.md); that
document's behavioral requirements and recorded limitations still apply.
Audio processing, scheduling, session schemas and native model behavior remain
outside this interface-only change.

The baseline inventory comes from `MainComponent`, `SourceWaveform`,
`WorkstationComponent`, `StudioComponent`, their callbacks and existing workflow
smoke, plus [the workstation architecture](WORKSTATION_ARCHITECTURE.md).
An engine API without an existing user-facing control is not a requirement to
add a feature. In particular, exposing a new MIDI-learn interface is outside
retention; existing MIDI input and its mappings must continue working.

## Structural gates

| ID | Required visible organization | Fails this gate |
| --- | --- | --- |
| V01 | Persistent vertical mode rail, a compact masthead and a separate bottom transport. Root layout reserves approximately 170 px, 60 px and 100 px including dock margins. | Keeping the former horizontal top tabs and adding decorative rail labels. |
| V02 | Sampler: a left performance deck with a banked 4×4 pad matrix; a distinct right source LCD and module pages for editing. Both banks expose the same 32 underlying cues. | Retaining the former full-width source strip, overview, waveform, horizontal cue-parameter strip and 8×4 grid, with new colors or frames. |
| V03 | Song: the timeline is the principal editing surface, with a separate right clip/content inspector. | Reusing the former stacked timeline and pattern-grid arrangement with a replacement heading. |
| V04 | Patterns: a left pattern-cartridge selector, central note editor and separate right selected-note inspector. | Keeping the former horizontal pattern-button row and bottom inspector arrangement. |
| V05 | FX: four inserts form a horizontal patchbay; built-in controls and hosted plug-in controls have visibly separate areas. | Keeping the former four-button vertical rack and merely changing its paint. |
| V06 | Perform: a performance console makes scenes, clip state and eight mixer channels legible as one working surface. | Carrying the old shared page scaffolding forward without rearranging its primary regions. |
| V07 | The beige hardware treatment, green LCDs, rubber pads, knobs and indicators support these new regions consistently. Control labels explain their musical action. | Decoration replacing a control, an unlabeled active toggle, or internal implementation wording in a normal user flow. |

Review screenshots of every page at the actual normal and minimum supported
window sizes. The redesigned shell declares 1180×800 minimum and targets
approximately 1440×890 on macOS. Its minimum page area is approximately
980×620 after the rail, masthead, dock and margins. Test the actual page bounds
**after** these reservations rather than an independently sized child component.
All active controls must have usable hit areas inside their visible ancestors.
Scrollbars or deliberate module pages may expose secondary controls; overlapping,
offscreen or zero-sized controls do not count as retained functionality. Capture
empty, loaded and compact states, including Grid and cue Options modules.

## Retained sampler controls and interactions

| ID | Reachable feature set | Required outcome |
| --- | --- | --- |
| S01 | Load chooser/drop, cancellable analysis, source metadata, eject, source Play/Pause, full-source overview and detailed waveform. | Empty/load/busy/loaded states are legible; seeking and source audition remain independent of cue assignment. Artist/title or filename fallback and elapsed position remain available. |
| S02 | Waveform zoom buttons/wheel/keys, Freeze, selected cue handles and explicit-end creation. | The overview retains full-source navigation; detail zoom changes view only; point cues remain distinguishable from explicit ranges; start/end edits use the existing quantize behavior. |
| S03 | Banks A/B, 16 physical pads per bank, all 32 cue numbers, names/colors, selected/active/favorite state. | Bank changes do not renumber or replace cues. MIDI notes 24–55 still address their existing IDs. A cue on the other bank remains discoverable when selected by MIDI or the waveform. |
| S04 | Empty-pad assignment, occupied-pad playback, Command/Control multi-selection, Select All, deletion and rename. | Selection may span both banks. Group edits affect selected cues on the hidden bank, and one cue can still be selected without losing a deliberate group unexpectedly. |
| S05 | Move to empty, swap occupied and Alt-copy/overwrite, including a destination on the other bank. | A reachable cross-bank gesture or destination menu preserves identity, color, parameters, routing and copy/swap semantics. Ordinary within-bank drag remains functional. |
| S06 | Level, filter, attack, release, reverse, favorite, cue key shift, speed and speed half/double. | Single/group selection, common-value/default display, reset/fine-edit gestures and one-action-per-drag undo retain existing behavior and ranges. |
| S07 | Hold/Trigger, Mono/Poly, velocity, keyboard mode, Glide time and Trigger/Sustain, quantize. | These are distinct modes. Glide is available in keyboard/Mono context. Keyboard note-off releases its original cue/pitch even after a bank, selection or page change. |
| S08 | Global key/detune, Alt-click key-format cycle, source/current tempo, tempo half/double, Sync and pitch/time toggle. | Source metadata and editable values remain distinct. Display cycling cannot change pitch; Sync remains unavailable when pitch/time processing is disabled. Studio tempo handoff remains intact. |
| S09 | Five Autoset modes; Slicer size 1/16–16 beats and left/right domain shift; Key Shift increment. | Repeated fills preserve favorites; unavailable embedded-cue import stays disabled; mode-specific controls are reachable. Slicer still starts from the playhead. |
| S10 | Vocals/Melody/Bass/Drums, native separation with progress/cancel, imported/restored aligned stems where already supported. | Click mute and Shift-click solo work globally or on selected cues; waveform content follows actual audible stems. Changing modules or banks does not reset the mix. |
| S11 | Cue Options, cue/MIDI identity and live sampler destination track; Audio/MIDI device settings and status. | The standalone routing adaptation remains explicit and changes the real mixer destination. Existing connected-device operation remains available. |
| S12 | Sampler Undo/Redo, source/session relocation, Open/Save and processed cue/bank export. | The 100-action history, file-reference recovery and legacy session compatibility survive the presentation change. Export remains reachable from the shell or sampler. |

Typing keys remain the existing `A W S E D F T G Y H U J K O L P` sequence.
The new bank selector may deliberately apply this sequence to the visible bank;
labels must explain that adaptation. Chromatic mode continues to play the
selected cue. Store the actual cue and pitch at key-down so key-up does not
depend on whichever bank or page is visible later. Text entry and numeric
editing must not accidentally launch notes or delete unrelated cues.

## Retained workstation controls and interactions

| ID | Reachable feature set | Required outcome |
| --- | --- | --- |
| W01 | Sixteen pattern selectors, destination track, pattern length, record quantization and Rec Notes. | The selected pattern and track remain unambiguous; played pad/MIDI notes record their timing, velocity, pitch and duration into the intended pattern. |
| W02 | Note drawing, selection, move in time/cue row, edge length, copy, delete, clear and selected-note duration/velocity/pitch. | Mouse edits and inspector widgets use existing validated edits and undo. Scroll exposes all cue rows and the full pattern. Pad-row audition remains reachable. |
| W03 | Eight-track timeline, pattern/audio content selection, place, move, edge trim/resize, duplicate, delete, clip length/gain/loop and timeline navigation. | The selected clip's inspector follows selection, track and content. The Song page need not retain a second embedded pattern editor when the dedicated Patterns page exposes it. |
| W04 | Arrangement loop enable/start/end and musical position. | Edits affect the existing transport; seeking and loop playback remain available without confusing source audition with song playback. |
| W05 | Import audio, asset selection, locate/replace, source BPM and removal; decoded PCM/peak reconciliation on undo/redo and session restore. | Assets play independently of later sampler-source replacement. A missing/replaced file cannot reuse stale PCM from a different session with the same numeric ID. |
| W06 | Bounce Pattern and Rec Mix with visible recording/finalizing/error states and completed recording import. | Bounce retains its existing one-period, built-in-processing scope. Hosted processing is captured through the live stereo recorder. Finalization completes before import; errors do not create a misleading saved take. |
| W07 | Four scenes, 32 track/scene cells, pattern or audio assignment, slot clear, individual/scene launch, launch quantization and eight track Stops. | Empty, assigned, pending and playing states are distinct. Both pattern and audio slots remain assignable; scene launch affects the intended tracks. |
| W08 | Eight mixer channels with selection, gain, pan, mute, solo, Send A/B, A/B/Thru assignment; crossfader, master gain and meters. | Controls remain accessible through the Perform console and affect their original channels. Muted/soloed behavior, sends and live sampler destination remain unchanged. |
| W09 | Four built-in slots on eight tracks, Master, Send A and Send B; Off plus all 15 types; six dynamic parameters, actual-unit values, wet/dry and bypass. | Slot/bus selection shows the correct effect state. Every parameter is reachable at minimum size; send returns expose built-ins without pretending to support hosted inserts. |
| W10 | All 26 chain presets across Sound Design, Mixing and Mastering. | Choosing a preset installs the existing four-slot chain on the chosen bus and updates the patchbay and controls together. |
| W11 | Four hosted slots on eight tracks and Master; scan, choose/load, editor, bypass, remove and retry of missing saved plug-ins. | Hosted controls identify their own slot and bus. Missing plug-ins retain their state/status and pass through as before. Built-in slot selection must not silently edit a different hosted slot. |
| W12 | Workstation Undo/Redo, shared tempo covering the existing 20–999 BPM range, Open/Save and mode-specific Play/Stop/record controls. | Navigation does not recreate or reset the sampler, patterns, plug-ins or audio assets. Whole-studio session capture remains connected to the existing shell handoff. |

## Interaction evidence required

These workflows supplement existing DSP, model and audio-engine tests. A check
that directly changes a model field, invokes a private edit helper, or compares
a control value with itself does not establish that the redesigned interface is
usable. Route the action through an actual visible widget, canvas event or key
handler; assert the resulting state and, where relevant, the existing audio
snapshot. Before a synthetic click, verify the component is showing/enabled,
has nonempty bounds and is not clipped or covered at its intended hit point.

| ID | Workflow | Evidence |
| --- | --- | --- |
| U01 | Navigate all five rail modes, return to Sampler, then switch every sampler module. Repeat at minimum size. | Correct page/module visibility, meaningful content bounds, retained source/cue selection and no covered active controls; screenshots for every page. |
| U02 | Click Bank B, click an empty displayed pad at a known playhead, release it, switch to A and back. | The intended cue 17–32 is created/played, its identity and range persist and the keyboard label matches the bank. |
| U03 | Select a cue on A, Command-click one on B, edit Level using its actual widget, then return to A. | Both selected cues change; unrelated cues do not; group indication remains legible across banks. |
| U04 | Open an actual pad's destination menu to copy across banks, then move/swap across banks and Undo. | Destination contents, identity/parameters and undo restoration match copy versus move/swap semantics. Within-bank drag remains separately exercised. |
| U05 | Press a mapped typing key on B, switch bank/page before release, release, then return to Sampler. Repeat chromatic mode. | The original cue/pitch releases and no held voice remains. Existing MIDI mapping is unchanged; physical MIDI requires its own device check. |
| U06 | Seek using the visible overview; adjust a real cue handle, zoom/freeze, open Grid, edit a marker, Cancel, reopen and Save another edit. | Playhead, cue range, view and grid backup/commit behavior agree; Grid tools remain reachable with no parameter controls covering them. |
| U07 | Open Stems, run the existing real native-separation workflow and use mute/Shift-solo; switch bank/module. | Existing inference checks pass, waveform follows actual stem PCM and selection/mix are retained. No substitute animation or fabricated meter evidence. |
| U08 | Choose pattern 16, draw a note with canvas input, move/resize/copy it, edit velocity/pitch in the inspector, Delete and Undo. | Correct note IDs/timing and inspector selection; note editor and controls are hit-testable at minimum size. |
| U09 | Place/move/resize/copy a pattern clip through timeline input; import a real audio asset and select it. | Song inspector tracks the selected object, audio remains available after source replacement and scroll/navigation exposes the intended timeline location. |
| U10 | Select Send B, patch slot 4, choose a preset and edit an actual-unit parameter; then choose a track's hosted slot. | Correct distinct rack/slot state, visible wet/bypass controls and a reachable real plug-in manager/editor path. |
| U11 | Assign track 8/scene 4 through its actual cell, launch it, launch a scene, stop the track; adjust mute/solo, Send B and crossfader assignment. | Pending/active/stop snapshots and mixer state correspond to the chosen track, with all channel controls visible or deliberately scrollable. |
| U12 | Record notes through pad/key handlers, stop recording, record/finalize/import a short live mix and reopen a saved studio. | Notes reach the intended pattern; take import waits for finalization; source, cues, assets, effects and plug-in state remain connected after restore. |

Automated audio stepping is allowed only when no real audio device is running,
or after an explicit safe disconnect. Never call `prepareToPlay` or
`getNextAudioBlock` from the GUI smoke while the hardware callback can execute
concurrently. Synthetic test fixtures belong only to an explicit smoke command;
normal startup must not install sample notes, clips, meters or playback states.

## Review status

Independent source and visual review confirms that the sampler, Song, Patterns
and FX pages replace their former major regions. Perform now combines clip
launch controls and mixer strips with a separate scene/master column. All 11
images from the passing Linux GUI run were reviewed: empty, loaded, compact,
Bank B, Envelope, Stems, Grid, Song, Patterns, FX and Perform. No visible panel
clipping or overlap was found. Combo labels are readable and source audition is
explicitly labeled. Tune and cue Options have source/layout coverage; they are
not separate images in this evidence set.

The Linux GUI report passed **97 flags**: 29 sampler, six involving real native
stem separation, 48 workstation and 14 shell/console checks. Its error field is
empty. All 11 CTest suites passed in approximately 53 seconds. These are Linux
receipts; the report correctly records `macOSRun: false`.

The new checks include Bank B pad mouse events, cross-bank group edits through
the Level widget, stored key-down identity across a bank change, all five module
layouts at minimum size, actual note/clip canvas drawing/moving/resizing/copying,
and normal/compact workstation control containment. Cross-bank copy exercises
the dispatch function used by its visible menu; it does not automate selecting
a native popup item. A console check described as wiring checks callback
presence; the Space shortcut check establishes controller routing, not audible
transport behavior by itself. Existing engine tests supply separate scheduling
and audio evidence.

Review fixes retained the visible 20–999 BPM workstation range and routed
otherwise unhandled shell keys to the active controller while preserving text
entry and sampler key-up ownership. The workstation is made visible before its
workflow runs. The final GUI rerun also passed the sampler numeric-entry focus regression:
a real active BPM text editor retains its focus and uncommitted text through
a control refresh, and commits the expected value afterward. Ordinary refresh
now changes visibility only for controls entering or leaving the layout.

The [native macOS workflow](https://github.com/sroohi/mellowdy-chop/actions/runs/39805453727)
then passed all 11 required suites in 38 seconds and all **96 native GUI flags**:
29 sampler, six real-stem, 47 workstation and 14 console checks. Its report has
`macOSRun: true` and an empty error field. The Linux-only offline callback guard
accounts for the one-check count difference. Native captures are 1440×890;
the compact capture is 1180×800. The repository's 11 images now use these actual
native captures.

The [published 0.4.0 release](https://github.com/sroohi/mellowdy-chop/releases/tag/samplify-v0.4.0-065cdf2)
was built from `065cdf2eae75bd3113c39f37c8fd4f335b3610e7`. Its downloadable
ZIP, DMG, model and notices were downloaded again and matched every published
SHA-256 entry. Physical audio/MIDI devices, arbitrary third-party plug-ins and
first-launch trust remain separate user-machine checks. This development
package is ad-hoc signed and is not Apple-notarized.
