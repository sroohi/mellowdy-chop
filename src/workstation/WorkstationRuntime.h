#pragma once

#include "WorkstationModel.h"
#include "core/Model.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace samplify {

enum class WorkstationPlayMode { Pattern, Arrangement, Performance };

struct WorkstationSnapshot {
    bool enabled = false;
    bool playing = false;
    bool recording = false;
    bool eventOverflow = false;
    double beat = 0;
    std::uint64_t sampleFrame = 0;
    WorkstationPlayMode mode = WorkstationPlayMode::Arrangement;
    std::array<int, 8> activeScene { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<int, 8> pendingScene { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<float, 8> trackPeaks {};
};

struct RecordedWorkstationNote {
    int patternIndex = 0;
    int track = 0;
    PatternNote note;
};

enum class CaptureError { None, OpenFailed, DiskWriteFailed, Overflow, EmptyRecording };
struct CaptureSnapshot {
    bool recording = false;
    bool finalizing = false;
    bool overflow = false;
    std::uint64_t frames = 0;
    CaptureError error = CaptureError::None;
};

struct WorkstationControl {
    enum class Type { Enable, Play, Stop, Seek, Record, Launch, StopTrack };
    Type type = Type::Stop;
    WorkstationPlayMode mode = WorkstationPlayMode::Arrangement;
    int track = 0, scene = 0;
    double beat = 0;
    bool flag = false;
};

struct ScheduledPadEvent {
    int track = 0;
    int pad = 0;
    float semitones = 0;
    float velocity = 1;
    bool noteOn = true;
    std::uint64_t token = 0;
};

// Sequencer and clip player. Control methods publish immutable state or enqueue
// bounded commands; beginBlock/tick/endBlock run on the audio thread. PCM is
// retained separately from persisted paths. Audio clips use honest rate playback
// with a duration/tempo-linked rate, rather than claiming pitch-preserving warp.
class WorkstationRuntime {
public:
    using EventCallback = void (*)(void*, const ScheduledPadEvent&);
    WorkstationRuntime();
    ~WorkstationRuntime();
    WorkstationRuntime(const WorkstationRuntime&) = delete;
    WorkstationRuntime& operator=(const WorkstationRuntime&) = delete;
    void prepare(double sampleRate, std::size_t maximumBlockSize);
    void setState(const WorkstationState&);
    void setAudioAsset(WorkstationId, std::shared_ptr<const AudioData>);
    void removeAudioAsset(WorkstationId);
    void setEnabled(bool);
    void play(WorkstationPlayMode);
    void stop();
    void seek(double beat);
    void setRecording(bool);
    void launch(int track, int scene);
    void stopTrack(int track);
    WorkstationSnapshot snapshot() const;
    bool popRecordedNote(RecordedWorkstationNote&);
    // IDs correlate actual sampler note-on/off applications, including same-pad
    // keyboard overlaps. Scheduling playback never records itself as new input.
    void recordNoteOn(std::uint64_t id, int track, int pad, float semitones, float velocity) noexcept;
    void recordNoteOff(std::uint64_t id) noexcept;
    void finishRecordingNotes() noexcept;
    void beginBlock() noexcept;
    void applyControl(const WorkstationControl&) noexcept;
    void tick(EventCallback, void*, float* clipLeft, float* clipRight) noexcept;
    void advance() noexcept;
    void endBlock() noexcept;
    const WorkstationState* currentState() const noexcept;
    WorkstationState stateForControl() const;
    double currentBeat() const noexcept;
    bool playing() const noexcept;
    int selectedTrack() const noexcept;
    bool enabled() const noexcept;
    void setTrackPeak(int track, float peak) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace samplify
