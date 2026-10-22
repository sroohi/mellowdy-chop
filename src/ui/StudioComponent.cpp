#include "StudioComponent.h"
#include "plugins/PluginHost.h"

namespace samplify {
StudioComponent::StudioComponent() : studio(instrument.audioEngine()) {
    setLookAndFeel(&hardware);
    addAndMakeVisible(instrument); addChildComponent(studio);
    constexpr const char* names[] { "SAMPLER", "ARRANGE", "PATTERNS", "FX RACK", "PERFORM" };
    for (int i = 0; i < 5; ++i) {
        auto& button = tabs[static_cast<std::size_t>(i)];
        button = std::make_unique<juce::TextButton>(names[i]);
        button->onClick = [this, i] { showPage(i); };
        addAndMakeVisible(*button);
    }
    for (auto* button : { &open, &save, &stop }) addAndMakeVisible(button);
    open.onClick = [this] { instrument.openSession(); };
    save.onClick = [this] { instrument.saveSession(); };
    stop.onClick = [this] {
        instrument.stopPlayback(); instrument.audioEngine().stopWorkstation();
        instrument.audioEngine().setPatternRecording(false);
        instrument.audioEngine().stopMasterCapture();
    };
    stop.setColour(juce::TextButton::buttonColourId, HardwareLookAndFeel::red());
    stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    instrument.onReadWorkstationState = [this] {
        return studio.captureState();
    };
    instrument.onRestoreWorkstationState = [this](const WorkstationState& state) { studio.restoreState(state); };
    instrument.configureStudioRouting([this] { return studio.getState().selectedTrack; }, [this](int track) { studio.setSelectedTrack(track); });
    instrument.onHostTempoChanged = [this](double bpm) { studio.setTempo(bpm); };
    studio.onStateChanged = [this] { instrument.setStudioTempo(studio.getState().tempo); instrument.refreshStudioRouting(); };
    showPage(0);
}
StudioComponent::~StudioComponent() {
    studio.onStateChanged = {};
    instrument.onReadStudioTrack = {}; instrument.onStudioTrackChanged = {};
    instrument.onHostTempoChanged = {};
    instrument.onReadWorkstationState = {}; instrument.onRestoreWorkstationState = {};
    setLookAndFeel(nullptr);
}
void StudioComponent::showPage(int index) {
    selectedPage = juce::jlimit(0, 4, index);
    instrument.setVisible(selectedPage == 0); studio.setVisible(selectedPage != 0);
    if (selectedPage != 0) {
        using Page = WorkstationComponent::Page;
        constexpr Page pages[] { Page::Arrange, Page::Patterns, Page::FX, Page::Perform };
        studio.showPage(pages[selectedPage - 1]);
    }
    for (int i = 0; i < 5; ++i) tabs[static_cast<std::size_t>(i)]->setToggleState(i == selectedPage, juce::dontSendNotification);
    repaint();
}
bool StudioComponent::keyPressed(const juce::KeyPress& key) {
    if (selectedPage == 0 || key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown()) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (juce::String("awsedftgyhujkolp").containsChar(ch)) return instrument.keyPressed(key);
    return false;
}
bool StudioComponent::keyStateChanged(bool down) { return instrument.keyStateChanged(down); }

void StudioComponent::paint(juce::Graphics& g) {
    using H = HardwareLookAndFeel;
    g.fillAll(H::chassis());
    g.setColour(H::ink()); g.fillRect(0, 0, getWidth(), 4);
    g.setFont(H::labelFont(27)); g.drawText("samplify", 26, 12, 155, 33, juce::Justification::centredLeft);
    g.setFont(H::labelFont(9)); g.setColour(H::ink().withAlpha(.72f));
    g.drawText("MP AUDIO  /  SAMPLING WORKSTATION", 27, 45, 240, 15, juce::Justification::centredLeft);
    g.setColour(H::trim().withAlpha(.45f)); g.drawHorizontalLine(77, 16, static_cast<float>(getWidth() - 16));
    H::paintScrew(g, {12, 18}); H::paintScrew(g, {static_cast<float>(getWidth() - 12), 18});
    H::paintScrew(g, {12, static_cast<float>(getHeight() - 12)});
    H::paintScrew(g, {static_cast<float>(getWidth() - 12), static_cast<float>(getHeight() - 12)});
}
void StudioComponent::resized() {
    const int start = 280, right = 265;
    const int tabWidth = std::max(74, std::min(112, (getWidth() - start - right) / 5));
    for (int i = 0; i < 5; ++i) tabs[static_cast<std::size_t>(i)]->setBounds(start + i * tabWidth, 22, tabWidth - 5, 34);
    open.setBounds(getWidth() - 245, 22, 65, 34);
    save.setBounds(getWidth() - 174, 22, 65, 34);
    stop.setBounds(getWidth() - 103, 22, 83, 34);
    auto content = getLocalBounds().withTrimmedTop(82).reduced(16, 0).withTrimmedBottom(19);
    instrument.setBounds(content); studio.setBounds(content);
}
}
