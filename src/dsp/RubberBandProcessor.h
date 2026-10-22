#pragma once

#include "core/Model.h"

namespace samplify {

// High-quality offline time/pitch processing for cue preparation and export.
// durationRatio multiplies duration, while semitones changes pitch independently.
// The output has max(1, round(input.frames() * durationRatio)) frames.
//
// Run on a worker thread: construction, the analysis pass, and processing allocate.
// Throws for malformed audio, unsupported ratios/pitches, or the 64 Mi-sample
// aggregate input/output budget. No level, envelope, filter, or stem processing
// is applied here. This is Rubber Band R3, not Serato Pitch 'n Time.
AudioData renderDurationPitch(const AudioData& input, double durationRatio,
                             double semitones);

} // namespace samplify
