#include "dsp/RubberBandProcessor.h"
#include <src/faster/PercussiveAudioCurve.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0, failures = 0;
void check(bool passed, const std::string& name) {
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
template<class Function> void rejects(Function function, const std::string& name) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, name);
}

samplify::AudioData tone(double frequency = 440, double duration = 1.5, double rate = 22050) {
    samplify::AudioData a;
    a.sampleRate = rate;
    a.channels.assign(2, std::vector<float>(static_cast<std::size_t>(std::llround(rate * duration))));
    for (std::size_t i = 0; i < a.frames(); ++i) {
        const float s = static_cast<float>(.4 * std::sin(2 * pi * frequency * static_cast<double>(i) / rate));
        a.channels[0][i] = s;
        a.channels[1][i] = -s;
    }
    return a;
}
bool finiteAudio(const samplify::AudioData& a) {
    if (a.frames() == 0 || !std::isfinite(a.sampleRate)) return false;
    return std::all_of(a.channels.begin(), a.channels.end(), [&](const auto& c) {
        return c.size() == a.frames() && std::all_of(c.begin(), c.end(), [](float v) { return std::isfinite(v); });
    });
}
double frequency(const samplify::AudioData& a) {
    const auto& s = a.channels[0];
    const auto begin = s.size() / 4, end = s.size() * 3 / 4;
    std::size_t crossings = 0, first = 0, last = 0;
    for (auto i = begin + 1; i < end; ++i) {
        if (s[i - 1] <= 0 && s[i] > 0) {
            if (!crossings) first = i;
            last = i; ++crossings;
        }
    }
    return crossings > 1 ? static_cast<double>(crossings - 1) * a.sampleRate / static_cast<double>(last - first) : 0;
}
double stereoError(const samplify::AudioData& a) {
    double maximum = 0;
    for (std::size_t i = 0; i < a.frames(); ++i)
        maximum = std::max(maximum, std::abs(static_cast<double>(a.channels[0][i]) + a.channels[1][i]));
    return maximum;
}
void renderTests() {
    using samplify::renderDurationPitch;
    auto a = tone();
    const auto passthrough = renderDurationPitch(a, 1, 0);
    check(passthrough.channels == a.channels && passthrough.sampleRate == a.sampleRate,
          "identity transform preserves exact samples and rate");
    const auto stretched = renderDurationPitch(a, 1.5, 0);
    check(stretched.frames() == static_cast<std::size_t>(std::llround(a.frames() * 1.5)), "stretch has exact target duration");
    check(finiteAudio(stretched) && std::abs(frequency(stretched) - 440) < 3, "stretch preserves pitch");
    check(stereoError(stretched) < 1e-5, "stretch preserves phase-inverted stereo relationship");
    const auto compressed = renderDurationPitch(a, .5, 0);
    check(compressed.frames() == static_cast<std::size_t>(std::llround(a.frames() * .5)), "compression has exact target duration");
    check(finiteAudio(compressed) && std::abs(frequency(compressed) - 440) < 3, "compression preserves pitch");
    const auto octave = renderDurationPitch(a, 1, 12);
    check(octave.frames() == a.frames(), "pitch shift preserves duration");
    check(finiteAudio(octave) && std::abs(frequency(octave) - 880) < 5, "octave shift doubles frequency");
    const auto combined = renderDurationPitch(a, .8, -12);
    check(combined.frames() == static_cast<std::size_t>(std::llround(a.frames() * .8))
          && std::abs(frequency(combined) - 220) < 3, "combined duration and pitch are independent");
    check(stereoError(octave) < 1e-5, "pitch shift preserves phase-inverted stereo relationship");
    std::cout << "Rubber Band R3: stretch " << frequency(stretched) << " Hz, octave " << frequency(octave) << " Hz\n";

    auto silence = a;
    for (auto& c : silence.channels) std::fill(c.begin(), c.end(), 0);
    auto quiet = renderDurationPitch(silence, 1.1, 7);
    check(finiteAudio(quiet) && std::all_of(quiet.channels[0].begin(), quiet.channels[0].end(), [](float v) { return v == 0; }),
          "silence is exact and finite at changed duration and pitch");
    samplify::AudioData one;
    one.channels = {{.25f}, {-.25f}};
    const auto single = renderDurationPitch(one, 3, 12);
    check(single.frames() == 3 && single.channels[0][2] == .25f && single.channels[1][2] == -.25f,
          "single-frame input has a finite duration fallback");
    const auto shortAudio = tone(1000, .005, 44100);
    const auto shortRender = renderDurationPitch(shortAudio, 1.25, 3);
    check(finiteAudio(shortRender) && shortRender.frames() == static_cast<std::size_t>(std::llround(shortAudio.frames() * 1.25)),
          "sub-window audio produces an exact finite output");
}
void invalidTests() {
    using samplify::renderDurationPitch;
    const auto a = tone();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { renderDurationPitch(a, 0, 0); }, "zero duration ratio is rejected");
    rejects([&] { renderDurationPitch(a, -1, 0); }, "negative duration ratio is rejected");
    rejects([&] { renderDurationPitch(a, nan, 0); }, "NaN duration ratio is rejected");
    rejects([&] { renderDurationPitch(a, 1, nan); }, "NaN pitch is rejected");
    rejects([&] { renderDurationPitch(a, 1, 100); }, "unsupported pitch is rejected");
    rejects([&] { renderDurationPitch({}, 1, 0); }, "empty audio is rejected");
    auto bad = a; bad.channels[1].pop_back();
    rejects([&] { renderDurationPitch(bad, 1, 0); }, "mismatched channels are rejected");
    bad = a; bad.channels[0][0] = std::numeric_limits<float>::infinity();
    rejects([&] { renderDurationPitch(bad, 1, 0); }, "non-finite samples are rejected");
    bad = a; bad.sampleRate = 0;
    rejects([&] { renderDurationPitch(bad, 1, 0); }, "invalid sample rate is rejected");
}

void realtimeCapacityTests() {
    // This narrow regression exercises the patched R2 onset detector directly:
    // after off-thread preparation, FFT-size changes must retain its storage,
    // while every logical reset must match a newly constructed detector.
    struct Detector final : RubberBand::PercussiveAudioCurve {
        explicit Detector(int size) : PercussiveAudioCurve({48000, size}) {}
        const double* storage() const { return m_prevMag; }
        int capacity() const { return m_prevMagCapacity; }
    };
    Detector detector(256);
    check(detector.capacity() == 129, "R2 detector initializes capacity for the original FFT");
    detector.setFftSize(8192);
    const auto* storage = detector.storage();
    bool retained = detector.capacity() == 4097;
    bool equivalent = true;
    std::vector<double> magnitude(4097);
    for (const int size : {64, 512, 2048, 8192, 256, 4096, 8192, 64, 8192}) {
        detector.setFftSize(size);
        retained = retained && detector.storage() == storage && detector.capacity() == 4097;
        Detector fresh(size);
        for (int frame = 0; frame < 4; ++frame) {
            for (int bin = 0; bin <= size / 2; ++bin)
                magnitude[static_cast<std::size_t>(bin)] = (bin + frame) % 11 == 0 ? 0. : .01 * (1 + ((bin * 17 + frame * 31) % 97));
            equivalent = equivalent && detector.processDouble(magnitude.data(), 64) == fresh.processDouble(magnitude.data(), 64);
        }
    }
    check(retained, "R2 detector changes FFT size without reallocating warmed storage");
    check(equivalent, "R2 detector reset and arithmetic match fresh detector after every FFT change");
}
}

int main() {
    try { renderTests(); invalidTests(); realtimeCapacityTests(); }
    catch (const std::exception& e) { ++failures; std::cerr << "Unexpected processor error: " << e.what() << '\n'; }
    std::cout << checks - failures << '/' << checks << " stretch checks passed\n";
    return failures ? 1 : 0;
}
