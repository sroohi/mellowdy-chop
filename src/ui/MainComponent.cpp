#include "MainComponent.h"
#include <cmath>
#include <algorithm>
#include <numbers>
#include <stdexcept>

namespace samplify {
namespace {
const juce::Colour canvas { 0xff151719 }, panel { 0xff1d2023 }, inset { 0xff111315 };
const juce::Colour line { 0xff33383c }, text { 0xffeceee8 }, muted { 0xff959d9f }, lime { 0xffc5f16b };
constexpr const char* stemNames[] { "Vocals", "Melody", "Bass", "Drums" };
constexpr const char* padKeys = "1234qwerasdfzxcv";
juce::Font font(float height, bool bold = false) { return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain)); }
void title(juce::Graphics& g, juce::String value, juce::Rectangle<int> area, juce::Colour colour = muted, float size = 11) {
    g.setColour(colour); g.setFont(font(size, true)); g.drawText(value, area, juce::Justification::centredLeft);
}
void card(juce::Graphics& g, juce::Rectangle<int> bounds) {
    g.setColour(panel); g.fillRoundedRectangle(bounds.toFloat(), 9);
    g.setColour(line.withAlpha(0.6f)); g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 9, 1);
}
}

class MainComponent::StudioLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    StudioLookAndFeel() {
        setColour(juce::Label::textColourId, text);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303539));
        setColour(juce::TextButton::buttonOnColourId, lime);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, canvas);
        setColour(juce::ComboBox::backgroundColourId, inset);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, lime.withAlpha(.22f));
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::Slider::trackColourId, lime);
        setColour(juce::Slider::thumbColourId, lime);
        setColour(juce::Slider::backgroundColourId, line);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, inset);
        setColour(juce::Slider::textBoxOutlineColourId, line);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, lime);
        setColour(juce::ToggleButton::tickDisabledColourId, muted);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::backgroundColourId, inset);
        setColour(juce::TextEditor::outlineColourId, lime);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int height) override { return font(juce::jmin(13.0f, height * .42f), true); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool over, bool down) override {
        auto c = b.isEnabled() ? colour : colour.withAlpha(.3f);
        if (over) c = c.brighter(.13f);
        if (down) c = c.brighter(.2f);
        g.setColour(c); g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(.5f), 5);
        g.setColour(line); g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(.5f), 5, 1);
    }
};

class WaveformView final : public juce::Component {
public:
    Analysis analysis;
    std::size_t frames = 0, cueStart = 0, cueEnd = 0;
    double sampleRate = 44100;
    std::function<void(std::size_t, std::size_t)> onCue;
    void paint(juce::Graphics& g) override {
        auto box = getLocalBounds().toFloat();
        g.setColour(inset); g.fillRoundedRectangle(box, 7);
        auto plot = box.reduced(14, 24);
        if (analysis.waveform.empty() || frames == 0) {
            g.setColour(muted); g.setFont(font(15));
            g.drawText("Drop an audio file here to start chopping", box, juce::Justification::centred);
            return;
        }
        const float scale = plot.getWidth() / static_cast<float>(frames);
        g.setColour(lime.withAlpha(.055f));
        g.fillRect(plot.getX() + static_cast<float>(cueStart) * scale, plot.getY(),
                   static_cast<float>(cueEnd - cueStart) * scale, plot.getHeight());
        g.setColour(line.withAlpha(.55f));
        for (const auto b : analysis.beats) {
            const float x = plot.getX() + static_cast<float>(b) * scale;
            g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
        }
        for (int x = 0; x < static_cast<int>(plot.getWidth()); ++x) {
            const auto idx = juce::jmin(analysis.waveform.size() - 1,
                static_cast<std::size_t>(static_cast<double>(x) / plot.getWidth() * analysis.waveform.size()));
            const auto& b = analysis.waveform[idx];
            const float energy = std::max(.001f, b.low + b.mid + b.high);
            const float r = juce::jlimit(.15f, 1.f, b.low / energy * 1.8f);
            const float gr = juce::jlimit(.2f, 1.f, b.mid / energy * 1.8f);
            const float bl = juce::jlimit(.2f, 1.f, b.high / energy * 2.1f);
            g.setColour(juce::Colour::fromFloatRGBA(r, gr, bl, .86f));
            const float height = juce::jlimit(1.f, plot.getHeight() * .49f, b.peak * plot.getHeight() * .49f);
            g.drawVerticalLine(static_cast<int>(plot.getX()) + x, plot.getCentreY() - height, plot.getCentreY() + height);
        }
        for (auto value : { cueStart, cueEnd }) {
            const float x = plot.getX() + static_cast<float>(value) * scale;
            g.setColour(lime); g.drawLine(x, plot.getY() - 6, x, plot.getBottom() + 6, 1.5f);
            g.fillRect(x - 3, plot.getY() - 6, 6.f, 5.f);
        }
        g.setColour(muted); g.setFont(font(10));
        for (int i = 0; i <= 4; ++i) {
            const float x = plot.getX() + plot.getWidth() * i / 4.f;
            const auto label = juce::String(static_cast<double>(frames) / sampleRate * i / 4, 2) + " s";
            g.drawText(label, static_cast<int>(x) - (i == 4 ? 42 : 0), getHeight() - 20, 48, 16, juce::Justification::centredLeft);
        }
        title(g, "DRAG TO SET THE SELECTED PAD'S CUE RANGE", { 14, 5, getWidth() - 28, 16 }, muted.withAlpha(.7f), 9);
    }
    void mouseDown(const juce::MouseEvent& e) override { dragStart = sampleAt(e.position.x); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (frames == 0) return;
        const auto last = sampleAt(e.position.x);
        cueStart = std::min(dragStart, last);
        cueEnd = std::max(cueStart + 1, std::max(dragStart, last));
        cueEnd = std::min(cueEnd, frames);
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.getDistanceFromDragStart() > 3 && onCue && cueEnd > cueStart) onCue(cueStart, cueEnd);
    }
private:
    std::size_t dragStart = 0;
    std::size_t sampleAt(float x) const {
        const auto f = juce::jlimit(0.0, 1.0, (static_cast<double>(x) - 14) / std::max(1, getWidth() - 28));
        return static_cast<std::size_t>(f * static_cast<double>(frames > 0 ? frames - 1 : 0));
    }
};

class SamplerPadButton final : public juce::Component {
public:
    int index = 0;
    Pad pad;
    double sampleRate = 44100;
    bool selected = false;
    double litUntil = 0;
    std::function<void()> onHit;
    void paint(juce::Graphics& g) override {
        const bool lit = juce::Time::getMillisecondCounterHiRes() < litUntil;
        auto r = getLocalBounds().toFloat().reduced(1);
        g.setColour(lit ? lime.withAlpha(.25f) : selected ? juce::Colour(0xff2b3325) : juce::Colour(0xff25292c));
        g.fillRoundedRectangle(r, 7);
        g.setColour(selected ? lime : line); g.drawRoundedRectangle(r, 7, selected ? 1.6f : 1.f);
        const bool compactHeight = getHeight() < 65;
        g.setColour(selected ? lime : text); g.setFont(font(compactHeight ? 16.f : 21.f, true));
        g.drawText(juce::String(index + 1).paddedLeft('0', 2), 13, compactHeight ? 3 : 9, getWidth() - 26, 26, juce::Justification::centredLeft);
        if (getWidth() >= 95) {
            g.setColour(muted); g.setFont(font(10));
            const auto key = index < 16 ? juce::String::charToString(padKeys[index]).toUpperCase() : "M" + juce::String(36 + index);
            g.drawText(key, getWidth() - 39, compactHeight ? 8 : 13, 25, 17, juce::Justification::centredRight);
        }
        if (!compactHeight) {
            g.setColour(pad.empty() ? muted.withAlpha(.45f) : text.withAlpha(.75f));
            g.setFont(font(11));
            const auto detail = pad.empty() ? juce::String("Empty") : juce::String(static_cast<double>(pad.end - pad.start) / sampleRate, 2) + " s";
            g.drawText(detail, 13, getHeight() - 29, getWidth() - 26, 17, juce::Justification::centredLeft);
        }
        if (!pad.empty()) {
            const float w = juce::jlimit(5.f, static_cast<float>(getWidth() - 26), static_cast<float>((pad.end - pad.start) / sampleRate) * 22.f);
            g.setColour((selected ? lime : muted).withAlpha(.5f));
            g.fillRoundedRectangle(13, static_cast<float>(getHeight() - 8), w, 2, 1);
        }
    }
    void mouseDown(const juce::MouseEvent&) override { if (isEnabled() && onHit) onHit(); }
};

MainComponent::MainComponent() {
    theme = std::make_unique<StudioLookAndFeel>();
    setLookAndFeel(theme.get());
    setWantsKeyboardFocus(true);
    waveform = std::make_unique<WaveformView>(); addAndMakeVisible(*waveform);
    waveform->onCue = [this](std::size_t in, std::size_t out) {
        if (busy || !source) return;
        auto& p = project.pads[static_cast<std::size_t>(selectedPad)]; p.start = in; p.end = out;
        rebuildCache();
    };
    auto show = [this](juce::Component& c) { addAndMakeVisible(c); };
    for (auto* c : std::initializer_list<juce::Component*> {
        &importButton, &demoButton, &openButton, &saveButton, &settingsButton, &stopButton,
        &findButton, &gridButton, &randomButton, &exportButton, &exportAllButton, &learnButton, &clearStemsButton,
        &padCountBox, &divisionBox, &sourceKeyBox, &targetKeyBox, &tempoSync, &keySync, &monoToggle,
        &quantizeToggle, &keyboardToggle, &reverseToggle, &sourceTitle, &sourceDetails,
        &estimatedKeyLabel, &padTitle, &padRangeLabel, &sourceBpmLabel, &targetBpmLabel }) show(*c);
    sourceTitle.setFont(font(23, true)); sourceDetails.setColour(juce::Label::textColourId, muted);
    sourceDetails.setFont(font(12)); sourceTitle.setMinimumHorizontalScale(.75f);
    padTitle.setFont(font(19, true)); padRangeLabel.setFont(font(11)); padRangeLabel.setColour(juce::Label::textColourId, muted);
    sourceBpmLabel.setText("SOURCE BPM", juce::dontSendNotification); sourceBpmLabel.setFont(font(10, true));
    targetBpmLabel.setText("PROJECT BPM", juce::dontSendNotification); targetBpmLabel.setFont(font(10, true));
    importButton.setColour(juce::TextButton::buttonColourId, lime);
    importButton.setColour(juce::TextButton::textColourOffId, canvas);
    findButton.setColour(juce::TextButton::buttonColourId, lime.withAlpha(.17f));
    findButton.setColour(juce::TextButton::textColourOffId, lime);
    for (int i = 0; i < 32; ++i) {
        pads[static_cast<std::size_t>(i)] = std::make_unique<SamplerPadButton>();
        auto& b = *pads[static_cast<std::size_t>(i)]; b.index = i;
        b.onHit = [this, i] { selectPad(i, true); grabKeyboardFocus(); }; show(b);
    }
    for (int i = 0; i < 4; ++i) {
        auto& b = stemImportButtons[static_cast<std::size_t>(i)]; b.setButtonText("+  " + juce::String(stemNames[i]));
        b.onClick = [this, i] { importStem(i); }; show(b);
        auto& mute = stemMuteButtons[static_cast<std::size_t>(i)]; mute.setButtonText(stemNames[i]);
        mute.setTooltip("Mute this imported stem for the selected pad");
        mute.onClick = [this] { commitInspector(); }; show(mute);
    }
    auto slider = [this, &show](juce::Slider& s, double lo, double hi, double step, juce::String suffix, juce::Label* label = nullptr, juce::String name = {}) {
        show(s); s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 69, 22); s.setRange(lo, hi, step); s.setTextValueSuffix(suffix);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        if (label != nullptr) { show(*label); label->setText(name, juce::dontSendNotification); label->setFont(font(11)); label->setColour(juce::Label::textColourId, muted); }
    };
    slider(sourceBpm, 30, 300, .1, ""); slider(targetBpm, 30, 300, .1, "");
    sourceBpm.setTooltip("Analysis estimate - edit to correct half/double tempo");
    slider(master, 0, 1, .01, ""); master.setValue(.8, juce::dontSendNotification);
    master.onValueChange = [this] { engine.setMasterGain(static_cast<float>(master.getValue())); };
    master.setTooltip("Master output gain");
    slider(gain, -60, 12, .1, " dB", &gainLabel, "Level");
    slider(pitch, -24, 24, 1, " st", &pitchLabel, "Pitch");
    slider(detune, -100, 100, 1, " ct", &detuneLabel, "Detune");
    slider(stretch, .25, 4, .01, " x", &stretchLabel, "Duration");
    slider(filter, 0, 1, .01, "", &filterLabel, "LP  <  Filter  >  HP");
    slider(attack, 0, 2000, 1, " ms", &attackLabel, "Attack");
    slider(release, 0, 5000, 1, " ms", &releaseLabel, "Release");
    slider(cueIn, 0, 1, .001, " s", &cueInLabel, "Cue in");
    slider(cueOut, .001, 1, .001, " s", &cueOutLabel, "Cue out");
    for (auto* s : { &gain, &pitch, &detune, &stretch, &filter, &attack, &release, &cueIn, &cueOut }) {
        s->onDragEnd = [this] { commitInspector(); };
        s->onValueChange = [this] { if (!refreshing && !juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()) commitInspector(); };
    }
    sourceBpm.onDragEnd = [this] { if (!refreshing) { project.sourceBpm = sourceBpm.getValue(); rebuildCache(); } };
    targetBpm.onDragEnd = [this] { if (!refreshing) { project.projectBpm = targetBpm.getValue(); rebuildCache(); } };
    sourceBpm.onValueChange = [this] { if (!refreshing && !juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()) { project.sourceBpm = sourceBpm.getValue(); rebuildCache(); } };
    targetBpm.onValueChange = [this] { if (!refreshing && !juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()) { project.projectBpm = targetBpm.getValue(); rebuildCache(); } };
    padCountBox.addItem("16 pads", 16); padCountBox.addItem("32 pads", 32); padCountBox.setSelectedId(16);
    divisionBox.addItem("1/2 beat", 1); divisionBox.addItem("1 beat", 2); divisionBox.addItem("1 bar", 3); divisionBox.addItem("2 bars", 4); divisionBox.setSelectedId(2);
    padCountBox.onChange = [this] { if (refreshing) return; project.padCount = padCountBox.getSelectedId(); rebuildPads(ChopMode::Transients); resized(); };
    constexpr const char* notes[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i) {
        for (int m = 0; m < 2; ++m) {
            const juce::String value = juce::String(notes[i]) + (m == 0 ? " major" : " minor");
            sourceKeyBox.addItem(value, i * 2 + m + 1); targetKeyBox.addItem(value, i * 2 + m + 1);
        }
    }
    sourceKeyBox.setTextWhenNothingSelected("Unknown key");
    sourceKeyBox.setTooltip("Estimated source key; change to correct the estimate");
    sourceKeyBox.onChange = [this] { if (!refreshing) { project.sourceKey = sourceKeyBox.getText().toStdString(); rebuildCache(); } };
    targetKeyBox.onChange = [this] { if (!refreshing) { project.projectKey = targetKeyBox.getText().toStdString(); rebuildCache(); } };
    tempoSync.onClick = [this] { project.syncTempo = tempoSync.getToggleState(); rebuildCache(); };
    keySync.onClick = [this] { project.syncKey = keySync.getToggleState(); rebuildCache(); };
    monoToggle.onClick = [this] { project.mono = monoToggle.getToggleState(); engine.setMono(project.mono); };
    quantizeToggle.onClick = [this] { project.quantize = quantizeToggle.getToggleState(); rebuildCache(); };
    keyboardToggle.onClick = [this] { keyboardMode = keyboardToggle.getToggleState(); engine.setKeyboardMode(keyboardMode, selectedPad); grabKeyboardFocus(); repaint(); };
    reverseToggle.onClick = [this] { commitInspector(); };
    importButton.onClick = [this] { importAudio(); }; demoButton.onClick = [this] { loadDemo(); };
    openButton.onClick = [this] { openSession(); }; saveButton.onClick = [this] { saveSession(); };
    stopButton.onClick = [this] { stopPlayback(); }; settingsButton.onClick = [this] { showAudioSettings(); };
    findButton.onClick = [this] { rebuildPads(ChopMode::Transients); };
    gridButton.onClick = [this] { rebuildPads(ChopMode::Grid); };
    randomButton.onClick = [this] { ++randomSeed; rebuildPads(ChopMode::Random); };
    exportButton.onClick = [this] { exportSelected(); }; exportAllButton.onClick = [this] { exportAll(); };
    learnButton.onClick = [this] { engine.learnMidiPad(selectedPad); status = "Play a MIDI note to map pad " + juce::String(selectedPad + 1); repaint(); };
    clearStemsButton.onClick = [this] {
        if (busy || !source) return;
        auto updated = project;
        for (auto& pad : updated.pads) pad.stemMute = {};
        runTask("Restoring the original source mix...", [this, updated] {
            engine.clearStems(); engine.preparePads(updated);
            return [this, updated] { project = updated; stems = {}; stemFiles = {}; status = "Stems cleared - playing the original source"; };
        });
    };
    // All child objects must exist before setSize invokes resized().
    setSize(1360, 900);
    audioWarning = engine.initialise(); engine.setMasterGain(.8f);
    refreshAll(); startTimerHz(30); loadDemo();
}

MainComponent::~MainComponent() {
    stopTimer(); engine.stop();
    if (worker.joinable()) worker.join();
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(canvas);
    g.setColour(juce::Colour(0xff1a1d1f)); g.fillRect(0, 0, getWidth(), 68);
    g.setColour(line); g.drawHorizontalLine(68, 0, static_cast<float>(getWidth()));
    g.setColour(lime); g.fillRoundedRectangle(22, 19, 30, 30, 7);
    g.setColour(canvas); g.setFont(font(21, true)); g.drawText("S", 22, 19, 30, 30, juce::Justification::centred);
    title(g, "SAMPLIFY", { 64, 15, 190, 25 }, text, 22);
    title(g, "MP AUDIO  /  STANDALONE SAMPLER", { 65, 40, 270, 16 }, muted, 9);
    const int sideWidth = 210, inspectorWidth = 274, ix = getWidth() - inspectorWidth - 18;
    g.setColour(line.withAlpha(.6f)); g.drawVerticalLine(sideWidth + 12, 86, static_cast<float>(getHeight() - 54));
    title(g, "WORKSPACE", { 23, 89, 170, 20 });
    title(g, "SAMPLE SOURCE", { 23, 218, 170, 20 });
    g.setColour(muted); g.setFont(font(12));
    g.drawFittedText("WAV, AIFF, FLAC, MP3\nDrop files onto the waveform.", { 24, 244, 177, 46 }, juce::Justification::topLeft, 3);
    title(g, "IMPORTED STEMS", { 23, 312, 175, 20 });
    g.setColour(muted); g.setFont(font(11));
    g.drawFittedText("Match source length, sample rate\nand channel count for alignment.", { 24, 339, 178, 40 }, juce::Justification::topLeft, 3);
    g.setColour(muted.withAlpha(.7f)); g.setFont(font(10));
    if (getHeight() >= 850)
        g.drawFittedText("Imported stems only; no AI model.\nWhen stems are loaded, only\nloaded stems are mixed.", { 24, 602, 178, 43 }, juce::Justification::topLeft, 3);
    const int cx = sideWidth + 32, cw = ix - cx - 20;
    card(g, { cx, 87, cw, 283 });
    title(g, "SAMPLE OVERVIEW", { cx + 16, 99, cw - 32, 16 }, muted, 9);
    title(g, "CHOP WORKFLOW", { cx, 386, 160, 17 });
    title(g, "PERFORMANCE PADS", { cx, 462, 200, 18 });
    const auto hint = keyboardMode ? "KEYBOARD: A W S E D F T G Y H U J K   |   MIDI C4 = ORIGINAL PITCH" : "KEYS: 1 2 3 4 / Q W E R / A S D F / Z X C V   |   SPACE TO STOP";
    title(g, hint, { cx, getHeight() - 85, cw, 24 }, muted, 9);
    card(g, { ix, 87, inspectorWidth, getHeight() - 143 });
    title(g, "PAD INSPECTOR", { ix + 16, 99, 230, 17 }, muted, 9);
    const float inspectorScale = std::min(1.f, static_cast<float>(getHeight() - 230) / 670.f);
    title(g, "STEM MUTES", { ix + 16, 87 + static_cast<int>(541 * inspectorScale), 230, 20 }, muted, 10);
    title(g, "MASTER", { 24, getHeight() - 148, 80, 18 });
    g.setColour(line); g.drawHorizontalLine(getHeight() - 38, 0, static_cast<float>(getWidth()));
    const auto stateColour = busy ? lime : !audioWarning.isEmpty() ? juce::Colour(0xffffbb66) : muted;
    g.setColour(stateColour); g.fillEllipse(18, static_cast<float>(getHeight() - 24), 6, 6);
    title(g, status, { 32, getHeight() - 33, getWidth() - 520, 28 }, stateColour, 11);
    auto device = audioWarning.isEmpty() ? engine.deviceName() : "No audio output | choose Audio / MIDI";
    title(g, device + "   |   " + juce::String(engine.activeVoiceCount()) + " voices", { getWidth() - 474, getHeight() - 33, 450, 28 }, muted, 10);
    const int meterX = 24, meterY = getHeight() - 78;
    g.setColour(inset); g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(meterY), 173, 5, 2);
    g.setColour(peak > .96f ? juce::Colour(0xfff07869) : lime);
    g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(meterY), 173 * std::min(1.f, peak), 5, 2);
}

void MainComponent::resized() {
    const int ix = getWidth() - 292, cx = 242, cw = ix - cx - 20;
    const int headerRight = getWidth() - 18;
    settingsButton.setBounds(headerRight - 120, 21, 120, 29);
    stopButton.setBounds(headerRight - 190, 21, 60, 29);
    monoToggle.setBounds(headerRight - 285, 20, 85, 30);
    quantizeToggle.setBounds(headerRight - 390, 20, 100, 30);
    keyboardToggle.setBounds(headerRight - 505, 20, 110, 30);
    importButton.setBounds(23, 123, 175, 36);
    demoButton.setBounds(23, 169, 175, 30);
    openButton.setBounds(23, 649, 77, 30); saveButton.setBounds(107, 649, 91, 30);
    for (int i = 0; i < 4; ++i) stemImportButtons[static_cast<std::size_t>(i)].setBounds(23, 391 + i * 42, 175, 33);
    clearStemsButton.setBounds(23, 567, 175, 26);
    const bool compact = getHeight() < 850;
    openButton.setVisible(!compact); saveButton.setVisible(!compact);
    master.setBounds(23, getHeight() - 125, 175, 34);
    sourceTitle.setBounds(cx + 12, 120, cw - 200, 35);
    sourceDetails.setBounds(cx + 12, 155, cw - 24, 22);
    estimatedKeyLabel.setBounds(cx + cw - 178, 122, 161, 28);
    waveform->setBounds(cx + 14, 185, cw - 28, 171);
    findButton.setBounds(cx, 411, 125, 33); gridButton.setBounds(cx + 133, 411, 103, 33);
    randomButton.setBounds(cx + 244, 411, 108, 33);
    divisionBox.setBounds(cx + 360, 411, 92, 33);
    padCountBox.setBounds(cx + cw - 105, 411, 105, 33);
    const int columns = project.padCount == 32 ? 8 : 4;
    const int gap = 9, startY = 491, bottom = getHeight() - 91;
    const int pw = (cw - (columns - 1) * gap) / columns;
    const int ph = std::max(36, (bottom - startY - gap * 3) / 4);
    for (int i = 0; i < 32; ++i) {
        auto& b = *pads[static_cast<std::size_t>(i)]; b.setVisible(i < project.padCount);
        b.setBounds(cx + (i % columns) * (pw + gap), startY + (i / columns) * (ph + gap), pw, ph);
    }
    const float sy = std::min(1.f, static_cast<float>(getHeight() - 230) / 670.f);
    auto y = [sy](int v) { return 87 + static_cast<int>((v - 87) * sy); };
    padTitle.setBounds(ix + 14, y(122), 244, 28); padRangeLabel.setBounds(ix + 14, y(152), 244, 19);
    sourceBpmLabel.setBounds(ix + 14, y(185), 111, 17); targetBpmLabel.setBounds(ix + 146, y(185), 112, 17);
    sourceBpm.setBounds(ix + 12, y(207), 117, 26); targetBpm.setBounds(ix + 144, y(207), 117, 26);
    sourceKeyBox.setBounds(ix + 16, y(244), 113, 23); targetKeyBox.setBounds(ix + 146, y(244), 113, 23);
    tempoSync.setBounds(ix + 13, y(278), 124, 24); keySync.setBounds(ix + 144, y(278), 115, 24);
    auto row = [&](juce::Slider& s, juce::Label& l, int baseY) { l.setBounds(ix + 16, y(baseY), 105, 24); s.setBounds(ix + 111, y(baseY), 149, 24); };
    row(cueIn, cueInLabel, 318); row(cueOut, cueOutLabel, 347);
    row(gain, gainLabel, 382); row(pitch, pitchLabel, 411); row(detune, detuneLabel, 440);
    row(stretch, stretchLabel, 469); row(filter, filterLabel, 498);
    row(attack, attackLabel, 527); row(release, releaseLabel, 556);
    reverseToggle.setBounds(ix + 13, y(592), 122, 24); learnButton.setBounds(ix + 141, y(591), 118, 25);
    for (int i = 0; i < 4; ++i) stemMuteButtons[static_cast<std::size_t>(i)].setBounds(ix + 13 + (i % 2) * 132, y(655 + (i / 2) * 30), 124, 24);
    exportButton.setBounds(ix + 16, getHeight() - 104, 114, 32);
    exportAllButton.setBounds(ix + 144, getHeight() - 104, 114, 32);
}

juce::String MainComponent::timeText(double seconds) {
    const int minutes = static_cast<int>(seconds / 60);
    return juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(static_cast<int>(seconds) % 60).paddedLeft('0', 2);
}

void MainComponent::timerCallback() {
    peak = std::max(engine.peakLevel(), peak * .88f);
    const auto midi = engine.midiStatus();
    if (midi != lastMidiStatus) {
        lastMidiStatus = midi; learnButton.setTooltip(midi);
        if (!busy && midi.startsWith("MIDI ")) status = midi;
    }
    for (auto& b : pads) if (b->isVisible()) b->repaint();
    repaint(0, getHeight() - 155, getWidth(), 155);
}

void MainComponent::setBusy(bool value, const juce::String& message) {
    busy = value;
    if (message.isNotEmpty()) status = message;
    for (int i = 0; i < getNumChildComponents(); ++i) getChildComponent(i)->setEnabled(!busy);
    stopButton.setEnabled(true);
    if (!busy) {
        const bool loaded = source && source->frames() > 0;
        for (auto* b : { &findButton, &gridButton, &randomButton, &exportButton, &exportAllButton, &saveButton, &learnButton }) b->setEnabled(loaded);
        for (int i = 0; i < 4; ++i) stemMuteButtons[static_cast<std::size_t>(i)].setEnabled(stems[static_cast<std::size_t>(i)] != nullptr);
        clearStemsButton.setEnabled(std::any_of(stems.begin(), stems.end(), [](const auto& stem) { return stem != nullptr; }));
    }
    repaint();
}

void MainComponent::runTask(juce::String progress, std::function<std::function<void()>()> work) {
    if (busy) return;
    setBusy(true, progress);
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    if (worker.joinable()) worker.join();
    worker = std::thread([safe, work = std::move(work)] {
        std::function<void()> apply;
        juce::String error;
        try { apply = work(); } catch (const std::exception& e) { error = e.what(); } catch (...) { error = "Unexpected processing error"; }
        juce::MessageManager::callAsync([safe, apply = std::move(apply), error] {
            if (!safe) return;
            safe->setBusy(false);
            if (error.isNotEmpty()) { safe->reportError(error); return; }
            try { if (apply) apply(); safe->refreshAll(); }
            catch (const std::exception& e) { safe->reportError(e.what()); }
        });
    });
}

void MainComponent::reportError(const juce::String& error) {
    status = "Action failed - " + error; repaint();
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Samplify", error);
}

void MainComponent::refreshAll() {
    refreshing = true;
    sourceTitle.setText(sourceName, juce::dontSendNotification);
    const auto detail = source ? timeText(source->duration()) + "  |  " + juce::String(source->sampleRate / 1000., 1) + " kHz  |  "
        + juce::String(static_cast<int>(source->channels.size())) + " ch  |  " + juce::String(static_cast<int>(analysis.onsets.size())) + " onsets" : "Import a sample or load the demo";
    sourceDetails.setText(detail, juce::dontSendNotification);
    estimatedKeyLabel.setText(juce::String(project.sourceKey) + "   /   " + juce::String(project.sourceBpm, 1) + " BPM", juce::dontSendNotification);
    estimatedKeyLabel.setColour(juce::Label::textColourId, lime); estimatedKeyLabel.setFont(font(11, true));
    sourceBpm.setValue(project.sourceBpm, juce::dontSendNotification); targetBpm.setValue(project.projectBpm, juce::dontSendNotification);
    sourceKeyBox.setText(project.sourceKey, juce::dontSendNotification); targetKeyBox.setText(project.projectKey, juce::dontSendNotification);
    padCountBox.setSelectedId(project.padCount, juce::dontSendNotification);
    tempoSync.setToggleState(project.syncTempo, juce::dontSendNotification); keySync.setToggleState(project.syncKey, juce::dontSendNotification);
    monoToggle.setToggleState(project.mono, juce::dontSendNotification); quantizeToggle.setToggleState(project.quantize, juce::dontSendNotification);
    if (source) {
        waveform->analysis = analysis; waveform->frames = source->frames(); waveform->sampleRate = source->sampleRate;
    }
    for (int i = 0; i < 32; ++i) {
        auto& b = *pads[static_cast<std::size_t>(i)]; b.pad = project.pads[static_cast<std::size_t>(i)]; b.selected = i == selectedPad;
        if (source) b.sampleRate = source->sampleRate;
    }
    for (int i = 0; i < 4; ++i) {
        const auto loaded = stems[static_cast<std::size_t>(i)] != nullptr;
        stemImportButtons[static_cast<std::size_t>(i)].setButtonText((loaded ? "*  " : "+  ") + juce::String(stemNames[i]));
        stemImportButtons[static_cast<std::size_t>(i)].setTooltip(loaded ? stemFiles[static_cast<std::size_t>(i)].getFileName() + " - click to replace" : "Import an aligned " + juce::String(stemNames[i]) + " stem");
    }
    refreshing = false;
    refreshInspector(); setBusy(busy); resized(); repaint();
}

void MainComponent::refreshInspector() {
    refreshing = true;
    const auto& p = project.pads[static_cast<std::size_t>(selectedPad)];
    padTitle.setText("Pad " + juce::String(selectedPad + 1).paddedLeft('0', 2), juce::dontSendNotification);
    const auto sr = source ? source->sampleRate : 44100;
    padRangeLabel.setText(juce::String(p.start / sr, 3) + " > " + juce::String(p.end / sr, 3) + " s", juce::dontSendNotification);
    gain.setValue(p.levelDb, juce::dontSendNotification); pitch.setValue(p.pitchSemitones, juce::dontSendNotification);
    detune.setValue(p.detuneCents, juce::dontSendNotification); stretch.setValue(p.timeRatio, juce::dontSendNotification);
    filter.setValue(p.filter, juce::dontSendNotification); attack.setValue(p.attackMs, juce::dontSendNotification);
    release.setValue(p.releaseMs, juce::dontSendNotification); reverseToggle.setToggleState(p.reverse, juce::dontSendNotification);
    const double duration = source ? source->duration() : 1;
    cueIn.setRange(0, std::max(.001, duration), .001); cueOut.setRange(.001, std::max(.001, duration), .001);
    cueIn.setValue(p.start / sr, juce::dontSendNotification); cueOut.setValue(p.end / sr, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) stemMuteButtons[static_cast<std::size_t>(i)].setToggleState(p.stemMute[static_cast<std::size_t>(i)], juce::dontSendNotification);
    waveform->cueStart = p.start; waveform->cueEnd = p.end; waveform->repaint();
    refreshing = false;
}

void MainComponent::commitInspector() {
    if (busy || refreshing || !source) return;
    auto& p = project.pads[static_cast<std::size_t>(selectedPad)];
    p.levelDb = static_cast<float>(gain.getValue()); p.pitchSemitones = static_cast<float>(pitch.getValue());
    p.detuneCents = static_cast<float>(detune.getValue()); p.timeRatio = static_cast<float>(stretch.getValue());
    p.filter = static_cast<float>(filter.getValue()); p.attackMs = static_cast<float>(attack.getValue());
    p.releaseMs = static_cast<float>(release.getValue()); p.reverse = reverseToggle.getToggleState();
    p.start = std::min(source->frames() - 1, static_cast<std::size_t>(cueIn.getValue() * source->sampleRate));
    p.end = std::max(p.start + 1, std::min(source->frames(), static_cast<std::size_t>(cueOut.getValue() * source->sampleRate)));
    for (int i = 0; i < 4; ++i) p.stemMute[static_cast<std::size_t>(i)] = stemMuteButtons[static_cast<std::size_t>(i)].getToggleState();
    rebuildCache();
}

void MainComponent::selectPad(int index, bool trigger, float semitones) {
    if (busy || index < 0 || index >= project.padCount) return;
    selectedPad = index; engine.setKeyboardMode(keyboardMode, selectedPad);
    for (int i = 0; i < 32; ++i) pads[static_cast<std::size_t>(i)]->selected = i == selectedPad;
    if (trigger && !project.pads[static_cast<std::size_t>(index)].empty()) {
        engine.triggerPad(index, project.quantize, semitones);
        pads[static_cast<std::size_t>(index)]->litUntil = juce::Time::getMillisecondCounterHiRes() + 180;
        status = "Pad " + juce::String(index + 1) + (project.quantize ? " queued to next beat" : " triggered");
    }
    refreshInspector(); repaint();
}

void MainComponent::rebuildCache() {
    if (busy || !source) return;
    const auto snapshot = project;
    runTask("Rendering pad changes...", [this, snapshot] {
        engine.preparePads(snapshot);
        return [this] { status = "Pads ready"; };
    });
}

void MainComponent::rebuildPads(ChopMode mode) {
    if (busy || !source) return;
    const auto input = source; auto a = analysis; a.bpm = project.sourceBpm;
    auto updated = project; const auto seed = randomSeed;
    constexpr double divisions[] { .5, 1, 4, 8 };
    const double division = divisions[juce::jlimit(0, 3, divisionBox.getSelectedId() - 1)];
    runTask("Finding your next chop...", [this, input, a = std::move(a), updated, mode, division, seed]() mutable {
        const auto slices = makeSlices(*input, a, mode, updated.padCount, division, seed);
        updated.pads = {};
        for (std::size_t i = 0; i < slices.size() && i < updated.pads.size(); ++i) updated.pads[i] = slices[i];
        engine.preparePads(updated);
        return [this, updated, count = slices.size()] { project = updated; selectedPad = 0; status = juce::String(static_cast<int>(count)) + " slices ready - play a pad"; };
    });
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return !busy && files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;aiff;aif;flac;mp3;samplify");
}
void MainComponent::filesDropped(const juce::StringArray& files, int, int) { if (files.size() == 1 && !busy) loadFile(juce::File(files[0])); }

void MainComponent::importAudio() {
    if (busy) return;
    chooser = std::make_unique<juce::FileChooser>("Import a sample", sourceFile.getParentDirectory(), "*.wav;*.aiff;*.aif;*.flac;*.mp3");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& c) {
        if (safe && c.getResult().existsAsFile()) safe->loadSource(c.getResult());
    });
}

void MainComponent::loadFile(const juce::File& file) {
    if (file.hasFileExtension("samplify")) {
        try { const auto session = samplify::loadSession(file); loadSource(session.source, session); }
        catch (const std::exception& e) { reportError(e.what()); }
    } else loadSource(file);
}

void MainComponent::loadSource(const juce::File& file, const std::optional<Session>& restored) {
    if (busy) return;
    engine.stop();
    runTask("Loading and analysing " + file.getFileName() + "...", [this, file, restored] {
        auto input = std::make_shared<AudioData>(AudioEngine::readAudio(file));
        auto a = analyse(*input);
        Project updated;
        std::array<juce::File, 4> paths;
        std::array<std::shared_ptr<const AudioData>, 4> imported;
        if (restored) { updated = restored->project; paths = restored->stems; }
        else {
            updated.sourceBpm = a.bpm; updated.projectBpm = a.bpm;
            updated.sourceKey = a.key; if (a.key != "Unknown") updated.projectKey = a.key;
            const auto slices = makeSlices(*input, a, ChopMode::Transients, updated.padCount);
            for (std::size_t i = 0; i < slices.size() && i < updated.pads.size(); ++i) updated.pads[i] = slices[i];
        }
        for (const auto& pad : updated.pads) if (pad.start > input->frames() || pad.end > input->frames())
            throw std::runtime_error("Session cue points extend beyond the referenced source audio. Restore the original audio file.");
        for (int i = 0; i < 4; ++i) if (paths[static_cast<std::size_t>(i)] != juce::File()) {
            imported[static_cast<std::size_t>(i)] = std::make_shared<AudioData>(AudioEngine::readAudio(paths[static_cast<std::size_t>(i)]));
            const auto& s = *imported[static_cast<std::size_t>(i)];
            if (s.frames() != input->frames() || std::abs(s.sampleRate - input->sampleRate) > .01)
                throw std::runtime_error("Stem files must have the same sample rate and frame count as the source.");
        }
        engine.setSource(input); engine.clearStems();
        for (int i = 0; i < 4; ++i) if (imported[static_cast<std::size_t>(i)]) engine.setStem(i, imported[static_cast<std::size_t>(i)]);
        engine.preparePads(updated);
        return [this, input, a = std::move(a), updated, file, paths, imported]() mutable {
            source = input; analysis = std::move(a); project = updated; sourceFile = file;
            sourceName = file.getFileNameWithoutExtension(); stems = imported; stemFiles = paths; selectedPad = 0;
            status = "Sample ready - BPM and key are estimates; edit them to correct analysis";
        };
    });
}

void MainComponent::loadDemo() {
    if (busy) return;
    engine.stop();
    runTask("Building the demo groove...", [this] {
        auto data = std::make_shared<AudioData>(); data->sampleRate = 44100;
        const int length = 44100 * 8;
        data->channels.assign(2, std::vector<float>(static_cast<std::size_t>(length), 0));
        std::uint32_t noise = 27;
        for (int i = 0; i < length; ++i) {
            const double t = i / data->sampleRate, beat = std::fmod(t, .5), hat = std::fmod(t, .25);
            noise = noise * 1664525u + 1013904223u;
            const double n = static_cast<double>(noise >> 8) / 8388608.0 - 1;
            const double kick = std::sin(2 * std::numbers::pi * (48 * beat + 7 * (1 - std::exp(-beat * 40)))) * std::exp(-beat * 22) * .55;
            const double snare = std::fmod(t, 1.) >= .5 ? n * std::exp(-beat * 34) * .19 : 0;
            const double h = n * std::exp(-hat * 100) * .09;
            constexpr double notes[] { 130.8128, 155.5635, 195.9977, 174.6141 };
            const double freq = notes[static_cast<int>(t / 2) % 4];
            const double melody = (std::sin(2 * std::numbers::pi * freq * t) + .25 * std::sin(4 * std::numbers::pi * freq * t)) * .13 * (1 - std::exp(-beat * 60)) * std::exp(-beat * 3);
            const float sample = static_cast<float>(kick + snare + h + melody);
            data->channels[0][static_cast<std::size_t>(i)] = sample;
            data->channels[1][static_cast<std::size_t>(i)] = sample * .96f;
        }
        auto a = analyse(*data);
        Project updated; updated.sourceBpm = 120; updated.projectBpm = 120; updated.sourceKey = "C minor"; updated.projectKey = "C minor";
        const auto slices = makeSlices(*data, a, ChopMode::Grid, 16, 1);
        for (std::size_t i = 0; i < slices.size() && i < updated.pads.size(); ++i) updated.pads[i] = slices[i];
       #if JUCE_LINUX
        // The cloud validation machine has no writable user Application Support folder.
        const auto demo = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Samplify/Demo.wav");
       #else
        const auto demo = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Samplify/Demo.wav");
       #endif
        if (!demo.getParentDirectory().createDirectory()) throw std::runtime_error("Cannot create the demo audio folder.");
        AudioEngine::writeAudio(demo, *data);
        engine.setSource(data); engine.clearStems(); engine.preparePads(updated);
        return [this, data, a = std::move(a), updated, demo]() mutable {
            source = data; analysis = std::move(a); project = updated; sourceFile = demo; sourceName = "Midnight cuts | demo groove";
            stems = {}; stemFiles = {}; selectedPad = 0;
            status = "Demo ready | 120 BPM / C minor | hit a pad or drop your own sample";
        };
    });
}

void MainComponent::importStem(int index) {
    if (busy || !source) return;
    chooser = std::make_unique<juce::FileChooser>("Import aligned " + juce::String(stemNames[index]) + " stem", sourceFile.getParentDirectory(), "*.wav;*.aiff;*.aif;*.flac;*.mp3");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, index](const juce::FileChooser& c) {
        if (!safe || !c.getResult().existsAsFile()) return;
        const auto file = c.getResult();
        const auto snapshot = safe->project;
        safe->runTask("Loading " + juce::String(stemNames[index]) + " stem...", [safe, file, index, snapshot] {
            if (!safe) return std::function<void()>{};
            auto imported = std::make_shared<AudioData>(AudioEngine::readAudio(file));
            safe->engine.setStem(index, imported); safe->engine.preparePads(snapshot);
            return std::function<void()>([safe, file, imported, index] {
                if (!safe) return;
                safe->stemFiles[static_cast<std::size_t>(index)] = file; safe->stems[static_cast<std::size_t>(index)] = imported;
                safe->status = "Stem imported - only loaded stems are now mixed";
            });
        });
    });
}

Session MainComponent::currentSession() const { return { project, sourceFile, stemFiles }; }

void MainComponent::openSession() {
    if (busy) return;
    chooser = std::make_unique<juce::FileChooser>("Open Samplify session", juce::File(), "*.samplify");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& c) {
        if (safe && c.getResult().existsAsFile()) safe->loadFile(c.getResult());
    });
}

void MainComponent::saveSession() {
    if (busy || !source) return;
    chooser = std::make_unique<juce::FileChooser>("Save session (references source audio)", sourceFile.withFileExtension("samplify"), "*.samplify");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe](const juce::FileChooser& c) {
        if (!safe || c.getResult() == juce::File()) return;
        try { samplify::saveSession(c.getResult().withFileExtension("samplify"), safe->currentSession()); safe->status = "Session saved - keep referenced audio files available"; safe->repaint(); }
        catch (const std::exception& e) { safe->reportError(e.what()); }
    });
}

void MainComponent::exportSelected() {
    if (busy || !source || project.pads[static_cast<std::size_t>(selectedPad)].empty()) return;
    const auto name = "Samplify-pad-" + juce::String(selectedPad + 1).paddedLeft('0', 2) + ".wav";
    chooser = std::make_unique<juce::FileChooser>("Export selected pad as WAV", sourceFile.getSiblingFile(name), "*.wav");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe](const juce::FileChooser& c) {
        if (safe && c.getResult() != juce::File()) safe->exportTo(c.getResult().withFileExtension("wav"), false);
    });
}

void MainComponent::exportAll() {
    if (busy || !source) return;
    chooser = std::make_unique<juce::FileChooser>("Choose a folder for exported pads", sourceFile.getParentDirectory(), "*");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe](const juce::FileChooser& c) {
        if (!safe || !c.getResult().isDirectory()) return;
        // Each export gets a unique folder, preserving previous exports.
        safe->exportTo(c.getResult().getNonexistentChildFile("Samplify-pads", "", true), true);
    });
}

void MainComponent::exportTo(const juce::File& file, bool all) {
    const auto snapshot = project; const int selected = selectedPad;
    runTask("Rendering WAV export...", [this, snapshot, selected, file, all] {
        int count = 0;
        if (all && !file.createDirectory()) throw std::runtime_error("Cannot create the export folder.");
        for (int i = 0; i < snapshot.padCount; ++i) {
            if ((!all && i != selected) || snapshot.pads[static_cast<std::size_t>(i)].empty()) continue;
            auto output = engine.renderForExport(snapshot, i);
            const auto destination = all ? file.getChildFile("Pad-" + juce::String(i + 1).paddedLeft('0', 2) + ".wav") : file;
            AudioEngine::writeAudio(destination, output); ++count;
        }
        return [this, count, file] { status = juce::String(count) + " WAV " + (count == 1 ? "file" : "files") + " exported to " + file.getFullPathName(); };
    });
}

void MainComponent::showAudioSettings() {
    engine.refreshMidiInputs();
    auto content = std::make_unique<juce::AudioDeviceSelectorComponent>(engine.deviceManager(), 0, 0, 0, 2, true, false, true, false);
    content->setSize(560, 470);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Samplify | Audio and MIDI"; options.dialogBackgroundColour = panel;
    options.content.setOwned(content.release()); options.componentToCentreAround = this;
    options.useNativeTitleBar = true; options.resizable = false; options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
    audioWarning.clear();
}

void MainComponent::stopPlayback() { engine.stop(); heldKeys.clear(); status = "Playback stopped"; repaint(); }

bool MainComponent::isReadyForSmokeTest() const {
    return !busy && source && source->frames() > 0 && !analysis.waveform.empty()
        && std::any_of(project.pads.begin(), project.pads.end(), [](const Pad& pad) { return !pad.empty(); });
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown()) return false;
    if (key.getKeyCode() == juce::KeyPress::spaceKey) { stopPlayback(); return true; }
    if (busy || !source) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (heldKeys.count(static_cast<int>(ch)) != 0) return true;
    const juce::String mapping = keyboardMode ? "awsedftgyhujk" : padKeys;
    const int index = mapping.indexOfChar(ch);
    if (index < 0) return false;
    const int targetPad = keyboardMode ? selectedPad : index;
    const float offset = keyboardMode ? static_cast<float>(index) : 0;
    heldKeys.emplace(static_cast<int>(ch), std::make_pair(targetPad, offset));
    selectPad(targetPad, true, offset);
    return true;
}

bool MainComponent::keyStateChanged(bool isDown) {
    if (isDown) return false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();) {
        if (!juce::KeyPress::isKeyCurrentlyDown(it->first) && !juce::KeyPress::isKeyCurrentlyDown(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(it->first)))) {
            engine.releasePad(it->second.first, it->second.second);
            it = heldKeys.erase(it);
        } else ++it;
    }
    return true;
}

} // namespace samplify
