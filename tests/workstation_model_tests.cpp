#include "workstation/WorkstationModel.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace samplify;
namespace {
int checks = 0, failures = 0;
void check(bool value, const char* message) {
    ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> void rejects(F action, const char* message) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } check(rejected, message);
}
PatternNote note(double beat, double duration = .25, WorkstationId id = 0) {
    PatternNote result; result.id = id; result.beat = beat; result.duration = duration; result.pad = 3; result.velocity = .75f; return result;
}
ArrangementClip clip(WorkstationId content, double start = 0, double length = 16) {
    ArrangementClip result; result.contentId = content; result.startBeat = start; result.lengthBeats = length; return result;
}
void timing() {
    check(quantizeBeat(.12, .25) == 0 && quantizeBeat(.125, .25) == .25, "nearest-grid quantization has defined halfway rounding");
    check(quantizeBeat(1.123, 0) == 1.123, "disabled record quantization preserves exact time");
    check(quantizeBeat(3.99, .25) == 4, "recorded event can quantize to a loop boundary");
    check(nextLaunchBeat(4, 4) == 4 && nextLaunchBeat(4.001, 4) == 8, "launch boundary is inclusive and later events wait for next bar");
    check(nextLaunchBeat(1.123, 0) == 1.123, "disabled launch quantization is immediate");
    check(wrapBeat(16, 0, 16) == 0 && wrapBeat(15.75, 0, 16) == 15.75, "loop end wraps while preceding phase remains unchanged");
    check(wrapBeat(-.25, 0, 16) == 15.75 && wrapBeat(101, 8, 24) == 21, "loop wrapping handles negative phase and multiple cycles");
    rejects([] { wrapBeat(1, 4, 4); }, "zero-length loop is rejected");
    rejects([] { quantizeBeat(1, -.25); }, "negative quantization is rejected");
    rejects([] { nextLaunchBeat(std::numeric_limits<double>::infinity(), 4); }, "nonfinite launch time is rejected");
}
void patterns() {
    WorkstationState state; validateWorkstation(state);
    std::set<WorkstationId> ids; for (const auto& p : state.patterns) ids.insert(p.id);
    check(ids.size() == 16 && !ids.count(0), "sixteen patterns start with distinct nonzero persistent identities");
    check(addPatternNote(state, 0, note(15.75, 1)), "note may cross pattern end for next-cycle noteoff");
    check(!addPatternNote(state, 0, note(16)), "note cannot start exactly at pattern end");
    check(!addPatternNote(state, 0, note(0, 16.01)), "note duration cannot exceed complete pattern cycle");
    check(!addPatternNote(state, -1, note(0)) && !addPatternNote(state, 16, note(0)), "invalid pattern indices are rejected");
    const auto firstId = state.patterns[0].notes.front().id;
    check(firstId != 0 && !addPatternNote(state, 0, note(0, 1, firstId)), "generated note ID is nonzero and duplicate ID is rejected");
    check(addPatternNote(state, 0, note(4)) && state.patterns[0].notes.front().beat == 4, "inserted notes are ordered by musical time");
    auto changed = note(5); changed.pad = 31; changed.semitones = -48;
    check(updatePatternNote(state, 0, firstId, changed), "note editing keeps identity while moving pitch and time");
    auto invalid = changed; invalid.beat = -1;
    check(!updatePatternNote(state, 0, firstId, invalid) && state.patterns[0].notes.back().beat == 5, "invalid note move leaves previous edit intact");
    check(removePatternNote(state, 0, firstId) && !removePatternNote(state, 0, firstId), "delete identifies exactly one note and reports missing note");
    check(addPatternNote(state, 0, note(3.75, 12)), "long cross-boundary note is representable");
    check(resizePattern(state, 0, 4) && state.patterns[0].notes.size() == 1
        && state.patterns[0].notes[0].beat == 3.75 && state.patterns[0].notes[0].duration == 4,
        "shortening pattern drops outside starts and caps duration while retaining a crossing note");
    check(!resizePattern(state, 0, 3.999) && !resizePattern(state, 0, 64.001)
        && state.patterns[0].lengthBeats == 4, "pattern length stays within four to sixty-four beats");
    validateWorkstation(state);
}
void clipsAndAssets() {
    WorkstationState state;
    auto c = clip(state.patterns[0].id); c.track = 7;
    check(addArrangementClip(state, c), "pattern clip can address last mixer track");
    const auto id = state.arrangement[0].id;
    c.id = id; c.startBeat = 12; c.offsetBeats = 4; c.lengthBeats = 32;
    check(updateArrangementClip(state, id, c), "looping clip supports source offset and repeated cycles");
    c.loop = false;
    check(!updateArrangementClip(state, id, c) && state.arrangement[0].loop,
        "nonlooping clip cannot exceed remaining content and rejection is transactional");
    c.lengthBeats = 12;
    check(updateArrangementClip(state, id, c), "nonlooping clip accepts exact source-end boundary");
    check(resizePattern(state, 0, 8) && state.arrangement[0].lengthBeats == 4,
        "shortened pattern trims dependent one-shot clip to valid remaining duration");
    check(resizePattern(state, 0, 4) && state.arrangement.empty(), "pattern shrink removes clip whose source offset no longer exists");
    c = clip(9999);
    check(!addArrangementClip(state, c), "clip cannot reference nonexistent pattern");
    c = clip(state.patterns[1].id); c.track = 8;
    check(!addArrangementClip(state, c), "clip cannot route to nonexistent track");
    c.track = 0; c.startBeat = workstationMaxBeat - 1; c.lengthBeats = 2;
    check(!addArrangementClip(state, c), "clip end cannot overflow supported arrangement timeline");
    AudioAsset asset; asset.name = "Frozen percussion"; asset.filePath = "/tmp/Frozen percussion.wav"; asset.lengthBeats = 8;
    check(addAudioAsset(state, asset), "frozen audio asset receives persistent ID");
    const auto assetId = state.audioAssets[0].id;
    c = clip(assetId, 4, 24); c.kind = ClipKind::Audio;
    check(addArrangementClip(state, c), "frozen audio may loop independently in arrangement");
    check(setLaunchSlot(state, 7, 3, {ClipKind::Audio, assetId, false})
        && state.launchSlots[31].contentId == assetId, "launch matrix indexing is track-major across eight tracks/four scenes");
    check(!setLaunchSlot(state, 8, 0, {}) && !setLaunchSlot(state, 0, 4, {})
        && !setLaunchSlot(state, 0, 0, {ClipKind::Pattern, 9999, true}), "launch assignment rejects invalid cells and missing content");
    check(removeAudioAsset(state, assetId) && state.audioAssets.empty() && state.arrangement.empty()
        && state.launchSlots[31].kind == ClipKind::Empty, "removing frozen audio clears dependent timeline clips and launch slots");
    check(!removeAudioAsset(state, assetId), "removing missing audio asset is harmless and reports no change");
    validateWorkstation(state);
}
void malformed() {
    WorkstationState state;
    state.patterns[1].id = state.patterns[0].id;
    rejects([&] { validateWorkstation(state); }, "duplicate pattern identities rejected");
    state = {}; state.tracks[7].pan = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { validateWorkstation(state); }, "nonfinite mixer state rejected");
    state = {}; state.masterEffects[0].type = static_cast<FxType>(999);
    rejects([&] { validateWorkstation(state); }, "unknown DSP effect rejected");
    state = {}; state.sendAEffects[0].parameters[5] = 1.01f;
    rejects([&] { validateWorkstation(state); }, "normalized effect parameters cannot escape bounds");
    state = {}; state.launchSlots[0] = {ClipKind::Audio, 42, true};
    rejects([&] { validateWorkstation(state); }, "dangling audio launch reference rejected");
    state = {}; state.recordQuantizeBeats = .001;
    rejects([&] { validateWorkstation(state); }, "nonzero quantization below finest supported grid rejected");
    state = {}; state.loopEndBeat = state.loopStartBeat;
    rejects([&] { validateWorkstation(state); }, "invalid persisted loop bounds rejected even while loop disabled");
    state = {}; auto& plugin = state.masterPlugins[0];
    plugin.xmlDescription = "<PLUGIN name=\"Unavailable AU\" format=\"AudioUnit\"/>";
    plugin.name = "Unavailable AU"; plugin.stateBase64 = "AQID";
    validateWorkstation(state); check(true, "unavailable plugin descriptor and exact opaque state are preserved structurally");
    plugin.stateBase64 = "AB==";
    rejects([&] { validateWorkstation(state); }, "noncanonical Base64 padding bits rejected");
    plugin.stateBase64 = "4.ABCD";
    rejects([&] { validateWorkstation(state); }, "JUCE proprietary binary encoding cannot masquerade as standard Base64");
    plugin.stateBase64.assign(4 * ((workstationMaxPluginStateBytes + 2) / 3) + 4, 'A');
    rejects([&] { validateWorkstation(state); }, "oversized plugin state rejected without truncating bytes");
    state = {}; state.masterPlugins[0].stateBase64 = "AA==";
    rejects([&] { validateWorkstation(state); }, "opaque plugin state requires a recoverable descriptor");
    state = {}; state.patterns[0].notes = {note(2, 1, 2), note(1, 1, 3), note(1, .5, 1)};
    normalizeWorkstation(state);
    check(state.patterns[0].notes[0].id == 1 && state.patterns[0].notes[1].id == 3
        && state.patterns[0].notes[2].id == 2, "normalization canonically orders events without changing persistent identities");
}
}
int main() {
    try { timing(); patterns(); clipsAndAssets(); malformed(); }
    catch (const std::exception& e) { ++failures; std::cerr << "Unexpected model error: " << e.what() << '\n'; }
    std::cout << checks - failures << '/' << checks << " workstation model checks passed\n";
    return failures ? 1 : 0;
}
