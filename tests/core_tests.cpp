#include "core/DSP.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace samplify;

namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0;
int failures = 0;
void check(bool success, const std::string& message) {
    ++checks;
    if (!success) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

AudioData tone(double hz, double seconds = 2, double rate = 22050) {
    AudioData audio;
    audio.sampleRate = rate;
    audio.channels.emplace_back(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < audio.frames(); ++i) audio.channels[0][i] = static_cast<float>(0.4 * std::sin(2 * pi * hz * i / rate));
    return audio;
}

double frequency(const AudioData& audio) {
    const auto& samples = audio.channels.front();
    const auto begin = samples.size() / 5, end = samples.size() * 4 / 5;
    std::size_t crossings = 0, first = 0, last = 0;
    for (auto i = begin + 1; i < end; ++i) {
        if (samples[i - 1] <= 0 && samples[i] > 0) {
            if (!crossings) first = i;
            last = i;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * audio.sampleRate / (last - first) : 0;
}

bool bounded(const std::vector<Pad>& pads, std::size_t frames) {
    return std::all_of(pads.begin(), pads.end(), [=](const Pad& pad) {
        return pad.start < pad.end && pad.end <= frames;
    });
}

void modelAndInvalidInputs() {
    const Project defaults;
    check(defaults.padCount == 32, "project has 32 cue slots by default");
    check(defaults.playbackMode == CuePlaybackMode::Hold, "Hold is the default cue playback mode");
    AudioData empty;
    check(!empty.valid(), "empty audio is invalid");
    auto audio = tone(440);
    check(audio.valid(), "finite audio is valid");
    audio.channels.push_back({0});
    check(!audio.valid(), "unequal channel lengths are invalid");
    audio.channels.pop_back();
    audio.channels[0][10] = std::numeric_limits<float>::quiet_NaN();
    check(!audio.valid(), "NaN samples are invalid");
    check(analyse(audio).waveform.empty(), "invalid analysis returns no waveform");
    check(makeSlices(audio, {}, ChopMode::Grid, 16).empty(), "invalid slicing returns no pads");
    audio = tone(440);
    audio.sampleRate = std::numeric_limits<double>::infinity();
    check(!audio.valid(), "nonfinite sample rate is invalid");
    audio.sampleRate = 0;
    check(!audio.valid(), "zero sample rate is invalid");
}

void rhythmAndWaveform() {
    constexpr double rate = 22050;
    AudioData clicks;
    clicks.sampleRate = rate;
    clicks.channels.emplace_back(static_cast<std::size_t>(12 * rate), 0);
    for (int beat = 0; beat < 24; ++beat) {
        const auto start = static_cast<std::size_t>(beat * rate / 2);
        for (std::size_t i = 0; i < 200; ++i) {
            clicks.channels[0][start + i] = static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 35)
                * std::cos(2 * pi * 1800 * i / rate));
        }
    }
    const auto analysis = analyse(clicks, 300);
    std::cout << "Click fixture BPM: " << analysis.bpm << ", onsets: " << analysis.onsets.size() << '\n';
    check(std::abs(analysis.bpm - 120) < 2, "120 BPM click train estimate within 2 BPM");
    check(analysis.onsets.size() >= 20 && analysis.onsets.size() <= 25, "click train yields real onset events");
    check(analysis.beats.size() >= 22, "detected tempo creates a useful beat grid");
    check(analysis.waveform.size() == 300, "requested waveform bin count is respected");
    check(std::all_of(analysis.onsets.begin(), analysis.onsets.end(), [&](auto value) { return value < clicks.frames(); }), "onsets remain inside source");
    check(std::all_of(analysis.beats.begin(), analysis.beats.end(), [&](auto value) { return value < clicks.frames(); }), "beat grid remains inside source");
    const auto low = analyse(tone(90), 32), high = analyse(tone(7000), 32);
    double lowEnergy = 0, lowHighEnergy = 0, highEnergy = 0, highLowEnergy = 0;
    for (const auto& bin : low.waveform) { lowEnergy += bin.low; lowHighEnergy += bin.high; }
    for (const auto& bin : high.waveform) { highEnergy += bin.high; highLowEnergy += bin.low; }
    check(lowEnergy > lowHighEnergy * 5, "low-frequency tone colors waveform low band");
    check(highEnergy > highLowEnergy * 5, "high-frequency tone colors waveform high band");
    auto silence = tone(0);
    const auto silent = analyse(silence);
    check(silent.onsets.empty() && silent.beats.empty() && silent.key == "Unknown", "silence has no fabricated rhythm or key");
    check(analyse(clicks, 0).waveform.empty(), "zero waveform request is safe");
    check(analyse(clicks, 1000000).waveform.size() == 16384, "waveform allocation is bounded");
}

void musicalKey() {
    auto chord = tone(0, 3);
    for (std::size_t i = 0; i < chord.frames(); ++i) {
        chord.channels[0][i] = static_cast<float>((std::sin(2 * pi * 261.625565 * i / chord.sampleRate)
            + std::sin(2 * pi * 329.627557 * i / chord.sampleRate)
            + std::sin(2 * pi * 391.995436 * i / chord.sampleRate)) / 5);
    }
    const auto result = analyse(chord, 0);
    std::cout << "C-major chord key: " << result.key << '\n';
    check(result.key == "C major", "C-major triad estimates C major");
    check(keySemitoneOffset("C major", "D minor") == 2, "key transpose moves C to D two semitones");
    check(keySemitoneOffset("A minor", "C major") == 3, "key transpose wraps octaves");
    check(keySemitoneOffset("Db minor", "C# major") == 0, "enharmonic spellings agree");
    check(keySemitoneOffset("D♭ minor", "C♯ major") == 0, "Unicode accidentals agree");
    check(keySemitoneOffset("Unknown", "C major") == 0, "unknown key does not invent transpose");
    check(keySemitoneOffset("", "A") == 0, "empty key is safe");
}

void slicing() {
    const auto audio = tone(440, 4);
    Analysis analysis;
    analysis.bpm = 120;
    analysis.onsets = {22050, 0, 44100, 44100, 66150, audio.frames() + 100};
    const auto grid = makeSlices(audio, analysis, ChopMode::Grid, 16);
    check(grid.size() == 8 && bounded(grid, audio.frames()), "grid makes eight half-second slices in four seconds");
    check(grid[0].end == 11025 && grid.back().end == audio.frames(), "grid boundaries match tempo and final frame");
    const auto transient = makeSlices(audio, analysis, ChopMode::Transients, 16);
    check(transient.size() == 4 && bounded(transient, audio.frames()), "transients sort, deduplicate, and reject out-of-range onsets");
    // Irregular phase/spacing ensures random mode consumes the supplied grid,
    // rather than generating arbitrary samples or substituting a BPM grid.
    for (std::size_t frame = 211; frame < audio.frames(); frame += 977) analysis.beats.push_back(frame);
    std::reverse(analysis.beats.begin(), analysis.beats.end());
    analysis.beats.push_back(211);
    analysis.beats.push_back(0);
    analysis.beats.push_back(audio.frames() + 1);
    const auto randomA = makeSlices(audio, analysis, ChopMode::Random, 16, 1, 27);
    const auto randomB = makeSlices(audio, analysis, ChopMode::Random, 16, 1, 27);
    const auto randomC = makeSlices(audio, analysis, ChopMode::Random, 16, 1, 28);
    bool same = randomA.size() == randomB.size(), different = false;
    for (std::size_t i = 0; i < randomA.size(); ++i) {
        same = same && randomA[i].start == randomB[i].start && randomA[i].end == randomB[i].end;
        different = different || randomA[i].end != randomC[i].end;
    }
    check(same && different, "seeded random slicing is reproducible and changes with seed");
    check(randomA.size() == 16 && bounded(randomA, audio.frames())
        && randomA.front().start == 0 && randomA.back().end == audio.frames(), "random slices partition the full source within bounds");
    check(std::all_of(randomA.begin(), randomA.end() - 1, [&](const Pad& pad) {
        return std::find(analysis.beats.begin(), analysis.beats.end(), pad.end) != analysis.beats.end();
    }), "every internal random cue lies on the supplied detected beat grid");
    Analysis sparse;
    sparse.beats = {441, 22050, 22050, audio.frames() + 100};
    const auto sparseRandom = makeSlices(audio, sparse, ChopMode::Random, 16);
    check(sparseRandom.size() == 3 && sparseRandom[0].end == 441 && sparseRandom[1].end == 22050
        && bounded(sparseRandom, audio.frames()), "insufficient detected beat grid yields fewer unique musical slices");
    const auto derivedRandom = makeSlices(audio, {}, ChopMode::Random, 16);
    check(derivedRandom.size() == 8 && std::all_of(derivedRandom.begin(), derivedRandom.end() - 1,
        [](const Pad& pad) { return pad.end % 11025 == 0; }), "missing detected beats fall back to a bounded BPM grid");
    check(makeSlices(audio, analysis, ChopMode::Grid, 16, 0).empty(), "invalid grid division is rejected");
    check(makeSlices(audio, analysis, ChopMode::Grid, 0).empty(), "zero pad request is rejected");
    check(makeSlices(audio, analysis, ChopMode::Random, 200).size() == 32, "pad count is limited to model capacity");
}

void rendering() {
    const auto audio = tone(440);
    Pad pad;
    pad.end = audio.frames();
    pad.attackMs = 0;
    pad.releaseMs = 0;
    const auto original = renderPad(audio, pad);
    check(original.valid() && original.frames() == audio.frames(), "neutral render preserves frame count");
    check(original.channels == audio.channels, "neutral render preserves samples exactly");
    const auto faster = renderPad(audio, pad, 1.5);
    std::cout << "Time-scaled tone frequency: " << frequency(faster) << '\n';
    check(faster.valid() && std::abs(static_cast<double>(faster.frames()) - audio.frames() / 1.5) <= 1,
          "tempo scales output duration");
    check(std::abs(frequency(faster) - 440) < 5, "tempo change preserves pitch");
    pad.pitchSemitones = 12;
    const auto higher = renderPad(audio, pad);
    std::cout << "Octave-shifted tone frequency: " << frequency(higher) << '\n';
    check(higher.valid() && higher.frames() == audio.frames(), "pitch shift preserves duration");
    check(std::abs(frequency(higher) - 880) < 10, "octave transpose doubles tone frequency");
    pad.pitchSemitones = 0;
    pad.speedRatio = 1.5;
    const auto cueFaster = renderPad(audio, pad);
    check(std::abs(static_cast<double>(cueFaster.frames()) - audio.frames() / 1.5) <= 1
        && std::abs(frequency(cueFaster) - 440) < 7,
          "cue speed ratio divides duration independently of pitch");
    pad.speedRatio = 2;
    const auto doubleSpeed = renderPad(audio, pad);
    check(doubleSpeed.frames() == audio.frames() / 2 && std::abs(frequency(doubleSpeed) - 440) < 7,
          "2x cue speed halves duration while preserving pitch");
    pad.speedRatio = .5f;
    const auto halfSpeed = renderPad(audio, pad);
    check(halfSpeed.frames() == audio.frames() * 2 && std::abs(frequency(halfSpeed) - 440) < 7,
          "half cue speed doubles duration while preserving pitch");
    pad.speedRatio = 1;
    pad.reverse = true;
    const auto reversed = renderPad(audio, pad);
    check(reversed.channels[0].front() == audio.channels[0].back()
        && reversed.channels[0].back() == audio.channels[0].front(), "reverse swaps source endpoints");
    pad.reverse = false;
    pad.attackMs = 50;
    pad.releaseMs = 50;
    const auto enveloped = renderPad(audio, pad);
    check(enveloped.channels[0].front() == 0 && enveloped.channels[0].back() == 0, "envelope fades both endpoints");
    pad.start = audio.frames() + 1;
    check(!renderPad(audio, pad).valid(), "out-of-range pad start is rejected");
    pad.start = 0;
    pad.end = audio.frames() + 200;
    check(renderPad(audio, pad).frames() == audio.frames(), "pad end clamps to source bounds");
    pad.speedRatio = std::numeric_limits<float>::quiet_NaN();
    check(!renderPad(audio, pad).valid(), "NaN control is rejected");
    pad.speedRatio = 1;
    check(!renderPad(audio, pad, 0).valid(), "zero playback speed is rejected");
    AudioData tiny;
    tiny.channels = {{0.5f}};
    pad.end = 1;
    pad.attackMs = 0;
    pad.releaseMs = 0;
    const auto tinyOutput = renderPad(tiny, pad);
    check(tinyOutput.valid() && tinyOutput.frames() == 1 && tinyOutput.channels[0][0] == 0.5f, "one-frame audio renders safely");
    check(bounded(makeSlices(tiny, {}, ChopMode::Random, 32), 1), "one-frame audio slices safely");
    check(analyse(tiny).key == "Unknown", "one-frame audio has unknown key");
    auto stereo = audio;
    stereo.channels.push_back(stereo.channels.front());
    for (auto& sample : stereo.channels.back()) sample = -sample;
    pad.end = stereo.frames();
    const auto stereoOutput = renderPad(stereo, pad, 1.3);
    bool coherent = stereoOutput.valid();
    for (std::size_t i = 0; i < stereoOutput.frames(); ++i) {
        coherent = coherent && std::abs(stereoOutput.channels[0][i] + stereoOutput.channels[1][i]) < 1e-6f;
    }
    check(coherent, "time stretching preserves stereo phase relationship");
    pad.filter = 0;
    const auto filtered = renderPad(audio, pad);
    double unfilteredEnergy = 0, filteredEnergy = 0;
    for (std::size_t i = 1000; i < audio.frames(); ++i) {
        unfilteredEnergy += audio.channels[0][i] * audio.channels[0][i];
        filteredEnergy += filtered.channels[0][i] * filtered.channels[0][i];
    }
    check(filteredEnergy < unfilteredEnergy * 0.1, "low-pass filter attenuates a tone above cutoff");
    pad.filter = 0.5f;
    pad.levelDb = -6.0206f;
    const auto quieter = renderPad(audio, pad);
    check(std::abs(quieter.channels[0][100] - audio.channels[0][100] * 0.5f) < 1e-5f,
          "decibel gain controls sample level");
    AudioData large;
    large.channels = {std::vector<float>(256, std::numeric_limits<float>::max() * 0.9f)};
    pad.end = large.frames();
    pad.levelDb = 24;
    check(renderPad(large, pad, 0.5).valid(), "finite extreme samples cannot overflow rendered audio");
}
} // namespace

int main() {
    modelAndInvalidInputs();
    rhythmAndWaveform();
    musicalKey();
    slicing();
    rendering();
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
