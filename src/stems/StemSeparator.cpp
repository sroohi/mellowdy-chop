#include "StemSeparator.h"
#include "SHA256.h"
#include <model.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <limits>
#include <mutex>
#include <numbers>

namespace samplify {
namespace {
constexpr double modelRate = 44100;
constexpr std::size_t maxInputFrames = 32 * 1024 * 1024;
constexpr const char* modelHash = "72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064";
// Upstream uses process-global rand() for time-equivariant shifts. Serialize
// inference across instances rather than racing that state or oversubscribing
// the substantial model workspaces. This is never acquired by an audio thread.
std::mutex inferenceMutex;

void checkCancelled(const std::atomic<bool>& cancelled) {
    if (cancelled.load(std::memory_order_relaxed)) throw SeparationCancelled();
}

std::vector<float> resample(const std::vector<float>& input, double fromRate,
                            double toRate, std::size_t outputFrames,
                            const std::atomic<bool>& cancelled) {
    if (input.size() == outputFrames && std::abs(fromRate - toRate) < .001) return input;
    std::vector<float> result(outputFrames);
    const double step = fromRate / toRate;
    const double cutoff = std::min(1.0, 1.0 / step);
    constexpr int radius = 16;
    for (std::size_t i = 0; i < outputFrames; ++i) {
        if ((i & 4095u) == 0) checkCancelled(cancelled);
        const double position = i * step;
        const auto centre = static_cast<std::ptrdiff_t>(position);
        double value = 0, weight = 0;
        for (int tap = -radius + 1; tap <= radius; ++tap) {
            const auto frame = centre + tap;
            if (frame < 0 || frame >= static_cast<std::ptrdiff_t>(input.size())) continue;
            const double distance = position - static_cast<double>(frame);
            const double angle = std::numbers::pi * cutoff * distance;
            const double sinc = std::abs(angle) < 1e-10 ? 1.0 : std::sin(angle) / angle;
            const double taper = .5 + .5 * std::cos(std::numbers::pi * distance / radius);
            const double w = cutoff * sinc * taper;
            value += input[static_cast<std::size_t>(frame)] * w; weight += w;
        }
        result[i] = weight > 1e-12 ? static_cast<float>(value / weight) : 0;
    }
    return result;
}
}

struct StemSeparator::Impl {
    std::unique_ptr<demucscpp::demucs_model> model;
};

const char* StemSeparator::modelFileName() { return "ggml-model-htdemucs-4s-f16.bin"; }
const char* StemSeparator::expectedModelSha256() { return modelHash; }
std::string StemSeparator::modelFileSha256(const std::filesystem::path& file) { return detail::sha256File(file); }

StemSeparator::StemSeparator(const std::filesystem::path& file) : impl(std::make_unique<Impl>()) {
    // Only the pinned, converted official checkpoint is accepted. In addition
    // to integrity, this bounds all tensor dimensions before the upstream
    // fixed-architecture loader reads the container.
    if (modelFileSha256(file) != modelHash)
        throw std::runtime_error("The bundled stem model failed SHA-256 verification. Reinstall the complete app.");
    impl->model = std::make_unique<demucscpp::demucs_model>();
    if (!demucscpp::load_demucs_model(file.string(), impl->model.get()) || !impl->model->is_4sources)
        throw std::runtime_error("The bundled four-stem model could not be loaded.");
}

StemSeparator::~StemSeparator() = default;

std::array<AudioData, 4> StemSeparator::separate(const AudioData& input,
                                               const std::atomic<bool>& cancelled,
                                               const Progress& progress) {
    checkCancelled(cancelled);
    if (!input.valid() || input.channels.size() > 2 || input.frames() > maxInputFrames
        || input.sampleRate < 8000 || input.sampleRate > 384000)
        throw std::runtime_error("Stem separation requires finite mono/stereo audio, 8-384 kHz, up to 32 million frames.");
    float reportedProgress = 0;
    auto announce = [&](float value, const std::string& stage) {
        checkCancelled(cancelled);
        reportedProgress = std::max(reportedProgress, std::clamp(value, 0.0f, 1.0f));
        if (progress) progress(reportedProgress, stage);
    };
    announce(0, "Preparing native stem separation");
    std::array<AudioData, 4> result;
    for (auto& stem : result) {
        checkCancelled(cancelled);
        stem.sampleRate = input.sampleRate;
        stem.channels.assign(input.channels.size(), std::vector<float>(input.frames(), 0));
    }
    // A constant or silent signal contains no separable musical information and
    // otherwise causes the model's normalization to divide by zero. Preserve it
    // in the residual/melody stream without pretending an inference was needed.
    double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
    for (const auto& channel : input.channels) for (float value : channel) {
        minimum = std::min(minimum, static_cast<double>(value)); maximum = std::max(maximum, static_cast<double>(value));
    }
    if (maximum - minimum < 1e-9) {
        result[1] = input;
        announce(1, "Silent/constant source prepared"); return result;
    }
    std::vector<float> accumulatedWeight(input.frames(), 0);
    // Process bounded 30-second windows instead of resampling/allocating four
    // full-length 44.1-kHz tracks. This also handles long low-rate sources
    // without multiplying their resident size by the resampling ratio.
    const auto chunkFrames = static_cast<std::size_t>(std::llround(30 * input.sampleRate));
    const auto overlapFrames = static_cast<std::size_t>(std::llround(input.sampleRate));
    const auto stride = chunkFrames - overlapFrames;
    std::unique_lock<std::mutex> lock(inferenceMutex, std::defer_lock);
    while (!lock.try_lock()) {
        checkCancelled(cancelled);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (std::size_t start = 0; start < input.frames(); start += stride) {
        checkCancelled(cancelled);
        const auto length = std::min(chunkFrames, input.frames() - start);
        const auto modelFrames = std::max<std::size_t>(2,
            static_cast<std::size_t>(std::llround(length * modelRate / input.sampleRate)));
        Eigen::MatrixXf chunk(2, static_cast<Eigen::Index>(modelFrames));
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const auto& original = input.channels[std::min(channel, input.channels.size() - 1)];
            const std::vector<float> selection(original.begin() + static_cast<std::ptrdiff_t>(start),
                original.begin() + static_cast<std::ptrdiff_t>(start + length));
            const auto converted = resample(selection, input.sampleRate, modelRate, modelFrames, cancelled);
            for (std::size_t i = 0; i < modelFrames; ++i)
                chunk(static_cast<Eigen::Index>(channel), static_cast<Eigen::Index>(i)) = converted[i];
        }
        auto callback = [&](float localProgress, const std::string&) {
            const double fraction = (static_cast<double>(start) + std::clamp(localProgress, 0.0f, 1.0f) * length)
                / static_cast<double>(input.frames());
            announce(static_cast<float>(.02 + .94 * fraction), "Separating vocals, melody, bass and drums");
        };
        auto outputs = demucscpp::demucs_inference(*impl->model, chunk, callback);
        if (outputs.dimension(0) != 4 || outputs.dimension(1) != 2
            || outputs.dimension(2) != static_cast<Eigen::Index>(modelFrames))
            throw std::runtime_error("The stem model returned unexpected output dimensions.");
        // Native model order is drums, bass, other, vocals.
        constexpr std::array<int,4> nativeOrder {3, 2, 1, 0};
        std::vector<float> weights(length);
        for (std::size_t i = 0; i < length; ++i) {
            const float fadeIn = start == 0 ? 1.0f
                : std::min(1.0f, static_cast<float>(i + 1) / static_cast<float>(overlapFrames));
            const float fadeOut = start + length == input.frames() ? 1.0f
                : std::min(1.0f, static_cast<float>(length - i) / static_cast<float>(overlapFrames));
            weights[i] = std::min(fadeIn, fadeOut);
            accumulatedWeight[start + i] += weights[i];
        }
        for (std::size_t stem = 0; stem < 4; ++stem) {
            for (std::size_t channel = 0; channel < input.channels.size(); ++channel) {
                std::vector<float> separated(modelFrames);
                for (std::size_t i = 0; i < modelFrames; ++i) {
                    const float left = outputs(nativeOrder[stem], static_cast<Eigen::Index>(channel), static_cast<Eigen::Index>(i));
                    const float value = input.channels.size() == 1
                        ? .5f * (left + outputs(nativeOrder[stem], 1, static_cast<Eigen::Index>(i))) : left;
                    if (!std::isfinite(value)) throw std::runtime_error("Stem inference produced nonfinite audio.");
                    separated[i] = value;
                }
                const auto aligned = resample(separated, modelRate, input.sampleRate, length, cancelled);
                for (std::size_t i = 0; i < length; ++i)
                    result[stem].channels[channel][start + i] += aligned[i] * weights[i];
            }
        }
    }
    lock.unlock();
    announce(.97f, "Aligning stems to the original source");
    for (auto& stem : result)
        for (auto& channel : stem.channels)
            for (std::size_t i = 0; i < input.frames(); ++i) {
                if ((i & 4095u) == 0) checkCancelled(cancelled);
                channel[i] /= std::max(accumulatedWeight[i], 1e-9f);
            }
    // Preserve the source when all four stems are enabled. Put the model's
    // residual (including resampling/DC error) into "other", a conventional
    // mixture-consistency correction, not a substitute for neural separation.
    for (std::size_t channel = 0; channel < input.channels.size(); ++channel)
        for (std::size_t i = 0; i < input.frames(); ++i) {
            if ((i & 4095u) == 0) checkCancelled(cancelled);
            const double fixed = static_cast<double>(input.channels[channel][i])
                - result[0].channels[channel][i] - result[2].channels[channel][i] - result[3].channels[channel][i];
            result[1].channels[channel][i] = static_cast<float>(fixed);
        }
    for (const auto& stem : result) if (!stem.valid()) throw std::runtime_error("Stem alignment produced invalid audio.");
    announce(1, "Four stems ready"); return result;
}
} // namespace samplify
