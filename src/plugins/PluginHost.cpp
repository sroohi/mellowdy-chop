#include "PluginHost.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>
#if JUCE_MAC && JUCE_PLUGINHOST_AU
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace samplify {
namespace {
bool valid(int bus, int slot) { return bus >= 0 && bus < PluginHost::busCount && slot >= 0 && slot < PluginHost::slotCount; }
HostedPluginSlot describe(const juce::PluginDescription& d) {
    HostedPluginSlot s;
    s.name = d.name.toStdString(); s.format = d.pluginFormatName.toStdString();
    s.identifier = d.fileOrIdentifier.toStdString(); s.manufacturer = d.manufacturerName.toStdString();
    if (auto xml = d.createXml()) s.xmlDescription = xml->toString().toStdString();
    return s;
}
bool sameIdentity(const HostedPluginSlot& a, const HostedPluginSlot& b) {
    if (a.xmlDescription == b.xmlDescription) return true;
    auto ax = juce::XmlDocument::parse(juce::String(a.xmlDescription));
    auto bx = juce::XmlDocument::parse(juce::String(b.xmlDescription));
    juce::PluginDescription ad, bd;
    return ax && bx && ad.loadFromXml(*ax) && bd.loadFromXml(*bx)
        && ad.pluginFormatName == bd.pluginFormatName && ad.isDuplicateOf(bd);
}
juce::File settingsFile(const juce::String& name) {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Samplify");
    dir.createDirectory();
    return dir.getChildFile(name);
}
class EditorWindow final : public juce::DocumentWindow {
public:
    EditorWindow(const juce::String& name, juce::AudioProcessorEditor* editor)
        : DocumentWindow(name, juce::Colour(0xff34342d), closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(juce::jmax(300, editor->getWidth()), juce::jmax(180, editor->getHeight()));
        setVisible(true);
    }
    ~EditorWindow() override {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(getContentComponent()))
            editor->processor.editorBeingDeleted(editor);
        clearContentComponent();
    }
    void closeButtonPressed() override { setVisible(false); }
};
#if JUCE_MAC && JUCE_PLUGINHOST_AU
juce::String fourCC(OSType value) {
    const char text[] { static_cast<char>(value >> 24), static_cast<char>(value >> 16),
                        static_cast<char>(value >> 8), static_cast<char>(value), 0 };
    return juce::String::fromUTF8(text, 4);
}
// AU discovery must not synchronously create instances from the scanner thread:
// JUCE marshals their creation to the message thread, which can deadlock a close
// that joins the scanner. The OS registry has everything needed for discovery.
// The actual bus configuration is checked by prepareNode when the user loads it.
void discoverAudioUnits(juce::KnownPluginList& list, const std::atomic<bool>& cancelled) {
    AudioComponent component = nullptr;
    AudioComponentDescription filter {};
    while (!cancelled.load() && (component = AudioComponentFindNext(component, &filter)) != nullptr) {
        AudioComponentDescription native {};
        if (AudioComponentGetDescription(component, &native) != noErr) continue;
        const auto type = native.componentType;
        if (type != kAudioUnitType_Effect && type != kAudioUnitType_MusicEffect && type != kAudioUnitType_Panner) continue;
        juce::PluginDescription d;
        CFStringRef name = nullptr;
        if (AudioComponentCopyName(component, &name) == noErr && name) { d.name = juce::String::fromCFString(name); CFRelease(name); }
        if (d.name.containsChar(':')) {
            d.manufacturerName = d.name.upToFirstOccurrenceOf(":", false, false).trim();
            d.name = d.name.fromFirstOccurrenceOf(":", false, false).trim();
        }
        if (d.name.isEmpty()) continue;
        if (d.manufacturerName.isEmpty()) d.manufacturerName = fourCC(native.componentManufacturer);
        d.descriptiveName = d.name;
        d.pluginFormatName = "AudioUnit"; d.category = "Effect";
        d.fileOrIdentifier = juce::String("AudioUnit:") + (type == kAudioUnitType_Panner ? "Panners/" : "Effects/")
            + fourCC(type) + "," + fourCC(native.componentSubType) + "," + fourCC(native.componentManufacturer);
        d.uniqueId = d.deprecatedUid = static_cast<int>(type ^ native.componentSubType ^ native.componentManufacturer);
        UInt32 version = 0;
        if (AudioComponentGetVersion(component, &version) == noErr)
            d.version = juce::String(static_cast<int>(version >> 16)) + "." + juce::String(static_cast<int>((version >> 8) & 255)) + "." + juce::String(static_cast<int>(version & 255));
        d.lastInfoUpdateTime = juce::Time::getCurrentTime();
        // Channel counts remain unknown until instantiation; they are not faked.
        list.addType(d);
    }
}
#endif
}

struct PluginHost::Core final : juce::Timer, std::enable_shared_from_this<Core> {
    struct Node {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::unique_ptr<EditorWindow> editor;
        bool prepared = false;
        ~Node() {
            editor.reset();
            if (prepared) { try { instance->releaseResources(); } catch (...) {} }
        }
    };
    struct Cell {
        HostedPluginSlot state;
        // captureState updates state to the live blob without pretending the
        // caller has already adopted that new snapshot into its domain model.
        std::string lastAppliedBase64;
        Node* node = nullptr;
        juce::String error;
        std::uint64_t generation = 0;
        bool loading = false;
    };
    struct Entry { Node* node = nullptr; bool bypass = false; };
    struct Rack { std::array<std::array<Entry, slotCount>, busCount> buses {}; };
    struct PlayHead final : juce::AudioPlayHead {
        explicit PlayHead(Core& c) : host(c) {}
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo p;
            const auto bpm = host.tempo.load(std::memory_order_relaxed);
            const auto beat = host.position.load(std::memory_order_relaxed);
            p.setBpm(bpm); p.setPpqPosition(beat); p.setIsPlaying(host.playing.load(std::memory_order_relaxed));
            p.setTimeSignature(TimeSignature { 4, 4 });
            p.setTimeInSeconds(beat * 60.0 / bpm);
            p.setTimeInSamples(static_cast<juce::int64>(beat * 60.0 / bpm * host.sampleRate));
            return p;
        }
        Core& host;
    };

    PluginHost* owner;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::array<std::array<Cell, slotCount>, busCount> cells;
    std::array<juce::MidiBuffer, busCount> stereoMidi;
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<std::unique_ptr<Rack>> racks;
    std::atomic<Rack*> active { nullptr }, hazard { nullptr };
    std::atomic<bool> suspended { false };
    std::atomic<double> tempo { 120 }, position { 0 };
    std::atomic<bool> playing { false };
    PlayHead playHead { *this };
    double sampleRate = 48000;
    int blockSize = 512;
    std::thread scanWorker;
    std::shared_ptr<std::atomic<bool>> scanCancelled = std::make_shared<std::atomic<bool>>(false);
    bool scanning = false;
    int notificationDepth = 0;
    bool pendingNotification = false;
    juce::String scanMessage = "Scan to discover installed Audio Units and VST3 effects";

    struct NotificationBatch {
        Core& host;
        explicit NotificationBatch(Core& c) : host(c) { ++host.notificationDepth; }
        ~NotificationBatch() {
            if (--host.notificationDepth == 0 && host.pendingNotification) {
                host.pendingNotification = false;
                host.changed();
            }
        }
    };

    Core(PluginHost* p, std::unique_ptr<juce::AudioPluginFormat> additionalFormat) : owner(p) {
        formats.addDefaultFormats();
        if (additionalFormat) formats.addFormat(additionalFormat.release());
        for (auto& midi : stereoMidi) midi.ensureSize(65536);
        if (auto xml = juce::XmlDocument::parse(settingsFile("Plugins.xml"))) known.recreateFromXml(*xml);
        publish(); startTimer(250);
    }
    ~Core() override { shutdown(); }
    void shutdown() {
        stopTimer(); owner = nullptr;
        scanCancelled->store(true);
        if (scanWorker.joinable()) scanWorker.join();
        suspended.store(true, std::memory_order_seq_cst);
        while (hazard.load(std::memory_order_seq_cst) != nullptr) std::this_thread::yield();
        active.store(nullptr, std::memory_order_seq_cst);
        racks.clear(); nodes.clear();
    }
    void changed() {
        if (notificationDepth > 0) { pendingNotification = true; return; }
        if (owner && owner->onChange) owner->onChange();
    }
    void timerCallback() override { reclaim(); }
    void reclaim() {
        const auto* current = active.load(std::memory_order_seq_cst);
        const auto* pinned = hazard.load(std::memory_order_seq_cst);
        std::erase_if(racks, [=](const auto& r) { return r.get() != current && r.get() != pinned; });
        std::erase_if(nodes, [this](const auto& n) {
            for (const auto& rack : racks)
                for (const auto& bus : rack->buses)
                    for (const auto& entry : bus) if (entry.node == n.get()) return false;
            return true;
        });
    }
    void publish() {
        auto next = std::make_unique<Rack>();
        for (int b = 0; b < busCount; ++b)
            for (int s = 0; s < slotCount; ++s) {
                const auto& c = cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
                next->buses[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)] = { c.node, c.state.bypass };
            }
        auto* raw = next.get(); racks.push_back(std::move(next));
        active.store(raw, std::memory_order_seq_cst); reclaim();
    }
    template<typename Fn> void withSuspendedAudio(Fn&& fn) {
        suspended.store(true, std::memory_order_seq_cst);
        while (hazard.load(std::memory_order_seq_cst) != nullptr) std::this_thread::yield();
        try { fn(); } catch (...) { suspended.store(false, std::memory_order_seq_cst); throw; }
        suspended.store(false, std::memory_order_seq_cst);
    }
    bool prepareNode(Node& n, juce::String& error) {
        auto& p = *n.instance;
        auto layout = p.getBusesLayout();
        if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty()) { error = "This slot accepts stereo audio effects only"; return false; }
        for (auto& bus : layout.inputBuses) bus = juce::AudioChannelSet::disabled();
        for (auto& bus : layout.outputBuses) bus = juce::AudioChannelSet::disabled();
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        if (!p.setBusesLayout(layout)) { error = "The effect does not support a stereo input and output"; return false; }
        p.setRateAndBufferSizeDetails(sampleRate, blockSize);
        p.setPlayHead(&playHead);
        n.prepared = true;
        p.prepareToPlay(sampleRate, blockSize);
        return true;
    }
    bool install(int b, int s, std::unique_ptr<juce::AudioPluginInstance> instance, const HostedPluginSlot& state, juce::String& error) {
        if (!instance) { error = "The effect could not be instantiated"; return false; }
        auto n = std::make_unique<Node>(); n->instance = std::move(instance);
        if (!prepareNode(*n, error)) return false;
        if (!state.stateBase64.empty()) {
            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(decoded, juce::String(state.stateBase64))
                || decoded.getDataSize() > workstationMaxPluginStateBytes) { error = "Invalid or oversized plug-in state"; return false; }
            n->instance->setStateInformation(decoded.getData(), static_cast<int>(decoded.getDataSize()));
        }
        juce::PluginDescription actual;
        n->instance->fillInPluginDescription(actual);
        auto nativeState = describe(actual);
        nativeState.stateBase64 = state.stateBase64; nativeState.bypass = state.bypass;
        auto& c = cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
        c.node = n.get(); c.state = std::move(nativeState); c.lastAppliedBase64 = state.stateBase64;
        c.error.clear(); c.loading = false;
        nodes.push_back(std::move(n)); publish(); return true;
    }
    void load(int b, int s, HostedPluginSlot state, Completion completion) {
        auto& c = cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
        const auto generation = ++c.generation;
        c.state = state; c.lastAppliedBase64 = state.stateBase64;
        c.node = nullptr; c.error.clear(); c.loading = true;
        publish(); changed();
        juce::PluginDescription description;
        auto xml = juce::XmlDocument::parse(juce::String(state.xmlDescription));
        if (!xml || !description.loadFromXml(*xml)) {
            c.loading = false; c.error = "Invalid plug-in descriptor"; changed();
            if (completion) completion(c.error); return;
        }
        std::weak_ptr<Core> weak = shared_from_this();
        auto deliver = [weak, b, s, generation, state = std::move(state), completion = std::move(completion)]
            (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& creationError) mutable {
                auto self = weak.lock();
                if (!self || !self->owner) return;
                auto& cell = self->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
                if (cell.generation != generation) { if (completion) completion("Load cancelled"); return; }
                juce::String error = creationError;
                // Bypass may have changed while the native factory was loading.
                state.bypass = cell.state.bypass;
                if (instance) {
                    try { self->install(b, s, std::move(instance), state, error); }
                    catch (const std::exception& e) { error = e.what(); }
                    catch (...) { error = "The effect failed during preparation"; }
                }
                if (error.isNotEmpty() || !cell.node) {
                    cell.loading = false;
                    cell.error = error.isNotEmpty() ? error : "Effect unavailable; install it and retry";
                }
                self->changed();
                if (completion) completion(cell.error);
            };
        formats.createPluginInstanceAsync(description, sampleRate, blockSize,
            [deliver = std::move(deliver)](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) mutable {
                // AUv3 can complete its OS instantiation on an auxiliary thread.
                // Pin the returned instance until its preparation/disposal has
                // been delivered to the message thread, including cancellation.
                if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
                    deliver(std::move(instance), error);
                } else {
                    struct Result { std::unique_ptr<juce::AudioPluginInstance> instance; juce::String error; };
                    auto result = std::make_shared<Result>(Result { std::move(instance), error });
                    juce::MessageManager::callAsync([deliver = std::move(deliver), result]() mutable {
                        deliver(std::move(result->instance), result->error);
                    });
                }
            });
    }
};

PluginHost::PluginHost(std::unique_ptr<juce::AudioPluginFormat> additionalFormat)
    : core(std::make_shared<Core>(this, std::move(additionalFormat))) {}
PluginHost::~PluginHost() { core->shutdown(); }
void PluginHost::prepare(double rate, int maximumBlockSize) {
    core->sampleRate = juce::jmax(8000.0, rate); core->blockSize = juce::jmax(1, maximumBlockSize);
    core->withSuspendedAudio([this] {
        for (auto& n : core->nodes) {
            juce::String error;
            try {
                if (n->prepared) n->instance->releaseResources();
                n->prepared = false;
                core->prepareNode(*n, error);
            } catch (const std::exception& e) { error = e.what(); }
            catch (...) { error = "The effect could not prepare for this audio device"; }
            if (error.isNotEmpty()) {
                for (auto& bus : core->cells) for (auto& c : bus) if (c.node == n.get()) { c.node = nullptr; c.error = error; }
            }
        }
        core->publish();
    });
}
void PluginHost::setTransport(double bpm, double beat, bool running) noexcept {
    core->tempo.store(juce::jmax(1.0, bpm), std::memory_order_relaxed);
    core->position.store(beat, std::memory_order_relaxed); core->playing.store(running, std::memory_order_relaxed);
}
void PluginHost::process(int bus, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept {
    if (bus < 0 || bus >= busCount || core->suspended.load(std::memory_order_seq_cst)) return;
    Core::Rack* rack;
    do { rack = core->active.load(std::memory_order_seq_cst); core->hazard.store(rack, std::memory_order_seq_cst); }
    while (rack != core->active.load(std::memory_order_seq_cst));
    if (rack && !core->suspended.load(std::memory_order_seq_cst)) {
        for (const auto& e : rack->buses[static_cast<std::size_t>(bus)]) {
            if (!e.node || e.bypass || !e.node->prepared) continue;
            // The host never acquires AudioProcessor's callback lock. Lifecycle
            // writes occur only before publication or behind the suspend barrier.
            try { e.node->instance->processBlock(buffer, midi); }
            catch (...) { /* A failed third-party callback must not unwind into the device. */ }
        }
    }
    core->hazard.store(nullptr, std::memory_order_seq_cst);
}
void PluginHost::processStereo(int bus, float* left, float* right, int frames) noexcept {
    if (!left || !right || frames <= 0 || bus < 0 || bus >= busCount) return;
    // The two-channel alias uses AudioBuffer's inline channel-pointer storage.
    // Native effects are given chunks no larger than their prepared block size.
    for (int offset = 0; offset < frames; offset += core->blockSize) {
        const auto count = juce::jmin(core->blockSize, frames - offset);
        float* channels[] { left + offset, right + offset };
        juce::AudioBuffer<float> alias(channels, 2, count);
        auto& midi = core->stereoMidi[static_cast<std::size_t>(bus)];
        midi.clear();
        process(bus, alias, midi);
    }
}
bool PluginHost::isScanning() const { return core->scanning; }
juce::String PluginHost::scanStatus() const { return core->scanMessage; }
juce::Array<juce::PluginDescription> PluginHost::availablePlugins() const { return core->known.getTypes(); }
void PluginHost::scanAsync(Completion completion) {
    if (core->scanning) { if (completion) completion("A scan is already running"); return; }
    if (core->scanWorker.joinable()) core->scanWorker.join();
    core->scanning = true; core->scanMessage = "Scanning installed AU / VST3 effects…";
    core->scanCancelled->store(false);
    core->changed();
    std::weak_ptr<Core> weak = core;
    const auto cancelled = core->scanCancelled;
    core->scanWorker = std::thread([weak, cancelled, completion = std::move(completion)]() mutable {
        juce::AudioPluginFormatManager formats; formats.addDefaultFormats();
        juce::KnownPluginList results;
        juce::StringArray failed;
        const auto pedal = settingsFile("PluginScanInProgress.txt");
        for (auto* format : formats.getFormats()) {
            if (cancelled->load()) return;
           #if JUCE_MAC && JUCE_PLUGINHOST_AU
            if (format->getName() == "AudioUnit") { discoverAudioUnits(results, *cancelled); continue; }
           #endif
            auto paths = format->getDefaultLocationsToSearch();
           #if JUCE_MAC
            if (format->getName() == "VST3") {
                paths.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
                paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"));
            }
           #endif
            juce::PluginDirectoryScanner scanner(results, *format, paths, true, pedal, true);
            juce::String name;
            while (!cancelled->load() && scanner.scanNextFile(true, name)) {}
            failed.addArray(scanner.getFailedFiles());
        }
        juce::Array<juce::PluginDescription> effects;
        for (const auto& d : results.getTypes())
            if (!d.isInstrument && ((d.numInputChannels > 0 && d.numOutputChannels > 0) || d.pluginFormatName == "AudioUnit")) effects.add(d);
        if (cancelled->load()) return;
        juce::MessageManager::callAsync([weak, effects = std::move(effects), failed, completion = std::move(completion)]() mutable {
            if (auto self = weak.lock(); self && self->owner) {
                self->known.clear();
                for (const auto& d : effects) self->known.addType(d);
                self->scanning = false;
                self->scanMessage = juce::String(effects.size()) + " installed effects found";
                if (!failed.isEmpty()) self->scanMessage += "; " + juce::String(failed.size()) + " could not be scanned";
                if (auto xml = self->known.createXml()) xml->writeTo(settingsFile("Plugins.xml"));
                self->changed(); if (completion) completion({});
            }
        });
    });
}
void PluginHost::loadAsync(int b, int s, const juce::PluginDescription& description, Completion completion) {
    if (!valid(b, s)) { if (completion) completion("Invalid insert slot"); return; }
    core->load(b, s, describe(description), std::move(completion));
}
void PluginHost::retryLoad(int b, int s, Completion completion) {
    if (!valid(b, s)) { if (completion) completion("Invalid insert slot"); return; }
    const auto state = getSlotState(b, s);
    if (state.empty()) { if (completion) completion("Empty insert slot"); return; }
    core->load(b, s, state, std::move(completion));
}
void PluginHost::remove(int b, int s) {
    if (!valid(b, s)) return;
    auto& c = core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
    ++c.generation; c.node = nullptr; c.state = {}; c.lastAppliedBase64.clear();
    c.loading = false; c.error.clear(); core->publish(); core->changed();
}
void PluginHost::setBypass(int b, int s, bool bypass) {
    if (!valid(b, s)) return;
    core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)].state.bypass = bypass;
    core->publish(); core->changed();
}
bool PluginHost::isLoading(int b, int s) const { return valid(b, s) && core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)].loading; }
juce::String PluginHost::status(int b, int s) const {
    if (!valid(b, s)) return "Invalid insert slot";
    const auto& c = core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
    if (c.loading) return "Loading effect…";
    if (c.error.isNotEmpty()) return c.error;
    return c.node ? (c.state.bypass ? "Bypassed" : "Ready") : "Empty insert";
}
HostedPluginSlot PluginHost::getSlotState(int b, int s) const { return valid(b, s) ? core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)].state : HostedPluginSlot {}; }
void PluginHost::applyState(const WorkstationState& state, bool forceRestore) {
    // Listeners commonly synchronize every UI slot from getSlotState(). They
    // may therefore mutate the very WorkstationState passed into this method.
    // Own the full request before invoking any rack operation, and expose one
    // coherent notification only after all requested cells have been applied.
    std::array<std::array<HostedPluginSlot, slotCount>, busCount> desired;
    for (int b = 0; b < busCount; ++b)
        desired[static_cast<std::size_t>(b)] = b == masterBus ? state.masterPlugins
            : state.tracks[static_cast<std::size_t>(b)].plugins;
    Core::NotificationBatch batch(*core);
    for (int b = 0; b < busCount; ++b) for (int s = 0; s < slotCount; ++s) {
        const auto& wanted = desired[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
        auto& c = core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
        const bool heldSnapshot = wanted.stateBase64 == c.lastAppliedBase64;
        const bool capturedSnapshot = wanted.stateBase64 == c.state.stateBase64;
        if (!forceRestore && sameIdentity(c.state, wanted) && (heldSnapshot || capturedSnapshot)) {
            if (capturedSnapshot) c.lastAppliedBase64 = wanted.stateBase64;
            if (c.state.bypass != wanted.bypass) setBypass(b, s, wanted.bypass);
        } else if (wanted.empty()) remove(b, s);
        else core->load(b, s, wanted, {});
    }
}
void PluginHost::captureState(WorkstationState& state) {
    core->withSuspendedAudio([this, &state] {
        for (int b = 0; b < busCount; ++b) for (int s = 0; s < slotCount; ++s) {
            auto& c = core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)];
            if (c.node) {
                juce::MemoryBlock block; c.node->instance->getStateInformation(block);
                if (block.getSize() > workstationMaxPluginStateBytes) throw std::runtime_error("Plug-in state exceeds the 4 MiB session limit");
                c.state.stateBase64 = juce::Base64::toBase64(block.getData(), block.getSize()).toStdString();
            }
            auto& target = b == masterBus ? state.masterPlugins[static_cast<std::size_t>(s)]
                                        : state.tracks[static_cast<std::size_t>(b)].plugins[static_cast<std::size_t>(s)];
            target = c.state;
        }
    });
}
void PluginHost::showEditor(int b, int s) {
    if (!valid(b, s)) return;
    auto* n = core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)].node;
    if (!n) return;
    if (!n->editor) {
        auto* editor = n->instance->createEditorIfNeeded();
        if (!editor) editor = new juce::GenericAudioProcessorEditor(*n->instance);
        n->editor = std::make_unique<EditorWindow>(n->instance->getName(), editor);
    }
    n->editor->setVisible(true); n->editor->toFront(true);
}
bool PluginHost::installInstance(int b, int s, std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d, juce::String& error) {
    if (!valid(b, s)) { error = "Invalid insert slot"; return false; }
    ++core->cells[static_cast<std::size_t>(b)][static_cast<std::size_t>(s)].generation;
    const bool ok = core->install(b, s, std::move(p), describe(d), error); core->changed(); return ok;
}

namespace {
class ManagerView final : public juce::Component, private juce::Timer {
public:
    ManagerView(PluginHost& h, int b, int s) : host(h), bus(b), slot(s) {
        title.setText((bus == PluginHost::masterBus ? "MASTER" : "TRACK " + juce::String(bus + 1)) + " / PLUG-IN " + juce::String(slot + 1), juce::dontSendNotification);
        for (auto* c : std::array<juce::Component*, 9> { &title, &plugins, &scan, &load, &editor, &remove, &bypass, &retry, &info }) addAndMakeVisible(c);
        scan.setButtonText("Scan installed AU / VST3"); load.setButtonText("Load selected"); editor.setButtonText("Open editor");
        remove.setButtonText("Remove"); retry.setButtonText("Retry saved effect"); bypass.setButtonText("Bypass"); info.setJustificationType(juce::Justification::topLeft);
        scan.onClick = [this] { host.scanAsync(); refresh(); };
        load.onClick = [this] { const auto i = plugins.getSelectedId() - 1; if (juce::isPositiveAndBelow(i, descriptions.size())) host.loadAsync(bus, slot, descriptions.getReference(i)); refresh(); };
        editor.onClick = [this] { host.showEditor(bus, slot); };
        remove.onClick = [this] { host.remove(bus, slot); refresh(); };
        retry.onClick = [this] { host.retryLoad(bus, slot); refresh(); };
        bypass.onClick = [this] { host.setBypass(bus, slot, bypass.getToggleState()); };
        setSize(620, 300); refresh(); startTimer(300);
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xffc5bda6)); }
    void resized() override {
        auto r = getLocalBounds().reduced(16); title.setBounds(r.removeFromTop(28)); r.removeFromTop(8);
        scan.setBounds(r.removeFromTop(30)); r.removeFromTop(8); plugins.setBounds(r.removeFromTop(30)); r.removeFromTop(8);
        auto buttons = r.removeFromTop(30); load.setBounds(buttons.removeFromLeft(135)); editor.setBounds(buttons.removeFromLeft(135));
        remove.setBounds(buttons.removeFromLeft(115)); bypass.setBounds(buttons); r.removeFromTop(8);
        retry.setBounds(r.removeFromTop(28)); r.removeFromTop(10); info.setBounds(r);
    }
private:
    PluginHost& host; int bus, slot;
    juce::Label title, info; juce::ComboBox plugins;
    juce::TextButton scan, load, editor, remove, retry; juce::ToggleButton bypass;
    juce::Array<juce::PluginDescription> descriptions;
    juce::String registrySignature;
    void timerCallback() override { refresh(); }
    void refresh() {
        auto found = host.availablePlugins();
        juce::String signature;
        for (const auto& d : found) signature += d.createIdentifierString();
        if (signature != registrySignature) {
            registrySignature = signature; descriptions = found;
            plugins.clear(juce::dontSendNotification);
            for (int i = 0; i < descriptions.size(); ++i) {
                const auto& d = descriptions.getReference(i);
                plugins.addItem(d.name + " — " + d.manufacturerName + " (" + d.pluginFormatName + ")", i + 1);
            }
            if (!descriptions.isEmpty()) plugins.setSelectedId(1, juce::dontSendNotification);
        }
        plugins.setTextWhenNothingSelected("No installed effects discovered");
        scan.setEnabled(!host.isScanning()); load.setEnabled(!descriptions.isEmpty() && !host.isLoading(bus, slot));
        const auto state = host.getSlotState(bus, slot);
        editor.setEnabled(!state.empty() && (host.status(bus, slot) == "Ready" || host.status(bus, slot) == "Bypassed"));
        retry.setEnabled(!state.empty() && !host.isLoading(bus, slot));
        remove.setEnabled(!state.empty()); bypass.setToggleState(state.bypass, juce::dontSendNotification);
        info.setText(host.scanStatus() + "\n" + juce::String(state.name) + ": " + host.status(bus, slot), juce::dontSendNotification);
    }
};
}
std::unique_ptr<juce::Component> PluginHost::createPluginManagerView(int b, int s) { return std::make_unique<ManagerView>(*this, b, s); }
} // namespace samplify
