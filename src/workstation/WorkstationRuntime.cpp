#include "WorkstationRuntime.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace samplify {
namespace {
constexpr std::size_t commandCapacity = 512, recordingCapacity = 8192;
constexpr double epsilon = 1e-10;

template <class T, std::size_t Size> class SpscQueue {
public:
    bool push(const T& value) noexcept {
        const auto write = writePosition.load(std::memory_order_relaxed);
        if (write - readPosition.load(std::memory_order_acquire) >= Size) return false;
        values[write % Size] = value;
        writePosition.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto read = readPosition.load(std::memory_order_relaxed);
        if (read == writePosition.load(std::memory_order_acquire)) return false;
        value = values[read % Size];
        readPosition.store(read + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<T, Size> values {};
    std::atomic<std::uint64_t> readPosition { 0 }, writePosition { 0 };
};

struct PatternEvent {
    double phase = 0;
    int pad = 0;
    float semitones = 0, velocity = 1;
    WorkstationId noteId = 0;
    bool noteOn = false;
    int cycleOffset = 0;
};
struct PreparedPattern {
    WorkstationId id = 0;
    double length = 16;
    std::vector<PatternEvent> events;
    std::uint64_t fingerprint = 0;
};
struct PreparedAsset {
    AudioAsset metadata;
    std::shared_ptr<const AudioData> audio;
};
struct RuntimeBank {
    WorkstationState state;
    std::array<PreparedPattern, 16> patterns;
    std::array<std::vector<ArrangementClip>, 8> arrangement;
    std::vector<PreparedAsset> assets;
    std::uint64_t revision = 0;
};
} // namespace

struct WorkstationRuntime::Impl {
    std::mutex publicationMutex, producerMutex;
    WorkstationState lastState;
    std::unordered_map<WorkstationId, std::shared_ptr<const AudioData>> audioAssets;
    std::vector<std::unique_ptr<RuntimeBank>> banks;
    std::atomic<const RuntimeBank*> published { nullptr }, hazard { nullptr };
    const RuntimeBank* current = nullptr;
    std::uint64_t revision = 0, appliedRevision = 0;
    SpscQueue<WorkstationControl, commandCapacity> commands;
    SpscQueue<RecordedWorkstationNote, recordingCapacity> recorded;
    std::atomic<bool> emergencyStop { false };
    double sampleRate = 44100, beat = 0, recordingBeatClock = 0;
    std::uint64_t frame = 0, laneSerial = 0;
    bool isEnabled = false, playing = false, recording = false;
    bool pendingStopAll = false;
    WorkstationPlayMode mode = WorkstationPlayMode::Arrangement;
    int recordPattern = 0, recordTrack = 0;
    std::atomic<bool> enabledStatus { false }, playingStatus { false }, recordingStatus { false }, overflowStatus { false };
    std::atomic<double> beatStatus { 0 };
    std::atomic<std::uint64_t> frameStatus { 0 };
    std::atomic<int> modeStatus { static_cast<int>(WorkstationPlayMode::Arrangement) };
    std::array<std::atomic<int>, 8> activeStatus, pendingStatus;
    std::array<std::atomic<float>, 8> peaks {};
    struct Lane {
        int activeScene = -1, pendingScene = -1;
        bool pendingStop = false;
        double pendingBeat = 0, launchOrigin = 0;
        ClipKind kind = ClipKind::Empty;
        WorkstationId contentId = 0, arrangementId = 0;
        bool loop = true;
        double origin = 0, offset = 0, endBeat = std::numeric_limits<double>::infinity();
        float gain = 1;
        std::uint64_t serial = 0;
        std::uint64_t contentFingerprint = 0;
        std::size_t eventIndex = 0;
        std::int64_t cycle = 0;
        double nextEventBeat = std::numeric_limits<double>::infinity();
        double nextArrangementChange = -1;
        bool dirty = true;
        const PreparedPattern* pattern = nullptr;
        const PreparedAsset* asset = nullptr;
    };
    std::array<Lane, 8> lanes;
    struct OpenNote {
        std::uint64_t id = 0;
        int pattern = 0, track = 0, pad = 0;
        double onset = 0, phase = 0, length = 16;
        float velocity = 1, semitones = 0;
    };
    std::array<OpenNote, 128> openNotes {};

    Impl() {
        for (auto& status : activeStatus) status.store(-1);
        for (auto& status : pendingStatus) status.store(-1);
    }
    void publish() {
        auto bank = std::make_unique<RuntimeBank>();
        bank->state = lastState;
        bank->revision = ++revision;
        for (std::size_t index = 0; index < 16; ++index) {
            const auto& source = lastState.patterns[index];
            auto& pattern = bank->patterns[index];
            pattern.id = source.id; pattern.length = source.lengthBeats;
            pattern.fingerprint = 1469598103934665603ull;
            const auto hash = [&](std::uint64_t value) { pattern.fingerprint = (pattern.fingerprint ^ value) * 1099511628211ull; };
            hash(std::bit_cast<std::uint64_t>(source.lengthBeats));
            pattern.events.reserve(source.notes.size() * 2);
            for (const auto& note : source.notes) {
                hash(note.id); hash(static_cast<std::uint64_t>(note.pad));
                hash(std::bit_cast<std::uint64_t>(note.beat)); hash(std::bit_cast<std::uint64_t>(note.duration));
                hash(std::bit_cast<std::uint32_t>(note.velocity)); hash(std::bit_cast<std::uint32_t>(note.semitones));
                pattern.events.push_back({ note.beat, note.pad, note.semitones, note.velocity, note.id, true, 0 });
                const double end = note.beat + note.duration;
                pattern.events.push_back({ wrapBeat(end, 0, pattern.length), note.pad, note.semitones, note.velocity,
                                           note.id, false, end >= pattern.length - epsilon ? -1 : 0 });
            }
            std::stable_sort(pattern.events.begin(), pattern.events.end(), [](const auto& a, const auto& b) {
                return a.phase == b.phase ? a.noteOn < b.noteOn : a.phase < b.phase;
            });
        }
        for (const auto& clip : lastState.arrangement) bank->arrangement[static_cast<std::size_t>(clip.track)].push_back(clip);
        for (auto& track : bank->arrangement)
            std::stable_sort(track.begin(), track.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        bank->assets.reserve(lastState.audioAssets.size());
        for (const auto& asset : lastState.audioAssets) {
            const auto found = audioAssets.find(asset.id);
            bank->assets.push_back({ asset, found == audioAssets.end() ? nullptr : found->second });
        }
        const auto* pointer = bank.get();
        banks.push_back(std::move(bank));
        published.store(pointer);
        banks.erase(std::remove_if(banks.begin(), banks.end(), [&](const auto& old) {
            return old.get() != pointer && old.get() != hazard.load();
        }), banks.end());
    }
    void enqueue(WorkstationControl command) {
        const std::lock_guard<std::mutex> lock(producerMutex);
        if (!commands.push(command)) {
            overflowStatus.store(true);
            if (command.type == WorkstationControl::Type::Stop) emergencyStop.store(true);
        }
    }
    const PreparedPattern* pattern(WorkstationId id) const {
        if (current) for (const auto& candidate : current->patterns) if (candidate.id == id) return &candidate;
        return nullptr;
    }
    const PreparedAsset* asset(WorkstationId id) const {
        if (current) for (const auto& candidate : current->assets) if (candidate.metadata.id == id) return &candidate;
        return nullptr;
    }
    double step() const { return current ? current->state.tempo / (60.0 * sampleRate) : 120.0 / (60.0 * sampleRate); }
    void updateNextEvent(Lane& lane) {
        if (!lane.pattern || lane.pattern->events.empty()) { lane.nextEventBeat = std::numeric_limits<double>::infinity(); return; }
        lane.nextEventBeat = lane.origin - lane.offset + static_cast<double>(lane.cycle) * lane.pattern->length
                            + lane.pattern->events[lane.eventIndex].phase;
        // Wrapped note-offs from the first iteration remain due in cycle one.
        if (!lane.loop && (lane.cycle < 0 || lane.cycle > 1)) lane.nextEventBeat = std::numeric_limits<double>::infinity();
    }
    void initialiseCursor(Lane& lane) {
        lane.pattern = lane.kind == ClipKind::Pattern ? pattern(lane.contentId) : nullptr;
        lane.asset = lane.kind == ClipKind::Audio ? asset(lane.contentId) : nullptr;
        lane.nextEventBeat = std::numeric_limits<double>::infinity();
        if (!lane.pattern || lane.pattern->events.empty()) return;
        const double local = beat - lane.origin + lane.offset;
        lane.cycle = static_cast<std::int64_t>(std::floor(local / lane.pattern->length));
        const double phase = wrapBeat(local, 0, lane.pattern->length);
        lane.eventIndex = static_cast<std::size_t>(std::lower_bound(lane.pattern->events.begin(), lane.pattern->events.end(), phase - step() * .5,
            [](const PatternEvent& event, double value) { return event.phase < value; }) - lane.pattern->events.begin());
        if (lane.eventIndex == lane.pattern->events.size()) { lane.eventIndex = 0; ++lane.cycle; }
        updateNextEvent(lane);
    }
    void emitStop(int track, WorkstationRuntime::EventCallback callback, void* context) {
        if (callback) callback(context, { track, -1, 0, 0, false, 0 });
    }
    void chooseLane(int track, WorkstationRuntime::EventCallback callback, void* context) {
        auto& lane = lanes[static_cast<std::size_t>(track)];
        if (!current) return;
        ClipKind kind = ClipKind::Empty;
        WorkstationId content = 0, arrangement = 0;
        double origin = 0, offset = 0, end = std::numeric_limits<double>::infinity();
        bool loop = true; float gain = 1;
        if (lane.activeScene >= 0) {
            const auto& slot = current->state.launchSlots[static_cast<std::size_t>(track * 4 + lane.activeScene)];
            kind = slot.kind; content = slot.contentId; origin = lane.launchOrigin; loop = slot.loop;
        } else if (mode == WorkstationPlayMode::Pattern && track == current->state.selectedTrack) {
            kind = ClipKind::Pattern; content = current->patterns[static_cast<std::size_t>(current->state.selectedPattern)].id;
        } else if (mode == WorkstationPlayMode::Arrangement) {
            const auto& clips = current->arrangement[static_cast<std::size_t>(track)];
            lane.nextArrangementChange = std::numeric_limits<double>::infinity();
            const auto upper = std::upper_bound(clips.begin(), clips.end(), beat + step() * .5,
                [](double value, const ArrangementClip& clip) { return value < clip.startBeat; });
            if (upper != clips.end()) lane.nextArrangementChange = upper->startBeat;
            for (auto it = upper; it != clips.begin();) {
                --it;
                if (beat < it->startBeat + it->lengthBeats - step() * .5) {
                    kind = it->kind; content = it->contentId; origin = it->startBeat; offset = it->offsetBeats;
                    end = it->startBeat + it->lengthBeats; loop = it->loop; gain = it->gain; arrangement = it->id;
                    lane.nextArrangementChange = std::min(lane.nextArrangementChange, end);
                    break;
                }
            }
        }
        const auto* selectedPattern = kind == ClipKind::Pattern ? pattern(content) : nullptr;
        const auto fingerprint = selectedPattern ? selectedPattern->fingerprint : 0;
        const bool changed = kind != lane.kind || content != lane.contentId || arrangement != lane.arrangementId
            || origin != lane.origin || offset != lane.offset || loop != lane.loop || fingerprint != lane.contentFingerprint;
        if (changed) {
            emitStop(track, callback, context);
            lane.kind = kind; lane.contentId = content; lane.arrangementId = arrangement;
            lane.origin = origin; lane.offset = offset; lane.loop = loop; lane.endBeat = end; lane.gain = gain;
            lane.contentFingerprint = fingerprint;
            lane.serial = ++laneSerial;
        } else { lane.endBeat = end; lane.gain = gain; }
        if (changed || lane.dirty) initialiseCursor(lane);
        lane.dirty = false;
    }
    void emitRecorded(OpenNote& note) {
        if (note.id == 0) return;
        RecordedWorkstationNote result;
        result.patternIndex = note.pattern; result.track = note.track;
        result.note.pad = note.pad; result.note.semitones = note.semitones; result.note.velocity = note.velocity;
        const double quantum = current ? current->state.recordQuantizeBeats : 0;
        result.note.beat = wrapBeat(quantizeBeat(note.phase, quantum), 0, note.length);
        const double rawDuration = std::max(step(), recordingBeatClock - note.onset);
        const double duration = quantum > 0 ? std::max(quantum, quantizeBeat(rawDuration, quantum)) : rawDuration;
        result.note.duration = std::clamp(duration, step(), note.length);
        if (!recorded.push(result)) { overflowStatus.store(true); recording = false; recordingStatus.store(false); }
        note = {};
    }
    void finishNotes() { for (auto& note : openNotes) emitRecorded(note); }
    void control(const WorkstationControl& command) {
        switch (command.type) {
            case WorkstationControl::Type::Enable:
                isEnabled = command.flag;
                if (!isEnabled) { playing = recording = false; finishNotes(); pendingStopAll = true; }
                break;
            case WorkstationControl::Type::Play:
                isEnabled = true; playing = true; mode = command.mode;
                for (auto& lane : lanes) { lane.dirty = true; lane.nextArrangementChange = -1; }
                break;
            case WorkstationControl::Type::Stop:
                playing = false; recording = false; finishNotes(); pendingStopAll = true;
                for (auto& lane : lanes) { lane.activeScene = lane.pendingScene = -1; lane.pendingStop = false; lane.kind = ClipKind::Empty; lane.dirty = true; }
                break;
            case WorkstationControl::Type::Seek:
                finishNotes();
                beat = std::clamp(command.beat, 0.0, workstationMaxBeat); pendingStopAll = true;
                for (auto& lane : lanes) { lane.dirty = true; lane.nextArrangementChange = -1; }
                break;
            case WorkstationControl::Type::Record:
                if (command.flag && current) {
                    isEnabled = true; recordPattern = current->state.selectedPattern; recordTrack = current->state.selectedTrack;
                    if (!playing) { playing = true; mode = WorkstationPlayMode::Pattern; }
                    recording = true;
                } else { finishNotes(); recording = false; }
                break;
            case WorkstationControl::Type::Launch:
                if (command.track >= 0 && command.track < 8 && command.scene >= 0 && command.scene < 4 && current) {
                    auto& lane = lanes[static_cast<std::size_t>(command.track)];
                    if (!playing) { playing = true; mode = WorkstationPlayMode::Performance; }
                    isEnabled = true;
                    lane.pendingScene = command.scene; lane.pendingStop = false;
                    lane.pendingBeat = nextLaunchBeat(beat, current->state.launchQuantizeBeats);
                }
                break;
            case WorkstationControl::Type::StopTrack:
                if (command.track >= 0 && command.track < 8) {
                    auto& lane = lanes[static_cast<std::size_t>(command.track)]; lane.pendingStop = true;
                    lane.pendingBeat = beat; lane.pendingScene = -1;
                }
                break;
        }
        enabledStatus.store(isEnabled); playingStatus.store(playing); recordingStatus.store(recording);
        modeStatus.store(static_cast<int>(mode));
    }
};

WorkstationRuntime::WorkstationRuntime() : impl(std::make_unique<Impl>()) {
    const std::lock_guard<std::mutex> lock(impl->publicationMutex); impl->publish();
}
WorkstationRuntime::~WorkstationRuntime() = default;
void WorkstationRuntime::prepare(double sampleRate, std::size_t) {
    impl->sampleRate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 44100;
    impl->pendingStopAll = true;
}
void WorkstationRuntime::setState(const WorkstationState& state) {
    validateWorkstation(state);
    const std::lock_guard<std::mutex> lock(impl->publicationMutex);
    for (const auto& previous : impl->lastState.audioAssets) {
        const auto* next = findAudioAsset(state, previous.id);
        if (!next || next->filePath != previous.filePath) impl->audioAssets.erase(previous.id);
    }
    impl->lastState = state; impl->publish();
}
void WorkstationRuntime::setAudioAsset(WorkstationId id, std::shared_ptr<const AudioData> audio) {
    if (id == 0 || (audio && (!audio->valid() || audio->channels.size() > 2))) throw std::invalid_argument("Audio clips require valid mono or stereo PCM and a nonzero asset ID.");
    const std::lock_guard<std::mutex> lock(impl->publicationMutex);
    if (!impl->audioAssets.contains(id) && impl->audioAssets.size() >= workstationMaxAssets) throw std::invalid_argument("The workstation audio asset limit has been reached.");
    impl->audioAssets[id] = std::move(audio); impl->publish();
}
void WorkstationRuntime::removeAudioAsset(WorkstationId id) {
    const std::lock_guard<std::mutex> lock(impl->publicationMutex); impl->audioAssets.erase(id); impl->publish();
}
void WorkstationRuntime::setEnabled(bool value) { WorkstationControl c; c.type = WorkstationControl::Type::Enable; c.flag = value; impl->enqueue(c); }
void WorkstationRuntime::play(WorkstationPlayMode mode) { WorkstationControl c; c.type = WorkstationControl::Type::Play; c.mode = mode; impl->enqueue(c); }
void WorkstationRuntime::stop() { WorkstationControl c; c.type = WorkstationControl::Type::Stop; impl->enqueue(c); }
void WorkstationRuntime::seek(double beat) { WorkstationControl c; c.type = WorkstationControl::Type::Seek; c.beat = std::isfinite(beat) ? beat : 0; impl->enqueue(c); }
void WorkstationRuntime::setRecording(bool value) { WorkstationControl c; c.type = WorkstationControl::Type::Record; c.flag = value; impl->enqueue(c); }
void WorkstationRuntime::launch(int track, int scene) { WorkstationControl c; c.type = WorkstationControl::Type::Launch; c.track = track; c.scene = scene; impl->enqueue(c); }
void WorkstationRuntime::stopTrack(int track) { WorkstationControl c; c.type = WorkstationControl::Type::StopTrack; c.track = track; impl->enqueue(c); }
void WorkstationRuntime::applyControl(const WorkstationControl& control) noexcept { impl->control(control); }
void WorkstationRuntime::beginBlock() noexcept {
    const RuntimeBank* bank;
    do { bank = impl->published.load(); impl->hazard.store(bank); } while (bank != impl->published.load());
    impl->current = bank;
    if (bank && bank->revision != impl->appliedRevision) {
        impl->appliedRevision = bank->revision;
        for (auto& lane : impl->lanes) { lane.dirty = true; lane.nextArrangementChange = -1; }
    }
    WorkstationControl control;
    while (impl->commands.pop(control)) impl->control(control);
    if (impl->emergencyStop.exchange(false)) { control.type = WorkstationControl::Type::Stop; impl->control(control); }
}
void WorkstationRuntime::tick(EventCallback callback, void* context, float* clipLeft, float* clipRight) noexcept {
    for (int track = 0; track < 8; ++track) { clipLeft[track] = 0; clipRight[track] = 0; }
    if (impl->pendingStopAll) {
        for (int track = 0; track < 8; ++track) impl->emitStop(track, callback, context);
        impl->pendingStopAll = false;
    }
    if (!impl->isEnabled || !impl->playing || !impl->current) return;
    const auto& state = impl->current->state;
    if (state.arrangementLoop && impl->mode == WorkstationPlayMode::Arrangement && impl->beat >= state.loopEndBeat - impl->step() * .5) {
        impl->beat = state.loopStartBeat;
        for (int track = 0; track < 8; ++track) { impl->emitStop(track, callback, context); impl->lanes[static_cast<std::size_t>(track)].dirty = true; impl->lanes[static_cast<std::size_t>(track)].nextArrangementChange = -1; }
    }
    int emitted = 0;
    for (int track = 0; track < 8; ++track) {
        auto& lane = impl->lanes[static_cast<std::size_t>(track)];
        if ((lane.pendingScene >= 0 || lane.pendingStop) && impl->beat >= lane.pendingBeat - impl->step() * .5) {
            impl->emitStop(track, callback, context);
            lane.activeScene = lane.pendingStop ? -1 : lane.pendingScene;
            lane.pendingScene = -1; lane.pendingStop = false; lane.launchOrigin = lane.pendingBeat;
            lane.dirty = true; lane.nextArrangementChange = -1;
        }
        if (lane.dirty || (impl->mode == WorkstationPlayMode::Arrangement && lane.activeScene < 0 && impl->beat >= lane.nextArrangementChange - impl->step() * .5))
            impl->chooseLane(track, callback, context);
        if (lane.kind == ClipKind::Pattern && lane.pattern && !lane.pattern->events.empty()) {
            while (lane.nextEventBeat <= impl->beat + impl->step() * .5 && lane.nextEventBeat < lane.endBeat - impl->step() * .5) {
                const auto& event = lane.pattern->events[lane.eventIndex];
                const auto eventCycle = lane.cycle + event.cycleOffset;
                if (eventCycle >= 0 && (lane.loop || eventCycle == 0)) {
                    if (++emitted > 512) { impl->overflowStatus.store(true); impl->playing = false; impl->pendingStopAll = true; break; }
                    const auto token = ((lane.serial & 0xffffu) << 48) | ((static_cast<std::uint64_t>(eventCycle) & 0xffffu) << 32) | event.noteId;
                    if (callback) callback(context, { track, event.pad, event.semitones, event.velocity * lane.gain, event.noteOn, token });
                }
                if (++lane.eventIndex == lane.pattern->events.size()) { lane.eventIndex = 0; ++lane.cycle; }
                impl->updateNextEvent(lane);
            }
        } else if (lane.kind == ClipKind::Audio && lane.asset && lane.asset->audio) {
            const auto& asset = *lane.asset;
            double local = impl->beat - lane.origin + lane.offset;
            if (local < 0 || (!lane.loop && local >= asset.metadata.lengthBeats)) continue;
            if (lane.loop) local = wrapBeat(local, 0, asset.metadata.lengthBeats);
            const double position = local * asset.audio->sampleRate * 60.0 / asset.metadata.sourceBpm;
            if (position < 0 || position >= static_cast<double>(asset.audio->frames())) continue;
            const auto first = static_cast<std::size_t>(position), next = std::min(first + 1, asset.audio->frames() - 1);
            const float fraction = static_cast<float>(position - static_cast<double>(first));
            const auto& left = asset.audio->channels[0];
            const auto& right = asset.audio->channels[std::min<std::size_t>(1, asset.audio->channels.size() - 1)];
            clipLeft[track] = (left[first] + fraction * (left[next] - left[first])) * lane.gain;
            clipRight[track] = (right[first] + fraction * (right[next] - right[first])) * lane.gain;
        }
    }
}
void WorkstationRuntime::advance() noexcept {
    ++impl->frame;
    if (impl->isEnabled && impl->playing) { const auto step = impl->step(); impl->beat += step; impl->recordingBeatClock += step; }
}
void WorkstationRuntime::endBlock() noexcept {
    impl->beatStatus.store(impl->beat); impl->frameStatus.store(impl->frame);
    impl->enabledStatus.store(impl->isEnabled); impl->playingStatus.store(impl->playing); impl->recordingStatus.store(impl->recording);
    for (std::size_t track = 0; track < 8; ++track) { impl->activeStatus[track].store(impl->lanes[track].activeScene); impl->pendingStatus[track].store(impl->lanes[track].pendingScene); }
    impl->current = nullptr; impl->hazard.store(nullptr);
}
const WorkstationState* WorkstationRuntime::currentState() const noexcept { return impl->current ? &impl->current->state : nullptr; }
WorkstationState WorkstationRuntime::stateForControl() const { const std::lock_guard<std::mutex> lock(impl->publicationMutex); return impl->lastState; }
double WorkstationRuntime::currentBeat() const noexcept { return impl->beat; }
bool WorkstationRuntime::playing() const noexcept { return impl->playing; }
int WorkstationRuntime::selectedTrack() const noexcept { return impl->current ? impl->current->state.selectedTrack : 0; }
bool WorkstationRuntime::enabled() const noexcept { return impl->isEnabled; }
void WorkstationRuntime::setTrackPeak(int track, float peak) noexcept { if (track >= 0 && track < 8) impl->peaks[static_cast<std::size_t>(track)].store(peak); }
WorkstationSnapshot WorkstationRuntime::snapshot() const {
    WorkstationSnapshot state;
    state.enabled = impl->enabledStatus.load(); state.playing = impl->playingStatus.load(); state.recording = impl->recordingStatus.load(); state.eventOverflow = impl->overflowStatus.load();
    state.beat = impl->beatStatus.load(); state.sampleFrame = impl->frameStatus.load(); state.mode = static_cast<WorkstationPlayMode>(impl->modeStatus.load());
    for (std::size_t track = 0; track < 8; ++track) { state.activeScene[track] = impl->activeStatus[track].load(); state.pendingScene[track] = impl->pendingStatus[track].load(); state.trackPeaks[track] = impl->peaks[track].load(); }
    return state;
}
bool WorkstationRuntime::popRecordedNote(RecordedWorkstationNote& note) { return impl->recorded.pop(note); }
void WorkstationRuntime::recordNoteOn(std::uint64_t id, int track, int pad, float semitones, float velocity) noexcept {
    if (!impl->recording || track != impl->recordTrack || !impl->current) return;
    for (auto& open : impl->openNotes) if (open.id == 0) {
        const auto length = impl->current->patterns[static_cast<std::size_t>(impl->recordPattern)].length;
        open = { id, impl->recordPattern, track, pad, impl->recordingBeatClock, wrapBeat(impl->beat, 0, length), length, velocity, semitones };
        return;
    }
    impl->overflowStatus.store(true);
}
void WorkstationRuntime::recordNoteOff(std::uint64_t id) noexcept { for (auto& open : impl->openNotes) if (open.id == id) { impl->emitRecorded(open); return; } }
void WorkstationRuntime::finishRecordingNotes() noexcept { impl->finishNotes(); }
} // namespace samplify
