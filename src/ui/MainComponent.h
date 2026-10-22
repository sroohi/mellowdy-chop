#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "audio/AudioEngine.h"
#include "audio/SessionIO.h"
#include <functional>
#include <optional>
#include <map>
#include <thread>

namespace samplify {

class WaveformView;
class SamplerPadButton;

class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    void importAudio();
    void openSession();
    void saveSession();
    void exportSelected();
    void exportAll();
    void showAudioSettings();
    void stopPlayback();
    void loadDemo();
    bool isReadyForSmokeTest() const;

private:
    class StudioLookAndFeel;
    void timerCallback() override;
    void setBusy(bool, const juce::String& message = {});
    void runTask(juce::String progress, std::function<std::function<void()>()> work);
    void reportError(const juce::String&);
    void loadFile(const juce::File&);
    void loadSource(const juce::File&, const std::optional<Session>& restored = std::nullopt);
    void importStem(int);
    void rebuildPads(ChopMode);
    void rebuildCache();
    void refreshAll();
    void refreshInspector();
    void commitInspector();
    void selectPad(int, bool trigger = false, float semitones = 0);
    void exportTo(const juce::File&, bool all);
    Session currentSession() const;
    static juce::String timeText(double);

    AudioEngine engine;
    std::thread worker;
    std::unique_ptr<StudioLookAndFeel> theme;
    std::shared_ptr<const AudioData> source;
    Analysis analysis;
    Project project;
    juce::File sourceFile;
    std::array<juce::File, 4> stemFiles;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<WaveformView> waveform;
    std::array<std::unique_ptr<SamplerPadButton>, 32> pads;
    int selectedPad = 0;
    bool busy = false;
    bool refreshing = false;
    bool keyboardMode = false;
    // Remember the original target even if selection or keyboard mode changes.
    std::map<int, std::pair<int, float>> heldKeys;
    juce::String sourceName = "No sample loaded";
    juce::String status = "Ready";
    juce::String audioWarning;
    juce::String lastMidiStatus;
    float peak = 0;
    std::uint32_t randomSeed = 42;

    juce::TextButton importButton { "Import audio" }, demoButton { "Load demo" };
    juce::TextButton openButton { "Open" }, saveButton { "Save session" };
    juce::TextButton settingsButton { "Audio / MIDI" }, stopButton { "Stop" };
    juce::TextButton findButton { "Find samples" }, gridButton { "Set slicer" }, randomButton { "Set random" };
    juce::TextButton exportButton { "Export pad" }, exportAllButton { "Export all" };
    juce::TextButton learnButton { "Learn MIDI note" };
    juce::TextButton clearStemsButton { "Clear stems" };
    std::array<juce::TextButton, 4> stemImportButtons;
    std::array<juce::ToggleButton, 4> stemMuteButtons;
    juce::ComboBox padCountBox, divisionBox, sourceKeyBox, targetKeyBox;
    juce::ToggleButton tempoSync { "Tempo sync" }, keySync { "Key sync" }, monoToggle { "Mono" };
    juce::ToggleButton quantizeToggle { "Quantize" }, keyboardToggle { "Keyboard" }, reverseToggle { "Reverse" };
    juce::Label sourceTitle, sourceDetails, estimatedKeyLabel, padTitle, padRangeLabel;
    juce::Label sourceBpmLabel, targetBpmLabel;
    juce::Slider sourceBpm, targetBpm, master;
    juce::Slider gain, pitch, detune, stretch, filter, attack, release, cueIn, cueOut;
    juce::Label gainLabel, pitchLabel, detuneLabel, stretchLabel, filterLabel, attackLabel, releaseLabel;
    juce::Label cueInLabel, cueOutLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace samplify
