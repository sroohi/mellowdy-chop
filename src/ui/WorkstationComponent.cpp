#include "WorkstationComponent.h"
#include "plugins/PluginHost.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace samplify {
namespace {
using HW = HardwareLookAndFeel;
void legend(juce::Graphics& g, const juce::String& value, juce::Rectangle<float> r, float size = 11,
            juce::Justification alignment = juce::Justification::centredLeft) {
    g.setColour(HW::ink()); g.setFont(HW::labelFont(size)); g.drawText(value, r, alignment);
}
juce::Colour trackColour(int track) {
    constexpr std::uint32_t colours[] { 0xffb97b45, 0xffa09a55, 0xff77977a, 0xff709593, 0xff7c8fa5, 0xffa98c9b, 0xffb28b6c, 0xff969777 };
    return juce::Colour(colours[std::clamp(track, 0, 7)]);
}
double editQuantum(const WorkstationState& state) { return state.recordQuantizeBeats > 0 ? state.recordQuantizeBeats : .0625; }
}

class ArrangementCanvas final : public juce::Component {
public:
    explicit ArrangementCanvas(WorkstationState& s) : state(s) { setWantsKeyboardFocus(false); }
    WorkstationId selected = 0;
    double playhead = 0;
    std::function<void(int, double)> onAdd;
    std::function<void(WorkstationId)> onSelect, onDelete;
    std::function<void(WorkstationId, ArrangementClip, bool)> onChange;
    std::function<const std::vector<float>*(WorkstationId)> peaks;
    void updateSize(int viewportWidth) {
        double end = 64;
        for (const auto& c : state.arrangement) end = std::max(end, c.startBeat + c.lengthBeats + 8);
        setSize(std::max(viewportWidth, 110 + static_cast<int>(std::ceil(end / 4) * 4 * pixelsPerBeat)), 22 + rowHeight * 8);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(HW::lcd().darker(.035f));
        for (int t = 0; t < 8; ++t) {
            const auto row = juce::Rectangle<int>(0, 22 + t * rowHeight, getWidth(), rowHeight);
            g.setColour(t == state.selectedTrack ? HW::lcdInk().withAlpha(.08f) : juce::Colours::transparentBlack); g.fillRect(row);
            g.setColour(HW::lcdInk().withAlpha(.2f)); g.drawHorizontalLine(row.getBottom() - 1, 0, static_cast<float>(getWidth()));
            legend(g, juce::String(t + 1).paddedLeft('0', 2) + "  " + state.tracks[static_cast<std::size_t>(t)].name, row.withWidth(104).toFloat().reduced(6, 0), 10);
        }
        for (int b = 0; b < (getWidth() - 110) / pixelsPerBeat + 1; ++b) {
            const int x = 110 + b * pixelsPerBeat;
            g.setColour(HW::lcdInk().withAlpha(b % 4 == 0 ? .26f : .09f)); g.drawVerticalLine(x, 0, static_cast<float>(getHeight()));
            if (b % 4 == 0) legend(g, juce::String(b / 4 + 1), { static_cast<float>(x + 3), 1, 40, 18 }, 10);
        }
        for (const auto& clip : state.arrangement) drawClip(g, dragging && clip.id == selected ? preview : clip, clip.id == selected);
        if (playhead >= 0) { g.setColour(HW::red()); const float x = static_cast<float>(110 + playhead * pixelsPerBeat); g.drawLine(x, 0, x, static_cast<float>(getHeight()), 1.5f); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragStart = e.position; dragging = false; resize = false; copy = e.mods.isAltDown();
        for (auto it = state.arrangement.rbegin(); it != state.arrangement.rend(); ++it) if (bounds(*it).contains(e.position)) {
            selected = it->id; original = preview = *it; resize = e.position.x > bounds(*it).getRight() - 9;
            if (onSelect) onSelect(selected);
            if (e.mods.isPopupMenu()) {
                juce::PopupMenu menu; menu.addItem(1, "Duplicate"); menu.addItem(2, "Remove clip");
                auto safe = juce::Component::SafePointer<ArrangementCanvas>(this); const auto clip = *it;
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, clip](int result) {
                    if (!safe) return;
                    if (result == 1 && safe->onChange) { auto duplicate = clip; duplicate.startBeat += clip.lengthBeats; safe->onChange(clip.id, duplicate, true); }
                    else if (result == 2 && safe->onDelete) safe->onDelete(clip.id);
                });
            }
            repaint(); return;
        }
        selected = 0; if (onSelect) onSelect(0);
        if (e.getNumberOfClicks() > 1 && e.position.x >= 110 && onAdd) onAdd(trackAt(e.position.y), beatAt(e.position.x));
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!selected || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 2) return;
        dragging = true; const double delta = (e.position.x - dragStart.x) / pixelsPerBeat;
        if (resize) preview.lengthBeats = std::max(editQuantum(state), quantizeBeat(std::max(0., original.lengthBeats + delta), editQuantum(state)));
        else { preview.startBeat = quantizeBeat(std::max(0., original.startBeat + delta), editQuantum(state)); preview.track = std::clamp(original.track + static_cast<int>(std::round((e.position.y - dragStart.y) / rowHeight)), 0, 7); }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (dragging && onChange) onChange(selected, preview, copy); dragging = false; repaint(); }
private:
    juce::Rectangle<float> bounds(const ArrangementClip& c) const { return { static_cast<float>(110 + c.startBeat * pixelsPerBeat), static_cast<float>(24 + c.track * rowHeight), static_cast<float>(std::max(6., c.lengthBeats * pixelsPerBeat)), static_cast<float>(rowHeight - 5) }; }
    int trackAt(float y) const { return std::clamp(static_cast<int>((y - 22) / rowHeight), 0, 7); }
    double beatAt(float x) const { return quantizeBeat(std::max(0., (x - 110.) / pixelsPerBeat), editQuantum(state)); }
    void drawClip(juce::Graphics& g, const ArrangementClip& c, bool isSelected) {
        auto r = bounds(c); g.setColour(trackColour(c.track)); g.fillRoundedRectangle(r, 2);
        g.setColour(isSelected ? HW::ink() : HW::lcdInk().withAlpha(.45f)); g.drawRoundedRectangle(r, 2, isSelected ? 2.f : .7f);
        juce::String name;
        if (c.kind == ClipKind::Pattern) { if (auto* p = findPattern(state, c.contentId)) name = "P" + juce::String(p->id).paddedLeft('0', 2) + " " + p->name; }
        else if (auto* a = findAudioAsset(state, c.contentId)) name = a->name;
        if (c.kind == ClipKind::Audio && peaks) if (const auto* wave = peaks(c.contentId); wave && !wave->empty()) {
            g.setColour(HW::ink().withAlpha(.3f));
            for (int x = 1; x < static_cast<int>(r.getWidth()) - 1; ++x) {
                const auto i = std::min(wave->size() - 1, static_cast<std::size_t>(x / r.getWidth() * wave->size()));
                const auto h = (*wave)[i] * r.getHeight() * .43f; g.drawVerticalLine(static_cast<int>(r.getX()) + x, r.getCentreY() - h, r.getCentreY() + h);
            }
        }
        legend(g, name, r.reduced(6, 1), 10); g.setColour(HW::ink().withAlpha(.5f)); g.drawLine(r.getRight() - 4, r.getY() + 5, r.getRight() - 4, r.getBottom() - 5);
    }
    WorkstationState& state;
    static constexpr int rowHeight = 34, pixelsPerBeat = 24;
    bool dragging = false, resize = false, copy = false;
    juce::Point<float> dragStart;
    ArrangementClip original, preview;
};

class PatternCanvas final : public juce::Component {
public:
    explicit PatternCanvas(WorkstationState& s) : state(s) { setWantsKeyboardFocus(false); }
    WorkstationId selected = 0;
    double playhead = -1;
    std::function<void(PatternNote)> onAdd;
    std::function<void(WorkstationId, PatternNote, bool)> onChange;
    std::function<void(WorkstationId)> onSelect, onDelete;
    std::function<void(int, bool)> onAudition;
    void updateSize(int width) { setSize(std::max(width, 86 + static_cast<int>(state.patterns[static_cast<std::size_t>(state.selectedPattern)].lengthBeats * pixelsPerBeat)), 22 + 32 * rowHeight); repaint(); }
    void paint(juce::Graphics& g) override {
        g.fillAll(HW::lcd()); const auto& p = state.patterns[static_cast<std::size_t>(state.selectedPattern)];
        const double q = editQuantum(state);
        for (int pad = 0; pad < 32; ++pad) {
            const int y = 22 + pad * rowHeight;
            if (pad % 2) { g.setColour(HW::lcdInk().withAlpha(.035f)); g.fillRect(0, y, getWidth(), rowHeight); }
            g.setColour(HW::lcdInk().withAlpha(.14f)); g.drawHorizontalLine(y + rowHeight - 1, 0, static_cast<float>(getWidth()));
            legend(g, "PAD " + juce::String(pad + 1).paddedLeft('0', 2), { 6, static_cast<float>(y), 74, static_cast<float>(rowHeight) }, 10);
        }
        for (double beat = 0; beat <= p.lengthBeats + .0001; beat += std::max(q, .25)) {
            const auto x = static_cast<int>(86 + beat * pixelsPerBeat); const bool bar = std::abs(std::fmod(beat, 4)) < .001;
            g.setColour(HW::lcdInk().withAlpha(bar ? .3f : .1f)); g.drawVerticalLine(x, 0, static_cast<float>(getHeight()));
            if (bar) legend(g, juce::String(static_cast<int>(beat / 4) + 1), { static_cast<float>(x + 2), 0, 40, 20 }, 10);
        }
        for (const auto& n : p.notes) {
            const auto& note = dragging && n.id == selected ? preview : n;
            auto r = bounds(note); g.setColour(n.id == selected ? HW::amber() : HW::lcdInk().withAlpha(.76f)); g.fillRoundedRectangle(r, 2);
            g.setColour(HW::lcd().brighter(.15f)); g.fillRect(r.getX() + 2, r.getBottom() - 3, std::max(1.f, (r.getWidth() - 4) * note.velocity), 2.f);
            if (r.getWidth() > 30) legend(g, juce::String(note.pad + 1), r.reduced(3, 0), 9);
        }
        if (playhead >= 0) { const auto x = static_cast<float>(86 + std::fmod(playhead, p.lengthBeats) * pixelsPerBeat); g.setColour(HW::red()); g.drawLine(x, 0, x, static_cast<float>(getHeight()), 1.5f); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragging = false; resize = false; copy = e.mods.isAltDown(); dragStart = e.position;
        if (e.position.y < 22) return;
        const int pad = std::clamp(static_cast<int>((e.position.y - 22) / rowHeight), 0, 31);
        if (e.position.x < 86) { audition = pad; if (onAudition) onAudition(pad, true); return; }
        const auto& notes = state.patterns[static_cast<std::size_t>(state.selectedPattern)].notes;
        for (auto it = notes.rbegin(); it != notes.rend(); ++it) if (bounds(*it).contains(e.position)) {
            selected = it->id; original = preview = *it; resize = e.position.x > bounds(*it).getRight() - 6;
            if (onSelect) onSelect(selected);
            if ((e.mods.isShiftDown() || e.mods.isPopupMenu()) && onDelete) onDelete(selected);
            repaint(); return;
        }
        selected = 0; if (onSelect) onSelect(0);
        const auto length = state.patterns[static_cast<std::size_t>(state.selectedPattern)].lengthBeats;
        const double beat = quantizeBeat(std::max(0., (e.position.x - 86.) / pixelsPerBeat), editQuantum(state));
        if (beat < length && onAdd && !e.mods.isShiftDown() && !e.mods.isPopupMenu()) {
            onAdd({ 0, pad, beat, std::min(.25, length), .85f, 0 });
            for (const auto& n : state.patterns[static_cast<std::size_t>(state.selectedPattern)].notes)
                if (n.id == selected) { original = preview = n; resize = true; break; }
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!selected || audition >= 0 || e.getDistanceFromDragStart() < 2) return;
        const auto& notes = state.patterns[static_cast<std::size_t>(state.selectedPattern)].notes;
        if (std::none_of(notes.begin(), notes.end(), [this](const PatternNote& n) { return n.id == selected; })) return;
        dragging = true; const auto length = state.patterns[static_cast<std::size_t>(state.selectedPattern)].lengthBeats;
        const double delta = (e.position.x - dragStart.x) / pixelsPerBeat;
        if (resize) preview.duration = std::clamp(quantizeBeat(std::max(.0625, original.duration + delta), editQuantum(state)), .0625, length);
        else { preview.beat = std::clamp(quantizeBeat(std::max(0., original.beat + delta), editQuantum(state)), 0., std::max(0., length - .0625)); preview.pad = std::clamp(original.pad + static_cast<int>(std::round((e.position.y - dragStart.y) / rowHeight)), 0, 31); }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (audition >= 0 && onAudition) onAudition(audition, false); audition = -1;
        if (dragging && onChange) onChange(selected, preview, copy); dragging = false; repaint();
    }
private:
    juce::Rectangle<float> bounds(const PatternNote& n) const { return { static_cast<float>(86 + n.beat * pixelsPerBeat), static_cast<float>(24 + n.pad * rowHeight), static_cast<float>(std::max(5., n.duration * pixelsPerBeat)), static_cast<float>(rowHeight - 4) }; }
    WorkstationState& state;
    static constexpr int rowHeight = 20, pixelsPerBeat = 48;
    bool dragging = false, resize = false, copy = false;
    int audition = -1;
    juce::Point<float> dragStart;
    PatternNote original, preview;
};

class LaunchCell final : public juce::Component, public juce::SettableTooltipClient {
public:
    juce::String label;
    bool active = false, pending = false, empty = true;
    int track = 0, scene = 0;
    std::function<void(const juce::ModifierKeys&)> onPress;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(2);
        g.setColour(active ? HW::lcd().brighter(.15f) : empty ? HW::panel().darker(.03f) : HW::lcd().darker(.07f)); g.fillRoundedRectangle(r, 3);
        g.setColour(active ? HW::lcdInk() : HW::trim()); g.drawRoundedRectangle(r, 3, active ? 2.f : 1.f);
        g.setColour(pending ? HW::amber() : active ? HW::lcdInk() : HW::trim().withAlpha(.4f)); g.fillEllipse(r.getX() + 7, r.getY() + 7, 5, 5);
        legend(g, empty ? "+ ASSIGN" : label, r.reduced(9, 4).withTrimmedTop(5), 10, juce::Justification::centred);
        if (pending) { g.setColour(HW::amber()); g.fillRect(r.getX() + 2, r.getBottom() - 4, r.getWidth() - 4, 2.f); }
    }
    void mouseDown(const juce::MouseEvent& e) override { if (isEnabled() && onPress) onPress(e.mods); }
};

WorkstationComponent::WorkstationComponent(AudioEngine& e) : engine(e) {
    setLookAndFeel(&hardware); setWantsKeyboardFocus(true);
    tooltips = std::make_unique<juce::TooltipWindow>(this, 650);
    auto add = [this](juce::Component& c) { addAndMakeVisible(c); };
    auto label = [&](juce::Label& l, const juce::String& text) { add(l); l.setText(text, juce::dontSendNotification); l.setFont(HW::labelFont(10)); l.setJustificationType(juce::Justification::centred); };
    auto button = [&](juce::TextButton& b, std::function<void()> action) { add(b); b.onClick = std::move(action); b.setWantsKeyboardFocus(false); };
    auto slider = [&](juce::Slider& s, double lo, double hi, double step, const juce::String& suffix, bool rotary = true) {
        add(s); s.setRange(lo, hi, step); s.setSliderStyle(rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 19); s.setTextValueSuffix(suffix);
        s.onDragStart = [this] { if (!refreshing) { engine.pluginHost().captureState(state); gestureBefore = state; } };
        s.onDragEnd = [this] { if (gestureBefore) { undoStack.push_back(std::move(*gestureBefore)); if (undoStack.size() > 100) undoStack.erase(undoStack.begin()); gestureBefore.reset(); redoStack.clear(); refresh(); } };
    };
    auto combo = [&](juce::ComboBox& c) { add(c); c.setWantsKeyboardFocus(false); };
    button(playButton, [this] { if (snapshot.playing) stopTransport(); else { engine.setWorkstationEnabled(true); engine.playWorkstation(modeForPage()); } });
    button(stopButton, [this] { stopTransport(); engine.seekWorkstation(0); });
    button(noteRecord, [this] { const bool arm = !snapshot.recording; engine.setWorkstationEnabled(true); if (arm && !snapshot.playing) engine.playWorkstation(WorkstationPlayMode::Pattern); engine.setPatternRecording(arm); status = arm ? "Recording played pad hits into the selected pattern; release notes to commit." : "Pattern recording stopped."; });
    button(masterRecord, [this] { toggleMasterRecording(); }); masterRecord.setTooltip("Record the live stereo mix, including hosted plug-ins, to a WAV file.");
    button(undoButton, [this] { undo(); }); button(redoButton, [this] { redo(); });
    button(loopButton, [this] { edit([](auto& s) { s.arrangementLoop = !s.arrangementLoop; }); });
    label(clockLabel, "001 : 01 : 000"); clockLabel.setFont(HW::labelFont(20)); clockLabel.setColour(juce::Label::backgroundColourId, HW::lcd()); clockLabel.setColour(juce::Label::textColourId, HW::lcdInk());
    label(tempoLabel, "TEMPO"); slider(tempo, 20, 999, .1, " BPM", false); tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 86, 26);
    tempo.onValueChange = [this] { if (!refreshing) edit([this](auto& s) { s.tempo = tempo.getValue(); }, !gestureBefore); };
    label(statusLabel, status); statusLabel.setJustificationType(juce::Justification::centredLeft);
    label(patternLabel, "PATTERN"); label(trackLabel, "TRACK");
    combo(trackChoice); for (int t = 0; t < 8; ++t) trackChoice.addItem("TRACK " + juce::String(t + 1), t + 1);
    trackChoice.onChange = [this] { if (!refreshing) selectTrack(trackChoice.getSelectedItemIndex()); };
    combo(patternLength); for (int bars : { 1, 2, 4, 8, 16 }) patternLength.addItem(juce::String(bars) + (bars == 1 ? " BAR" : " BARS"), bars);
    patternLength.onChange = [this] { if (!refreshing) edit([this](auto& s) { resizePattern(s, s.selectedPattern, patternLength.getSelectedId() * 4.); }); };
    combo(recordQuantize); for (auto entry : { std::pair{1, "FREE"}, {2, "1/4"}, {3, "1/8"}, {4, "1/16"}, {5, "1/32"} }) recordQuantize.addItem(entry.second, entry.first);
    recordQuantize.setTooltip("Note recording and editing quantization. FREE keeps recorded event timing.");
    recordQuantize.onChange = [this] { if (!refreshing) edit([this](auto& s) { constexpr double values[] { 0, 1, .5, .25, .125 }; s.recordQuantizeBeats = values[std::clamp(recordQuantize.getSelectedItemIndex(), 0, 4)]; }); };
    combo(launchQuantize); for (auto entry : { std::pair{1, "LAUNCH NOW"}, {2, "NEXT BEAT"}, {3, "NEXT BAR"}, {4, "NEXT 2 BARS"} }) launchQuantize.addItem(entry.second, entry.first);
    launchQuantize.onChange = [this] { if (!refreshing) edit([this](auto& s) { constexpr double values[] { 0, 1, 4, 8 }; s.launchQuantizeBeats = values[std::clamp(launchQuantize.getSelectedItemIndex(), 0, 3)]; }); };
    slider(loopStart, 0, 1048560, .25, " beat", false); slider(loopEnd, .25, 1048576, .25, " beat", false);
    loopStart.setTooltip("Arrangement loop start, in beats from zero."); loopEnd.setTooltip("Arrangement loop end, in beats from zero.");
    loopStart.onValueChange = [this] { if (!refreshing) edit([this](auto& s) { s.loopStartBeat = std::min(loopStart.getValue(), s.loopEndBeat - .25); }, !gestureBefore); };
    loopEnd.onValueChange = [this] { if (!refreshing) edit([this](auto& s) { s.loopEndBeat = std::max(loopEnd.getValue(), s.loopStartBeat + .25); }, !gestureBefore); };
    combo(contentChoice); contentChoice.onChange = [this] { const int i = contentChoice.getSelectedItemIndex(); if (!refreshing && i >= 0 && i < static_cast<int>(contentChoices.size())) { selectedContentKind = contentChoices[static_cast<std::size_t>(i)].first; selectedContentId = contentChoices[static_cast<std::size_t>(i)].second; } };
    button(addClipButton, [this] { addClipAt(state.selectedTrack, quantizeBeat(snapshot.beat, editQuantum(state))); });
    button(importButton, [this] { importAudioClip(); }); button(bounceButton, [this] { bounceSelectedPattern(); }); button(assetButton, [this] { chooseAssetAction(); });
    assetButton.setTooltip("Locate, rename or remove the selected audio asset. Audio clips follow tempo through rate playback, which also changes pitch.");
    button(deleteClipButton, [this] { removeSelectedClip(); }); button(deleteNoteButton, [this] { removeSelectedNote(); });
    button(clearPatternButton, [this] { edit([](auto& s) { s.patterns[static_cast<std::size_t>(s.selectedPattern)].notes.clear(); }); selectedNoteId = 0; refreshEditor(); });
    button(clipLoop, [this] { if (auto* c = selectedClip()) { auto copy = *c; copy.loop = !copy.loop; if (!copy.loop) { double length = 0; if (auto* p = findPattern(state, copy.contentId); copy.kind == ClipKind::Pattern && p) length = p->lengthBeats; else if (auto* a = findAudioAsset(state, copy.contentId)) length = a->lengthBeats; copy.lengthBeats = std::min(copy.lengthBeats, length - copy.offsetBeats); } changeClip(copy.id, copy, false); } });
    slider(noteDuration, .0625, 64, .0625, " beat"); slider(noteVelocity, 1, 100, 1, "%"); slider(notePitch, -48, 48, 1, " st");
    slider(clipLength, .0625, 4096, .0625, " beat"); slider(clipGain, -60, 12, .1, " dB");
    label(durationLabel, "LENGTH"); label(velocityLabel, "VELOCITY"); label(pitchLabel, "TUNE"); label(clipLengthLabel, "CLIP LENGTH"); label(clipGainLabel, "CLIP GAIN");
    noteDuration.onValueChange = [this] { if (!refreshing) if (auto* n = selectedNote()) { auto copy = *n; copy.duration = std::min(noteDuration.getValue(), state.patterns[static_cast<std::size_t>(state.selectedPattern)].lengthBeats); edit([this, copy](auto& s) { updatePatternNote(s, s.selectedPattern, copy.id, copy); }, !gestureBefore); } };
    noteVelocity.onValueChange = [this] { if (!refreshing) if (auto* n = selectedNote()) { auto copy = *n; copy.velocity = static_cast<float>(noteVelocity.getValue() / 100); edit([this, copy](auto& s) { updatePatternNote(s, s.selectedPattern, copy.id, copy); }, !gestureBefore); } };
    notePitch.onValueChange = [this] { if (!refreshing) if (auto* n = selectedNote()) { auto copy = *n; copy.semitones = static_cast<float>(notePitch.getValue()); edit([this, copy](auto& s) { updatePatternNote(s, s.selectedPattern, copy.id, copy); }, !gestureBefore); } };
    clipLength.onValueChange = [this] { if (!refreshing) if (auto* c = selectedClip()) { auto copy = *c; copy.lengthBeats = clipLength.getValue(); edit([copy](auto& s) { updateArrangementClip(s, copy.id, copy); }, !gestureBefore); } };
    clipGain.onValueChange = [this] { if (!refreshing) if (auto* c = selectedClip()) { auto copy = *c; copy.gain = juce::Decibels::decibelsToGain(static_cast<float>(clipGain.getValue())); edit([copy](auto& s) { updateArrangementClip(s, copy.id, copy); }, !gestureBefore); } };

    arrangement = std::make_unique<ArrangementCanvas>(state); pattern = std::make_unique<PatternCanvas>(state);
    add(arrangementViewport); add(patternViewport); arrangementViewport.setViewedComponent(arrangement.get(), false); patternViewport.setViewedComponent(pattern.get(), false);
    arrangementViewport.setScrollBarsShown(true, true); patternViewport.setScrollBarsShown(true, true);
    arrangement->onAdd = [this](int t, double b) { addClipAt(t, b); }; arrangement->onSelect = [this](auto id) { selectClip(id); };
    arrangement->onDelete = [this](auto id) { selectClip(id); removeSelectedClip(); }; arrangement->onChange = [this](auto id, auto c, bool copy) { changeClip(id, c, copy); };
    arrangement->peaks = [this](auto id) -> const std::vector<float>* { auto it = assetPeaks.find(id); return it == assetPeaks.end() ? nullptr : &it->second; };
    pattern->onAdd = [this](auto n) { addNote(n); }; pattern->onSelect = [this](auto id) { selectNote(id); }; pattern->onDelete = [this](auto id) { selectNote(id); removeSelectedNote(); };
    pattern->onChange = [this](auto id, auto n, bool copy) { changeNote(id, n, copy); };
    pattern->onAudition = [this](int pad, bool down) { if (down) engine.triggerPad(pad); else engine.releasePad(pad, 0); };
    for (int p = 0; p < 16; ++p) { auto& b = patternButtons[static_cast<std::size_t>(p)]; b.setButtonText("P" + juce::String(p + 1).paddedLeft('0', 2)); button(b, [this, p] { selectPattern(p); }); }
    for (int t = 0; t < 8; ++t) {
        auto i = static_cast<std::size_t>(t);
        trackButtons[i].setButtonText("TRACK " + juce::String(t + 1)); button(trackButtons[i], [this, t] { selectTrack(t); });
        muteButtons[i].setButtonText("M"); button(muteButtons[i], [this, t] { edit([t](auto& s) { auto& v = s.tracks[static_cast<std::size_t>(t)].mute; v = !v; }); });
        soloButtons[i].setButtonText("S"); button(soloButtons[i], [this, t] { edit([t](auto& s) { auto& v = s.tracks[static_cast<std::size_t>(t)].solo; v = !v; }); });
        stopTrackButtons[i].setButtonText("STOP"); button(stopTrackButtons[i], [this, t] { engine.stopClipTrack(t); });
        slider(trackGain[i], -60, 12, .1, " dB", false); trackGain[i].setSliderStyle(juce::Slider::LinearVertical); trackGain[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider(trackPan[i], -1, 1, .01, ""); slider(sendA[i], 0, 100, .1, "%"); slider(sendB[i], 0, 100, .1, "%");
        for (auto* s : { &trackPan[i], &sendA[i], &sendB[i] }) s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        trackPan[i].setTooltip("Pan: left / centre / right"); sendA[i].setTooltip("Send A level; choose SEND A on FX page to edit its built-in rack."); sendB[i].setTooltip("Send B level; choose SEND B on FX page to edit its built-in rack.");
        trackGain[i].onValueChange = [this, t] { if (!refreshing) edit([this, t](auto& s) { s.tracks[static_cast<std::size_t>(t)].gainDb = static_cast<float>(trackGain[static_cast<std::size_t>(t)].getValue()); }, !gestureBefore); };
        trackPan[i].onValueChange = [this, t] { if (!refreshing) edit([this, t](auto& s) { s.tracks[static_cast<std::size_t>(t)].pan = static_cast<float>(trackPan[static_cast<std::size_t>(t)].getValue()); }, !gestureBefore); };
        sendA[i].onValueChange = [this, t] { if (!refreshing) edit([this, t](auto& s) { s.tracks[static_cast<std::size_t>(t)].sendA = static_cast<float>(sendA[static_cast<std::size_t>(t)].getValue() / 100); }, !gestureBefore); };
        sendB[i].onValueChange = [this, t] { if (!refreshing) edit([this, t](auto& s) { s.tracks[static_cast<std::size_t>(t)].sendB = static_cast<float>(sendB[static_cast<std::size_t>(t)].getValue() / 100); }, !gestureBefore); };
        combo(crossAssignments[i]); crossAssignments[i].addItem("THRU", 1); crossAssignments[i].addItem("A", 2); crossAssignments[i].addItem("B", 3);
        crossAssignments[i].setTooltip("Crossfader assignment. THRU bypasses the crossfader.");
        crossAssignments[i].onChange = [this, t] { if (!refreshing) edit([this, t](auto& s) { s.tracks[static_cast<std::size_t>(t)].crossfadeAssignment = static_cast<CrossfadeAssignment>(crossAssignments[static_cast<std::size_t>(t)].getSelectedItemIndex()); }); };
        label(gainLabels[i], "0.0 dB");
        for (int scene = 0; scene < 4; ++scene) {
            auto& cell = launchCells[i * 4 + static_cast<std::size_t>(scene)]; cell = std::make_unique<LaunchCell>(); cell->track = t; cell->scene = scene; add(*cell);
            cell->onPress = [this, t, scene](const auto& mods) {
                const auto& slot = state.launchSlots[static_cast<std::size_t>(t * 4 + scene)];
                if (mods.isShiftDown()) setLaunchContent(t, scene, {});
                else if (mods.isPopupMenu() || slot.kind == ClipKind::Empty) assignLaunchSlot(t, scene);
                else engine.launchClip(t, scene);
            };
            cell->setTooltip("Click: launch on the selected quantum. Right-click: assign. Shift-click: clear.");
        }
    }
    for (int scene = 0; scene < 4; ++scene) { sceneButtons[static_cast<std::size_t>(scene)].setButtonText("SCENE " + juce::String(scene + 1)); button(sceneButtons[static_cast<std::size_t>(scene)], [this, scene] { launchScene(scene); }); }
    slider(crossfader, 0, 1, .001, "", false); crossfader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    crossfader.onValueChange = [this] { if (!refreshing) edit([this](auto& s) { s.crossfader = static_cast<float>(crossfader.getValue()); }, !gestureBefore); };
    slider(masterGain, -60, 12, .1, " dB", false); masterGain.onValueChange = [this] { if (!refreshing) edit([this](auto& s) { s.masterGainDb = static_cast<float>(masterGain.getValue()); }, !gestureBefore); };

    combo(fxBusChoice); for (int t = 0; t < 8; ++t) fxBusChoice.addItem("TRACK " + juce::String(t + 1), t + 1); fxBusChoice.addItem("MASTER", 9); fxBusChoice.addItem("SEND A", 10); fxBusChoice.addItem("SEND B", 11);
    fxBusChoice.onChange = [this] { if (!refreshing) { fxBus = std::max(0, fxBusChoice.getSelectedItemIndex()); refreshFX(); } };
    combo(fxTypeChoice); for (const auto& definition : fxDefinitions()) fxTypeChoice.addItem(definition.name, static_cast<int>(definition.type) + 1);
    fxTypeChoice.onChange = [this] { if (!refreshing) { const auto type = static_cast<FxType>(fxTypeChoice.getSelectedId() - 1); edit([this, type](auto&) { effectRack()[static_cast<std::size_t>(fxSlot)] = defaultFxSlot(type); }); } };
    combo(fxPresetChoice); int presetId = 1; for (const auto& preset : fxPresets()) fxPresetChoice.addItem(juce::String(fxPresetCategoryName(preset.category)) + " / " + preset.name, presetId++);
    fxPresetChoice.setTextWhenNothingSelected("LOAD CHAIN PRESET..."); fxPresetChoice.onChange = [this] { if (!refreshing && fxPresetChoice.getSelectedId() > 0) applyPreset(fxPresetChoice.getSelectedId() - 1); };
    for (int slot = 0; slot < 4; ++slot) { button(fxSlotButtons[static_cast<std::size_t>(slot)], [this, slot] { fxSlot = slot; refreshFX(); }); }
    for (int p = 0; p < 6; ++p) {
        auto i = static_cast<std::size_t>(p); slider(fxParameters[i], 0, 1, .0001, ""); label(fxParameterLabels[i], "");
        fxParameters[i].textFromValueFunction = [this, p](double value) { const auto type = effectRack()[static_cast<std::size_t>(fxSlot)].type; const auto& d = fxDefinition(type); if (p >= d.parameterCount) return juce::String(); const auto actual = fxParameterValue(type, static_cast<std::size_t>(p), static_cast<float>(value)); return juce::String(actual, std::abs(actual) >= 100 ? 0 : std::abs(actual) >= 10 ? 1 : 2) + " " + d.parameters[static_cast<std::size_t>(p)].unit; };
        fxParameters[i].valueFromTextFunction = [this, p](const juce::String& text) { return static_cast<double>(fxParameterNormalized(effectRack()[static_cast<std::size_t>(fxSlot)].type, static_cast<std::size_t>(p), text.getFloatValue())); };
        fxParameters[i].onValueChange = [this, p] { if (!refreshing) edit([this, p](auto&) { effectRack()[static_cast<std::size_t>(fxSlot)].parameters[static_cast<std::size_t>(p)] = static_cast<float>(fxParameters[static_cast<std::size_t>(p)].getValue()); }, !gestureBefore); };
    }
    slider(fxMix, 0, 100, .1, "%"); fxMix.onValueChange = [this] { if (!refreshing) edit([this](auto&) { effectRack()[static_cast<std::size_t>(fxSlot)].mix = static_cast<float>(fxMix.getValue() / 100); }, !gestureBefore); };
    label(fxName, "OFF"); fxName.setFont(HW::labelFont(25)); label(fxDescription, "BUILT-IN EFFECT PROCESSOR"); label(fxMixLabel, "DRY / WET");
    button(fxBypass, [this] { edit([this](auto&) { auto& b = effectRack()[static_cast<std::size_t>(fxSlot)].bypass; b = !b; }); });
    button(pluginManager, [this] { openPluginManager(); }); button(pluginEditor, [this] { openPluginEditor(); });
    button(pluginBypass, [this] { const int bus = pluginBus(); if (bus >= 0) engine.pluginHost().setBypass(bus, fxSlot, !engine.pluginHost().getSlotState(bus, fxSlot).bypass); });
    label(pluginStatus, ""); pluginStatus.setJustificationType(juce::Justification::centredLeft);
    label(bounceNotice, "BOUNCE PATTERN prints built-in track, send and master FX. REC MIX also captures hosted plug-ins.");
    engine.pluginHost().onChange = [safe = juce::Component::SafePointer<WorkstationComponent>(this)] { if (safe) safe->syncPluginState(); };
    engine.setWorkstationState(state);
    refresh(); startTimerHz(30); setSize(1200, 790);
}

WorkstationComponent::~WorkstationComponent() {
    stopTimer(); engine.pluginHost().onChange = {}; if (pluginDialog) delete pluginDialog.getComponent();
    engine.setPatternRecording(false); engine.stopMasterCapture(); engine.stopWorkstation();
    if (worker.joinable()) worker.join();
    arrangementViewport.setViewedComponent(nullptr, false); patternViewport.setViewedComponent(nullptr, false); setLookAndFeel(nullptr);
}

juce::String WorkstationComponent::beatText(double beat) {
    const auto bar = static_cast<int>(std::floor(beat / 4)) + 1; const auto within = beat - std::floor(beat / 4) * 4;
    return juce::String(bar).paddedLeft('0', 3) + " : " + juce::String(static_cast<int>(within) + 1).paddedLeft('0', 2) + " : " + juce::String(static_cast<int>((within - std::floor(within)) * 960)).paddedLeft('0', 3);
}
WorkstationPlayMode WorkstationComponent::modeForPage() const {
    return page == Page::Patterns ? WorkstationPlayMode::Pattern : page == Page::Perform ? WorkstationPlayMode::Performance : page == Page::FX ? snapshot.mode : WorkstationPlayMode::Arrangement;
}
void WorkstationComponent::publish(bool update) {
    reconcileAssets(); engine.setWorkstationState(state); if (onStateChanged) onStateChanged(); if (update) refresh();
}
void WorkstationComponent::edit(const std::function<void(WorkstationState&)>& action, bool history) {
    if (refreshing || busy) return;
    if (history) engine.pluginHost().captureState(state);
    auto before = state;
    try { action(state); validateWorkstation(state); }
    catch (const std::exception& e) { state = std::move(before); reportError(e.what()); return; }
    if (history) { undoStack.push_back(std::move(before)); if (undoStack.size() > 100) undoStack.erase(undoStack.begin()); redoStack.clear(); }
    publish();
}
void WorkstationComponent::undo() {
    if (busy || undoStack.empty()) return; engine.pluginHost().captureState(state); redoStack.push_back(state); state = std::move(undoStack.back()); undoStack.pop_back(); selectedNoteId = selectedClipId = 0; reconcileAssets(); engine.setWorkstationState(state, true); if (onStateChanged) onStateChanged(); refresh();
}
void WorkstationComponent::redo() {
    if (busy || redoStack.empty()) return; engine.pluginHost().captureState(state); undoStack.push_back(state); state = std::move(redoStack.back()); redoStack.pop_back(); selectedNoteId = selectedClipId = 0; reconcileAssets(); engine.setWorkstationState(state, true); if (onStateChanged) onStateChanged(); refresh();
}
void WorkstationComponent::showPage(Page value) { page = value; refresh(); resized(); repaint(); }
void WorkstationComponent::stopTransport() { engine.setPatternRecording(false); engine.stopWorkstation(); engine.stop(); }
void WorkstationComponent::selectPattern(int p) {
    if (p < 0 || p >= 16) return; state.selectedPattern = p; selectedNoteId = 0; selectedContentKind = ClipKind::Pattern; selectedContentId = state.patterns[static_cast<std::size_t>(p)].id; publish();
}
void WorkstationComponent::selectTrack(int t) { if (t < 0 || t >= 8) return; state.selectedTrack = t; publish(); }
PatternNote* WorkstationComponent::selectedNote() {
    auto& notes = state.patterns[static_cast<std::size_t>(state.selectedPattern)].notes; auto i = std::find_if(notes.begin(), notes.end(), [this](const auto& n) { return n.id == selectedNoteId; }); return i == notes.end() ? nullptr : &*i;
}
ArrangementClip* WorkstationComponent::selectedClip() {
    auto i = std::find_if(state.arrangement.begin(), state.arrangement.end(), [this](const auto& c) { return c.id == selectedClipId; }); return i == state.arrangement.end() ? nullptr : &*i;
}
void WorkstationComponent::selectNote(WorkstationId id) { selectedNoteId = id; pattern->selected = id; refreshEditor(); pattern->repaint(); }
void WorkstationComponent::addNote(PatternNote n) {
    WorkstationId added = 0;
    edit([&](auto& s) { if (addPatternNote(s, s.selectedPattern, n)) { for (const auto& x : s.patterns[static_cast<std::size_t>(s.selectedPattern)].notes) added = std::max(added, x.id); } });
    selectNote(added);
}
void WorkstationComponent::changeNote(WorkstationId id, PatternNote n, bool copy) {
    if (copy) { n.id = 0; addNote(n); return; } edit([this, id, n](auto& s) { if (!updatePatternNote(s, s.selectedPattern, id, n)) throw std::runtime_error("That note would fall outside the pattern."); }); selectNote(id);
}
void WorkstationComponent::removeSelectedNote() { if (!selectedNoteId) return; const auto id = selectedNoteId; edit([this, id](auto& s) { removePatternNote(s, s.selectedPattern, id); }); selectNote(0); }
void WorkstationComponent::selectClip(WorkstationId id) {
    selectedClipId = id; arrangement->selected = id;
    if (auto* c = selectedClip()) { state.selectedTrack = c->track; if (c->kind == ClipKind::Pattern) for (int p = 0; p < 16; ++p) if (state.patterns[static_cast<std::size_t>(p)].id == c->contentId) { state.selectedPattern = p; selectedNoteId = 0; } engine.setWorkstationState(state); }
    refresh();
}
void WorkstationComponent::addClipAt(int track, double beat) {
    ArrangementClip c; c.track = track; c.startBeat = beat; c.kind = selectedContentKind; c.contentId = selectedContentId;
    if (c.kind == ClipKind::Pattern) { const auto* p = findPattern(state, c.contentId); if (!p) return; c.lengthBeats = p->lengthBeats; }
    else { const auto* a = findAudioAsset(state, c.contentId); if (!a) return; c.lengthBeats = a->lengthBeats; }
    WorkstationId added = 0; edit([&](auto& s) { if (!addArrangementClip(s, c)) throw std::runtime_error("Could not place that clip."); for (const auto& x : s.arrangement) added = std::max(added, x.id); }); selectClip(added);
}
void WorkstationComponent::changeClip(WorkstationId id, ArrangementClip c, bool copy) {
    WorkstationId added = id;
    edit([&](auto& s) { if (copy) { c.id = 0; if (!addArrangementClip(s, c)) throw std::runtime_error("Could not copy that clip."); for (const auto& x : s.arrangement) added = std::max(added, x.id); } else if (!updateArrangementClip(s, id, c)) throw std::runtime_error("Clip length exceeds its non-looped content."); }); selectClip(added);
}
void WorkstationComponent::removeSelectedClip() { if (!selectedClipId) return; const auto id = selectedClipId; edit([id](auto& s) { removeArrangementClip(s, id); }); selectClip(0); }
void WorkstationComponent::setLaunchContent(int track, int scene, LaunchSlot slot) { edit([=](auto& s) { if (!setLaunchSlot(s, track, scene, slot)) throw std::runtime_error("Could not assign launch slot."); }); }
void WorkstationComponent::assignLaunchSlot(int track, int scene) {
    juce::PopupMenu menu; int item = 1; std::vector<LaunchSlot> choices;
    for (const auto& p : state.patterns) { choices.push_back({ ClipKind::Pattern, p.id, true }); menu.addItem(item++, "P" + juce::String(p.id).paddedLeft('0', 2) + "  " + p.name); }
    if (!state.audioAssets.empty()) menu.addSeparator();
    for (const auto& a : state.audioAssets) { choices.push_back({ ClipKind::Audio, a.id, true }); menu.addItem(item++, "AUDIO  " + a.name); }
    menu.addSeparator(); menu.addItem(10000, "Clear slot");
    auto safe = juce::Component::SafePointer<WorkstationComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(launchCells[static_cast<std::size_t>(track * 4 + scene)].get()), [safe, track, scene, choices = std::move(choices)](int result) {
        if (!safe || !result) return; if (result == 10000) safe->setLaunchContent(track, scene, {}); else if (result > 0 && result <= static_cast<int>(choices.size())) safe->setLaunchContent(track, scene, choices[static_cast<std::size_t>(result - 1)]);
    });
}
void WorkstationComponent::launchScene(int scene) { for (int track = 0; track < 8; ++track) if (state.launchSlots[static_cast<std::size_t>(track * 4 + scene)].kind != ClipKind::Empty) engine.launchClip(track, scene); }

std::array<FxSlot, 4>& WorkstationComponent::effectRack() {
    return fxBus < 8 ? state.tracks[static_cast<std::size_t>(fxBus)].effects : fxBus == 8 ? state.masterEffects : fxBus == 9 ? state.sendAEffects : state.sendBEffects;
}
const std::array<FxSlot, 4>& WorkstationComponent::effectRack() const {
    return fxBus < 8 ? state.tracks[static_cast<std::size_t>(fxBus)].effects : fxBus == 8 ? state.masterEffects : fxBus == 9 ? state.sendAEffects : state.sendBEffects;
}
int WorkstationComponent::pluginBus() const { return fxBus <= 8 ? fxBus : -1; }
void WorkstationComponent::reconcileAssets() {
    for (auto it = assetAudio.begin(); it != assetAudio.end();) {
        const auto* asset = findAudioAsset(state, it->first);
        const auto cached = asset ? cachedAudio.find(asset->filePath) : cachedAudio.end();
        if (!asset || cached == cachedAudio.end() || cached->second != it->second) {
            engine.removeAudioAsset(it->first); assetPeaks.erase(it->first); it = assetAudio.erase(it);
        } else ++it;
    }
    for (const auto& asset : state.audioAssets) if (const auto found = cachedAudio.find(asset.filePath); found != cachedAudio.end()) {
        if (const auto active = assetAudio.find(asset.id); active == assetAudio.end() || active->second != found->second) {
            assetAudio[asset.id] = found->second; assetPeaks[asset.id] = cachedPeaks[asset.filePath]; engine.setAudioAsset(asset.id, found->second);
        }
    }
}
void WorkstationComponent::applyPreset(int index) {
    const auto presets = fxPresets(); if (index < 0 || index >= static_cast<int>(presets.size())) return;
    edit([this, &presets, index](auto&) { effectRack() = presets[static_cast<std::size_t>(index)].slots; }); status = "Loaded " + juce::String(presets[static_cast<std::size_t>(index)].name) + " on " + fxBusChoice.getText() + ".";
}
void WorkstationComponent::syncPluginState() {
    for (int bus = 0; bus < 9; ++bus) for (int slot = 0; slot < 4; ++slot) {
        auto& target = bus == 8 ? state.masterPlugins[static_cast<std::size_t>(slot)] : state.tracks[static_cast<std::size_t>(bus)].plugins[static_cast<std::size_t>(slot)];
        target = engine.pluginHost().getSlotState(bus, slot);
    }
    if (onStateChanged) onStateChanged(); refreshFX();
}
WorkstationState WorkstationComponent::captureState() { engine.pluginHost().captureState(state); engine.setWorkstationState(state); return state; }
void WorkstationComponent::setTempo(double value) {
    if (!std::isfinite(value)) return; value = std::clamp(value, 20., 999.); if (std::abs(state.tempo - value) < .00001) return;
    edit([value](auto& s) { s.tempo = value; });
}
void WorkstationComponent::openPluginManager() {
    if (pluginBus() < 0) return;
    if (pluginDialog) delete pluginDialog.getComponent();
    auto view = engine.pluginHost().createPluginManagerView(pluginBus(), fxSlot); view->setLookAndFeel(&hardware);
    juce::DialogWindow::LaunchOptions options; options.dialogTitle = "Samplify - AU / VST3 manager"; options.dialogBackgroundColour = HW::chassis(); options.useNativeTitleBar = true; options.resizable = true;
    options.content.setOwned(view.release()); options.componentToCentreAround = this; pluginDialog = options.launchAsync();
}
void WorkstationComponent::openPluginEditor() { if (pluginBus() >= 0) engine.pluginHost().showEditor(pluginBus(), fxSlot); }
void WorkstationComponent::refreshContentChoices() {
    contentChoice.clear(juce::dontSendNotification); contentChoices.clear(); int choice = 1, selected = 0;
    for (const auto& p : state.patterns) { contentChoices.emplace_back(ClipKind::Pattern, p.id); contentChoice.addItem("P" + juce::String(p.id).paddedLeft('0', 2) + "  " + p.name, choice); if (selectedContentKind == ClipKind::Pattern && selectedContentId == p.id) selected = choice; ++choice; }
    for (const auto& a : state.audioAssets) { contentChoices.emplace_back(ClipKind::Audio, a.id); contentChoice.addItem("AUDIO  " + a.name, choice); if (selectedContentKind == ClipKind::Audio && selectedContentId == a.id) selected = choice; ++choice; }
    if (!selected) { selected = 1; selectedContentKind = ClipKind::Pattern; selectedContentId = state.patterns[0].id; }
    contentChoice.setSelectedId(selected, juce::dontSendNotification); assetButton.setEnabled(!busy && selectedContentKind == ClipKind::Audio);
}
void WorkstationComponent::refreshEditor() {
    const bool before = refreshing; refreshing = true;
    auto* note = selectedNote(); auto* clip = selectedClip();
    for (auto* c : std::initializer_list<juce::Component*> { &noteDuration, &noteVelocity, &notePitch, &deleteNoteButton }) c->setEnabled(note && !busy);
    if (note) { noteDuration.setValue(note->duration, juce::dontSendNotification); noteVelocity.setValue(note->velocity * 100, juce::dontSendNotification); notePitch.setValue(note->semitones, juce::dontSendNotification); }
    for (auto* c : std::initializer_list<juce::Component*> { &clipLength, &clipGain, &clipLoop, &deleteClipButton }) c->setEnabled(clip && !busy);
    if (clip) { clipLength.setValue(clip->lengthBeats, juce::dontSendNotification); clipGain.setValue(juce::Decibels::gainToDecibels(clip->gain, -60.f), juce::dontSendNotification); clipLoop.setToggleState(clip->loop, juce::dontSendNotification); }
    if (pattern) pattern->selected = selectedNoteId; if (arrangement) arrangement->selected = selectedClipId;
    refreshing = before;
}
void WorkstationComponent::refreshFX() {
    const bool before = refreshing; refreshing = true;
    const auto& rack = effectRack(); const auto& slot = rack[static_cast<std::size_t>(fxSlot)]; const auto& definition = fxDefinition(slot.type);
    fxBusChoice.setSelectedId(fxBus + 1, juce::dontSendNotification); fxTypeChoice.setSelectedId(static_cast<int>(slot.type) + 1, juce::dontSendNotification);
    fxName.setText(definition.name, juce::dontSendNotification); fxDescription.setText(fxBusChoice.getText() + "  /  INSERT " + juce::String(fxSlot + 1), juce::dontSendNotification);
    for (int s = 0; s < 4; ++s) { auto& button = fxSlotButtons[static_cast<std::size_t>(s)]; button.setButtonText("0" + juce::String(s + 1) + "   " + fxDefinition(rack[static_cast<std::size_t>(s)].type).name); button.setToggleState(s == fxSlot, juce::dontSendNotification); }
    for (int p = 0; p < 6; ++p) { const auto i = static_cast<std::size_t>(p); const bool used = p < definition.parameterCount; fxParameters[i].setEnabled(used && !busy); fxParameterLabels[i].setText(used ? definition.parameters[i].name : "", juce::dontSendNotification); fxParameters[i].setValue(slot.parameters[i], juce::dontSendNotification); fxParameters[i].updateText(); }
    fxMix.setValue(slot.mix * 100, juce::dontSendNotification); fxMix.setEnabled(slot.type != FxType::None && !busy); fxBypass.setToggleState(slot.bypass, juce::dontSendNotification); fxBypass.setEnabled(slot.type != FxType::None && !busy);
    const int bus = pluginBus(); const auto hosted = bus >= 0 ? engine.pluginHost().getSlotState(bus, fxSlot) : HostedPluginSlot{};
    pluginManager.setEnabled(bus >= 0 && !busy); pluginEditor.setEnabled(bus >= 0 && !hosted.empty() && !engine.pluginHost().isLoading(bus, fxSlot)); pluginBypass.setEnabled(bus >= 0 && !hosted.empty()); pluginBypass.setToggleState(hosted.bypass, juce::dontSendNotification);
    pluginStatus.setText(bus < 0 ? "Send returns use the four built-in processors above." : "HOSTED SLOT " + juce::String(fxSlot + 1) + "  /  " + (hosted.empty() ? juce::String("Empty - open Plug-in Manager to scan and load AU / VST3.") : juce::String(hosted.name) + "  " + engine.pluginHost().status(bus, fxSlot)), juce::dontSendNotification);
    refreshing = before;
}
void WorkstationComponent::refresh() {
    refreshing = true;
    tempo.setValue(state.tempo, juce::dontSendNotification); loopStart.setValue(state.loopStartBeat, juce::dontSendNotification); loopEnd.setValue(state.loopEndBeat, juce::dontSendNotification); loopButton.setToggleState(state.arrangementLoop, juce::dontSendNotification);
    trackChoice.setSelectedId(state.selectedTrack + 1, juce::dontSendNotification); patternLength.setSelectedId(static_cast<int>(state.patterns[static_cast<std::size_t>(state.selectedPattern)].lengthBeats / 4), juce::dontSendNotification);
    const auto quantizeId = state.recordQuantizeBeats == 0 ? 1 : state.recordQuantizeBeats >= 1 ? 2 : state.recordQuantizeBeats >= .5 ? 3 : state.recordQuantizeBeats >= .25 ? 4 : 5;
    recordQuantize.setSelectedId(quantizeId, juce::dontSendNotification); launchQuantize.setSelectedId(state.launchQuantizeBeats == 0 ? 1 : state.launchQuantizeBeats == 1 ? 2 : state.launchQuantizeBeats == 4 ? 3 : 4, juce::dontSendNotification);
    for (int p = 0; p < 16; ++p) { auto& b = patternButtons[static_cast<std::size_t>(p)]; b.setToggleState(p == state.selectedPattern, juce::dontSendNotification); b.setTooltip(state.patterns[static_cast<std::size_t>(p)].name + " / " + std::to_string(state.patterns[static_cast<std::size_t>(p)].notes.size()) + " notes"); }
    for (int t = 0; t < 8; ++t) {
        const auto i = static_cast<std::size_t>(t); const auto& track = state.tracks[i];
        trackButtons[i].setButtonText(track.name); trackButtons[i].setToggleState(t == state.selectedTrack, juce::dontSendNotification);
        muteButtons[i].setToggleState(track.mute, juce::dontSendNotification); soloButtons[i].setToggleState(track.solo, juce::dontSendNotification);
        trackGain[i].setValue(track.gainDb, juce::dontSendNotification); trackPan[i].setValue(track.pan, juce::dontSendNotification); sendA[i].setValue(track.sendA * 100, juce::dontSendNotification); sendB[i].setValue(track.sendB * 100, juce::dontSendNotification);
        gainLabels[i].setText(juce::String(track.gainDb, 1) + " dB", juce::dontSendNotification); crossAssignments[i].setSelectedId(static_cast<int>(track.crossfadeAssignment) + 1, juce::dontSendNotification);
        for (int scene = 0; scene < 4; ++scene) { const auto& slot = state.launchSlots[i * 4 + static_cast<std::size_t>(scene)]; auto& cell = *launchCells[i * 4 + static_cast<std::size_t>(scene)]; cell.empty = slot.kind == ClipKind::Empty; cell.label.clear(); if (slot.kind == ClipKind::Pattern) { if (const auto* p = findPattern(state, slot.contentId)) cell.label = "P" + juce::String(p->id).paddedLeft('0', 2) + "  " + p->name; } else if (const auto* a = findAudioAsset(state, slot.contentId)) cell.label = a->name; cell.repaint(); }
    }
    crossfader.setValue(state.crossfader, juce::dontSendNotification); masterGain.setValue(state.masterGainDb, juce::dontSendNotification);
    undoButton.setEnabled(!busy && !undoStack.empty()); redoButton.setEnabled(!busy && !redoStack.empty());
    importButton.setEnabled(!busy); bounceButton.setEnabled(!busy && !state.patterns[static_cast<std::size_t>(state.selectedPattern)].notes.empty()); arrangement->setEnabled(!busy); pattern->setEnabled(!busy);
    refreshContentChoices(); refreshEditor(); refreshFX(); refreshing = false;
    arrangement->updateSize(arrangementViewport.getMaximumVisibleWidth()); pattern->updateSize(patternViewport.getMaximumVisibleWidth());
    arrangement->repaint(); pattern->repaint(); repaint();
}

void WorkstationComponent::runTask(const juce::String& message, std::function<std::function<void()>()> job) {
    if (busy) return; if (worker.joinable()) worker.join(); busy = true; status = message; refresh();
    auto safe = juce::Component::SafePointer<WorkstationComponent>(this);
    worker = std::thread([safe, job = std::move(job)] {
        std::function<void()> completion; juce::String error;
        try { completion = job(); } catch (const std::exception& e) { error = e.what(); } catch (...) { error = "Audio operation failed."; }
        juce::MessageManager::callAsync([safe, completion = std::move(completion), error] { if (!safe) return; safe->busy = false; if (error.isNotEmpty()) safe->reportError(error); else if (completion) completion(); if (safe->pendingRestore) { auto incoming = std::move(*safe->pendingRestore); safe->pendingRestore.reset(); safe->restoreState(incoming); } safe->refresh(); });
    });
}
void WorkstationComponent::reportError(const juce::String& error) { status = error; statusLabel.setText(status, juce::dontSendNotification); repaint(); }
namespace {
std::vector<float> makePeaks(const AudioData& audio) {
    std::vector<float> peaks(512, 0); if (!audio.frames()) return peaks;
    for (std::size_t i = 0; i < audio.frames(); ++i) { float peak = 0; for (const auto& channel : audio.channels) peak = std::max(peak, std::abs(channel[i])); const auto bin = std::min<std::size_t>(511, i * 512 / audio.frames()); peaks[bin] = std::max(peaks[bin], std::min(1.f, peak)); }
    return peaks;
}
}
void WorkstationComponent::restoreState(const WorkstationState& incoming) {
    validateWorkstation(incoming); if (busy) { pendingRestore = incoming; return; } stopTransport(); for (const auto& [id, data] : assetAudio) { juce::ignoreUnused(data); engine.removeAudioAsset(id); } cachedAudio.clear(); cachedPeaks.clear(); state = incoming; selectedNoteId = selectedClipId = 0; undoStack.clear(); redoStack.clear(); assetAudio.clear(); assetPeaks.clear();
    engine.setWorkstationState(state, true); refresh();
    if (state.audioAssets.empty()) return;
    const auto assets = state.audioAssets;
    runTask("Restoring audio clip files...", [this, assets] {
        std::map<WorkstationId, std::shared_ptr<const AudioData>> audio; std::map<WorkstationId, std::vector<float>> peaks; juce::StringArray missing;
        for (const auto& asset : assets) { try { auto data = std::make_shared<AudioData>(AudioEngine::readAudio(juce::File(asset.filePath))); peaks[asset.id] = makePeaks(*data); audio[asset.id] = std::move(data); } catch (...) { missing.add(asset.name); } }
        return [this, audio = std::move(audio), peaks = std::move(peaks), missing] { assetAudio = audio; assetPeaks = peaks; for (const auto& asset : state.audioAssets) if (const auto it = assetAudio.find(asset.id); it != assetAudio.end()) { cachedAudio[asset.filePath] = it->second; cachedPeaks[asset.filePath] = assetPeaks[asset.id]; engine.setAudioAsset(asset.id, it->second); } status = missing.isEmpty() ? "Workstation restored." : "Missing audio: " + missing.joinIntoString(", ") + ". Select its audio asset, then AUDIO FILE > Locate / replace."; };
    });
}
void WorkstationComponent::importAudioClip() {
    if (busy) return; chooser = std::make_unique<juce::FileChooser>("Import audio clip", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe = juce::Component::SafePointer<WorkstationComponent>(this)](const auto& c) { if (safe && c.getResult().existsAsFile()) safe->importFile(c.getResult()); });
}
void WorkstationComponent::importFile(const juce::File& file, std::optional<WorkstationId> replace) {
    runTask("Reading audio clip...", [this, file, replace] { auto audio = std::make_shared<AudioData>(AudioEngine::readAudio(file)); auto peaks = makePeaks(*audio);
        return [this, file, replace, audio, peaks = std::move(peaks)] {
            WorkstationId id = 0; edit([&](auto& s) {
                if (replace) { for (auto& a : s.audioAssets) if (a.id == *replace) { id = a.id; a.filePath = file.getFullPathName().toStdString(); a.name = file.getFileNameWithoutExtension().toStdString(); a.lengthBeats = audio->duration() * a.sourceBpm / 60; } }
                else { AudioAsset a; a.name = file.getFileNameWithoutExtension().toStdString(); a.filePath = file.getFullPathName().toStdString(); a.sourceBpm = state.tempo; a.lengthBeats = audio->duration() * a.sourceBpm / 60; if (!addAudioAsset(s, a)) throw std::runtime_error("Could not register audio clip."); for (const auto& x : s.audioAssets) id = std::max(id, x.id); }
            });
            if (id && findAudioAsset(state, id) && findAudioAsset(state, id)->filePath == file.getFullPathName().toStdString()) { cachedAudio[file.getFullPathName().toStdString()] = audio; cachedPeaks[file.getFullPathName().toStdString()] = peaks; assetAudio[id] = audio; assetPeaks[id] = peaks; engine.setAudioAsset(id, audio); selectedContentKind = ClipKind::Audio; selectedContentId = id; status = "Audio imported. Place it in the timeline or assign it to a launch slot. Audio tempo changes use rate playback."; refresh(); }
        };
    });
}
void WorkstationComponent::importRenderedAudio(const juce::File& file, std::shared_ptr<const AudioData> audio, const juce::String& name, std::vector<float> peaks) {
    WorkstationId id = 0;
    edit([&](auto& s) { AudioAsset asset; asset.name = name.toStdString(); asset.filePath = file.getFullPathName().toStdString(); asset.sourceBpm = s.tempo; asset.lengthBeats = audio->duration() * s.tempo / 60; if (!addAudioAsset(s, asset)) throw std::runtime_error("Could not register rendered clip."); for (const auto& a : s.audioAssets) id = std::max(id, a.id); });
    if (id && findAudioAsset(state, id)) { cachedAudio[file.getFullPathName().toStdString()] = audio; cachedPeaks[file.getFullPathName().toStdString()] = peaks; assetAudio[id] = std::move(audio); assetPeaks[id] = std::move(peaks); engine.setAudioAsset(id, assetAudio[id]); selectedContentKind = ClipKind::Audio; selectedContentId = id; refresh(); }
}
void WorkstationComponent::bounceSelectedPattern() {
    if (busy || state.patterns[static_cast<std::size_t>(state.selectedPattern)].notes.empty()) return;
    const int p = state.selectedPattern, t = state.selectedTrack; const auto name = juce::String(state.patterns[static_cast<std::size_t>(p)].name) + " - bounce";
    chooser = std::make_unique<juce::FileChooser>("Bounce pattern with built-in FX (hosted plug-ins excluded)", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name + ".wav"), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe = juce::Component::SafePointer<WorkstationComponent>(this), p, t, name](const auto& c) { if (!safe || c.getResult() == juce::File()) return; const auto file = c.getResult().withFileExtension("wav"); safe->runTask("Bouncing pattern with built-in FX...", [safe, p, t, file, name] { auto audio = std::make_shared<AudioData>(safe->engine.bouncePattern(p, t)); auto peaks = makePeaks(*audio); AudioEngine::writeFloatAudio(file, *audio); return [safe, file, audio, name, peaks = std::move(peaks)] { if (!safe) return; safe->importRenderedAudio(file, audio, name, peaks); safe->status = "Pattern bounced and added to audio clips. Hosted plug-ins are printed through REC MIX."; }; }); });
}
void WorkstationComponent::chooseAssetAction() {
    if (selectedContentKind != ClipKind::Audio) return; const auto* asset = findAudioAsset(state, selectedContentId); if (!asset) return;
    const auto id = asset->id; juce::PopupMenu menu; menu.addItem(1, "Locate / replace audio file..."); menu.addItem(2, "Reveal file"); menu.addItem(3, "Set source BPM..."); menu.addSeparator(); menu.addItem(4, "Remove asset and its clips");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&assetButton), [safe = juce::Component::SafePointer<WorkstationComponent>(this), id](int choice) {
        if (!safe) return; const auto* a = findAudioAsset(safe->state, id); if (!a) return;
        if (choice == 1) { safe->chooser = std::make_unique<juce::FileChooser>("Locate audio clip", juce::File(a->filePath), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"); safe->chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, id](const auto& c) { if (safe && c.getResult().existsAsFile()) safe->importFile(c.getResult(), id); }); }
        else if (choice == 2) juce::File(a->filePath).revealToUser();
        else if (choice == 3) {
            auto* alert = new juce::AlertWindow("Audio source BPM", "Audio clips use rate playback. Set the original file tempo in BPM.", juce::MessageBoxIconType::NoIcon);
            alert->addTextEditor("tempo", juce::String(a->sourceBpm, 2)); alert->addButton("SET", 1); alert->addButton("CANCEL", 0);
            juce::Component::SafePointer<juce::AlertWindow> window(alert);
            alert->enterModalState(true, juce::ModalCallbackFunction::create([safe, window, id](int result) { if (safe && window && result == 1) { const auto value = window->getTextEditorContents("tempo").getDoubleValue(); safe->setAssetSourceTempo(id, value); } }), true);
        } else if (choice == 4) { safe->edit([id](auto& s) { removeAudioAsset(s, id); });  }
    });
}
void WorkstationComponent::setAssetSourceTempo(WorkstationId id, double bpm) {
    if (!std::isfinite(bpm) || bpm < 20 || bpm > 999) { reportError("Source BPM must be between 20 and 999."); return; }
    const auto audio = assetAudio.find(id);
    if (audio == assetAudio.end()) { reportError("Locate the missing audio file before setting its source BPM."); return; }
    const auto beats = audio->second->duration() * bpm / 60;
    edit([=](auto& s) {
        for (auto& asset : s.audioAssets) if (asset.id == id) {
            const auto scale = beats / asset.lengthBeats;
            asset.sourceBpm = bpm; asset.lengthBeats = beats;
            for (auto& clip : s.arrangement) if (clip.kind == ClipKind::Audio && clip.contentId == id) {
                clip.offsetBeats *= scale;
                if (!clip.loop) clip.lengthBeats = std::min(clip.lengthBeats, beats - clip.offsetBeats);
            }
        }
    });
    status = "Source BPM set to " + juce::String(bpm, 2) + ". Audio clips follow studio tempo through rate playback.";
}
void WorkstationComponent::toggleMasterRecording() {
    capture = engine.captureSnapshot();
    if (capture.recording) { engine.stopMasterCapture(); capturePendingImport = true; status = "Finalizing recorded mix..."; return; }
    if (capture.finalizing || busy) return;
    chooser = std::make_unique<juce::FileChooser>("Record live stereo mix to WAV", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Samplify live mix.wav"), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe = juce::Component::SafePointer<WorkstationComponent>(this)](const auto& c) { if (!safe || c.getResult() == juce::File()) return; try { safe->captureFile = c.getResult().withFileExtension("wav"); safe->engine.startMasterCapture(safe->captureFile); safe->capturePendingImport = false; safe->status = "Recording the live stereo mix. Click REC MIX again to finish and import the recording."; }
        catch (const std::exception& error) { safe->reportError(error.what()); } });
}

void WorkstationComponent::resized() {
    if (!arrangement || !pattern) return;
    for (auto* c : getChildren()) c->setVisible(false);
    auto place = [](juce::Component& c, int x, int y, int w, int h) { c.setBounds(x, y, std::max(0, w), std::max(0, h)); c.setVisible(true); };
    const int width = getWidth(), height = getHeight(), left = 20, right = width - 20, top = 100, bottom = height - 34;
    int x = left;
    for (auto* b : { &playButton, &stopButton }) { place(*b, x, 25, 62, 36); x += 68; }
    place(noteRecord, x, 25, 87, 36); x += 93; place(masterRecord, x, 25, 82, 36); x += 94;
    place(clockLabel, x, 21, 177, 44); x += 190; place(tempoLabel, x, 12, 146, 14); place(tempo, x, 29, 146, 34);
    place(undoButton, right - 140, 25, 66, 34); place(redoButton, right - 69, 25, 66, 34);
    place(statusLabel, left + 3, height - 26, width - 46, 20);
    auto patternRow = [&](int y, bool twoRows) {
        const int gap = 5, columns = twoRows ? 8 : 16, bw = (right - left - (columns - 1) * gap) / columns;
        for (int p = 0; p < 16; ++p) place(patternButtons[static_cast<std::size_t>(p)], left + (p % columns) * (bw + gap), y + (p / columns) * 34, bw, 29);
    };
    auto noteInspector = [&](int y, int start, int available) {
        const int kw = std::clamp((available - 140) / 3, 66, 100);
        place(durationLabel, start, y, kw, 15); place(noteDuration, start, y + 13, kw, 65);
        place(velocityLabel, start + kw, y, kw, 15); place(noteVelocity, start + kw, y + 13, kw, 65);
        place(pitchLabel, start + kw * 2, y, kw, 15); place(notePitch, start + kw * 2, y + 13, kw, 65);
        place(deleteNoteButton, start + kw * 3 + 8, y + 17, 112, 28); place(clearPatternButton, start + kw * 3 + 8, y + 49, 112, 25);
    };
    if (page == Page::Arrange) {
        place(contentChoice, left, top, std::max(190, width - 890), 30); x = left + std::max(190, width - 890) + 8;
        for (auto* b : { &addClipButton, &importButton, &bounceButton, &assetButton }) { const int bw = b == &bounceButton ? 132 : b == &addClipButton ? 116 : 112; place(*b, x, top, bw, 30); x += bw + 6; }
        place(loopButton, right - 206, top, 64, 30); place(loopStart, right - 138, top, 64, 30); place(loopEnd, right - 69, top, 69, 30);
        loopStart.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 64, 18); loopEnd.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 64, 18);
        const int timelineHeight = std::clamp((bottom - top - 190) / 2, 180, 294);
        place(arrangementViewport, left + 4, top + 43, width - 48, timelineHeight);
        const int row = top + 52 + timelineHeight; patternRow(row, false);
        place(patternViewport, left + 4, row + 37, width - 48, std::max(70, bottom - row - 126));
        const int inspector = bottom - 82; noteInspector(inspector, left + 8, std::min(460, (width - 40) / 2));
        const int start = width / 2 + 12; place(clipLengthLabel, start, inspector, 86, 15); place(clipLength, start, inspector + 13, 86, 65); place(clipGainLabel, start + 92, inspector, 86, 15); place(clipGain, start + 92, inspector + 13, 86, 65);
        place(clipLoop, start + 184, inspector + 17, 96, 28); place(deleteClipButton, start + 184, inspector + 49, 96, 25);
        place(trackChoice, right - 138, inspector + 17, 138, 27); place(recordQuantize, right - 138, inspector + 49, 138, 25);
    } else if (page == Page::Patterns) {
        patternRow(top, true); const int toolsY = top + 74;
        place(trackLabel, left, toolsY, 48, 28); place(trackChoice, left + 50, toolsY, 138, 28);
        place(patternLength, left + 198, toolsY, 120, 28); place(recordQuantize, left + 328, toolsY, 126, 28); place(bounceButton, right - 142, toolsY, 142, 28);
        place(patternViewport, left + 4, toolsY + 38, width - 48, std::max(80, bottom - toolsY - 128));
        noteInspector(bottom - 82, left + 20, 500); place(bounceNotice, left + 545, bottom - 78, width - 585, 68);
    } else if (page == Page::FX) {
        place(fxBusChoice, left + 4, top, 195, 32); place(fxPresetChoice, left + 212, top, std::min(500, width - 450), 32);
        const int rackTop = top + 56, rackWidth = 212, panelX = left + 240, panelWidth = right - panelX;
        for (int s = 0; s < 4; ++s) place(fxSlotButtons[static_cast<std::size_t>(s)], left + 8, rackTop + s * 70, rackWidth - 8, 57);
        place(fxBypass, left + 8, rackTop + 290, rackWidth - 8, 31);
        place(fxDescription, panelX + 10, rackTop, panelWidth - 20, 23); place(fxName, panelX + 10, rackTop + 22, panelWidth - 20, 48);
        place(fxTypeChoice, panelX + 24, rackTop + 91, panelWidth - 174, 31); place(fxMixLabel, right - 129, rackTop + 82, 104, 16); place(fxMix, right - 129, rackTop + 99, 104, 106);
        const int columns = panelWidth >= 850 ? 6 : 3, knobWidth = (panelWidth - 153) / columns, rowHeight = 116;
        for (int p = 0; p < 6; ++p) { const int px = panelX + 15 + (p % columns) * knobWidth, py = rackTop + 143 + (p / columns) * rowHeight; place(fxParameterLabels[static_cast<std::size_t>(p)], px, py, knobWidth, 20); place(fxParameters[static_cast<std::size_t>(p)], px + 3, py + 19, knobWidth - 6, 95); }
        const int hostY = std::max(rackTop + 398, bottom - 132); place(pluginStatus, left + 14, hostY, width - 68, 42);
        place(pluginManager, left + 15, hostY + 49, 190, 34); place(pluginEditor, left + 215, hostY + 49, 145, 34); place(pluginBypass, left + 370, hostY + 49, 150, 34);
        place(bounceNotice, left + 15, bottom - 37, width - 70, 28);
    } else {
        place(launchQuantize, left + 4, top, 150, 29); place(importButton, left + 164, top, 128, 29); place(contentChoice, left + 303, top, 260, 29);
        const int sceneWidth = 91, gridWidth = right - left - sceneWidth - 8, stripWidth = gridWidth / 8;
        const int rowHeight = std::clamp((height - 480) / 4, 32, 47), trackY = top + 42, gridY = trackY + 28;
        for (int t = 0; t < 8; ++t) {
            const int tx = left + t * stripWidth; place(trackButtons[static_cast<std::size_t>(t)], tx + 2, trackY, stripWidth - 4, 25);
            for (int scene = 0; scene < 4; ++scene) place(*launchCells[static_cast<std::size_t>(t * 4 + scene)], tx, gridY + scene * rowHeight, stripWidth, rowHeight);
        }
        for (int s = 0; s < 4; ++s) place(sceneButtons[static_cast<std::size_t>(s)], left + gridWidth + 8, gridY + s * rowHeight + 3, sceneWidth - 6, rowHeight - 6);
        const int mixerY = gridY + rowHeight * 4 + 16, masterY = bottom - 58, stripHeight = masterY - mixerY - 13;
        for (int t = 0; t < 8; ++t) {
            const auto i = static_cast<std::size_t>(t); const int tx = left + t * stripWidth;
            place(muteButtons[i], tx + 5, mixerY, (stripWidth - 16) / 2, 26); place(soloButtons[i], tx + 10 + (stripWidth - 16) / 2, mixerY, (stripWidth - 16) / 2, 26);
            const int faderHeight = std::max(50, stripHeight - 94); place(trackGain[i], tx + 8, mixerY + 35, std::max(25, stripWidth / 3), faderHeight);
            const int knobX = tx + stripWidth / 2, knobW = std::max(30, stripWidth / 2 - 7);
            place(trackPan[i], knobX, mixerY + 34, knobW, 38); place(sendA[i], knobX, mixerY + 82, knobW, 38); place(sendB[i], knobX, mixerY + 130, knobW, 38);
            place(gainLabels[i], tx + 3, mixerY + stripHeight - 51, stripWidth - 6, 18); place(crossAssignments[i], tx + 5, mixerY + stripHeight - 28, stripWidth - 10, 25);
            place(stopTrackButtons[i], left + gridWidth + 8, mixerY + t * std::max(20, stripHeight / 8), sceneWidth - 6, std::max(18, stripHeight / 8 - 2)); stopTrackButtons[i].setButtonText("STOP " + juce::String(t + 1));
        }
        place(crossfader, left + 39, masterY + 13, std::min(360, width / 3), 31); place(masterGain, width / 2 + 12, masterY + 3, 195, 47);
    }
    arrangement->updateSize(arrangementViewport.getMaximumVisibleWidth()); pattern->updateSize(patternViewport.getMaximumVisibleWidth());
}

void WorkstationComponent::paint(juce::Graphics& g) {
    g.fillAll(HW::chassis()); const auto bounds = getLocalBounds().toFloat();
    g.setColour(HW::trim().withAlpha(.25f)); g.drawHorizontalLine(81, 16, static_cast<float>(getWidth() - 16));
    for (auto point : { juce::Point<float>(9, 10), { static_cast<float>(getWidth() - 9), 10 }, { 9, static_cast<float>(getHeight() - 11) }, { static_cast<float>(getWidth() - 9), static_cast<float>(getHeight() - 11) } }) HW::paintScrew(g, point, 3.2f);
    legend(g, "TRANSPORT / SEQUENCER", { 22, 4, 290, 15 }, 9);
    const juce::String heading = page == Page::Arrange ? "SONG ARRANGER" : page == Page::Patterns ? "PATTERN / EVENT EDITOR" : page == Page::FX ? "EFFECTS / INSERT RACK" : "LIVE PERFORMANCE / MIXER";
    legend(g, heading, { 22, 83, 380, 15 }, 10);
    if (page == Page::Arrange || page == Page::Patterns) {
        if (arrangementViewport.isVisible()) HW::paintBezel(g, arrangementViewport.getBounds().toFloat().expanded(4), true);
        if (patternViewport.isVisible()) HW::paintBezel(g, patternViewport.getBounds().toFloat().expanded(4), true);
        const auto hint = page == Page::Arrange ? "Double-click: place  |  Drag: move / edge trim  |  Alt-drag: copy" : "Click: draw note  |  Drag: move / edge length  |  Shift-click: delete  |  Pad label: audition";
        legend(g, hint, { static_cast<float>(getWidth() / 2), 84, static_cast<float>(getWidth() / 2 - 24), 14 }, 9, juce::Justification::centredRight);
    } else if (page == Page::FX) {
        const int panelX = 260, top = 156; HW::paintLCD(g, { static_cast<float>(panelX), static_cast<float>(top - 3), static_cast<float>(getWidth() - panelX - 20), 80 });
        HW::paintBezel(g, { 23, static_cast<float>(top - 2), 214, 326 });
        legend(g, "FOUR BUILT-IN INSERTS", { 28, static_cast<float>(top - 21), 209, 17 }, 9);
        const int hostY = pluginStatus.getY(); g.setColour(HW::trim().withAlpha(.4f)); g.drawHorizontalLine(hostY - 9, 28, static_cast<float>(getWidth() - 28));
    } else {
        const int gridWidth = getWidth() - 40 - 99, stripWidth = gridWidth / 8;
        for (int t = 0; t < 8; ++t) {
            const auto i = static_cast<std::size_t>(t); const int tx = 20 + t * stripWidth; auto area = juce::Rectangle<float>(static_cast<float>(tx + 1), static_cast<float>(muteButtons[i].getY() - 5), static_cast<float>(stripWidth - 2), static_cast<float>(crossAssignments[i].getBottom() - muteButtons[i].getY() + 10));
            HW::paintBezel(g, area); const int knobX = trackPan[i].getX(), knobWidth = trackPan[i].getWidth();
            legend(g, "PAN", { static_cast<float>(knobX), static_cast<float>(trackPan[i].getY() - 9), static_cast<float>(knobWidth), 12 }, 8, juce::Justification::centred);
            legend(g, "SEND A", { static_cast<float>(knobX), static_cast<float>(sendA[i].getY() - 9), static_cast<float>(knobWidth), 12 }, 8, juce::Justification::centred);
            legend(g, "SEND B", { static_cast<float>(knobX), static_cast<float>(sendB[i].getY() - 9), static_cast<float>(knobWidth), 12 }, 8, juce::Justification::centred);
            const auto meter = juce::Rectangle<float>(static_cast<float>(trackGain[i].getRight() + 2), static_cast<float>(trackGain[i].getY()), 4, static_cast<float>(trackGain[i].getHeight()));
            g.setColour(HW::ink().withAlpha(.2f)); g.fillRect(meter); g.setColour(snapshot.trackPeaks[i] > 1 ? HW::red() : HW::lcdInk()); const auto level = std::clamp((juce::Decibels::gainToDecibels(snapshot.trackPeaks[i], -60.f) + 60) / 60, 0.f, 1.f); g.fillRect(meter.withTrimmedTop(meter.getHeight() * (1 - level)));
        }
        legend(g, "A", { static_cast<float>(crossfader.getX() - 19), static_cast<float>(crossfader.getY()), 18, 30 }, 13); legend(g, "B", { static_cast<float>(crossfader.getRight() + 5), static_cast<float>(crossfader.getY()), 18, 30 }, 13);
        legend(g, "CROSSFADER", { static_cast<float>(crossfader.getX()), static_cast<float>(crossfader.getY() - 13), static_cast<float>(crossfader.getWidth()), 14 }, 9, juce::Justification::centred);
        legend(g, "MASTER", { static_cast<float>(masterGain.getX() - 65), static_cast<float>(masterGain.getY()), 64, 40 }, 10);
        const auto meter = juce::Rectangle<float>(static_cast<float>(getWidth() - 248), static_cast<float>(masterGain.getY() + 10), 206, 16); g.setColour(HW::ink()); g.fillRect(meter); const auto level = std::clamp((juce::Decibels::gainToDecibels(engine.peakLevel(), -60.f) + 60) / 60, 0.f, 1.f); g.setColour(engine.peakLevel() > 1 ? HW::red() : HW::lcd()); g.fillRect(meter.reduced(2).withWidth((meter.getWidth() - 4) * level));
        legend(g, capture.recording ? "RECORDING  " + juce::String(static_cast<juce::int64>(capture.frames)) + " frames" : "LIVE STEREO BUS", { meter.getX(), meter.getBottom() + 1, meter.getWidth(), 18 }, 9, juce::Justification::centred);
    }
    if (busy) { g.setColour(HW::amber()); g.fillEllipse(getWidth() - 21.f, getHeight() - 21.f, 7, 7); }
    juce::ignoreUnused(bounds);
}

void WorkstationComponent::timerCallback() {
    snapshot = engine.workstationSnapshot(); capture = engine.captureSnapshot();
    clockLabel.setText(beatText(snapshot.beat), juce::dontSendNotification); playButton.setToggleState(snapshot.playing, juce::dontSendNotification); noteRecord.setToggleState(snapshot.recording, juce::dontSendNotification); masterRecord.setToggleState(capture.recording || capture.finalizing, juce::dontSendNotification);
    bool changed = false; RecordedWorkstationNote event;
    while (engine.popRecordedNote(event)) {
        if (!changed) { engine.pluginHost().captureState(state); undoStack.push_back(state); redoStack.clear(); }
        if (addPatternNote(state, event.patternIndex, event.note)) { changed = true; status = "Recorded pad " + juce::String(event.note.pad + 1) + " at " + beatText(event.note.beat) + "."; }
        else status = "Recorded note could not fit the selected pattern.";
    }
    if (changed) publish();
    if (snapshot.eventOverflow) status = "Event queue overflow: reduce simultaneous input or recording density.";
    if (capture.overflow) status = "Recording stopped: disk writer could not keep up; this take was not saved.";
    if (capturePendingImport && !capture.recording && !capture.finalizing && !busy) {
        capturePendingImport = false;
        if (capture.error == CaptureError::None && capture.frames > 0 && captureFile.existsAsFile()) importFile(captureFile);
        else reportError(capture.error == CaptureError::EmptyRecording ? "No audio frames were recorded." : "Mix recording could not be completed. Check the destination and available disk space.");
    }
    arrangement->playhead = snapshot.beat; pattern->playhead = snapshot.playing ? snapshot.beat : -1; if (page == Page::Arrange) arrangement->repaint(); if (page == Page::Arrange || page == Page::Patterns) pattern->repaint();
    for (int t = 0; t < 8; ++t) for (int scene = 0; scene < 4; ++scene) { auto& cell = *launchCells[static_cast<std::size_t>(t * 4 + scene)]; const bool active = snapshot.activeScene[static_cast<std::size_t>(t)] == scene, pending = snapshot.pendingScene[static_cast<std::size_t>(t)] == scene; if (cell.active != active || cell.pending != pending) { cell.active = active; cell.pending = pending; cell.repaint(); } }
    statusLabel.setText(status, juce::dontSendNotification); if (page == Page::Perform) repaint();
}

bool WorkstationComponent::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == juce::KeyPress::spaceKey) { if (snapshot.playing) stopTransport(); else engine.playWorkstation(modeForPage()); return true; }
    if (key.getModifiers().isCommandDown() && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z')) { if (key.getModifiers().isShiftDown()) redo(); else undo(); return true; }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) { if (selectedNoteId) removeSelectedNote(); else removeSelectedClip(); return true; }
    return false;
}

juce::var WorkstationComponent::runWorkflowSmoke() {
    if (busy) throw std::runtime_error("Workstation is busy before workflow smoke.");
    auto* result = new juce::DynamicObject();
    auto check = [result](const char* name, bool ok) { result->setProperty(name, ok); if (!ok) throw std::runtime_error(std::string("Workstation UI workflow failed: ") + name); };
    stopTransport(); engine.stopMasterCapture(); state = WorkstationState(); undoStack.clear(); redoStack.clear(); selectedNoteId = selectedClipId = 0; publish();
    selectPattern(0); addNote({ 0, 0, 0, .25, .9f, 0 });
    check("draw_pattern_note", state.patterns[0].notes.size() == 1 && selectedNote() && selectedNote()->pad == 0);
    const auto firstNote = *selectedNote();
    const juce::Point<float> beyondEnd(86 + static_cast<float>((state.patterns[0].lengthBeats + 1) * 48), 28);
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto modifiers = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    const juce::MouseEvent emptyDown(source, beyondEnd, modifiers, 1, 0, 0, 0, 0, pattern.get(), pattern.get(), now, beyondEnd, now, 1, false);
    const juce::MouseEvent emptyDrag(source, beyondEnd.translated(-96, 20), modifiers, 1, 0, 0, 0, 0, pattern.get(), pattern.get(), now, beyondEnd, now, 1, true);
    pattern->mouseDown(emptyDown); pattern->mouseDrag(emptyDrag); pattern->mouseUp(emptyDrag);
    check("empty_beyond_end_drag_keeps_existing_note", selectedNoteId == 0 && state.patterns[0].notes.size() == 1 && state.patterns[0].notes[0].id == firstNote.id && state.patterns[0].notes[0].beat == firstNote.beat && state.patterns[0].notes[0].pad == firstNote.pad);
    selectNote(firstNote.id);
    auto note = *selectedNote(); note.beat = .5; note.pad = 2; changeNote(note.id, note, false);
    check("move_note_in_time_and_pad", selectedNote() && selectedNote()->beat == .5 && selectedNote()->pad == 2);
    note = *selectedNote(); note.duration = .75; changeNote(note.id, note, false);
    check("resize_note", selectedNote() && selectedNote()->duration == .75);
    note.beat = 1.25; changeNote(note.id, note, true);
    check("copy_note_has_distinct_id", state.patterns[0].notes.size() == 2 && selectedNoteId != note.id);
    noteVelocity.setValue(67, juce::sendNotificationSync); notePitch.setValue(7, juce::sendNotificationSync);
    check("note_inspector_velocity_and_pitch", selectedNote() && std::abs(selectedNote()->velocity - .67f) < .001f && selectedNote()->semitones == 7);
    removeSelectedNote(); check("delete_note", state.patterns[0].notes.size() == 1);
    undo(); check("undo_note_delete", state.patterns[0].notes.size() == 2); redo(); check("redo_note_delete", state.patterns[0].notes.size() == 1);
    patternLength.setSelectedId(2, juce::sendNotificationSync); check("pattern_length_bars", state.patterns[0].lengthBeats == 8);
    recordQuantize.setSelectedId(3, juce::sendNotificationSync); check("record_quantization_control", state.recordQuantizeBeats == .5);
    addClipAt(1, 4); check("place_pattern_clip", state.arrangement.size() == 1 && selectedClip() && selectedClip()->track == 1);
    auto clip = *selectedClip(); clip.startBeat = 8; clip.track = 2; clip.lengthBeats = 12; changeClip(clip.id, clip, false);
    check("move_and_resize_arrangement_clip", selectedClip() && selectedClip()->startBeat == 8 && selectedClip()->lengthBeats == 12 && selectedClip()->track == 2);
    clip.startBeat = 24; changeClip(clip.id, clip, true); check("copy_arrangement_clip", state.arrangement.size() == 2 && selectedClipId != clip.id);
    removeSelectedClip(); check("delete_arrangement_clip", state.arrangement.size() == 1); undo();
    setLaunchContent(3, 2, { ClipKind::Pattern, state.patterns[0].id, true }); check("assign_launch_slot_track_major", state.launchSlots[14].contentId == state.patterns[0].id);
    setLaunchContent(3, 2, {}); check("clear_launch_slot", state.launchSlots[14].kind == ClipKind::Empty);
    trackGain[2].setValue(-6, juce::sendNotificationSync); trackPan[2].setValue(-.4, juce::sendNotificationSync); sendA[2].setValue(25, juce::sendNotificationSync);
    check("mixer_gain_pan_send_controls", state.tracks[2].gainDb == -6 && std::abs(state.tracks[2].pan + .4f) < .001f && state.tracks[2].sendA == .25f);
    muteButtons[2].onClick(); soloButtons[1].onClick(); check("mixer_mute_solo_controls", state.tracks[2].mute && state.tracks[1].solo); muteButtons[2].onClick(); soloButtons[1].onClick();
    crossAssignments[0].setSelectedId(2, juce::sendNotificationSync); crossAssignments[1].setSelectedId(3, juce::sendNotificationSync); crossfader.setValue(.7, juce::sendNotificationSync);
    check("crossfader_assignments_and_position", state.tracks[0].crossfadeAssignment == CrossfadeAssignment::A && state.tracks[1].crossfadeAssignment == CrossfadeAssignment::B && std::abs(state.crossfader - .7f) < .001f);
    fxBus = 0; fxSlot = 0; refreshFX(); fxTypeChoice.setSelectedId(static_cast<int>(FxType::Delay) + 1, juce::sendNotificationSync);
    check("fx_type_loads_registry_defaults", effectRack()[0].type == FxType::Delay && effectRack()[0].mix == defaultFxSlot(FxType::Delay).mix);
    fxParameters[0].setValue(.37, juce::sendNotificationSync); fxBypass.onClick(); check("fx_parameter_and_bypass", std::abs(effectRack()[0].parameters[0] - .37f) < .001f && effectRack()[0].bypass);
    applyPreset(0); check("fx_chain_preset", effectRack()[0].type == fxPresets()[0].slots[0].type && effectRack()[3].mix == fxPresets()[0].slots[3].mix);
    setTempo(112); check("shared_host_tempo", state.tempo == 112);
    // A useful screenshot fixture is only installed by this explicit smoke command.
    edit([](auto& s) {
        for (int p = 0; p < 4; ++p) { auto& pattern = s.patterns[static_cast<std::size_t>(p)]; pattern.name = p == 0 ? "DUSTY BREAK" : p == 1 ? "BASS MOTION" : p == 2 ? "CHORD MEMORY" : "TOPLINE"; pattern.lengthBeats = 16; pattern.notes.clear(); for (int step = 0; step < 32; ++step) if (step % (p + 2) == 0 || step % 8 == 3) addPatternNote(s, p, { 0, (step % 4) + p * 4, step * .5, p == 2 ? 1.5 : .25, step % 8 == 0 ? .95f : .68f, p == 1 ? -12.f : 0.f }); }
        s.arrangement.clear(); for (int track = 0; track < 8; ++track) { s.tracks[static_cast<std::size_t>(track)].name = std::array<std::string, 8>{ "DRUMS", "BASS", "CHORDS", "MELODY", "PERC", "TEXTURE", "VOCAL", "RETURN" }[static_cast<std::size_t>(track)]; for (int section = 0; section < 3; ++section) if (track < 4 || section != 0) addArrangementClip(s, { 0, track, ClipKind::Pattern, s.patterns[static_cast<std::size_t>(track % 4)].id, section * 16., 16, 0, true, 1 }); for (int scene = 0; scene < 4; ++scene) setLaunchSlot(s, track, scene, { ClipKind::Pattern, s.patterns[static_cast<std::size_t>((track + scene) % 4)].id, true }); }
        s.selectedPattern = 0; s.selectedTrack = 0; s.crossfader = .5f; s.recordQuantizeBeats = .25; s.launchQuantizeBeats = 4; s.loopEndBeat = 48; s.arrangementLoop = true;
    });
    selectNote(state.patterns[0].notes.front().id); selectClip(state.arrangement.front().id); fxBus = 0; fxSlot = 0; refreshFX();
    launchScene(0); stopTransport(); engine.setPatternRecording(false); engine.seekWorkstation(0);
    if (engine.deviceManager().getCurrentAudioDevice() == nullptr) {
        engine.prepareToPlay(512, 44100); juce::AudioBuffer<float> audio(2, 512);
        for (int i = 0; i < 3; ++i) { audio.clear(); juce::AudioSourceChannelInfo block(&audio, 0, 512); engine.getNextAudioBlock(block); }
        snapshot = engine.workstationSnapshot(); check("offline_callback_stops_workstation", !snapshot.playing && !snapshot.recording);
    }
    check("smoke_fixture_is_valid", (validateWorkstation(state), state.arrangement.size() >= 16 && state.patterns[0].notes.size() >= 8));
    status = "Studio ready. Pattern notes, arranged clips, launch slots and built-in effects are editable."; refresh(); resized(); return juce::var(result);
}
} // namespace samplify
