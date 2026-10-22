#include "HardwareLookAndFeel.h"
#include <algorithm>

namespace samplify {
juce::Colour HardwareLookAndFeel::chassis() { return juce::Colour(0xffd6cbb5); }
juce::Colour HardwareLookAndFeel::panel() { return juce::Colour(0xffbfb6a3); }
juce::Colour HardwareLookAndFeel::ink() { return juce::Colour(0xff393c34); }
juce::Colour HardwareLookAndFeel::lcd() { return juce::Colour(0xffa8b686); }
juce::Colour HardwareLookAndFeel::lcdInk() { return juce::Colour(0xff283b2d); }
juce::Colour HardwareLookAndFeel::amber() { return juce::Colour(0xffd78936); }
juce::Colour HardwareLookAndFeel::red() { return juce::Colour(0xffb7523f); }
juce::Colour HardwareLookAndFeel::trim() { return juce::Colour(0xff69685c); }
juce::Font HardwareLookAndFeel::labelFont(float size, bool bold) { return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)); }

HardwareLookAndFeel::HardwareLookAndFeel() {
    setColour(juce::Label::textColourId, ink());
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd9d2c3));
    setColour(juce::TextButton::buttonOnColourId, amber());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff302e25));
    setColour(juce::Slider::backgroundColourId, trim());
    setColour(juce::Slider::trackColourId, amber());
    setColour(juce::Slider::thumbColourId, juce::Colour(0xff373a34));
    setColour(juce::Slider::rotarySliderFillColourId, amber());
    setColour(juce::Slider::rotarySliderOutlineColourId, trim());
    setColour(juce::Slider::textBoxTextColourId, lcdInk());
    setColour(juce::Slider::textBoxBackgroundColourId, lcd());
    setColour(juce::Slider::textBoxOutlineColourId, trim());
    setColour(juce::ComboBox::backgroundColourId, lcd());
    setColour(juce::ComboBox::textColourId, lcdInk());
    setColour(juce::ComboBox::outlineColourId, trim());
    setColour(juce::PopupMenu::backgroundColourId, chassis());
    setColour(juce::PopupMenu::textColourId, ink());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink());
    setColour(juce::PopupMenu::highlightedTextColourId, chassis());
    setColour(juce::TextEditor::backgroundColourId, lcd());
    setColour(juce::TextEditor::textColourId, lcdInk());
    setColour(juce::TextEditor::outlineColourId, trim());
    setColour(juce::ListBox::backgroundColourId, lcd());
    setColour(juce::ListBox::textColourId, lcdInk());
    setColour(juce::ScrollBar::thumbColourId, trim().withAlpha(.75f));
    setColour(juce::ProgressBar::backgroundColourId, trim());
    setColour(juce::ProgressBar::foregroundColourId, amber());
}

void HardwareLookAndFeel::paintScrew(juce::Graphics& g, juce::Point<float> p, float r) {
    g.setColour(juce::Colour(0xff7d7d70)); g.fillEllipse(p.x - r, p.y - r, r * 2, r * 2);
    g.setColour(juce::Colour(0xffeee7d8)); g.drawEllipse(p.x - r, p.y - r, r * 2, r * 2, .7f);
    g.setColour(juce::Colour(0xff43493f)); g.drawLine(p.x - r * .55f, p.y + r * .25f, p.x + r * .55f, p.y - r * .25f, 1.2f);
}
void HardwareLookAndFeel::paintBezel(juce::Graphics& g, juce::Rectangle<float> area, bool inset) {
    g.setColour(inset ? trim() : chassis().brighter(.08f)); g.fillRoundedRectangle(area, 5);
    g.setColour(inset ? ink() : trim().withAlpha(.6f)); g.drawRoundedRectangle(area.reduced(.5f), 5, 1);
    if (!inset) { g.setColour(juce::Colours::white.withAlpha(.35f)); g.drawHorizontalLine(static_cast<int>(area.getY() + 1), area.getX() + 5, area.getRight() - 5); }
}
void HardwareLookAndFeel::paintLCD(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setColour(ink()); g.fillRoundedRectangle(area, 4);
    const auto inner = area.reduced(5);
    g.setGradientFill(juce::ColourGradient(lcd().darker(.08f), inner.getX(), inner.getY(), lcd(), inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle(inner, 1);
    g.setColour(lcdInk().withAlpha(.1f));
    for (int y = static_cast<int>(inner.getY()); y < inner.getBottom(); y += 3) g.drawHorizontalLine(y, inner.getX(), inner.getRight());
}
juce::Font HardwareLookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return labelFont(juce::jlimit(9.f, 12.f, h * .36f)); }
void HardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool over, bool down) {
    auto r = button.getLocalBounds().toFloat().reduced(1);
    auto c = button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId) : colour;
    if (over) c = c.brighter(.05f); if (down) c = c.darker(.09f);
    if (!button.isEnabled()) c = c.withMultipliedSaturation(.25f).withMultipliedBrightness(.9f);
    g.setColour(ink().withAlpha(.45f)); g.fillRoundedRectangle(r.translated(0, 1), 3);
    g.setGradientFill(juce::ColourGradient(c.brighter(.09f), r.getX(), r.getY(), c.darker(.08f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3); g.setColour(trim()); g.drawRoundedRectangle(r, 3, 1);
    if (!down) { g.setColour(juce::Colours::white.withAlpha(.4f)); g.drawLine(r.getX() + 3, r.getY() + 1.5f, r.getRight() - 3, r.getY() + 1.5f); }
}
void HardwareLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha(b.isEnabled() ? 1.f : .4f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 1), juce::Justification::centred, 2);
}
void HardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) {
    const float radius = std::max(2.f, std::min(w, h) * .39f);
    const juce::Point<float> centre(x + w * .5f, y + h * .5f);
    const auto r = juce::Rectangle<float>(radius * 2, radius * 2).withCentre(centre);
    g.setColour(ink().withAlpha(.35f)); g.fillEllipse(r.translated(0, 2));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff50534a), r.getX(), r.getY(), juce::Colour(0xff252a25), r.getRight(), r.getBottom(), false));
    g.fillEllipse(r); g.setColour(juce::Colour(0xff20281f)); g.drawEllipse(r, 1.1f);
    const auto angle = start + pos * (end - start);
    g.setColour(s.isEnabled() ? chassis().brighter(.15f) : trim());
    g.drawLine({ centre.getPointOnCircumference(radius * .35f, angle), centre.getPointOnCircumference(radius * .86f, angle) }, std::max(2.f, radius * .13f));
}
void HardwareLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, const juce::Slider::SliderStyle, juce::Slider& s) {
    const bool vertical = s.isVertical();
    const float centre = vertical ? x + w * .5f : y + h * .5f;
    g.setColour(ink().withAlpha(.85f));
    if (vertical) g.fillRoundedRectangle(centre - 2, static_cast<float>(y), 4, static_cast<float>(h), 2);
    else g.fillRoundedRectangle(static_cast<float>(x), centre - 2, static_cast<float>(w), 4, 2);
    auto thumb = vertical ? juce::Rectangle<float>(centre - 12, pos - 8, 24, 16) : juce::Rectangle<float>(pos - 7, centre - 10, 14, 20);
    g.setColour(juce::Colour(0xff333b32)); g.fillRoundedRectangle(thumb, 2);
    g.setColour(chassis());
    if (vertical) g.drawLine(thumb.getX() + 3, pos, thumb.getRight() - 3, pos, 1.5f);
    else g.drawLine(pos, thumb.getY() + 3, pos, thumb.getBottom() - 3, 1.5f);
}
void HardwareLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    const auto r = juce::Rectangle<float>(0, 0, static_cast<float>(w), static_cast<float>(h)).reduced(.5f);
    g.setColour(lcd()); g.fillRoundedRectangle(r, 2); g.setColour(trim()); g.drawRoundedRectangle(r, 2, 1);
    g.setColour(box.isEnabled() ? lcdInk() : trim()); juce::Path p;
    p.addTriangle(w - 16.f, h * .4f, w - 8.f, h * .4f, w - 12.f, h * .63f); g.fillPath(p);
}
juce::Font HardwareLookAndFeel::getComboBoxFont(juce::ComboBox&) { return labelFont(11, false); }
} // namespace samplify
