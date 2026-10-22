#pragma once

#include "core/Model.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace samplify {

class SeparationCancelled final : public std::runtime_error {
public:
    SeparationCancelled() : std::runtime_error("Stem separation cancelled") {}
};

// Native HTDemucs inference. Own an instance on a background worker, never on
// the audio callback. The model is loaded once and reused for subsequent files.
class StemSeparator final {
public:
    using Progress = std::function<void(float fraction, const std::string& stage)>;
    explicit StemSeparator(const std::filesystem::path& modelFile);
    ~StemSeparator();
    StemSeparator(const StemSeparator&) = delete;
    StemSeparator& operator=(const StemSeparator&) = delete;

    // Order: Vocals, Melody (Demucs "other"), Bass, Drums. All four outputs
    // preserve the input's exact frame count, rate and mono/stereo layout.
    std::array<AudioData, 4> separate(const AudioData& input,
                                     const std::atomic<bool>& cancelled,
                                     const Progress& progress = {});

    static const char* modelFileName();
    static const char* expectedModelSha256();
    static std::string modelFileSha256(const std::filesystem::path&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace samplify
