#pragma once
#include "Model.h"

namespace samplify {

Analysis analyse(const AudioData& audio, std::size_t waveformBins = 1024);
std::vector<Pad> makeSlices(const AudioData& audio, const Analysis& analysis,
                          ChopMode mode, int count, double beatsPerSlice = 1.0,
                          std::uint32_t seed = 42);
// speedRatio is source playback speed (project BPM / source BPM), independent of pitch.
// Non-realtime, deterministic offline rendering for playback and export.
AudioData renderPad(const AudioData& audio, const Pad& pad, double speedRatio = 1.0);
int keySemitoneOffset(const std::string& source, const std::string& target);

} // namespace samplify
