#pragma once
#include "MainComponent.h"
#include "WorkstationComponent.h"
#include "HardwareLookAndFeel.h"

namespace samplify {
// Cabinet, mode rail and permanent clock/transport. Each work surface owns its
// editing controls while all modes share the same session and audio engine.
class StudioComponent final : public juce::Component, private juce::Timer {
public:
    StudioComponent();
    ~StudioComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPage(int index);
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;
    MainComponent& sampler() { return instrument; }
    WorkstationComponent& workstation() { return studio; }
    juce::var runConsoleWorkflowSmoke();
private:
    void timerCallback() override;
    void refreshTransport();
    void stopAll();
    WorkstationPlayMode playMode();
    HardwareLookAndFeel hardware;
    MainComponent instrument;
    WorkstationComponent studio;
    std::array<std::unique_ptr<juce::TextButton>, 5> modes;
    juce::TextButton open { "OPEN" }, save { "SAVE" }, audio { "AUDIO / MIDI" };
    juce::TextButton play { "PLAY" }, stop { "STOP" }, noteRecord { "REC NOTES" }, mixRecord { "REC MIX" };
    juce::TextButton undo { "UNDO" }, redo { "REDO" };
    juce::Slider tempo;
    juce::ComboBox patternChoice;
    juce::Label clock, transportState;
    juce::Rectangle<int> rail, masthead, dock, clockPanel, meterPanel;
    int selectedPage = 0;
    float peak = 0;
    bool refreshing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioComponent)
};
}
