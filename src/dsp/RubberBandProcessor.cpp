#include "RubberBandProcessor.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace samplify {
namespace {
constexpr std::size_t blockFrames = 2048;
constexpr std::size_t maximumSamples = 64u * 1024u * 1024u;

double validateAndFindPeak(const AudioData& audio) {
    if (!std::isfinite(audio.sampleRate) || audio.sampleRate < 8000
        || audio.sampleRate > 384000 || audio.channels.empty()
        || audio.channels.size() > 32 || audio.frames() == 0)
        throw std::invalid_argument("Pitch/time processing needs nonempty audio at 8-384 kHz.");
    if (audio.frames() > maximumSamples / audio.channels.size())
        throw std::length_error("Audio exceeds the pitch/time processing memory budget.");
    double peak = 0;
    for (const auto& channel : audio.channels) {
        if (channel.size() != audio.frames())
            throw std::invalid_argument("Audio channels have different lengths.");
        for (const auto sample : channel) {
            if (!std::isfinite(sample))
                throw std::invalid_argument("Audio contains non-finite samples.");
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
        }
    }
    return peak;
}
} // namespace

AudioData renderDurationPitch(const AudioData& input, double durationRatio,
                             double semitones) {
    if (!std::isfinite(durationRatio) || durationRatio < 1.0 / 64.0 || durationRatio > 64)
        throw std::invalid_argument("Duration ratio must be between 1/64 and 64.");
    if (!std::isfinite(semitones) || semitones < -60 || semitones > 60)
        throw std::invalid_argument("Pitch must be between -60 and +60 semitones.");
    const auto peak = validateAndFindPeak(input);
    const double desiredFrames = static_cast<double>(input.frames()) * durationRatio;
    if (desiredFrames > static_cast<double>(maximumSamples / input.channels.size()))
        throw std::length_error("Rendered audio exceeds the pitch/time processing memory budget.");
    const auto outputFrames = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(desiredFrames)));
    if (outputFrames == input.frames() && std::abs(semitones) < 1e-12)
        return input;

    AudioData output;
    output.sampleRate = input.sampleRate;
    output.channels.assign(input.channels.size(), std::vector<float>(outputFrames, 0));
    if (peak == 0)
        return output;
    // A single sample contains no pitch information. Holding that sample is the
    // only useful duration transform and avoids windowed processing of no signal.
    if (input.frames() == 1) {
        for (std::size_t c = 0; c < output.channels.size(); ++c)
            std::fill(output.channels[c].begin(), output.channels[c].end(), input.channels[c][0]);
        return output;
    }

    // Keep the transform in a numerically safe PCM range. Float WAV files may
    // legitimately exceed full scale; restore their headroom after processing.
    AudioData scaled;
    const AudioData* processInput = &input;
    const double normalization = peak > 1 ? peak : 1;
    if (normalization > 1) {
        scaled = input;
        for (auto& channel : scaled.channels)
            for (auto& sample : channel)
                sample = static_cast<float>(static_cast<double>(sample) / normalization);
        processInput = &scaled;
    }

    using Stretcher = RubberBand::RubberBandStretcher;
    const auto options = Stretcher::OptionProcessOffline
        | Stretcher::OptionEngineFiner | Stretcher::OptionChannelsTogether
        | Stretcher::OptionThreadingNever;
    const double effectiveRatio = static_cast<double>(outputFrames) / static_cast<double>(input.frames());
    Stretcher stretcher(static_cast<std::size_t>(std::llround(input.sampleRate)),
                        input.channels.size(), options, effectiveRatio,
                        std::exp2(semitones / 12));
    if (stretcher.getEngineVersion() != 3)
        throw std::runtime_error("The required Rubber Band R3 engine is unavailable.");
    stretcher.setExpectedInputDuration(input.frames());
    stretcher.setMaxProcessSize(blockFrames);

    std::vector<const float*> inPointers(input.channels.size());
    auto pointAt = [&](std::size_t frame) {
        for (std::size_t channel = 0; channel < inPointers.size(); ++channel)
            inPointers[channel] = processInput->channels[channel].data() + frame;
    };
    // Offline study/process perform their own start padding and latency trim.
    // Do not apply the real-time mode's getStartDelay() compensation a second time.
    for (std::size_t frame = 0; frame < input.frames(); frame += blockFrames) {
        const auto count = std::min(blockFrames, input.frames() - frame);
        pointAt(frame);
        stretcher.study(inPointers.data(), count, frame + count == input.frames());
    }

    std::vector<std::vector<float>> scratch(input.channels.size(), std::vector<float>(blockFrames));
    std::vector<float*> outPointers(input.channels.size());
    for (std::size_t channel = 0; channel < outPointers.size(); ++channel)
        outPointers[channel] = scratch[channel].data();
    std::size_t produced = 0;
    auto retrieve = [&] {
        while (stretcher.available() > 0) {
            const auto available = static_cast<std::size_t>(stretcher.available());
            const auto count = stretcher.retrieve(outPointers.data(), std::min(blockFrames, available));
            if (count == 0)
                throw std::runtime_error("Pitch/time processor failed to return available audio.");
            const auto toCopy = produced < outputFrames ? std::min(count, outputFrames - produced) : 0;
            for (std::size_t channel = 0; channel < output.channels.size(); ++channel)
                std::copy_n(scratch[channel].data(), toCopy, output.channels[channel].data() + std::min(produced, outputFrames));
            produced += count;
        }
    };
    for (std::size_t frame = 0; frame < input.frames(); frame += blockFrames) {
        const auto count = std::min(blockFrames, input.frames() - frame);
        pointAt(frame);
        stretcher.process(inPointers.data(), count, frame + count == input.frames());
        retrieve();
    }
    retrieve();
    // The offline API promises exact duration. Permit only the rounding of one
    // final frame; a substantial shortfall must never turn into silent success.
    if (produced + 1 < outputFrames || produced > outputFrames + 1)
        throw std::runtime_error("Pitch/time processor returned an unexpected duration.");
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    for (auto& channel : output.channels) {
        for (auto& sample : channel) {
            const double restored = static_cast<double>(sample) * normalization;
            if (!std::isfinite(restored))
                throw std::runtime_error("Pitch/time processor returned invalid audio.");
            sample = static_cast<float>(std::clamp(restored, -maximumFloat, maximumFloat));
        }
    }
    return output;
}

} // namespace samplify
