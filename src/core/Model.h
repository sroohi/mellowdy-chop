#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace samplify {

struct AudioData {
    double sampleRate = 44100.0;
    std::vector<std::vector<float>> channels;
    std::size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
    double duration() const { return sampleRate > 0 ? static_cast<double>(frames()) / sampleRate : 0; }
    bool valid() const;
};

struct Pad {
    std::size_t start = 0;
    std::size_t end = 0;
    float levelDb = 0;
    float pitchSemitones = 0;
    float detuneCents = 0;
    float timeRatio = 1;
    float filter = 0.5f;
    float attackMs = 5;
    float releaseMs = 150;
    bool reverse = false;
    std::array<bool, 4> stemMute { false, false, false, false };
    bool empty() const { return end <= start; }
};

struct Project {
    double projectBpm = 120;
    double sourceBpm = 120;
    std::string projectKey = "C minor";
    std::string sourceKey = "Unknown";
    bool mono = true;
    bool quantize = false;
    bool syncTempo = false;
    bool syncKey = false;
    int padCount = 16;
    std::array<Pad, 32> pads;
};

struct WaveformBin {
    float peak = 0;
    float low = 0;
    float mid = 0;
    float high = 0;
};

struct Analysis {
    double bpm = 120;
    std::string key = "Unknown";
    std::vector<std::size_t> onsets;
    std::vector<std::size_t> beats;
    std::vector<WaveformBin> waveform;
};

enum class ChopMode { Transients, Grid, Random };

} // namespace samplify
