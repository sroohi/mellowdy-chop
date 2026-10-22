#include "DSP.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <string_view>

namespace samplify {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t maxWaveformBins = 16384;
constexpr double analysisRate = 11025.0;
constexpr double analysisSeconds = 120.0;

void fft(std::vector<std::complex<double>>& values) {
    const auto n = values.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t width = 2; width <= n; width <<= 1) {
        const auto step = std::polar(1.0, -2 * pi / static_cast<double>(width));
        for (std::size_t begin = 0; begin < n; begin += width) {
            std::complex<double> rotation(1, 0);
            for (std::size_t i = 0; i < width / 2; ++i) {
                const auto a = values[begin + i];
                const auto b = values[begin + i + width / 2] * rotation;
                values[begin + i] = a + b;
                values[begin + i + width / 2] = a - b;
                rotation *= step;
            }
        }
    }
}

std::vector<double> analysisSignal(const AudioData& audio, double& rate) {
    // Decimation uses a one-pole antialias prefilter. This analysis is approximate;
    // it is intentionally bounded to the first two minutes of very long files.
    rate = std::min(analysisRate, audio.sampleRate);
    const double step = audio.sampleRate / rate;
    const auto sourceFrames = std::min(audio.frames(),
        static_cast<std::size_t>(std::min(audio.sampleRate * analysisSeconds,
                                          static_cast<double>(audio.frames()))));
    const auto count = static_cast<std::size_t>(sourceFrames / step);
    std::vector<double> result;
    result.reserve(count);
    double filtered = 0;
    const auto alpha = 1.0 - std::exp(-2 * pi * (rate * 0.4) / audio.sampleRate);
    double next = 0;
    for (std::size_t i = 0; i < sourceFrames && result.size() < count; ++i) {
        // A reference channel avoids cancellation in phase-inverted stereo files.
        filtered += alpha * (audio.channels.front()[i] - filtered);
        if (static_cast<double>(i) >= next) {
            result.push_back(filtered);
            next += step;
        }
    }
    return result;
}

std::vector<WaveformBin> makeWaveform(const AudioData& audio, std::size_t requested) {
    const auto count = std::min({requested, maxWaveformBins, audio.frames()});
    std::vector<WaveformBin> bins(count);
    if (!count) return bins;
    const auto lowAlpha = 1.0 - std::exp(-2 * pi * 250.0 / audio.sampleRate);
    const auto midAlpha = 1.0 - std::exp(-2 * pi * 2500.0 / audio.sampleRate);
    std::vector<double> low(audio.channels.size()), broad(audio.channels.size());
    std::size_t begin = 0;
    for (std::size_t bin = 0; bin < count; ++bin) {
        const auto end = (bin + 1) * audio.frames() / count;
        double lo = 0, mi = 0, hi = 0;
        for (auto frame = begin; frame < end; ++frame) {
            for (std::size_t c = 0; c < audio.channels.size(); ++c) {
                const auto sample = audio.channels[c][frame];
                bins[bin].peak = std::max(bins[bin].peak, std::abs(sample));
                low[c] += lowAlpha * (sample - low[c]);
                broad[c] += midAlpha * (sample - broad[c]);
                const auto middle = broad[c] - low[c];
                const auto high = sample - broad[c];
                lo += low[c] * low[c];
                mi += middle * middle;
                hi += high * high;
            }
        }
        const double divisor = static_cast<double>((end - begin) * audio.channels.size());
        bins[bin].low = static_cast<float>(std::sqrt(lo / divisor));
        bins[bin].mid = static_cast<float>(std::sqrt(mi / divisor));
        bins[bin].high = static_cast<float>(std::sqrt(hi / divisor));
        begin = end;
    }
    return bins;
}

std::string estimateKey(const std::vector<double>& samples, double rate) {
    constexpr std::size_t window = 4096, hop = 2048;
    if (samples.size() < 512) return "Unknown";
    std::array<double, 12> chroma{};
    std::vector<std::complex<double>> spectrum(window);
    double energy = 0;
    for (std::size_t start = 0; start < samples.size(); start += hop) {
        std::fill(spectrum.begin(), spectrum.end(), 0);
        for (std::size_t i = 0; i < window && start + i < samples.size(); ++i) {
            spectrum[i] = samples[start + i] * (0.5 - 0.5 * std::cos(2 * pi * i / (window - 1)));
        }
        fft(spectrum);
        for (std::size_t bin = 1; bin < window / 2; ++bin) {
            const double frequency = bin * rate / window;
            if (frequency < 65 || frequency > std::min(4000.0, rate * 0.45)) continue;
            const double midi = 69 + 12 * std::log2(frequency / 440.0);
            const auto note = static_cast<int>(std::lround(midi));
            // Concentrate each bin near a tuned semitone. Broad noise should not
            // contribute as strongly as pitched energy.
            const double tuning = std::max(0.0, 1.0 - 2 * std::abs(midi - note));
            const double magnitude = std::abs(spectrum[bin]) * tuning;
            chroma[static_cast<std::size_t>((note % 12 + 12) % 12)] += magnitude;
            energy += magnitude;
        }
    }
    if (energy < 1e-6) return "Unknown";
    constexpr std::array<double, 12> major {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    constexpr std::array<double, 12> minor {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    constexpr std::array<std::string_view, 12> names {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto mean = std::accumulate(chroma.begin(), chroma.end(), 0.0) / 12;
    double best = -2;
    int tonic = 0;
    bool isMinor = false;
    for (int mode = 0; mode < 2; ++mode) {
        const auto& profile = mode ? minor : major;
        const double profileMean = std::accumulate(profile.begin(), profile.end(), 0.0) / 12;
        for (int root = 0; root < 12; ++root) {
            double dot = 0, xNorm = 0, yNorm = 0;
            for (int note = 0; note < 12; ++note) {
                const double x = chroma[static_cast<std::size_t>(note)] - mean;
                const double y = profile[static_cast<std::size_t>((note - root + 12) % 12)] - profileMean;
                dot += x * y;
                xNorm += x * x;
                yNorm += y * y;
            }
            const auto score = dot / std::sqrt(std::max(1e-30, xNorm * yNorm));
            if (score > best) { best = score; tonic = root; isMinor = mode != 0; }
        }
    }
    // Profile matching is a musical-key estimate, not a guarantee, especially
    // for drums, atonal samples, isolated notes, and modulation.
    if (best < 0.35) return "Unknown";
    return std::string(names[static_cast<std::size_t>(tonic)]) + (isMinor ? " minor" : " major");
}

void estimateRhythm(const std::vector<double>& samples, double rate, const AudioData& audio, Analysis& analysis) {
    constexpr std::size_t window = 1024, hop = 256;
    if (samples.size() < window) return;
    const auto count = 1 + (samples.size() - window) / hop;
    std::vector<double> flux(count), previous(window / 2);
    std::vector<std::complex<double>> spectrum(window);
    for (std::size_t frame = 0; frame < count; ++frame) {
        for (std::size_t i = 0; i < window; ++i) {
            spectrum[i] = samples[frame * hop + i] * (0.5 - 0.5 * std::cos(2 * pi * i / (window - 1)));
        }
        fft(spectrum);
        for (std::size_t bin = 1; bin < window / 2; ++bin) {
            const auto magnitude = std::log1p(10 * std::abs(spectrum[bin]));
            flux[frame] += std::max(0.0, magnitude - previous[bin]);
            previous[bin] = magnitude;
        }
    }
    const auto maximum = *std::max_element(flux.begin(), flux.end());
    if (maximum < 1e-5) return;
    for (auto& value : flux) value /= maximum;
    for (std::size_t i = 1; i + 1 < count; ++i) {
        const auto begin = i > 8 ? i - 8 : 0;
        const auto end = std::min(count, i + 9);
        const double mean = std::accumulate(flux.begin() + static_cast<std::ptrdiff_t>(begin),
                                            flux.begin() + static_cast<std::ptrdiff_t>(end), 0.0) / (end - begin);
        if (flux[i] < 0.12 || flux[i] < mean * 1.5 || flux[i] <= flux[i - 1] || flux[i] < flux[i + 1]) continue;
        const auto sourceFrame = static_cast<std::size_t>((i * hop + window / 2) * audio.sampleRate / rate);
        if (sourceFrame >= audio.frames()) continue;
        if (analysis.onsets.empty() || sourceFrame - analysis.onsets.back() > audio.sampleRate * 0.07) {
            analysis.onsets.push_back(sourceFrame);
        }
    }
    // Positive spectral flux autocorrelation with a broad musical tempo prior.
    // Meter ambiguity (half/double tempo) remains possible with sparse material.
    if (analysis.onsets.size() < 3 || count < 40) return;
    double bestScore = 0, bestBpm = 120;
    for (double bpm = 60; bpm <= 200; bpm += 0.25) {
        const double lag = 60 * rate / (hop * bpm);
        double dot = 0, left = 0, right = 0;
        for (std::size_t i = static_cast<std::size_t>(std::ceil(lag)); i < count; ++i) {
            const double position = i - lag;
            const auto index = static_cast<std::size_t>(position);
            const auto part = position - index;
            const double earlier = flux[index] * (1 - part) + flux[std::min(index + 1, count - 1)] * part;
            dot += flux[i] * earlier;
            left += flux[i] * flux[i];
            right += earlier * earlier;
        }
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 120) / 0.65, 2));
        const double score = dot / std::sqrt(std::max(1e-20, left * right)) * prior;
        if (score > bestScore) { bestScore = score; bestBpm = bpm; }
    }
    if (bestScore < 0.15) return;
    // Refine a regular onset train across many beats to remove hop-size bias.
    const double expected = audio.sampleRate * 60 / bestBpm;
    double intervalSum = 0;
    std::size_t intervals = 0;
    for (std::size_t i = 1; i < analysis.onsets.size(); ++i) {
        const auto delta = static_cast<double>(analysis.onsets[i] - analysis.onsets[i - 1]);
        if (std::abs(delta - expected) < expected * 0.2) { intervalSum += delta; ++intervals; }
    }
    if (intervals >= 2) bestBpm = 60 * audio.sampleRate * intervals / intervalSum;
    analysis.bpm = std::clamp(bestBpm, 60.0, 200.0);
    const double beatFrames = audio.sampleRate * 60 / analysis.bpm;
    const auto first = analysis.onsets.front();
    const double phase = std::fmod(static_cast<double>(first), beatFrames);
    for (double beat = phase; beat < audio.frames() && analysis.beats.size() < 1000000; beat += beatFrames) {
        const auto frame = static_cast<std::size_t>(std::llround(beat));
        if (frame >= audio.frames()) break;
        analysis.beats.push_back(frame);
    }
}

std::vector<std::vector<float>> stretch(const std::vector<std::vector<float>>& input,
                                      std::size_t outputFrames, double sampleRate) {
    const auto frames = input.front().size();
    if (frames == outputFrames) return input;
    std::vector<std::vector<float>> output(input.size(), std::vector<float>(outputFrames));
    if (frames < 128 || outputFrames < 64) {
        // There is no meaningful periodicity to preserve in sub-millisecond
        // fragments. Interpolation is the explicit short-fragment fallback.
        for (std::size_t c = 0; c < input.size(); ++c) {
            for (std::size_t i = 0; i < outputFrames; ++i) {
                const double position = outputFrames > 1 ? static_cast<double>(i) * (frames - 1) / (outputFrames - 1) : 0;
                const auto left = static_cast<std::size_t>(position);
                const auto part = position - left;
                output[c][i] = static_cast<float>(input[c][left] * (1 - part) + input[c][std::min(left + 1, frames - 1)] * part);
            }
        }
        return output;
    }
    // WSOLA selects a shared alignment for all channels, maintaining stereo
    // coherence. It works well for modest ratios; extreme ratios can smear
    // transients and it is not a phase-vocoder/formant preservation algorithm.
    const auto desiredWindow = static_cast<std::size_t>(std::clamp(sampleRate * 0.04, 128.0, 4096.0));
    const auto window = std::min(frames, desiredWindow);
    const auto hop = std::max<std::size_t>(1, window / 4);
    const auto search = window / 4;
    const auto lastStart = frames - window;
    const double ratio = static_cast<double>(outputFrames) / frames;
    std::vector<float> weights(outputFrames);
    std::vector<double> taper(window);
    for (std::size_t i = 0; i < window; ++i) taper[i] = 0.5 - 0.5 * std::cos(2 * pi * (i + 0.5) / window);
    for (std::size_t position = 0; position < outputFrames; position += hop) {
        const auto nominal = std::min(lastStart, static_cast<std::size_t>(position / ratio));
        auto selected = nominal;
        if (position > 0 && lastStart > 0) {
            const auto begin = nominal > search ? nominal - search : 0;
            const auto end = std::min(lastStart, nominal + search);
            double best = -std::numeric_limits<double>::infinity();
            const auto overlap = std::min(window - hop, outputFrames - position);
            auto score = [&](std::size_t candidate) {
                double dot = 0, a = 0, b = 0;
                for (std::size_t k = 0; k < overlap; k += 8) {
                    if (weights[position + k] < 1e-5f) continue;
                    const double x = output.front()[position + k];
                    const double y = input.front()[candidate + k];
                    dot += x * y;
                    a += x * x;
                    b += y * y;
                }
                if (a * b < 1e-20) return -1.0 - std::abs(static_cast<double>(candidate) - nominal) / (search + 1);
                return dot / std::sqrt(a * b) - 0.01 * std::abs(static_cast<double>(candidate) - nominal) / (search + 1);
            };
            for (auto candidate = begin; candidate <= end; candidate += 8) {
                const auto value = score(candidate);
                if (value > best) { best = value; selected = candidate; }
                if (end - candidate < 8) break;
            }
            const auto coarse = selected;
            for (auto candidate = coarse > 7 ? std::max(begin, coarse - 7) : begin;
                 candidate <= std::min(end, coarse + 7); ++candidate) {
                const auto value = score(candidate);
                if (value > best) { best = value; selected = candidate; }
            }
        }
        const auto available = std::min(window, outputFrames - position);
        for (std::size_t i = 0; i < available; ++i) {
            const auto weight = static_cast<float>(taper[i]);
            const auto previousWeight = weights[position + i];
            weights[position + i] += weight;
            for (std::size_t c = 0; c < input.size(); ++c) {
                // Normalize while accumulating, using double arithmetic. Even
                // finite float samples near their representable limit cannot
                // overflow a temporary overlap sum.
                output[c][position + i] = static_cast<float>(
                    (static_cast<double>(output[c][position + i]) * previousWeight
                        + static_cast<double>(input[c][selected + i]) * weight) / weights[position + i]);
            }
        }
    }
    return output;
}

std::optional<int> keyRoot(std::string name) {
    const auto begin = name.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) return std::nullopt;
    name.erase(0, begin);
    int note = 0;
    switch (name.front()) {
        case 'C': case 'c': note = 0; break;
        case 'D': case 'd': note = 2; break;
        case 'E': case 'e': note = 4; break;
        case 'F': case 'f': note = 5; break;
        case 'G': case 'g': note = 7; break;
        case 'A': case 'a': note = 9; break;
        case 'B': case 'b': note = 11; break;
        default: return std::nullopt;
    }
    std::size_t position = 1;
    if (name.compare(position, 3, "♯") == 0) { ++note; position += 3; }
    else if (name.compare(position, 3, "♭") == 0) { --note; position += 3; }
    else if (position < name.size() && name[position] == '#') { ++note; ++position; }
    else if (position < name.size() && name[position] == 'b') { --note; ++position; }
    if (position < name.size() && name[position] != ' ' && name[position] != '\t'
        && name[position] != 'm' && name[position] != 'M') return std::nullopt;
    return (note + 12) % 12;
}
} // namespace

Analysis analyse(const AudioData& audio, std::size_t waveformBins) {
    Analysis result;
    if (!audio.valid()) return result;
    result.waveform = makeWaveform(audio, waveformBins);
    double rate = 0;
    const auto samples = analysisSignal(audio, rate);
    result.key = estimateKey(samples, rate);
    estimateRhythm(samples, rate, audio, result);
    return result;
}

std::vector<Pad> makeSlices(const AudioData& audio, const Analysis& analysis,
                          ChopMode mode, int count, double beatsPerSlice, std::uint32_t seed) {
    if (!audio.valid() || count <= 0) return {};
    count = std::min(count, 32);
    std::vector<std::size_t> boundaries {0};
    if (mode == ChopMode::Grid) {
        if (!std::isfinite(beatsPerSlice) || beatsPerSlice <= 0) return {};
        const double bpm = std::isfinite(analysis.bpm) && analysis.bpm > 0 ? analysis.bpm : 120;
        const double length = std::max(1.0, audio.sampleRate * 60 / bpm * beatsPerSlice);
        for (int i = 1; i <= count; ++i) {
            const auto edge = static_cast<std::size_t>(std::min(static_cast<double>(audio.frames()), i * length));
            if (edge > boundaries.back()) boundaries.push_back(edge);
            if (edge == audio.frames()) break;
        }
    } else if (mode == ChopMode::Transients) {
        for (auto onset : analysis.onsets) if (onset > 0 && onset < audio.frames()) boundaries.push_back(onset);
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        boundaries.push_back(audio.frames());
        if (boundaries.size() > static_cast<std::size_t>(count + 1)) boundaries.resize(static_cast<std::size_t>(count + 1));
    } else {
        std::mt19937 random(seed);
        std::vector<std::size_t> beats;
        for (const auto frame : analysis.beats) {
            if (frame > 0 && frame < audio.frames()) beats.push_back(frame);
        }
        std::sort(beats.begin(), beats.end());
        beats.erase(std::unique(beats.begin(), beats.end()), beats.end());
        // Prefer the detected grid, including its phase and variable spacing.
        // With no detected interior beats, derive a fixed BPM grid anchored at
        // zero (120 BPM for invalid tempo). Derive positions lazily so an
        // implausibly high tempo cannot allocate an enormous candidate vector.
        const double bpm = std::isfinite(analysis.bpm) && analysis.bpm > 0 ? analysis.bpm : 120;
        const double step = std::max(1.0, audio.sampleRate * 60 / bpm);
        const auto candidates = beats.empty()
            ? static_cast<std::size_t>((audio.frames() - 1) / step) : beats.size();
        if (candidates > 0) {
            const auto cuts = std::min(static_cast<std::size_t>(count - 1), candidates);
            for (std::size_t i = 0; i < cuts; ++i) {
                const auto lower = i * candidates / cuts;
                const auto upper = (i + 1) * candidates / cuts;
                const auto chosen = lower + random() % (upper - lower);
                boundaries.push_back(beats.empty()
                    ? static_cast<std::size_t>(std::llround((chosen + 1) * step)) : beats[chosen]);
            }
            // An insufficient grid yields fewer slices, keeping every internal
            // cut musical instead of filling remaining pads with off-grid cuts.
        } else {
            // Sub-beat and one-frame clips have no interior grid positions.
            // Retain a bounded stratified sample fallback for these tiny clips.
            const auto slices = std::min(static_cast<std::size_t>(count), audio.frames());
            for (std::size_t i = 1; i < slices; ++i) {
                const auto lower = i * audio.frames() / slices;
                const auto upper = (i + 1) * audio.frames() / slices - 1;
                boundaries.push_back(lower + random() % (upper - lower + 1));
            }
        }
        boundaries.push_back(audio.frames());
    }
    std::vector<Pad> result;
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
        if (boundaries[i] <= boundaries[i - 1]) continue;
        Pad pad;
        pad.start = boundaries[i - 1];
        pad.end = boundaries[i];
        result.push_back(pad);
    }
    return result;
}

AudioData renderPad(const AudioData& audio, const Pad& pad, double speedRatio) {
    AudioData result;
    result.sampleRate = audio.sampleRate;
    if (!audio.valid() || pad.empty() || pad.start >= audio.frames()
        || !std::isfinite(speedRatio) || speedRatio <= 0 || !std::isfinite(pad.timeRatio) || pad.timeRatio <= 0
        || !std::isfinite(pad.pitchSemitones) || !std::isfinite(pad.detuneCents)
        || !std::isfinite(pad.levelDb) || !std::isfinite(pad.filter)
        || !std::isfinite(pad.attackMs) || !std::isfinite(pad.releaseMs)) return result;
    const auto end = std::min(pad.end, audio.frames());
    const auto frames = end - pad.start;
    const double durationRatio = std::clamp(static_cast<double>(pad.timeRatio) / speedRatio, 0.125, 8.0);
    const double pitch = std::pow(2.0, std::clamp(static_cast<double>(pad.pitchSemitones)
                                                + pad.detuneCents / 100.0, -24.0, 24.0) / 12);
    const double desired = frames * durationRatio;
    // Avoid excessive allocations from malformed settings or very long pads.
    // Limits are explicit failures, never silent truncation of exported audio.
    const double sampleBudget = 64.0 * 1024 * 1024 / audio.channels.size();
    if (desired <= 0 || desired > sampleBudget || desired * pitch > sampleBudget) return result;
    const auto outputFrames = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(desired)));
    const auto stretchedFrames = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(outputFrames * pitch)));
    std::vector<std::vector<float>> selected;
    selected.reserve(audio.channels.size());
    for (const auto& channel : audio.channels) {
        selected.emplace_back(channel.begin() + static_cast<std::ptrdiff_t>(pad.start),
                              channel.begin() + static_cast<std::ptrdiff_t>(end));
        if (pad.reverse) std::reverse(selected.back().begin(), selected.back().end());
    }
    auto expanded = stretch(selected, stretchedFrames, audio.sampleRate);
    result.channels.assign(audio.channels.size(), std::vector<float>(outputFrames));
    for (std::size_t c = 0; c < result.channels.size(); ++c) {
        if (stretchedFrames == outputFrames) result.channels[c] = std::move(expanded[c]);
        else {
            // Windowed-sinc resampling changes pitch after WSOLA changes time.
            // A lower cutoff on downsampling prevents simple aliasing.
            const double step = static_cast<double>(stretchedFrames) / outputFrames;
            const double cutoff = std::min(1.0, 1.0 / step);
            constexpr int radius = 12;
            for (std::size_t i = 0; i < outputFrames; ++i) {
                const double position = i * step;
                const auto center = static_cast<std::ptrdiff_t>(position);
                double sample = 0, weight = 0;
                for (int tap = -radius + 1; tap <= radius; ++tap) {
                    const auto index = center + tap;
                    if (index < 0 || index >= static_cast<std::ptrdiff_t>(stretchedFrames)) continue;
                    const double distance = position - index;
                    const double angle = pi * cutoff * distance;
                    const double sinc = std::abs(angle) < 1e-10 ? 1.0 : std::sin(angle) / angle;
                    const double window = 0.5 + 0.5 * std::cos(pi * distance / radius);
                    const double kernel = cutoff * sinc * window;
                    sample += expanded[c][static_cast<std::size_t>(index)] * kernel;
                    weight += kernel;
                }
                const auto limit = static_cast<double>(std::numeric_limits<float>::max());
                result.channels[c][i] = weight != 0 ? static_cast<float>(std::clamp(sample / weight, -limit, limit)) : 0;
            }
        }
        const double gain = std::pow(10.0, std::clamp(static_cast<double>(pad.levelDb), -96.0, 24.0) / 20);
        const double filter = std::clamp(static_cast<double>(pad.filter), 0.0, 1.0);
        const double cutoff = filter < 0.5 ? 80 * std::pow(200.0, filter * 2)
                                            : 20 * std::pow(500.0, (filter - 0.5) * 2);
        const double alpha = 1 - std::exp(-2 * pi * std::min(cutoff, audio.sampleRate * 0.45) / audio.sampleRate);
        const double attack = std::clamp(static_cast<double>(pad.attackMs), 0.0, 5000.0) * audio.sampleRate / 1000;
        const double release = std::clamp(static_cast<double>(pad.releaseMs), 0.0, 5000.0) * audio.sampleRate / 1000;
        double state = 0;
        for (std::size_t i = 0; i < outputFrames; ++i) {
            double sample = result.channels[c][i];
            state += alpha * (sample - state);
            if (filter < 0.4999) sample = state;
            else if (filter > 0.5001) sample -= state;
            double envelope = 1;
            if (attack > 0) envelope = std::min(envelope, i / attack);
            if (release > 0) envelope = std::min(envelope, (outputFrames - 1 - i) / release);
            const auto value = sample * gain * std::max(0.0, envelope);
            const auto limit = static_cast<double>(std::numeric_limits<float>::max());
            result.channels[c][i] = static_cast<float>(std::clamp(value, -limit, limit));
        }
    }
    // stemMute cannot operate on mixed AudioData; source separation requires a
    // backend that supplies individual stem signals before this renderer.
    return result;
}

int keySemitoneOffset(const std::string& source, const std::string& target) {
    const auto from = keyRoot(source), to = keyRoot(target);
    return from && to ? (*to - *from + 18) % 12 - 6 : 0;
}

} // namespace samplify
