#include "HardwareLookAndFeel.h"
#include <algorithm>

namespace samplify {
namespace {
bool isDarkKey(const juce::Button& button) {
    const auto role = button.getProperties().getWithDefault("hardwareRole", "").toString();
    return role == "mode" || role == "dark";
}
juce::Rectangle<float> keyFace(const juce::Button& button, bool down) {
    return button.getLocalBounds().toFloat().reduced(2.5f, 2.f)
        .withTrimmedBottom(down ? 1.f : 3.f).translated(0, down ? 2.f : 0.f);
}
} // namespace

juce::Colour HardwareLookAndFeel::chassis() { return juce::Colour(0xffd8cdb6); }
juce::Colour HardwareLookAndFeel::panel() { return juce::Colour(0xffbfb49d); }
juce::Colour HardwareLookAndFeel::ink() { return juce::Colour(0xff34362f); }
juce::Colour HardwareLookAndFeel::lcd() { return juce::Colour(0xffb2bd83); }
juce::Colour HardwareLookAndFeel::lcdInk() { return juce::Colour(0xff263a26); }
juce::Colour HardwareLookAndFeel::amber() { return juce::Colour(0xffd99a44); }
juce::Colour HardwareLookAndFeel::red() { return juce::Colour(0xffbc5a46); }
juce::Colour HardwareLookAndFeel::trim() { return juce::Colour(0xff737060); }
juce::Colour HardwareLookAndFeel::graphite() { return juce::Colour(0xff353a35); }
juce::Font HardwareLookAndFeel::labelFont(float size, bool bold) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font HardwareLookAndFeel::monoFont(float size, bool bold) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                     bold ? juce::Font::bold : juce::Font::plain));
}

HardwareLookAndFeel::HardwareLookAndFeel() {
    setColour(juce::Label::textColourId, ink());
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe1d8c5));
    setColour(juce::TextButton::buttonOnColourId, amber());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff302d22));
    setColour(juce::Slider::backgroundColourId, trim());
    setColour(juce::Slider::trackColourId, amber());
    setColour(juce::Slider::thumbColourId, graphite());
    setColour(juce::Slider::rotarySliderFillColourId, amber());
    setColour(juce::Slider::rotarySliderOutlineColourId, trim());
    setColour(juce::Slider::textBoxTextColourId, lcdInk());
    setColour(juce::Slider::textBoxBackgroundColourId, lcd());
    setColour(juce::Slider::textBoxOutlineColourId, graphite());
    setColour(juce::Slider::textBoxHighlightColourId, lcdInk().withAlpha(.2f));
    setColour(juce::ComboBox::backgroundColourId, lcd());
    setColour(juce::ComboBox::textColourId, lcdInk());
    setColour(juce::ComboBox::outlineColourId, graphite());
    setColour(juce::ComboBox::arrowColourId, chassis());
    setColour(juce::PopupMenu::backgroundColourId, chassis());
    setColour(juce::PopupMenu::textColourId, ink());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, graphite());
    setColour(juce::PopupMenu::highlightedTextColourId, chassis());
    setColour(juce::TextEditor::backgroundColourId, lcd());
    setColour(juce::TextEditor::textColourId, lcdInk());
    setColour(juce::TextEditor::outlineColourId, graphite());
    setColour(juce::TextEditor::focusedOutlineColourId, amber());
    setColour(juce::TextEditor::highlightColourId, lcdInk().withAlpha(.2f));
    setColour(juce::ListBox::backgroundColourId, lcd());
    setColour(juce::ListBox::textColourId, lcdInk());
    setColour(juce::ScrollBar::thumbColourId, trim().withAlpha(.85f));
    setColour(juce::ProgressBar::backgroundColourId, graphite());
    setColour(juce::ProgressBar::foregroundColourId, amber());
}

void HardwareLookAndFeel::paintScrew(juce::Graphics& g, juce::Point<float> p, float r) {
    const auto head = juce::Rectangle<float>(r * 2.f, r * 2.f).withCentre(p);
    g.setColour(juce::Colours::black.withAlpha(.24f));
    g.fillEllipse(head.expanded(1.f).translated(0, .5f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb4b0a0), head.getTopLeft(),
                                        juce::Colour(0xff6c6c60), head.getBottomRight(), false));
    g.fillEllipse(head);
    g.setColour(chassis().brighter(.3f).withAlpha(.7f));
    g.drawEllipse(head.reduced(.4f), .6f);
    g.setColour(graphite().darker(.35f));
    g.drawLine(p.x - r * .55f, p.y + r * .28f, p.x + r * .55f, p.y - r * .28f, 1.3f);
    g.setColour(chassis().withAlpha(.45f));
    g.drawLine(p.x - r * .5f, p.y + r * .28f + 1.f, p.x + r * .5f, p.y - r * .28f + 1.f, .5f);
}

void HardwareLookAndFeel::paintBezel(juce::Graphics& g, juce::Rectangle<float> area, bool inset) {
    if (area.isEmpty()) return;
    const auto face = area.reduced(1.5f);
    g.setColour(juce::Colours::black.withAlpha(inset ? .4f : .23f));
    g.fillRoundedRectangle(area.translated(0, inset ? 0.f : 1.5f), 4.f);
    g.setGradientFill(juce::ColourGradient(inset ? graphite().darker(.22f) : chassis().brighter(.15f),
                                        face.getTopLeft(), inset ? graphite() : panel(), face.getBottomLeft(), false));
    g.fillRoundedRectangle(face, 3.f);
    g.setColour(inset ? juce::Colours::black.withAlpha(.5f) : trim().withAlpha(.8f));
    g.drawRoundedRectangle(face.reduced(.5f), 3.f, 1.f);
    g.setColour(chassis().brighter(.3f).withAlpha(inset ? .2f : .65f));
    const float edgeY = inset ? area.getBottom() - 1.f : area.getY() + 2.f;
    g.drawLine(area.getX() + 5.f, edgeY, area.getRight() - 5.f, edgeY, 1.f);
}

void HardwareLookAndFeel::panelTitle(juce::Graphics& g, const juce::String& text,
                                    juce::Rectangle<float> area, bool inverse) {
    g.setFont(monoFont(11.5f, true));
    g.setColour(inverse ? juce::Colours::black.withAlpha(.35f) : juce::Colours::white.withAlpha(.38f));
    g.drawText(text.toUpperCase(), area.translated(0, 1), juce::Justification::centredLeft, false);
    g.setColour(inverse ? chassis() : ink());
    g.drawText(text.toUpperCase(), area, juce::Justification::centredLeft, false);
}

void HardwareLookAndFeel::paintModule(juce::Graphics& g, juce::Rectangle<float> area,
                                     const juce::String& title, bool dark) {
    if (area.isEmpty()) return;
    paintBezel(g, area, dark);
    if (title.isNotEmpty()) {
        const auto header = area.reduced(11.f, 0).withTrimmedTop(5.f).withHeight(23.f);
        panelTitle(g, title, header, dark);
        g.setColour((dark ? chassis() : ink()).withAlpha(.22f));
        g.drawLine(area.getX() + 11.f, area.getY() + 31.f, area.getRight() - 11.f, area.getY() + 31.f, .7f);
        g.setColour((dark ? juce::Colours::black : juce::Colours::white).withAlpha(.2f));
        g.drawLine(area.getX() + 11.f, area.getY() + 32.f, area.getRight() - 11.f, area.getY() + 32.f, .7f);
    }
}

void HardwareLookAndFeel::paintVent(juce::Graphics& g, juce::Rectangle<float> area) {
    if (area.getWidth() < 6.f || area.getHeight() < 3.f) return;
    const int slots = juce::jmax(1, static_cast<int>(area.getWidth() / 7.f));
    const float pitch = area.getWidth() / static_cast<float>(slots);
    for (int i = 0; i < slots; ++i) {
        const auto slot = juce::Rectangle<float>(area.getX() + static_cast<float>(i) * pitch, area.getY(),
                                                 juce::jmin(2.5f, pitch * .45f), area.getHeight());
        g.setColour(chassis().brighter(.3f).withAlpha(.5f));
        g.fillRoundedRectangle(slot.translated(1, 1), 1.2f);
        g.setColour(graphite().darker(.4f));
        g.fillRoundedRectangle(slot, 1.2f);
    }
}

void HardwareLookAndFeel::paintLCD(juce::Graphics& g, juce::Rectangle<float> area) {
    if (area.isEmpty()) return;
    const auto rim = area.reduced(1.f);
    g.setColour(chassis().brighter(.3f).withAlpha(.5f));
    g.fillRoundedRectangle(area.translated(0, 1), 4.f);
    g.setColour(juce::Colour(0xff222921));
    g.fillRoundedRectangle(rim, 4.f);
    g.setColour(juce::Colour(0xff62675a));
    g.drawRoundedRectangle(rim.reduced(.5f), 4.f, 1.f);
    const auto inner = area.reduced(5.f);
    if (inner.isEmpty()) return;
    g.setGradientFill(juce::ColourGradient(lcd().darker(.12f), inner.getTopLeft(),
                                        lcd(), inner.getBottomLeft(), false));
    g.fillRect(inner);
    g.setColour(lcdInk().withAlpha(.3f));
    g.drawRect(inner, .8f);
    g.setColour(lcdInk().withAlpha(.045f));
    for (int y = static_cast<int>(inner.getY()) + 2; y < inner.getBottom(); y += 3)
        g.drawHorizontalLine(y, inner.getX() + 1.f, inner.getRight() - 1.f);
    g.setColour(lcd().brighter(.3f).withAlpha(.12f));
    g.fillRect(inner.withHeight(juce::jmin(3.f, inner.getHeight())));
}

juce::Font HardwareLookAndFeel::getTextButtonFont(juce::TextButton& button, int height) {
    const auto size = button.getWidth() <= height + 4 ? 13.f : juce::jlimit(10.5f, 12.5f, height * .36f);
    return labelFont(size);
}

void HardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& colour, bool over, bool down) {
    if (button.getWidth() < 5 || button.getHeight() < 5) return;
    const bool dark = isDarkKey(button);
    const bool on = button.getToggleState();
    auto cap = dark ? graphite().brighter(.1f) : (on ? button.findColour(juce::TextButton::buttonOnColourId) : colour);
    if (over) cap = cap.brighter(.07f);
    if (down) cap = cap.darker(.08f);
    if (!button.isEnabled()) cap = cap.withMultipliedSaturation(.3f).withMultipliedBrightness(.93f);
    const auto socket = button.getLocalBounds().toFloat().reduced(1.f);
    const auto face = keyFace(button, down);
    g.setColour(juce::Colours::black.withAlpha(.26f));
    g.fillRoundedRectangle(socket.translated(0, .7f), 3.f);
    g.setColour(graphite().darker(.35f));
    g.fillRoundedRectangle(socket.reduced(.5f), 3.f);
    g.setColour(cap.darker(.32f));
    g.fillRoundedRectangle(face.translated(0, down ? 1.f : 2.f), 2.3f);
    g.setGradientFill(juce::ColourGradient(cap.brighter(.1f), face.getTopLeft(), cap.darker(.045f),
                                        face.getBottomLeft(), false));
    g.fillRoundedRectangle(face, 2.3f);
    g.setColour(cap.brighter(dark ? .35f : .5f).withAlpha(.75f));
    g.drawLine(face.getX() + 3.f, face.getY() + 1.f, face.getRight() - 3.f, face.getY() + 1.f, .8f);
    g.setColour(cap.darker(.35f));
    g.drawRoundedRectangle(face.reduced(.2f), 2.2f, .7f);
    if (dark && button.getWidth() > 58 && button.getHeight() > 26) {
        const auto led = juce::Rectangle<float>(4.f, juce::jmin(12.f, face.getHeight() - 10.f))
                             .withCentre({ face.getX() + 8.f, face.getCentreY() });
        g.setColour(juce::Colours::black.withAlpha(.55f));
        g.fillRoundedRectangle(led.expanded(1.f), 1.5f);
        g.setColour((on ? button.findColour(juce::TextButton::buttonOnColourId) : trim().darker(.35f))
                        .withMultipliedAlpha(button.isEnabled() ? 1.f : .4f));
        g.fillRoundedRectangle(led, 1.f);
    }
    if (button.hasKeyboardFocus(true)) {
        g.setColour(amber());
        g.drawRoundedRectangle(socket.reduced(.6f), 3.f, 1.f);
    }
}

void HardwareLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) {
    const bool dark = isDarkKey(button);
    const auto colour = dark ? (button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId).brighter(.25f)
                                                      : chassis().brighter(.1f))
                            : button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                        : juce::TextButton::textColourOffId);
    auto area = keyFace(button, down).toNearestInt().reduced(3, 0);
    if (dark && button.getWidth() > 58 && button.getHeight() > 26) area.removeFromLeft(9);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.f : .38f));
    g.drawFittedText(button.getButtonText(), area, juce::Justification::centred, 2, .85f);
}

void HardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float position, float start, float end, juce::Slider& slider) {
    const float diameter = static_cast<float>(std::min(width, height));
    if (diameter < 5.f) return;
    const float radius = diameter * .33f;
    const juce::Point<float> centre(x + width * .5f, y + height * .5f);
    const auto knob = juce::Rectangle<float>(radius * 2.f, radius * 2.f).withCentre(centre);
    const auto enabledAlpha = slider.isEnabled() ? 1.f : .4f;
    for (int i = 0; i < 11; ++i) {
        const float angle = start + static_cast<float>(i) * (end - start) / 10.f;
        const bool major = i % 5 == 0;
        g.setColour(ink().withAlpha((major ? .8f : .45f) * enabledAlpha));
        g.drawLine({ centre.getPointOnCircumference(radius * 1.22f, angle),
                     centre.getPointOnCircumference(radius * (major ? 1.43f : 1.35f), angle) }, major ? 1.2f : .8f);
    }
    g.setColour(juce::Colours::black.withAlpha(.2f));
    g.fillEllipse(knob.expanded(2.f).translated(0, 2.f));
    g.setGradientFill(juce::ColourGradient(trim().brighter(.2f), knob.getTopLeft(),
                                        graphite().darker(.35f), knob.getBottomRight(), false));
    g.fillEllipse(knob.expanded(1.f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff555b51), knob.getTopLeft(),
                                        juce::Colour(0xff242b25), knob.getBottomRight(), false));
    g.fillEllipse(knob);
    for (int i = 0; i < 24; ++i) {
        const float angle = static_cast<float>(i) * juce::MathConstants<float>::twoPi / 24.f;
        g.setColour(juce::Colours::black.withAlpha(.24f));
        g.drawLine({ centre.getPointOnCircumference(radius * .84f, angle),
                     centre.getPointOnCircumference(radius * .98f, angle) }, .9f);
    }
    const auto cap = knob.reduced(radius * .2f);
    g.setGradientFill(juce::ColourGradient(graphite().brighter(.22f), cap.getTopLeft(), graphite(), cap.getBottomRight(), false));
    g.fillEllipse(cap);
    g.setColour(juce::Colours::black.withAlpha(.45f));
    g.drawEllipse(cap, .65f);
    const auto angle = start + position * (end - start);
    g.setColour((slider.isEnabled() ? chassis().brighter(.2f) : trim()).withAlpha(enabledAlpha));
    g.drawLine({ centre.getPointOnCircumference(radius * .35f, angle),
                 centre.getPointOnCircumference(radius * .86f, angle) }, juce::jmax(1.8f, radius * .12f));
}

void HardwareLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float position, float, float,
                                          const juce::Slider::SliderStyle style, juce::Slider& slider) {
    const bool vertical = slider.isVertical();
    const float centre = vertical ? x + width * .5f : y + height * .5f;
    const auto track = vertical ? juce::Rectangle<float>(centre - 3.f, static_cast<float>(y), 6.f, static_cast<float>(height))
                                : juce::Rectangle<float>(static_cast<float>(x), centre - 3.f, static_cast<float>(width), 6.f);
    if (track.isEmpty()) return;
    g.setColour(chassis().brighter(.25f).withAlpha(.6f));
    g.fillRoundedRectangle(track.expanded(1.f).translated(0, 1.f), 3.f);
    g.setColour(graphite().darker(.45f));
    g.fillRoundedRectangle(track, 2.f);
    g.setColour(juce::Colours::black.withAlpha(.45f));
    g.drawRoundedRectangle(track.reduced(.4f), 2.f, .7f);
    const bool bar = style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical;
    if (bar) {
        const auto fill = vertical ? track.withTop(position) : track.withRight(position);
        g.setColour(slider.findColour(juce::Slider::trackColourId).withAlpha(slider.isEnabled() ? 1.f : .35f));
        g.fillRoundedRectangle(fill, 2.f);
        return;
    }
    if (vertical && width > 22) {
        for (int i = 0; i < 11; ++i) {
            const float tickY = y + height * static_cast<float>(i) / 10.f;
            const float tick = i % 5 == 0 ? 6.f : 3.5f;
            g.setColour(ink().withAlpha(.45f));
            g.drawLine(centre - 10.f - tick, tickY, centre - 10.f, tickY, .8f);
            g.drawLine(centre + 10.f, tickY, centre + 10.f + tick, tickY, .8f);
        }
    }
    const float cross = vertical ? juce::jlimit(14.f, 28.f, width * .7f) : juce::jlimit(14.f, 24.f, height * .78f);
    const auto thumb = vertical ? juce::Rectangle<float>(centre - cross * .5f, position - 8.f, cross, 16.f)
                                : juce::Rectangle<float>(position - 8.f, centre - cross * .5f, 16.f, cross);
    g.setColour(juce::Colours::black.withAlpha(.35f));
    g.fillRoundedRectangle(thumb.translated(0, 2.f).expanded(.6f), 2.f);
    g.setGradientFill(juce::ColourGradient(graphite().brighter(.25f), thumb.getTopLeft(), graphite().darker(.25f), thumb.getBottomRight(), false));
    g.fillRoundedRectangle(thumb, 2.f);
    g.setColour(trim().withAlpha(.6f));
    g.drawRoundedRectangle(thumb.reduced(.5f), 2.f, .7f);
    for (float offset : { -4.f, 4.f }) {
        g.setColour(juce::Colours::black.withAlpha(.4f));
        if (vertical) g.drawLine(thumb.getX() + 3.f, position + offset, thumb.getRight() - 3.f, position + offset, .7f);
        else g.drawLine(position + offset, thumb.getY() + 3.f, position + offset, thumb.getBottom() - 3.f, .7f);
    }
    g.setColour(chassis().brighter(.1f).withAlpha(slider.isEnabled() ? 1.f : .4f));
    if (vertical) g.drawLine(thumb.getX() + 3.f, position, thumb.getRight() - 3.f, position, 1.7f);
    else g.drawLine(position, thumb.getY() + 3.f, position, thumb.getBottom() - 3.f, 1.7f);
}

void HardwareLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool down,
                                      int, int, int, int, juce::ComboBox& box) {
    const auto area = juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height)).reduced(.5f);
    g.setColour(graphite().darker(.25f));
    g.fillRoundedRectangle(area, 2.f);
    const auto display = area.reduced(2.f).withTrimmedRight(24.f);
    g.setGradientFill(juce::ColourGradient(lcd().darker(.09f), display.getTopLeft(), lcd(), display.getBottomLeft(), false));
    g.fillRect(display);
    g.setColour(chassis().brighter(.25f).withAlpha(.35f));
    g.drawLine(area.getX() + 2.f, area.getBottom(), area.getRight() - 2.f, area.getBottom(), .7f);
    const auto rocker = juce::Rectangle<float>(area.getRight() - 24.f, area.getY() + 2.f,
                                              22.f, juce::jmax(0.f, area.getHeight() - 4.f));
    g.setGradientFill(juce::ColourGradient(graphite().brighter(down ? .1f : .28f), rocker.getTopLeft(),
                                        graphite().darker(.1f), rocker.getBottomLeft(), false));
    g.fillRoundedRectangle(rocker, 1.f);
    g.setColour((box.isEnabled() ? chassis() : trim()).withAlpha(box.isEnabled() ? 1.f : .5f));
    juce::Path arrow;
    const auto c = rocker.getCentre();
    arrow.addTriangle(c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y - 1.5f, c.x, c.y + 2.5f);
    g.fillPath(arrow);
}

juce::Font HardwareLookAndFeel::getComboBoxFont(juce::ComboBox&) { return monoFont(12.f); }
void HardwareLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(7, 2, juce::jmax(0, box.getWidth() - 36), juce::jmax(0, box.getHeight() - 4));
    label.setFont(getComboBoxFont(box));
}
juce::Label* HardwareLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(monoFont(11.5f));
    label->setBorderSize(juce::BorderSize<int>(1, 3, 1, 3));
    return label;
}
juce::Font HardwareLookAndFeel::getPopupMenuFont() { return labelFont(13.f, false); }
} // namespace samplify
