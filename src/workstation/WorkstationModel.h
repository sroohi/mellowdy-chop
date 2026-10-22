#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace samplify {

using WorkstationId = std::uint32_t;
inline constexpr std::size_t workstationTrackCount = 8;
inline constexpr std::size_t workstationPatternCount = 16;
inline constexpr std::size_t workstationSceneCount = 4;
inline constexpr std::size_t workstationFxCount = 4;
inline constexpr std::size_t workstationMaxNotes = 4096; // Per pattern.
inline constexpr std::size_t workstationMaxClips = 4096;
inline constexpr std::size_t workstationMaxAssets = 256;
inline constexpr std::size_t workstationMaxPluginStateBytes = 4 * 1024 * 1024;
inline constexpr std::size_t workstationMaxSessionBytes = 64 * 1024 * 1024;
inline constexpr double workstationMaxBeat = 1048576.0;

enum class CrossfadeAssignment { None, A, B };
enum class ClipKind { Empty, Pattern, Audio };
enum class FxType {
    None, Equalizer, Compressor, Saturator, BitCrusher, Chorus, Delay,
    Reverb, Filter, Tremolo, Limiter, StereoWidth, Transient, Flanger, Phaser, Tape
};

struct FxSlot {
    FxType type = FxType::None;
    bool bypass = false;
    float mix = 1;
    std::array<float, 6> parameters {}; // Normalized [0, 1]; DSP metadata supplies units.
};

// A JUCE PluginDescription XML string preserves the real host descriptor.
// Runtime instances and PCM never belong in the persisted domain model.
struct HostedPluginSlot {
    std::string name;
    std::string format;
    std::string identifier;
    std::string manufacturer;
    std::string xmlDescription;
    std::string stateBase64;
    bool bypass = false;
    bool empty() const { return xmlDescription.empty(); }
};

struct WorkstationTrack {
    std::string name;
    float gainDb = 0;
    float pan = 0; // -1 left, +1 right.
    bool mute = false;
    bool solo = false;
    float sendA = 0;
    float sendB = 0;
    CrossfadeAssignment crossfadeAssignment = CrossfadeAssignment::None;
    std::array<FxSlot, workstationFxCount> effects {};
    std::array<HostedPluginSlot, workstationFxCount> plugins {};
};

struct PatternNote {
    WorkstationId id = 0;
    int pad = 0;
    double beat = 0;
    double duration = .25;
    float velocity = 1;
    float semitones = 0;
};

struct Pattern {
    WorkstationId id = 0;
    std::string name;
    double lengthBeats = 16;
    std::vector<PatternNote> notes;
};

struct ArrangementClip {
    WorkstationId id = 0;
    int track = 0;
    ClipKind kind = ClipKind::Pattern;
    WorkstationId contentId = 0;
    double startBeat = 0;
    double lengthBeats = 16;
    double offsetBeats = 0;
    bool loop = true;
    float gain = 1;
};

struct LaunchSlot {
    ClipKind kind = ClipKind::Empty;
    WorkstationId contentId = 0;
    bool loop = true;
};

struct AudioAsset {
    WorkstationId id = 0;
    std::string name;
    std::string filePath; // Absolute in memory; SessionIO stores portable relative paths.
    double sourceBpm = 120;
    double lengthBeats = 16;
};

struct WorkstationState {
    WorkstationState();
    std::array<WorkstationTrack, workstationTrackCount> tracks;
    std::array<Pattern, workstationPatternCount> patterns;
    std::vector<ArrangementClip> arrangement;
    // Track-major: launchSlots[track * workstationSceneCount + scene].
    std::array<LaunchSlot, workstationTrackCount * workstationSceneCount> launchSlots {};
    std::vector<AudioAsset> audioAssets;
    std::array<FxSlot, workstationFxCount> masterEffects {};
    std::array<HostedPluginSlot, workstationFxCount> masterPlugins {};
    std::array<FxSlot, workstationFxCount> sendAEffects {};
    std::array<FxSlot, workstationFxCount> sendBEffects {};
    float masterGainDb = 0;
    float crossfader = .5f;
    double tempo = 120;
    double recordQuantizeBeats = .25; // Zero disables quantization.
    double launchQuantizeBeats = 4; // Zero launches immediately.
    bool arrangementLoop = false;
    double loopStartBeat = 0;
    double loopEndBeat = 64;
    int selectedPattern = 0;
    int selectedTrack = 0;
};

// Strict validation never silently discards malformed persisted user data.
// Editing helpers reject invalid input without changing the state.
void validateWorkstation(const WorkstationState&);
// Validate first, then canonicalize ordering without dropping user data.
void normalizeWorkstation(WorkstationState&);
double quantizeBeat(double beat, double quantum);
double wrapBeat(double beat, double start, double end);
double nextLaunchBeat(double beat, double quantum);
const Pattern* findPattern(const WorkstationState&, WorkstationId);
Pattern* findPattern(WorkstationState&, WorkstationId);
const AudioAsset* findAudioAsset(const WorkstationState&, WorkstationId);
bool addPatternNote(WorkstationState&, int patternIndex, PatternNote);
bool updatePatternNote(WorkstationState&, int patternIndex, WorkstationId, PatternNote);
bool removePatternNote(WorkstationState&, int patternIndex, WorkstationId);
bool resizePattern(WorkstationState&, int patternIndex, double lengthBeats);
bool addArrangementClip(WorkstationState&, ArrangementClip);
bool updateArrangementClip(WorkstationState&, WorkstationId, ArrangementClip);
bool removeArrangementClip(WorkstationState&, WorkstationId);
bool setLaunchSlot(WorkstationState&, int track, int scene, LaunchSlot);
bool addAudioAsset(WorkstationState&, AudioAsset);
bool removeAudioAsset(WorkstationState&, WorkstationId);

} // namespace samplify
