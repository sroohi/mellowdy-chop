#include "Model.h"

#include <cmath>

namespace samplify {

bool AudioData::valid() const {
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || channels.empty()
        || channels.size() > 32 || channels.front().empty()) return false;
    const auto count = channels.front().size();
    for (const auto& channel : channels) {
        if (channel.size() != count) return false;
        for (const float sample : channel) {
            if (!std::isfinite(sample)) return false;
        }
    }
    return true;
}

} // namespace samplify
