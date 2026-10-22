#pragma once

#include "core/Model.h"

namespace juce { class File; }

namespace samplify {

// Read embedded Serato DJ hotcues without decoding audio. Supports ID3 GEOB
// frames in MP3 and ID3 chunks in AIFF/WAV, plus FLAC SERATO_MARKERS_V2 comments.
// Positions are stored milliseconds converted to the supplied decoded sample
// rate. MP3 decoder-specific delay offsets are not guessed.
//
// Original hotcue indices are preserved: unset slots are empty Pads, and the
// result ends after the highest valid index (at most 32). A missing, unreadable,
// unsupported, or malformed tag returns an empty vector. Imported hotcues have
// no explicit end marker (hasEnd=false, end=totalFrames).
std::vector<Pad> readSeratoCues(const juce::File& file, double sampleRate,
                              std::size_t totalFrames);

} // namespace samplify
