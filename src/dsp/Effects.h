#pragma once

#include "workstation/WorkstationModel.h"
#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace samplify {

struct FxParameter {
    const char* name = "";
    const char* unit = "";
    float minimum = 0, maximum = 1, defaultValue = 0;
    bool logarithmic = false;
};

struct FxDefinition {
    FxType type = FxType::None;
    const char* name = "Off";
    std::array<FxParameter, 6> parameters;
    int parameterCount = 0;
};

enum class FxPresetCategory { SoundDesign, Mixing, Mastering };
struct FxPreset {
    const char* name = "";
    FxPresetCategory category = FxPresetCategory::SoundDesign;
    std::array<FxSlot, 4> slots;
};

std::span<const FxDefinition> fxDefinitions() noexcept;
const FxDefinition& fxDefinition(FxType) noexcept;
float fxParameterValue(FxType, std::size_t parameter, float normalized) noexcept;
float fxParameterNormalized(FxType, std::size_t parameter, float actual) noexcept;
FxSlot defaultFxSlot(FxType) noexcept;
std::span<const FxPreset> fxPresets() noexcept;
const char* fxPresetCategoryName(FxPresetCategory) noexcept;

// Prepare allocates delay/reverb memory. setSlots, reset and processStereo never
// allocate or lock. The owner serializes these calls on its audio thread; build
// immutable control snapshots elsewhere rather than mutating a rack concurrently.
class FxRack {
public:
    FxRack();
    ~FxRack();
    FxRack(FxRack&&) noexcept;
    FxRack& operator=(FxRack&&) noexcept;
    FxRack(const FxRack&) = delete;
    FxRack& operator=(const FxRack&) = delete;
    void prepare(double sampleRate, std::size_t maximumBlockSize);
    void setSlots(const std::array<FxSlot, 4>&) noexcept;
    void processStereo(float* left, float* right, std::size_t samples) noexcept;
    void reset() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace samplify
