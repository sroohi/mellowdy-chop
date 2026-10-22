#include "audio/AudioEngine.h"
#include "audio/SessionIO.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

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
    pad.attackMs = pad.releaseMs = 0;
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
    const auto original = tone();
    const auto file = directory.getChildFile("stereo.wav");
    AudioEngine::writeAudio(file, original);
    const auto loaded = AudioEngine::readAudio(file);
    c.require(loaded.valid() && loaded.channels.size() == 2, "WAV stereo import returns valid two-channel samples");
    c.require(loaded.frames() == original.frames() && loaded.sampleRate == original.sampleRate,
              "WAV export preserves frame count and sample rate");
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
    auto& pad = saved.project.pads[31];
    pad.start = 441;
    pad.end = 4410;
    pad.levelDb = -9.5f;
    pad.pitchSemitones = 7;
    pad.detuneCents = -12;
    pad.timeRatio = 1.5f;
    pad.filter = 0.73f;
    pad.attackMs = 24;
    pad.releaseMs = 400;
    pad.reverse = true;
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
        && readPad.timeRatio == pad.timeRatio && readPad.filter == pad.filter
        && readPad.attackMs == pad.attackMs && readPad.releaseMs == pad.releaseMs
        && readPad.reverse && readPad.stemMute == pad.stemMute, "Session restores every pad edit including stem mutes");
    const auto originalText = file.loadFileAsString();

    const auto malformed = directory.getChildFile("malformed.samplify");
    malformed.replaceWithText("{broken JSON");
    c.rejects([&] { (void) loadSession(malformed); }, "Malformed session JSON is rejected");
    malformed.replaceWithText("{}");
    c.rejects([&] { (void) loadSession(malformed); }, "Missing session schema is rejected");
    malformed.replaceWithText(originalText.replace("\"version\": 1", "\"version\": 2"));
    c.rejects([&] { (void) loadSession(malformed); }, "Unsupported session version is rejected");
    malformed.replaceWithText(originalText.replace("\"padCount\": 32", "\"padCount\": 17"));
    c.rejects([&] { (void) loadSession(malformed); }, "Unsupported pad count is rejected");
    auto invalid = saved;
    invalid.project.pads[0].timeRatio = -1;
    c.rejects([&] { saveSession(file, invalid); }, "Session save validates invalid state before replacement");
    c.require(file.loadFileAsString() == originalText, "Failed session save preserves prior document");
}

void testStemsAndExport(Checks& c) {
    auto source = std::make_shared<AudioData>(constant(0.55f));
    auto p = projectFor(*source);
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
    p.syncTempo = true;
    p.projectBpm = 180;
    const auto stretched = engine.renderForExport(p, 0);
    c.require(stretched.valid() && std::abs(static_cast<double>(stretched.frames()) - 29400.0) < 3,
              "BPM sync exports expected independent duration");
    c.require(std::abs(positiveCrossingFrequency(stretched) - 440) < 12, "BPM sync preserves source pitch");
    p.syncTempo = false;
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
    const auto reversed = engine.renderForExport(p, 0);
    c.require(reversed.valid() && reversed.channels[0].front() > 0.99f && reversed.channels[0].back() < 0.001f,
              "Reverse export changes cue audio order");
}

void testPlaybackAndMidi(Checks& c) {
    auto source = std::make_shared<AudioData>(tone(220, 44100));
    auto p = projectFor(*source);
    AudioEngine engine;
    engine.setSource(source);
    engine.preparePads(p);
    engine.prepareToPlay(512, 44100);
    engine.setMasterGain(1);
    juce::AudioBuffer<float> output(2, 512);
    c.require(processBlock(engine, output) == 0 && engine.activeVoiceCount() == 0, "Untriggered callback produces silence");
    engine.triggerPad(0);
    c.require(processBlock(engine, output) > 0.05f && engine.activeVoiceCount() == 1,
              "Pad trigger produces audible stereo buffer without hardware");
    c.require(std::isfinite(engine.peakLevel()) && engine.peakLevel() > 0, "Meter reports finite playback peak");
    engine.setMono(false);
    engine.triggerPad(1);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 2, "Poly mode mixes simultaneous pad voices");
    engine.setMono(true);
    engine.triggerPad(0);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1, "Mono retrigger fades prior voices within 5 ms");
    engine.releasePad(0);
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 0, "Note release obeys zero-length pad release");
    engine.triggerPad(0);
    processBlock(engine, output);
    engine.stop();
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 0, "Stop fades all active voices");
    c.require(processBlock(engine, output) == 0, "Stopped callback returns silence");

    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)));
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1, "Default MIDI note 36 triggers Pad 1");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 36));
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 0, "MIDI note-off releases mapped pad");
    engine.learnMidiPad(1);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 80, static_cast<juce::uint8>(100)));
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1 && engine.midiStatus().contains("80"), "MIDI learn maps next note to selected pad");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::allNotesOff(1));
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 0, "MIDI all-notes-off stops voices");
    engine.setKeyboardMode(true, 0);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)));
    c.require(processBlock(engine, output) > 0.05f && engine.activeVoiceCount() == 1,
              "Keyboard mode maps arbitrary MIDI note to selected pad");
    engine.releaseResources();
    c.require(engine.activeVoiceCount() == 0 && engine.peakLevel() == 0, "Resource release clears voices and meter");

    // Quantize is tied to the engine's beat clock, not to callback arrival.
    engine.prepareToPlay(512, 44100);
    processBlock(engine, output); // Move just beyond the first beat.
    engine.triggerPad(0, true);
    float beforeBeat = processBlock(engine, output);
    for (int block = 0; block < 41; ++block)
        beforeBeat = std::max(beforeBeat, processBlock(engine, output));
    c.require(beforeBeat == 0 && engine.activeVoiceCount() == 1, "Quantized trigger remains silent while awaiting next beat");
    c.require(processBlock(engine, output) > 0.05f, "Quantized trigger sounds when internal beat is reached");
    engine.stop();
    processBlock(engine, output);

    engine.setMono(false);
    engine.setKeyboardMode(true, 0);
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)));
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 2, "Keyboard polyphony allows two pitches of one selected pad");
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 1, "Keyboard note-off releases only its pitch");
    engine.stop();
    processBlock(engine, output);

    // A panic stop must survive a producer burst larger than the FIFO capacity.
    for (int command = 0; command < 300; ++command) engine.triggerPad(command % 2);
    engine.stop();
    processBlock(engine, output);
    c.require(engine.activeVoiceCount() == 0, "Panic stop is honored even when the control queue overflows");

    juce::AudioBuffer<float> region(2, 1024);
    for (int channel = 0; channel < region.getNumChannels(); ++channel)
        for (int sample = 0; sample < region.getNumSamples(); ++sample) region.setSample(channel, sample, 0.2f);
    engine.getNextAudioBlock(juce::AudioSourceChannelInfo(&region, 128, 512));
    c.require(region.getSample(0, 0) == 0.2f && region.getSample(1, 1023) == 0.2f
        && region.getMagnitude(128, 512) == 0, "Callback clears only the requested output region");
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Checks checks;
    try {
        TemporaryDirectory temporary;
        checks.section("WAV import/export", [&] { testAudioFiles(checks, temporary.path); });
        checks.section("Session persistence", [&] { testSessions(checks, temporary.path); });
        checks.section("Imported stems and rendered export", [&] { testStemsAndExport(checks); });
        checks.section("Playback callback and MIDI", [&] { testPlaybackAndMidi(checks); });
    } catch (const std::exception& error) {
        std::cerr << "Test setup failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks.checked << " checks, " << checks.failed << " failures\n";
    return checks.failed == 0 ? 0 : 1;
}
