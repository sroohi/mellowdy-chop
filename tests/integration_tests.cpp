#include "audio/AudioEngine.h"
#include "audio/SessionIO.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

// C++ allocation/deallocation is probed on the calling thread. Linux additionally
// wraps direct C allocator calls from linked code, as described below.
namespace callbackHeapProbe {
thread_local bool enabled = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;
thread_local std::size_t cAllocatorCalls = 0;
thread_local std::size_t cFreeCalls = 0;
void allocated() noexcept { if (enabled) ++allocations; }
void deallocated(void* pointer) noexcept { if (enabled && pointer) ++deallocations; }
}

#if defined(SAMPLIFY_WRAP_C_ALLOCATIONS)
// GNU ld wrapping intercepts direct C-allocator references from this executable
// and its linked static libraries. Calls inside shared system libraries are not
// intercepted, so this is deliberately not a claim about every OS allocation.
extern "C" {
void* __real_malloc(std::size_t) noexcept;
void* __real_calloc(std::size_t, std::size_t) noexcept;
void* __real_realloc(void*, std::size_t) noexcept;
void __real_free(void*) noexcept;
int __real_posix_memalign(void**, std::size_t, std::size_t) noexcept;
void* __real_aligned_alloc(std::size_t, std::size_t) noexcept;
void* __wrap_malloc(std::size_t size) noexcept {
    if (callbackHeapProbe::enabled) ++callbackHeapProbe::cAllocatorCalls;
    return __real_malloc(size);
}
void* __wrap_calloc(std::size_t count, std::size_t size) noexcept {
    if (callbackHeapProbe::enabled) ++callbackHeapProbe::cAllocatorCalls;
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* pointer, std::size_t size) noexcept {
    if (callbackHeapProbe::enabled) ++callbackHeapProbe::cAllocatorCalls;
    return __real_realloc(pointer, size);
}
void __wrap_free(void* pointer) noexcept {
    if (callbackHeapProbe::enabled && pointer) ++callbackHeapProbe::cFreeCalls;
    __real_free(pointer);
}
int __wrap_posix_memalign(void** pointer, std::size_t alignment, std::size_t size) noexcept {
    if (callbackHeapProbe::enabled) ++callbackHeapProbe::cAllocatorCalls;
    return __real_posix_memalign(pointer, alignment, size);
}
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size) noexcept {
    if (callbackHeapProbe::enabled) ++callbackHeapProbe::cAllocatorCalls;
    return __real_aligned_alloc(alignment, size);
}
}
#endif

void* operator new(std::size_t size) {
    callbackHeapProbe::allocated();
    if (void* pointer = std::malloc(std::max<std::size_t>(1, size))) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { callbackHeapProbe::deallocated(pointer); std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    callbackHeapProbe::allocated();
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), std::max<std::size_t>(1, size)) == 0) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { ::operator delete(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { ::operator delete(pointer); }

namespace {
using namespace samplify;
constexpr double pi = 3.14159265358979323846;

struct Checks {
    int checked = 0;
    int failed = 0;
    void require(bool condition, const std::string& description) {
        ++checked;
        if (!condition) {
            ++failed;
            std::cerr << "FAIL: " << description << '\n';
        }
    }
    template <typename Fn>
    void rejects(Fn&& fn, const std::string& description) {
        bool rejected = false;
        try { fn(); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, description);
    }
    template <typename Fn>
    void section(const char* name, Fn&& fn) {
        const int before = failed;
        try { fn(); }
        catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
        std::cout << (before == failed ? "PASS: " : "FAIL: ") << name << '\n';
    }
};

struct TemporaryDirectory {
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("samplify-integration", {}, false);
    TemporaryDirectory() {
        if (path.createDirectory().failed()) throw std::runtime_error("Could not create temporary test directory");
    }
    ~TemporaryDirectory() { path.deleteRecursively(); }
};

AudioData tone(double frequency = 440.0, std::size_t frames = 22050) {
    AudioData data;
    data.sampleRate = 44100;
    data.channels.assign(2, std::vector<float>(frames));
    for (std::size_t i = 0; i < frames; ++i) {
        data.channels[0][i] = 0.25f * static_cast<float>(std::sin(2 * pi * frequency * static_cast<double>(i) / data.sampleRate));
        data.channels[1][i] = 0.15f * static_cast<float>(std::sin(2 * pi * frequency * static_cast<double>(i) / data.sampleRate + 0.3));
    }
    return data;
}

AudioData constant(float value, std::size_t frames = 22050) {
    auto data = tone(440, frames);
    for (auto& channel : data.channels) std::fill(channel.begin(), channel.end(), value);
    return data;
}

Project projectFor(const AudioData& audio) {
    Project project;
    project.sourceBpm = 120;
    project.projectBpm = 120;
    auto& pad = project.pads[0];
    pad.start = 0;
    pad.end = audio.frames();
    pad.attackMs = 0;
    pad.releaseMs = 50;
    project.pads[1] = pad;
    return project;
}

double mean(const AudioData& data) {
    if (!data.valid()) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0;
    for (const auto value : data.channels[0]) sum += value;
    return sum / static_cast<double>(data.frames());
}

double positiveCrossingFrequency(const AudioData& data) {
    if (!data.valid() || data.frames() < 20) return 0;
    const auto begin = data.frames() / 10;
    const auto end = data.frames() * 9 / 10;
    int crossings = 0;
    for (auto i = begin + 1; i < end; ++i)
        if (data.channels[0][i - 1] <= 0 && data.channels[0][i] > 0) ++crossings;
    return static_cast<double>(crossings) * data.sampleRate / static_cast<double>(end - begin);
}

float processBlock(AudioEngine& engine, juce::AudioBuffer<float>& buffer) {
    buffer.clear();
    engine.getNextAudioBlock(juce::AudioSourceChannelInfo(&buffer, 0, buffer.getNumSamples()));
    return buffer.getMagnitude(0, buffer.getNumSamples());
}

void testAudioFiles(Checks& c, const juce::File& directory) {
    auto original = tone();
    original.artist = "Test Artist";
    original.title = "Test Track";
    const auto file = directory.getChildFile("stereo.wav");
    AudioEngine::writeAudio(file, original);
    const auto loaded = AudioEngine::readAudio(file);
    c.require(loaded.valid() && loaded.channels.size() == 2, "WAV stereo import returns valid two-channel samples");
    c.require(loaded.frames() == original.frames() && loaded.sampleRate == original.sampleRate,
              "WAV export preserves frame count and sample rate");
    c.require(loaded.artist == original.artist && loaded.title == original.title,
              "WAV RIFF metadata roundtrip preserves actual artist and track title");
    double error = 0;
    for (std::size_t channel = 0; channel < loaded.channels.size(); ++channel)
        for (std::size_t i = 0; i < loaded.frames(); ++i)
            error = std::max(error, std::abs(static_cast<double>(loaded.channels[channel][i] - original.channels[channel][i])));
    c.require(error < 0.000002, "24-bit WAV roundtrip retains sample values");

    c.rejects([&] { (void) AudioEngine::readAudio(directory.getChildFile("missing.wav")); }, "Missing audio file is rejected");
    const auto damaged = directory.getChildFile("damaged.wav");
    damaged.replaceWithText("RIFF0000WAVEfmt ");
    c.rejects([&] { (void) AudioEngine::readAudio(damaged); }, "Truncated WAV header is rejected");
    const auto invalid = directory.getChildFile("invalid.wav");
    invalid.replaceWithText("This is not audio");
    c.rejects([&] { (void) AudioEngine::readAudio(invalid); }, "Unsupported audio bytes are rejected");
    auto nonfinite = original;
    nonfinite.channels[0][30] = std::numeric_limits<float>::quiet_NaN();
    c.rejects([&] { AudioEngine::writeAudio(file, nonfinite); }, "Export rejects non-finite samples");
    c.require(AudioEngine::readAudio(file).frames() == original.frames(), "Invalid export preserves existing WAV");

    auto stemA = constant(0, 512), stemB = stemA;
    for (std::size_t channel = 0; channel < stemA.channels.size(); ++channel) {
        for (std::size_t frame = 0; frame < stemA.frames(); ++frame) {
            const float sign = (frame % 2 == 0) ? 1.f : -1.f;
            stemA.channels[channel][frame] = sign * 1.2f;
            stemB.channels[channel][frame] = -sign * .8f;
        }
    }
    const auto cacheA = directory.getChildFile("cached-stem-a.wav");
    const auto cacheB = directory.getChildFile("cached-stem-b.wav");
    AudioEngine::writeFloatAudio(cacheA, stemA);
    AudioEngine::writeFloatAudio(cacheB, stemB);
    const auto restoredA = AudioEngine::readAudio(cacheA), restoredB = AudioEngine::readAudio(cacheB);
    c.require(restoredA.valid() && restoredA.channels == stemA.channels && restoredB.channels == stemB.channels,
              "32-bit float stem cache preserves finite samples above and below full scale without clipping");
    auto sourceMix = stemA;
    bool cancellation = true;
    for (std::size_t channel = 0; channel < stemA.channels.size(); ++channel) {
        for (std::size_t frame = 0; frame < stemA.frames(); ++frame) {
            sourceMix.channels[channel][frame] = stemA.channels[channel][frame] + stemB.channels[channel][frame];
            const float restoredSum = restoredA.channels[channel][frame] + restoredB.channels[channel][frame];
            cancellation &= std::abs(restoredSum - sourceMix.channels[channel][frame]) < .000001f
                && std::abs(std::abs(restoredSum) - .4f) < .000001f;
        }
    }
    c.require(cancellation, "Float cache retains cancelling stem sums of plus/minus 0.4 instead of clipped plus/minus 0.2");
    AudioEngine stemEngine;
    stemEngine.setSource(std::make_shared<AudioData>(sourceMix));
    stemEngine.setStem(0, std::make_shared<AudioData>(restoredA));
    stemEngine.setStem(1, std::make_shared<AudioData>(restoredB));
    const auto mixedExport = stemEngine.renderForExport(projectFor(sourceMix), 0);
    c.require(mixedExport.valid() && mixedExport.channels == sourceMix.channels,
              "Reloaded float cache stems reproduce the original in-memory cue mix exactly");
    const auto pcm = directory.getChildFile("clamped-pcm.wav");
    AudioEngine::writeAudio(pcm, stemA);
    const auto clipped = AudioEngine::readAudio(pcm);
    c.require(clipped.valid() && clipped.channels[0][0] > .9999f && clipped.channels[0][0] <= 1
        && clipped.channels[0][1] <= -.9999f && clipped.channels[0][1] >= -1,
        "User-facing 24-bit PCM export keeps its documented full-scale clipping behavior");
    auto badCache = stemA;
    badCache.channels[0][0] = std::numeric_limits<float>::quiet_NaN();
    c.rejects([&] { AudioEngine::writeFloatAudio(cacheA, badCache); }, "Float cache rejects non-finite audio");
    c.require(AudioEngine::readAudio(cacheA).channels == stemA.channels,
              "Rejected float cache write leaves prior valid cache file intact");
}

void testSessions(Checks& c, const juce::File& directory) {
    Session saved;
    saved.source = directory.getChildFile("stereo.wav");
    saved.stems[1] = directory.getChildFile("melody.wav");
    saved.project = projectFor(tone());
    saved.project.projectBpm = 98;
    saved.project.sourceBpm = 123;
    saved.project.projectKey = "G minor";
    saved.project.sourceKey = "C major";
    saved.project.padCount = 32;
    saved.project.mono = false;
    saved.project.syncTempo = true;
    saved.project.syncKey = true;
    saved.project.quantize = true;
    saved.project.playbackMode = CuePlaybackMode::Trigger;
    saved.project.velocityEnabled = true;
    saved.project.pitchTimeEnabled = false;
    saved.project.keyboardMode = true;
    saved.project.globalPitchSemitones = -3;
    saved.project.globalDetuneCents = 22;
    saved.project.glideMs = 420;
    saved.project.glideMode = GlideMode::Sustain;
    saved.project.stemMute = {false, true, false, true};
    saved.project.beatgridEnabled = false;
    saved.project.tempoMarkers = {{0, 123}, {7000, 98}};
    auto& pad = saved.project.pads[31];
    pad.start = 441;
    pad.end = 4410;
    pad.levelDb = -9.5f;
    pad.pitchSemitones = 7;
    pad.detuneCents = -12;
    pad.speedRatio = 1.5f;
    pad.filter = 0.73f;
    pad.attackMs = 24;
    pad.releaseMs = 400;
    pad.reverse = true;
    pad.hasEnd = false;
    pad.favorite = true;
    pad.name = "A favorite cue";
    pad.colour = 0xffc15d29;
    pad.outputChannel = 5;
    pad.stemMute = { true, false, true, false };
    const auto file = directory.getChildFile("session.samplify");
    saveSession(file, saved);
    const auto restored = loadSession(file);
    c.require(restored.source == saved.source && restored.stems == saved.stems, "Session resolves relative source and stem references");
    c.require(restored.project.projectBpm == 98 && restored.project.sourceBpm == 123
        && restored.project.padCount == 32 && !restored.project.mono && restored.project.quantize
        && restored.project.syncTempo && restored.project.syncKey, "Session restores global tempo, mode, sync and pad count");
    c.require(restored.project.projectKey == "G minor" && restored.project.sourceKey == "C major", "Session restores source/project keys");
    const auto& readPad = restored.project.pads[31];
    c.require(readPad.start == pad.start && readPad.end == pad.end && readPad.levelDb == pad.levelDb
        && readPad.pitchSemitones == pad.pitchSemitones && readPad.detuneCents == pad.detuneCents
        && readPad.speedRatio == pad.speedRatio && readPad.filter == pad.filter
        && readPad.attackMs == pad.attackMs && readPad.releaseMs == pad.releaseMs
        && readPad.reverse && readPad.stemMute == pad.stemMute && readPad.hasEnd == pad.hasEnd
        && readPad.favorite && readPad.name == pad.name && readPad.colour == pad.colour
        && readPad.outputChannel == pad.outputChannel, "Session restores every cue edit, color, name, favorite and routing");
    c.require(restored.project.playbackMode == CuePlaybackMode::Trigger && restored.project.velocityEnabled
        && !restored.project.pitchTimeEnabled && restored.project.keyboardMode
        && restored.project.globalPitchSemitones == -3 && restored.project.globalDetuneCents == 22
        && restored.project.glideMs == 420 && restored.project.glideMode == GlideMode::Sustain
        && restored.project.stemMute == saved.project.stemMute && !restored.project.beatgridEnabled,
        "Session restores global performance, glide, stems and cleared beatgrid state");
    c.require(restored.project.tempoMarkers.size() == 2 && restored.project.tempoMarkers[1].frame == 7000
        && restored.project.tempoMarkers[1].bpm == 98, "Session restores edited piecewise tempo markers");
    const auto originalText = file.loadFileAsString();

    const auto malformed = directory.getChildFile("malformed.samplify");
    malformed.replaceWithText("{broken JSON");
    c.rejects([&] { (void) loadSession(malformed); }, "Malformed session JSON is rejected");
    malformed.replaceWithText("{}");
    c.rejects([&] { (void) loadSession(malformed); }, "Missing session schema is rejected");
    auto badVersion = juce::JSON::parse(originalText);
    badVersion.getDynamicObject()->setProperty("version", 999);
    malformed.replaceWithText(juce::JSON::toString(badVersion));
    c.rejects([&] { (void) loadSession(malformed); }, "Unsupported session version is rejected");
    malformed.replaceWithText(originalText.replace("\"padCount\": 32", "\"padCount\": 17"));
    c.rejects([&] { (void) loadSession(malformed); }, "Unsupported pad count is rejected");
    auto invalid = saved;
    invalid.project.pads[0].speedRatio = -1;
    c.rejects([&] { saveSession(file, invalid); }, "Session save validates invalid state before replacement");
    c.require(file.loadFileAsString() == originalText, "Failed session save preserves prior document");

    // Earlier Samplify sessions stored a duration multiplier. Its inverse is
    // required by the reference cue-speed convention (2x means half duration).
    auto legacy = juce::JSON::parse(originalText);
    legacy.getDynamicObject()->setProperty("version", 1);
    auto oldProject = legacy.getProperty("project", juce::var());
    for (const auto* field : {"playbackMode", "velocityEnabled", "pitchTimeEnabled", "keyboardMode",
                             "globalPitchSemitones", "globalDetuneCents", "glideMs", "glideMode",
                             "stemMute", "tempoMarkers", "beatgridEnabled"})
        oldProject.getDynamicObject()->removeProperty(field);
    auto oldPads = oldProject.getProperty("pads", juce::var());
    for (int index = 0; index < oldPads.size(); ++index) {
        auto* object = oldPads[index].getDynamicObject();
        object->setProperty("timeRatio", 2.0);
        object->setProperty("releaseMs", 150.0);
        for (const auto* field : {"speedRatio", "hasEnd", "favorite", "name", "colour", "outputChannel"})
            object->removeProperty(field);
    }
    const auto legacyFile = directory.getChildFile("legacy.samplify");
    legacyFile.replaceWithText(juce::JSON::toString(legacy));
    const auto migrated = loadSession(legacyFile);
    c.require(std::abs(migrated.project.pads[31].speedRatio - .5f) < .00001f,
              "Version-one duration multiplier migrates to reciprocal cue speed");
    c.require(migrated.project.playbackMode == CuePlaybackMode::Hold && !migrated.project.pads[31].favorite
        && migrated.project.beatgridEnabled,
              "Legacy session receives explicit default performance and cue metadata");
}

void testStemsAndExport(Checks& c) {
    auto source = std::make_shared<AudioData>(constant(0.55f));
    auto p = projectFor(*source);
    p.pads[0].releaseMs = 0;
    AudioEngine engine;
    engine.setSource(source);
    c.require(std::abs(mean(engine.renderForExport(p, 0)) - 0.55) < 0.001, "Without stems export uses original source");
    engine.setStem(0, std::make_shared<AudioData>(constant(0.1f)));
    engine.setStem(1, std::make_shared<AudioData>(constant(0.2f)));
    c.require(std::abs(mean(engine.renderForExport(p, 0)) - 0.3) < 0.001, "Imported stems replace source and sum aligned audio");
    p.pads[0].stemMute[0] = true;
    c.require(std::abs(mean(engine.renderForExport(p, 0)) - 0.2) < 0.001, "Per-pad stem mute removes only selected stem");
    p.pads[0].stemMute[1] = true;
    c.require(std::abs(mean(engine.renderForExport(p, 0))) < 0.000001, "All imported stems muted exports silence");
    engine.clearStems();
    c.require(std::abs(mean(engine.renderForExport(p, 0)) - 0.55) < 0.001, "Clearing imported stems restores original mix");
    auto mismatched = std::make_shared<AudioData>(constant(0.1f, source->frames() - 1));
    c.rejects([&] { engine.setStem(0, mismatched); }, "Stem with mismatched frame count is rejected");
    mismatched = std::make_shared<AudioData>(*source);
    mismatched->sampleRate = 48000;
    c.rejects([&] { engine.setStem(0, mismatched); }, "Stem with mismatched sample rate is rejected");
    mismatched = std::make_shared<AudioData>(*source);
    mismatched->channels.resize(1);
    c.rejects([&] { engine.setStem(0, mismatched); }, "Stem with mismatched channels is rejected");
    c.rejects([&] { engine.setStem(4, source); }, "Unknown stem slot is rejected");
    c.require(engine.renderForExport(p, -1).frames() == 0, "Invalid export pad index is harmless");

    source = std::make_shared<AudioData>(tone(440, 44100));
    engine.setSource(source);
    p = projectFor(*source);
    p.syncTempo = false;
    p.projectBpm = 180;
    const auto stretched = engine.renderForExport(p, 0);
    c.require(stretched.valid() && std::abs(static_cast<double>(stretched.frames()) - 29400.0) < 3,
              "Current BPM exports expected independent duration even without host sync");
    c.require(std::abs(positiveCrossingFrequency(stretched) - 440) < 12, "Current BPM preserves source pitch");
    p.syncTempo = false;
    p.projectBpm = p.sourceBpm;
    p.pads[0].pitchSemitones = 12;
    const auto pitched = engine.renderForExport(p, 0);
    c.require(pitched.valid() && std::abs(static_cast<double>(pitched.frames()) - 44100.0) < 3,
              "Pitch edit preserves pad duration");
    c.require(std::abs(positiveCrossingFrequency(pitched) - 880) < 24, "Pitch octave edit changes rendered frequency");
    p.pads[0].pitchSemitones = 0;
    p.syncKey = true;
    p.sourceKey = "C major";
    p.projectKey = "D major";
    const auto transposed = engine.renderForExport(p, 0);
    c.require(std::abs(positiveCrossingFrequency(transposed) - 440 * std::pow(2.0, 2.0 / 12.0)) < 16,
              "Project key sync applies root semitone offset");

    source = std::make_shared<AudioData>(constant(0));
    for (auto& channel : source->channels)
        for (std::size_t i = 0; i < channel.size(); ++i) channel[i] = static_cast<float>(i) / static_cast<float>(channel.size());
    engine.setSource(source);
    p = projectFor(*source);
    p.pads[0].reverse = true;
    p.pads[0].releaseMs = 0;
    const auto reversed = engine.renderForExport(p, 0);
    c.require(reversed.valid() && reversed.channels[0].front() > 0.99f && reversed.channels[0].back() < 0.001f,
              "Reverse export changes cue audio order");

    source = std::make_shared<AudioData>(constant(.2f, 44100));
    engine.setSource(source);
    p = projectFor(*source);
    p.pads[0].attackMs = 20;
    p.pads[0].releaseMs = 80;
    const auto heldExport = engine.renderForExport(p, 0);
    c.require(heldExport.valid() && heldExport.frames() == source->frames()
        && heldExport.channels[0].front() == 0 && std::abs(heldExport.channels[0].back() - .2f) < .001f,
        "Hold export keeps full finite cue and attack without inventing an external note-off release");
    p.playbackMode = CuePlaybackMode::Trigger;
    const auto triggeredExport = engine.renderForExport(p, 0);
    c.require(triggeredExport.valid() && std::abs(static_cast<double>(triggeredExport.frames()) - 4410) <= 1
        && std::abs(triggeredExport.channels[0].back()) < .001f,
        "Trigger export ends after attack plus release instead of exporting remaining source tail");
    p.playbackMode = CuePlaybackMode::Hold;
    p.pitchTimeEnabled = false;
    p.projectBpm = 240;
    p.pads[0].attackMs = 0;
    p.pads[0].pitchSemitones = 12;
    const auto bypass = engine.renderForExport(p, 0);
    c.require(bypass.valid() && std::abs(static_cast<double>(bypass.frames()) - 11025) <= 1,
        "Pitch/time bypass combines Current BPM with octave varispeed in exported duration");
}

float processMany(AudioEngine& engine, juce::AudioBuffer<float>& buffer, int blocks) {
    float peak = 0;
    for (int i = 0; i < blocks; ++i) peak = std::max(peak, processBlock(engine, buffer));
    return peak;
}

void stopAndDrain(AudioEngine& engine, juce::AudioBuffer<float>& buffer) {
    engine.stop();
    processMany(engine, buffer, 4);
}

void testTriggerStopChronology(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 44100));
    const auto p = projectFor(*source);
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    juce::AudioBuffer<float> output(2, 512);
    const auto before = engine.playbackSnapshot().triggerSerial;
    engine.triggerPad(0);
    engine.stop();
    processMany(engine, output, 4);
    const auto stopped = engine.playbackSnapshot();
    c.require(stopped.lastTriggeredPad == -1,
              "Trigger followed by Stop in one callback clears stale cue-selection notification");
    c.require(stopped.triggerSerial > before,
              "Processed Stop preserves monotonic trigger history rather than resetting serial");
    engine.stop();
    engine.triggerPad(1);
    processBlock(engine, output);
    const auto restarted = engine.playbackSnapshot();
    c.require(restarted.lastTriggeredPad == 1 && restarted.triggerSerial > stopped.triggerSerial
        && engine.activeVoiceCount() == 1,
        "Stop followed by Trigger in one callback retains newer cue notification and playback");
    stopAndDrain(engine, output);
}

void testPlaybackAndMidi(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 176400));
    auto p = projectFor(*source);
    p.pads[0].start = 1000;
    p.pads[0].attackMs = 5;
    p.pads[0].releaseMs = 30;
    p.pads[1] = p.pads[0];
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    engine.prepareToPlay(512, 44100);
    engine.setMasterGain(1);
    juce::AudioBuffer<float> output(2, 512);
    c.require(processBlock(engine, output) == 0 && engine.activeVoiceCount() == 0, "Untriggered callback produces silence");
    engine.triggerPad(0);
    c.require(processMany(engine, output, 4) > 0.05f && engine.activeVoiceCount() == 1,
              "Hold trigger produces audible stereo buffer without hardware");
    c.require(std::isfinite(engine.peakLevel()) && engine.peakLevel() > 0, "Meter reports finite playback peak");
    c.require((engine.playbackSnapshot().activeCueMask & 1u) != 0 && engine.playbackSnapshot().lastTriggeredPad == 0,
              "Snapshot identifies sounding selected cue");
    engine.releasePad(0);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1, "Hold release begins when trigger is released rather than at attack end");
    processMany(engine, output, 4);
    c.require(engine.activeVoiceCount() == 0 && engine.playbackSnapshot().sourceFrame == p.pads[0].start,
              "Hold release completion silences cue and returns playhead to cue start");

    p.pads[0].end = p.pads[0].start + 4410;
    p.pads[0].hasEnd = true;
    engine.setLiveProject(p);
    engine.triggerPad(0);
    processMany(engine, output, 12);
    c.require(engine.activeVoiceCount() == 1 && engine.playbackSnapshot().sourceFrame == p.pads[0].end,
              "Held cue with set length pauses at its end until trigger release");
    c.require(processBlock(engine, output) == 0, "Held cue at set end emits silence");
    engine.releasePad(0);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 0 && engine.playbackSnapshot().sourceFrame == p.pads[0].start,
              "Releasing held cue at set end returns playhead to start");

    p.pads[0].end = source->frames();
    p.playbackMode = CuePlaybackMode::Trigger;
    p.pads[0].attackMs = 30;
    p.pads[0].releaseMs = 100;
    engine.setLiveProject(p);
    engine.triggerPad(0);
    const float triggerPeak = processMany(engine, output, 6);
    engine.releasePad(0);
    processMany(engine, output, 3);
    c.require(triggerPeak > 0.05f && engine.activeVoiceCount() == 1,
              "Trigger mode ignores note-off while attack/release envelope is running");
    processMany(engine, output, 6);
    const auto triggerEnd = engine.playbackSnapshot().sourceFrame;
    c.require(engine.activeVoiceCount() == 0 && triggerEnd > p.pads[0].start && triggerEnd < p.pads[0].end,
              "Trigger mode begins release after attack and finishes before a longer cue end");
    processMany(engine, output, 2);
    c.require(engine.playbackSnapshot().sourceFrame == triggerEnd,
              "Trigger envelope completion leaves playhead stationary instead of returning to start");

    p.playbackMode = CuePlaybackMode::Hold;
    p.pads[0].attackMs = 0;
    p.pads[0].releaseMs = 30;
    p.quantize = true;
    engine.setLiveProject(p);
    engine.triggerPad(0);
    c.require(processBlock(engine, output) > 0.05f,
              "Quantize affects cue editing and does not delay playback trigger to a beat");
    stopAndDrain(engine, output);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 24, static_cast<juce::uint8>(100)));
    processBlock(engine, output);
    c.require(engine.playbackSnapshot().lastTriggeredPad == 0 && engine.activeVoiceCount() == 1,
              "Default first displayed MIDI note 24 triggers Cue 1");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 24));
    processMany(engine, output, 5);

    engine.seekSource(5000);
    processBlock(engine, output);
    c.require(engine.playbackSnapshot().sourceFrame == 5000 && !engine.playbackSnapshot().sourcePlaying,
              "Source seek positions paused transport without sounding a cue");
    engine.playSource();
    c.require(processMany(engine, output, 4) > 0.05f && engine.playbackSnapshot().sourcePlaying,
              "Source Play auditions actual audio independently of cue triggers");
    c.require(engine.playbackSnapshot().sourceFrame > 5000, "Source audition advances its playhead");
    engine.pauseSource();
    processBlock(engine, output);
    const auto pausedFrame = engine.playbackSnapshot().sourceFrame;
    c.require(!engine.playbackSnapshot().sourcePlaying && processBlock(engine, output) == 0,
              "Source Pause stops audition audio");
    c.require(engine.playbackSnapshot().sourceFrame == pausedFrame, "Paused source transport keeps its current position");
    engine.seekSource(9000);
    engine.playSource();
    processBlock(engine, output);
    c.require(engine.playbackSnapshot().sourceFrame >= 9000 && engine.playbackSnapshot().sourceFrame < 10000,
              "Seek followed by Play starts audition near requested source frame");
    engine.pauseSource();
    processBlock(engine, output);

    engine.seekSource(7000);
    processBlock(engine, output);
    const auto eventBefore = engine.playbackSnapshot().pendingCueSerial;
    engine.learnMidiPad(15);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 80, static_cast<juce::uint8>(100)));
    processBlock(engine, output);
    const auto pending = engine.playbackSnapshot();
    c.require(pending.pendingCuePad == 15 && pending.pendingCueFrame == 7000 && pending.pendingCueSerial > eventBefore,
              "MIDI empty pad emits a cue-assignment event at current playhead");
    c.require(std::abs(pending.pendingCueVelocity - 100.0f / 127) < .001f && pending.pendingCueSemitoneOffset == 0,
              "Empty-cue event retains velocity and pitch for assignment-and-trigger completion");
    c.require(engine.activeVoiceCount() == 0 && engine.midiStatus().contains("80"),
              "Learning an empty cue maps its note without fabricating existing audio");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 80));
    processBlock(engine, output);
    c.require(engine.playbackSnapshot().pendingCueSerial == pending.pendingCueSerial,
              "MIDI note-off cannot duplicate cue-assignment event");

    p.quantize = false;
    p.mono = false;
    for (auto& cue : p.pads) cue = p.pads[0];
    engine.setLiveProject(p);
    for (int cue = 0; cue < 32; ++cue) engine.triggerPad(cue);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 32 && engine.playbackSnapshot().activeCueMask == 0xffffffffu,
              "Poly mode supports all 32 independently active cue voices");
    engine.setMono(true);
    engine.triggerPad(4);
    processMany(engine, output, 3);
    c.require(engine.activeVoiceCount() == 1 && engine.playbackSnapshot().activeCueMask == (1u << 4),
              "Mono retrigger crossfades prior voices to one selected cue");
    stopAndDrain(engine, output);

    p.mono = false;
    p.keyboardMode = true;
    engine.setLiveProject(p);
    engine.setKeyboardMode(true, 0);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)));
    c.require(processMany(engine, output, 8) > 0.05f && engine.activeVoiceCount() == 2,
              "Keyboard polyphony plays two pitches of selected cue");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
    processMany(engine, output, 5);
    c.require(engine.activeVoiceCount() == 1, "Keyboard note-off releases only its own pitch");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::allNotesOff(1));
    processMany(engine, output, 4);
    c.require(engine.activeVoiceCount() == 0, "MIDI all-notes-off silences cue voices");

    // Deliberately exceed the bounded command FIFO; panic stop must survive.
    for (int command = 0; command < 1100; ++command) engine.triggerPad(command % 32);
    engine.stop();
    processMany(engine, output, 4);
    c.require(engine.activeVoiceCount() == 0, "Panic stop is honored when producer burst overflows control queue");
    c.require(processBlock(engine, output) == 0, "Stopped callback produces silence");
    juce::AudioBuffer<float> region(2, 1024);
    for (int channel = 0; channel < region.getNumChannels(); ++channel)
        for (int sample = 0; sample < region.getNumSamples(); ++sample) region.setSample(channel, sample, .2f);
    engine.getNextAudioBlock(juce::AudioSourceChannelInfo(&region, 128, 512));
    c.require(region.getSample(0, 0) == .2f && region.getSample(1, 1023) == .2f
        && region.getMagnitude(128, 512) == 0, "Callback clears only requested output region");
    engine.releaseResources();
    c.require(engine.activeVoiceCount() == 0 && engine.peakLevel() == 0, "Resource release clears voices and meter");
}

void testLiveControlsVelocityAndKeyboard(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 132300));
    auto p = projectFor(*source);
    p.pads[0].attackMs = 0;
    p.pads[0].releaseMs = 40;
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    engine.prepareToPlay(512, 44100);
    engine.setMasterGain(1);
    juce::AudioBuffer<float> output(2, 512);
    engine.triggerPad(0);
    processMany(engine, output, 8);
    const float initialPeak = processMany(engine, output, 4);
    p.pads[0].levelDb = -12;
    engine.setLiveProject(p); // No cache rebuild and no new note.
    const float reducedPeak = processMany(engine, output, 4);
    c.require(initialPeak > .1f && reducedPeak > .02f && reducedPeak < initialPeak * .3f,
              "Live cue gain affects already sounding voice without preparing pads or retriggering");
    stopAndDrain(engine, output);
    p.pads[0].levelDb = 0;
    p.velocityEnabled = true;
    engine.setLiveProject(p);
    engine.triggerPad(0, 0, 1);
    processMany(engine, output, 4);
    const auto loud = processMany(engine, output, 4);
    stopAndDrain(engine, output);
    engine.triggerPad(0, 0, .25f);
    processMany(engine, output, 4);
    const auto soft = processMany(engine, output, 4);
    c.require(loud > .1f && soft > .01f && std::abs(soft / loud - .25f) < .04f,
              "Enabled velocity scales cue audio by incoming note velocity");
    stopAndDrain(engine, output);
    p.velocityEnabled = false;
    engine.setLiveProject(p);
    engine.triggerPad(0, 0, .25f);
    processMany(engine, output, 4);
    const auto disabled = processMany(engine, output, 4);
    c.require(std::abs(disabled / loud - 1) < .04f, "Velocity-disabled playback ignores note velocity");
    stopAndDrain(engine, output);

    auto vocals = std::make_shared<AudioData>(constant(.1f, source->frames()));
    auto melody = std::make_shared<AudioData>(constant(.2f, source->frames()));
    engine.setStem(0, vocals);
    engine.setStem(1, melody);
    engine.setLiveProject(p);
    engine.triggerPad(0);
    processMany(engine, output, 4);
    const auto stemSum = processMany(engine, output, 2);
    p.stemMute[0] = true;
    engine.setLiveProject(p);
    const auto withoutVocals = processMany(engine, output, 2);
    c.require(std::abs(stemSum - .3f) < .002f && std::abs(withoutVocals - .2f) < .002f,
              "Global stem mute updates current cue mix in the next callback without retrigger");
    p.pads[0].stemMute[1] = true;
    engine.setLiveProject(p);
    c.require(processMany(engine, output, 2) < .00001f, "Live cue stem mask combines with global stem mask");
    stopAndDrain(engine, output);
    p.stemMute = {};
    p.pads[0].stemMute = {false, true, false, false};
    p.pads[1].stemMute = {true, false, false, false};
    engine.setLiveProject(p);
    engine.setKeyboardMode(false, 0);
    engine.seekSource(1000);
    engine.playSource();
    const auto selectedVocals = processMany(engine, output, 4);
    c.require(std::abs(selectedVocals - .1f) < .002f && engine.playbackSnapshot().sourcePlaying,
              "Source audition honors selected cue stem mask together with waveform mix");
    engine.setKeyboardMode(false, 1);
    const auto selectedMelody = processMany(engine, output, 2);
    c.require(std::abs(selectedMelody - .2f) < .002f && engine.playbackSnapshot().sourcePlaying,
              "Changing selected cue updates active source audition stems without pause or retrigger");
    engine.pauseSource();
    processBlock(engine, output);
    engine.clearStems();
    p.stemMute = {};
    p.pads[0].stemMute = {};
    p.pads[0].outputChannel = 2;
    engine.setLiveProject(p);
    juce::AudioBuffer<float> multichannel(6, 512);
    engine.triggerPad(0);
    processMany(engine, multichannel, 4);
    c.require(multichannel.getMagnitude(0, 0, 512) == 0 && multichannel.getMagnitude(1, 0, 512) == 0
        && multichannel.getMagnitude(4, 0, 512) > .05f && multichannel.getMagnitude(5, 0, 512) > .05f,
              "Cue output routing sends stereo audio to selected hardware pair when available");
    stopAndDrain(engine, output);

    // Independent real-time keyboard transposition must not consume source at double speed.
    p.pads[0].outputChannel = 0;
    p.pads[0].end = 44100;
    p.pads[0].hasEnd = true;
    p.keyboardMode = true;
    p.mono = false;
    engine.setLiveProject(p);
    auto capture = [&](float offset) {
        stopAndDrain(engine, output);
        engine.triggerPad(0, offset);
        AudioData captured;
        captured.sampleRate = 44100;
        captured.channels.resize(1);
        for (int block = 0; block < 50; ++block) {
            processBlock(engine, output);
            captured.channels[0].insert(captured.channels[0].end(), output.getReadPointer(0), output.getReadPointer(0) + 512);
        }
        return captured;
    };
    const auto root = capture(0);
    const auto rootFrame = engine.playbackSnapshot().sourceFrame;
    const auto octave = capture(12);
    const auto octaveFrame = engine.playbackSnapshot().sourceFrame;
    c.require(std::abs(positiveCrossingFrequency(root) - 220) < 8 && std::abs(positiveCrossingFrequency(octave) - 440) < 15,
              "Keyboard octave changes real-time audio pitch");
    c.require(std::abs(static_cast<double>(rootFrame) - static_cast<double>(octaveFrame)) < 1536
        && engine.activeVoiceCount() == 1, "Keyboard pitch preserves source progression and cue duration");
    p.globalPitchSemitones = -12;
    engine.setLiveProject(p);
    AudioData livePitch;
    livePitch.sampleRate = 44100;
    livePitch.channels.resize(1);
    processMany(engine, output, 6);
    for (int block = 0; block < 18; ++block) {
        processBlock(engine, output);
        livePitch.channels[0].insert(livePitch.channels[0].end(), output.getReadPointer(0), output.getReadPointer(0) + 512);
    }
    c.require(std::abs(positiveCrossingFrequency(livePitch) - 220) < 18 && engine.activeVoiceCount() == 1,
              "Live global key shift updates sounding keyboard voice without a new trigger");
    stopAndDrain(engine, output);
}

void testGlideAndMetronome(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 132300));
    auto p = projectFor(*source);
    p.pads[0].start = 1000;
    p.pads[0].releaseMs = 40;
    p.keyboardMode = true;
    p.mono = true;
    p.glideMs = 100;
    p.glideMode = GlideMode::Sustain;
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    engine.prepareToPlay(512, 44100);
    engine.setMasterGain(1);
    juce::AudioBuffer<float> output(2, 512);
    engine.setKeyboardMode(true, 0);
    engine.triggerPad(0, 0);
    processMany(engine, output, 20);
    const auto sustainBefore = engine.playbackSnapshot().sourceFrame;
    engine.triggerPad(0, 12);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1 && engine.playbackSnapshot().sourceFrame > sustainBefore,
              "Sustain glide reuses mono keyboard voice without restarting cue playhead");
    processMany(engine, output, 18);
    AudioData shifted;
    shifted.sampleRate = 44100;
    shifted.channels.resize(1);
    for (int block = 0; block < 20; ++block) {
        processBlock(engine, output);
        shifted.channels[0].insert(shifted.channels[0].end(), output.getReadPointer(0), output.getReadPointer(0) + 512);
    }
    c.require(std::abs(positiveCrossingFrequency(shifted) - 440) < 20,
              "Sustain glide reaches the new keyboard note pitch");
    engine.releasePad(0, 0);
    processMany(engine, output, 5);
    c.require(engine.activeVoiceCount() == 1, "Releasing preceding glide note does not stop current sustained note");
    engine.releasePad(0, 12);
    processMany(engine, output, 5);
    c.require(engine.activeVoiceCount() == 0, "Releasing current sustained glide note starts Hold release");
    p.glideMode = GlideMode::Trigger;
    engine.setLiveProject(p);
    engine.triggerPad(0, 0);
    processMany(engine, output, 20);
    const auto triggerBefore = engine.playbackSnapshot().sourceFrame;
    engine.triggerPad(0, 12);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1 && engine.playbackSnapshot().sourceFrame < triggerBefore
        && engine.playbackSnapshot().sourceFrame >= p.pads[0].start,
              "Trigger glide starts a new mono keyboard cue while pitch transitions");
    stopAndDrain(engine, output);

    source = std::make_shared<AudioData>(constant(0, 88200));
    p = projectFor(*source);
    engine.setSource(source);
    engine.preparePads(p);
    engine.seekSource(0);
    engine.setMetronome(true);
    engine.playSource();
    c.require(processMany(engine, output, 4) > .05f && engine.playbackSnapshot().sourcePlaying,
              "Metronome generates real click audio while auditioning a silent source");
    engine.setMetronome(false);
    c.require(processMany(engine, output, 4) == 0, "Disabling metronome removes click from silent source audition");
    engine.pauseSource();
    processBlock(engine, output);
    engine.setMetronome(true);
    engine.triggerPad(0);
    c.require(processMany(engine, output, 48) == 0 && engine.activeVoiceCount() == 1,
              "Metronome is source-audition only and never sounds during cue playback");
    stopAndDrain(engine, output);
}

void testOpenEndedCueCapacity(Checks& c) {
    auto source = std::make_shared<AudioData>();
    source->sampleRate = 44100;
    source->channels.assign(2, std::vector<float>(44100 * 120, .01f));
    Project p;
    for (std::size_t i = 0; i < p.pads.size(); ++i) {
        auto& cue = p.pads[i];
        cue.start = i * 1000;
        cue.end = source->frames();
        cue.hasEnd = false;
    }
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    c.require(engine.activeVoiceCount() == 0,
              "A two-minute stereo source accepts 32 open-ended cues without eager tail rendering or playback");
}

void testPendingCueEventQueue(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 44100));
    Project p;
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    juce::AudioBuffer<float> output(2, 512);
    // Empty cues do not require DSP processor preparation or an audio device.
    engine.seekSource(4242);
    for (int note = 24; note < 56; ++note)
        engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(64 + note - 24)));
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 24));
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 55));
    processBlock(engine, output);
    int count = 0;
    std::uint32_t mask = 0;
    bool fields = true;
    CueAssignment event;
    while (engine.popPendingCue(event)) {
        fields &= event.pad >= 0 && event.pad < 32 && event.frame == 4242 && event.semitoneOffset == 0;
        if (event.pad >= 0 && event.pad < 32) {
            mask |= std::uint32_t(1) << event.pad;
            fields &= std::abs(event.velocity - (64.f + event.pad) / 127.f) < .001f;
            fields &= event.held == (event.pad != 0 && event.pad != 31);
        }
        ++count;
    }
    c.require(count == 32 && mask == 0xffffffffu,
              "All 32 simultaneous empty MIDI cues are queued instead of losing all but last event");
    c.require(fields, "Queued cue assignments retain frame, note velocity and note-off-before-consumption state");
    c.require(!engine.popPendingCue(event), "Consumed assignment queue is empty");

    engine.triggerPad(1);
    processBlock(engine, output);
    engine.setSource(std::make_shared<AudioData>(tone(330, 44100)));
    engine.preparePads(p);
    c.require(!engine.popPendingCue(event), "Changing source discards pending assignments from prior source generation");
    engine.seekSource(9000);
    engine.triggerPad(2);
    processBlock(engine, output);
    c.require(engine.popPendingCue(event) && event.pad == 2 && event.frame == 9000,
              "New source generation can queue its own cue assignments");

    engine.setKeyboardMode(true, 0);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(70)));
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
    processBlock(engine, output);
    CueAssignment root, octave;
    c.require(engine.popPendingCue(root) && engine.popPendingCue(octave)
        && root.pad == 0 && root.semitoneOffset == 0 && root.held
        && octave.pad == 0 && octave.semitoneOffset == 12 && !octave.held,
        "Empty keyboard cue chord retains independent pitch and held state for each assignment");

    p.playbackMode = CuePlaybackMode::Trigger;
    engine.setLiveProject(p);
    engine.triggerPad(4);
    processBlock(engine, output);
    engine.stop(); // UI panic must cancel assignments before the next audio tick.
    c.require(!engine.popPendingCue(event),
              "Stop immediately cancels queued Trigger-mode assignments before UI can retrigger them");
    processBlock(engine, output);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 29, static_cast<juce::uint8>(100)));
    processBlock(engine, output);
    c.require(engine.popPendingCue(event) && event.pad == 5,
              "After processed Stop a new empty MIDI cue assignment is accepted normally");
}

struct CallbackHeapCalls {
    std::size_t allocations = 0, deallocations = 0, cAllocatorCalls = 0, cFreeCalls = 0;
    bool empty() const { return allocations == 0 && deallocations == 0 && cAllocatorCalls == 0 && cFreeCalls == 0; }
};

CallbackHeapCalls probeCallbackHeap(AudioEngine& engine, juce::AudioBuffer<float>& output, int blocks) {
    callbackHeapProbe::allocations = callbackHeapProbe::deallocations = 0;
    callbackHeapProbe::cAllocatorCalls = callbackHeapProbe::cFreeCalls = 0;
    struct Scope {
        Scope() { callbackHeapProbe::enabled = true; }
        ~Scope() { callbackHeapProbe::enabled = false; }
    };
    {
        const Scope scope;
        for (int block = 0; block < blocks; ++block)
            engine.getNextAudioBlock(juce::AudioSourceChannelInfo(&output, 0, output.getNumSamples()));
    }
    return {callbackHeapProbe::allocations, callbackHeapProbe::deallocations,
            callbackHeapProbe::cAllocatorCalls, callbackHeapProbe::cFreeCalls};
}

void testCallbackCppHeap(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 176400));
    auto p = projectFor(*source);
    p.pads[0].releaseMs = 80;
    p.keyboardMode = true;
    p.glideMs = 100;
    p.glideMode = GlideMode::Sustain;
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    engine.prepareToPlay(512, 44100);
    juce::AudioBuffer<float> output(2, 512);
    engine.triggerPad(0);
    const auto neutral = probeCallbackHeap(engine, output, 4);
    c.require(neutral.empty(),
              "Prepared neutral callback performs no calling-thread allocator/deallocator calls");
    stopAndDrain(engine, output);
    engine.triggerPad(0, 12);
    const auto pitched = probeCallbackHeap(engine, output, 12);
    c.require(pitched.empty(),
              "Prepared octave trigger and streaming callbacks perform no calling-thread allocator/deallocator calls");
    p.pads[0].levelDb = -6;
    engine.setLiveProject(p);
    const auto live = probeCallbackHeap(engine, output, 4);
    c.require(live.empty(),
              "Adopting published live cue controls performs no callback allocator/deallocator calls");
    engine.triggerPad(0, 0);
    const auto glide = probeCallbackHeap(engine, output, 12);
    c.require(glide.empty(),
              "Prepared sustain-glide pitch transition performs no callback allocator/deallocator calls");
    std::cout << "Callback C++ heap calls (alloc/free): neutral " << neutral.allocations << '/' << neutral.deallocations
              << ", pitch " << pitched.allocations << '/' << pitched.deallocations << ", live " << live.allocations << '/' << live.deallocations
              << ", glide " << glide.allocations << '/' << glide.deallocations << '\n';
   #if defined(SAMPLIFY_WRAP_C_ALLOCATIONS)
    std::cout << "Callback linked-code C allocator/free calls: neutral " << neutral.cAllocatorCalls << '/' << neutral.cFreeCalls
              << ", pitch " << pitched.cAllocatorCalls << '/' << pitched.cFreeCalls << ", live " << live.cAllocatorCalls << '/' << live.cFreeCalls
              << ", glide " << glide.cAllocatorCalls << '/' << glide.cFreeCalls << '\n';
   #else
    std::cout << "C allocator wrapping is unavailable in this build; heap probe covers C++ operators only.\n";
   #endif
    stopAndDrain(engine, output);
    p.glideMs = 0;
    engine.setLiveProject(p);
    for (const float semitones : {7.f, 48.f, -48.f}) {
        engine.triggerPad(0, semitones);
        const auto extreme = probeCallbackHeap(engine, output, 16);
        c.require(extreme.empty(), "Supported keyboard pitch trigger/stream performs no intercepted heap calls");
        std::cout << "Callback extreme pitch " << semitones << " C++ " << extreme.allocations << '/' << extreme.deallocations
                  << ", linked C " << extreme.cAllocatorCalls << '/' << extreme.cFreeCalls << '\n';
        stopAndDrain(engine, output);
    }
    p.pads[0].speedRatio = .25f;
    p.projectBpm = 30;
    p.sourceBpm = 300;
    engine.setLiveProject(p);
    engine.triggerPad(0, 12);
    const auto slow = probeCallbackHeap(engine, output, 16);
    c.require(slow.empty(), "Supported slow cue/global tempo combination performs no intercepted callback heap calls");
    stopAndDrain(engine, output);
    p.pads[0].speedRatio = 4;
    p.projectBpm = 300;
    p.sourceBpm = 30;
    engine.setLiveProject(p);
    engine.triggerPad(0, -12);
    const auto fast = probeCallbackHeap(engine, output, 16);
    c.require(fast.empty(), "Supported fast cue/global tempo combination performs no intercepted callback heap calls");
    std::cout << "Callback tempo edges C++ slow " << slow.allocations << '/' << slow.deallocations
              << ", fast " << fast.allocations << '/' << fast.deallocations << "; linked C slow "
              << slow.cAllocatorCalls << '/' << slow.cFreeCalls << ", fast " << fast.cAllocatorCalls << '/' << fast.cFreeCalls << '\n';
    stopAndDrain(engine, output);
    p.projectBpm = p.sourceBpm = 120;
    for (const float speed : {.25f, 4.f}) {
        p.pads[0].speedRatio = speed;
        engine.setLiveProject(p);
        engine.triggerPad(0, 7);
        const auto tempo = probeCallbackHeap(engine, output, 16);
        c.require(tempo.empty(), "Quarter/four-times cue tempo with non-octave pitch performs no intercepted heap calls");
        std::cout << "Callback cue speed " << speed << " C++ " << tempo.allocations << '/' << tempo.deallocations
                  << ", linked C " << tempo.cAllocatorCalls << '/' << tempo.cFreeCalls << '\n';
        stopAndDrain(engine, output);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string only;
    if (argc == 3 && std::string(argv[1]) == "--only") only = argv[2];
    else if (argc != 1) {
        std::cerr << "Usage: SamplifyIntegrationTests [--only audio|session|export|chronology|playback|live|glide|capacity|pending|heap]\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Checks checks;
    bool selected = false;
    try {
        TemporaryDirectory temporary;
        const auto run = [&](const char* id, const char* title, auto&& fn) {
            if (!only.empty() && only != id) return;
            selected = true;
            checks.section(title, fn);
        };
        run("audio", "WAV import/export", [&] { testAudioFiles(checks, temporary.path); });
        run("session", "Session persistence", [&] { testSessions(checks, temporary.path); });
        run("export", "Imported stems and rendered export", [&] { testStemsAndExport(checks); });
        run("chronology", "Trigger/Stop callback chronology", [&] { testTriggerStopChronology(checks); });
        run("playback", "Playback callback and MIDI", [&] { testPlaybackAndMidi(checks); });
        run("live", "Live controls, velocity and keyboard", [&] { testLiveControlsVelocityAndKeyboard(checks); });
        run("glide", "Glide and source metronome", [&] { testGlideAndMetronome(checks); });
        run("capacity", "Open-ended cue capacity", [&] { testOpenEndedCueCapacity(checks); });
        run("pending", "Pending cue assignment queue", [&] { testPendingCueEventQueue(checks); });
        run("heap", "Callback C++ heap probe", [&] { testCallbackCppHeap(checks); });
    } catch (const std::exception& error) {
        std::cerr << "Test setup failed: " << error.what() << '\n';
        return 1;
    }
    if (!selected) { std::cerr << "Unknown test section: " << only << '\n'; return 2; }
    std::cout << checks.checked << " checks, " << checks.failed << " failures\n";
    return checks.failed == 0 ? 0 : 1;
}
