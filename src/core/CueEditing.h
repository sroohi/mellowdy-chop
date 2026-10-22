#pragma once
#include "DSP.h"

namespace samplify {
std::size_t snapCueFrame(const Project&, std::size_t frame, double sampleRate, std::size_t totalFrames);
void assignCueAt(Project&, int index, std::size_t frame, double sampleRate, std::size_t totalFrames);
void moveCue(Project&, int from, int to, bool copy);
void autoSetCues(Project&, const AudioData&, const Analysis&, ChopMode,
                std::size_t playhead, double beatsPerSlice = 1,
                int keyStep = 1, std::uint32_t seed = 42, int selectedCue = 0);
std::vector<std::size_t> projectBeatFrames(const Project&, double sampleRate, std::size_t totalFrames,
                                          double subdivision = 1);
std::uint32_t cueColour(std::size_t frame, std::size_t totalFrames);
} // namespace samplify
