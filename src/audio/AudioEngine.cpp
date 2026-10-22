#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace samplify {
namespace {
static_assert(std::atomic<const void*>::is_always_lock_free, "Audio bank publication requires lock-free pointers");
static_assert(std::atomic<float>::is_always_lock_free, "Audio metering requires lock-free floats");
static_assert(std::atomic<int>::is_always_lock_free, "Audio controls require lock-free integers");
constexpr int voiceLimit = 16;
constexpr int commandCapacity = 256;
constexpr std::size_t maxFileFrames = 32 * 1024 * 1024;
constexpr std::size_t maxBankSamples = 32 * 1024 * 1024;

float finiteGain(float gain) {
    return std::isfinite(gain) ? std::clamp(gain, 0.0f, 2.0f) : 0.0f;
}

AudioData renderSelection(const std::shared_ptr<const AudioData>& source,
                          const std::array<std::shared_ptr<const AudioData>, 4>& stems,
                          const Project& project, int index) {
    if (!source || source->frames() == 0 || index < 0 || index >= 32)
        return {};
    Pad pad = project.pads[static_cast<std::size_t>(index)];
    if (pad.empty() || pad.start >= source->frames())
        return {};
    pad.end = std::min(pad.end, source->frames());
    if (project.syncKey)
        pad.pitchSemitones += static_cast<float>(keySemitoneOffset(project.sourceKey, project.projectKey));
    double speed = 1.0;
    if (project.syncTempo && std::isfinite(project.projectBpm) && std::isfinite(project.sourceBpm)
        && project.sourceBpm > 0)
        speed = std::clamp(project.projectBpm / project.sourceBpm, 0.25, 4.0);

    const bool hasStems = std::any_of(stems.begin(), stems.end(), [](const auto& stem) { return bool(stem); });
    if (!hasStems) {
        auto rendered = renderPad(*source, pad, speed);
        if (!rendered.valid())
            throw std::runtime_error("A pad could not be rendered. Shorten the slice or check its pitch and stretch settings.");
        return rendered;
    }

    // Imported stems replace the original mix. Unimported slots are silent.
    // Mix only the selected source region so lengthy imports do not multiply working memory.
    AudioData mix;
    mix.sampleRate = source->sampleRate;
    const auto length = pad.end - pad.start;
    mix.channels.assign(source->channels.size(), std::vector<float>(length, 0.0f));
    for (std::size_t stemIndex = 0; stemIndex < stems.size(); ++stemIndex) {
        const auto& stem = stems[stemIndex];
        if (!stem || pad.stemMute[stemIndex])
            continue;
        for (std::size_t channel = 0; channel < mix.channels.size(); ++channel)
            for (std::size_t sample = 0; sample < length; ++sample)
                mix.channels[channel][sample] += stem->channels[channel][pad.start + sample];
    }
    pad.start = 0;
    pad.end = length;
    auto rendered = renderPad(mix, pad, speed);
    if (!rendered.valid())
        throw std::runtime_error("A stem pad could not be rendered. Shorten the slice or check its pitch and stretch settings.");
    return rendered;
}
} // namespace

struct AudioEngine::Impl {
    struct Bank {
        std::array<AudioData, 32> pads;
        std::array<float, 32> releaseMs {};
        double bpm = 120;
        bool quantize = false;
    };
    struct Voice {
        const AudioData* data = nullptr;
        const Bank* bank = nullptr;
        int pad = -1;
        float semitones = 0;
        double position = 0;
        double increment = 1;
        std::uint64_t startsAt = 0;
        std::uint64_t serial = 0;
        int releaseRemaining = 0;
        int releaseLength = 0;
    };
    enum class CommandType { Trigger, Release, Stop };
    struct Command {
        CommandType type = CommandType::Stop;
        int index = -1;
        bool quantize = false;
        float semitones = 0;
        bool exactPitch = false;
    };

    std::mutex sourceMutex;
    std::shared_ptr<const AudioData> source;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    std::mutex preparationMutex;
    std::uint64_t sourceRevision = 0;
    std::vector<std::unique_ptr<Bank>> banks;
    std::atomic<const Bank*> currentBank { nullptr };
    // Slot zero pins the bank while commands are interpreted; one further slot
    // pins each voice. Only the non-realtime preparation thread deletes banks.
    std::array<std::atomic<const Bank*>, voiceLimit + 1> hazards {};
    std::array<Voice, voiceLimit> voices;
    std::uint64_t serial = 0;
    std::uint64_t clock = 0;
    double outputRate = 44100;
    float smoothedGain = 0.8f;
    std::atomic<float> masterGain { 0.8f };
    std::atomic<bool> mono { true };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> peak { 0 };
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> keyboardMode { false };
    std::atomic<bool> midiQuantize { false };
    std::atomic<int> keyboardPad { 0 };
    std::atomic<int> learningPad { -1 };
    std::atomic<int> learnedPad { -1 };
    std::atomic<int> learnedNote { -1 };
    std::atomic<int> midiInputCount { 0 };
    std::array<std::atomic<int>, 128> midiMap;
    std::array<std::atomic<int>, 128> playingMidiPad;
    std::array<std::atomic<float>, 128> playingMidiOffset;
    std::mutex midiDevicesMutex;
    std::vector<juce::String> midiDevices;
    juce::AbstractFifo fifo { commandCapacity };
    std::array<Command, commandCapacity> commands;
    std::mutex producerMutex;
    bool connected = false;

    Impl() {
        for (int note = 0; note < 128; ++note) {
            midiMap[static_cast<std::size_t>(note)].store(note >= 36 && note < 68 ? note - 36 : -1);
            playingMidiPad[static_cast<std::size_t>(note)].store(-1);
            playingMidiOffset[static_cast<std::size_t>(note)].store(0);
        }
    }

    bool enqueue(Command command) {
        // There are multiple control producers (UI and MIDI), but the audio
        // callback is the sole consumer and never acquires this mutex.
        const std::lock_guard<std::mutex> lock(producerMutex);
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 == 0)
            return false;
        commands[static_cast<std::size_t>(start1)] = command;
        fifo.finishedWrite(1);
        return true;
    }

    const Bank* pinCurrentBank() {
        const Bank* bank = nullptr;
        do {
            bank = currentBank.load();
            hazards[0].store(bank);
        } while (bank != currentBank.load());
        return bank;
    }

    void reclaimBanks() {
        // Hazard-pointer protocol: a candidate read before retirement is checked
        // again after publication; it cannot be dereferenced unless still live.
        const auto* current = currentBank.load();
        banks.erase(std::remove_if(banks.begin(), banks.end(), [&](const auto& bank) {
            if (bank.get() == current)
                return false;
            for (const auto& hazard : hazards)
                if (hazard.load() == bank.get())
                    return false;
            return true;
        }), banks.end());
    }

    void finishVoice(int index) {
        voices[static_cast<std::size_t>(index)] = {};
        hazards[static_cast<std::size_t>(index + 1)].store(nullptr);
    }

    void fadeVoice(Voice& voice, double milliseconds) {
        if (!voice.data)
            return;
        if (voice.startsAt > clock) {
            // Scheduled but not audible yet; cancel it without consuming a fade.
            voice.releaseRemaining = voice.releaseLength = 1;
            voice.startsAt = clock;
            voice.position = static_cast<double>(voice.data->frames());
            return;
        }
        const int length = std::max(1, static_cast<int>(outputRate * milliseconds / 1000.0));
        if (voice.releaseRemaining == 0 || voice.releaseRemaining > length)
            voice.releaseRemaining = voice.releaseLength = length;
    }

    void applyCommand(const Command& command) {
        if (command.type == CommandType::Stop) {
            for (auto& voice : voices)
                fadeVoice(voice, 5);
            return;
        }
        if (command.type == CommandType::Release) {
            for (auto& voice : voices)
                if (voice.data && voice.pad == command.index
                    && (!command.exactPitch || std::abs(voice.semitones - command.semitones) < 0.01f))
                    fadeVoice(voice, std::clamp(voice.bank->releaseMs[static_cast<std::size_t>(voice.pad)], 1.0f, 5000.0f));
            return;
        }
        const auto* bank = pinCurrentBank();
        if (!bank || command.index < 0 || command.index >= 32) {
            hazards[0].store(nullptr);
            return;
        }
        const auto& data = bank->pads[static_cast<std::size_t>(command.index)];
        if (data.frames() == 0 || data.channels.empty()) {
            hazards[0].store(nullptr);
            return;
        }
        if (mono.load(std::memory_order_relaxed))
            for (auto& voice : voices)
                fadeVoice(voice, 5);
        int slot = -1;
        for (int index = 0; index < voiceLimit; ++index)
            if (!voices[static_cast<std::size_t>(index)].data) {
                slot = index;
                break;
            }
        if (slot < 0) {
            slot = 0;
            for (int index = 1; index < voiceLimit; ++index)
                if (voices[static_cast<std::size_t>(index)].serial < voices[static_cast<std::size_t>(slot)].serial)
                    slot = index;
        }
        auto& voice = voices[static_cast<std::size_t>(slot)];
        voice = {};
        voice.bank = bank;
        voice.data = &data;
        voice.pad = command.index;
        voice.semitones = command.semitones;
        voice.increment = (data.sampleRate / outputRate) * std::pow(2.0, command.semitones / 12.0);
        voice.serial = ++serial;
        voice.startsAt = clock;
        if (command.quantize) {
            const auto beatLength = static_cast<std::uint64_t>(std::max(1.0, outputRate * 60.0 / std::clamp(bank->bpm, 20.0, 400.0)));
            const auto remainder = clock % beatLength;
            voice.startsAt += remainder == 0 ? 0 : beatLength - remainder;
        }
        hazards[static_cast<std::size_t>(slot + 1)].store(bank);
        hazards[0].store(nullptr);
    }
};

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    {
        const std::lock_guard<std::mutex> lock(impl->midiDevicesMutex);
        for (const auto& identifier : impl->midiDevices)
            devices.removeMidiInputDeviceCallback(identifier, this);
    }
    if (impl->connected)
        devices.removeAudioCallback(&player);
    player.setSource(nullptr);
    devices.closeAudioDevice();
}

juce::String AudioEngine::initialise() {
    const auto error = devices.initialiseWithDefaultDevices(0, 2);
    if (!impl->connected) {
        player.setSource(this);
        devices.addAudioCallback(&player);
        impl->connected = true;
    }
    refreshMidiInputs();
    return error;
}

AudioData AudioEngine::readAudio(const juce::File& file) {
    if (!file.existsAsFile())
        throw std::runtime_error("The audio file could not be found.");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader)
        throw std::runtime_error("Unsupported or damaged audio file. Choose WAV, AIFF, FLAC, or MP3.");
    if (reader->numChannels < 1 || reader->numChannels > 2)
        throw std::runtime_error("Import a mono or stereo audio file.");
    if (!std::isfinite(reader->sampleRate) || reader->sampleRate < 8000 || reader->sampleRate > 384000)
        throw std::runtime_error("The audio sample rate must be between 8 and 384 kHz.");
    if (reader->lengthInSamples <= 0 || static_cast<std::uint64_t>(reader->lengthInSamples) > maxFileFrames)
        throw std::runtime_error("The audio file is empty or too long. Import fewer than 32 million frames (about 12 minutes at 44.1 kHz).");
    AudioData data;
    data.sampleRate = reader->sampleRate;
    data.channels.resize(reader->numChannels);
    std::array<float*, 2> pointers {};
    for (std::size_t channel = 0; channel < data.channels.size(); ++channel) {
        data.channels[channel].resize(static_cast<std::size_t>(reader->lengthInSamples));
        pointers[channel] = data.channels[channel].data();
    }
    juce::AudioBuffer<float> buffer(pointers.data(), static_cast<int>(data.channels.size()), static_cast<int>(data.frames()));
    if (!reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true))
        throw std::runtime_error("The audio decoder could not read the complete file.");
    if (!data.valid())
        throw std::runtime_error("The audio file contains invalid sample data.");
    return data;
}

void AudioEngine::writeAudio(const juce::File& file, const AudioData& data) {
    if (!data.valid() || data.channels.size() > 2 || data.frames() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("There is no valid audio to export.");
    juce::TemporaryFile temporary(file);
    std::unique_ptr<juce::FileOutputStream> stream(temporary.getFile().createOutputStream());
    if (!stream || !stream->openedOk())
        throw std::runtime_error("The export file could not be opened for writing.");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        stream.get(), data.sampleRate, static_cast<unsigned int>(data.channels.size()), 24, {}, 0));
    if (!writer)
        throw std::runtime_error("The WAV encoder could not be created.");
    stream.release(); // The writer now owns the stream.
    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> block(static_cast<int>(data.channels.size()), blockSize);
    bool succeeded = true;
    for (std::size_t offset = 0; offset < data.frames(); offset += blockSize) {
        const auto count = static_cast<int>(std::min<std::size_t>(blockSize, data.frames() - offset));
        for (std::size_t channel = 0; channel < data.channels.size(); ++channel)
            for (int sample = 0; sample < count; ++sample)
                block.setSample(static_cast<int>(channel), sample, std::clamp(data.channels[channel][offset + static_cast<std::size_t>(sample)], -1.0f, 1.0f));
        if (!writer->writeFromAudioSampleBuffer(block, 0, count)) {
            succeeded = false;
            break;
        }
    }
    // Closing the writer flushes data and patches the WAV header before replacing.
    writer.reset();
    if (!succeeded || !temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("The WAV file could not be saved. Check disk space and folder permissions.");
}

void AudioEngine::setSource(std::shared_ptr<const AudioData> source) {
    if (source && (!source->valid() || source->channels.size() > 2))
        throw std::runtime_error("Source audio must be valid mono or stereo audio.");
    const std::lock_guard<std::mutex> lock(impl->sourceMutex);
    impl->source = std::move(source);
    impl->stems = {};
    ++impl->sourceRevision;
    impl->currentBank.store(nullptr);
    stop();
}

void AudioEngine::setStem(int index, std::shared_ptr<const AudioData> stem) {
    if (index < 0 || index >= 4)
        throw std::runtime_error("Unknown stem slot.");
    const std::lock_guard<std::mutex> lock(impl->sourceMutex);
    if (!impl->source)
        throw std::runtime_error("Import source audio before importing aligned stems.");
    if (stem && (!stem->valid() || std::abs(stem->sampleRate - impl->source->sampleRate) > 0.01
        || stem->frames() != impl->source->frames() || stem->channels.size() != impl->source->channels.size()))
        throw std::runtime_error("Stems must match the source's sample rate, channel count, and exact duration. Export aligned stems from your separator.");
    impl->stems[static_cast<std::size_t>(index)] = std::move(stem);
    ++impl->sourceRevision;
    impl->currentBank.store(nullptr);
}

void AudioEngine::clearStems() {
    const std::lock_guard<std::mutex> lock(impl->sourceMutex);
    impl->stems = {};
    ++impl->sourceRevision;
    impl->currentBank.store(nullptr);
}

void AudioEngine::preparePads(const Project& project) {
    const std::lock_guard<std::mutex> preparationLock(impl->preparationMutex);
    std::uint64_t sourceRevision = 0;
    std::shared_ptr<const AudioData> source;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    {
        const std::lock_guard<std::mutex> lock(impl->sourceMutex);
        source = impl->source;
        stems = impl->stems;
        sourceRevision = impl->sourceRevision;
    }
    auto bank = std::make_unique<Impl::Bank>();
    bank->bpm = std::isfinite(project.projectBpm) ? std::clamp(project.projectBpm, 20.0, 400.0) : 120.0;
    bank->quantize = project.quantize;
    std::size_t samples = 0;
    for (int index = 0; index < 32; ++index) {
        bank->pads[static_cast<std::size_t>(index)] = renderSelection(source, stems, project, index);
        const auto& data = bank->pads[static_cast<std::size_t>(index)];
        samples += data.frames() * data.channels.size();
        if (samples > maxBankSamples)
            throw std::runtime_error("The pad cache exceeds 128 MB. Shorten the slices or reduce the stretch amount.");
        const auto release = project.pads[static_cast<std::size_t>(index)].releaseMs;
        bank->releaseMs[static_cast<std::size_t>(index)] = std::isfinite(release) ? std::clamp(release, 1.0f, 5000.0f) : 150.0f;
    }
    {
        const std::lock_guard<std::mutex> lock(impl->sourceMutex);
        if (sourceRevision != impl->sourceRevision)
            throw std::runtime_error("Audio changed while preparing pads. Apply the pad settings again.");
        const auto* current = bank.get();
        impl->banks.push_back(std::move(bank));
        impl->currentBank.store(current);
    }
    setMono(project.mono);
    impl->midiQuantize.store(project.quantize);
    impl->reclaimBanks();
}

void AudioEngine::triggerPad(int index, bool quantize, float semitoneOffset) {
    if (index < 0 || index >= 32 || !std::isfinite(semitoneOffset))
        return;
    impl->enqueue({ Impl::CommandType::Trigger, index, quantize, std::clamp(semitoneOffset, -48.0f, 48.0f), false });
}

void AudioEngine::releasePad(int index) {
    impl->enqueue({ Impl::CommandType::Release, index, false, 0, false });
}

void AudioEngine::releasePad(int index, float semitoneOffset) {
    if (index < 0 || index >= 32 || !std::isfinite(semitoneOffset)) return;
    impl->enqueue({ Impl::CommandType::Release, index, false,
                    std::clamp(semitoneOffset, -48.0f, 48.0f), true });
}

void AudioEngine::stop() {
    if (!impl->enqueue({ Impl::CommandType::Stop, -1, false, 0, false }))
        impl->stopRequested.store(true);
}

void AudioEngine::setMasterGain(float gain) { impl->masterGain.store(finiteGain(gain), std::memory_order_relaxed); }
void AudioEngine::setMono(bool value) { impl->mono.store(value, std::memory_order_relaxed); }
int AudioEngine::activeVoiceCount() const { return impl->activeVoices.load(std::memory_order_relaxed); }
float AudioEngine::peakLevel() const { return impl->peak.load(std::memory_order_relaxed); }

juce::String AudioEngine::deviceName() const {
    const auto* device = devices.getCurrentAudioDevice();
    return device ? device->getName() : "No audio output";
}

void AudioEngine::setKeyboardMode(bool enabled, int selectedPad) {
    impl->keyboardPad.store(std::clamp(selectedPad, 0, 31));
    impl->keyboardMode.store(enabled);
}

void AudioEngine::learnMidiPad(int padIndex) {
    impl->learningPad.store(padIndex >= 0 && padIndex < 32 ? padIndex : -1);
}

juce::String AudioEngine::midiStatus() const {
    const int learning = impl->learningPad.load();
    if (learning >= 0)
        return "Play a MIDI note for Pad " + juce::String(learning + 1);
    const int learned = impl->learnedPad.load();
    if (learned >= 0)
        return "MIDI " + juce::String(impl->learnedNote.load()) + " -> Pad " + juce::String(learned + 1);
    return impl->midiInputCount.load() > 0 ? "MIDI connected / notes 36-67" : "No MIDI input / computer keys available";
}

void AudioEngine::refreshMidiInputs() {
    const std::lock_guard<std::mutex> lock(impl->midiDevicesMutex);
    for (const auto& identifier : impl->midiDevices)
        devices.removeMidiInputDeviceCallback(identifier, this);
    impl->midiDevices.clear();
    for (const auto& input : juce::MidiInput::getAvailableDevices()) {
        devices.setMidiInputDeviceEnabled(input.identifier, true);
        devices.addMidiInputDeviceCallback(input.identifier, this);
        impl->midiDevices.push_back(input.identifier);
    }
    impl->midiInputCount.store(static_cast<int>(impl->midiDevices.size()));
}

AudioData AudioEngine::renderForExport(const Project& project, int padIndex) const {
    std::shared_ptr<const AudioData> source;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    {
        const std::lock_guard<std::mutex> lock(impl->sourceMutex);
        source = impl->source;
        stems = impl->stems;
    }
    return renderSelection(source, stems, project, padIndex);
}

void AudioEngine::prepareToPlay(int, double sampleRate) {
    impl->outputRate = std::isfinite(sampleRate) && sampleRate > 0 ? std::clamp(sampleRate, 1.0, 768000.0) : 44100.0;
    impl->clock = 0;
    for (int index = 0; index < voiceLimit; ++index)
        impl->finishVoice(index);
    impl->activeVoices.store(0);
}

void AudioEngine::releaseResources() {
    for (int index = 0; index < voiceLimit; ++index)
        impl->finishVoice(index);
    impl->activeVoices.store(0);
    impl->peak.store(0);
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    info.clearActiveBufferRegion();
    if (!info.buffer || info.numSamples <= 0)
        return;
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    impl->fifo.prepareToRead(commandCapacity, start1, size1, start2, size2);
    for (int offset = 0; offset < size1; ++offset)
        impl->applyCommand(impl->commands[static_cast<std::size_t>(start1 + offset)]);
    for (int offset = 0; offset < size2; ++offset)
        impl->applyCommand(impl->commands[static_cast<std::size_t>(start2 + offset)]);
    impl->fifo.finishedRead(size1 + size2);
    // Stop cannot be lost even if MIDI floods the bounded command queue.
    if (impl->stopRequested.exchange(false))
        impl->applyCommand({ Impl::CommandType::Stop, -1, false, 0, false });

    float blockPeak = 0;
    const auto targetGain = impl->masterGain.load(std::memory_order_relaxed);
    for (int sample = 0; sample < info.numSamples; ++sample) {
        impl->smoothedGain += 0.001f * (targetGain - impl->smoothedGain);
        std::array<float, 2> sum {};
        for (int slot = 0; slot < voiceLimit; ++slot) {
            auto& voice = impl->voices[static_cast<std::size_t>(slot)];
            if (!voice.data || impl->clock < voice.startsAt)
                continue;
            const auto frameCount = voice.data->frames();
            if (voice.position >= static_cast<double>(frameCount)) {
                impl->finishVoice(slot);
                continue;
            }
            const auto index = static_cast<std::size_t>(voice.position);
            const auto next = std::min(index + 1, frameCount - 1);
            const auto fraction = static_cast<float>(voice.position - static_cast<double>(index));
            const float envelope = voice.releaseRemaining > 0
                ? static_cast<float>(voice.releaseRemaining) / static_cast<float>(voice.releaseLength) : 1.0f;
            for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                const auto& data = voice.data->channels[std::min(channel, voice.data->channels.size() - 1)];
                sum[channel] += (data[index] + fraction * (data[next] - data[index])) * envelope;
            }
            voice.position += voice.increment;
            if (voice.releaseRemaining > 0 && --voice.releaseRemaining == 0)
                impl->finishVoice(slot);
        }
        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel) {
            const float value = std::clamp(sum[std::min<std::size_t>(static_cast<std::size_t>(channel), 1)] * impl->smoothedGain, -1.0f, 1.0f);
            info.buffer->setSample(channel, info.startSample + sample, value);
            blockPeak = std::max(blockPeak, std::abs(value));
        }
        ++impl->clock;
    }
    int count = 0;
    for (const auto& voice : impl->voices)
        if (voice.data)
            ++count;
    impl->activeVoices.store(count, std::memory_order_relaxed);
    impl->peak.store(blockPeak, std::memory_order_relaxed);
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (message.isAllNotesOff() || message.isAllSoundOff()) {
        stop();
        return;
    }
    if (!message.isNoteOnOrOff())
        return;
    const auto note = static_cast<std::size_t>(message.getNoteNumber());
    if (message.isNoteOn()) {
        const int learning = impl->learningPad.exchange(-1);
        if (learning >= 0) {
            for (auto& mapped : impl->midiMap)
                if (mapped.load() == learning)
                    mapped.store(-1);
            impl->midiMap[note].store(learning);
            impl->learnedPad.store(learning);
            impl->learnedNote.store(static_cast<int>(note));
        }
        const bool keyboard = impl->keyboardMode.load();
        const int pad = keyboard ? impl->keyboardPad.load() : impl->midiMap[note].load();
        if (pad < 0)
            return;
        const float semitones = keyboard ? std::clamp(static_cast<float>(static_cast<int>(note) - 60), -48.0f, 48.0f) : 0.0f;
        impl->playingMidiPad[note].store(pad);
        impl->playingMidiOffset[note].store(semitones);
        // Quantize metadata is atomic; MIDI never dereferences a rendered bank.
        triggerPad(pad, impl->midiQuantize.load(), semitones);
    } else {
        const int pad = impl->playingMidiPad[note].exchange(-1);
        if (pad >= 0)
            impl->enqueue({ Impl::CommandType::Release, pad, false, impl->playingMidiOffset[note].load(), true });
    }
}

} // namespace samplify
