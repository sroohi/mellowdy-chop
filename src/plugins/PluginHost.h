#pragma once

#include "workstation/WorkstationModel.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>

namespace samplify {

// A stereo insert host. All control methods run on the message thread; prepare
// runs while the device is stopped. process* is single-audio-thread only. Native
// plug-ins are never constructed, destroyed, or state-serialized in that thread.
class PluginHost {
public:
    static constexpr int busCount = 9;
    static constexpr int slotCount = 4;
    static constexpr int masterBus = 8;
    using Completion = std::function<void(juce::String error)>;

    // Optional format injection permits deterministic host testing without
    // installing a plug-in binary. Production registers AU and VST3 itself.
    explicit PluginHost(std::unique_ptr<juce::AudioPluginFormat> additionalFormat = {});
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    void prepare(double sampleRate, int maximumBlockSize);
    void process(int bus, juce::AudioBuffer<float>&, juce::MidiBuffer&) noexcept;
    void processStereo(int bus, float* left, float* right, int frames) noexcept;
    void setTransport(double bpm, double beat, bool playing) noexcept;

    void scanAsync(Completion = {});
    bool isScanning() const;
    juce::String scanStatus() const;
    juce::Array<juce::PluginDescription> availablePlugins() const;
    void loadAsync(int bus, int slot, const juce::PluginDescription&, Completion = {});
    void retryLoad(int bus, int slot, Completion = {});
    void remove(int bus, int slot);
    void setBypass(int bus, int slot, bool);
    bool isLoading(int bus, int slot) const;
    juce::String status(int bus, int slot) const;
    HostedPluginSlot getSlotState(int bus, int slot) const;
    // Ordinary edits preserve native editor changes when the caller still holds
    // its last accepted blob. Explicit session restore may force that blob back.
    void applyState(const WorkstationState&, bool forceRestore = false);
    void captureState(WorkstationState&);

    void showEditor(int bus, int slot);
    std::unique_ptr<juce::Component> createPluginManagerView(int bus, int slot);
    // A listener may copy getSlotState into its domain model after edits. The
    // callback is always on the message thread, including async completions.
    std::function<void()> onChange;

    // Factory injection exercises the exact production preparation, publication,
    // bypass, retirement and state path without shipping a test plug-in binary.
    bool installInstance(int bus, int slot, std::unique_ptr<juce::AudioPluginInstance>,
                         const juce::PluginDescription&, juce::String& error);

private:
    struct Core;
    std::shared_ptr<Core> core;
};

} // namespace samplify
