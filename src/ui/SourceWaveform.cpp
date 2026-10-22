#include "SourceWaveform.h"
#include "HardwareLookAndFeel.h"
#include "core/CueEditing.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace samplify {
namespace {
const auto background = HardwareLookAndFeel::lcd();
const auto subdued = HardwareLookAndFeel::lcdInk().withAlpha(.7f);

juce::Colour cueColourFor(const Pad& cue) {
    auto colour = juce::Colour(cue.colour);
    return colour.getAlpha() > 0 ? colour : juce::Colour(0xffbf382b);
}

juce::Colour spectralColour(const WaveformBin& bin) {
    const auto total = std::max(.00001f, bin.low + bin.mid + bin.high);
    const auto brightness = std::clamp((bin.high + .5f * bin.mid) / total, 0.f, 1.f);
    return HardwareLookAndFeel::lcdInk().interpolatedWith(juce::Colour(0xff46674b), brightness);
}

std::size_t boundedFrame(double value, std::size_t maximum) {
    if (!std::isfinite(value) || value <= 0) return 0;
    return static_cast<std::size_t>(std::min(value, static_cast<double>(maximum)));
}
}

SourceWaveform::SourceWaveform(bool fullFile) : overview(fullFile) {
    setOpaque(true);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip(overview ? "Click the overview to seek through the source"
                        : "Drag to scrub; drag cue flags to trim; Alt-drag to set an end; wheel to zoom; Shift for fine edits");
    if (!overview) {
        for (auto* button : { &plusButton, &minusButton, &freezeButton, &gridButton }) {
            addChildComponent(*button);
            button->setWantsKeyboardFocus(false);
            button->setColour(juce::TextButton::buttonColourId, HardwareLookAndFeel::chassis());
            button->setColour(juce::TextButton::textColourOffId, HardwareLookAndFeel::ink());
            button->setColour(juce::TextButton::buttonOnColourId, HardwareLookAndFeel::amber());
        }
        plusButton.setTooltip("Zoom into the source waveform");
        minusButton.setTooltip("Zoom out of the source waveform");
        freezeButton.setTooltip("Freeze the editing view while playback continues");
        gridButton.setTooltip("Edit beatgrid markers");
        plusButton.onClick = [this] { zoomIn(); };
        minusButton.onClick = [this] { zoomOut(); };
        freezeButton.onClick = [this] {
            setFrozen(!frozen);
            if (onFreezeChanged) onFreezeChanged(frozen);
        };
        gridButton.onClick = [this] {
            setGridEditMode(!gridEdit);
            if (onGridEditChanged) onGridEditChanged(gridEdit);
        };
        startTimerHz(10);
    }
}

void SourceWaveform::setData(std::shared_ptr<const AudioData> audio, const Analysis& details) {
    // A newly rendered stem mix has a new buffer identity but the same source
    // timeline. Preserve the user's zoom/scroll while changing that display.
    // The controller clears the data explicitly when loading a new file.
    const bool sameTimeline = source && audio && source->frames() == audio->frames()
        && std::isfinite(source->sampleRate) && std::isfinite(audio->sampleRate)
        && std::abs(source->sampleRate - audio->sampleRate) < .001;
    const bool changed = audio != source && !sameTimeline;
    source = std::move(audio);
    analysis = details;
    if (changed) {
        viewStart = 0;
        const auto frames = source ? source->frames() : 0;
        const double bpm = std::isfinite(analysis.bpm) && analysis.bpm > 0 ? analysis.bpm : 120;
        viewLength = source ? std::min(static_cast<double>(frames), source->sampleRate * 60.0 / bpm * 8.0) : 0;
        dragCue = -1;
        pointerDown = false;
    }
    clampView();
    rebuildGrid();
    repaint();
}

void SourceWaveform::setProject(const Project& state, std::uint32_t selectedMask) {
    project = state;
    selection = selectedMask;
    rebuildGrid();
    repaint();
}

void SourceWaveform::setPlayback(const PlaybackSnapshot& state) {
    const bool moved = playback.sourceFrame != state.sourceFrame;
    playback = state;
    if (!overview && !frozen && !pointerDown && moved)
        centreOn(state.sourceFrame);
    else
        repaint();
}

void SourceWaveform::setFrozen(bool enabled) {
    frozen = enabled;
    freezeButton.setToggleState(enabled, juce::dontSendNotification);
    repaint();
}

void SourceWaveform::setGridEditMode(bool enabled) {
    gridEdit = enabled;
    gridButton.setToggleState(enabled, juce::dontSendNotification);
    repaint();
}

void SourceWaveform::rebuildGrid() {
    beatFrames.clear();
    snapFrames.clear();
    if (!source || source->frames() == 0 || !project.beatgridEnabled
        || !std::isfinite(source->sampleRate) || source->sampleRate <= 0) return;
    // Render the editable project grid, which is also used by quantization.
    // Falling back to analysis.beats here would resurrect a cleared grid or
    // display stale beats after the user edits the tempo.
    beatFrames = projectBeatFrames(project, source->sampleRate, source->frames());
    if (!overview && project.quantize)
        snapFrames = projectBeatFrames(project, source->sampleRate, source->frames(), .125);
}

juce::Rectangle<float> SourceWaveform::plotBounds() const {
    auto bounds = getLocalBounds().toFloat();
    return overview ? bounds.reduced(2, 4) : bounds.reduced(3, 8);
}

void SourceWaveform::clampView() {
    const auto total = source ? static_cast<double>(source->frames()) : 0;
    if (total <= 0) { viewStart = viewLength = 0; return; }
    if (overview) { viewStart = 0; viewLength = total; return; }
    const auto minimum = std::min(64.0, total);
    if (!std::isfinite(viewLength) || viewLength <= 0) viewLength = total;
    viewLength = std::clamp(viewLength, minimum, total);
    viewStart = std::clamp(std::isfinite(viewStart) ? viewStart : 0.0, 0.0, std::max(0.0, total - viewLength));
}

void SourceWaveform::centreOn(std::size_t frame) {
    if (overview) return;
    viewStart = static_cast<double>(frame) - viewLength * .5;
    clampView();
    repaint();
}

void SourceWaveform::zoomAt(double factor, float x) {
    if (overview || !source || source->frames() == 0 || factor <= 0) return;
    const auto plot = plotBounds();
    const double fraction = std::clamp(static_cast<double>((x - plot.getX()) / std::max(1.f, plot.getWidth())), 0.0, 1.0);
    const auto anchor = viewStart + fraction * viewLength;
    viewLength *= factor;
    clampView();
    viewStart = anchor - fraction * viewLength;
    clampView();
    repaint();
}

void SourceWaveform::zoomIn() { zoomAt(1.0 / 1.5, plotBounds().getCentreX()); }
void SourceWaveform::zoomOut() { zoomAt(1.5, plotBounds().getCentreX()); }

std::size_t SourceWaveform::frameAt(float x) const {
    if (!source || source->frames() == 0) return 0;
    const auto plot = plotBounds();
    const auto fraction = std::clamp(static_cast<double>((x - plot.getX()) / std::max(1.f, plot.getWidth())), 0.0, 1.0);
    return boundedFrame(viewStart + fraction * viewLength, source->frames() - 1);
}

float SourceWaveform::xAt(std::size_t frame) const {
    const auto plot = plotBounds();
    return plot.getX() + static_cast<float>((static_cast<double>(frame) - viewStart) / std::max(1.0, viewLength)) * plot.getWidth();
}

std::size_t SourceWaveform::draggedFrame(const juce::MouseEvent& event) const {
    if (!source || source->frames() == 0) return 0;
    const auto pixels = static_cast<double>(event.position.x - dragMouseX);
    const auto delta = pixels * viewLength / std::max(1.f, plotBounds().getWidth())
        * (event.mods.isShiftDown() ? .1 : 1.0);
    const auto maximum = dragCue >= 0 && dragEnd ? source->frames() : source->frames() - 1;
    return boundedFrame(static_cast<double>(dragOriginal) + delta, maximum);
}

void SourceWaveform::paint(juce::Graphics& g) {
    g.fillAll(background);
    if (!source || source->frames() == 0 || viewLength <= 0) return;
    const auto plot = plotBounds();
    const auto totalFrames = source->frames();
    const auto maximumFrame = totalFrames - 1;

    // Cue regions are displayed only for an explicitly defined end point.
    if (!overview) {
        for (int index = 0; index < 32; ++index) {
            const auto& cue = project.pads[static_cast<std::size_t>(index)];
            if (cue.empty()) continue;
            const auto start = dragCue == index ? dragStart : cue.start;
            const auto end = dragCue == index ? dragEndFrame : cue.end;
            const bool hasEnd = dragCue == index ? (dragEnd || dragHadEnd) : cue.hasEnd;
            if (!hasEnd || end <= start) continue;
            const auto left = std::max(plot.getX(), xAt(start));
            const auto right = std::min(plot.getRight(), xAt(end));
            if (right <= left) continue;
            const bool selected = (selection & (std::uint32_t(1) << index)) != 0;
            g.setColour(cueColourFor(cue).withAlpha(selected ? .25f : .085f));
            g.fillRect(left, plot.getY(), right - left, plot.getHeight());
        }
    }

    // Analysis owns spectral bands. A bounded PCM pass adds real detail at deep
    // zoom; no FFT, allocation, or whole-source sample validation occurs here.
    const int columns = std::clamp(static_cast<int>(std::ceil(plot.getWidth())), 1, 4096);
    const bool pcmDetail = !overview && viewLength <= 200000 && !source->channels.empty();
    for (int column = 0; column < columns; ++column) {
        const auto fraction = static_cast<double>(column) / columns;
        const auto from = boundedFrame(viewStart + fraction * viewLength, maximumFrame);
        const auto to = boundedFrame(viewStart + static_cast<double>(column + 1) / columns * viewLength, maximumFrame);
        WaveformBin bin;
        if (!analysis.waveform.empty()) {
            const auto binIndex = std::min(analysis.waveform.size() - 1,
                static_cast<std::size_t>(static_cast<double>(from) / totalFrames * analysis.waveform.size()));
            bin = analysis.waveform[binIndex];
        }
        float peak = bin.peak;
        if (pcmDetail) {
            peak = 0;
            for (const auto& channel : source->channels) {
                if (channel.empty()) continue;
                const auto last = std::min(to, channel.size() - 1);
                for (auto frame = std::min(from, last); frame <= last; ++frame)
                    peak = std::max(peak, std::abs(channel[frame]));
            }
        }
        if (!std::isfinite(peak)) peak = 0;
        const auto height = std::clamp(peak, 0.f, 1.f) * plot.getHeight() * .49f;
        if (height <= 0) continue;
        g.setColour(spectralColour(bin));
        const auto x = plot.getX() + static_cast<float>(column) / columns * plot.getWidth();
        g.drawLine(x, plot.getCentreY() - height, x, plot.getCentreY() + height,
                   std::max(1.f, plot.getWidth() / columns));
    }

    // Beat and bar positions are cached when source/project state changes.
    const double gridOffset = draggingGrid && slipGrid ? static_cast<double>(dragEndFrame) - dragOriginal : 0;
    const double visibleGridStart = viewStart - gridOffset;
    const double visibleGridEnd = viewStart + viewLength - gridOffset;
    auto firstBeat = std::lower_bound(beatFrames.begin(), beatFrames.end(), boundedFrame(visibleGridStart, maximumFrame));
    float lastBeatX = -std::numeric_limits<float>::max();
    float lastLabelX = -std::numeric_limits<float>::max();
    for (auto it = firstBeat; it != beatFrames.end() && static_cast<double>(*it) <= visibleGridEnd; ++it) {
        const auto index = static_cast<std::size_t>(it - beatFrames.begin());
        double frame = static_cast<double>(*it);
        if (draggingGrid && slipGrid) frame += static_cast<double>(dragEndFrame) - dragOriginal;
        const auto x = xAt(boundedFrame(frame, maximumFrame));
        if (x < plot.getX() || x > plot.getRight() || x - lastBeatX < 1.5f) continue;
        lastBeatX = x;
        const bool bar = index % 4 == 0;
        g.setColour(HardwareLookAndFeel::lcdInk().withAlpha(gridEdit ? (bar ? .42f : .29f) : (bar ? .18f : .09f)));
        if (bar || overview) g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
        else {
            g.drawLine(x, plot.getY(), x, plot.getY() + 13, 1);
            g.drawLine(x, plot.getBottom() - 13, x, plot.getBottom(), 1);
        }
        if (gridEdit && bar && !overview && x - lastLabelX >= 36) {
            lastLabelX = x;
            g.setFont(juce::FontOptions(10));
            g.setColour(subdued);
            g.drawText(juce::String(index / 4 + 1), static_cast<int>(x) + 3, 1, 50, 15, juce::Justification::centredLeft);
        }
    }

    // Tempo anchors differ visually from grey beat/bar markers.
    if (gridEdit && project.beatgridEnabled && !overview) {
        for (const auto& marker : project.tempoMarkers) {
            const auto x = xAt(marker.frame);
            if (x < plot.getX() - 1 || x > plot.getRight() + 1) continue;
            g.setColour(juce::Colour(0xffed4d40));
            g.drawLine(x, plot.getY(), x, plot.getBottom(), 1.6f);
            g.fillRect(x - 3, plot.getY(), 6.f, 8.f);
        }
        if (draggingGrid) {
            const auto x = xAt(dragEndFrame);
            g.setColour(juce::Colour(0xffffc94a));
            g.drawLine(x, plot.getY(), x, plot.getBottom(), 2);
        }
    }

    for (int pass = 0; pass < 2; ++pass) {
        for (int index = 0; index < 32; ++index) {
            const auto& cue = project.pads[static_cast<std::size_t>(index)];
            if (cue.empty()) continue;
            const bool selected = (selection & (std::uint32_t(1) << index)) != 0;
            if (selected != (pass == 1)) continue;
            const auto start = dragCue == index ? dragStart : cue.start;
            const auto end = dragCue == index ? dragEndFrame : cue.end;
            const bool hasEnd = dragCue == index ? (dragEnd || dragHadEnd) : cue.hasEnd;
            const auto x = xAt(start);
            const auto colour = cueColourFor(cue);
            if (x >= plot.getX() - 1 && x <= plot.getRight() + 1) {
                g.setColour(colour.withAlpha(selected ? 1.f : .75f));
                g.drawLine(x, plot.getY(), x, plot.getBottom(), selected ? 2.3f : 1.3f);
                juce::Path flag;
                const float size = overview ? 9.f : selected ? 21.f : 15.f;
                flag.startNewSubPath(x - size, plot.getY());
                flag.lineTo(x, plot.getY()); flag.lineTo(x, plot.getY() + size); flag.closeSubPath();
                g.fillPath(flag);
            }
            if (hasEnd && end > start) {
                const auto endX = xAt(end);
                if (endX >= plot.getX() - 1 && endX <= plot.getRight() + 1) {
                    g.setColour(colour.withAlpha(selected ? 1.f : .6f));
                    g.drawLine(endX, plot.getY(), endX, plot.getBottom(), selected ? 2.3f : 1.f);
                    juce::Path flag;
                    const float size = overview ? 7.f : selected ? 21.f : 12.f;
                    flag.startNewSubPath(endX, plot.getBottom() - size);
                    flag.lineTo(endX, plot.getBottom()); flag.lineTo(endX + size, plot.getBottom()); flag.closeSubPath();
                    g.fillPath(flag);
                }
            }
        }
    }

    const auto playhead = xAt(playback.sourceFrame);
    if (playhead >= plot.getX() && playhead <= plot.getRight()) {
        g.setColour(HardwareLookAndFeel::lcdInk());
        g.drawLine(playhead, plot.getY(), playhead, plot.getBottom(), overview ? 1.5f : 3.f);
    }
}

void SourceWaveform::resized() {
    if (overview) return;
    const int size = std::clamp(getHeight() / 8, 27, 42);
    plusButton.setBounds(8, 22, size, size);
    minusButton.setBounds(8, 26 + size, size, size);
    freezeButton.setBounds(8, std::max(22, getHeight() - 54), 58, 30);
    gridButton.setBounds(71, std::max(22, getHeight() - 54), 51, 30);
}

void SourceWaveform::timerCallback() {
    if (overview) return;
    const bool show = getLocalBounds().contains(getMouseXYRelative()) || pointerDown;
    for (auto* button : { &plusButton, &minusButton, &freezeButton, &gridButton })
        button->setVisible(show);
}

void SourceWaveform::mouseEnter(const juce::MouseEvent&) { timerCallback(); }
void SourceWaveform::mouseExit(const juce::MouseEvent&) { timerCallback(); }

void SourceWaveform::mouseDown(const juce::MouseEvent& event) {
    if (!source || source->frames() == 0) return;
    pointerDown = true;
    dragCue = -1; draggingGrid = false; slipGrid = false;
    dragMouseX = event.position.x;
    const auto plot = plotBounds();
    if (overview) {
        if (onSeek) onSeek(frameAt(event.position.x));
        return;
    }
    if (gridEdit && project.beatgridEnabled) {
        std::size_t closest = frameAt(event.position.x);
        float distance = std::numeric_limits<float>::max();
        for (auto frame : beatFrames) {
            const auto d = std::abs(xAt(frame) - event.position.x);
            if (d < distance) { distance = d; closest = frame; }
        }
        for (const auto& marker : project.tempoMarkers) {
            const auto d = std::abs(xAt(marker.frame) - event.position.x);
            if (d <= distance) { distance = d; closest = marker.frame; }
        }
        if (event.mods.isCtrlDown() || distance <= 9.f) {
            draggingGrid = true;
            slipGrid = event.mods.isCtrlDown();
            dragOriginal = slipGrid ? frameAt(event.position.x) : closest;
            dragEndFrame = dragOriginal;
            return;
        }
    }
    if (!gridEdit) {
        for (int pass = 0; pass < 2 && dragCue < 0; ++pass) {
            for (int index = 31; index >= 0; --index) {
                const auto& cue = project.pads[static_cast<std::size_t>(index)];
                const bool selected = (selection & (std::uint32_t(1) << index)) != 0;
                if (cue.empty() || selected != (pass == 0)) continue;
                const auto startX = xAt(cue.start), endX = xAt(cue.end);
                const float startSize = selected ? 21.f : 15.f, endSize = selected ? 21.f : 12.f;
                const bool startHit = event.position.x >= startX - startSize - 3 && event.position.x <= startX + 6
                    && event.position.y <= plot.getY() + startSize + 5;
                const bool endHit = cue.hasEnd && event.position.x >= endX - 6 && event.position.x <= endX + endSize + 3
                    && event.position.y >= plot.getBottom() - endSize - 5;
                if (!startHit && !endHit) continue;
                dragCue = index; dragEnd = endHit; dragHadEnd = cue.hasEnd;
                dragStart = cue.start; dragEndFrame = cue.end;
                dragOriginal = dragEnd ? cue.end : cue.start;
                if (onCueSelected) onCueSelected(index);
                return;
            }
        }
        if (event.mods.isAltDown() && selection != 0) {
            for (int index = 0; index < 32; ++index) {
                const auto& cue = project.pads[static_cast<std::size_t>(index)];
                if ((selection & (std::uint32_t(1) << index)) == 0 || cue.empty()) continue;
                dragCue = index; dragEnd = true; dragHadEnd = cue.hasEnd;
                dragStart = cue.start;
                const auto endpoint = event.position.x >= plot.getRight()
                    ? source->frames() : frameAt(event.position.x);
                dragEndFrame = std::max(cue.start + 1, endpoint);
                dragOriginal = dragEndFrame;
                return;
            }
        }
    }
    if (onSeek) onSeek(frameAt(event.position.x));
}

void SourceWaveform::mouseDrag(const juce::MouseEvent& event) {
    if (!source || source->frames() == 0) return;
    const auto frame = draggedFrame(event);
    if (draggingGrid) {
        dragEndFrame = frame;
    } else if (dragCue >= 0) {
        if (dragEnd) {
            auto end = frame >= source->frames() ? source->frames()
                : snapCueFrame(project, frame, source->sampleRate, source->frames());
            if (end <= dragStart && !snapFrames.empty()) {
                const auto next = std::upper_bound(snapFrames.begin(), snapFrames.end(), dragStart);
                end = next != snapFrames.end() ? *next : source->frames();
            }
            dragEndFrame = std::clamp(end, dragStart + 1, source->frames());
        } else {
            auto start = snapCueFrame(project, frame, source->sampleRate, source->frames());
            if (start >= dragEndFrame && !snapFrames.empty()) {
                const auto previous = std::lower_bound(snapFrames.begin(), snapFrames.end(), dragEndFrame);
                start = previous == snapFrames.begin() ? 0 : *std::prev(previous);
            }
            dragStart = std::min(start, dragEndFrame > 0 ? dragEndFrame - 1 : 0);
        }
    } else if (onSeek) {
        onSeek(frameAt(event.position.x));
    }
    repaint();
}

void SourceWaveform::mouseUp(const juce::MouseEvent& event) {
    if (draggingGrid) {
        if (onGridDrag && dragOriginal != dragEndFrame) onGridDrag(dragOriginal, dragEndFrame, slipGrid);
    } else if (dragCue >= 0) {
        const auto& original = project.pads[static_cast<std::size_t>(dragCue)];
        const bool hasEnd = dragEnd || dragHadEnd;
        const bool changed = dragStart != original.start || dragEndFrame != original.end || hasEnd != original.hasEnd;
        if (onCueEdited && changed) onCueEdited(dragCue, dragStart, dragEndFrame, hasEnd);
    } else if (pointerDown && onSeek) {
        onSeek(frameAt(event.position.x));
    }
    pointerDown = false;
    draggingGrid = false;
    dragCue = -1;
    repaint();
}

void SourceWaveform::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (overview) return;
    const auto delta = static_cast<double>(wheel.deltaY);
    if (delta != 0) zoomAt(std::pow(1.5, -delta * 3), event.position.x);
}

} // namespace samplify
