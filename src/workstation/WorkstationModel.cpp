#include "WorkstationModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace samplify {
namespace {
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }
bool bounded(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}
void require(bool condition, const char* message) { if (!condition) invalid(message); }
bool shortText(const std::string& text, std::size_t maximum = 256) {
    return text.size() <= maximum && text.find('\0') == std::string::npos;
}
bool validKind(ClipKind kind, bool emptyAllowed) {
    return kind == ClipKind::Pattern || kind == ClipKind::Audio || (emptyAllowed && kind == ClipKind::Empty);
}
void validateFx(const FxSlot& slot) {
    require(static_cast<int>(slot.type) >= static_cast<int>(FxType::None)
            && static_cast<int>(slot.type) <= static_cast<int>(FxType::Tape), "Unknown effect type");
    require(bounded(slot.mix, 0, 1), "Effect mix out of range");
    for (const auto value : slot.parameters) require(bounded(value, 0, 1), "Effect parameter out of range");
}
void validateBase64(const std::string& state) {
    require(state.size() <= 4 * ((workstationMaxPluginStateBytes + 2) / 3), "Plugin state exceeds 4 MiB");
    require(state.size() % 4 == 0, "Plugin state is not standard Base64");
    if (state.empty()) return;
    auto digit = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::size_t padding = 0;
    if (state.back() == '=') ++padding;
    if (state.size() >= 2 && state[state.size() - 2] == '=') ++padding;
    require(state.size() / 4 * 3 - padding <= workstationMaxPluginStateBytes, "Plugin state exceeds 4 MiB");
    for (std::size_t i = 0; i < state.size() - padding; ++i)
        require(digit(static_cast<unsigned char>(state[i])) >= 0, "Plugin state is not standard Base64");
    if (padding == 2) require((digit(static_cast<unsigned char>(state[state.size() - 3])) & 15) == 0, "Noncanonical Base64 padding");
    if (padding == 1) require((digit(static_cast<unsigned char>(state[state.size() - 2])) & 3) == 0, "Noncanonical Base64 padding");
}
void validatePlugin(const HostedPluginSlot& slot) {
    require(shortText(slot.name) && shortText(slot.format) && shortText(slot.manufacturer)
            && shortText(slot.identifier, 4096) && shortText(slot.xmlDescription, 65536), "Plugin descriptor text exceeds bounds");
    validateBase64(slot.stateBase64);
    if (slot.xmlDescription.empty())
        require(slot.stateBase64.empty() && slot.name.empty() && slot.format.empty()
                && slot.identifier.empty() && slot.manufacturer.empty(), "Plugin state has no descriptor");
}
bool validNote(const PatternNote& note, double length) {
    return note.id != 0 && note.pad >= 0 && note.pad < 32
        && bounded(note.beat, 0, length) && note.beat < length
        && bounded(note.duration, std::numeric_limits<double>::min(), length)
        && bounded(note.velocity, 0, 1) && bounded(note.semitones, -48, 48);
}
double contentLength(const WorkstationState& state, ClipKind kind, WorkstationId id) {
    if (kind == ClipKind::Pattern) { if (const auto* p = findPattern(state, id)) return p->lengthBeats; }
    if (kind == ClipKind::Audio) { if (const auto* a = findAudioAsset(state, id)) return a->lengthBeats; }
    return 0;
}
bool validClip(const WorkstationState& state, const ArrangementClip& clip) {
    const double length = contentLength(state, clip.kind, clip.contentId);
    return clip.id != 0 && clip.track >= 0 && clip.track < static_cast<int>(workstationTrackCount)
        && validKind(clip.kind, false) && length > 0
        && bounded(clip.startBeat, 0, workstationMaxBeat)
        && bounded(clip.lengthBeats, std::numeric_limits<double>::min(), workstationMaxBeat)
        && clip.startBeat + clip.lengthBeats <= workstationMaxBeat
        && bounded(clip.offsetBeats, 0, length) && clip.offsetBeats < length
        && (clip.loop || clip.lengthBeats <= length - clip.offsetBeats + 1e-9)
        && bounded(clip.gain, 0, 4);
}
bool validAsset(const AudioAsset& asset) {
    return asset.id != 0 && shortText(asset.name) && shortText(asset.filePath, 4096)
        && !asset.filePath.empty() && bounded(asset.sourceBpm, 20, 999)
        && bounded(asset.lengthBeats, std::numeric_limits<double>::min(), workstationMaxBeat);
}
template<class Range> WorkstationId nextId(const Range& range) {
    std::set<WorkstationId> ids;
    for (const auto& item : range) ids.insert(item.id);
    for (WorkstationId id = 1; id != 0; ++id) if (!ids.count(id)) return id;
    return 0;
}
void sortNotes(Pattern& pattern) {
    std::stable_sort(pattern.notes.begin(), pattern.notes.end(), [](const auto& a, const auto& b) {
        return a.beat == b.beat ? a.id < b.id : a.beat < b.beat;
    });
}
bool validPatternIndex(int index) { return index >= 0 && index < static_cast<int>(workstationPatternCount); }
bool validQuantum(double quantum, double maximum) { return quantum == 0 || bounded(quantum, 1. / 64, maximum); }
}

WorkstationState::WorkstationState() {
    for (std::size_t i = 0; i < tracks.size(); ++i) tracks[i].name = "Track " + std::to_string(i + 1);
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        patterns[i].id = static_cast<WorkstationId>(i + 1);
        patterns[i].name = "Pattern " + std::to_string(i + 1);
    }
}

const Pattern* findPattern(const WorkstationState& state, WorkstationId id) {
    const auto it = std::find_if(state.patterns.begin(), state.patterns.end(), [id](const auto& p) { return p.id == id; });
    return it == state.patterns.end() ? nullptr : &*it;
}
Pattern* findPattern(WorkstationState& state, WorkstationId id) {
    return const_cast<Pattern*>(findPattern(static_cast<const WorkstationState&>(state), id));
}
const AudioAsset* findAudioAsset(const WorkstationState& state, WorkstationId id) {
    const auto it = std::find_if(state.audioAssets.begin(), state.audioAssets.end(), [id](const auto& a) { return a.id == id; });
    return it == state.audioAssets.end() ? nullptr : &*it;
}

void validateWorkstation(const WorkstationState& state) {
    require(bounded(state.tempo, 20, 999), "Workstation tempo out of range");
    require(bounded(state.masterGainDb, -60, 12) && bounded(state.crossfader, 0, 1), "Master mixer value out of range");
    require(validQuantum(state.recordQuantizeBeats, 16) && validQuantum(state.launchQuantizeBeats, 64), "Quantization out of range");
    require(bounded(state.loopStartBeat, 0, workstationMaxBeat) && bounded(state.loopEndBeat, 0, workstationMaxBeat)
            && state.loopEndBeat > state.loopStartBeat, "Invalid arrangement loop");
    require(validPatternIndex(state.selectedPattern) && state.selectedTrack >= 0
            && state.selectedTrack < static_cast<int>(workstationTrackCount), "Workstation selection out of range");
    for (const auto& track : state.tracks) {
        require(shortText(track.name) && bounded(track.gainDb, -60, 12) && bounded(track.pan, -1, 1)
                && bounded(track.sendA, 0, 1) && bounded(track.sendB, 0, 1), "Track mixer value out of range");
        require(track.crossfadeAssignment == CrossfadeAssignment::None || track.crossfadeAssignment == CrossfadeAssignment::A
                || track.crossfadeAssignment == CrossfadeAssignment::B, "Unknown crossfade assignment");
        for (const auto& slot : track.effects) validateFx(slot);
        for (const auto& slot : track.plugins) validatePlugin(slot);
    }
    for (const auto* rack : { &state.masterEffects, &state.sendAEffects, &state.sendBEffects })
        for (const auto& slot : *rack) validateFx(slot);
    for (const auto& slot : state.masterPlugins) validatePlugin(slot);
    std::set<WorkstationId> patternIds, assetIds, clipIds;
    for (const auto& pattern : state.patterns) {
        require(pattern.id != 0 && patternIds.insert(pattern.id).second, "Duplicate or zero pattern ID");
        require(shortText(pattern.name) && bounded(pattern.lengthBeats, 4, 64)
                && pattern.notes.size() <= workstationMaxNotes, "Pattern bounds exceeded");
        std::set<WorkstationId> noteIds;
        for (const auto& note : pattern.notes)
            require(validNote(note, pattern.lengthBeats) && noteIds.insert(note.id).second, "Invalid or duplicate pattern note");
    }
    require(state.audioAssets.size() <= workstationMaxAssets, "Too many audio assets");
    for (const auto& asset : state.audioAssets)
        require(validAsset(asset) && assetIds.insert(asset.id).second, "Invalid or duplicate audio asset");
    require(state.arrangement.size() <= workstationMaxClips, "Too many arrangement clips");
    for (const auto& clip : state.arrangement)
        require(validClip(state, clip) && clipIds.insert(clip.id).second, "Invalid or duplicate arrangement clip");
    for (const auto& slot : state.launchSlots) {
        require(validKind(slot.kind, true), "Unknown launch slot kind");
        require(slot.kind == ClipKind::Empty ? slot.contentId == 0 : contentLength(state, slot.kind, slot.contentId) > 0,
                "Launch slot references missing content");
    }
}

void normalizeWorkstation(WorkstationState& state) {
    validateWorkstation(state);
    for (auto& pattern : state.patterns) sortNotes(pattern);
    std::stable_sort(state.arrangement.begin(), state.arrangement.end(), [](const auto& a, const auto& b) {
        if (a.track != b.track) return a.track < b.track;
        return a.startBeat == b.startBeat ? a.id < b.id : a.startBeat < b.startBeat;
    });
}

double quantizeBeat(double beat, double quantum) {
    require(bounded(beat, 0, workstationMaxBeat) && validQuantum(quantum, 64), "Invalid beat quantization");
    return quantum == 0 ? beat : std::min(workstationMaxBeat, std::floor(beat / quantum + .5) * quantum);
}
double wrapBeat(double beat, double start, double end) {
    require(std::isfinite(beat) && bounded(start, 0, workstationMaxBeat) && bounded(end, 0, workstationMaxBeat)
            && end > start, "Invalid beat loop");
    const double length = end - start;
    double phase = std::fmod(beat - start, length);
    if (phase < 0) phase += length;
    return phase >= length ? start : start + phase;
}
double nextLaunchBeat(double beat, double quantum) {
    require(bounded(beat, 0, workstationMaxBeat) && validQuantum(quantum, 64), "Invalid launch quantization");
    if (quantum == 0) return beat;
    const double units = beat / quantum;
    const double nearest = std::round(units);
    return std::min(workstationMaxBeat, (std::abs(units - nearest) < 1e-10 ? nearest : std::ceil(units)) * quantum);
}

bool addPatternNote(WorkstationState& state, int index, PatternNote note) {
    if (!validPatternIndex(index)) return false;
    auto& pattern = state.patterns[static_cast<std::size_t>(index)];
    if (pattern.notes.size() >= workstationMaxNotes) return false;
    if (note.id == 0) note.id = nextId(pattern.notes);
    if (!validNote(note, pattern.lengthBeats) || std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& n) { return n.id == note.id; })) return false;
    pattern.notes.push_back(note); sortNotes(pattern); return true;
}
bool updatePatternNote(WorkstationState& state, int index, WorkstationId id, PatternNote note) {
    if (!validPatternIndex(index) || (note.id != 0 && note.id != id)) return false;
    auto& pattern = state.patterns[static_cast<std::size_t>(index)];
    auto it = std::find_if(pattern.notes.begin(), pattern.notes.end(), [id](const auto& n) { return n.id == id; });
    note.id = id;
    if (it == pattern.notes.end() || !validNote(note, pattern.lengthBeats)) return false;
    *it = note; sortNotes(pattern); return true;
}
bool removePatternNote(WorkstationState& state, int index, WorkstationId id) {
    if (!validPatternIndex(index)) return false;
    auto& notes = state.patterns[static_cast<std::size_t>(index)].notes;
    const auto before = notes.size(); std::erase_if(notes, [id](const auto& n) { return n.id == id; }); return notes.size() != before;
}
bool resizePattern(WorkstationState& state, int index, double length) {
    if (!validPatternIndex(index) || !bounded(length, 4, 64)) return false;
    auto& pattern = state.patterns[static_cast<std::size_t>(index)];
    pattern.lengthBeats = length;
    std::erase_if(pattern.notes, [length](const auto& n) { return n.beat >= length; });
    for (auto& note : pattern.notes) note.duration = std::min(note.duration, length);
    std::erase_if(state.arrangement, [&](const auto& c) { return c.kind == ClipKind::Pattern && c.contentId == pattern.id && c.offsetBeats >= length; });
    for (auto& clip : state.arrangement) if (clip.kind == ClipKind::Pattern && clip.contentId == pattern.id && !clip.loop)
        clip.lengthBeats = std::min(clip.lengthBeats, length - clip.offsetBeats);
    return true;
}
bool addArrangementClip(WorkstationState& state, ArrangementClip clip) {
    if (state.arrangement.size() >= workstationMaxClips) return false;
    if (clip.id == 0) clip.id = nextId(state.arrangement);
    if (!validClip(state, clip) || std::any_of(state.arrangement.begin(), state.arrangement.end(), [&](const auto& c) { return c.id == clip.id; })) return false;
    state.arrangement.push_back(clip); return true;
}
bool updateArrangementClip(WorkstationState& state, WorkstationId id, ArrangementClip clip) {
    if (clip.id != 0 && clip.id != id) return false;
    auto it = std::find_if(state.arrangement.begin(), state.arrangement.end(), [id](const auto& c) { return c.id == id; });
    clip.id = id;
    if (it == state.arrangement.end() || !validClip(state, clip)) return false;
    *it = clip; return true;
}
bool removeArrangementClip(WorkstationState& state, WorkstationId id) {
    const auto before = state.arrangement.size(); std::erase_if(state.arrangement, [id](const auto& c) { return c.id == id; }); return before != state.arrangement.size();
}
bool setLaunchSlot(WorkstationState& state, int track, int scene, LaunchSlot slot) {
    if (track < 0 || track >= static_cast<int>(workstationTrackCount) || scene < 0 || scene >= static_cast<int>(workstationSceneCount)
        || !validKind(slot.kind, true) || (slot.kind == ClipKind::Empty ? slot.contentId != 0 : contentLength(state, slot.kind, slot.contentId) <= 0)) return false;
    state.launchSlots[static_cast<std::size_t>(track) * workstationSceneCount + static_cast<std::size_t>(scene)] = slot; return true;
}
bool addAudioAsset(WorkstationState& state, AudioAsset asset) {
    if (state.audioAssets.size() >= workstationMaxAssets) return false;
    if (asset.id == 0) asset.id = nextId(state.audioAssets);
    if (!validAsset(asset) || findAudioAsset(state, asset.id)) return false;
    state.audioAssets.push_back(std::move(asset)); return true;
}
bool removeAudioAsset(WorkstationState& state, WorkstationId id) {
    const auto before = state.audioAssets.size(); std::erase_if(state.audioAssets, [id](const auto& a) { return a.id == id; });
    if (before == state.audioAssets.size()) return false;
    std::erase_if(state.arrangement, [id](const auto& c) { return c.kind == ClipKind::Audio && c.contentId == id; });
    for (auto& slot : state.launchSlots) if (slot.kind == ClipKind::Audio && slot.contentId == id) slot = {};
    return true;
}

} // namespace samplify
