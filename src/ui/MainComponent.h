#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "audio/AudioEngine.h"
#include "audio/SessionIO.h"
#include "core/CueEditing.h"
#include "SourceWaveform.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>

namespace samplify {
class CuePad;
class ModeButton;
class StemButton;
class StemSeparator;

// The source player, per-cue strip, Autoset toolbar and 8 x 4 performance grid
// follow the supplied Sample manual. StudioComponent handles the outer workstation.
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropContainer,
                            private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void importAudio();
    void openSession();
    void saveSession();
    void exportSelected();
    void exportAll();
    void showAudioSettings();
    void stopPlayback();
    void loadDemo();
    void setStudioTempo(double bpm);
    void configureStudioRouting(std::function<int()> read, std::function<void(int)> change);
    void refreshStudioRouting();
    AudioEngine& audioEngine() { return engine; }
    const Project& samplerProject() const { return project; }
    std::shared_ptr<const AudioData> samplerSource() const { return source; }
    std::function<int()> onReadStudioTrack;
    std::function<void(int)> onStudioTrackChanged;
    std::function<void(double)> onHostTempoChanged;
    std::function<WorkstationState()> onReadWorkstationState;
    std::function<void(const WorkstationState&)> onRestoreWorkstationState;
    bool isReadyForSmokeTest() const;
    juce::var runWorkflowSmoke();
    void beginStemSmoke();
    bool isStemSmokeReady() const;
    juce::var stemSmokeResult() const;

private:
    class ReferenceLookAndFeel;
    using Completion = std::function<void()>;
    using Work = std::function<Completion(const std::atomic<bool>&)>;
    void timerCallback() override;
    void runTask(const juce::String&, Work);
    void reportError(const juce::String&);
    void loadFile(const juce::File&);
    void loadSource(const juce::File&, std::optional<Session> = {});
    void installSource(std::shared_ptr<const AudioData>, Analysis, Project,
                       const juce::File&, std::array<juce::File, 4>,
                       std::array<std::shared_ptr<const AudioData>, 4>);
    void ejectSource();
    void refreshControls();
    void refreshWaveforms();
    void publishProject(bool refresh = true);
    void syncSelectionToEngine();
    void cycleKeyDisplay();
    void editProject(const std::function<void(Project&)>&);
    void editSelected(const std::function<void(Pad&)>&);
    void pushUndo(const Project&);
    void undo();
    void redo();
    void selectPad(int, const juce::ModifierKeys&, bool trigger);
    void triggerCue(int, float semitoneOffset = 0, float velocity = 1);
    void releaseCue(int, float semitoneOffset = 0);
    void deleteSelection();
    void renameCue(int);
    void renameCueTo(int, const juce::String&);
    void movePad(int, int, bool copy);
    void selectAll();
    void chooseAutoSetMode();
    void runAutoSet(bool shiftDomain = false);
    void shiftSlicer(int);
    void requestStem(int, bool solo);
    void toggleStemMask(int, bool solo);
    void separateStems();
    void updateStemWaveform();
    void toggleGridEdit(bool);
    void gridEdit(const std::function<void(Project&)>&);
    void gridDrag(std::size_t, std::size_t, bool);
    void applyGridBpm(double);
    void tapTempo();
    void exportTo(const juce::File&, bool);
    Session currentSession() const;
    std::uint32_t occupiedMask() const;
    juce::Colour selectionColour() const;
    static juce::String noteName(int);
    static juce::String timeText(double);

    AudioEngine engine;
    std::thread worker;
    std::thread waveformWorker;
    std::atomic<bool> waveformCancelled { false };
    bool waveformWorking = false;
    std::optional<std::array<bool, 4>> displayedStemMask, runningStemMask;
    std::unique_ptr<ReferenceLookAndFeel> theme;
    std::shared_ptr<const AudioData> source;
    Analysis analysis, stemAnalysis;
    Project project;
    juce::File sourceFile;
    std::array<juce::File, 4> stemFiles;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    std::shared_ptr<const AudioData> waveformAudio;
    std::vector<Pad> importedCues;
    std::unique_ptr<StemSeparator> separator;
    std::unique_ptr<juce::FileChooser> chooser;
    SourceWaveform overview { true }, waveform { false };
    std::array<std::unique_ptr<CuePad>, 32> pads;
    std::unique_ptr<ModeButton> holdMode, voiceMode, keyMode;
    std::array<std::unique_ptr<StemButton>, 4> stemButtons;
    std::uint32_t selection = 1;
    int selectedPad = 0;
    std::map<int, std::pair<int, float>> heldKeys;
    std::vector<Project> undoStack, redoStack;
    std::optional<Project> gestureBefore;
    Project gridBackup;
    bool refreshing = false, busy = false, gridEditing = false, gridDirty = false, optionsOpen = false;
    bool stemsReady = false;
    std::atomic<bool> cancelled { false };
    std::atomic<double> taskProgress { 0 };
    double progressDisplay = 0;
    std::unique_ptr<juce::ProgressBar> progressBar;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::String status, sourceName, audioWarning;
    PlaybackSnapshot playback;
    std::uint64_t lastObservedTrigger = 0;
    ChopMode autoMode = ChopMode::Transients;
    std::uint32_t randomSeed = 42;
    std::size_t slicerStart = 0;
    double sliceBeats = 1;
    int keyShiftStep = 1;
    double hostBpm = 120;
    int keyDisplayFormat = 0;
    bool keyCycleGesture = false;
    double keyCycleOriginalPitch = 0;
    std::vector<double> tapTimes;
    int stemSmokeStage = 0;
    Project stemSmokeOriginal;
    std::uint32_t stemSmokeSelection = 1;
    int stemSmokeSelectedPad = 0;
    juce::var stemSmokeChecks;

    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton velocityButton { "V" }, quantizeButton { "Q" }, settingsButton { "Audio" };
    juce::TextButton playButton { ">" }, ejectButton { "Eject" }, syncButton { "SYNC" };
    juce::TextButton bpmDouble { "x 2" }, bpmHalf { "1/2" }, pitchTimeButton { "P / T" };
    juce::TextButton loadButton { "LOAD AUDIO FILE" }, cancelButton { "CANCEL" };
    juce::TextButton reverseButton { "Reverse" }, favoriteButton { "Favorite" }, optionsButton { "Options" };
    juce::TextButton speedDouble { "x 2" }, speedHalf { "1/2" };
    juce::TextButton autoSetButton { "Find Samples" }, autoModeButton { "v" };
    juce::TextButton selectAllButton { "Select All" }, deleteButton { "Delete" }, keyboardButton { "Keyboard" };
    juce::TextButton sliceLeft { "<" }, sliceRight { ">" };
    juce::ComboBox sliceSize, keyStep, outputRouting, glideMode;
    juce::Label trackTitle, trackInfo, levelLabel, filterLabel, attackLabel, releaseLabel;
    juce::Label speedLabel, cuePitchLabel, optionsInfo, outputLabel, glideLabel, statusLabel;
    juce::Slider globalPitch, globalDetune, currentBpm, level, filter, attack, release, speed, cuePitch, glide;

    juce::Component gridPanel;
    juce::Slider gridBpm;
    juce::TextButton gridSave { "SAVE" }, gridCancel { "CANCEL" }, gridTap { "TAP" };
    juce::TextButton gridDouble { "x 2" }, gridHalf { "1/2" }, metronome { "CLICK" };
    juce::TextButton gridStart { "SET GRID START" }, gridAdjustLeft { "<" }, gridAdjustRight { ">" };
    juce::TextButton gridSlipLeft { "<" }, gridSlipRight { ">" }, gridSet { "SET" }, gridClear { "CLEAR" }, gridClearAll { "CLEAR ALL" };
    bool metronomeOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
} // namespace samplify
