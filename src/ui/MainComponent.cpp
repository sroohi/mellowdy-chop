#include "MainComponent.h"
#include "HardwareLookAndFeel.h"
#include "audio/SeratoMetadata.h"
#include "stems/StemSeparator.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <filesystem>

namespace samplify {
namespace {
const juce::Colour black(0xff29362b), top(0xffd6cbb5), strip(0xffbfb6a3), grey(0xff55584b), white(0xff393c34), yellow(0xffd78936);
constexpr const char* stemNames[] { "VOCALS", "MELODY", "BASS", "DRUMS" };
constexpr const char* typingKeys = "awsedftgyhujkolp";
juce::Font face(float size, bool bold = false) { return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)); }
void moduleLabel(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, float size, juce::Colour colour = grey, int alignment = juce::Justification::centred) {
    g.setColour(colour); g.setFont(face(size)); g.drawText(s, r, alignment);
}
juce::String formattedKey(const std::string& key, int offset, int format) {
    constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto text = juce::String(key).trim();
    if (text.isEmpty() || text.equalsIgnoreCase("Unknown")) return "--";
    const auto letter = juce::CharacterFunctions::toUpperCase(text[0]);
    int pitch = -1;
    switch (letter) { case 'C': pitch = 0; break; case 'D': pitch = 2; break; case 'E': pitch = 4; break; case 'F': pitch = 5; break; case 'G': pitch = 7; break; case 'A': pitch = 9; break; case 'B': pitch = 11; break; default: return "--"; }
    if (text.length() > 1) { if (text[1] == '#' || text[1] == 0x266f) ++pitch; else if (text[1] == 'b' || text[1] == 0x266d) --pitch; }
    const bool minor = text.containsIgnoreCase("minor") || text.containsIgnoreCase("min") || text.endsWithChar('m');
    pitch = ((pitch + offset) % 12 + 12) % 12;
    constexpr int camelotMajor[] { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
    constexpr int camelotMinor[] { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
    constexpr int openMajor[] { 1, 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6 };
    constexpr int openMinor[] { 10, 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3 };
    if (format == 1) return juce::String(minor ? camelotMinor[pitch] : camelotMajor[pitch]) + (minor ? "A" : "B");
    if (format == 2) return juce::String(minor ? openMinor[pitch] : openMajor[pitch]) + (minor ? "m" : "d");
    return juce::String(names[pitch]) + (minor ? "m" : "");
}

int bitCount(std::uint32_t mask) { int n = 0; for (; mask; mask &= mask - 1) ++n; return n; }
void restoreGrid(Project& target, const Project& from) { target.sourceBpm = from.sourceBpm; target.tempoMarkers = from.tempoMarkers; target.beatgridEnabled = from.beatgridEnabled; }
}

class MainComponent::SamplerLookAndFeel final : public HardwareLookAndFeel {
public:
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool down) override {
        const auto icon = b.getProperties().getWithDefault("icon", "").toString();
        if (icon.isEmpty()) { HardwareLookAndFeel::drawButtonText(g, b, over, down); return; }
        const auto bounds = b.getLocalBounds().toFloat().reduced(b.getHeight() * .2f);
        const auto c = bounds.getCentre(); const float r = std::min(bounds.getWidth(), bounds.getHeight()) * .45f;
        g.setColour((b.getToggleState() ? b.findColour(juce::TextButton::textColourOnId) : b.findColour(juce::TextButton::textColourOffId)).withMultipliedAlpha(b.isEnabled() ? 1.f : .3f));
        juce::Path path;
        if (icon == "reverse" || icon == "play") {
            if (icon == "play" && b.getButtonText() == "II") { g.fillRect(c.x - r * .6f, c.y - r, r * .35f, r * 2); g.fillRect(c.x + r * .25f, c.y - r, r * .35f, r * 2); return; }
            const float direction = icon == "reverse" ? -1.f : 1.f;
            path.addTriangle(c.x + direction * r, c.y, c.x - direction * r * .7f, c.y - r, c.x - direction * r * .7f, c.y + r); g.fillPath(path);
        } else if (icon == "favorite") { path.addStar(c, 5, r * .48f, r); g.fillPath(path); }
        else if (icon == "options") {
            for (int i = 0; i < 8; ++i) { const auto p = c.getPointOnCircumference(r * .8f, i * juce::MathConstants<float>::twoPi / 8); g.fillEllipse(p.x - r * .23f, p.y - r * .23f, r * .46f, r * .46f); }
            g.drawEllipse(c.x - r * .56f, c.y - r * .56f, r * 1.12f, r * 1.12f, r * .28f);
        } else if (icon == "keyboard") {
            g.drawRect(bounds, 1.5f);
            for (int i = 1; i < 7; ++i) g.drawVerticalLine(static_cast<int>(bounds.getX() + bounds.getWidth() * i / 7), bounds.getY(), bounds.getBottom());
            for (int i : { 1, 2, 4, 5, 6 }) g.fillRect(bounds.getX() + bounds.getWidth() * i / 7 - bounds.getWidth() * .025f, bounds.getY(), bounds.getWidth() * .05f, bounds.getHeight() * .6f);
        } else if (icon == "pitchtime") {
            const float side = r * 1.15f;
            g.drawRect(c.x - side * 1.28f, c.y - side * .5f, side, side, 2.5f);
            g.drawRect(c.x + side * .28f, c.y - side * .5f, side, side, 2.5f);
            g.drawLine(c.x - side * .28f, c.y, c.x + side * .28f, c.y, 2.5f);
        } else if (icon == "delete") {
            g.drawRect(c.x - r * .62f, c.y - r * .55f, r * 1.24f, r * 1.4f, 1.4f);
            g.drawLine(c.x - r * .8f, c.y - r * .7f, c.x + r * .8f, c.y - r * .7f, 1.4f);
            g.drawLine(c.x - r * .2f, c.y - r, c.x + r * .2f, c.y - r, 1.4f);
        } else if (icon == "undo" || icon == "redo") {
            const float direction = icon == "undo" ? 1.f : -1.f;
            path.startNewSubPath(c.x + direction * r * .6f, c.y + r * .5f);
            path.quadraticTo(c.x + direction * r * .7f, c.y - r, c.x - direction * r * .6f, c.y - r * .4f);
            g.strokePath(path, juce::PathStrokeType(2));
            path.clear(); path.addTriangle(c.x - direction * r * .85f, c.y - r * .35f, c.x - direction * r * .35f, c.y - r * .8f, c.x - direction * r * .25f, c.y); g.fillPath(path);
        } else HardwareLookAndFeel::drawButtonText(g, b, over, down);
    }
};

class ModeButton final : public juce::Button {
public:
    ModeButton(juce::String first, juce::String second) : Button(first), upper(std::move(first)), lower(std::move(second)) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1);
        g.setColour(down ? HardwareLookAndFeel::panel().darker(.12f) : over ? HardwareLookAndFeel::panel().brighter(.08f) : HardwareLookAndFeel::panel());
        g.fillRoundedRectangle(r, 3); g.setColour(HardwareLookAndFeel::trim()); g.drawRoundedRectangle(r, 3, 1);
        g.setColour(HardwareLookAndFeel::amber()); g.fillEllipse(r.getX() + 6, r.getCentreY() - 2, 4, 4);
        moduleLabel(g, getToggleState() ? upper : lower, r.withTrimmedLeft(11), 10, HardwareLookAndFeel::ink());
    }
private:
    juce::String upper, lower;
};

class StemButton final : public juce::Button {
public:
    explicit StemButton(int i) : Button(stemNames[i]), index(i) { setTooltip(juce::String(stemNames[i]) + ": click mute; Shift-click solo"); }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        g.setColour(juce::Colour(over ? 0xffded6c5 : 0xffc5bcaa)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 2);
        const auto colour = getToggleState() ? grey.withAlpha(.4f) : black;
        g.setColour(colour.withMultipliedAlpha(isEnabled() ? 1.f : .3f));
        const float w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight());
        const float cx = w * .5f, cy = h * .36f, unit = std::min(w, h) * .24f;
        if (index == 0) {
            g.fillRoundedRectangle(cx - unit * .25f, cy - unit * .6f, unit * .5f, unit, unit * .25f);
            g.drawRoundedRectangle(cx - unit * .43f, cy - unit * .18f, unit * .86f, unit * .86f, unit * .4f, 1.5f);
            g.drawLine(cx, cy + unit * .65f, cx, cy + unit * .95f, 2);
        } else if (index == 1) {
            for (int k = 0; k < 5; ++k) g.fillRect(cx - unit + k * unit * .42f, cy - unit * .65f, unit * .29f, unit * 1.4f);
            g.setColour(juce::Colour(0xff2b2b2b));
            for (int k : { 0, 1, 3 }) g.fillRect(cx - unit * .73f + k * unit * .42f, cy - unit * .65f, unit * .15f, unit * .82f);
        } else if (index == 2) {
            g.drawLine(cx - unit * .7f, cy + unit * .65f, cx + unit * .75f, cy - unit * .8f, unit * .21f);
            g.fillEllipse(cx - unit * .85f, cy + unit * .05f, unit, unit * .83f);
            g.fillEllipse(cx - unit * .28f, cy - unit * .14f, unit * .66f, unit * .65f);
        } else {
            g.drawEllipse(cx - unit * .8f, cy - unit * .05f, unit * 1.6f, unit * .55f, 2);
            g.drawLine(cx - unit * .8f, cy + unit * .2f, cx - unit * .7f, cy + unit * .7f, 2);
            g.drawLine(cx + unit * .8f, cy + unit * .2f, cx + unit * .7f, cy + unit * .7f, 2);
            g.drawLine(cx - unit * .7f, cy + unit * .7f, cx + unit * .7f, cy + unit * .7f, 2);
            g.drawLine(cx - unit * .7f, cy - unit * .8f, cx + unit * .2f, cy - unit * .1f, 2);
            g.drawLine(cx + unit * .8f, cy - unit * .8f, cx - unit * .2f, cy - unit * .1f, 2);
        }
        moduleLabel(g, stemNames[index], { 2, h - 23, w - 4, 18 }, 10, colour);
    }
private: int index;
};

class CuePad final : public juce::Component, public juce::DragAndDropTarget {
public:
    int index = 0;
    Pad pad;
    bool selected = false, active = false, dragOver = false;
    std::function<void(int, const juce::ModifierKeys&)> onPress;
    std::function<void(int)> onRelease, onRename;
    std::function<void(int, int, bool)> onMove;
    void paint(juce::Graphics& g) override {
        using H = HardwareLookAndFeel;
        auto socket = getLocalBounds().toFloat().reduced(1);
        g.setColour(H::graphite().darker(.35f)); g.fillRoundedRectangle(socket, 8);
        const auto r = socket.reduced(4).translated(0, active ? 1.f : 0.f);
        const auto rubber = active ? H::graphite().brighter(.22f) : H::graphite();
        g.setColour(H::ink().withAlpha(.45f)); g.fillRoundedRectangle(r.translated(0, 2), 6);
        g.setGradientFill(juce::ColourGradient(rubber.brighter(.07f), r.getX(), r.getY(), rubber.darker(.09f), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle(r, 6);
        g.setColour(H::chassis().withAlpha(.1f)); g.drawRoundedRectangle(r.reduced(.5f), 6, 1);
        if (selected || dragOver || active) { g.setColour(H::amber().withAlpha(active || dragOver ? 1.f : .8f)); g.drawRoundedRectangle(r.reduced(1), 5, active || dragOver ? 2.2f : 1.4f); }
        const auto ink = H::chassis().brighter(.12f);
        const auto accent = pad.empty() ? H::trim() : juce::Colour(pad.colour);
        g.setColour(accent.withAlpha(pad.empty() ? .45f : 1.f)); g.fillRoundedRectangle(r.getX() + 10, r.getY() + 10, 21, 3, 1);
        moduleLabel(g, juce::String(index / 16 == 0 ? "A" : "B") + juce::String(index % 16 + 1).paddedLeft('0', 2), {r.getX() + 10, r.getY() + 19, r.getWidth() - 20, 16}, 10, ink.withAlpha(.55f), juce::Justification::centredLeft);
        moduleLabel(g, juce::String(index % 16 + 1).paddedLeft('0', 2), r.withTrimmedTop(r.getHeight() * .26f).withHeight(r.getHeight() * .4f), std::clamp(r.getHeight() * .3f, 22.f, 40.f), ink.withAlpha(pad.empty() ? .3f : .9f));
        moduleLabel(g, pad.empty() ? "EMPTY" : pad.name.empty() ? "CUE " + juce::String(index + 1) : juce::String(pad.name), {r.getX() + 8, r.getBottom() - 29, r.getWidth() - 31, 17}, 9, ink.withAlpha(.7f), juce::Justification::centredLeft);
        moduleLabel(g, juce::String::charToString(typingKeys[index % 16]).toUpperCase(), {r.getRight() - 22, r.getBottom() - 28, 15, 17}, 10, ink.withAlpha(.48f));
        if (pad.favorite) { juce::Path star; star.addStar({r.getRight() - 14, r.getY() + 14}, 5, 2.5f, 5); g.setColour(H::amber()); g.fillPath(star); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragging = false;
        if (e.mods.isPopupMenu()) { if (onRename) onRename(index); return; }
        if (onPress) onPress(index, e.mods);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragging || pad.empty() || e.getDistanceFromDragStart() < 7) return;
        dragging = true;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) container->startDragging(index, this);
    }
    void mouseUp(const juce::MouseEvent&) override { if (onRelease) onRelease(index); }
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.isInt(); }
    void itemDragEnter(const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { dragOver = false; repaint(); }
    void itemDropped(const SourceDetails& d) override {
        dragOver = false; repaint();
        if (onMove) onMove(static_cast<int>(d.description), index, juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown());
    }
private: bool dragging = false;
};

MainComponent::MainComponent() {
    theme = std::make_unique<SamplerLookAndFeel>(); setLookAndFeel(theme.get());
    setWantsKeyboardFocus(true); addMouseListener(this, true);
    tooltips = std::make_unique<juce::TooltipWindow>(this, 650);
    holdMode = std::make_unique<ModeButton>("HOLD", "TRIGGER");
    voiceMode = std::make_unique<ModeButton>("MONO", "POLY");
    keyMode = std::make_unique<ModeButton>("KEY", "DETUNE");
    for (auto* c : std::initializer_list<juce::Component*> {
        &overview, &waveform, holdMode.get(), voiceMode.get(), keyMode.get(), &bankA, &bankB, &bankInfo, &cueSummary, &moduleInfo,
        &undoButton, &redoButton, &velocityButton, &quantizeButton, &settingsButton,
        &playButton, &ejectButton, &syncButton, &bpmDouble, &bpmHalf, &pitchTimeButton,
        &loadButton, &cancelButton, &reverseButton, &favoriteButton, &optionsButton, &speedDouble, &speedHalf,
        &autoSetButton, &autoModeButton, &selectAllButton, &deleteButton, &keyboardButton, &sliceLeft, &sliceRight,
        &sliceSize, &keyStep, &outputRouting, &glideMode, &trackTitle, &trackInfo,
        &levelLabel, &filterLabel, &attackLabel, &releaseLabel, &speedLabel, &cuePitchLabel,
        &optionsInfo, &outputLabel, &glideLabel, &statusLabel, &globalPitch, &globalDetune, &currentBpm,
        &level, &filter, &attack, &release, &speed, &cuePitch, &glide,
        &gridPanel, &gridBpm, &gridSave, &gridCancel, &gridTap, &gridDouble, &gridHalf, &metronome,
        &gridStart, &gridAdjustLeft, &gridAdjustRight, &gridSlipLeft, &gridSlipRight, &gridSet, &gridClear, &gridClearAll }) addAndMakeVisible(c);
    constexpr const char* modules[] { "TRIM", "TUNE", "ENVELOPE", "STEMS", "GRID" };
    for (int i = 0; i < 5; ++i) { auto& button = moduleButtons[static_cast<std::size_t>(i)]; button.setButtonText(modules[i]); button.getProperties().set("hardwareRole", "mode"); button.onClick = [this, i] { showSampleModule(static_cast<SampleModule>(i)); }; addAndMakeVisible(button); }
    bankA.onClick = [this] { setPadBank(0); }; bankB.onClick = [this] { setPadBank(1); };
    for (int i = 0; i < 32; ++i) {
        auto& pad = pads[static_cast<std::size_t>(i)]; pad = std::make_unique<CuePad>(); pad->index = i;
        pad->onPress = [this](int p, const juce::ModifierKeys& mods) { selectPad(p, mods, true); grabKeyboardFocus(); };
        pad->onRelease = [this](int p) { releaseCue(p); };
        pad->onMove = [this](int from, int to, bool copy) { movePad(from, to, copy); };
        pad->onRename = [this](int p) { renameCue(p); };
        addAndMakeVisible(*pad);
    }
    for (int i = 0; i < 4; ++i) {
        auto& b = stemButtons[static_cast<std::size_t>(i)]; b = std::make_unique<StemButton>(i);
        b->onClick = [this, i] { requestStem(i, juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()); };
        addAndMakeVisible(*b);
    }
    auto outline = [](juce::Button& b) { b.getProperties().set("outline", true); };
    for (auto* b : { &undoButton, &redoButton, &velocityButton, &quantizeButton, &autoSetButton, &autoModeButton, &selectAllButton, &deleteButton, &keyboardButton, &loadButton, &gridStart, &gridAdjustLeft, &gridAdjustRight, &gridSlipLeft, &gridSlipRight, &gridSet, &gridClear, &gridClearAll, &gridSave, &gridCancel, &metronome }) outline(*b);
    for (auto* b : { &velocityButton, &quantizeButton, &syncButton, &pitchTimeButton, &reverseButton, &favoriteButton, &optionsButton, &keyboardButton, &metronome }) b->setClickingTogglesState(true);
    undoButton.setTooltip("Undo (Command-Z) - 100 actions"); redoButton.setTooltip("Redo (Command-Shift-Z)");
    velocityButton.setButtonText("VEL"); quantizeButton.setButtonText("QNT");
    velocityButton.setTooltip("Use MIDI note velocity"); quantizeButton.setTooltip("Snap new cue starts and ends to 1/8 beat");
    pitchTimeButton.setTooltip("Independent pitch and time processing"); syncButton.setTooltip("Match source playback BPM to the studio tempo");
    playButton.setTooltip("Audition / Pause the sample source"); keyboardButton.setTooltip("Play the selected cue chromatically");
    autoSetButton.setTooltip("Populate cues using the selected Autoset mode; favorites are retained");
    reverseButton.setTooltip("Reverse selected cues"); favoriteButton.setTooltip("Protect selected cues from Autoset");
    optionsButton.setTooltip("Cue number, MIDI note and output routing");
    loadButton.setColour(juce::TextButton::buttonColourId, black);
    loadButton.setColour(juce::TextButton::textColourOffId, yellow);
    for (auto pair : std::initializer_list<std::pair<juce::TextButton*, const char*>> { { &undoButton, "undo" }, { &redoButton, "redo" }, { &playButton, "play" }, { &reverseButton, "reverse" }, { &favoriteButton, "favorite" }, { &optionsButton, "options" }, { &keyboardButton, "keyboard" }, { &deleteButton, "delete" }, { &pitchTimeButton, "pitchtime" } }) pair.first->getProperties().set("icon", pair.second);
    playButton.getProperties().set("icon", ""); playButton.setButtonText("AUDITION");
    pitchTimeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2b2b2b));
    pitchTimeButton.setColour(juce::TextButton::textColourOnId, yellow);
    for (auto* button : { &reverseButton, &favoriteButton, &optionsButton }) {
        button->setColour(juce::TextButton::buttonColourId, strip); button->setColour(juce::TextButton::buttonOnColourId, strip);
        button->setColour(juce::TextButton::textColourOnId, yellow);
    }
    keyMode->setToggleState(true, juce::dontSendNotification);
    auto knob = [this](juce::Slider& s, double lo, double hi, double step, juce::Label& l, const char* name) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag); s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(lo, hi, step); s.setPopupDisplayEnabled(true, true, this);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        l.setText(name, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred); l.setColour(juce::Label::textColourId, grey);
    };
    knob(level, -12, 12, .1, levelLabel, "Level"); knob(filter, 0, 1, .001, filterLabel, "Filter");
    knob(attack, 0, 10000, 1, attackLabel, "Attack"); knob(release, 0, 10000, 1, releaseLabel, "Release");
    knob(glide, 0, 5000, .01, glideLabel, "Glide");
    level.setTextValueSuffix(" dB");
    filter.textFromValueFunction = [](double v) { if (std::abs(v - .5) < .002) return juce::String("OFF"); const double f = v < .5 ? 80 * std::pow(17000. / 80, v * 2) : 80 * std::pow(17000. / 80, (v - .5) * 2); return juce::String(v < .5 ? "LP " : "HP ") + (f >= 1000 ? juce::String(f / 1000, 1) + " kHz" : juce::String(f, 0) + " Hz"); };
    for (auto* s : { &attack, &release, &glide }) s->textFromValueFunction = [](double v) { return v >= 1000 ? juce::String(v / 1000, 2) + " s" : juce::String(v, 1) + " ms"; };
    auto numeric = [](juce::Slider& s, double lo, double hi, double step) {
        s.setSliderStyle(juce::Slider::IncDecButtons); s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 96, 32);
        s.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical); s.setRange(lo, hi, step);
    };
    numeric(globalPitch, -24, 24, 1); numeric(globalDetune, -100, 100, 1); numeric(currentBpm, 30, 300, .01);
    numeric(speed, .25, 4, .01); numeric(cuePitch, -24, 24, 1); numeric(gridBpm, 30, 300, .01);
    cuePitch.textFromValueFunction = [this](double value) { return juce::String(value + project.globalPitchSemitones, 0); };
    cuePitch.valueFromTextFunction = [this](const juce::String& value) { return value.getDoubleValue() - project.globalPitchSemitones; };
    globalPitch.textFromValueFunction = [this](double v) { return formattedKey(project.sourceKey, static_cast<int>(v), keyDisplayFormat) + "  " + juce::String(v, 0); };
    globalPitch.setTooltip("Alt-click to cycle Traditional / Camelot / OpenKey notation");
    globalDetune.setTextValueSuffix(" ct"); currentBpm.setNumDecimalPlacesToDisplay(2); gridBpm.setNumDecimalPlacesToDisplay(2);
    speed.textFromValueFunction = [](double v) { return juce::String((v - 1) * 100, 0) + "%"; };
    speed.valueFromTextFunction = [](const juce::String& t) { return 1 + t.getDoubleValue() / 100; };
    speedLabel.setText("Time Stretch", juce::dontSendNotification); cuePitchLabel.setText("Key Shift", juce::dontSendNotification);
    outputLabel.setText("Output", juce::dontSendNotification);
    for (auto* l : { &speedLabel, &cuePitchLabel, &outputLabel, &glideLabel }) { l->setJustificationType(juce::Justification::centred); l->setColour(juce::Label::textColourId, grey); }
    constexpr double sizes[] { .0625, .125, .25, .5, 1, 2, 4, 8, 16 };
    for (int i = 0; i < 9; ++i) sliceSize.addItem(juce::String(sizes[i], sizes[i] < 1 ? 4 : 0).trimCharactersAtEnd("0.") + " beats", i + 1);
    sliceSize.setSelectedId(5, juce::dontSendNotification);
    keyStep.addItem("1 semitone", 1); keyStep.addItem("2 semitones", 2); keyStep.setSelectedId(1, juce::dontSendNotification);
    for (int i = 0; i < 32; ++i) outputRouting.addItem("Channel " + juce::String(i + 1), i + 1);
    outputRouting.setTooltip("Stereo hardware pair; unavailable outputs fall back to the main pair");
    glideMode.addItem("Trigger", 1); glideMode.addItem("Sustain", 2); glideMode.setSelectedId(1, juce::dontSendNotification);
    auto cueParameter = [this](juce::Slider& s, auto apply) {
        s.onDragStart = [this] { if (!refreshing) gestureBefore = project; };
        s.onDragEnd = [this] { if (gestureBefore) { pushUndo(*gestureBefore); gestureBefore.reset(); } };
        s.onValueChange = [this, &s, apply] {
            if (refreshing || busy || !source) return;
            const auto before = project; const auto mask = selection & occupiedMask();
            for (int i = 0; i < 32; ++i) if (mask & (std::uint32_t(1) << i)) apply(project.pads[static_cast<std::size_t>(i)], s.getValue());
            if (!gestureBefore) pushUndo(before);
            publishProject(false);
        };
    };
    cueParameter(level, [](Pad& p, double v) { p.levelDb = static_cast<float>(v); });
    cueParameter(filter, [](Pad& p, double v) { p.filter = static_cast<float>(v); });
    cueParameter(attack, [](Pad& p, double v) { p.attackMs = static_cast<float>(v); });
    cueParameter(release, [](Pad& p, double v) { p.releaseMs = static_cast<float>(v); });
    cueParameter(speed, [](Pad& p, double v) { p.speedRatio = static_cast<float>(v); });
    cueParameter(cuePitch, [](Pad& p, double v) { p.pitchSemitones = static_cast<float>(v); });
    auto globalParameter = [this](juce::Slider& s, auto apply) {
        s.onDragStart = [this, &s] { if (!refreshing && !(&s == &globalPitch && (keyCycleGesture || juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown()))) gestureBefore = project; };
        s.onDragEnd = [this, &s] { if (&s == &globalPitch && keyCycleGesture) { gestureBefore.reset(); return; } if (gestureBefore) { pushUndo(*gestureBefore); gestureBefore.reset(); } };
        s.onValueChange = [this, &s, apply] {
            if (&s == &globalPitch && (keyCycleGesture || juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown())) { s.setValue(project.globalPitchSemitones, juce::dontSendNotification); s.updateText(); return; }
            if (!refreshing && !busy && source) { const auto before = project; apply(project, s.getValue()); if (!gestureBefore) pushUndo(before); publishProject(false); }
        };
    };
    globalParameter(globalPitch, [](Project& p, double v) { p.globalPitchSemitones = static_cast<float>(v); });
    globalParameter(globalDetune, [](Project& p, double v) { p.globalDetuneCents = static_cast<float>(v); });
    globalParameter(currentBpm, [](Project& p, double v) { p.projectBpm = v; });
    globalParameter(glide, [](Project& p, double v) { p.glideMs = static_cast<float>(v); });
    undoButton.onClick = [this] { undo(); }; redoButton.onClick = [this] { redo(); };
    settingsButton.onClick = [this] { showAudioSettings(); }; loadButton.onClick = [this] { importAudio(); };
    cancelButton.onClick = [this] { cancelled.store(true); status = "Cancelling analysis..."; statusLabel.setText(status, juce::dontSendNotification); };
    velocityButton.onClick = [this] { editProject([](Project& p) { p.velocityEnabled = !p.velocityEnabled; }); };
    quantizeButton.onClick = [this] { editProject([](Project& p) { p.quantize = !p.quantize; }); };
    holdMode->onClick = [this] { editProject([](Project& p) { p.playbackMode = p.playbackMode == CuePlaybackMode::Hold ? CuePlaybackMode::Trigger : CuePlaybackMode::Hold; }); };
    voiceMode->onClick = [this] { editProject([](Project& p) { p.mono = !p.mono; }); };
    keyMode->onClick = [this] { resized(); };
    playButton.onClick = [this] { if (playback.sourcePlaying) engine.pauseSource(); else engine.playSource(); };
    ejectButton.onClick = [this] { ejectSource(); };
    syncButton.onClick = [this] { editProject([this](Project& p) { p.syncTempo = !p.syncTempo; if (p.syncTempo) p.projectBpm = hostBpm; }); };
    bpmDouble.onClick = [this] { editProject([](Project& p) { p.projectBpm = std::min(300., p.projectBpm * 2); }); };
    bpmHalf.onClick = [this] { editProject([](Project& p) { p.projectBpm = std::max(30., p.projectBpm * .5); }); };
    pitchTimeButton.onClick = [this] { editProject([](Project& p) { p.pitchTimeEnabled = !p.pitchTimeEnabled; if (!p.pitchTimeEnabled) p.syncTempo = false; }); };
    reverseButton.onClick = [this] { const bool state = reverseButton.getToggleState(); editSelected([state](Pad& p) { p.reverse = state; }); };
    favoriteButton.onClick = [this] { const bool state = favoriteButton.getToggleState(); editSelected([state](Pad& p) { p.favorite = state; }); };
    speedDouble.onClick = [this] { editSelected([](Pad& p) { p.speedRatio = std::min(4.f, p.speedRatio * 2); }); };
    speedHalf.onClick = [this] { editSelected([](Pad& p) { p.speedRatio = std::max(.25f, p.speedRatio * .5f); }); };
    optionsButton.onClick = [this] { optionsOpen = optionsButton.getToggleState(); refreshControls(); resized(); repaint(); };
    keyboardButton.onClick = [this] { editProject([](Project& p) { p.keyboardMode = !p.keyboardMode; }); grabKeyboardFocus(); };
    autoSetButton.onClick = [this] { runAutoSet(); }; autoModeButton.onClick = [this] { chooseAutoSetMode(); };
    selectAllButton.onClick = [this] { selectAll(); }; deleteButton.onClick = [this] { deleteSelection(); };
    sliceLeft.onClick = [this] { shiftSlicer(-1); }; sliceRight.onClick = [this] { shiftSlicer(1); };
    sliceSize.onChange = [this] { if (!refreshing) { constexpr double values[] { .0625, .125, .25, .5, 1, 2, 4, 8, 16 }; sliceBeats = values[juce::jlimit(0, 8, sliceSize.getSelectedId() - 1)]; if (autoMode == ChopMode::Grid) runAutoSet(true); } };
    keyStep.onChange = [this] { keyShiftStep = keyStep.getSelectedId(); };
    outputRouting.onChange = [this] { if (!refreshing) { const int output = outputRouting.getSelectedId() - 1; editSelected([output](Pad& p) { p.outputChannel = output; }); if (onStudioTrackChanged) onStudioTrackChanged(output); } };
    glideMode.onChange = [this] { if (!refreshing) editProject([this](Project& p) { p.glideMode = glideMode.getSelectedId() == 2 ? GlideMode::Sustain : GlideMode::Trigger; }); };
    overview.onSeek = [this](std::size_t frame) { engine.seekSource(frame); waveform.centreOn(frame); };
    waveform.onSeek = [this](std::size_t frame) { engine.seekSource(frame); };
    waveform.onCueSelected = [this](int p) { selectPad(p, {}, false); };
    waveform.onCueEdited = [this](int i, std::size_t start, std::size_t end, bool hasEnd) {
        if (!source || busy) return;
        editProject([=, this](Project& p) { auto& cue = p.pads[static_cast<std::size_t>(i)]; cue.start = snapCueFrame(p, start, source->sampleRate, source->frames()); cue.end = hasEnd && end < source->frames() ? snapCueFrame(p, end, source->sampleRate, source->frames()) : source->frames(); cue.end = std::max(cue.start + 1, cue.end); cue.hasEnd = hasEnd; });
    };
    waveform.onGridDrag = [this](std::size_t from, std::size_t to, bool slip) { gridDrag(from, to, slip); };
    // SourceWaveform owns the hover Zoom / Freeze / Grid buttons.
    waveform.onGridEditChanged = [this](bool value) { toggleGridEdit(value); };
    gridSave.onClick = [this] { if (gridDirty) { auto before = project; restoreGrid(before, gridBackup); pushUndo(before); } gridEditing = false; gridDirty = false; activeModule = SampleModule::Trim; metronomeOn = false; metronome.setToggleState(false, juce::dontSendNotification); engine.setMetronome(false); waveform.setGridEditMode(false); refreshControls(); resized(); repaint(); };
    gridCancel.onClick = [this] { restoreGrid(project, gridBackup); gridEditing = false; gridDirty = false; activeModule = SampleModule::Trim; metronomeOn = false; metronome.setToggleState(false, juce::dontSendNotification); engine.setMetronome(false); waveform.setGridEditMode(false); publishProject(); };
    gridBpm.onValueChange = [this] { if (!refreshing && gridEditing) applyGridBpm(gridBpm.getValue()); };
    gridTap.onClick = [this] { tapTempo(); };
    gridDouble.onClick = [this] { applyGridBpm(std::min(300., project.sourceBpm * 2)); };
    gridHalf.onClick = [this] { applyGridBpm(std::max(30., project.sourceBpm * .5)); };
    metronome.onClick = [this] { metronomeOn = metronome.getToggleState(); engine.setMetronome(metronomeOn); };
    gridStart.onClick = [this] { gridEdit([this](Project& p) { p.beatgridEnabled = true; p.tempoMarkers = { { playback.sourceFrame, p.sourceBpm } }; }); };
    gridSet.onClick = [this] { gridEdit([this](Project& p) { if (p.tempoMarkers.size() >= 128) return; p.beatgridEnabled = true; const auto frame = playback.sourceFrame; auto it = std::find_if(p.tempoMarkers.begin(), p.tempoMarkers.end(), [frame](const TempoMarker& m) { return m.frame == frame; }); if (it == p.tempoMarkers.end()) p.tempoMarkers.push_back({ frame, p.sourceBpm }); }); };
    gridClear.onClick = [this] { gridEdit([this](Project& p) { if (p.tempoMarkers.empty()) { p.beatgridEnabled = false; return; } const auto frame = playback.sourceFrame; auto it = std::min_element(p.tempoMarkers.begin(), p.tempoMarkers.end(), [frame](const TempoMarker& a, const TempoMarker& b) { return std::abs(static_cast<double>(a.frame) - frame) < std::abs(static_cast<double>(b.frame) - frame); }); p.tempoMarkers.erase(it); if (p.tempoMarkers.empty()) p.beatgridEnabled = false; }); };
    gridClearAll.onClick = [this] { gridEdit([](Project& p) { p.tempoMarkers.clear(); p.beatgridEnabled = false; }); };
    gridAdjustLeft.onClick = [this] { applyGridBpm(std::max(30., project.sourceBpm - .1)); };
    gridAdjustRight.onClick = [this] { applyGridBpm(std::min(300., project.sourceBpm + .1)); };
    gridSlipLeft.onClick = [this] { if (source) { const auto step = static_cast<std::size_t>(source->sampleRate * .001); gridDrag(step, 0, true); } };
    gridSlipRight.onClick = [this] { if (source) { const auto step = static_cast<std::size_t>(source->sampleRate * .001); gridDrag(0, step, true); } };
    trackTitle.setColour(juce::Label::textColourId, HardwareLookAndFeel::lcdInk()); trackTitle.setFont(face(20)); trackInfo.setFont(face(13)); trackInfo.setColour(juce::Label::textColourId, HardwareLookAndFeel::lcdInk());
    statusLabel.setFont(face(11)); statusLabel.setColour(juce::Label::textColourId, grey);
    progressBar = std::make_unique<juce::ProgressBar>(progressDisplay); addAndMakeVisible(*progressBar);
    audioWarning = engine.initialise(); engine.setMasterGain(.8f);
    status = "Load audio to begin";
    // setSize calls resized; every child must exist first.
    setSize(1230, 700); refreshControls(); startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer(); cancelled.store(true); waveformCancelled.store(true); engine.stop();
    if (worker.joinable()) worker.join();
    if (waveformWorker.joinable()) waveformWorker.join();
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g) {
    using H = HardwareLookAndFeel;
    g.fillAll(H::chassis());
    H::paintModule(g, padCabinet.toFloat(), "PERFORMANCE PADS", false);
    H::paintModule(g, sampleDeck.toFloat(), "SAMPLING DECK", false);
    const bool loaded = source && source->frames() > 0;
    if (!sampleDeck.isEmpty()) {
        const auto lcd = juce::Rectangle<int>(sampleDeck.getX() + 12, 35, sampleDeck.getWidth() - 24, 54);
        H::paintLCD(g, lcd.toFloat());
        if (loaded) {
            H::paintBezel(g, overview.getBounds().toFloat().expanded(3), true);
            H::paintBezel(g, waveform.getBounds().toFloat().expanded(3), true);
        } else {
            auto blank = juce::Rectangle<int>(sampleDeck.getX() + 15, 101, sampleDeck.getWidth() - 30, std::max(100, parameterDeck.getY() - 151));
            H::paintLCD(g, blank.toFloat());
            moduleLabel(g, busy ? "READING AUDIO" : "NO SAMPLE LOADED", blank.toFloat().reduced(20).withHeight(35).translated(0, 12), 18, H::lcdInk());
            moduleLabel(g, "Drop an audio file onto the sampling deck", blank.toFloat().withTrimmedTop(50).withHeight(30), 11, H::lcdInk().withAlpha(.75f));
        }
        H::paintBezel(g, parameterDeck.toFloat());
    }
    const auto localMask = std::uint32_t(0xffff) << (activeBank * 16);
    moduleLabel(g, "A W S E  /  D F T G  /  Y H U J  /  K O L P", { static_cast<float>(padCabinet.getX() + 15), static_cast<float>(getHeight() - 27), static_cast<float>(padCabinet.getWidth() - 30), 16 }, 9, H::ink().withAlpha(.65f));
    juce::ignoreUnused(localMask);
    if (activeModule == SampleModule::Tune) {
        const float column = static_cast<float>(parameterDeck.getWidth() - 38) * .5f;
        moduleLabel(g, "GLOBAL KEY / DETUNE", { static_cast<float>(parameterDeck.getX() + 12), static_cast<float>(parameterDeck.getY() + 37), column, 16 }, 9);
        moduleLabel(g, "PLAYBACK BPM", { static_cast<float>(parameterDeck.getX() + 26) + column, static_cast<float>(parameterDeck.getY() + 37), column, 16 }, 9);
    } else if (activeModule == SampleModule::Grid) {
        moduleLabel(g, "TEMPO", { static_cast<float>(gridBpm.getX()), static_cast<float>(gridBpm.getY() - 17), static_cast<float>(gridBpm.getWidth()), 15 }, 9);
        moduleLabel(g, "ADJUST", { static_cast<float>(gridAdjustLeft.getX()), static_cast<float>(gridAdjustLeft.getY() - 16), static_cast<float>(gridAdjustRight.getRight() - gridAdjustLeft.getX()), 14 }, 9);
        moduleLabel(g, "SLIP 1 ms", { static_cast<float>(gridSlipLeft.getX()), static_cast<float>(gridSlipLeft.getY() - 16), static_cast<float>(gridSlipRight.getRight() - gridSlipLeft.getX()), 14 }, 9);
    }
    if (busy) {
        const auto area = juce::Rectangle<int>(sampleDeck.getX() + 12, 95, sampleDeck.getWidth() - 24, std::max(80, parameterDeck.getY() - 102));
        g.setColour(H::panel().withAlpha(.95f)); g.fillRoundedRectangle(area.toFloat(), 4);
        moduleLabel(g, status, area.toFloat().reduced(12).withHeight(45), 12, H::ink());
    }
}

void MainComponent::resized() {
    if (!pads[0] || !progressBar) return;
    const int w = getWidth(), h = getHeight(), gap = 12;
    const bool loaded = source && source->frames() > 0;
    std::vector<juce::Component*> placed;
    placed.reserve(static_cast<std::size_t>(getNumChildComponents()));
    auto put = [&placed](juce::Component& c, int x, int y, int width, int height) { placed.push_back(&c); c.setBounds(x, y, std::max(1, width), std::max(1, height)); c.setVisible(true); };
    const int available = std::max(260, std::min(static_cast<int>(w * .49) - 36, h - 192));
    const int side = std::max(56, (available - gap * 3) / 4), gridSide = side * 4 + gap * 3;
    padCabinet = { 8, 8, gridSide + 24, h - 16 }; padGrid = { 20, 82, gridSide, gridSide };
    sampleDeck = { padCabinet.getRight() + 18, 8, w - padCabinet.getRight() - 26, h - 16 };
    const int px = padCabinet.getX() + 12, dx = sampleDeck.getX() + 14, dw = sampleDeck.getWidth() - 28;
    put(bankInfo, px + 190, 12, padCabinet.getWidth() - 214, 18); bankInfo.setJustificationType(juce::Justification::centredRight);
    put(bankA, px, 40, 72, 28); put(bankB, px + 78, 40, 72, 28);
    int x = padCabinet.getRight() - 12;
    for (auto* button : { &redoButton, &undoButton, &deleteButton, &selectAllButton }) { const int bw = button == &selectAllButton ? 62 : 34; x -= bw; put(*button, x, 40, bw, 28); x -= 5; }
    for (int i = 0; i < 32; ++i) {
        auto& pad = *pads[static_cast<std::size_t>(i)]; if (i / 16 != activeBank) continue;
        const int local = i % 16, row = 3 - local / 4;
        put(pad, padGrid.getX() + (local % 4) * (side + gap), padGrid.getY() + row * (side + gap), side, side);
    }
    const int foot = padGrid.getBottom() + 12, inner = padCabinet.getWidth() - 24;
    if (autoMode == ChopMode::Grid) {
        const int autoWidth = std::max(112, inner - 222); put(autoSetButton, px, foot, autoWidth, 30); put(autoModeButton, px + autoWidth + 3, foot, 28, 30);
        put(sliceLeft, px + autoWidth + 38, foot, 26, 30); put(sliceSize, px + autoWidth + 69, foot, 112, 30); put(sliceRight, px + autoWidth + 186, foot, 26, 30);
    } else if (autoMode == ChopMode::KeyShift) {
        put(autoSetButton, px, foot, inner - 184, 30); put(autoModeButton, px + inner - 181, foot, 29, 30); put(keyStep, px + inner - 145, foot, 145, 30);
    } else { put(autoSetButton, px, foot, inner - 36, 30); put(autoModeButton, px + inner - 32, foot, 32, 30); }
    const int modeY = foot + 42;
    put(*holdMode, px, modeY, 76, 30); put(*voiceMode, px + 82, modeY, 70, 30); put(keyboardButton, px + 158, modeY, 72, 30);
    put(velocityButton, padCabinet.getRight() - 100, modeY, 40, 30); put(quantizeButton, padCabinet.getRight() - 54, modeY, 40, 30);
    put(trackTitle, dx + 5, 40, dw - 97, 23); put(trackInfo, dx + 5, 64, dw - 50, 20);
    put(loadButton, sampleDeck.getRight() - 95, 13, 78, 22); loadButton.setButtonText(loaded ? "LOAD" : "LOAD AUDIO");
    put(playButton, sampleDeck.getRight() - 205, 13, 102, 22);
    put(ejectButton, sampleDeck.getRight() - 68, 41, 43, 20); ejectButton.setVisible(loaded && !busy);
    const int overviewY = 102, overviewH = 34, waveY = 146;
    const int waveH = std::clamp(h / 4, 138, 188), moduleY = waveY + waveH + 13;
    if (loaded) { put(overview, dx + 2, overviewY, dw - 4, overviewH); put(waveform, dx + 2, waveY, dw - 4, waveH); }
    else { put(loadButton, dx + (dw - 190) / 2, 193, 190, 34); }
    const int mw = (dw - 4 * 5) / 5;
    for (int i = 0; i < 5; ++i) put(moduleButtons[static_cast<std::size_t>(i)], dx + i * (mw + 5), moduleY, mw, 32);
    parameterDeck = { dx - 1, moduleY + 44, dw + 2, h - moduleY - 77 };
    const int bx = parameterDeck.getX() + 12, by = parameterDeck.getY(), bw = parameterDeck.getWidth() - 24, bodyH = parameterDeck.getHeight();
    put(cueSummary, bx, by + 8, bw, 22); cueSummary.setJustificationType(juce::Justification::centredLeft);
    const int lower = by + 38;
    auto knob = [&](juce::Slider& s, juce::Label& label, int kx, int ky, int kw, int kh) { put(label, kx, ky, kw, 18); put(s, kx, ky + 18, kw, kh - 18); s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, std::min(90, kw - 6), 20); label.setFont(face(11)); };
    auto numeric = [&](juce::Slider& s, int nx, int ny, int nw, int nh = 32) { put(s, nx, ny, nw, nh); s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, std::max(40, nw - 38), nh); };
    if (activeModule == SampleModule::Trim) {
        if (optionsOpen) {
            put(optionsInfo, bx, lower + 6, bw, 36); put(outputLabel, bx, lower + 52, bw - 110, 20); put(outputRouting, bx, lower + 76, bw - 110, 33); put(optionsButton, bx + bw - 96, lower + 76, 96, 33);
        } else {
            const int kw = std::min(128, (bw - 135) / 2), kh = std::max(105, std::min(142, bodyH - 56));
            knob(level, levelLabel, bx + 5, lower, kw, kh); knob(filter, filterLabel, bx + kw + 14, lower, kw, kh);
            const int buttonX = bx + bw - 110; put(reverseButton, buttonX, lower + 10, 105, 32); put(favoriteButton, buttonX, lower + 52, 105, 32); put(optionsButton, buttonX, lower + 94, 105, 32);
        }
    } else if (activeModule == SampleModule::Tune) {
        const int column = (bw - 14) / 2;
        put(*keyMode, bx, lower + 21, 70, 32); numeric(globalPitch, bx + 78, lower + 21, column - 78); numeric(globalDetune, bx + 78, lower + 21, column - 78);
        globalPitch.setVisible(keyMode->getToggleState()); globalDetune.setVisible(!keyMode->getToggleState());
        numeric(currentBpm, bx + column + 14, lower + 21, column - 88); put(syncButton, bx + bw - 80, lower + 21, 80, 32);
        put(pitchTimeButton, bx, lower + 64, 70, 30); put(bpmHalf, bx + column + 14, lower + 64, 58, 30); put(bpmDouble, bx + column + 78, lower + 64, 58, 30);
        put(cuePitchLabel, bx, lower + 109, column, 17); numeric(cuePitch, bx, lower + 129, column);
        put(speedLabel, bx + column + 14, lower + 109, column, 17); numeric(speed, bx + column + 14, lower + 129, column - 78); put(speedHalf, bx + bw - 72, lower + 129, 33, 32); put(speedDouble, bx + bw - 34, lower + 129, 34, 32);
    } else if (activeModule == SampleModule::Envelope) {
        const int kw = std::min(136, (bw - 20) / 3), kh = std::min(144, bodyH - 72);
        knob(attack, attackLabel, bx, lower, kw, kh); knob(release, releaseLabel, bx + kw + 8, lower, kw, kh); knob(glide, glideLabel, bx + (kw + 8) * 2, lower, kw, kh);
        put(glideMode, bx + (kw + 8) * 2, lower + kh + 5, kw, 26);
    } else if (activeModule == SampleModule::Stems) {
        const int stemW = (bw - 18) / 4, stemH = std::min(100, bodyH - 100);
        for (int i = 0; i < 4; ++i) put(*stemButtons[static_cast<std::size_t>(i)], bx + i * (stemW + 6), lower + 8, stemW, stemH);
        put(moduleInfo, bx, lower + stemH + 20, bw, std::max(30, bodyH - stemH - 70));
    } else {
        const int rowY = lower + 18, col = (bw - 18) / 4;
        numeric(gridBpm, bx, rowY, col + 34); put(gridHalf, bx + col + 40, rowY, 37, 31); put(gridDouble, bx + col + 81, rowY, 37, 31);
        put(gridTap, bx + bw - 142, rowY, 66, 31); put(metronome, bx + bw - 70, rowY, 70, 31);
        const int row2 = rowY + 57;
        put(gridStart, bx, row2, col + 27, 32); put(gridAdjustLeft, bx + col + 36, row2, 35, 32); put(gridAdjustRight, bx + col + 76, row2, 35, 32);
        put(gridSlipLeft, bx + bw - 80, row2, 35, 32); put(gridSlipRight, bx + bw - 40, row2, 35, 32);
        const int row3 = row2 + 43; put(gridSet, bx, row3, col, 31); put(gridClear, bx + col + 6, row3, col, 31); put(gridClearAll, bx + (col + 6) * 2, row3, col, 31);
        put(gridSave, bx + bw - col, row3, col, 31); put(gridCancel, bx + bw - col, row3 + 37, col, 27);
        gridSave.setEnabled(gridDirty && loaded && !busy);
    }
    put(statusLabel, dx, sampleDeck.getBottom() - 29, dw, 23);
    if (busy) { put(*progressBar, dx + 18, 166, dw - 36, 14); put(cancelButton, dx + (dw - 112) / 2, 206, 112, 28); }
    trackTitle.setFont(face(15, true)); trackInfo.setFont(HardwareLookAndFeel::monoFont(10)); bankInfo.setFont(face(10)); cueSummary.setFont(HardwareLookAndFeel::monoFont(12, true)); moduleInfo.setFont(face(11));
    for (auto* label : { &speedLabel, &cuePitchLabel, &outputLabel, &glideLabel }) label->setFont(face(11));
    if (!loaded) { trackTitle.setText("SAMPLE MEMORY", juce::dontSendNotification); trackInfo.setText("WAV / AIFF / FLAC / MP3", juce::dontSendNotification); }
    for (auto* child : getChildren()) if (std::find(placed.begin(), placed.end(), child) == placed.end()) child->setVisible(false);
}

std::uint32_t MainComponent::occupiedMask() const {
    std::uint32_t mask = 0;
    for (int i = 0; i < 32; ++i) if (!project.pads[static_cast<std::size_t>(i)].empty()) mask |= std::uint32_t(1) << i;
    return mask;
}
juce::Colour MainComponent::selectionColour() const {
    const auto mask = selection & occupiedMask();
    if (bitCount(mask) != 1) return mask == 0 ? grey : white;
    for (int i = 0; i < 32; ++i) if (mask & (std::uint32_t(1) << i)) return juce::Colour(project.pads[static_cast<std::size_t>(i)].colour).brighter(.08f);
    return grey;
}
juce::String MainComponent::noteName(int index) { return juce::MidiMessage::getMidiNoteName(24 + index, true, true, 4); }
juce::String MainComponent::timeText(double seconds) { return juce::String(static_cast<int>(seconds) / 60).paddedLeft('0', 2) + ":" + juce::String(static_cast<int>(seconds) % 60).paddedLeft('0', 2); }

void MainComponent::setPadBank(int bank) {
    activeBank = std::clamp(bank, 0, 1); refreshControls();
}
void MainComponent::showSampleModule(SampleModule module) {
    if (module != SampleModule::Grid && gridEditing) toggleGridEdit(false);
    activeModule = module;
    if (module == SampleModule::Grid && source && !gridEditing) { toggleGridEdit(true); return; }
    refreshControls();
}
void MainComponent::refreshControls() {
    refreshing = true;
    const bool loaded = source && source->frames() > 0;
    const auto mask = selection & occupiedMask(); const bool cueSelected = mask != 0;
    for (auto* child : getChildren()) child->setEnabled(loaded && !busy);
    for (auto* c : { &bankA, &bankB, &settingsButton, &loadButton }) c->setEnabled(!busy);
    for (auto& button : moduleButtons) button.setEnabled(!busy);
    undoButton.setEnabled(canUndoEdit()); redoButton.setEnabled(canRedoEdit());
    cancelButton.setEnabled(true); progressBar->setEnabled(true);
    bankA.setToggleState(activeBank == 0, juce::dontSendNotification); bankB.setToggleState(activeBank == 1, juce::dontSendNotification);
    const auto bankMask = std::uint32_t(0xffff) << (activeBank * 16);
    const int hidden = bitCount(selection & ~bankMask);
    bankInfo.setText((activeBank == 0 ? "01 - 16" : "17 - 32") + juce::String(hidden ? "  / " + juce::String(hidden) + " selected off bank" : ""), juce::dontSendNotification);
    for (int i = 0; i < 5; ++i) moduleButtons[static_cast<std::size_t>(i)].setToggleState(static_cast<int>(activeModule) == i, juce::dontSendNotification);
    statusLabel.setText(status, juce::dontSendNotification);
    velocityButton.setToggleState(project.velocityEnabled, juce::dontSendNotification); quantizeButton.setToggleState(project.quantize, juce::dontSendNotification);
    holdMode->setToggleState(project.playbackMode == CuePlaybackMode::Hold, juce::dontSendNotification); voiceMode->setToggleState(project.mono, juce::dontSendNotification);
    keyboardButton.setToggleState(project.keyboardMode, juce::dontSendNotification); keyboardButton.setEnabled(loaded && !busy && (cueSelected || project.keyboardMode));
    syncButton.setToggleState(project.syncTempo, juce::dontSendNotification); syncButton.setEnabled(loaded && project.pitchTimeEnabled && !busy);
    pitchTimeButton.setToggleState(project.pitchTimeEnabled, juce::dontSendNotification);
    globalPitch.setValue(project.globalPitchSemitones, juce::dontSendNotification); globalDetune.setValue(project.globalDetuneCents, juce::dontSendNotification); currentBpm.setValue(project.projectBpm, juce::dontSendNotification);
    trackTitle.setText(sourceName, juce::dontSendNotification);
    trackInfo.setText(juce::String(project.sourceKey) + "  /  " + juce::String(project.sourceBpm, 2) + " BPM  /  " + timeText(playback.sourceFrame / (source ? source->sampleRate : 44100)), juce::dontSendNotification);
    for (int i = 0; i < 32; ++i) {
        auto& button = *pads[static_cast<std::size_t>(i)]; button.pad = project.pads[static_cast<std::size_t>(i)];
        button.selected = (selection & (std::uint32_t(1) << i)) != 0; button.active = (playback.activeCueMask & (std::uint32_t(1) << i)) != 0; button.repaint();
    }
    auto common = [&](auto getter, double fallback) { bool first = true; double value = fallback; for (int i = 0; i < 32; ++i) if (mask & (std::uint32_t(1) << i)) { const double next = getter(project.pads[static_cast<std::size_t>(i)]); if (first) { value = next; first = false; } else if (std::abs(value - next) > .0001) return fallback; } return value; };
    level.setValue(common([](const Pad& p) { return p.levelDb; }, 0), juce::dontSendNotification); filter.setValue(common([](const Pad& p) { return p.filter; }, .5), juce::dontSendNotification);
    attack.setValue(common([](const Pad& p) { return p.attackMs; }, 0), juce::dontSendNotification); release.setValue(common([](const Pad& p) { return p.releaseMs; }, 10000), juce::dontSendNotification);
    speed.setValue(common([](const Pad& p) { return p.speedRatio; }, 1), juce::dontSendNotification); cuePitch.setValue(common([](const Pad& p) { return p.pitchSemitones; }, 0), juce::dontSendNotification);
    reverseButton.setToggleState(common([](const Pad& p) { return p.reverse ? 1 : 0; }, 0) == 1, juce::dontSendNotification); favoriteButton.setToggleState(common([](const Pad& p) { return p.favorite ? 1 : 0; }, 0) == 1, juce::dontSendNotification);
    const auto colour = selectionColour();
    for (auto* slider : { &level, &filter, &attack, &release, &speed, &cuePitch, &glide }) slider->setColour(juce::Slider::rotarySliderFillColourId, colour);
    for (auto* c : std::initializer_list<juce::Component*> { &level, &filter, &attack, &release, &reverseButton, &favoriteButton, &speed, &cuePitch, &speedDouble, &speedHalf, &optionsButton, &optionsInfo, &outputRouting }) c->setEnabled(cueSelected && !busy);
    glide.setValue(project.glideMs, juce::dontSendNotification); glide.setEnabled(loaded && project.keyboardMode && project.mono && cueSelected && !busy);
    glideMode.setSelectedId(project.glideMode == GlideMode::Sustain ? 2 : 1, juce::dontSendNotification); glideMode.setEnabled(glide.isEnabled()); optionsButton.setToggleState(optionsOpen, juce::dontSendNotification);
    const auto cueId = juce::String(selectedPad / 16 == 0 ? "A" : "B") + juce::String(selectedPad % 16 + 1).paddedLeft('0', 2);
    cueSummary.setText(!loaded ? "SELECT AN AUDIO FILE TO START" : bitCount(mask) > 1 ? juce::String(bitCount(mask)) + " CUES SELECTED / ALL BANKS" : "PAD " + cueId + "   /   " + noteName(selectedPad) + (project.pads[static_cast<std::size_t>(selectedPad)].empty() ? "   EMPTY" : "   " + juce::String(project.pads[static_cast<std::size_t>(selectedPad)].name)), juce::dontSendNotification);
    optionsInfo.setText(bitCount(mask) > 1 ? "Multiple cues selected across both banks" : "Cue " + juce::String(selectedPad + 1) + " / MIDI " + juce::String(24 + selectedPad) + " / " + noteName(selectedPad), juce::dontSendNotification);
    optionsInfo.setFont(face(14)); optionsInfo.setColour(juce::Label::textColourId, grey);
    outputRouting.setSelectedId(onReadStudioTrack ? onReadStudioTrack() + 1 : static_cast<int>(common([](const Pad& p) { return p.outputChannel; }, 0)) + 1, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) {
        bool muted = project.stemMute[static_cast<std::size_t>(i)]; if (mask) muted = muted || common([i](const Pad& p) { return p.stemMute[static_cast<std::size_t>(i)] ? 1 : 0; }, 0) == 1;
        stemButtons[static_cast<std::size_t>(i)]->setToggleState(muted, juce::dontSendNotification);
    }
    moduleInfo.setText(stemsReady ? "FOUR STEMS READY\nClick a stem to mute. Shift-click to solo." : "NATIVE STEM SEPARATION\nClick a stem to separate this source, then mute or solo it.", juce::dontSendNotification); moduleInfo.setJustificationType(juce::Justification::centred);
    gridBpm.setValue(project.sourceBpm, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &gridBpm, &gridSave, &gridCancel, &gridTap, &gridDouble, &gridHalf, &metronome, &gridStart, &gridAdjustLeft, &gridAdjustRight, &gridSlipLeft, &gridSlipRight, &gridSet, &gridClear, &gridClearAll }) c->setEnabled(loaded && gridEditing && !busy);
    refreshing = false; resized(); repaint();
}

void MainComponent::refreshWaveforms() {
    overview.setProject(project, selection); waveform.setProject(project, selection);
}
void MainComponent::publishProject(bool refresh) {
    engine.setLiveProject(project); syncSelectionToEngine();
    refreshWaveforms(); cuePitch.updateText(); globalPitch.updateText();
    if (stemsReady) updateStemWaveform();
    if (refresh) refreshControls();
}
void MainComponent::syncSelectionToEngine() {
    engine.setKeyboardMode(project.keyboardMode, (selection & occupiedMask()) ? selectedPad : -1);
}
void MainComponent::pushUndo(const Project& before) { undoStack.push_back(before); if (undoStack.size() > 100) undoStack.erase(undoStack.begin()); redoStack.clear(); undoButton.setEnabled(true); redoButton.setEnabled(false); }
void MainComponent::editProject(const std::function<void(Project&)>& edit) { if (busy || !source) return; const auto before = project; edit(project); pushUndo(before); publishProject(); }
void MainComponent::editSelected(const std::function<void(Pad&)>& edit) { const auto mask = selection & occupiedMask(); if (!mask) return; editProject([&](Project& p) { for (int i = 0; i < 32; ++i) if (mask & (std::uint32_t(1) << i)) edit(p.pads[static_cast<std::size_t>(i)]); }); }
void MainComponent::undo() { if (busy || undoStack.empty() || gridEditing) return; redoStack.push_back(project); project = undoStack.back(); undoStack.pop_back(); publishProject(); }
void MainComponent::redo() { if (busy || redoStack.empty() || gridEditing) return; undoStack.push_back(project); project = redoStack.back(); redoStack.pop_back(); publishProject(); }

void MainComponent::selectPad(int index, const juce::ModifierKeys& mods, bool trigger) {
    if (busy || !source || index < 0 || index >= 32) return;
    activeBank = index / 16;
    const auto bit = std::uint32_t(1) << index;
    if (mods.isShiftDown()) { selection = bit; selectedPad = index; deleteSelection(); return; }
    if (mods.isCommandDown() || mods.isCtrlDown()) { selection ^= bit; selectedPad = index; syncSelectionToEngine(); refreshControls(); refreshWaveforms(); if (stemsReady) updateStemWaveform(); return; }
    selection = bit; selectedPad = index;
    if (trigger) triggerCue(index); else { syncSelectionToEngine(); refreshControls(); refreshWaveforms(); }
    if (stemsReady) updateStemWaveform();
}
void MainComponent::triggerCue(int index, float semitoneOffset, float velocity) {
    if (!source || busy) return;
    if (project.pads[static_cast<std::size_t>(index)].empty()) {
        const auto before = project;
        assignCueAt(project, index, playback.sourceFrame, source->sampleRate, source->frames());
        pushUndo(before); publishProject();
    }
    syncSelectionToEngine();
    engine.triggerPad(index, semitoneOffset, velocity);
    refreshControls(); refreshWaveforms();
}
void MainComponent::releaseCue(int index, float offset) { engine.releasePad(index, offset); }
void MainComponent::deleteSelection() { if (busy || !source) return; engine.stop(); editProject([this](Project& p) { for (int i = 0; i < 32; ++i) if (selection & (std::uint32_t(1) << i)) p.pads[static_cast<std::size_t>(i)] = Pad {}; }); }
void MainComponent::selectAll() { selection = occupiedMask(); syncSelectionToEngine(); refreshControls(); refreshWaveforms(); if (stemsReady) updateStemWaveform(); }
void MainComponent::movePad(int from, int to, bool copy) { if (busy || from == to || !source) return; engine.stop(); editProject([=](Project& p) { moveCue(p, from, to, copy); }); selectedPad = to; activeBank = to / 16; selection = std::uint32_t(1) << to; syncSelectionToEngine(); refreshControls(); refreshWaveforms(); if (stemsReady) updateStemWaveform(); }
void MainComponent::renameCue(int index) {
    if (busy || !source) return;
    juce::PopupMenu menu; menu.addItem(1, "Rename cue..."); menu.addItem(2, "Delete cue");
    juce::PopupMenu move, copy;
    for (int bank = 0; bank < 2; ++bank) {
        juce::PopupMenu moveBank, copyBank;
        for (int local = 0; local < 16; ++local) {
            const int destination = bank * 16 + local;
            const auto& cue = project.pads[static_cast<std::size_t>(destination)];
            const auto label = juce::String(bank == 0 ? "A" : "B") + juce::String(local + 1).paddedLeft('0', 2) + (cue.empty() ? "  Empty" : "  " + juce::String(cue.name));
            moveBank.addItem(100 + destination, label, destination != index);
            copyBank.addItem(200 + destination, label, destination != index);
        }
        move.addSubMenu(bank == 0 ? "Bank A / 01 - 16" : "Bank B / 17 - 32", moveBank);
        copy.addSubMenu(bank == 0 ? "Bank A / 01 - 16" : "Bank B / 17 - 32", copyBank);
    }
    menu.addSeparator(); menu.addSubMenu("Move / swap to", move, !project.pads[static_cast<std::size_t>(index)].empty()); menu.addSubMenu("Copy to", copy, !project.pads[static_cast<std::size_t>(index)].empty());
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(pads[static_cast<std::size_t>(index)].get()), [safe, index](int result) { if (safe) safe->cueMenuAction(index, result); });
}
void MainComponent::cueMenuAction(int index, int result) {
    if (result >= 100 && result < 132) { movePad(index, result - 100, false); return; }
    if (result >= 200 && result < 232) { movePad(index, result - 200, true); return; }
    if (result == 2) { selection = std::uint32_t(1) << index; deleteSelection(); return; }
    if (result != 1) return;
    auto* dialog = new juce::AlertWindow("Rename Cue", "Cue name", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", project.pads[static_cast<std::size_t>(index)].name);
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, index](int accepted) { if (safe && accepted == 1) safe->renameCueTo(index, dialog->getTextEditorContents("name")); }), true);
}
void MainComponent::renameCueTo(int index, const juce::String& value) {
    const auto name = value.substring(0, 128).toStdString();
    editProject([index, name](Project& p) { p.pads[static_cast<std::size_t>(index)].name = name; });
}

void MainComponent::chooseAutoSetMode() {
    juce::PopupMenu menu;
    menu.addItem(1, "Find Samples", true, autoMode == ChopMode::Transients);
    menu.addItem(2, "Set Slicer", true, autoMode == ChopMode::Grid);
    menu.addItem(3, "Set Random", true, autoMode == ChopMode::Random);
    menu.addItem(4, "Key Shift Pad", true, autoMode == ChopMode::KeyShift);
    menu.addItem(5, "Serato DJ", !importedCues.empty(), autoMode == ChopMode::SeratoDJ);
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&autoModeButton), [safe](int id) {
        if (!safe || id == 0) return;
        constexpr ChopMode modes[] { ChopMode::Transients, ChopMode::Grid, ChopMode::Random, ChopMode::KeyShift, ChopMode::SeratoDJ };
        constexpr const char* names[] { "Find Samples", "Set Slicer", "Set Random", "Key Shift Pad", "Serato DJ" };
        safe->autoMode = modes[id - 1]; safe->autoSetButton.setButtonText(names[id - 1]);
        safe->refreshControls();
    });
}
void MainComponent::runAutoSet(bool shiftDomain) {
    if (busy || !source) return;
    const auto before = project;
    if (autoMode == ChopMode::SeratoDJ) {
        if (importedCues.empty()) return;
        for (int i = 0; i < 32; ++i) {
            auto& pad = project.pads[static_cast<std::size_t>(i)];
            if (pad.favorite) continue;
            pad = static_cast<std::size_t>(i) < importedCues.size() ? importedCues[static_cast<std::size_t>(i)] : Pad {};
        }
    } else {
        if (autoMode == ChopMode::KeyShift && project.pads[static_cast<std::size_t>(selectedPad)].empty()) { status = "Select a Cue before Key Shift Pad"; refreshControls(); return; }
        if (autoMode == ChopMode::Grid && !shiftDomain) slicerStart = playback.sourceFrame;
        const auto start = autoMode == ChopMode::Grid ? slicerStart : playback.sourceFrame;
        autoSetCues(project, *source, analysis, autoMode, start, sliceBeats, keyShiftStep, ++randomSeed, selectedPad);
    }
    pushUndo(before); selection = std::uint32_t(1) << selectedPad;
    status = autoSetButton.getButtonText() + " applied; favorites retained";
    publishProject();
}
void MainComponent::shiftSlicer(int direction) {
    if (!source) return;
    const auto step = static_cast<std::int64_t>(source->sampleRate * 60. / project.sourceBpm * sliceBeats);
    slicerStart = static_cast<std::size_t>(std::clamp(static_cast<std::int64_t>(slicerStart) + direction * step, std::int64_t(0), static_cast<std::int64_t>(source->frames() - 1)));
    runAutoSet(true);
}

void MainComponent::runTask(const juce::String& message, Work work) {
    if (busy) return;
    busy = true; status = message; cancelled.store(false); taskProgress.store(.02); refreshControls();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    if (worker.joinable()) worker.join();
    worker = std::thread([safe, work = std::move(work)] {
        Completion complete; juce::String error;
        try { if (safe) complete = work(safe->cancelled); }
        catch (const std::exception& e) { error = e.what(); }
        catch (...) { error = "Unexpected audio processing error"; }
        juce::MessageManager::callAsync([safe, complete = std::move(complete), error] {
            if (!safe) return;
            const bool wasCancelled = safe->cancelled.load(); safe->busy = false;
            if (wasCancelled) safe->status = "Operation cancelled";
            else if (error.isNotEmpty()) safe->reportError(error);
            else if (complete) { try { complete(); } catch (const std::exception& e) { safe->reportError(e.what()); } }
            safe->refreshControls();
        });
    });
}
void MainComponent::reportError(const juce::String& message) {
    status = message; statusLabel.setText(status, juce::dontSendNotification); repaint();
    if (stemSmokeStage > 0 && stemSmokeStage < 4) { stemSmokeChecks.getDynamicObject()->setProperty("error", message); stemSmokeStage = 4; return; }
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Samplify", message);
}
bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) { return !busy && files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;mp3;samplify"); }
void MainComponent::filesDropped(const juce::StringArray& files, int, int) { if (!busy && files.size() == 1) loadFile(juce::File(files[0])); }
void MainComponent::importAudio() {
    if (busy) return;
    chooser = std::make_unique<juce::FileChooser>("Load audio file", sourceFile.getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& c) { if (safe && c.getResult().existsAsFile()) safe->loadSource(c.getResult()); });
}
void MainComponent::loadFile(const juce::File& file) {
    if (file.hasFileExtension("samplify")) {
        try {
            const auto session = samplify::loadSession(file);
            if (!session.source.existsAsFile()) {
                chooser = std::make_unique<juce::FileChooser>("LOCATE AUDIO FILE - " + session.source.getFileName(), file.getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                auto safe = juce::Component::SafePointer<MainComponent>(this);
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, session](const juce::FileChooser& c) {
                    if (!safe || !c.getResult().existsAsFile()) return;
                    auto relocated = session; relocated.source = c.getResult(); safe->loadSource(relocated.source, relocated);
                });
                return;
            }
            loadSource(session.source, session);
        }
        catch (const std::exception& e) { reportError(e.what()); }
    } else loadSource(file);
}
void MainComponent::loadSource(const juce::File& file, std::optional<Session> restored) {
    if (busy) return;
    engine.stop();
    runTask("Analysing " + file.getFileName() + "...", [this, file, restored](const std::atomic<bool>& cancel) {
        auto input = std::make_shared<AudioData>(AudioEngine::readAudio(file)); taskProgress.store(.2);
        if (cancel.load()) return Completion {};
        auto a = analyse(*input, 8192); taskProgress.store(.55);
        if (cancel.load()) return Completion {};
        Project updated;
        std::array<juce::File, 4> paths;
        std::array<std::shared_ptr<const AudioData>, 4> imported;
        if (restored) { updated = restored->project; paths = restored->stems; }
        else {
            // Loading audio never invents Cues: clicks, MIDI, or Autoset create them.
            updated.padCount = 32; updated.sourceBpm = a.bpm; updated.projectBpm = a.bpm;
            updated.sourceKey = a.key; if (a.key != "Unknown") updated.projectKey = a.key;
            updated.tempoMarkers = { { 0, updated.sourceBpm } };
            updated.pitchTimeEnabled = !(input->duration() < 3 || (input->duration() < 10 && file.getFileNameWithoutExtension().contains("808")));
        }
        for (const auto& cue : updated.pads) if (cue.start > input->frames() || cue.end > input->frames()) throw std::runtime_error("Cue offsets exceed this audio file. Locate the original source audio.");
        bool regenerateStems = false;
       #if JUCE_LINUX
        const auto oldGeneratedCacheRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Samplify/Stems");
       #else
        const auto oldGeneratedCacheRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Samplify/Stems");
       #endif
        for (int i = 0; i < 4; ++i) {
            if (cancel.load()) return Completion {};
            if (paths[static_cast<std::size_t>(i)] != juce::File()) {
                if (paths[static_cast<std::size_t>(i)].isAChildOf(oldGeneratedCacheRoot)) { regenerateStems = true; break; }
                try {
                    auto stem = std::make_shared<AudioData>(AudioEngine::readAudio(paths[static_cast<std::size_t>(i)]));
                    if (stem->sampleRate != input->sampleRate || stem->frames() != input->frames() || stem->channels.size() != input->channels.size()) { regenerateStems = true; break; }
                    imported[static_cast<std::size_t>(i)] = stem;
                } catch (const std::exception&) { regenerateStems = true; break; }
            }
        }
        if (regenerateStems) { paths = {}; imported = {}; }
        const auto dj = readSeratoCues(file, input->sampleRate, input->frames());
        if (cancel.load()) return Completion {};
        engine.setSource(input);
        for (int i = 0; i < 4; ++i) if (imported[static_cast<std::size_t>(i)]) engine.setStem(i, imported[static_cast<std::size_t>(i)]);
        engine.setLiveProject(updated); taskProgress.store(1);
        return Completion([this, input, a = std::move(a), updated, file, paths, imported, dj, restored, regenerateStems]() mutable {
            installSource(input, std::move(a), updated, file, paths, imported); importedCues = dj;
            if (restored && onRestoreWorkstationState) onRestoreWorkstationState(restored->workstation);
            status = restored && restored->migrationNotice.isNotEmpty() ? juce::String(restored->migrationNotice) : "Source ready - click a pad to create a Cue at the playhead";
            refreshControls();
            if (regenerateStems) { status = "Cached stems missing - regenerating while retaining Cue settings"; separateStems(); }
        });
    });
}
void MainComponent::installSource(std::shared_ptr<const AudioData> input, Analysis result, Project state,
                                  const juce::File& file, std::array<juce::File, 4> paths,
                                  std::array<std::shared_ptr<const AudioData>, 4> imported) {
    source = std::move(input); waveformAudio = source; analysis = std::move(result); project = std::move(state);
    waveformCancelled.store(true); displayedStemMask.reset();
    sourceFile = file;
    const auto title = juce::String(source->title).trim(), artist = juce::String(source->artist).trim();
    sourceName = title.isNotEmpty() ? title : file.getFileNameWithoutExtension();
    if (artist.isNotEmpty()) sourceName = artist + " - " + sourceName;
    stemFiles = std::move(paths); stems = std::move(imported);
    stemsReady = std::all_of(stems.begin(), stems.end(), [](const auto& s) { return s != nullptr; });
    selection = 1; selectedPad = 0; activeBank = 0; activeModule = SampleModule::Trim; undoStack.clear(); redoStack.clear(); heldKeys.clear();
    playback = {}; gridEditing = false; gridDirty = false; optionsOpen = false;
    metronomeOn = false; metronome.setToggleState(false, juce::dontSendNotification); engine.setMetronome(false);
    waveform.setGridEditMode(false); waveform.setFrozen(false);
    overview.setData(nullptr, {}); waveform.setData(nullptr, {});
    overview.setData(source, analysis); waveform.setData(source, analysis);
    overview.setPlayback(playback); waveform.setPlayback(playback);
    publishProject();
}
void MainComponent::ejectSource() {
    if (busy) return;
    engine.stop(); engine.setMetronome(false); engine.setSource(nullptr);
    source.reset(); waveformAudio.reset(); sourceFile = juce::File(); sourceName.clear(); stems = {}; stemFiles = {}; importedCues.clear(); stemsReady = false;
    waveformCancelled.store(true); displayedStemMask.reset();
    project = {}; analysis = {}; undoStack.clear(); redoStack.clear(); heldKeys.clear(); selection = 1; selectedPad = 0; activeBank = 0; activeModule = SampleModule::Trim;
    overview.setData(nullptr, {}); waveform.setData(nullptr, {}); busy = false; status = "Load audio to begin"; refreshControls();
}

void MainComponent::loadDemo() {
    if (busy) return;
    runTask("Loading demo source...", [this](const std::atomic<bool>& cancel) {
        auto data = std::make_shared<AudioData>(); data->sampleRate = 44100;
        constexpr int length = 44100 * 16; data->channels.assign(2, std::vector<float>(length, 0));
        std::uint32_t noise = 27;
        for (int i = 0; i < length; ++i) {
            const double t = i / data->sampleRate, beat = std::fmod(t, .5), hat = std::fmod(t, .25);
            noise = noise * 1664525u + 1013904223u; const double n = static_cast<double>(noise >> 8) / 8388608. - 1;
            const double kick = std::sin(2 * std::numbers::pi * (48 * beat + 7 * (1 - std::exp(-beat * 40)))) * std::exp(-beat * 22) * .55;
            const double snare = std::fmod(t, 1.) >= .5 ? n * std::exp(-beat * 34) * .19 : 0;
            constexpr double notes[] { 130.8128, 155.5635, 195.9977, 174.6141 };
            const double freq = notes[static_cast<int>(t / 2) % 4];
            const double melody = (std::sin(2 * std::numbers::pi * freq * t) + .25 * std::sin(4 * std::numbers::pi * freq * t)) * .13 * (1 - std::exp(-beat * 60)) * std::exp(-beat * 3);
            const float value = static_cast<float>(kick + snare + n * std::exp(-hat * 100) * .09 + melody);
            data->channels[0][static_cast<std::size_t>(i)] = value; data->channels[1][static_cast<std::size_t>(i)] = value * .96f;
        }
        if (cancel.load()) return Completion {};
        auto a = analyse(*data, 8192); taskProgress.store(.65);
        Project state; state.sourceBpm = state.projectBpm = 120; state.sourceKey = state.projectKey = "C minor"; state.tempoMarkers = { { 0, 120 } };
        // Demo is an explicit example session, unlike ordinary audio loading.
        autoSetCues(state, *data, a, ChopMode::Grid, 0, 1, 1, 42, 0);
       #if JUCE_LINUX
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Samplify/Demo.wav");
       #else
        const auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Samplify/Demo.wav");
       #endif
        if (file.getParentDirectory().createDirectory().failed()) throw std::runtime_error("Cannot create the demo audio folder");
        AudioEngine::writeAudio(file, *data);
        if (cancel.load()) return Completion {};
        engine.setSource(data); engine.setLiveProject(state); taskProgress.store(1);
        return Completion([this, data, a = std::move(a), state, file]() mutable {
            installSource(data, std::move(a), state, file, {}, {}); sourceName = "Demo - Midnight Cuts"; importedCues.clear();
            status = "Demo loaded - 32 example Cues. New audio loads with empty pads.";
        });
    });
}

void MainComponent::toggleStemMask(int index, bool solo) {
    const auto mask = selection & occupiedMask();
    editProject([index, solo, mask](Project& state) {
        if (!mask) {
            auto& muted = state.stemMute;
            if (solo) { const bool alreadySolo = !muted[static_cast<std::size_t>(index)] && std::count(muted.begin(), muted.end(), true) == 3; muted.fill(!alreadySolo); muted[static_cast<std::size_t>(index)] = false; }
            else muted[static_cast<std::size_t>(index)] = !muted[static_cast<std::size_t>(index)];
            return;
        }
        bool allMuted = true, allSolo = true;
        for (int i = 0; i < 32; ++i) if (mask & (std::uint32_t(1) << i)) {
            const auto& cue = state.pads[static_cast<std::size_t>(i)];
            for (int stem = 0; stem < 4; ++stem) {
                const bool muted = state.stemMute[static_cast<std::size_t>(stem)] || cue.stemMute[static_cast<std::size_t>(stem)];
                if (stem == index) { allMuted = allMuted && muted; allSolo = allSolo && !muted; }
                else allSolo = allSolo && muted;
            }
        }
        // Consolidate the current audible state before switching from source
        // audition to cue editing. No global bit can secretly defeat Unmute.
        for (auto& cue : state.pads) if (!cue.empty()) for (int stem = 0; stem < 4; ++stem)
            cue.stemMute[static_cast<std::size_t>(stem)] = cue.stemMute[static_cast<std::size_t>(stem)] || state.stemMute[static_cast<std::size_t>(stem)];
        state.stemMute.fill(false);
        for (int i = 0; i < 32; ++i) if (mask & (std::uint32_t(1) << i)) {
            auto& muted = state.pads[static_cast<std::size_t>(i)].stemMute;
            if (solo) { muted.fill(!allSolo); muted[static_cast<std::size_t>(index)] = false; }
            else muted[static_cast<std::size_t>(index)] = !allMuted;
        }
    });
}
void MainComponent::requestStem(int index, bool solo) {
    if (busy || !source) return;
    toggleStemMask(index, solo);
    if (!stemsReady) separateStems(); else updateStemWaveform();
}

void MainComponent::separateStems() {
    if (busy || !source) return;
    const auto input = source; const auto file = sourceFile; const auto state = project;
    runTask("Initialising native stem separation...", [this, input, file, state](const std::atomic<bool>& cancel) {
        const auto stamp = file.getFullPathName() + juce::String(file.getSize()) + juce::String(file.getLastModificationTime().toMilliseconds());
       #if JUCE_LINUX
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Samplify/Stems-float32");
       #else
        const auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Samplify/Stems-float32");
       #endif
        const auto cache = root.getChildFile(juce::String::toHexString(stamp.hashCode64()));
        if (cache.createDirectory().failed()) throw std::runtime_error("Cannot create the stem cache folder");
        std::array<juce::File, 4> paths;
        std::array<std::shared_ptr<const AudioData>, 4> separated;
        bool cached = true;
        for (int i = 0; i < 4; ++i) { paths[static_cast<std::size_t>(i)] = cache.getChildFile(juce::String(stemNames[i]).toLowerCase() + ".wav"); cached = cached && paths[static_cast<std::size_t>(i)].existsAsFile(); }
        if (cached) {
            for (int i = 0; i < 4; ++i) {
                if (cancel.load()) return Completion {};
                auto audio = std::make_shared<AudioData>(AudioEngine::readAudio(paths[static_cast<std::size_t>(i)]));
                if (audio->frames() != input->frames() || audio->sampleRate != input->sampleRate || audio->channels.size() != input->channels.size()) { cached = false; break; }
                separated[static_cast<std::size_t>(i)] = audio;
            }
        }
        if (!cached) {
            if (!separator) {
                auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
               #if JUCE_MAC
                auto model = executable.getParentDirectory().getSiblingFile("Resources").getChildFile(StemSeparator::modelFileName());
               #else
                auto model = executable.getParentDirectory().getChildFile("Resources").getChildFile(StemSeparator::modelFileName());
               #endif
                const auto override = juce::SystemStats::getEnvironmentVariable("SAMPLIFY_MODEL_PATH", {});
                if (override.isNotEmpty()) model = juce::File(override);
                if (!model.existsAsFile()) throw std::runtime_error("The verified stem model is missing from the app's Resources folder. Reinstall the complete Samplify app.");
                separator = std::make_unique<StemSeparator>(std::filesystem::path(model.getFullPathName().toStdString()));
            }
            auto result = separator->separate(*input, cancel, [this](float fraction, const std::string&) { taskProgress.store(std::clamp(static_cast<double>(fraction) * .85, .02, .85)); });
            for (int i = 0; i < 4; ++i) {
                if (cancel.load()) return Completion {};
                auto audio = std::make_shared<AudioData>(std::move(result[static_cast<std::size_t>(i)]));
                AudioEngine::writeFloatAudio(paths[static_cast<std::size_t>(i)], *audio); separated[static_cast<std::size_t>(i)] = audio;
            }
        }
        if (cancel.load()) return Completion {};
        for (int i = 0; i < 4; ++i) engine.setStem(i, separated[static_cast<std::size_t>(i)]);
        engine.setLiveProject(state); taskProgress.store(1);
        return Completion([this, paths, separated] { stemFiles = paths; stems = separated; stemsReady = true; status = "Native stems ready - click to mute; Shift-click to solo"; updateStemWaveform(); });
    });
}
void MainComponent::updateStemWaveform() {
    if (!source || !stemsReady) return;
    auto mask = project.stemMute;
    if (selection & occupiedMask()) for (int i = 0; i < 4; ++i) mask[static_cast<std::size_t>(i)] = mask[static_cast<std::size_t>(i)] || project.pads[static_cast<std::size_t>(selectedPad)].stemMute[static_cast<std::size_t>(i)];
    if (displayedStemMask && *displayedStemMask == mask && waveformAudio != source) return;
    if (waveformWorking) {
        if (!runningStemMask || *runningStemMask != mask) waveformCancelled.store(true);
        return;
    }
    const auto input = source; const auto separated = stems;
    waveformWorking = true; runningStemMask = mask; waveformCancelled.store(false);
    if (waveformWorker.joinable()) waveformWorker.join();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    waveformWorker = std::thread([safe, input, separated, mask] {
        std::shared_ptr<AudioData> mixed; Analysis result; juce::String error;
        try {
            mixed = std::make_shared<AudioData>(); mixed->sampleRate = input->sampleRate;
            mixed->channels.assign(input->channels.size(), std::vector<float>(input->frames(), 0));
            for (int stem = 0; stem < 4; ++stem) {
                if (!safe || safe->waveformCancelled.load()) break;
                if (mask[static_cast<std::size_t>(stem)] || !separated[static_cast<std::size_t>(stem)]) continue;
                for (std::size_t ch = 0; ch < mixed->channels.size(); ++ch)
                    for (std::size_t i = 0; i < input->frames(); ++i) mixed->channels[ch][i] += separated[static_cast<std::size_t>(stem)]->channels[ch][i];
            }
            if (safe && !safe->waveformCancelled.load()) result = analyse(*mixed, 8192);
        } catch (const std::exception& e) { error = e.what(); }
        juce::MessageManager::callAsync([safe, input, mixed, mask, result = std::move(result), error]() mutable {
            if (!safe) return;
            const bool discarded = safe->waveformCancelled.load() || safe->source != input;
            safe->waveformWorking = false; safe->runningStemMask.reset();
            if (error.isNotEmpty()) { safe->reportError(error); return; }
            if (!discarded && mixed) {
                safe->waveformAudio = mixed; safe->stemAnalysis = std::move(result); safe->displayedStemMask = mask;
                safe->overview.setData(mixed, safe->stemAnalysis); safe->waveform.setData(mixed, safe->stemAnalysis); safe->refreshWaveforms();
            }
            // Selection may have changed during analysis. Only the latest mask is shown.
            if (safe->source && safe->stemsReady) safe->updateStemWaveform();
        });
    });
}

void MainComponent::toggleGridEdit(bool value) {
    if (busy || !source) return;
    if (value && !gridEditing) { gridBackup = project; gridDirty = false; tapTimes.clear(); }
    if (!value && gridEditing && gridDirty) restoreGrid(project, gridBackup);
    if (!value) { metronomeOn = false; metronome.setToggleState(false, juce::dontSendNotification); engine.setMetronome(false); }
    gridEditing = value; if (value) activeModule = SampleModule::Grid; else if (activeModule == SampleModule::Grid) activeModule = SampleModule::Trim; waveform.setGridEditMode(value); publishProject();
}
void MainComponent::gridEdit(const std::function<void(Project&)>& edit) {
    if (!gridEditing || busy || !source) return;
    edit(project); gridDirty = true;
    std::sort(project.tempoMarkers.begin(), project.tempoMarkers.end(), [](const TempoMarker& a, const TempoMarker& b) { return a.frame < b.frame; });
    project.tempoMarkers.erase(std::unique(project.tempoMarkers.begin(), project.tempoMarkers.end(), [](const TempoMarker& a, const TempoMarker& b) { return a.frame == b.frame; }), project.tempoMarkers.end());
    publishProject();
}
void MainComponent::applyGridBpm(double value) {
    gridEdit([this, value](Project& state) {
        state.beatgridEnabled = true;
        state.sourceBpm = std::clamp(value, 30., 300.);
        if (state.tempoMarkers.empty()) state.tempoMarkers.push_back({ 0, state.sourceBpm });
        auto it = state.tempoMarkers.begin();
        for (auto marker = state.tempoMarkers.begin(); marker != state.tempoMarkers.end() && marker->frame <= playback.sourceFrame; ++marker) it = marker;
        it->bpm = state.sourceBpm;
    });
}
void MainComponent::gridDrag(std::size_t from, std::size_t to, bool slip) {
    if (!source || !gridEditing) return;
    const auto frames = source->frames();
    gridEdit([=, this](Project& state) {
        if (state.tempoMarkers.empty()) state.tempoMarkers.push_back({ 0, state.sourceBpm });
        if (slip) {
            const auto difference = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
            for (auto& marker : state.tempoMarkers) marker.frame = static_cast<std::size_t>(std::clamp(static_cast<std::int64_t>(marker.frame) + difference, std::int64_t(0), static_cast<std::int64_t>(frames - 1)));
            return;
        }
        auto nearest = std::min_element(state.tempoMarkers.begin(), state.tempoMarkers.end(), [from](const TempoMarker& a, const TempoMarker& b) { return std::abs(static_cast<double>(a.frame) - from) < std::abs(static_cast<double>(b.frame) - from); });
        const double near = source->sampleRate * 60. / state.sourceBpm / 8;
        if (std::abs(static_cast<double>(nearest->frame) - from) < near) { nearest->frame = std::min(to, frames - 1); return; }
        auto anchor = state.tempoMarkers.begin();
        for (auto it = state.tempoMarkers.begin(); it != state.tempoMarkers.end() && it->frame < from; ++it) anchor = it;
        if (from <= anchor->frame || to <= anchor->frame) return;
        anchor->bpm = std::clamp(anchor->bpm * static_cast<double>(from - anchor->frame) / static_cast<double>(to - anchor->frame), 30., 300.);
        state.sourceBpm = anchor->bpm;
    });
}
void MainComponent::tapTempo() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (!tapTimes.empty() && now - tapTimes.back() > 2000) tapTimes.clear();
    tapTimes.push_back(now); if (tapTimes.size() > 8) tapTimes.erase(tapTimes.begin());
    if (tapTimes.size() >= 2) { const double interval = (tapTimes.back() - tapTimes.front()) / (tapTimes.size() - 1); applyGridBpm(std::clamp(60000. / interval, 30., 300.)); }
}

void MainComponent::configureStudioRouting(std::function<int()> read, std::function<void(int)> change) {
    onReadStudioTrack = std::move(read); onStudioTrackChanged = std::move(change);
    outputRouting.clear(juce::dontSendNotification);
    for (int i = 0; i < 8; ++i) outputRouting.addItem("Studio Track " + juce::String(i + 1), i + 1);
    outputLabel.setText("Live sampler track", juce::dontSendNotification);
    outputRouting.setTooltip("Route live pads and source audition to this studio mixer track. Pattern and arrangement clips use their own destination track.");
    refreshStudioRouting();
}
void MainComponent::refreshStudioRouting() {
    if (onReadStudioTrack) outputRouting.setSelectedId(onReadStudioTrack() + 1, juce::dontSendNotification);
}

void MainComponent::setStudioTempo(double bpm) {
    if (!std::isfinite(bpm) || std::abs(hostBpm - bpm) < .0001) return;
    hostBpm = std::clamp(bpm, 30., 300.);
    if (project.syncTempo) { project.projectBpm = hostBpm; engine.setLiveProject(project); refreshControls(); }
}

Session MainComponent::currentSession() const {
    Session result { project, sourceFile, stemFiles, {} };
    if (onReadWorkstationState) result.workstation = onReadWorkstationState();
    return result;
}
void MainComponent::openSession() {
    if (busy) return;
    chooser = std::make_unique<juce::FileChooser>("Open Samplify session", juce::File(), "*.samplify");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& c) { if (safe && c.getResult().existsAsFile()) safe->loadFile(c.getResult()); });
}
void MainComponent::saveSession() {
    if (busy || !source) return;
    chooser = std::make_unique<juce::FileChooser>("Save sampler, patterns, arrangement, mixer and effects", sourceFile.withFileExtension("samplify"), "*.samplify");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe](const juce::FileChooser& c) {
        if (!safe || c.getResult() == juce::File()) return;
        try { samplify::saveSession(c.getResult().withFileExtension("samplify"), safe->currentSession()); safe->status = "Studio session saved - keep source, stems and audio assets available"; safe->refreshControls(); }
        catch (const std::exception& e) { safe->reportError(e.what()); }
    });
}
void MainComponent::exportSelected() {
    if (busy || !source || project.pads[static_cast<std::size_t>(selectedPad)].empty()) return;
    chooser = std::make_unique<juce::FileChooser>("Export selected Cue as WAV", sourceFile.getSiblingFile("Samplify-Cue-" + juce::String(selectedPad + 1) + ".wav"), "*.wav");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe](const juce::FileChooser& c) { if (safe && c.getResult() != juce::File()) safe->exportTo(c.getResult().withFileExtension("wav"), false); });
}
void MainComponent::exportAll() {
    if (busy || !source) return;
    chooser = std::make_unique<juce::FileChooser>("Choose a folder for Cue WAV files", sourceFile.getParentDirectory(), "*");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe](const juce::FileChooser& c) { if (safe && c.getResult().isDirectory()) safe->exportTo(c.getResult().getNonexistentChildFile("Samplify-Cues", "", true), true); });
}
void MainComponent::exportTo(const juce::File& file, bool all) {
    const auto state = project; const auto chosen = selectedPad;
    runTask("Rendering Cue WAV files...", [this, state, chosen, file, all](const std::atomic<bool>& cancel) {
        if (all && file.createDirectory().failed()) throw std::runtime_error("Cannot create the export folder");
        int count = 0;
        for (int i = 0; i < 32; ++i) {
            if (cancel.load()) return Completion {};
            if ((!all && i != chosen) || state.pads[static_cast<std::size_t>(i)].empty()) continue;
            auto data = engine.renderForExport(state, i);
            AudioEngine::writeAudio(all ? file.getChildFile("Cue-" + juce::String(i + 1).paddedLeft('0', 2) + ".wav") : file, data);
            ++count; taskProgress.store((i + 1) / 32.);
        }
        return Completion([this, count, file] { status = juce::String(count) + " Cue WAV files exported to " + file.getFullPathName(); });
    });
}

namespace {
class AudioSettingsPanel final : public juce::Component {
public:
    AudioSettingsPanel(juce::AudioDeviceManager& manager, double bpm, std::function<void(double)> change)
        : devices(manager, 0, 0, 0, 64, true, false, true, false) {
        addAndMakeVisible(devices); addAndMakeVisible(label); addAndMakeVisible(tempo);
        label.setText("Studio tempo", juce::dontSendNotification);
        tempo.setRange(30, 300, .01); tempo.setSliderStyle(juce::Slider::LinearHorizontal); tempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 28); tempo.setValue(bpm, juce::dontSendNotification);
        tempo.onValueChange = [this, change] { change(tempo.getValue()); };
        setSize(590, 550);
    }
    void resized() override { label.setBounds(18, 10, 200, 30); tempo.setBounds(222, 10, getWidth() - 240, 30); devices.setBounds(0, 50, getWidth(), getHeight() - 50); }
private: juce::AudioDeviceSelectorComponent devices; juce::Label label; juce::Slider tempo;
};
}
void MainComponent::showAudioSettings() {
    if (busy) return;
    engine.refreshMidiInputs();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    auto content = std::make_unique<AudioSettingsPanel>(engine.deviceManager(), hostBpm, [safe](double value) { if (safe) { safe->hostBpm = value; if (safe->onHostTempoChanged) safe->onHostTempoChanged(value); if (safe->project.syncTempo) safe->editProject([value](Project& p) { p.projectBpm = value; }); } });
    juce::DialogWindow::LaunchOptions options; options.content.setOwned(content.release()); options.dialogTitle = "Samplify - Audio / MIDI"; options.dialogBackgroundColour = top;
    options.componentToCentreAround = this; options.useNativeTitleBar = true; options.resizable = false; options.escapeKeyTriggersCloseButton = true; options.launchAsync();
}
void MainComponent::stopPlayback() { engine.stopWorkstation(); engine.stop(); engine.pauseSource(); heldKeys.clear(); status = "Playback stopped"; statusLabel.setText(status, juce::dontSendNotification); }
bool MainComponent::isReadyForSmokeTest() const { return !busy && source && source->frames() > 0 && !analysis.waveform.empty(); }

void MainComponent::timerCallback() {
    playback = engine.playbackSnapshot();
    if (!busy && source) {
        const auto before = project; std::vector<CueAssignment> notes; notes.reserve(32);
        CueAssignment event;
        while (notes.size() < 256 && engine.popPendingCue(event)) {
            if (event.pad < 0 || event.pad >= 32) continue;
            if (project.pads[static_cast<std::size_t>(event.pad)].empty()) assignCueAt(project, event.pad, event.frame, source->sampleRate, source->frames());
            notes.push_back(event);
        }
        if (!notes.empty()) {
            pushUndo(before); selectedPad = notes.back().pad; activeBank = selectedPad / 16; selection = std::uint32_t(1) << selectedPad;
            publishProject();
            for (const auto& note : notes) if (note.held || project.playbackMode == CuePlaybackMode::Trigger) engine.triggerPad(note.pad, note.semitoneOffset, note.velocity);
        }
    }

    if (!busy && playback.triggerSerial != lastObservedTrigger) {
        lastObservedTrigger = playback.triggerSerial;
        if (playback.lastTriggeredPad >= 0 && playback.lastTriggeredPad < 32) {
            selectedPad = playback.lastTriggeredPad; activeBank = selectedPad / 16; selection = std::uint32_t(1) << selectedPad;
            syncSelectionToEngine(); refreshControls(); refreshWaveforms(); if (stemsReady) updateStemWaveform();
        }
    }
    overview.setPlayback(playback); waveform.setPlayback(playback);
    for (int i = 0; i < 32; ++i) { auto& p = *pads[static_cast<std::size_t>(i)]; const bool active = (playback.activeCueMask & (std::uint32_t(1) << i)) != 0; if (active != p.active) { p.active = active; p.repaint(); } }
    playButton.setButtonText(playback.sourcePlaying ? "PAUSE SOURCE" : "AUDITION");
    if (source) trackInfo.setText(juce::String(project.sourceKey) + "     " + juce::String(project.sourceBpm, 2) + " BPM     " + timeText(playback.sourceFrame / source->sampleRate), juce::dontSendNotification);
    if (busy) { progressDisplay = taskProgress.load(); repaint(); }
    if (stemSmokeStage == 1 && !busy && !waveformWorking && stemsReady && displayedStemMask) {
        bool aligned = true, distinct = false;
        for (const auto& s : stems) aligned = aligned && s && s->frames() == source->frames() && s->sampleRate == source->sampleRate && s->channels.size() == source->channels.size();
        if (aligned) for (std::size_t i = 0; i < source->frames(); i += 97)
            if (std::abs(stems[0]->channels[0][i] - stems[3]->channels[0][i]) > 1e-7f) { distinct = true; break; }
        bool changed = false;
        if (stemAnalysis.waveform.size() == analysis.waveform.size()) for (std::size_t i = 0; i < analysis.waveform.size(); ++i)
            if (std::abs(stemAnalysis.waveform[i].peak - analysis.waveform[i].peak) > 1e-5f) { changed = true; break; }
        auto* checks = stemSmokeChecks.getDynamicObject();
        checks->setProperty("nativeFourStemsAligned", aligned); checks->setProperty("nativeOutputsAreDistinct", distinct);
        checks->setProperty("stemClickMutesVocals", project.stemMute[0]); checks->setProperty("waveformChangesWithStemMix", changed);
        stemSmokeStage = 2; requestStem(2, true);
    } else if (stemSmokeStage == 2 && !busy && !waveformWorking && displayedStemMask) {
        const bool solo = project.stemMute[0] && project.stemMute[1] && !project.stemMute[2] && project.stemMute[3];
        bool matching = waveformAudio && waveformAudio->frames() == source->frames();
        if (matching) for (std::size_t i = 0; i < source->frames(); i += 97)
            if (std::abs(waveformAudio->channels[0][i] - stems[2]->channels[0][i]) > 1e-5f) { matching = false; break; }
        stemSmokeChecks.getDynamicObject()->setProperty("shiftClickSolosBass", solo);
        stemSmokeChecks.getDynamicObject()->setProperty("soloWaveformUsesActualStemAudio", matching);
        stemSmokeStage = 3; project = stemSmokeOriginal; selection = stemSmokeSelection; selectedPad = stemSmokeSelectedPad; publishProject();
    } else if (stemSmokeStage == 3 && !busy && !waveformWorking) {
        stemSmokeStage = 4; status = "Native stem UI workflow verified"; statusLabel.setText(status, juce::dontSendNotification);
    }
}

void MainComponent::cycleKeyDisplay() {
    keyDisplayFormat = (keyDisplayFormat + 1) % 3;
    globalPitch.updateText();
}
void MainComponent::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isAltDown() && keyMode->getToggleState() &&
        (event.eventComponent == &globalPitch || globalPitch.isParentOf(event.eventComponent))) {
        if (!keyCycleGesture) { keyCycleGesture = true; keyCycleOriginalPitch = project.globalPitchSemitones; cycleKeyDisplay(); }
        globalPitch.hideTextBox(true);
        return;
    }
    if (!event.mods.isShiftDown()) return;
    auto* s = dynamic_cast<juce::Slider*>(event.eventComponent); if (!s) return;
    if (s == &level || s == &attack || s == &cuePitch || s == &globalPitch || s == &globalDetune || s == &glide) s->setValue(0);
    else if (s == &filter) s->setValue(.5);
    else if (s == &release) s->setValue(10000);
    else if (s == &speed) s->setValue(1);
}
void MainComponent::mouseUp(const juce::MouseEvent&) {
    if (!keyCycleGesture) return;
    globalPitch.setValue(keyCycleOriginalPitch, juce::dontSendNotification); globalPitch.updateText();
    // Keep the guard until child increment-button callbacks have finished.
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    juce::MessageManager::callAsync([safe] {
        if (!safe) return;
        safe->globalPitch.hideTextBox(true);
        safe->globalPitch.setValue(safe->project.globalPitchSemitones, juce::dontSendNotification);
        safe->globalPitch.updateText(); safe->keyCycleGesture = false; safe->gestureBefore.reset();
    });
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) {
        const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        if (ch == 'z') { if (key.getModifiers().isShiftDown()) redo(); else undo(); return true; }
        if (ch == 'a') { selectAll(); return true; }
        return false;
    }
    if (key.getKeyCode() == juce::KeyPress::spaceKey) { if (source && !busy) { if (playback.sourcePlaying) engine.pauseSource(); else engine.playSource(); } return true; }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (key.getKeyCode() == '+' || key.getKeyCode() == '=') { waveform.zoomIn(); return true; }
    if (key.getKeyCode() == '-') { waveform.zoomOut(); return true; }
    if (busy || !source || key.getModifiers().isAltDown()) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    const int position = juce::String(typingKeys).indexOfChar(ch); if (position < 0) return false;
    if (heldKeys.count(static_cast<int>(ch))) return true;
    const int pad = project.keyboardMode ? selectedPad : activeBank * 16 + position;
    const float offset = project.keyboardMode ? static_cast<float>(position) : 0;
    heldKeys.emplace(static_cast<int>(ch), std::make_pair(pad, offset));
    if (!project.keyboardMode) { selectedPad = pad; selection = std::uint32_t(1) << pad; }
    triggerCue(pad, offset); return true;
}
bool MainComponent::keyStateChanged(bool isDown) {
    if (isDown) return false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();) {
        if (!juce::KeyPress::isKeyCurrentlyDown(it->first) && !juce::KeyPress::isKeyCurrentlyDown(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(it->first)))) {
            releaseCue(it->second.first, it->second.second); it = heldKeys.erase(it);
        } else ++it;
    }
    return true;
}

juce::var MainComponent::runWorkflowSmoke() {
    if (!isReadyForSmokeTest()) throw std::runtime_error("UI workflow smoke requires loaded demo audio");
    const auto original = project;
    const auto originalSelection = selection;
    const auto originalSelected = selectedPad;
    const auto originalMode = autoMode;
    const auto originalKeyFormat = keyDisplayFormat;
    const auto originalUndo = undoStack, originalRedo = redoStack;
    const auto originalPlayback = playback;
    const auto originalBank = activeBank; const auto originalModule = activeModule;
    const juce::Point<int> originalSize(getWidth(), getHeight());
    auto result = juce::var(new juce::DynamicObject());
    auto require = [&](const char* name, bool passed) {
        result.getDynamicObject()->setProperty(name, passed);
        if (!passed) throw std::runtime_error(std::string("UI workflow failed: ") + name);
    };
    auto restore = [&] {
        engine.stop(); engine.pauseSource(); project = original;
        selection = originalSelection; selectedPad = originalSelected; activeBank = originalBank; activeModule = originalModule; autoMode = originalMode;
        keyDisplayFormat = originalKeyFormat; keyCycleGesture = false;
        undoStack = originalUndo; redoStack = originalRedo; playback = originalPlayback;
        gridEditing = false; gridDirty = false; optionsOpen = false;
        waveform.setGridEditMode(false); waveform.setFrozen(false); waveform.centreOn(0);
        engine.seekSource(originalPlayback.sourceFrame); setSize(originalSize.x, originalSize.y); publishProject();
    };
    try {
        auto usable = [this](const juce::Component& control) {
            return control.isVisible() && control.isEnabled() && !control.getBounds().isEmpty() && getLocalBounds().contains(control.getBounds());
        };
        auto clickPad = [this](int index, juce::ModifierKeys modifiers = {}) {
            auto* target = pads[static_cast<std::size_t>(index)].get(); const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), { 12, 12 }, modifiers, 1, 0, 0, 0, 0, target, target, now, { 12, 12 }, now, 1, false);
            target->mouseDown(event); target->mouseUp(event);
        };
        bankB.onClick(); selectedPad = 16; selection = std::uint32_t(1) << 16; deleteSelection();
        playback.sourceFrame = static_cast<std::size_t>(source->sampleRate * .187); project.quantize = false; publishProject();
        clickPad(16);
        require("bankBWidgetAssignsCue17", activeBank == 1 && !project.pads[16].empty() && project.pads[16].start == playback.sourceFrame && usable(*pads[16]) && !pads[0]->isVisible());
        bankA.onClick(); clickPad(0); bankB.onClick(); clickPad(16, juce::ModifierKeys(juce::ModifierKeys::commandModifier));
        moduleButtons[0].onClick(); level.setValue(-4, juce::sendNotificationSync);
        require("multiSelectEditsHiddenBankCue", (selection & 1) && (selection & (std::uint32_t(1) << 16)) && project.pads[0].levelDb == -4 && project.pads[16].levelDb == -4 && usable(level));
        project.keyboardMode = false; publishProject(); bankB.onClick();
        keyPressed(juce::KeyPress('a', {}, 'a'));
        const bool typedBankB = heldKeys.count('a') && heldKeys.at('a').first == 16;
        bankA.onClick(); const bool heldOriginal = heldKeys.count('a') && heldKeys.at('a').first == 16; keyStateChanged(false);
        require("typingFollowsBankAndReleaseRetainsOriginalCue", typedBankB && heldOriginal && !heldKeys.count('a') && activeBank == 0);
        const auto originalCue = project.pads[0]; cueMenuAction(0, 231);
        require("contextCopyReachesOtherBank", activeBank == 1 && usable(*pads[31]) && project.pads[31].start == originalCue.start && project.pads[31].colour == originalCue.colour && project.pads[0].levelDb == originalCue.levelDb);
        selectAllButton.onClick(); require("selectAllRetainsBothBanks", selection == occupiedMask() && (selection & 0xffff) && (selection >> 16));
        selection = 1; selectedPad = 0; project.keyboardMode = true; project.mono = true; publishProject();
        auto inspectLayout = [&] {
            bool valid = true;
            for (int bank = 0; bank < 2; ++bank) {
                (bank == 0 ? bankA : bankB).onClick(); int visiblePads = 0;
                for (int i = 0; i < 32; ++i) { if (pads[static_cast<std::size_t>(i)]->isVisible()) { ++visiblePads; valid = valid && i / 16 == bank && usable(*pads[static_cast<std::size_t>(i)]) && pads[static_cast<std::size_t>(i)]->getWidth() == pads[static_cast<std::size_t>(i)]->getHeight() && pads[static_cast<std::size_t>(i)]->getWidth() >= 90; } }
                valid = valid && visiblePads == 16;
            }
            for (int m = 0; m < 5; ++m) {
                moduleButtons[static_cast<std::size_t>(m)].onClick();
                for (auto& button : moduleButtons) valid = valid && usable(button);
                valid = valid && usable(playButton) && playButton.getY() < overview.getY();
                if (m == 0) valid = valid && usable(level) && usable(filter) && !attack.isVisible();
                if (m == 1) valid = valid && usable(currentBpm) && usable(globalPitch) && usable(speed) && usable(cuePitch) && !level.isVisible();
                if (m == 2) valid = valid && usable(attack) && usable(release) && usable(glide) && usable(glideMode);
                if (m == 3) for (auto& stem : stemButtons) valid = valid && usable(*stem);
                if (m == 4) { valid = valid && usable(gridBpm) && usable(gridStart) && usable(gridClearAll) && usable(gridCancel) && parameterDeck.contains(gridCancel.getBounds()); gridCancel.onClick(); }
            }
            return valid;
        };
        require("squarePadsAndAllModulesReachableAtFullSize", inspectLayout());
        setSize(980, 620); require("squarePadsAndAllModulesReachableAtMinimumSize", inspectLayout());
        moduleButtons[static_cast<std::size_t>(SampleModule::Tune)].onClick();
        currentBpm.showTextBox();
        juce::Component::SafePointer<juce::TextEditor> tempoEditor;
        for (auto* child : currentBpm.getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child))
            if (auto* editor = label->getCurrentTextEditor()) tempoEditor = editor;
        if (tempoEditor) { tempoEditor->setText("118.50", false); tempoEditor->grabKeyboardFocus(); }
        refreshControls();
        const bool editorRetained = tempoEditor && tempoEditor->isShowing() && tempoEditor->hasKeyboardFocus(false) && tempoEditor->getText() == "118.50";
        currentBpm.hideTextBox(false);
        require("numericEditorRetainsFocusAcrossControlRefresh", editorRetained && std::abs(project.projectBpm - 118.5) < .0001);
        restore();
        project.quantize = false; publishProject();
        selectedPad = 31; selection = std::uint32_t(1) << 31;
        deleteSelection();
        require("deleteCue", project.pads[31].empty());
        playback.sourceFrame = static_cast<std::size_t>(source->sampleRate * .123);
        selectPad(31, {}, true);
        require("manualCueAtPlayhead", project.pads[31].start == playback.sourceFrame);
        require("cueWithoutEndRunsToFileEnd", !project.pads[31].hasEnd && project.pads[31].end == source->frames());
        selectPad(0, {}, false); selectPad(1, juce::ModifierKeys(juce::ModifierKeys::commandModifier), false);
        level.setValue(-6, juce::sendNotificationSync);
        require("commandMultiSelectParameterEdit", project.pads[0].levelDb == -6 && project.pads[1].levelDb == -6 && bitCount(selection) == 2);
        require("multipleSelectionUsesWhiteParameters", selectionColour() == white);
        selectPad(0, {}, false);
        favoriteButton.setToggleState(true, juce::dontSendNotification); favoriteButton.onClick();
        const auto favoriteStart = project.pads[0].start; const auto favoriteColour = project.pads[0].colour;
        autoMode = ChopMode::Random; runAutoSet();
        require("autoSetRetainsFavorite", project.pads[0].favorite && project.pads[0].start == favoriteStart && project.pads[0].colour == favoriteColour && project.pads[0].levelDb == -6);
        selectPad(0, {}, false); level.setValue(-3, juce::sendNotificationSync);
        undo(); require("undoParameter", project.pads[0].levelDb == -6);
        redo(); require("redoParameter", project.pads[0].levelDb == -3);
        const auto copied = project.pads[0];
        movePad(0, 30, true);
        require("optionDragCopiesCueAndColour", project.pads[30].start == copied.start && project.pads[30].colour == copied.colour && project.pads[30].levelDb == copied.levelDb && project.pads[0].start == copied.start);
        const auto swapped = project.pads[29];
        movePad(30, 29, false);
        require("dragSwapsOccupiedCues", project.pads[29].start == copied.start && project.pads[30].start == swapped.start && project.pads[30].colour == swapped.colour);
        renameCueTo(29, "Smoke named Cue"); require("renameCue", project.pads[29].name == "Smoke named Cue");
        selectedPad = 28; selection = std::uint32_t(1) << 28; deleteSelection();
        if (!project.quantize) quantizeButton.onClick();
        playback.sourceFrame = static_cast<std::size_t>(source->sampleRate * .123);
        triggerCue(28);
        const double eighth = source->sampleRate * 60. / project.sourceBpm / 8.;
        const auto expected = static_cast<std::size_t>(std::llround(playback.sourceFrame / eighth) * eighth);
        require("quantizeSnapsOneEighthBeat", std::abs(static_cast<double>(project.pads[28].start) - expected) <= 1);
        if (!project.keyboardMode) keyboardButton.onClick();
        require("keyboardModeAction", project.keyboardMode);
        selectPad(29, {}, false); optionsOpen = true; refreshControls();
        outputRouting.setSelectedId(3, juce::sendNotificationSync);
        require("cueOutputRoutingAction", project.pads[29].outputChannel == 2);
        const auto beforeGrid = project.tempoMarkers;
        toggleGridEdit(true); gridClearAll.onClick();
        require("gridClearAllDisablesGrid", !project.beatgridEnabled && project.tempoMarkers.empty());
        gridCancel.onClick();
        require("gridCancelRestoresMarkers", project.beatgridEnabled && project.tempoMarkers.size() == beforeGrid.size());
        toggleGridEdit(true); playback.sourceFrame = static_cast<std::size_t>(source->sampleRate);
        gridSet.onClick(); gridSave.onClick();
        require("gridSaveRetainsTempoMarker", std::any_of(project.tempoMarkers.begin(), project.tempoMarkers.end(), [&](const TempoMarker& m) { return m.frame == playback.sourceFrame; }));
        waveform.setFrozen(true); require("waveformFreeze", waveform.isFrozen());
        waveform.zoomIn(); waveform.zoomOut();
        // Reproduce a source-audition mute followed by editing one Cue.
        // The same controller operation used by the stem button must unmute it.
        for (auto& cue : project.pads) cue.stemMute.fill(false);
        project.stemMute = { true, false, false, false };
        selection = 1; selectedPad = 0; publishProject();
        toggleStemMask(0, false);
        require("cueCanUnmutePreviousSourceMute", !project.stemMute[0] && !project.pads[0].stemMute[0] && project.pads[1].stemMute[0]);
        project.stemMute[2] = true; publishProject(); toggleStemMask(2, true);
        require("cueSoloOverridesPreviousSourceMute", !project.stemMute[2] && !project.pads[0].stemMute[2] && project.pads[0].stemMute[0] && project.pads[0].stemMute[1] && project.pads[0].stemMute[3]);
        const auto pitchBeforeAlt = project.globalPitchSemitones;
        const auto formatBeforeAlt = keyDisplayFormat;
        juce::Component* keyDisplayTarget = &globalPitch;
        for (int i = 0; i < globalPitch.getNumChildComponents(); ++i) if (dynamic_cast<juce::Label*>(globalPitch.getChildComponent(i))) { keyDisplayTarget = globalPitch.getChildComponent(i); break; }
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent altClick(juce::Desktop::getInstance().getMainMouseSource(), { 1, 1 }, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0, 0, 0, 0, 0, keyDisplayTarget, keyDisplayTarget, now, { 1, 1 }, now, 1, false);
        mouseDown(altClick);
        globalPitch.setValue(pitchBeforeAlt < 24 ? pitchBeforeAlt + 1 : pitchBeforeAlt - 1, juce::sendNotificationSync);
        mouseUp(altClick);
        require("altKeyDisplayCyclePreservesPitch", keyDisplayFormat == (formatBeforeAlt + 1) % 3 && project.globalPitchSemitones == pitchBeforeAlt && globalPitch.getValue() == pitchBeforeAlt && formattedKey("C minor", 0, 1) == "5A" && formattedKey("C minor", 0, 2) == "10m" && formattedKey("C major", 0, 1) == "8B" && formattedKey("C major", 0, 2) == "1d");
        restore();
        if (engine.deviceManager().getCurrentAudioDevice() == nullptr) {
            // Headless Linux has no hardware callback. Exercise the queued
            // workflow triggers and final Stop using the real audio callback.
            // Preparation and scratch allocation happen outside that callback.
            engine.prepareToPlay(512, 44100);
            juce::AudioBuffer<float> scratch(2, 512);
            const juce::AudioSourceChannelInfo block(&scratch, 0, 512);
            for (int i = 0; i < 3; ++i) engine.getNextAudioBlock(block);
        }
        return result;
    } catch (...) { restore(); throw; }
}

void MainComponent::beginStemSmoke() {
    if (!isReadyForSmokeTest()) throw std::runtime_error("Stem UI smoke requires a loaded source");
    stemSmokeChecks = juce::var(new juce::DynamicObject());
    stemSmokeOriginal = project; stemSmokeSelection = selection; stemSmokeSelectedPad = selectedPad;
    selection = 0; project.stemMute.fill(false); publishProject();
    stemSmokeStage = 1; requestStem(0, false);
}
bool MainComponent::isStemSmokeReady() const { return stemSmokeStage == 4 && !busy && !waveformWorking; }
juce::var MainComponent::stemSmokeResult() const { return stemSmokeChecks; }

} // namespace samplify
