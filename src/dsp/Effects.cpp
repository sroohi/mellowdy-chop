#include "Effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace samplify {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr FxParameter parameter(const char* name, const char* unit, float lo, float hi,
                                float initial, bool logarithmic = false) {
    return { name, unit, lo, hi, initial, logarithmic };
}
const std::array<FxDefinition, 16> definitions {{
    { FxType::None, "Off", {}, 0 },
    { FxType::Equalizer, "Three-band EQ", {{
        parameter("Low", "dB", -18, 18, 0), parameter("Mid", "dB", -18, 18, 0),
        parameter("High", "dB", -18, 18, 0), parameter("Mid frequency", "Hz", 120, 8000, 1000, true),
        parameter("Low frequency", "Hz", 40, 400, 120, true), parameter("High frequency", "Hz", 1500, 16000, 6000, true) }}, 6 },
    { FxType::Compressor, "Compressor", {{
        parameter("Threshold", "dB", -48, 0, -18), parameter("Ratio", ":1", 1, 20, 4, true),
        parameter("Attack", "ms", .1f, 100, 10, true), parameter("Release", "ms", 10, 1000, 120, true),
        parameter("Makeup", "dB", 0, 18, 3), parameter("Knee", "dB", 0, 18, 6) }}, 6 },
    { FxType::Saturator, "Saturator", {{
        parameter("Drive", "dB", 0, 24, 8), parameter("Tone", "Hz", 300, 18000, 8000, true),
        parameter("Bias", "", -.4f, .4f, 0), parameter("Output", "dB", -18, 6, -3),
        parameter("Shape", "%", 0, 100, 40), {} }}, 5 },
    { FxType::BitCrusher, "Bit crusher", {{
        parameter("Resolution", "bits", 2, 24, 12), parameter("Sample rate", "Hz", 500, 48000, 12000, true),
        parameter("Clock jitter", "%", 0, 20, 0), {}, {}, {} }}, 3 },
    { FxType::Chorus, "Chorus", {{
        parameter("Delay", "ms", 8, 40, 18), parameter("Depth", "ms", 0, 12, 4),
        parameter("Rate", "Hz", .05f, 5, .7f, true), parameter("Feedback", "%", -50, 50, 10),
        parameter("Stereo phase", "deg", 0, 180, 90), {} }}, 5 },
    { FxType::Delay, "Stereo delay", {{
        parameter("Time", "ms", 1, 2000, 320, true), parameter("Feedback", "%", 0, 90, 35),
        parameter("Damping", "%", 0, 100, 35), parameter("Ping-pong", "%", 0, 100, 0),
        parameter("Low cut", "Hz", 20, 1000, 30, true), parameter("Stereo spread", "%", 0, 100, 0) }}, 6 },
    { FxType::Reverb, "Room reverb", {{
        parameter("Decay", "s", .2f, 8, 1.5f, true), parameter("Damping", "%", 0, 100, 45),
        parameter("Predelay", "ms", 0, 100, 12), parameter("Room size", "x", .5f, 1.5f, 1),
        parameter("Stereo width", "%", 0, 200, 100), parameter("Low cut", "Hz", 20, 1000, 120, true) }}, 6 },
    { FxType::Filter, "Resonant filter", {{
        parameter("Cutoff", "Hz", 20, 20000, 4000, true), parameter("Resonance", "Q", .35f, 12, .707f, true),
        parameter("Mode LP/BP/HP", "", 0, 2, 0), parameter("Drive", "dB", 0, 12, 0), {}, {} }}, 4 },
    { FxType::Tremolo, "Tremolo", {{
        parameter("Rate", "Hz", .05f, 20, 4, true), parameter("Depth", "%", 0, 100, 60),
        parameter("Square shape", "%", 0, 100, 0), parameter("Stereo phase", "deg", 0, 180, 0), {}, {} }}, 4 },
    { FxType::Limiter, "Peak limiter", {{
        parameter("Ceiling", "dB", -12, 0, -.3f), parameter("Release", "ms", 10, 500, 80, true),
        parameter("Input gain", "dB", 0, 12, 0), {}, {}, {} }}, 3 },
    { FxType::StereoWidth, "Stereo width", {{
        parameter("Width", "%", 0, 200, 125), parameter("Balance", "%", -100, 100, 0),
        parameter("Mono bass", "Hz", 0, 400, 0), parameter("Output", "dB", -12, 6, 0), {}, {} }}, 4 },
    { FxType::Transient, "Transient shaper", {{
        parameter("Attack", "%", -100, 100, 25), parameter("Sustain", "%", -100, 100, 0),
        parameter("Sensitivity", "x", .1f, 10, 2, true), parameter("Output", "dB", -12, 6, 0), {}, {} }}, 4 },
    { FxType::Flanger, "Flanger", {{
        parameter("Delay", "ms", .1f, 10, 2), parameter("Depth", "ms", 0, 8, 2),
        parameter("Rate", "Hz", .02f, 5, .25f, true), parameter("Feedback", "%", -95, 95, 50),
        parameter("Stereo phase", "deg", 0, 180, 90), parameter("Polarity", "", -1, 1, 1) }}, 6 },
    { FxType::Phaser, "Phaser", {{
        parameter("Centre", "Hz", 100, 3000, 700, true), parameter("Depth", "%", 0, 100, 70),
        parameter("Rate", "Hz", .02f, 5, .4f, true), parameter("Feedback", "%", -90, 90, 30),
        parameter("Stereo phase", "deg", 0, 180, 90), parameter("Stages", "", 2, 8, 6) }}, 6 },
    { FxType::Tape, "Tape colour", {{
        parameter("Drive", "dB", 0, 18, 5), parameter("Wow", "%", 0, 100, 20),
        parameter("Flutter", "%", 0, 100, 10), parameter("Hiss", "dB", -90, -30, -90),
        parameter("Tone", "Hz", 1000, 18000, 12000, true), parameter("Output", "dB", -12, 6, -1) }}, 6 }
}};

double dbGain(double db) { return std::pow(10.0, db / 20.0); }
float safe(float value) {
    return std::isfinite(value) && std::abs(value) > 1e-20f ? std::clamp(value, -32.f, 32.f) : 0.f;
}
double coefficient(double milliseconds, double rate) {
    return std::exp(-1.0 / (std::max(.01, milliseconds) * .001 * rate));
}
double lowPassAlpha(double hz, double rate) {
    return 1.0 - std::exp(-2 * pi * std::clamp(hz, 1.0, rate * .45) / rate);
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 2> z1 {}, z2 {};
    float process(float sample, std::size_t channel) {
        const double output = b0 * sample + z1[channel];
        z1[channel] = b1 * sample - a1 * output + z2[channel];
        z2[channel] = b2 * sample - a2 * output;
        if (std::abs(z1[channel]) < 1e-25) z1[channel] = 0;
        if (std::abs(z2[channel]) < 1e-25) z2[channel] = 0;
        return safe(static_cast<float>(output));
    }
    void reset() { z1 = {}; z2 = {}; }
    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void filter(double hz, double q, int mode, double rate) {
        const double w = 2 * pi * std::clamp(hz, 10.0, rate * .45) / rate;
        const double c = std::cos(w), alpha = std::sin(w) / (2 * std::max(.1, q));
        if (mode == 1) set(alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
        else if (mode == 2) set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        else set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    void peak(double hz, double gain, double rate) {
        const double w = 2 * pi * std::clamp(hz, 10.0, rate * .45) / rate;
        const double a = std::pow(10.0, gain / 40.0), alpha = std::sin(w) / 2;
        set(1 + alpha * a, -2 * std::cos(w), 1 - alpha * a, 1 + alpha / a, -2 * std::cos(w), 1 - alpha / a);
    }
    void shelf(double hz, double gain, bool high, double rate) {
        const double w = 2 * pi * std::clamp(hz, 10.0, rate * .45) / rate;
        const double a = std::pow(10.0, gain / 40.0), c = std::cos(w);
        const double x = 2 * std::sqrt(a) * std::sin(w) / std::sqrt(2.0);
        if (high) set(a * ((a + 1) + (a - 1) * c + x), -2 * a * ((a - 1) + (a + 1) * c),
            a * ((a + 1) + (a - 1) * c - x), (a + 1) - (a - 1) * c + x,
            2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - x);
        else set(a * ((a + 1) - (a - 1) * c + x), 2 * a * ((a - 1) - (a + 1) * c),
            a * ((a + 1) - (a - 1) * c - x), (a + 1) + (a - 1) * c + x,
            -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - x);
    }
};

// Logical reset invalidates old samples rather than clearing large buffers on
// the audio thread. Memory is acquired only during prepare().
struct DelayLine {
    std::vector<float> buffer;
    std::size_t position = 0, valid = 0;
    void prepare(std::size_t length) { buffer.assign(std::max<std::size_t>(length, 4), 0); reset(); }
    void reset() { position = valid = 0; }
    float read(double delay) const {
        if (buffer.empty()) return 0;
        delay = std::clamp(delay, 1.0, static_cast<double>(buffer.size() - 2));
        // At startup a fractional read can straddle the oldest valid sample.
        // Zero only the missing neighbour, rather than dropping that sample's
        // contribution and swallowing the first delayed impulse.
        if (delay > static_cast<double>(valid) + 1) return 0;
        double index = static_cast<double>(position) - delay;
        if (index < 0) index += static_cast<double>(buffer.size());
        const auto left = static_cast<std::size_t>(index);
        const auto right = left + 1 == buffer.size() ? 0 : left + 1;
        const auto fraction = static_cast<float>(index - static_cast<double>(left));
        const auto older = std::ceil(delay);
        const auto a = older <= static_cast<double>(valid) ? buffer[left] : 0.f;
        const auto b = older - 1 >= 1 && older - 1 <= static_cast<double>(valid) ? buffer[right] : 0.f;
        return a + fraction * (b - a);
    }
    void write(float sample) {
        if (buffer.empty()) return;
        buffer[position] = safe(sample);
        if (++position == buffer.size()) position = 0;
        valid = std::min(valid + 1, buffer.size());
    }
};

struct EffectState {
    FxSlot slot;
    bool initialised = false;
    std::array<double, 6> current {}, target {};
    double mix = 0, mixTarget = 0;
    double rate = 44100, smooth = .001;
    std::array<DelayLine, 2> delay;
    std::array<std::array<DelayLine, 4>, 2> comb;
    std::array<std::array<DelayLine, 2>, 2> allpass;
    std::array<Biquad, 3> eq;
    Biquad filter;
    std::array<double, 2> tone {}, highpass {}, lastWet {}, held {};
    std::array<std::array<double, 8>, 2> phaserState {};
    std::array<double, 2> phaserCoefficient {};
    std::array<std::array<double, 4>, 2> reverbDamp {};
    std::array<std::array<double, 4>, 2> reverbSeconds {}, reverbFeedback {};
    double envelope = 0, gain = 1, fast = 0, slow = 0, phase = 0, flutterPhase = 0, clock = 1;
    double alpha = .5, alpha2 = .01, attack = .9, release = .99, drive = 1, output = 1, quantize = 4096;
    double noiseGain = 0;
    std::uint32_t randomState = 0x12345678u;
    unsigned int coefficientClock = 0;

    float random() {
        randomState = randomState * 1664525u + 1013904223u;
        return static_cast<float>(randomState >> 8) / 8388608.f - 1;
    }
    void reset() {
        for (auto& line : delay) line.reset();
        for (auto& channel : comb) for (auto& line : channel) line.reset();
        for (auto& channel : allpass) for (auto& line : channel) line.reset();
        for (auto& band : eq) band.reset();
        filter.reset();
        tone = {}; highpass = {}; lastWet = {}; held = {}; phaserState = {}; reverbDamp = {};
        envelope = fast = slow = phase = flutterPhase = 0; clock = gain = 1;
        coefficientClock = 0;
        randomState = 0x12345678u;
    }
    void prepare(double sampleRate) {
        rate = sampleRate; smooth = 1 - std::exp(-1 / (.02 * rate));
        for (auto& line : delay) line.prepare(static_cast<std::size_t>(rate * 2.1) + 8);
        for (auto& channel : comb) for (auto& line : channel) line.prepare(static_cast<std::size_t>(rate * .075) + 8);
        for (auto& channel : allpass) for (auto& line : channel) line.prepare(static_cast<std::size_t>(rate * .012) + 8);
        initialised = false; reset();
    }
    void configure(const FxSlot& desired) {
        const auto& definition = fxDefinition(desired.type);
        const bool changed = !initialised || slot.type != definition.type || slot.bypass != desired.bypass;
        slot = desired; slot.type = definition.type;
        mixTarget = std::isfinite(slot.mix) ? std::clamp(static_cast<double>(slot.mix), 0.0, 1.0) : 0;
        for (std::size_t i = 0; i < target.size(); ++i) {
            const auto n = std::isfinite(slot.parameters[i]) ? slot.parameters[i]
                : fxParameterNormalized(slot.type, i, definition.parameters[i].defaultValue);
            target[i] = fxParameterValue(slot.type, i, n);
        }
        if (changed) {
            reset(); current = target; mix = mixTarget; initialised = true;
        }
    }
    void coefficients() {
        switch (slot.type) {
            case FxType::Equalizer:
                eq[0].shelf(current[4], current[0], false, rate);
                eq[1].peak(current[3], current[1], rate);
                eq[2].shelf(current[5], current[2], true, rate); break;
            case FxType::Compressor:
                attack = coefficient(current[2], rate); release = coefficient(current[3], rate);
                output = dbGain(current[4]); break;
            case FxType::Saturator:
                drive = dbGain(current[0]); alpha = lowPassAlpha(current[1], rate);
                alpha2 = lowPassAlpha(15, rate); output = dbGain(current[3]); break;
            case FxType::BitCrusher: quantize = std::pow(2.0, std::round(current[0]) - 1); break;
            case FxType::Delay:
                alpha = lowPassAlpha(18000 * std::pow(.025, current[2] / 100), rate);
                alpha2 = lowPassAlpha(current[4], rate); break;
            case FxType::Reverb:
                alpha = lowPassAlpha(16000 * std::pow(.03, current[1] / 100), rate);
                alpha2 = lowPassAlpha(current[5], rate);
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    constexpr std::array<double, 4> times { .0297, .0371, .0411, .0437 };
                    for (std::size_t i = 0; i < times.size(); ++i) {
                        reverbSeconds[ch][i] = (times[i] + ch * .00073) * current[3];
                        reverbFeedback[ch][i] = std::min(.985, std::pow(10.0, -3 * reverbSeconds[ch][i] / current[0]));
                    }
                }
                break;
            case FxType::Filter:
                filter.filter(current[0], current[1], static_cast<int>(std::round(current[2])), rate);
                drive = dbGain(current[3]); break;
            case FxType::Limiter:
                release = coefficient(current[1], rate); drive = dbGain(current[2]); output = dbGain(current[0]); break;
            case FxType::StereoWidth:
                alpha = current[2] > 0 ? lowPassAlpha(current[2], rate) : 0;
                output = dbGain(current[3]); break;
            case FxType::Transient: output = dbGain(current[3]); break;
            case FxType::Phaser:
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto frequency = std::clamp(current[0] * std::pow(2.0, current[1] / 50
                        * std::sin(phase + ch * current[4] * pi / 180)), 20.0, rate * .43);
                    const auto t = std::tan(pi * frequency / rate);
                    phaserCoefficient[ch] = (1 - t) / (1 + t);
                } break;
            case FxType::Tape:
                drive = dbGain(current[0]); alpha = lowPassAlpha(current[4], rate);
                alpha2 = lowPassAlpha(15, rate); output = dbGain(current[5]);
                noiseGain = current[3] > -89.9 ? dbGain(current[3]) : 0; break;
            case FxType::None: case FxType::Chorus: case FxType::Tremolo: case FxType::Flanger: break;
        }
    }
    void process(float& left, float& right) {
        if (slot.type == FxType::None || slot.bypass) return;
        for (std::size_t i = 0; i < current.size(); ++i) current[i] += smooth * (target[i] - current[i]);
        mix += smooth * (mixTarget - mix);
        if ((coefficientClock++ & 31u) == 0) coefficients();
        const std::array<double, 2> input { left, right };
        std::array<double, 2> wet = input;
        switch (slot.type) {
            case FxType::Equalizer:
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    float value = static_cast<float>(input[ch]);
                    for (auto& band : eq) value = band.process(value, ch);
                    wet[ch] = value;
                } break;
            case FxType::Compressor: {
                const auto peak = std::max(std::abs(left), std::abs(right));
                const auto c = peak > envelope ? attack : release;
                envelope = c * envelope + (1 - c) * peak;
                const double over = 20 * std::log10(std::max(envelope, 1e-12)) - current[0];
                double reduction = 0;
                const double knee = current[5];
                if (knee > .001 && over > -knee / 2 && over < knee / 2)
                    reduction = (1 / current[1] - 1) * std::pow(over + knee / 2, 2) / (2 * knee);
                else if (over > 0) reduction = (1 / current[1] - 1) * over;
                const auto level = dbGain(reduction) * output;
                wet[0] *= level; wet[1] *= level;
                break;
            }
            case FxType::Saturator:
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto x = input[ch] * drive + current[2];
                    const auto soft = std::tanh(x) - std::tanh(current[2]);
                    const auto rounded = (std::atan(x) - std::atan(current[2])) * 2 / pi;
                    const auto shaped = soft * (1 - current[4] / 100) + rounded * current[4] / 100;
                    tone[ch] += alpha * (shaped - tone[ch]);
                    highpass[ch] += alpha2 * (tone[ch] - highpass[ch]);
                    wet[ch] = (tone[ch] - highpass[ch]) * output;
                } break;
            case FxType::BitCrusher:
                if (clock >= 1) {
                    clock -= std::floor(clock);
                    for (std::size_t ch = 0; ch < 2; ++ch) held[ch] = std::round(input[ch] * quantize) / quantize;
                }
                clock += std::min(1.0, current[1] / rate * (1 + random() * current[2] / 100));
                wet = held; break;
            case FxType::Chorus: case FxType::Flanger: {
                const auto feedback = current[3] / 100;
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto modulation = std::sin(phase + ch * current[4] * pi / 180);
                    const auto time = std::max(.05, current[0] + modulation * current[1]);
                    wet[ch] = delay[ch].read(time * .001 * rate);
                    delay[ch].write(static_cast<float>(input[ch] + wet[ch] * feedback));
                    if (slot.type == FxType::Flanger) wet[ch] *= current[5];
                }
                phase += 2 * pi * current[2] / rate; break;
            }
            case FxType::Delay: {
                std::array<float, 2> delayed {};
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    delayed[ch] = delay[ch].read(current[0] * .001 * rate * (1 + ch * current[5] * .000125));
                    tone[ch] += alpha * (delayed[ch] - tone[ch]);
                    highpass[ch] += alpha2 * (tone[ch] - highpass[ch]);
                }
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto same = tone[ch] - highpass[ch], other = tone[1 - ch] - highpass[1 - ch];
                    const auto returning = same * (1 - current[3] / 100) + other * current[3] / 100;
                    delay[ch].write(static_cast<float>(input[ch] + returning * current[1] / 100));
                    wet[ch] = delayed[ch];
                } break;
            }
            case FxType::Reverb: {
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto predelay = current[2] > .001 ? delay[ch].read(current[2] * .001 * rate) : static_cast<float>(input[ch]);
                    delay[ch].write(static_cast<float>(input[ch]));
                    double sum = 0;
                    for (std::size_t i = 0; i < reverbSeconds[ch].size(); ++i) {
                        const auto delayed = comb[ch][i].read(reverbSeconds[ch][i] * rate);
                        reverbDamp[ch][i] += alpha * (delayed - reverbDamp[ch][i]);
                        comb[ch][i].write(static_cast<float>(predelay + reverbFeedback[ch][i] * reverbDamp[ch][i]));
                        sum += delayed * .25;
                    }
                    constexpr std::array<double, 2> diffusionTimes { .005, .0017 };
                    for (std::size_t i = 0; i < diffusionTimes.size(); ++i) {
                        const auto delayed = allpass[ch][i].read((diffusionTimes[i] + ch * .00011) * rate * current[3]);
                        const auto diffused = delayed - .5 * sum;
                        allpass[ch][i].write(static_cast<float>(sum + .5 * diffused)); sum = diffused;
                    }
                    highpass[ch] += alpha2 * (sum - highpass[ch]);
                    wet[ch] = sum - highpass[ch];
                }
                const auto mid = (wet[0] + wet[1]) * .5, side = (wet[0] - wet[1]) * .5 * current[4] / 100;
                wet = { mid + side, mid - side }; break;
            }
            case FxType::Filter:
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto value = drive > 1.001 ? std::tanh(input[ch] * drive) : input[ch];
                    wet[ch] = filter.process(static_cast<float>(value), ch);
                } break;
            case FxType::Tremolo:
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto wave = std::sin(phase + ch * current[3] * pi / 180);
                    const auto shape = current[2] / 100;
                    const auto lfo = .5 + .5 * wave * (1 - shape) + (wave >= 0 ? .5 : -.5) * shape;
                    wet[ch] *= 1 - current[1] / 100 + current[1] / 100 * lfo;
                }
                phase += 2 * pi * current[0] / rate; break;
            case FxType::Limiter: {
                const auto peak = std::max(std::abs(input[0] * drive), std::abs(input[1] * drive));
                const auto required = peak > output ? output / peak : 1;
                gain = required < gain ? required : release * gain + (1 - release) * required;
                for (std::size_t ch = 0; ch < 2; ++ch) wet[ch] = std::clamp(input[ch] * drive * gain, -output, output);
                break;
            }
            case FxType::StereoWidth: {
                const auto mid = (input[0] + input[1]) * .5;
                auto side = (input[0] - input[1]) * .5;
                if (current[2] > .01) { highpass[0] += alpha * (side - highpass[0]); side -= highpass[0]; }
                side *= current[0] / 100;
                wet[0] = (mid + side) * std::min(1.0, 1 - current[1] / 100) * output;
                wet[1] = (mid - side) * std::min(1.0, 1 + current[1] / 100) * output; break;
            }
            case FxType::Transient: {
                const auto peak = std::max(std::abs(input[0]), std::abs(input[1]));
                const auto fastC = coefficient(peak > fast ? 1 : 25, rate);
                const auto slowC = coefficient(peak > slow ? 20 : 200, rate);
                fast = fastC * fast + (1 - fastC) * peak;
                slow = slowC * slow + (1 - slowC) * peak;
                const auto change = std::clamp((fast - slow) / std::max(.0001, slow) * current[2], 0.0, 2.0);
                const auto amount = std::clamp(1 + current[0] / 100 * change
                    + current[1] / 100 * (1 - std::min(1.0, change)), .05, 4.0) * output;
                wet[0] *= amount; wet[1] *= amount; break;
            }
            case FxType::Phaser: {
                const auto stages = std::clamp(static_cast<int>(std::round(current[5])), 2, 8);
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    auto value = input[ch] + lastWet[ch] * current[3] / 100;
                    const auto a = phaserCoefficient[ch];
                    for (int stage = 0; stage < stages; ++stage) {
                        auto& z = phaserState[ch][static_cast<std::size_t>(stage)];
                        const auto y = -a * value + z; z = value + a * y; value = y;
                    }
                    wet[ch] = lastWet[ch] = std::clamp(value, -16.0, 16.0);
                }
                phase += 2 * pi * current[2] / rate; break;
            }
            case FxType::Tape:
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    const auto time = .002 + std::sin(phase) * current[1] * .000007
                        + std::sin(flutterPhase + ch * .2) * current[2] * .0000012;
                    auto value = delay[ch].read(time * rate);
                    delay[ch].write(static_cast<float>(input[ch]));
                    value = static_cast<float>(std::tanh(value * drive));
                    if (noiseGain > 0) value += static_cast<float>(random() * noiseGain);
                    tone[ch] += alpha * (value - tone[ch]); highpass[ch] += alpha2 * (tone[ch] - highpass[ch]);
                    wet[ch] = (tone[ch] - highpass[ch]) * output;
                }
                phase += 2 * pi * .55 / rate; flutterPhase += 2 * pi * 6.7 / rate; break;
            case FxType::None: break;
        }
        if (phase >= 2 * pi) phase -= 2 * pi;
        if (flutterPhase >= 2 * pi) flutterPhase -= 2 * pi;
        left = safe(static_cast<float>(input[0] * (1 - mix) + wet[0] * mix));
        right = safe(static_cast<float>(input[1] * (1 - mix) + wet[1] * mix));
    }
};
}

std::span<const FxDefinition> fxDefinitions() noexcept { return definitions; }
const FxDefinition& fxDefinition(FxType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < definitions.size() ? definitions[index] : definitions[0];
}
float fxParameterValue(FxType type, std::size_t index, float normalized) noexcept {
    const auto& d = fxDefinition(type);
    if (index >= static_cast<std::size_t>(d.parameterCount)) return 0;
    const auto& p = d.parameters[index];
    const auto n = std::isfinite(normalized) ? std::clamp(normalized, 0.f, 1.f) : 0.f;
    return p.logarithmic && p.minimum > 0
        ? p.minimum * std::pow(p.maximum / p.minimum, n) : p.minimum + n * (p.maximum - p.minimum);
}
float fxParameterNormalized(FxType type, std::size_t index, float actual) noexcept {
    const auto& d = fxDefinition(type);
    if (index >= static_cast<std::size_t>(d.parameterCount)) return 0;
    const auto& p = d.parameters[index];
    actual = std::isfinite(actual) ? std::clamp(actual, p.minimum, p.maximum) : p.defaultValue;
    return p.logarithmic && p.minimum > 0
        ? std::log(actual / p.minimum) / std::log(p.maximum / p.minimum)
        : (actual - p.minimum) / (p.maximum - p.minimum);
}
FxSlot defaultFxSlot(FxType type) noexcept {
    FxSlot slot; slot.type = fxDefinition(type).type;
    const auto& d = fxDefinition(slot.type);
    for (int i = 0; i < d.parameterCount; ++i)
        slot.parameters[static_cast<std::size_t>(i)] = fxParameterNormalized(slot.type, static_cast<std::size_t>(i), d.parameters[static_cast<std::size_t>(i)].defaultValue);
    switch (slot.type) {
        case FxType::Chorus: case FxType::Flanger: case FxType::Phaser: slot.mix = .5f; break;
        case FxType::Delay: slot.mix = .3f; break;
        case FxType::Reverb: slot.mix = .25f; break;
        default: break;
    }
    return slot;
}

const char* fxPresetCategoryName(FxPresetCategory category) noexcept {
    switch (category) {
        case FxPresetCategory::SoundDesign: return "Sound design";
        case FxPresetCategory::Mixing: return "Mixing";
        case FxPresetCategory::Mastering: return "Mastering";
    }
    return "Sound design";
}

std::span<const FxPreset> fxPresets() noexcept {
    static const auto presets = [] {
        std::array<FxPreset, 26> result;
        auto slot = [](FxType type, std::initializer_list<std::pair<std::size_t, float>> changes, float mix = -1) {
            auto s = defaultFxSlot(type);
            for (const auto& [index, value] : changes) if (index < s.parameters.size()) s.parameters[index] = fxParameterNormalized(type, index, value);
            if (mix >= 0) s.mix = mix;
            return s;
        };
        std::size_t i = 0;
        auto add = [&](const char* name, FxPresetCategory category, std::initializer_list<FxSlot> slots) {
            auto& p = result[i++]; p.name = name; p.category = category;
            std::copy_n(slots.begin(), std::min<std::size_t>(4, slots.size()), p.slots.begin());
        };
        using T = FxType; using C = FxPresetCategory;
        add("Dusty chops", C::SoundDesign, { slot(T::Tape, {{0,8},{1,35},{4,6500}}), slot(T::BitCrusher, {{0,10},{1,18000}}, .3f), slot(T::Equalizer, {{2,-3}}) });
        add("Cassette keys", C::SoundDesign, { slot(T::Tape, {{1,55},{2,30},{4,7000}}), slot(T::Chorus, {{1,3},{2,.4f}}, .3f) });
        add("Wide dream", C::SoundDesign, { slot(T::Chorus, {{1,7},{2,.25f}}), slot(T::Reverb, {{0,4},{2,30}}, .45f), slot(T::StereoWidth, {{0,145},{2,120}}) });
        add("Ping-pong space", C::SoundDesign, { slot(T::Delay, {{0,375},{1,55},{3,100}}, .4f), slot(T::Reverb, {{0,2.5f}}, .2f) });
        add("Telephone", C::SoundDesign, { slot(T::Filter, {{0,1300},{1,.7f},{2,1}}), slot(T::Saturator, {{0,10},{3,-5}}) });
        add("Crunch drums", C::SoundDesign, { slot(T::Saturator, {{0,15},{3,-7}}), slot(T::BitCrusher, {{0,8},{1,16000}}, .4f) });
        add("Pumping motion", C::SoundDesign, { slot(T::Compressor, {{0,-25},{1,8},{2,2},{4,6}}), slot(T::Tremolo, {{0,2},{1,80},{2,25}}) });
        add("Resonant motion", C::SoundDesign, { slot(T::Filter, {{0,1600},{1,4}}), slot(T::Phaser, {{0,850},{1,90},{2,.35f}}, .45f) });
        add("Jet sweep", C::SoundDesign, { slot(T::Flanger, {{1,4},{2,.12f},{3,70}}, .55f), slot(T::Filter, {{0,14000}}) });
        add("Dub echoes", C::SoundDesign, { slot(T::Delay, {{0,500},{1,75},{2,75},{3,100}}, .4f), slot(T::Reverb, {{0,3.5f},{1,75}}, .18f) });
        add("Tremolo keys", C::SoundDesign, { slot(T::Tremolo, {{0,5},{1,75},{3,90}}), slot(T::Reverb, {{0,1.8f}}, .2f) });
        add("Transient snap", C::SoundDesign, { slot(T::Transient, {{0,60},{1,-20}}), slot(T::Limiter, {{0,-1}}) });
        add("Vocal lift", C::Mixing, { slot(T::Equalizer, {{0,-2},{1,1},{2,2},{3,2500}}), slot(T::Compressor, {{0,-20},{1,3},{4,2}}), slot(T::Delay, {{0,110},{1,15}}, .12f), slot(T::Reverb, {{0,1.2f}}, .14f) });
        add("Drum bus", C::Mixing, { slot(T::Compressor, {{0,-16},{1,3},{2,25},{3,90},{4,2}}), slot(T::Transient, {{0,20},{1,-10}}), slot(T::Saturator, {{0,3},{3,-1}}, .2f) });
        add("Bass control", C::Mixing, { slot(T::Equalizer, {{0,-2},{1,-2},{3,300}}), slot(T::Compressor, {{0,-22},{1,4},{2,20},{3,180},{4,3}}) });
        add("Clean channel", C::Mixing, { slot(T::Equalizer, {}), slot(T::Compressor, {{0,-12},{1,2},{4,0}}) });
        add("Spacious pads", C::Mixing, { slot(T::Chorus, {{1,3},{2,.2f}}, .25f), slot(T::StereoWidth, {{0,135},{2,100}}), slot(T::Reverb, {{0,2.8f}}, .25f) });
        add("Tight room", C::Mixing, { slot(T::Reverb, {{0,.45f},{2,2},{3,.7f}}, .2f) });
        add("Parallel punch", C::Mixing, { slot(T::Compressor, {{0,-30},{1,10},{2,3},{3,70},{4,9}}, .5f) });
        add("Mono bass width", C::Mixing, { slot(T::StereoWidth, {{0,150},{2,150}}), slot(T::Equalizer, {{0,-1}}) });
        add("Gentle glue", C::Mastering, { slot(T::Compressor, {{0,-12},{1,2},{2,30},{3,200},{4,1}}), slot(T::Equalizer, {{0,.5f},{2,.5f}}), slot(T::Limiter, {{0,-.8f}}) });
        add("Warm tape master", C::Mastering, { slot(T::Tape, {{0,2},{1,0},{2,0},{4,16000},{5,0}}), slot(T::Limiter, {{0,-.5f}}) });
        add("Transparent ceiling", C::Mastering, { slot(T::Limiter, {{0,-.3f},{1,100}}) });
        add("Wide and safe", C::Mastering, { slot(T::StereoWidth, {{0,115},{2,120}}), slot(T::Limiter, {{0,-.8f}}) });
        add("Loud preview", C::Mastering, { slot(T::Compressor, {{0,-20},{1,4},{2,15},{4,5}}), slot(T::Saturator, {{0,2},{3,0}}, .1f), slot(T::Limiter, {{0,-.8f},{1,100}}) });
        add("Gentle air", C::Mastering, { slot(T::Equalizer, {{0,.5f},{2,1},{5,9000}}), slot(T::Compressor, {{0,-10},{1,1.5f},{4,.5f}}), slot(T::Limiter, {{0,-.5f}}) });
        return result;
    }();
    return presets;
}

struct FxRack::Impl {
    std::array<EffectState, 4> effects;
    std::array<FxSlot, 4> desired;
    bool prepared = false;
};
FxRack::FxRack() : impl(std::make_unique<Impl>()) {}
FxRack::~FxRack() = default;
FxRack::FxRack(FxRack&&) noexcept = default;
FxRack& FxRack::operator=(FxRack&&) noexcept = default;
void FxRack::prepare(double sampleRate, std::size_t maximumBlockSize) {
    (void) maximumBlockSize;
    if (!impl) return;
    const auto rate = std::isfinite(sampleRate) && sampleRate > 0 ? std::clamp(sampleRate, 8000.0, 384000.0) : 44100.0;
    for (std::size_t i = 0; i < impl->effects.size(); ++i) {
        impl->effects[i].prepare(rate); impl->effects[i].configure(impl->desired[i]);
    }
    impl->prepared = true;
}
void FxRack::setSlots(const std::array<FxSlot, 4>& slots) noexcept {
    if (!impl) return;
    impl->desired = slots;
    if (impl->prepared) for (std::size_t i = 0; i < slots.size(); ++i) impl->effects[i].configure(slots[i]);
}
void FxRack::processStereo(float* left, float* right, std::size_t samples) noexcept {
    if (!impl || !left || !right) return;
    for (std::size_t i = 0; i < samples; ++i) {
        float l = safe(left[i]), r = safe(right[i]);
        if (impl->prepared) for (auto& effect : impl->effects) effect.process(l, r);
        left[i] = l; right[i] = r;
    }
}
void FxRack::reset() noexcept {
    if (!impl) return;
    for (auto& effect : impl->effects) effect.reset();
}

} // namespace samplify
