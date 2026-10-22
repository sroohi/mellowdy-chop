#pragma once
#include "MainComponent.h"
#include "WorkstationComponent.h"
#include "HardwareLookAndFeel.h"

namespace samplify {
class StudioComponent final : public juce::Component {
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
private:
    HardwareLookAndFeel hardware;
    MainComponent instrument;
    WorkstationComponent studio;
    std::array<std::unique_ptr<juce::TextButton>, 5> tabs;
    juce::TextButton open { "OPEN" }, save { "SAVE" }, stop { "STOP ALL" };
    int selectedPage = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioComponent)
};
}
