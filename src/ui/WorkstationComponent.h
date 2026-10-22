#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "audio/AudioEngine.h"
#include "dsp/Effects.h"
#include "HardwareLookAndFeel.h"
#include "workstation/WorkstationModel.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>

namespace samplify {
class ArrangementCanvas;
class PatternCanvas;
class LaunchCell;

class WorkstationComponent final : public juce::Component, private juce::Timer {
public:
    enum class Page { Arrange, Patterns, FX, Perform };
    explicit WorkstationComponent(AudioEngine&);
    ~WorkstationComponent() override;
    const WorkstationState& getState() const { return state; }
    WorkstationState captureState();
    void setTempo(double);
    void setSelectedTrack(int track) { selectTrack(track); }
    void setSelectedPattern(int patternIndex) { selectPattern(patternIndex); }
    bool canUndo() const { return !busy && !undoStack.empty(); }
    bool canRedo() const { return !busy && !redoStack.empty(); }
    void restoreState(const WorkstationState&);
    void showPage(Page);
    Page getPage() const { return page; }
    juce::var runWorkflowSmoke();
    void importAudioClip();
    void bounceSelectedPattern();
    void toggleMasterRecording();
    void undo();
    void redo();
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    std::function<void()> onStateChanged;

private:
    void timerCallback() override;
    void publish(bool refresh = true);
    void edit(const std::function<void(WorkstationState&)>&, bool history = true);
    void refresh();
    void refreshContentChoices();
    void refreshFX();
    void refreshEditor();
    void syncPluginState();
    void reconcileAssets();
    void selectPattern(int);
    void selectTrack(int);
    void addNote(PatternNote);
    void changeNote(WorkstationId, PatternNote, bool copy);
    void selectNote(WorkstationId);
    void removeSelectedNote();
    void addClipAt(int track, double beat);
    void changeClip(WorkstationId, ArrangementClip, bool copy);
    void selectClip(WorkstationId);
    void removeSelectedClip();
    void assignLaunchSlot(int track, int scene);
    void setLaunchContent(int track, int scene, LaunchSlot);
    void launchScene(int scene);
    void importFile(const juce::File&, std::optional<WorkstationId> replace = {});
    void importRenderedAudio(const juce::File&, std::shared_ptr<const AudioData>, const juce::String&, std::vector<float>);
    void chooseAssetAction();
    void setAssetSourceTempo(WorkstationId, double);
    void runTask(const juce::String&, std::function<std::function<void()>()>);
    void reportError(const juce::String&);
    void openPluginManager();
    void openPluginEditor();
    std::array<FxSlot, 4>& effectRack();
    const std::array<FxSlot, 4>& effectRack() const;
    int pluginBus() const;
    void applyPreset(int);
    void stopTransport();
    PatternNote* selectedNote();
    ArrangementClip* selectedClip();
    WorkstationPlayMode modeForPage() const;
    static juce::String beatText(double);

    AudioEngine& engine;
    HardwareLookAndFeel hardware;
    WorkstationState state;
    WorkstationSnapshot snapshot;
    CaptureSnapshot capture;
    Page page = Page::Arrange;
    bool refreshing = false, busy = false, restoring = false;
    WorkstationId selectedNoteId = 0, selectedClipId = 0;
    ClipKind selectedContentKind = ClipKind::Pattern;
    WorkstationId selectedContentId = 1;
    int fxBus = 0, fxSlot = 0;
    juce::String status = "Select a pattern, draw notes, then arrange or launch it.";
    juce::File captureFile;
    bool capturePendingImport = false;
    std::thread worker;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::Component::SafePointer<juce::DialogWindow> pluginDialog;
    std::vector<WorkstationState> undoStack, redoStack;
    std::optional<WorkstationState> gestureBefore;
    std::vector<std::pair<ClipKind, WorkstationId>> contentChoices;
    std::map<WorkstationId, std::shared_ptr<const AudioData>> assetAudio;
    std::map<WorkstationId, std::vector<float>> assetPeaks;
    std::map<std::string, std::shared_ptr<const AudioData>> cachedAudio;
    std::map<std::string, std::vector<float>> cachedPeaks;
    std::optional<WorkstationState> pendingRestore;

    std::unique_ptr<ArrangementCanvas> arrangement;
    std::unique_ptr<PatternCanvas> pattern;
    juce::Viewport arrangementViewport, patternViewport;
    std::array<juce::TextButton, 16> patternButtons;
    std::array<std::unique_ptr<LaunchCell>, 32> launchCells;
    std::array<juce::TextButton, 4> sceneButtons;
    std::array<juce::TextButton, 8> trackButtons, muteButtons, soloButtons, stopTrackButtons;
    std::array<juce::Slider, 8> trackGain, trackPan, sendA, sendB;
    std::array<juce::ComboBox, 8> crossAssignments;
    std::array<juce::Label, 8> gainLabels;

    juce::TextButton playButton { "PLAY" }, stopButton { "STOP" }, noteRecord { "REC NOTES" }, masterRecord { "REC MIX" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, loopButton { "LOOP" };
    juce::Label clockLabel, tempoLabel, statusLabel, patternLabel, trackLabel;
    juce::Slider tempo, loopStart, loopEnd;
    juce::ComboBox trackChoice, patternLength, recordQuantize, launchQuantize;
    juce::TextButton addClipButton { "+ PLACE CLIP" }, importButton { "IMPORT AUDIO" }, bounceButton { "BOUNCE PATTERN" }, assetButton { "AUDIO FILE..." };
    juce::TextButton deleteClipButton { "DELETE CLIP" }, deleteNoteButton { "DELETE NOTE" }, clearPatternButton { "CLEAR PATTERN" };
    juce::ComboBox contentChoice;
    juce::Slider noteDuration, noteVelocity, notePitch, clipLength, clipGain, crossfader, masterGain;
    juce::Label durationLabel, velocityLabel, pitchLabel, clipLengthLabel, clipGainLabel;
    juce::TextButton clipLoop { "CLIP LOOP" };

    juce::ComboBox fxBusChoice, fxTypeChoice, fxPresetChoice;
    std::array<juce::TextButton, 4> fxSlotButtons;
    std::array<juce::TextButton, 4> hostedSlotButtons;
    std::array<juce::Slider, 6> fxParameters;
    std::array<juce::Label, 6> fxParameterLabels;
    juce::Slider fxMix;
    juce::Label fxName, fxDescription, fxMixLabel;
    juce::TextButton fxBypass { "BYPASS FX" }, pluginManager { "PLUG-IN MANAGER" }, pluginEditor { "OPEN EDITOR" }, pluginBypass { "BYPASS PLUG-IN" };
    juce::Label pluginStatus, bounceNotice;
    juce::Rectangle<int> libraryPanel, editorPanel, inspectorPanel, utilityPanel;
    juce::Rectangle<int> processorPanel, hostedPanel, performancePanel, masterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkstationComponent)
};
} // namespace samplify
