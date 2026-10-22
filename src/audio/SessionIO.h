#pragma once
#include <juce_core/juce_core.h>
#include "core/Model.h"
#include "workstation/WorkstationModel.h"

namespace samplify {
struct Session {
    Project project;
    juce::File source;
    std::array<juce::File, 4> stems;
    juce::String migrationNotice;
    WorkstationState workstation;
};
// Versioned JSON sessions reference audio files; they never embed audio data.
void saveSession(const juce::File& file, const Session& session);
Session loadSession(const juce::File& file);
} // namespace samplify
