#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/DSP.h"
#include "workstation/WorkstationRuntime.h"
#include <atomic>
#include <memory>

namespace samplify {

class PluginHost;

struct CueAssignment {
    int pad = -1;
    std::size_t frame = 0;
    float velocity = 1;
    float semitoneOffset = 0;
    bool held = true;
};

struct PlaybackSnapshot {
    std::size_t sourceFrame = 0;
    bool sourcePlaying = false;
    std::uint32_t activeCueMask = 0;
    int lastTriggeredPad = -1;
    std::uint64_t triggerSerial = 0;
    int pendingCuePad = -1;
    std::size_t pendingCueFrame = 0;
    std::uint64_t pendingCueSerial = 0;
    float pendingCueVelocity = 1;
    float pendingCueSemitoneOffset = 0;
    bool pendingCueHeld = true;
    float peak = 0;
};

class AudioEngine final : public juce::AudioSource, public juce::MidiInputCallback {
public:
    AudioEngine();
    ~AudioEngine() override;
    juce::String initialise();
    static AudioData readAudio(const juce::File& file);
    static void writeAudio(const juce::File& file, const AudioData& data);
    static void writeFloatAudio(const juce::File& file, const AudioData& data);
    // Source/stem assignment and cache preparation run off the audio callback.
    void setSource(std::shared_ptr<const AudioData> source);
    void setStem(int index, std::shared_ptr<const AudioData> stem);
    void clearStems();
    void preparePads(const Project& project);
    void triggerPad(int index, float semitoneOffset = 0, float velocity = 1);
    void releasePad(int index);
    void releasePad(int index, float semitoneOffset);
    void stop();
    void playSource();
    void pauseSource();
    void seekSource(std::size_t frame);
    PlaybackSnapshot playbackSnapshot() const;
    bool popPendingCue(CueAssignment& assignment);
    void setLiveProject(const Project& project);
    void setMetronome(bool enabled);
    void setMasterGain(float gain);
    void setMono(bool mono);
    int activeVoiceCount() const;
    float peakLevel() const;
    juce::String deviceName() const;
    juce::AudioDeviceManager& deviceManager() { return devices; }
    void setWorkstationState(const WorkstationState&, bool restorePlugins = false);
    void setWorkstationEnabled(bool enabled);
    void playWorkstation(WorkstationPlayMode mode = WorkstationPlayMode::Arrangement);
    void stopWorkstation();
    void seekWorkstation(double beat);
    void setPatternRecording(bool enabled);
    void launchClip(int track, int scene);
    void stopClipTrack(int track);
    void setAudioAsset(WorkstationId id, std::shared_ptr<const AudioData>);
    void removeAudioAsset(WorkstationId id);
    WorkstationSnapshot workstationSnapshot() const;
    bool popRecordedNote(RecordedWorkstationNote&);
    void startMasterCapture(const juce::File& file);
    void stopMasterCapture();
    CaptureSnapshot captureSnapshot() const;
    // Worker-thread offline freeze. Includes built-in track, send and master FX;
    // third-party plug-ins are intentionally excluded, not cloned unsafely.
    AudioData bouncePattern(int patternIndex, int track) const;
    PluginHost& pluginHost();
    juce::String midiStatus() const;
    void refreshMidiInputs();
    void setKeyboardMode(bool enabled, int selectedPad);
    void learnMidiPad(int padIndex);
    AudioData renderForExport(const Project& project, int padIndex) const;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::AudioDeviceManager devices;
    juce::AudioSourcePlayer player;
};

} // namespace samplify
