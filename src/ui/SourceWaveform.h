#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "audio/AudioEngine.h"
#include <functional>
#include <memory>

namespace samplify {

// Full-file overview and zoomed editor share the same source/selection state.
class SourceWaveform final : public juce::Component, public juce::SettableTooltipClient,
                             private juce::Timer {
public:
    explicit SourceWaveform(bool overview = false);
    void setData(std::shared_ptr<const AudioData>, const Analysis&);
    void setProject(const Project&, std::uint32_t selectedMask);
    void setPlayback(const PlaybackSnapshot&);
    void setFrozen(bool);
    bool isFrozen() const { return frozen; }
    void setGridEditMode(bool);
    void zoomIn();
    void zoomOut();
    void centreOn(std::size_t frame);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void(std::size_t)> onSeek;
    std::function<void(int)> onCueSelected;
    std::function<void(int, std::size_t, std::size_t, bool)> onCueEdited;
    std::function<void(std::size_t, std::size_t, bool)> onGridDrag;
    std::function<void(bool)> onFreezeChanged;
    std::function<void(bool)> onGridEditChanged;
private:
    void timerCallback() override;
    void rebuildGrid();
    void zoomAt(double factor, float x);
    juce::Rectangle<float> plotBounds() const;
    std::size_t draggedFrame(const juce::MouseEvent&) const;
    std::size_t frameAt(float x) const;
    float xAt(std::size_t frame) const;
    void clampView();
    bool overview = false, frozen = false, gridEdit = false;
    std::shared_ptr<const AudioData> source;
    Analysis analysis;
    Project project;
    PlaybackSnapshot playback;
    std::uint32_t selection = 0;
    std::vector<std::size_t> beatFrames;
    std::vector<std::size_t> snapFrames;
    double viewStart = 0, viewLength = 0;
    bool pointerDown = false;
    bool dragHadEnd = false;
    float dragMouseX = 0;
    int dragCue = -1;
    bool dragEnd = false, draggingGrid = false, slipGrid = false;
    std::size_t dragOriginal = 0, dragStart = 0, dragEndFrame = 0;
    juce::TextButton plusButton { "+" }, minusButton { "-" };
    juce::TextButton freezeButton { "FREEZE" }, gridButton { "GRID" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceWaveform)
};

} // namespace samplify
