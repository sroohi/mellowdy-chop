#include "StudioComponent.h"
#include "plugins/PluginHost.h"
#include <cmath>

namespace samplify {
namespace {
constexpr const char* modeNames[] { "01  SAMPLE", "02  SONG", "03  PATTERN", "04  EFFECTS", "05  PERFORM" };
constexpr const char* pageNames[] { "CHOP LAB", "SONG ASSEMBLY", "PATTERN WRITER", "SIGNAL WORKSHOP", "LIVE CONSOLE" };
constexpr const char* descriptions[] {
    "32 CUES  /  TWO PAD BANKS  /  FIVE EDIT MODULES",
    "EIGHT TRACKS  /  PATTERNS + AUDIO  /  CLIP ARRANGEMENT",
    "SIXTEEN PATTERNS  /  NOTE RECORDING + EDITING",
    "SOUND DESIGN  /  MIXING  /  MASTERING  /  AU + VST3",
    "FOUR SCENES  /  EIGHT CHANNELS  /  LIVE MIX RECORDING"
};
}
StudioComponent::StudioComponent() : studio(instrument.audioEngine()) {
    setLookAndFeel(&hardware);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(instrument); addChildComponent(studio);
    for (int i = 0; i < 5; ++i) {
        auto& button = modes[static_cast<std::size_t>(i)];
        button = std::make_unique<juce::TextButton>(modeNames[i]);
        button->getProperties().set("hardwareRole", "mode");
        button->setWantsKeyboardFocus(false);
        button->onClick = [this, i] { showPage(i); };
        addAndMakeVisible(*button);
    }
    for (auto* button : { &open, &save, &audio, &play, &stop, &noteRecord, &mixRecord, &undo, &redo }) {
        addAndMakeVisible(button);
        button->setWantsKeyboardFocus(false);
    }
    for (auto* button : { &play, &stop, &noteRecord, &mixRecord }) button->getProperties().set("hardwareRole", "dark");
    open.onClick = [this] { instrument.openSession(); };
    save.onClick = [this] { instrument.saveSession(); };
    audio.onClick = [this] { instrument.showAudioSettings(); };
    play.onClick = [this] { instrument.audioEngine().playWorkstation(playMode()); };
    stop.onClick = [this] { stopAll(); };
    noteRecord.onClick = [this] {
        auto& engine = instrument.audioEngine();
        const auto runtime = engine.workstationSnapshot();
        engine.setPatternRecording(!runtime.recording);
        if (!runtime.playing) engine.playWorkstation(playMode());
    };
    mixRecord.onClick = [this] { studio.toggleMasterRecording(); };
    undo.onClick = [this] { if (selectedPage == 0) instrument.undoEdit(); else studio.undo(); refreshTransport(); };
    redo.onClick = [this] { if (selectedPage == 0) instrument.redoEdit(); else studio.redo(); refreshTransport(); };
    play.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff7a9569));
    for (auto* button : { &noteRecord, &mixRecord }) button->setColour(juce::TextButton::buttonOnColourId, HardwareLookAndFeel::red());
    stop.setTooltip("Stop all sampler voices, the song clock, note recording and mix capture.");
    play.setTooltip("Play the selected pattern, song or performance clock. Source audition is on the sampling deck.");
    noteRecord.setTooltip("Record pad and MIDI notes into the selected pattern. Press again to disarm.");
    mixRecord.setTooltip("Record the app's stereo master output to a WAV file.");
    tempo.setSliderStyle(juce::Slider::IncDecButtons);
    tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 58, 30);
    tempo.setRange(20, 999, .1); tempo.setNumDecimalPlacesToDisplay(1);
    tempo.setTooltip("Shared pattern, song and performance tempo.");
    tempo.onValueChange = [this] { if (!refreshing) studio.setTempo(tempo.getValue()); };
    addAndMakeVisible(tempo);
    for (int i = 0; i < workstationPatternCount; ++i) patternChoice.addItem("PATTERN " + juce::String(i + 1).paddedLeft('0', 2), i + 1);
    patternChoice.onChange = [this] { if (!refreshing) studio.setSelectedPattern(patternChoice.getSelectedId() - 1); };
    addAndMakeVisible(patternChoice);
    clock.setFont(HardwareLookAndFeel::monoFont(24, true));
    clock.setColour(juce::Label::textColourId, HardwareLookAndFeel::lcdInk());
    transportState.setFont(HardwareLookAndFeel::monoFont(10, true));
    transportState.setColour(juce::Label::textColourId, HardwareLookAndFeel::lcdInk());
    addAndMakeVisible(clock); addAndMakeVisible(transportState);
    instrument.onReadWorkstationState = [this] { return studio.captureState(); };
    instrument.onRestoreWorkstationState = [this](const WorkstationState& state) { studio.restoreState(state); };
    instrument.configureStudioRouting([this] { return studio.getState().selectedTrack; }, [this](int track) { studio.setSelectedTrack(track); });
    instrument.onHostTempoChanged = [this](double bpm) { studio.setTempo(bpm); };
    studio.onStateChanged = [this] {
        instrument.setStudioTempo(studio.getState().tempo);
        instrument.refreshStudioRouting(); refreshTransport();
    };
    showPage(0); refreshTransport(); startTimerHz(20);
}
StudioComponent::~StudioComponent() {
    stopTimer();
    studio.onStateChanged = {};
    instrument.onReadStudioTrack = {}; instrument.onStudioTrackChanged = {};
    instrument.onHostTempoChanged = {};
    instrument.onReadWorkstationState = {}; instrument.onRestoreWorkstationState = {};
    setLookAndFeel(nullptr);
}
void StudioComponent::stopAll() {
    instrument.stopPlayback();
    auto& engine = instrument.audioEngine();
    engine.stopWorkstation(); engine.setPatternRecording(false); engine.stopMasterCapture();
}
WorkstationPlayMode StudioComponent::playMode() {
    if (selectedPage == 1) return WorkstationPlayMode::Arrangement;
    if (selectedPage == 4) return WorkstationPlayMode::Performance;
    if (selectedPage == 3) return instrument.audioEngine().workstationSnapshot().mode;
    return WorkstationPlayMode::Pattern;
}
void StudioComponent::showPage(int index) {
    selectedPage = juce::jlimit(0, 4, index);
    instrument.setVisible(selectedPage == 0); studio.setVisible(selectedPage != 0);
    if (selectedPage != 0) {
        using Page = WorkstationComponent::Page;
        constexpr Page pages[] { Page::Arrange, Page::Patterns, Page::FX, Page::Perform };
        studio.showPage(pages[selectedPage - 1]);
    }
    for (int i = 0; i < 5; ++i) modes[static_cast<std::size_t>(i)]->setToggleState(i == selectedPage, juce::dontSendNotification);
    refreshTransport(); repaint();
}
bool StudioComponent::keyPressed(const juce::KeyPress& key) {
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    if (selectedPage == 0) return instrument.keyPressed(key);
    if (studio.keyPressed(key)) return true;
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown()) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (juce::String("awsedftgyhujkolp").containsChar(ch)) return instrument.keyPressed(key);
    return false;
}
bool StudioComponent::keyStateChanged(bool down) { return instrument.keyStateChanged(down); }
void StudioComponent::timerCallback() { refreshTransport(); }
void StudioComponent::refreshTransport() {
    const juce::ScopedValueSetter<bool> guard(refreshing, true);
    const auto& state = studio.getState();
    const auto runtime = instrument.audioEngine().workstationSnapshot();
    const auto capture = instrument.audioEngine().captureSnapshot();
    tempo.setValue(state.tempo, juce::dontSendNotification);
    patternChoice.setSelectedId(state.selectedPattern + 1, juce::dontSendNotification);
    for (int i = 0; i < workstationPatternCount; ++i) {
        const auto text = "P" + juce::String(i + 1).paddedLeft('0', 2) + "  " + juce::String(state.patterns[static_cast<std::size_t>(i)].name);
        if (patternChoice.getItemText(i) != text) patternChoice.changeItemText(i + 1, text);
    }
    play.setToggleState(runtime.playing, juce::dontSendNotification);
    noteRecord.setToggleState(runtime.recording, juce::dontSendNotification);
    mixRecord.setToggleState(capture.recording, juce::dontSendNotification);
    mixRecord.setEnabled(!capture.finalizing);
    mixRecord.setButtonText(capture.finalizing ? "WRITING" : "REC MIX");
    undo.setEnabled(selectedPage == 0 ? instrument.canUndoEdit() : studio.canUndo());
    redo.setEnabled(selectedPage == 0 ? instrument.canRedoEdit() : studio.canRedo());
    const auto beat = std::max(0.0, runtime.beat);
    const int bar = static_cast<int>(beat / 4) + 1, quarter = static_cast<int>(beat) % 4 + 1;
    const int tick = static_cast<int>((beat - std::floor(beat)) * 96);
    clock.setText(juce::String(bar).paddedLeft('0', 3) + " . " + juce::String(quarter) + " . " + juce::String(tick).paddedLeft('0', 2), juce::dontSendNotification);
    const auto mode = runtime.mode == WorkstationPlayMode::Pattern ? "PATTERN" : runtime.mode == WorkstationPlayMode::Arrangement ? "SONG" : "PERFORM";
    transportState.setText(juce::String(runtime.playing ? "RUN  " : "STOP ") + mode + (runtime.recording ? "  NOTE REC" : "") + (capture.recording ? "  MIX REC" : ""), juce::dontSendNotification);
    peak = juce::jlimit(0.0f, 1.0f, instrument.audioEngine().peakLevel());
    repaint(meterPanel);
}
void StudioComponent::paint(juce::Graphics& g) {
    using H = HardwareLookAndFeel;
    g.fillAll(H::chassis());
    g.setColour(H::trim().withAlpha(.3f)); g.drawRect(getLocalBounds().reduced(3), 2);
    H::paintModule(g, rail.toFloat(), {}, true);
    g.setColour(H::chassis()); g.setFont(H::labelFont(25));
    g.drawText("SAMPLIFY", rail.getX() + 17, 22, rail.getWidth() - 30, 30, juce::Justification::centredLeft);
    g.setColour(H::lcd()); g.setFont(H::monoFont(9, true));
    g.drawText("MP AUDIO  /  MPC-32", rail.getX() + 18, 55, rail.getWidth() - 24, 16, juce::Justification::centredLeft);
    H::paintVent(g, juce::Rectangle<float>(rail.getX() + 18.0f, 87.0f, rail.getWidth() - 36.0f, 19));
    g.setColour(H::chassis().withAlpha(.55f)); g.setFont(H::labelFont(9));
    g.drawText("WORK SURFACE", rail.getX() + 18, 122, rail.getWidth() - 26, 15, juce::Justification::centredLeft);
    g.drawText("SESSION MEMORY", rail.getX() + 18, rail.getBottom() - 144, rail.getWidth() - 26, 15, juce::Justification::centredLeft);
    g.setColour(H::ink()); g.setFont(H::labelFont(23));
    g.drawText(pageNames[selectedPage], masthead.withWidth(390), juce::Justification::centredLeft);
    g.setColour(H::ink().withAlpha(.7f)); g.setFont(H::monoFont(9));
    g.drawText(descriptions[selectedPage], masthead.withTrimmedLeft(390), juce::Justification::centredRight);
    H::paintModule(g, dock.toFloat()); H::paintLCD(g, clockPanel.toFloat());
    g.setColour(H::ink().withAlpha(.7f)); g.setFont(H::labelFont(9));
    g.drawText("TEMPO", tempo.getX(), dock.getY() + 9, tempo.getWidth(), 14, juce::Justification::centred);
    g.drawText("PATTERN MEMORY", patternChoice.getX(), dock.getY() + 9, patternChoice.getWidth(), 14, juce::Justification::centred);
    g.drawText("TRANSPORT / RECORDER", play.getX(), dock.getY() + 9, mixRecord.getRight() - play.getX(), 14, juce::Justification::centred);
    g.drawText("EDIT HISTORY", undo.getX(), dock.getY() + 9, redo.getRight() - undo.getX(), 14, juce::Justification::centred);
    g.drawText("OUT", meterPanel.getX(), dock.getY() + 9, meterPanel.getWidth(), 14, juce::Justification::centred);
    g.setColour(H::graphite()); g.fillRoundedRectangle(meterPanel.toFloat(), 2);
    const int segments = std::max(1, meterPanel.getWidth() / 6);
    for (int i = 0; i < segments; ++i) {
        const float level = static_cast<float>(i + 1) / segments;
        g.setColour((level > .88f ? H::red() : level > .65f ? H::amber() : H::lcd()).withAlpha(peak >= level ? 1.0f : .18f));
        g.fillRect(meterPanel.getX() + i * 6 + 2, meterPanel.getY() + 3, 3, meterPanel.getHeight() - 6);
    }
    for (const auto p : {juce::Point<float>(6, 8), juce::Point<float>(getWidth() - 6.0f, 8), juce::Point<float>(6, getHeight() - 8.0f), juce::Point<float>(getWidth() - 6.0f, getHeight() - 8.0f)}) H::paintScrew(g, p, 3);
}
void StudioComponent::resized() {
    rail = {12, 12, 160, getHeight() - 124};
    masthead = {190, 12, getWidth() - 208, 46};
    dock = {12, getHeight() - 100, getWidth() - 24, 88};
    for (int i = 0; i < 5; ++i) modes[static_cast<std::size_t>(i)]->setBounds(rail.getX() + 14, 148 + i * 62, rail.getWidth() - 28, 50);
    open.setBounds(rail.getX() + 14, rail.getBottom() - 118, 63, 36);
    save.setBounds(rail.getX() + 83, rail.getBottom() - 118, 63, 36);
    audio.setBounds(rail.getX() + 14, rail.getBottom() - 72, rail.getWidth() - 28, 36);
    const juce::Rectangle<int> content(184, 64, getWidth() - 198, getHeight() - 172);
    instrument.setBounds(content); studio.setBounds(content);
    int x = dock.getX() + 14;
    clockPanel = {x, dock.getY() + 13, 211, 61};
    clock.setBounds(clockPanel.reduced(9).withTrimmedBottom(17));
    transportState.setBounds(clockPanel.getX() + 9, clockPanel.getBottom() - 24, clockPanel.getWidth() - 18, 17);
    x = clockPanel.getRight() + 14;
    tempo.setBounds(x, dock.getY() + 32, 84, 36); x += 98;
    const int choiceWidth = getWidth() < 1280 ? 132 : 166;
    patternChoice.setBounds(x, dock.getY() + 32, choiceWidth, 36); x += choiceWidth + 16;
    play.setBounds(x, dock.getY() + 32, 62, 38); x += 69;
    stop.setBounds(x, dock.getY() + 32, 62, 38); x += 69;
    noteRecord.setBounds(x, dock.getY() + 32, 84, 38); x += 91;
    mixRecord.setBounds(x, dock.getY() + 32, 78, 38); x += 92;
    undo.setBounds(x, dock.getY() + 32, 50, 38); x += 56;
    redo.setBounds(x, dock.getY() + 32, 50, 38); x += 64;
    meterPanel = {x, dock.getY() + 35, std::max(18, dock.getRight() - x - 16), 31};
}
juce::var StudioComponent::runConsoleWorkflowSmoke() {
    auto* checks = new juce::DynamicObject();
    auto check = [checks](const char* name, bool result) { checks->setProperty(name, result); };
    const auto originalSize = getBounds(); const int originalPage = selectedPage;
    const auto savedState = studio.captureState();
    bool navigation = true;
    for (int i = 0; i < 5; ++i) {
        modes[static_cast<std::size_t>(i)]->onClick();
        navigation &= selectedPage == i && instrument.isVisible() == (i == 0) && studio.isVisible() == (i != 0);
        navigation &= modes[static_cast<std::size_t>(i)]->getToggleState();
    }
    check("physicalModeRailNavigatesAllFiveSurfaces", navigation);
    auto layoutValid = [this] {
        const auto content = selectedPage == 0 ? instrument.getBounds() : studio.getBounds();
        bool valid = getLocalBounds().contains(content) && !content.intersects(rail) && !content.intersects(dock);
        std::vector<juce::Rectangle<int>> bounds;
        for (const juce::Component* c : std::initializer_list<const juce::Component*> { &play, &stop, &noteRecord, &mixRecord, &undo, &redo, &tempo, &patternChoice, &clock }) {
            valid &= c->isVisible() && dock.contains(c->getBounds()) && c->getWidth() > 0 && c->getHeight() > 0;
            for (const auto b : bounds) valid &= !b.intersects(c->getBounds());
            bounds.push_back(c->getBounds());
        }
        for (const auto& mode : modes) valid &= rail.contains(mode->getBounds()) && mode->getBounds().getRight() < content.getX();
        return valid;
    };
    check("globalDockAndRailHaveSeparateInteractiveBounds", layoutValid());
    setSize(1180, 800); check("compactConsoleKeepsAllGlobalControlsReachable", layoutValid() && instrument.getWidth() >= 980 && instrument.getHeight() >= 620);
    setSize(originalSize.getWidth(), originalSize.getHeight());
    patternChoice.setSelectedId(16, juce::sendNotificationSync);
    check("globalPatternMemorySelectsBankSixteen", studio.getState().selectedPattern == 15);
    patternChoice.setSelectedId(1, juce::sendNotificationSync);
    check("globalPatternMemorySelectsBankOne", studio.getState().selectedPattern == 0);
    tempo.setValue(113.7, juce::sendNotificationSync);
    check("globalTempoEditsSharedSongClock", std::abs(studio.getState().tempo - 113.7) < .001);
    showPage(1);
    const bool undoRouted = keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z'));
    check("modeRailKeepsSongUndoKeyboardRouting", undoRouted);
    check("globalUndoRevertsSongTempo", std::abs(studio.getState().tempo - savedState.tempo) < .001);
    redo.onClick(); check("globalRedoRestoresSongTempo", std::abs(studio.getState().tempo - 113.7) < .001);
    showPage(0); check("sampleSurfaceUsesPatternTransport", playMode() == WorkstationPlayMode::Pattern);
    showPage(1); check("songSurfaceUsesArrangementTransport", playMode() == WorkstationPlayMode::Arrangement);
    showPage(4); check("liveSurfaceUsesPerformanceTransport", playMode() == WorkstationPlayMode::Performance);
    check("performanceSpaceShortcutReachesTransportController", keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    stopAll();
    check("globalControlsAreWiredToExistingControllers", static_cast<bool>(play.onClick) && static_cast<bool>(stop.onClick) && static_cast<bool>(noteRecord.onClick) && static_cast<bool>(mixRecord.onClick) && static_cast<bool>(open.onClick) && static_cast<bool>(save.onClick) && static_cast<bool>(audio.onClick));
    studio.restoreState(savedState); showPage(originalPage);
    return juce::var(checks);
}
}
