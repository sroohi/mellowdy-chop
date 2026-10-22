#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace samplify {
class HardwareLookAndFeel : public juce::LookAndFeel_V4 {
public:
    HardwareLookAndFeel();
    static juce::Colour chassis();
    static juce::Colour panel();
    static juce::Colour ink();
    static juce::Colour lcd();
    static juce::Colour lcdInk();
    static juce::Colour amber();
    static juce::Colour red();
    static juce::Colour trim();
    static void paintBezel(juce::Graphics&, juce::Rectangle<float>, bool inset = false);
    static void paintScrew(juce::Graphics&, juce::Point<float>, float radius = 4);
    static void paintLCD(juce::Graphics&, juce::Rectangle<float>);
    static juce::Font labelFont(float size = 11, bool bold = true);
    juce::Font getTextButtonFont(juce::TextButton&, int height) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          const juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};
} // namespace samplify
