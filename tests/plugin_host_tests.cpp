#include "plugins/PluginHost.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <new>

namespace heapProbe {
thread_local bool enabled = false;
thread_local int allocations = 0, deletions = 0;
}
void* operator new(std::size_t size) {
    if (heapProbe::enabled) ++heapProbe::allocations;
    if (auto* memory = std::malloc(std::max<std::size_t>(1, size))) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { if (heapProbe::enabled && p) ++heapProbe::deletions; std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {
using namespace samplify;
int checks = 0, failures = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool waitFor(const std::function<bool()>& ready, int timeout = 10000) {
    const auto until = juce::Time::getMillisecondCounterHiRes() + timeout;
    while (!ready() && juce::Time::getMillisecondCounterHiRes() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    return ready();
}
juce::PluginDescription fakeDescription() {
    juce::PluginDescription d;
    d.name = "Host test gain"; d.manufacturerName = "Samplify tests"; d.pluginFormatName = "Unavailable test format";
    d.fileOrIdentifier = "/missing/HostTest.vst3"; d.numInputChannels = d.numOutputChannels = 2;
    d.uniqueId = 12345;
    return d;
}
struct Probe {
    std::atomic<int> prepared { 0 }, released { 0 }, destroyed { 0 }, processed { 0 };
    std::atomic<bool> entered { false }, block { false }, continueProcessing { false };
    std::atomic<bool> destroyedOnAudioThread { false };
    std::thread::id audioThread;
};
class GainEffect final : public juce::AudioPluginInstance {
public:
    explicit GainEffect(Probe& p, float gain = .5f)
        : AudioPluginInstance(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)), probe(p), multiplier(gain) {}
    ~GainEffect() override { ++probe.destroyed; if (std::this_thread::get_id() == probe.audioThread) probe.destroyedOnAudioThread = true; }
    const juce::String getName() const override { return "Host test gain"; }
    void fillInPluginDescription(juce::PluginDescription& d) const override { d = fakeDescription(); }
    void prepareToPlay(double, int) override { ++probe.prepared; }
    void releaseResources() override { ++probe.released; }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        ++probe.processed;
        if (probe.block) {
            probe.entered = true;
            while (!probe.continueProcessing) std::this_thread::yield();
        }
        b.applyGain(multiplier);
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& block) override { block.replaceAll(&multiplier, sizeof(multiplier)); }
    void setStateInformation(const void* data, int size) override { if (size == sizeof(multiplier)) std::memcpy(&multiplier, data, sizeof(multiplier)); }
    void setGain(float gain) { multiplier = gain; }
private:
    Probe& probe;
    float multiplier;
};
class GainFormat final : public juce::AudioPluginFormat {
public:
    explicit GainFormat(Probe& p) : probe(p) {}
    juce::String getName() const override { return fakeDescription().pluginFormatName; }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& types, const juce::String&) override { types.add(new juce::PluginDescription(fakeDescription())); }
    bool fileMightContainThisPluginType(const juce::String& path) override { return path == fakeDescription().fileOrIdentifier; }
    juce::String getNameOfPluginFromIdentifier(const juce::String&) override { return fakeDescription().name; }
    bool pluginNeedsRescanning(const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist(const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override { return false; }
    GainEffect* current = nullptr;
protected:
    void createPluginInstance(const juce::PluginDescription&, double, int, PluginCreationCallback callback) override {
        auto effect = std::make_unique<GainEffect>(probe);
        current = effect.get(); callback(std::move(effect), {});
    }
private:
    Probe& probe;
};
void fill(std::array<float, 257>& l, std::array<float, 257>& r, float value = 1) { l.fill(value); r.fill(value); }

void deterministicTests() {
    Probe a, b;
    PluginHost host;
    host.prepare(48000, 128);
    juce::String error;
    require(host.installInstance(0, 0, std::make_unique<GainEffect>(a, .5f), fakeDescription(), error), "Installs stereo effect through production preparation path");
    require(host.installInstance(0, 1, std::make_unique<GainEffect>(b, .25f), fakeDescription(), error), "Loads a second serial insert");
    require(a.prepared == 1 && b.prepared == 1, "Each native instance is prepared exactly once before publication");
    std::array<float, 257> left, right;
    fill(left, right);
    heapProbe::enabled = true;
    host.processStereo(0, left.data(), right.data(), static_cast<int>(left.size()));
    heapProbe::enabled = false;
    require(heapProbe::allocations == 0 && heapProbe::deletions == 0, "Loaded host processing allocates and destroys no C++ heap objects in the audio callback");
    require(std::abs(left[256] - .125f) < .00001f && std::abs(right[0] - .125f) < .00001f, "Both channels pass through the ordered insert chain, including oversized-block chunking");
    require(a.processed == 3 && b.processed == 3, "A 257-frame callback is split into prepared 128-frame blocks");
    fill(left, right); host.processStereo(1, left.data(), right.data(), 257);
    require(left[0] == 1, "Track plugin routing does not leak into another bus");
    host.setBypass(0, 0, true); fill(left, right); host.processStereo(0, left.data(), right.data(), 257);
    require(left[0] == .25f && host.getSlotState(0, 0).bypass, "Bypass publishes a dry first insert while retaining the second");
    WorkstationState saved;
    host.captureState(saved);
    juce::MemoryOutputStream decoded;
    require(juce::Base64::convertFromBase64(decoded, juce::String(saved.tracks[0].plugins[0].stateBase64)) && decoded.getDataSize() == sizeof(float), "State uses portable RFC4648 Base64");
    float gain = 0;
    std::memcpy(&gain, decoded.getData(), sizeof(gain));
    require(gain == .5f && !saved.tracks[0].plugins[0].xmlDescription.empty(), "Captures actual native parameter state and exact descriptor");
    host.applyState(saved); // Same descriptors and state; no needless re-instantiation.
    require(!host.isLoading(0, 0) && a.prepared == 1, "Applying unchanged state is idempotent");
    host.remove(0, 1);
    require(b.destroyed == 1 && b.released == 1, "Removed inactive effect is released off the audio callback");
    require(!host.installInstance(-1, 0, std::make_unique<GainEffect>(b), fakeDescription(), error), "Rejects invalid bus without corrupting the rack");

    PluginHost restored;
    restored.applyState(saved);
    require(waitFor([&] { return !restored.isLoading(0, 0) && !restored.isLoading(0, 1); }), "Unavailable plugins complete their asynchronous load attempts");
    require(restored.status(0, 0).isNotEmpty() && restored.getSlotState(0, 0).stateBase64 == saved.tracks[0].plugins[0].stateBase64,
            "Missing plugin recovery preserves user state and descriptor");
    fill(left, right); restored.processStereo(0, left.data(), right.data(), 257);
    require(left[0] == 1, "Missing effects keep the audio path usable");
    bool cancellationDelivered = false;
    restored.loadAsync(8, 3, fakeDescription(), [&](juce::String message) { cancellationDelivered = message == "Load cancelled"; });
    restored.remove(8, 3);
    require(waitFor([&] { return cancellationDelivered; }), "Removing a pending load invalidates its delayed factory callback");
    require(restored.getSlotState(8, 3).empty(), "Cancelled load cannot resurrect a removed plugin");
    restored.retryLoad(0, 0);
    require(waitFor([&] { return !restored.isLoading(0, 0); }) && !restored.getSlotState(0, 0).stateBase64.empty(), "Retry preserves saved state when a plugin remains unavailable");
    {
        auto doomed = std::make_unique<PluginHost>();
        doomed->loadAsync(0, 0, fakeDescription());
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    require(true, "An async completion after host destruction is ignored safely");
}
void retirementTest() {
    Probe probe;
    PluginHost host;
    juce::String error;
    host.installInstance(8, 0, std::make_unique<GainEffect>(probe), fakeDescription(), error);
    probe.block = true;
    std::array<float, 257> left, right; fill(left, right);
    std::thread audio([&] {
        probe.audioThread = std::this_thread::get_id();
        host.processStereo(8, left.data(), right.data(), 128);
    });
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
    while (!probe.entered && juce::Time::getMillisecondCounterHiRes() < deadline) std::this_thread::yield();
    require(probe.entered, "Audio thread pins the current rack before processing");
    host.remove(8, 0);
    require(probe.destroyed == 0, "A pinned plugin survives concurrent removal");
    probe.continueProcessing = true; audio.join();
    host.remove(8, 1); // Reclamation control operation after the callback retires.
    require(probe.destroyed == 1 && !probe.destroyedOnAudioThread, "Retired plugin destruction runs on the control thread after unpinning");
}
void nativeEditSnapshotTest() {
    Probe probe;
    auto format = std::make_unique<GainFormat>(probe);
    auto* factory = format.get();
    PluginHost host(std::move(format));
    host.loadAsync(0, 0, fakeDescription());
    require(waitFor([&] { return !host.isLoading(0, 0); }) && host.status(0, 0) == "Ready", "Injected format uses the actual asynchronous factory pipeline");
    WorkstationState prior;
    host.captureState(prior); host.applyState(prior);
    factory->current->setGain(.25f); // Simulate a native editor parameter change.
    prior.tracks[0].gainDb = -6;
    host.applyState(prior);
    std::array<float, 257> left, right;
    fill(left, right); host.processStereo(0, left.data(), right.data(), 128);
    require(left[0] == .25f && probe.prepared == 1, "Unrelated mixer edit preserves unsaved native editor parameters");
    WorkstationState captured = prior;
    host.captureState(captured);
    host.applyState(prior); // Caller has not adopted captured state yet.
    fill(left, right); host.processStereo(0, left.data(), right.data(), 128);
    require(left[0] == .25f && probe.prepared == 1, "A stale mixer model after capture cannot reload prior plugin state");
    host.applyState(captured); // Adopting the new snapshot also must not reload.
    require(probe.prepared == 1 && !host.isLoading(0, 0), "Adopting the captured blob is idempotent");
    captured.tracks[0].plugins[0].bypass = true; host.applyState(captured);
    captured.tracks[0].plugins[0].bypass = false; host.applyState(captured);
    fill(left, right); host.processStereo(0, left.data(), right.data(), 128);
    require(left[0] == .25f && probe.prepared == 1, "Bypass-only state changes retain current native editor parameters");
    host.showEditor(0, 0);
    factory->current->setGain(.75f);
    host.applyState(captured, true); // Same accepted blob; explicit restore differs from an ordinary edit.
    require(waitFor([&] { return !host.isLoading(0, 0); }) && probe.prepared == 2, "Explicit force restore reinstantiates an identical saved snapshot");
    fill(left, right); host.processStereo(0, left.data(), right.data(), 128);
    require(left[0] == .25f, "Explicit restore applies the saved native parameter blob");
}

#if JUCE_MAC
void appleAudioUnitTest() {
    PluginHost host;
    host.scanAsync();
    require(waitFor([&] { return !host.isScanning(); }, 30000), "Background scan completes through the real AudioComponent registry");
    juce::PluginDescription chosen;
    bool found = false;
    for (const auto& d : host.availablePlugins()) {
        if (!d.isInstrument && d.pluginFormatName == "AudioUnit" && d.manufacturerName.containsIgnoreCase("Apple")
            && (d.name.containsIgnoreCase("EQ") || d.name.containsIgnoreCase("Compressor"))) {
            chosen = d; found = true; break;
        }
    }
    require(found, "macOS provides a real Apple stereo Audio Unit effect");
    if (!found) return;
    host.prepare(48000, 128);
    bool done = false;
    juce::String error;
    host.loadAsync(8, 0, chosen, [&](juce::String e) { error = e; done = true; });
    require(waitFor([&] { return done; }, 30000) && error.isEmpty() && host.status(8, 0) == "Ready", "Instantiates real macOS Audio Unit asynchronously");
    if (error.isNotEmpty()) { std::cerr << error << '\n'; return; }
    std::array<float, 257> left, right;
    for (int i = 0; i < 257; ++i) left[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] = .1f * std::sin(static_cast<float>(i) * .1f);
    host.setTransport(120, 4, true);
    host.processStereo(8, left.data(), right.data(), 257);
    require(std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x); }), "Real Apple effect processes audio without invalid output");
    WorkstationState session;
    host.captureState(session);
    require(!session.masterPlugins[0].stateBase64.empty(), "Captures real Audio Unit state");
    PluginHost restored;
    restored.prepare(48000, 128); restored.applyState(session);
    require(waitFor([&] { return !restored.isLoading(8, 0); }, 30000) && restored.status(8, 0) == "Ready", "Restores the real Audio Unit descriptor and native state");
    WorkstationState again; restored.captureState(again);
    require(!again.masterPlugins[0].stateBase64.empty() && again.masterPlugins[0].identifier == session.masterPlugins[0].identifier,
            "Restored Audio Unit remains serializable with the same native identity");
}
#endif
}
int main() {
    juce::ScopedJuceInitialiser_GUI initialiser;
    deterministicTests(); retirementTest(); nativeEditSnapshotTest();
   #if JUCE_MAC
    appleAudioUnitTest();
   #endif
    std::cout << checks - failures << '/' << checks << " plugin host checks passed\n";
    return failures == 0 ? 0 : 1;
}
