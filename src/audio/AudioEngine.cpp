#include "AudioEngine.h"
#include "dsp/RubberBandProcessor.h"
#include "dsp/Effects.h"
#include "plugins/PluginHost.h"
#include <rubberband/RubberBandStretcher.h>
#if JUCE_MAC
#include <AudioToolbox/AudioToolbox.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace samplify {
namespace {
static_assert(std::atomic<const void*>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
constexpr int voiceLimit = 32;
constexpr int auditionSlot = voiceLimit;
constexpr int totalVoices = voiceLimit + 1;
constexpr int commandCapacity = 512;
constexpr int assignmentCapacity = 257;
constexpr std::size_t maxFileFrames = 32 * 1024 * 1024;
constexpr std::size_t processFrames = 512;
constexpr std::size_t retrieveFrames = 2048;
constexpr double pi = 3.14159265358979323846;
using Stretcher = RubberBand::RubberBandStretcher;

// The callback only copies stereo samples into this bounded ring. Opening,
// encoding, WAV-header finalization and transactional replacement stay on the
// control/writer threads. A full ring stops capture and reports the lost take.
class MasterCapture {
public:
    MasterCapture() : worker([this] { run(); }) {}
    ~MasterCapture() {
        accepting.store(false); finalizing.store(true); finalizeRequested.store(true);
        quitting.store(true); if (worker.joinable()) worker.join();
    }
    void start(const juce::File& file, double rate) {
        const std::lock_guard<std::mutex> lock(writerMutex);
        if (recording.load() || finalizing.load() || writer) throw std::runtime_error("Stop the current master recording before starting another.");
        temporary = std::make_unique<juce::TemporaryFile>(file);
        auto stream = temporary->getFile().createOutputStream();
        if (!stream || !stream->openedOk()) { temporary.reset(); error.store(CaptureError::OpenFailed); throw std::runtime_error("The master recording file could not be opened."); }
        juce::WavAudioFormat format;
        writer.reset(format.createWriterFor(stream.get(), rate, 2, 32, {}, 0));
        if (!writer) { temporary.reset(); error.store(CaptureError::OpenFailed); throw std::runtime_error("The float WAV recording encoder could not be created."); }
        stream.release();
        readPosition.store(0); writePosition.store(0); frames.store(0);
        error.store(CaptureError::None); overflow.store(false); finalizeRequested.store(false);
        recording.store(true); accepting.store(false);
    }
    void activate() noexcept { if (recording.load() && !finalizeRequested.load()) accepting.store(true); }
    void stop() noexcept {
        accepting.store(false);
        if (recording.load()) { finalizing.store(true); if (recording.exchange(false)) finalizeRequested.store(true); }
    }
    void push(float left, float right) noexcept {
        if (!accepting.load(std::memory_order_relaxed)) return;
        producers.fetch_add(1);
        if (!accepting.load()) { producers.fetch_sub(1); return; }
        const auto write = writePosition.load(std::memory_order_relaxed);
        if (write - readPosition.load(std::memory_order_acquire) >= capacity) {
            overflow.store(true); error.store(CaptureError::Overflow); stop(); producers.fetch_sub(1); return;
        }
        samples[static_cast<std::size_t>(write % capacity)] = { left, right };
        writePosition.store(write + 1, std::memory_order_release); frames.fetch_add(1, std::memory_order_relaxed);
        producers.fetch_sub(1);
    }
    CaptureSnapshot snapshot() const { return { recording.load(), finalizing.load(), overflow.load(), frames.load(), error.load() }; }
private:
    static constexpr std::uint64_t capacity = 262144;
    std::array<std::array<float, 2>, capacity> samples {};
    std::atomic<std::uint64_t> readPosition { 0 }, writePosition { 0 }, frames { 0 };
    std::atomic<bool> accepting { false }, recording { false }, finalizing { false }, finalizeRequested { false }, overflow { false }, quitting { false };
    std::atomic<CaptureError> error { CaptureError::None };
    std::atomic<int> producers { 0 };
    std::mutex writerMutex;
    std::unique_ptr<juce::TemporaryFile> temporary;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::thread worker;
    void run() {
        juce::AudioBuffer<float> block(2, 8192);
        for (;;) {
            {
                const std::lock_guard<std::mutex> lock(writerMutex);
                auto read = readPosition.load(std::memory_order_relaxed);
                const auto write = writePosition.load(std::memory_order_acquire);
                const auto count = static_cast<int>(std::min<std::uint64_t>(8192, write - read));
                if (writer && count > 0) {
                    for (int sample = 0; sample < count; ++sample) {
                        const auto& value = samples[static_cast<std::size_t>((read + static_cast<std::uint64_t>(sample)) % capacity)];
                        block.setSample(0, sample, value[0]); block.setSample(1, sample, value[1]);
                    }
                    if (!writer->writeFromAudioSampleBuffer(block, 0, count)) { error.store(CaptureError::DiskWriteFailed); stop(); }
                    read += static_cast<std::uint64_t>(count); readPosition.store(read, std::memory_order_release);
                }
                if (finalizeRequested.load() && producers.load() == 0 && read == writePosition.load(std::memory_order_acquire)) {
                    writer.reset();
                    if (temporary) {
                        if (frames.load() == 0 && error.load() == CaptureError::None) error.store(CaptureError::EmptyRecording);
                        if (error.load() == CaptureError::None && !temporary->overwriteTargetFileWithTemporary()) error.store(CaptureError::DiskWriteFailed);
                        temporary.reset();
                    }
                    recording.store(false); finalizeRequested.store(false); finalizing.store(false);
                }
                if (quitting.load() && !writer) return;
            }
            if (readPosition.load() == writePosition.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
};

float finiteGain(float gain) { return std::isfinite(gain) ? std::clamp(gain, 0.0f, 4.0f) : 0.0f; }
float bounded(float value, float low, float high, float fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

juce::String cleanMetadata(juce::String text) {
    return text.replaceCharacters("\r\n\t", "   ").trim().substring(0, 1024);
}

std::string metadataTag(const juce::StringPairArray& metadata, std::initializer_list<const char*> candidates) {
    const auto& keys = metadata.getAllKeys();
    const auto& values = metadata.getAllValues();
    for (const auto* candidate : candidates)
        for (int i = 0; i < keys.size(); ++i)
            if (keys[i].toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789") == candidate) {
                const auto text = cleanMetadata(values[i]);
                if (text.isNotEmpty()) return text.toStdString();
            }
    return {};
}

#if JUCE_MAC
void readNativeMetadata(const juce::File& file, AudioData& data) {
    if (!data.artist.empty() && !data.title.empty()) return;
    const auto path = file.getFullPathName().toCFString();
    const auto url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, false);
    CFRelease(path);
    if (url == nullptr) return;
    AudioFileID audioFile = nullptr;
    const auto opened = AudioFileOpenURL(url, kAudioFileReadPermission, 0, &audioFile);
    CFRelease(url);
    if (opened != noErr || audioFile == nullptr) return;
    CFDictionaryRef information = nullptr;
    UInt32 size = sizeof(information);
    if (AudioFileGetProperty(audioFile, kAudioFilePropertyInfoDictionary, &size, &information) == noErr && information != nullptr) {
        auto read = [&](CFStringRef key) -> std::string {
            const auto value = CFDictionaryGetValue(information, key);
            if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return {};
            const auto text = static_cast<CFStringRef>(value);
            // Ignore pathological tag lengths rather than copying an unbounded
            // metadata value into the source-player title.
            if (CFStringGetLength(text) > 65536) return {};
            return cleanMetadata(juce::String::fromCFString(text)).toStdString();
        };
        if (data.artist.empty()) data.artist = read(CFSTR(kAFInfoDictionary_Artist));
        if (data.title.empty()) data.title = read(CFSTR(kAFInfoDictionary_Title));
        CFRelease(information);
    }
    AudioFileClose(audioFile);
}
#endif

struct RealtimeProcessor {
    std::unique_ptr<Stretcher> stretch;
    std::array<std::array<float, processFrames>, 2> input {};
    std::array<std::array<float, retrieveFrames>, 2> output {};
    std::size_t next = 0, count = 0, padRemaining = 0, discard = 0;
    bool final = false;
    double rate = 0;

    void prepare(double sampleRate) {
        if (stretch && rate == sampleRate) return;
        rate = sampleRate;
        const auto options = Stretcher::OptionProcessRealTime | Stretcher::OptionEngineFaster
            | Stretcher::OptionWindowShort | Stretcher::OptionPitchHighConsistency
            | Stretcher::OptionThreadingNever | Stretcher::OptionChannelsTogether;
        stretch = std::make_unique<Stretcher>(static_cast<std::size_t>(std::llround(rate)), 2,
                                            options, 16.0, 1.0 / 32.0);
        stretch->setMaxProcessSize(processFrames);
        const float* in[] = { input[0].data(), input[1].data() };
        float* out[] = { output[0].data(), output[1].data() };
        // Reserve supported pitch/tempo corners before entering the audio callback.
        // Rubber Band's normal real-time path does not allocate; oversized blocks
        // and rapidly changing extreme ratios are explicitly avoided here.
        for (std::size_t i = 0; i < processFrames; ++i)
            input[0][i] = input[1][i] = .15f * static_cast<float>(std::sin(2 * pi * 220.0 * static_cast<double>(i) / rate));
        // Configuration reserves all supported window/output sizes; processing
        // the extreme 16x-duration/32x-pitch corner would do needless work.
        for (const double duration : { 1.0 / 16.0, 1.0, 16.0 })
            for (const double pitch : { 1.0 / 32.0, 1.0, 32.0 }) {
                stretch->setTimeRatio(duration);
                stretch->setPitchScale(pitch);
            }
        stretch->setTimeRatio(1);
        // BQ has two alternating resampler filter states. Both must see actual
        // signal, with a ratio change between them (not reset), before realtime
        // use; silent or reset-between-pitches warmups leave a lazy allocation.
        for (const double pitch : { 1.0 / 32.0, 32.0, 95999.0 / 96000.0, 95989.0 / 96000.0 }) {
            stretch->setPitchScale(pitch);
            for (int block = 0; block < 40; ++block) {
                stretch->process(in, processFrames, false);
                const bool producedOutput = stretch->available() > 0;
                while (stretch->available() > 0)
                    if (stretch->retrieve(out, std::min<std::size_t>(retrieveFrames, static_cast<std::size_t>(stretch->available()))) == 0) break;
                if (producedOutput) break;
            }
        }
        // reset also scavenges retired warm-up buffers, off the callback.
        stretch->reset();
        stretch->setMaxProcessSize(processFrames);
        stretch->setTimeRatio(1);
        stretch->setPitchScale(1);
        next = count = 0;
    }

    void reset(double speed, double semitones) {
        next = count = 0;
        final = false;
        if (!stretch) return;
        stretch->reset();
        stretch->setMaxProcessSize(processFrames);
        stretch->setTimeRatio(1.0 / speed);
        stretch->setPitchScale(std::exp2(semitones / 12.0));
        padRemaining = stretch->getPreferredStartPad();
        discard = stretch->getStartDelay();
    }
};

AudioData renderSelection(const std::shared_ptr<const AudioData>& source,
                          const std::array<std::shared_ptr<const AudioData>, 4>& stems,
                          const Project& project, int index) {
    if (!source || index < 0 || index >= 32 || project.pads[static_cast<std::size_t>(index)].empty()) return {};
    Pad pad = project.pads[static_cast<std::size_t>(index)];
    pad.end = std::min(pad.end, source->frames());
    if (pad.start >= pad.end) return {};
    AudioData region;
    region.sampleRate = source->sampleRate;
    region.channels.assign(source->channels.size(), std::vector<float>(pad.end - pad.start, 0));
    const bool hasStems = std::any_of(stems.begin(), stems.end(), [](const auto& stem) { return bool(stem); });
    for (std::size_t channel = 0; channel < region.channels.size(); ++channel)
        for (std::size_t i = 0; i < region.frames(); ++i) {
            if (!hasStems) region.channels[channel][i] = source->channels[channel][pad.start + i];
            else for (std::size_t stem = 0; stem < 4; ++stem)
                if (stems[stem] && !project.stemMute[stem] && !pad.stemMute[stem])
                    region.channels[channel][i] += stems[stem]->channels[channel][pad.start + i];
        }
    pad.start = 0; pad.end = region.frames();
    pad.pitchSemitones += project.globalPitchSemitones;
    pad.detuneCents += project.globalDetuneCents;
    if (project.syncKey) pad.pitchSemitones += static_cast<float>(keySemitoneOffset(project.sourceKey, project.projectKey));
    const double pitch = std::clamp(static_cast<double>(pad.pitchSemitones + pad.detuneCents / 100.0f), -60.0, 60.0);
    pad.pitchSemitones = static_cast<float>(pitch);
    pad.detuneCents = 0;
    const float attackMs = bounded(pad.attackMs, 0, 10000, 0);
    const float releaseMs = bounded(pad.releaseMs, 0, 10000, 0);
    // The core renderer has a conventional end fade. Performance mode envelopes
    // are applied here instead: Hold sustains until note-off; Trigger releases
    // immediately after its attack. Export represents a held note for full cue.
    pad.attackMs = pad.releaseMs = 0;
    AudioData rendered;
    double tempo = 1;
    if (std::isfinite(project.sourceBpm) && project.sourceBpm > 0 && std::isfinite(project.projectBpm))
        tempo = std::clamp(project.projectBpm / project.sourceBpm, .25, 4.0);
    if (project.pitchTimeEnabled) {
        rendered = renderPad(region, pad, tempo);
    } else {
        // Bypass is varispeed: pitch and duration change together, matching the
        // direct realtime read path rather than pitch-preserving stretching.
        const double step = std::clamp(tempo * static_cast<double>(bounded(pad.speedRatio, .25f, 4.f, 1.f)) * std::exp2(pitch / 12.0), 1.0 / 32.0, 32.0);
        const double requested = static_cast<double>(region.frames()) / step;
        if (requested > static_cast<double>((64u * 1024u * 1024u) / region.channels.size()))
            throw std::runtime_error("The varispeed export exceeds the audio memory budget.");
        const auto frames = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(requested)));
        rendered.sampleRate = region.sampleRate;
        rendered.channels.assign(region.channels.size(), std::vector<float>(frames));
        const float filter = bounded(pad.filter, 0, 1, .5f);
        const double cutoff = filter < .5f ? 80.0 * std::pow(17000.0 / 80.0, filter * 2.0)
                                          : 80.0 * std::pow(17000.0 / 80.0, (filter - .5) * 2.0);
        const float alpha = static_cast<float>(1 - std::exp(-2 * pi * cutoff / region.sampleRate));
        const float gain = std::pow(10.f, bounded(pad.levelDb, -12, 12, 0) / 20.f);
        for (std::size_t channel = 0; channel < region.channels.size(); ++channel) {
            float state = 0;
            for (std::size_t i = 0; i < frames; ++i) {
                const double position = pad.reverse ? static_cast<double>(region.frames() - 1) - i * step : i * step;
                const double clamped = std::clamp(position, 0.0, static_cast<double>(region.frames() - 1));
                const auto first = static_cast<std::size_t>(clamped);
                const auto next = std::min(first + 1, region.frames() - 1);
                const float fraction = static_cast<float>(clamped - static_cast<double>(first));
                float value = region.channels[channel][first] + fraction * (region.channels[channel][next] - region.channels[channel][first]);
                if (std::abs(filter - .5f) > .0001f) { state += alpha * (value - state); value = filter < .5f ? state : value - state; }
                rendered.channels[channel][i] = value * gain;
            }
        }
    }
    if (!rendered.valid()) throw std::runtime_error("The selected cue could not be rendered. Check its pitch and tempo settings.");
    const auto attack = static_cast<std::size_t>(attackMs * rendered.sampleRate / 1000.0);
    const auto release = static_cast<std::size_t>(releaseMs * rendered.sampleRate / 1000.0);
    if (project.playbackMode == CuePlaybackMode::Trigger) {
        if (attack + release == 0) throw std::runtime_error("The trigger envelope has zero duration. Increase its attack or release before exporting.");
        const auto frames = std::min(rendered.frames(), attack + release);
        for (auto& channel : rendered.channels) channel.resize(frames);
    }
    for (std::size_t i = 0; i < rendered.frames(); ++i) {
        float envelope = attack == 0 ? 1.f : static_cast<float>(std::min(1.0, static_cast<double>(i) / static_cast<double>(attack)));
        if (project.playbackMode == CuePlaybackMode::Trigger && i >= attack)
            envelope = release == 0 ? 0.f : 1.f - static_cast<float>(i - attack) / static_cast<float>(release);
        for (auto& channel : rendered.channels) channel[i] *= envelope;
    }
    rendered.artist = source->artist;
    rendered.title = source->title;
    return rendered;
}
} // namespace

struct AudioEngine::Impl {
    struct Bank {
        std::shared_ptr<const AudioData> source;
        std::array<std::shared_ptr<const AudioData>, 4> stems;
        Project project;
        Pad auditionPad;
        float keyOffset = 0;
        bool hasStems = false;
        std::uint64_t sourceGeneration = 0;
    };
    struct Voice {
        const Bank* bank = nullptr;
        int pad = -1;
        float note = 0, velocity = 1;
        double position = 0, inputPosition = 0;
        double currentPitch = 0, targetPitch = 0, glideStep = 0;
        std::uint64_t glideRemaining = 0, age = 0, serial = 0;
        std::uint64_t releaseAge = 0, releaseLength = 0;
        float releaseStart = 1;
        bool held = true, releasing = false, atEnd = false, audition = false;
        bool processorReady = false, processedLast = false;
        std::uint8_t mixMask = 0;
        std::array<float, 2> filterState {};
        int track = -1;
        bool sequenced = false;
        std::uint64_t sequenceToken = 0;
    };
    enum class CommandType { Trigger, Release, Stop, PlaySource, PauseSource, SeekSource, Workstation, CaptureStart, CaptureStop };
    struct Command {
        CommandType type = CommandType::Stop;
        int index = -1;
        float note = 0, velocity = 1;
        bool exactPitch = false;
        std::size_t frame = 0;
        WorkstationControl workstation;
        int track = -1;
        bool sequenced = false;
        std::uint64_t sequenceToken = 0;
    };

    std::mutex sourceMutex, publicationMutex;
    std::shared_ptr<const AudioData> source;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    Project lastProject;
    std::vector<std::unique_ptr<Bank>> banks;
    std::atomic<const Bank*> currentBank { nullptr };
    std::array<std::atomic<const Bank*>, totalVoices + 1> hazards {};
    std::array<Voice, totalVoices> voices;
    std::array<RealtimeProcessor, totalVoices> processors;
    std::uint64_t serial = 0, displayedSerial = 0;
    double outputRate = 44100, auditionPosition = 0;
    bool displayAudition = true;
    float smoothedGain = 0.8f;
    std::atomic<float> masterGain { 0.8f }, peak { 0 };
    std::atomic<bool> mono { true }, stopRequested { false }, keyboardMode { false }, metronome { false }, resetSourceRequested { false };
    std::int64_t metronomeBeat = std::numeric_limits<std::int64_t>::min();
    std::size_t metronomeMarker = 0;
    int metronomeRemaining = 0, metronomeLength = 1;
    double metronomePhase = 0, metronomeFrequency = 1400;
    std::atomic<int> activeVoices { 0 }, keyboardPad { 0 }, learningPad { -1 }, learnedPad { -1 }, learnedNote { -1 }, midiInputCount { 0 };
    std::atomic<std::size_t> displayFrame { 0 };
    std::atomic<bool> sourcePlaying { false };
    std::atomic<std::uint32_t> activeMask { 0 };
    std::atomic<int> lastTriggeredPad { -1 }, pendingCuePad { -1 };
    std::atomic<std::uint64_t> triggerSerial { 0 };
    std::atomic<std::size_t> pendingCueFrame { 0 };
    std::atomic<std::uint64_t> pendingCueSerial { 0 };
    std::atomic<float> pendingCueVelocity { 1 }, pendingCueSemitoneOffset { 0 };
    std::atomic<bool> pendingCueHeld { false };
    struct AssignmentEvent { CueAssignment cue; std::uint64_t generation = 0, cancellationGeneration = 0; };
    juce::AbstractFifo assignmentFifo { assignmentCapacity };
    std::array<AssignmentEvent, assignmentCapacity> assignments;
    std::array<std::array<std::atomic<bool>, 97>, 32> pendingHolds {};
    std::atomic<std::uint64_t> sourceGeneration { 0 }, assignmentCancellation { 0 };
    std::array<std::atomic<int>, 128> midiMap, playingMidiPad;
    std::array<std::atomic<float>, 128> playingMidiOffset;
    std::mutex midiDevicesMutex;
    std::vector<juce::String> midiDevices;
    juce::AbstractFifo fifo { commandCapacity };
    std::array<Command, commandCapacity> commands;
    std::mutex producerMutex;
    bool connected = false;
    WorkstationRuntime workstation;
    PluginHost plugins;
    MasterCapture capture;
    std::array<FxRack, 11> effects;
    std::array<juce::AudioBuffer<float>, 11> buses;
    std::vector<float> masterEnvelope;
    std::size_t maximumBlock = 0;
    std::array<float, 8> trackLeftGain { 1, 1, 1, 1, 1, 1, 1, 1 }, trackRightGain { 1, 1, 1, 1, 1, 1, 1, 1 };
    std::array<std::array<std::uint64_t, 97>, 32> recordingIds {};

    Impl() {
        for (int note = 0; note < 128; ++note) {
            midiMap[static_cast<std::size_t>(note)].store(note >= 24 && note < 56 ? note - 24 : -1);
            playingMidiPad[static_cast<std::size_t>(note)].store(-1);
            playingMidiOffset[static_cast<std::size_t>(note)].store(0);
        }
    }
    bool enqueue(Command command) {
        const std::lock_guard<std::mutex> lock(producerMutex);
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (!size1) return false;
        commands[static_cast<std::size_t>(start1)] = command;
        fifo.finishedWrite(1);
        return true;
    }
    const Bank* pinCurrentBank() {
        const Bank* bank;
        do { bank = currentBank.load(); hazards[0].store(bank); } while (bank != currentBank.load());
        return bank;
    }
    void reclaimBanks() {
        const auto* current = currentBank.load();
        banks.erase(std::remove_if(banks.begin(), banks.end(), [&](const auto& bank) {
            if (bank.get() == current) return false;
            for (const auto& hazard : hazards) if (hazard.load() == bank.get()) return false;
            return true;
        }), banks.end());
    }
    void publish(const Project* incomingProject) {
        const std::lock_guard<std::mutex> publishLock(publicationMutex);
        auto bank = std::make_unique<Bank>();
        {
            // Source assignment and publication are atomic relative to each
            // other. Refreshing stems never restores an older UI parameter set.
            const std::lock_guard<std::mutex> sourceLock(sourceMutex);
            if (incomingProject) lastProject = *incomingProject;
            bank->project = lastProject;
            bank->source = source;
            bank->stems = stems;
            bank->sourceGeneration = sourceGeneration.load();
            bank->keyOffset = lastProject.syncKey ? static_cast<float>(keySemitoneOffset(lastProject.sourceKey, lastProject.projectKey)) : 0;
            bank->hasStems = std::any_of(bank->stems.begin(), bank->stems.end(), [](const auto& stem) { return bool(stem); });
            bank->auditionPad.start = 0;
            bank->auditionPad.end = bank->source ? bank->source->frames() : 0;
            bank->auditionPad.attackMs = bank->auditionPad.releaseMs = 0;
            const int selected = keyboardPad.load();
            if (selected >= 0 && selected < 32 && !lastProject.pads[static_cast<std::size_t>(selected)].empty())
                bank->auditionPad.stemMute = lastProject.pads[static_cast<std::size_t>(selected)].stemMute;
            mono.store(lastProject.mono);
            keyboardMode.store(lastProject.keyboardMode && selected >= 0);
            const auto* pointer = bank.get();
            banks.push_back(std::move(bank));
            currentBank.store(pointer);
        }
        reclaimBanks();
    }
    const Pad& padFor(const Voice& voice) const {
        return voice.audition ? voice.bank->auditionPad : voice.bank->project.pads[static_cast<std::size_t>(voice.pad)];
    }
    double pitchFor(const Voice& voice) const {
        const auto& pad = padFor(voice);
        return std::clamp(static_cast<double>(bounded(voice.bank->project.globalPitchSemitones, -60, 60, 0)
            + bounded(pad.pitchSemitones, -24, 24, 0) + voice.bank->keyOffset
            + (bounded(voice.bank->project.globalDetuneCents, -100, 100, 0) + bounded(pad.detuneCents, -100, 100, 0)) / 100.0f)
            + voice.currentPitch, -60.0, 60.0);
    }
    double speedFor(const Voice& voice) const {
        const auto& project = voice.bank->project;
        const auto& pad = padFor(voice);
        double ratio = bounded(pad.speedRatio, .25f, 4.f, 1.f);
        if (std::isfinite(project.sourceBpm) && project.sourceBpm > 0 && std::isfinite(project.projectBpm))
            ratio *= std::clamp(project.projectBpm / project.sourceBpm, .25, 4.0);
        if (!project.pitchTimeEnabled) ratio *= std::exp2(pitchFor(voice) / 12.0);
        return std::clamp(ratio, 1.0 / 32.0, 32.0);
    }
    std::uint8_t maskFor(const Voice& voice) const {
        std::uint8_t mask = 0;
        const auto& pad = padFor(voice);
        for (std::size_t stem = 0; stem < 4; ++stem)
            if (voice.bank->stems[stem] && !voice.bank->project.stemMute[stem] && !pad.stemMute[stem])
                mask |= static_cast<std::uint8_t>(1u << stem);
        return mask;
    }
    std::array<float, 2> readMixed(const Voice& voice, double frame) const {
        std::array<float, 2> result {};
        const auto* sourceData = voice.bank->source.get();
        if (!sourceData || frame < 0 || frame >= static_cast<double>(sourceData->frames())) return result;
        const auto index = static_cast<std::size_t>(frame);
        const auto next = std::min(index + 1, sourceData->frames() - 1);
        const float fraction = static_cast<float>(frame - static_cast<double>(index));
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const auto channelIndex = std::min(channel, sourceData->channels.size() - 1);
            if (!voice.bank->hasStems) {
                const auto& data = sourceData->channels[channelIndex];
                result[channel] = data[index] + fraction * (data[next] - data[index]);
            } else for (std::size_t stem = 0; stem < 4; ++stem) {
                if (!(voice.mixMask & (1u << stem))) continue;
                const auto& data = voice.bank->stems[stem]->channels[channelIndex];
                result[channel] += data[index] + fraction * (data[next] - data[index]);
            }
        }
        return result;
    }
    bool inside(const Voice& voice, double frame) const {
        const auto& pad = padFor(voice);
        const auto end = std::min(pad.end, voice.bank->source->frames());
        return frame >= static_cast<double>(pad.start) && frame < static_cast<double>(end);
    }
    void resetProcessor(int slot) {
        auto& voice = voices[static_cast<std::size_t>(slot)];
        voice.inputPosition = voice.position;
        processors[static_cast<std::size_t>(slot)].reset(speedFor(voice), pitchFor(voice));
        voice.processorReady = true;
    }
    bool processedSample(int slot, std::array<float, 2>& sample) {
        auto& voice = voices[static_cast<std::size_t>(slot)];
        auto& processor = processors[static_cast<std::size_t>(slot)];
        if (!processor.stretch) return false;
        if (!voice.processorReady) resetProcessor(slot);
        if (processor.count == 0) {
            processor.stretch->setTimeRatio(1.0 / speedFor(voice));
            processor.stretch->setPitchScale(std::exp2(pitchFor(voice) / 12.0));
        }
        const auto& pad = padFor(voice);
        const double inputStep = voice.bank->source->sampleRate / outputRate * (pad.reverse ? -1 : 1);
        const float* input[] = { processor.input[0].data(), processor.input[1].data() };
        float* output[] = { processor.output[0].data(), processor.output[1].data() };
        // At most 32k source frames of lookahead per pull; normal short-window
        // latency needs a fraction of that. The bound prevents a stalled backend
        // from trapping the audio callback.
        for (int attempt = 0; processor.count == 0 && attempt < 64; ++attempt) {
            const int available = processor.stretch->available();
            if (available > 0) {
                const auto got = processor.stretch->retrieve(output, std::min({ retrieveFrames, std::size_t(512), static_cast<std::size_t>(available) }));
                const auto skip = std::min(got, processor.discard);
                processor.discard -= skip;
                processor.next = skip;
                processor.count = got - skip;
                continue;
            }
            if (processor.padRemaining > 0) {
                const auto count = std::min(processFrames, processor.padRemaining);
                std::fill_n(processor.input[0].data(), count, 0.f);
                std::fill_n(processor.input[1].data(), count, 0.f);
                processor.stretch->process(input, count, false);
                processor.padRemaining -= count;
                continue;
            }
            if (processor.final || available < 0) break;
            std::size_t count = 0;
            while (count < processFrames && inside(voice, voice.inputPosition)) {
                const auto mixed = readMixed(voice, voice.inputPosition);
                processor.input[0][count] = mixed[0]; processor.input[1][count] = mixed[1];
                voice.inputPosition += inputStep;
                ++count;
            }
            processor.final = !inside(voice, voice.inputPosition);
            processor.stretch->process(input, count, processor.final);
        }
        if (processor.count == 0) return false;
        sample[0] = processor.output[0][processor.next]; sample[1] = processor.output[1][processor.next];
        ++processor.next; --processor.count;
        return true;
    }
    float attackEnvelope(const Voice& voice) const {
        const double attack = bounded(padFor(voice).attackMs, 0, 10000, 0) * outputRate / 1000.0;
        return attack <= 0 ? 1.f : static_cast<float>(std::min(1.0, static_cast<double>(voice.age) / attack));
    }
    void beginRelease(Voice& voice, double milliseconds) {
        if (!voice.bank) return;
        const auto length = static_cast<std::uint64_t>(std::max(0.0, milliseconds * outputRate / 1000));
        if (voice.releasing) {
            const auto remaining = voice.releaseLength > voice.releaseAge ? voice.releaseLength - voice.releaseAge : 0;
            if (length >= remaining) return;
            voice.releaseStart *= voice.releaseLength == 0 ? 0.f
                : 1.f - static_cast<float>(voice.releaseAge) / static_cast<float>(voice.releaseLength);
        } else voice.releaseStart = attackEnvelope(voice);
        voice.releasing = true;
        voice.releaseAge = 0;
        voice.releaseLength = length;
    }
    void finishVoice(int slot, bool returnToStart) {
        auto& voice = voices[static_cast<std::size_t>(slot)];
        if (voice.bank && !voice.audition && returnToStart && voice.serial == displayedSerial)
            displayFrame.store(padFor(voice).start);
        if (voice.audition) sourcePlaying.store(false);
        voice = {};
        hazards[static_cast<std::size_t>(slot + 1)].store(nullptr);
    }
    void startAudition(const Bank* bank) {
        if (!bank || !bank->source) return;
        auto& voice = voices[auditionSlot];
        voice = {};
        voice.bank = bank; voice.audition = true; voice.pad = -1;
        voice.position = std::clamp(auditionPosition, 0.0, static_cast<double>(bank->source->frames()));
        if (voice.position >= static_cast<double>(bank->source->frames())) voice.position = 0;
        voice.inputPosition = voice.position;
        voice.mixMask = maskFor(voice);
        voice.serial = ++serial;
        hazards[auditionSlot + 1].store(bank);
        sourcePlaying.store(true);
        metronomeBeat = std::numeric_limits<std::int64_t>::min();
        displayAudition = true;
        displayFrame.store(static_cast<std::size_t>(voice.position));
    }
    void applyCommand(const Command& command, const Bank* bank) {
        if (command.type == CommandType::Workstation) { workstation.applyControl(command.workstation); return; }
        if (command.type == CommandType::CaptureStart) { capture.activate(); return; }
        if (command.type == CommandType::CaptureStop) { capture.stop(); return; }
        if (command.type == CommandType::Stop) {
            workstation.finishRecordingNotes();
            for (auto& padIds : recordingIds) padIds.fill(0);
            lastTriggeredPad.store(-1);
            assignmentCancellation.fetch_add(1);
            pendingCueHeld.store(false);
            for (auto& padHolds : pendingHolds) for (auto& hold : padHolds) hold.store(false);
            for (int slot = 0; slot < totalVoices; ++slot) {
                auto& voice = voices[static_cast<std::size_t>(slot)];
                if (!voice.bank) continue;
                if (voice.atEnd || voice.audition) finishVoice(slot, false);
                else beginRelease(voice, 5);
            }
            sourcePlaying.store(false);
            return;
        }
        if (command.type == CommandType::PlaySource) { startAudition(bank); return; }
        if (command.type == CommandType::PauseSource) {
            if (voices[auditionSlot].bank) auditionPosition = voices[auditionSlot].position;
            finishVoice(auditionSlot, false);
            return;
        }
        if (command.type == CommandType::SeekSource) {
            if (!bank || !bank->source) return;
            auditionPosition = static_cast<double>(std::min(command.frame, bank->source->frames()));
            displayFrame.store(static_cast<std::size_t>(auditionPosition));
            displayAudition = true;
            if (sourcePlaying.load()) startAudition(bank);
            return;
        }
        if (command.type == CommandType::Release) {
            if (!command.sequenced && command.index >= 0 && command.index < 32) {
                auto& ids = recordingIds[static_cast<std::size_t>(command.index)];
                for (std::size_t pitch = 0; pitch < ids.size(); ++pitch) {
                    if (command.exactPitch && pitch != static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(command.note)) + 48, 0, 96))) continue;
                    if (ids[pitch]) { workstation.recordNoteOff(ids[pitch]); ids[pitch] = 0; }
                }
            }
            if (command.index >= 0 && command.index < 32) {
                auto& padHolds = pendingHolds[static_cast<std::size_t>(command.index)];
                if (command.exactPitch && std::isfinite(command.note))
                    padHolds[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(command.note)) + 48, 0, 96))].store(false);
                else for (auto& hold : padHolds) hold.store(false);
            }
            if (pendingCuePad.load() == command.index
                && (!command.exactPitch || std::abs(pendingCueSemitoneOffset.load() - command.note) < .01f))
                pendingCueHeld.store(false);
            for (int slot = 0; slot < voiceLimit; ++slot) {
                auto& voice = voices[static_cast<std::size_t>(slot)];
                if (!voice.bank || voice.sequenced != command.sequenced) continue;
                if (command.sequenced) {
                    if (voice.track != command.track || (command.index >= 0 && voice.sequenceToken != command.sequenceToken)) continue;
                } else if (voice.pad != command.index) continue;
                if (command.exactPitch && std::abs(voice.note - command.note) >= .01f) continue;
                voice.held = false;
                if (command.sequenced && command.index < 0) { beginRelease(voice, 5); continue; }
                if (voice.bank->project.playbackMode == CuePlaybackMode::Trigger) continue;
                if (voice.atEnd) finishVoice(slot, true);
                else beginRelease(voice, bounded(padFor(voice).releaseMs, 0, 10000, 0));
            }
            return;
        }
        if (!bank || !bank->source || command.index < 0 || command.index >= 32) return;
        const auto& pad = bank->project.pads[static_cast<std::size_t>(command.index)];
        if (pad.empty() || pad.start >= bank->source->frames()) {
            if (command.sequenced) return;
            pendingCuePad.store(command.index);
            pendingCueFrame.store(std::min(displayFrame.load(), bank->source->frames() - 1));
            pendingCueVelocity.store(command.velocity); pendingCueSemitoneOffset.store(command.note); pendingCueHeld.store(true);
            pendingHolds[static_cast<std::size_t>(command.index)][static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(command.note)) + 48, 0, 96))].store(true);
            int eventStart = 0, eventSize = 0, unusedStart = 0, unusedSize = 0;
            assignmentFifo.prepareToWrite(1, eventStart, eventSize, unusedStart, unusedSize);
            if (eventSize > 0) {
                auto& event = assignments[static_cast<std::size_t>(eventStart)];
                event.cue = { command.index, pendingCueFrame.load(), command.velocity, command.note, true };
                event.generation = bank->sourceGeneration;
                event.cancellationGeneration = assignmentCancellation.load();
                assignmentFifo.finishedWrite(1);
            }
            pendingCueSerial.fetch_add(1, std::memory_order_release);
            return;
        }
        const int track = workstation.enabled() ? (command.track >= 0 ? command.track : workstation.selectedTrack()) : -1;
        int previous = -1;
        if (mono.load()) {
            for (int slot = 0; slot < voiceLimit; ++slot) {
                const auto& voice = voices[static_cast<std::size_t>(slot)];
                if (voice.bank && voice.held && !voice.atEnd && (!workstation.enabled() || voice.track == track)
                    && (!voice.releasing || voice.bank->project.playbackMode == CuePlaybackMode::Trigger)
                    && (previous < 0 || voice.serial > voices[static_cast<std::size_t>(previous)].serial)) previous = slot;
            }
        }
        const bool glide = previous >= 0 && keyboardMode.load() && bank->project.glideMs > 0;
        if (glide && bank->project.glideMode == GlideMode::Sustain
            && voices[static_cast<std::size_t>(previous)].pad == command.index) {
            auto& voice = voices[static_cast<std::size_t>(previous)];
            voice.bank = bank;
            hazards[static_cast<std::size_t>(previous + 1)].store(bank);
            voice.note = command.note;
            voice.targetPitch = command.note;
            voice.glideRemaining = static_cast<std::uint64_t>(bounded(bank->project.glideMs, .01f, 5000.f, .01f) * outputRate / 1000);
            voice.glideStep = (voice.targetPitch - voice.currentPitch) / static_cast<double>(std::max<std::uint64_t>(1, voice.glideRemaining));
            voice.velocity = command.velocity;
            voice.serial = ++serial;
            voice.sequenced = command.sequenced; voice.sequenceToken = command.sequenceToken; voice.track = track;
            recordInput(command, voice);
            displayedSerial = voice.serial;
            displayAudition = false;
            lastTriggeredPad.store(command.index);
            triggerSerial.fetch_add(1);
            return;
        }
        const double initialPitch = glide ? voices[static_cast<std::size_t>(previous)].currentPitch : command.note;
        if (mono.load()) for (auto& voice : voices) if (voice.bank && !voice.audition && (!workstation.enabled() || voice.track == track)) beginRelease(voice, 5);
        int slot = -1;
        for (int candidate = 0; candidate < voiceLimit; ++candidate) if (!voices[static_cast<std::size_t>(candidate)].bank) { slot = candidate; break; }
        if (slot < 0) {
            slot = 0;
            for (int candidate = 1; candidate < voiceLimit; ++candidate)
                if (voices[static_cast<std::size_t>(candidate)].serial < voices[static_cast<std::size_t>(slot)].serial) slot = candidate;
        }
        auto& voice = voices[static_cast<std::size_t>(slot)];
        voice = {};
        voice.bank = bank; voice.pad = command.index; voice.note = command.note; voice.velocity = command.velocity;
        voice.position = pad.reverse ? static_cast<double>(std::min(pad.end, bank->source->frames()) - 1) : static_cast<double>(pad.start);
        voice.inputPosition = voice.position; voice.currentPitch = initialPitch; voice.targetPitch = command.note;
        voice.mixMask = maskFor(voice); voice.serial = ++serial;
        voice.track = track; voice.sequenced = command.sequenced; voice.sequenceToken = command.sequenceToken;
        recordInput(command, voice);
        if (glide) {
            voice.glideRemaining = static_cast<std::uint64_t>(bounded(bank->project.glideMs, .01f, 5000.f, .01f) * outputRate / 1000);
            voice.glideStep = (voice.targetPitch - voice.currentPitch) / static_cast<double>(std::max<std::uint64_t>(1, voice.glideRemaining));
        }
        hazards[static_cast<std::size_t>(slot + 1)].store(bank);
        displayedSerial = voice.serial;
        displayAudition = false;
        lastTriggeredPad.store(command.index);
        triggerSerial.fetch_add(1);
        displayFrame.store(static_cast<std::size_t>(voice.position));
    }
    void recordInput(const Command& command, const Voice& voice) {
        if (command.sequenced || !workstation.enabled()) return;
        auto& id = recordingIds[static_cast<std::size_t>(command.index)][static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(command.note)) + 48, 0, 96))];
        if (id) workstation.recordNoteOff(id);
        id = voice.serial;
        workstation.recordNoteOn(id, voice.track, command.index, command.note, command.velocity);
    }
};

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() {
    { const std::lock_guard<std::mutex> lock(impl->midiDevicesMutex);
      for (const auto& identifier : impl->midiDevices) devices.removeMidiInputDeviceCallback(identifier, this); }
    if (impl->connected) devices.removeAudioCallback(&player);
    player.setSource(nullptr);
    devices.closeAudioDevice();
}
juce::String AudioEngine::initialise() {
    const auto error = devices.initialiseWithDefaultDevices(0, 2);
    if (!impl->connected) { player.setSource(this); devices.addAudioCallback(&player); impl->connected = true; }
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
    data.artist = metadataTag(reader->metadataValues, { "artist", "iart", "riffinfoartist", "aswgartist", "tpe1", "tp1", "performer", "author", "albumartist" });
    data.title = metadataTag(reader->metadataValues, { "title", "inam", "riffinfotitle", "aswgsongtitle", "tit2", "tt2", "tracktitle", "songtitle", "name" });
#if JUCE_MAC
    readNativeMetadata(file, data);
#endif
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

namespace {
void writeWaveFile(const juce::File& file, const AudioData& data, int bitsPerSample) {
    if (!data.valid() || data.channels.size() > 2 || data.frames() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("There is no valid audio to export.");
    juce::TemporaryFile temporary(file);
    std::unique_ptr<juce::FileOutputStream> stream(temporary.getFile().createOutputStream());
    if (!stream || !stream->openedOk())
        throw std::runtime_error("The export file could not be opened for writing.");
    juce::WavAudioFormat format;
    juce::StringPairArray metadata;
    if (!data.artist.empty()) metadata.set(juce::WavAudioFormat::riffInfoArtist, cleanMetadata(juce::String::fromUTF8(data.artist.c_str())));
    if (!data.title.empty()) metadata.set(juce::WavAudioFormat::riffInfoTitle, cleanMetadata(juce::String::fromUTF8(data.title.c_str())));
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        stream.get(), data.sampleRate, static_cast<unsigned int>(data.channels.size()), bitsPerSample, metadata, 0));
    if (!writer)
        throw std::runtime_error("The WAV encoder could not be created.");
    stream.release(); // The writer now owns the stream.
    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> block(static_cast<int>(data.channels.size()), blockSize);
    bool succeeded = true;
    for (std::size_t offset = 0; offset < data.frames(); offset += blockSize) {
        const auto count = static_cast<int>(std::min<std::size_t>(blockSize, data.frames() - offset));
        for (std::size_t channel = 0; channel < data.channels.size(); ++channel)
            for (int sample = 0; sample < count; ++sample) {
                const float value = data.channels[channel][offset + static_cast<std::size_t>(sample)];
                block.setSample(static_cast<int>(channel), sample, bitsPerSample == 32 ? value : std::clamp(value, -1.f, 1.f));
            }
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
} // namespace

void AudioEngine::writeAudio(const juce::File& file, const AudioData& data) {
    writeWaveFile(file, data, 24);
}

void AudioEngine::writeFloatAudio(const juce::File& file, const AudioData& data) {
    writeWaveFile(file, data, 32);
}

void AudioEngine::setSource(std::shared_ptr<const AudioData> source) {
    if (source && (!source->valid() || source->channels.size() > 2 || source->sampleRate < 8000
        || source->sampleRate > 384000 || source->frames() > maxFileFrames))
        throw std::runtime_error("Source audio must be mono or stereo at 8–384 kHz, with fewer than 32 million frames.");
    const std::lock_guard<std::mutex> lock(impl->sourceMutex);
    impl->source = std::move(source);
    impl->stems = {};
    impl->sourceGeneration.fetch_add(1);
    for (auto& padHolds : impl->pendingHolds) for (auto& hold : padHolds) hold.store(false);
    impl->currentBank.store(nullptr);
    impl->displayFrame.store(0);
    impl->pendingCuePad.store(-1);
    impl->lastTriggeredPad.store(-1);
    impl->resetSourceRequested.store(true);
    stop();
}
void AudioEngine::setStem(int index, std::shared_ptr<const AudioData> stem) {
    if (index < 0 || index >= 4) throw std::runtime_error("Unknown stem slot.");
    { const std::lock_guard<std::mutex> lock(impl->sourceMutex);
      if (!impl->source) throw std::runtime_error("Import source audio before loading aligned stems.");
      if (stem && (!stem->valid() || std::abs(stem->sampleRate - impl->source->sampleRate) > .01
          || stem->frames() != impl->source->frames() || stem->channels.size() != impl->source->channels.size()))
          throw std::runtime_error("Stems must match the source's exact sample rate, channel count, and duration.");
      impl->stems[static_cast<std::size_t>(index)] = std::move(stem); }
    impl->publish(nullptr);
}
void AudioEngine::clearStems() {
    { const std::lock_guard<std::mutex> lock(impl->sourceMutex); impl->stems = {}; }
    impl->publish(nullptr);
}
void AudioEngine::preparePads(const Project& project) { setLiveProject(project); }
void AudioEngine::setLiveProject(const Project& project) { impl->publish(&project); }
void AudioEngine::triggerPad(int index, float semitoneOffset, float velocity) {
    if (index < 0 || index >= 32 || !std::isfinite(semitoneOffset)) return;
    impl->enqueue({ Impl::CommandType::Trigger, index, std::clamp(semitoneOffset, -48.f, 48.f), bounded(velocity, 0, 1, 1), false, 0 });
}
void AudioEngine::releasePad(int index) { impl->enqueue({ Impl::CommandType::Release, index, 0, 1, false, 0 }); }
void AudioEngine::releasePad(int index, float semitoneOffset) { impl->enqueue({ Impl::CommandType::Release, index, semitoneOffset, 1, true, 0 }); }
void AudioEngine::stop() {
    impl->assignmentCancellation.fetch_add(1);
    if (!impl->enqueue({ Impl::CommandType::Stop })) impl->stopRequested.store(true);
}
void AudioEngine::playSource() { impl->enqueue({ Impl::CommandType::PlaySource }); }
void AudioEngine::pauseSource() { impl->enqueue({ Impl::CommandType::PauseSource }); }
void AudioEngine::seekSource(std::size_t frame) { Impl::Command command; command.type = Impl::CommandType::SeekSource; command.frame = frame; impl->enqueue(command); }
PlaybackSnapshot AudioEngine::playbackSnapshot() const {
    PlaybackSnapshot state;
    state.sourceFrame = impl->displayFrame.load(); state.sourcePlaying = impl->sourcePlaying.load();
    state.activeCueMask = impl->activeMask.load(); state.lastTriggeredPad = impl->lastTriggeredPad.load();
    state.triggerSerial = impl->triggerSerial.load();
    state.pendingCueSerial = impl->pendingCueSerial.load(std::memory_order_acquire);
    state.pendingCuePad = impl->pendingCuePad.load(); state.pendingCueFrame = impl->pendingCueFrame.load();
    state.pendingCueVelocity = impl->pendingCueVelocity.load(); state.pendingCueSemitoneOffset = impl->pendingCueSemitoneOffset.load();
    state.pendingCueHeld = impl->pendingCueHeld.load();
    state.peak = impl->peak.load();
    return state;
}
bool AudioEngine::popPendingCue(CueAssignment& assignment) {
    for (;;) {
        int start = 0, count = 0, unusedStart = 0, unusedCount = 0;
        impl->assignmentFifo.prepareToRead(1, start, count, unusedStart, unusedCount);
        if (count == 0) return false;
        const auto event = impl->assignments[static_cast<std::size_t>(start)];
        impl->assignmentFifo.finishedRead(1);
        if (event.generation != impl->sourceGeneration.load()
            || event.cancellationGeneration != impl->assignmentCancellation.load()) continue;
        assignment = event.cue;
        const auto note = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(assignment.semitoneOffset)) + 48, 0, 96));
        assignment.held = impl->pendingHolds[static_cast<std::size_t>(assignment.pad)][note].load();
        return true;
    }
}
void AudioEngine::setMetronome(bool enabled) { impl->metronome.store(enabled); }
void AudioEngine::setMasterGain(float gain) { impl->masterGain.store(finiteGain(gain)); }
void AudioEngine::setMono(bool mono) { impl->mono.store(mono); }
int AudioEngine::activeVoiceCount() const { return impl->activeVoices.load(); }
float AudioEngine::peakLevel() const { return impl->peak.load(); }
juce::String AudioEngine::deviceName() const { const auto* device = devices.getCurrentAudioDevice(); return device ? device->getName() : "No audio output"; }
void AudioEngine::setKeyboardMode(bool enabled, int selectedPad) {
    const int selected = selectedPad >= 0 && selectedPad < 32 ? selectedPad : -1;
    const int previous = impl->keyboardPad.exchange(selected);
    impl->keyboardMode.store(enabled && selected >= 0);
    if (previous != selected) {
        impl->publish(nullptr);
        impl->keyboardMode.store(enabled && selected >= 0);
    }
}
void AudioEngine::learnMidiPad(int padIndex) { impl->learningPad.store(padIndex >= 0 && padIndex < 32 ? padIndex : -1); }
juce::String AudioEngine::midiStatus() const {
    const int learning = impl->learningPad.load(); if (learning >= 0) return "Play a MIDI note for Cue " + juce::String(learning + 1);
    const int learned = impl->learnedPad.load(); if (learned >= 0) return "MIDI " + juce::String(impl->learnedNote.load()) + " → Cue " + juce::String(learned + 1);
    return impl->midiInputCount.load() > 0 ? "MIDI connected · notes 24–55" : "No MIDI input · computer keys available";
}
void AudioEngine::refreshMidiInputs() {
    const std::lock_guard<std::mutex> lock(impl->midiDevicesMutex);
    for (const auto& identifier : impl->midiDevices) devices.removeMidiInputDeviceCallback(identifier, this);
    impl->midiDevices.clear();
    for (const auto& input : juce::MidiInput::getAvailableDevices()) {
        devices.setMidiInputDeviceEnabled(input.identifier, true); devices.addMidiInputDeviceCallback(input.identifier, this); impl->midiDevices.push_back(input.identifier);
    }
    impl->midiInputCount.store(static_cast<int>(impl->midiDevices.size()));
}
AudioData AudioEngine::renderForExport(const Project& project, int padIndex) const {
    std::shared_ptr<const AudioData> source; std::array<std::shared_ptr<const AudioData>, 4> stems;
    { const std::lock_guard<std::mutex> lock(impl->sourceMutex); source = impl->source; stems = impl->stems; }
    return renderSelection(source, stems, project, padIndex);
}
void AudioEngine::setWorkstationState(const WorkstationState& state, bool restorePlugins) {
    impl->workstation.setState(state);
    impl->plugins.applyState(state, restorePlugins);
    setWorkstationEnabled(true);
}
void AudioEngine::setWorkstationEnabled(bool enabled) {
    Impl::Command command; command.type = Impl::CommandType::Workstation;
    command.workstation.type = WorkstationControl::Type::Enable; command.workstation.flag = enabled;
    if (!impl->enqueue(command)) throw std::runtime_error("The audio command queue is full. Try the workstation control again.");
}
void AudioEngine::playWorkstation(WorkstationPlayMode mode) {
    Impl::Command command; command.type = Impl::CommandType::Workstation;
    command.workstation.type = WorkstationControl::Type::Play; command.workstation.mode = mode;
    if (!impl->enqueue(command)) throw std::runtime_error("The audio command queue is full. Try Play again.");
}
void AudioEngine::stopWorkstation() {
    Impl::Command command; command.type = Impl::CommandType::Workstation; command.workstation.type = WorkstationControl::Type::Stop;
    if (!impl->enqueue(command)) impl->workstation.stop();
}
void AudioEngine::seekWorkstation(double beat) {
    Impl::Command command; command.type = Impl::CommandType::Workstation;
    command.workstation.type = WorkstationControl::Type::Seek; command.workstation.beat = std::isfinite(beat) ? beat : 0;
    if (!impl->enqueue(command)) throw std::runtime_error("The audio command queue is full. Try seeking again.");
}
void AudioEngine::setPatternRecording(bool enabled) {
    Impl::Command command; command.type = Impl::CommandType::Workstation;
    command.workstation.type = WorkstationControl::Type::Record; command.workstation.flag = enabled;
    if (!impl->enqueue(command)) { if (enabled) throw std::runtime_error("The audio command queue is full. Try recording again."); impl->workstation.setRecording(false); }
}
void AudioEngine::launchClip(int track, int scene) {
    Impl::Command command; command.type = Impl::CommandType::Workstation;
    command.workstation.type = WorkstationControl::Type::Launch; command.workstation.track = track; command.workstation.scene = scene;
    if (!impl->enqueue(command)) throw std::runtime_error("The audio command queue is full. Try launching again.");
}
void AudioEngine::stopClipTrack(int track) {
    Impl::Command command; command.type = Impl::CommandType::Workstation;
    command.workstation.type = WorkstationControl::Type::StopTrack; command.workstation.track = track;
    if (!impl->enqueue(command)) impl->workstation.stopTrack(track);
}
void AudioEngine::setAudioAsset(WorkstationId id, std::shared_ptr<const AudioData> audio) { impl->workstation.setAudioAsset(id, std::move(audio)); }
void AudioEngine::removeAudioAsset(WorkstationId id) { impl->workstation.removeAudioAsset(id); }
WorkstationSnapshot AudioEngine::workstationSnapshot() const { return impl->workstation.snapshot(); }
bool AudioEngine::popRecordedNote(RecordedWorkstationNote& note) { return impl->workstation.popRecordedNote(note); }
PluginHost& AudioEngine::pluginHost() { return impl->plugins; }
void AudioEngine::startMasterCapture(const juce::File& file) {
    impl->capture.start(file, impl->outputRate);
    Impl::Command command; command.type = Impl::CommandType::CaptureStart;
    if (!impl->enqueue(command)) { impl->capture.stop(); throw std::runtime_error("The audio command queue is full. The recording could not start."); }
}
void AudioEngine::stopMasterCapture() {
    Impl::Command command; command.type = Impl::CommandType::CaptureStop;
    if (!impl->enqueue(command)) impl->capture.stop();
}
CaptureSnapshot AudioEngine::captureSnapshot() const { return impl->capture.snapshot(); }

AudioData AudioEngine::bouncePattern(int patternIndex, int track) const {
    if (patternIndex < 0 || patternIndex >= 16 || track < 0 || track >= 8) throw std::runtime_error("Choose a valid pattern and mixer track to freeze.");
    const auto workstation = impl->workstation.stateForControl();
    std::shared_ptr<const AudioData> source;
    std::array<std::shared_ptr<const AudioData>, 4> stems;
    Project project;
    { const std::lock_guard<std::mutex> lock(impl->sourceMutex); source = impl->source; stems = impl->stems; project = impl->lastProject; }
    if (!source) throw std::runtime_error("Import a sampler source before freezing a pattern.");
    const auto& pattern = workstation.patterns[static_cast<std::size_t>(patternIndex)];
    const auto& settings = workstation.tracks[static_cast<std::size_t>(track)];
    const double rate = source->sampleRate;
    const double framesPerBeat = rate * 60.0 / workstation.tempo;
    const double requested = std::round(pattern.lengthBeats * framesPerBeat);
    if (!std::isfinite(requested) || requested < 1 || requested > 32.0 * 1024 * 1024)
        throw std::runtime_error("This pattern is too long to freeze in memory. Shorten the pattern first.");
    const auto frames = static_cast<std::size_t>(requested);
    AudioData result; result.sampleRate = rate; result.channels.assign(2, std::vector<float>(frames));
    for (std::size_t noteIndex = 0; noteIndex < pattern.notes.size(); ++noteIndex) {
        const auto& note = pattern.notes[noteIndex];
        Project effective = project;
        auto& pad = effective.pads[static_cast<std::size_t>(note.pad)];
        if (pad.empty()) continue;
        pad.pitchSemitones += note.semitones;
        const auto onset = static_cast<std::size_t>(std::llround(note.beat * framesPerBeat));
        const auto held = static_cast<std::size_t>(std::llround(note.duration * framesPerBeat));
        const auto attack = static_cast<std::size_t>(bounded(pad.attackMs, 0, 10000, 0) * rate / 1000);
        const auto release = static_cast<std::size_t>(bounded(pad.releaseMs, 0, 10000, 0) * rate / 1000);
        const bool hold = project.playbackMode == CuePlaybackMode::Hold;
        const auto gate = hold ? held : attack;
        auto length = gate + release;
        if (length == 0) continue;
        std::size_t nextOnset = std::numeric_limits<std::size_t>::max();
        if (project.mono) for (std::size_t nextIndex = 0; nextIndex < pattern.notes.size(); ++nextIndex) {
            const auto& next = pattern.notes[nextIndex];
            const double nextBeat = next.beat > note.beat || (next.beat == note.beat && nextIndex > noteIndex)
                ? next.beat : next.beat + pattern.lengthBeats;
            nextOnset = std::min(nextOnset, static_cast<std::size_t>(std::llround((nextBeat - note.beat) * framesPerBeat)));
        }
        const auto stealFade = static_cast<std::size_t>(rate * .005);
        if (nextOnset != std::numeric_limits<std::size_t>::max()) length = std::min(length, nextOnset + stealFade);
        // Render only the portion this note can sound. An open-ended cue over a
        // long source must not copy/render the entire remainder for every note.
        double speed = bounded(pad.speedRatio, .25f, 4.f, 1.f);
        if (project.sourceBpm > 0 && std::isfinite(project.sourceBpm) && std::isfinite(project.projectBpm))
            speed *= std::clamp(project.projectBpm / project.sourceBpm, .25, 4.0);
        if (!project.pitchTimeEnabled) {
            double pitch = pad.pitchSemitones + project.globalPitchSemitones + (pad.detuneCents + project.globalDetuneCents) / 100.0;
            if (project.syncKey) pitch += keySemitoneOffset(project.sourceKey, project.projectKey);
            speed *= std::exp2(std::clamp(pitch, -60.0, 60.0) / 12.0);
        }
        speed = std::clamp(speed, 1.0 / 32.0, 32.0);
        const auto inputFrames = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(length * speed)));
        pad.end = std::min(pad.end, source->frames());
        if (pad.end <= pad.start) continue;
        if (inputFrames < pad.end - pad.start) {
            if (pad.reverse) pad.start = pad.end - inputFrames;
            else pad.end = pad.start + inputFrames;
        }
        // Apply the performance envelope once, including a frozen attack level
        // when a Hold gate is released before the attack reaches full level.
        effective.playbackMode = CuePlaybackMode::Hold;
        pad.attackMs = pad.releaseMs = 0;
        const auto rendered = renderSelection(source, stems, effective, note.pad);
        if (!rendered.valid()) continue;
        length = std::min(length, rendered.frames());
        const auto envelopeAt = [&](std::size_t sample) {
            const float attackLevel = attack == 0 ? 1.f : static_cast<float>(std::min(1.0, static_cast<double>(std::min(sample, gate)) / static_cast<double>(attack)));
            return sample < gate ? attackLevel : attackLevel * (release == 0 ? 0.f : std::max(0.f, 1.f - static_cast<float>(sample - gate) / static_cast<float>(release)));
        };
        const float velocity = project.velocityEnabled ? note.velocity : 1.f;
        for (std::size_t sample = 0; sample < length; ++sample) {
            float envelope = envelopeAt(sample);
            if (sample >= nextOnset && gate + release > nextOnset + stealFade)
                envelope = envelopeAt(nextOnset) * (stealFade == 0 ? 0.f : std::max(0.f, 1.f - static_cast<float>(sample - nextOnset) / static_cast<float>(stealFade)));
            // The bounce contains one period; sampler release tails crossing
            // that boundary are folded into the start of the audio clip.
            const auto destination = (onset + sample) % frames;
            for (std::size_t channel = 0; channel < 2; ++channel)
                result.channels[channel][destination] += rendered.channels[std::min(channel, rendered.channels.size() - 1)][sample] * envelope * velocity;
        }
    }
    std::array<FxRack, 4> racks;
    for (auto& rack : racks) rack.prepare(rate, 512);
    racks[0].setSlots(settings.effects); racks[1].setSlots(workstation.sendAEffects);
    racks[2].setSlots(workstation.sendBEffects); racks[3].setSlots(workstation.masterEffects);
    const bool solo = std::any_of(workstation.tracks.begin(), workstation.tracks.end(), [](const auto& t) { return t.solo; });
    float gain = settings.mute || (solo && !settings.solo) ? 0 : std::pow(10.f, settings.gainDb / 20.f);
    if (settings.crossfadeAssignment == CrossfadeAssignment::A) gain *= static_cast<float>(std::cos(workstation.crossfader * pi * .5));
    if (settings.crossfadeAssignment == CrossfadeAssignment::B) gain *= static_cast<float>(std::sin(workstation.crossfader * pi * .5));
    const std::array<float, 2> pan { std::sqrt(1.f - std::max(0.f, settings.pan)), std::sqrt(1.f + std::min(0.f, settings.pan)) };
    const float master = std::pow(10.f, workstation.masterGainDb / 20.f) * impl->masterGain.load();
    std::array<std::array<float, 512>, 4> sends {};
    for (std::size_t offset = 0; offset < frames; offset += 512) {
        const auto count = std::min<std::size_t>(512, frames - offset);
        auto* left = result.channels[0].data() + offset; auto* right = result.channels[1].data() + offset;
        racks[0].processStereo(left, right, count);
        for (std::size_t sample = 0; sample < count; ++sample) {
            left[sample] *= gain * pan[0]; right[sample] *= gain * pan[1];
            sends[0][sample] = left[sample] * settings.sendA; sends[1][sample] = right[sample] * settings.sendA;
            sends[2][sample] = left[sample] * settings.sendB; sends[3][sample] = right[sample] * settings.sendB;
        }
        racks[1].processStereo(sends[0].data(), sends[1].data(), count); racks[2].processStereo(sends[2].data(), sends[3].data(), count);
        for (std::size_t sample = 0; sample < count; ++sample) { left[sample] += sends[0][sample] + sends[2][sample]; right[sample] += sends[1][sample] + sends[3][sample]; }
        racks[3].processStereo(left, right, count);
        for (std::size_t sample = 0; sample < count; ++sample) { left[sample] = std::clamp(left[sample] * master, -1.f, 1.f); right[sample] = std::clamp(right[sample] * master, -1.f, 1.f); }
    }
    return result;
}

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    impl->outputRate = std::isfinite(sampleRate) && sampleRate > 0 ? std::clamp(sampleRate, 8000.0, 384000.0) : 44100;
    for (int slot = 0; slot < totalVoices; ++slot) { impl->finishVoice(slot, false); impl->processors[static_cast<std::size_t>(slot)].prepare(impl->outputRate); }
    impl->activeVoices.store(0); impl->activeMask.store(0);
    impl->maximumBlock = static_cast<std::size_t>(std::clamp(samplesPerBlockExpected, 512, 16384));
    impl->masterEnvelope.resize(impl->maximumBlock);
    impl->workstation.prepare(impl->outputRate, impl->maximumBlock);
    for (std::size_t bus = 0; bus < impl->buses.size(); ++bus) {
        impl->buses[bus].setSize(2, static_cast<int>(impl->maximumBlock));
        impl->effects[bus].prepare(impl->outputRate, impl->maximumBlock);
    }
    impl->plugins.prepare(impl->outputRate, static_cast<int>(impl->maximumBlock));
}
void AudioEngine::releaseResources() {
    impl->capture.stop();
    for (int slot = 0; slot < totalVoices; ++slot) impl->finishVoice(slot, false);
    impl->activeVoices.store(0); impl->activeMask.store(0); impl->peak.store(0);
}
void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    const juce::ScopedNoDenormals noDenormals;
    info.clearActiveBufferRegion();
    if (!info.buffer || info.numSamples <= 0) return;
    if (impl->maximumBlock > 0 && static_cast<std::size_t>(info.numSamples) > impl->maximumBlock) {
        for (int offset = 0; offset < info.numSamples; offset += static_cast<int>(impl->maximumBlock)) {
            const juce::AudioSourceChannelInfo chunk(info.buffer, info.startSample + offset, std::min(static_cast<int>(impl->maximumBlock), info.numSamples - offset));
            getNextAudioBlock(chunk);
        }
        return;
    }
    impl->workstation.beginBlock();
    const auto* bank = impl->pinCurrentBank();
    if (impl->resetSourceRequested.exchange(false)) {
        for (int slot = 0; slot < totalVoices; ++slot) impl->finishVoice(slot, false);
        impl->auditionPosition = 0; impl->displayAudition = true; impl->displayFrame.store(0);
    }
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    impl->fifo.prepareToRead(commandCapacity, start1, size1, start2, size2);
    for (int offset = 0; offset < size1; ++offset) impl->applyCommand(impl->commands[static_cast<std::size_t>(start1 + offset)], bank);
    for (int offset = 0; offset < size2; ++offset) impl->applyCommand(impl->commands[static_cast<std::size_t>(start2 + offset)], bank);
    impl->fifo.finishedRead(size1 + size2);
    if (impl->stopRequested.exchange(false)) impl->applyCommand({ Impl::CommandType::Stop }, bank);
    const bool studio = impl->workstation.enabled() && impl->maximumBlock > 0;
    const auto* studioState = impl->workstation.currentState();
    const double studioBlockBeat = impl->workstation.currentBeat();
    if (studio) for (auto& bus : impl->buses) bus.clear(0, info.numSamples);
    // Adopt fresh controls and aligned stem streams at a block boundary, while
    // the source identity is unchanged. Every raw bank remains hazard-protected.
    for (int slot = 0; slot < totalVoices; ++slot) {
        auto& voice = impl->voices[static_cast<std::size_t>(slot)];
        if (!voice.bank || !bank || voice.bank == bank || voice.bank->source != bank->source) continue;
        const auto previousMask = voice.mixMask;
        const bool previousStems = voice.bank->hasStems;
        const bool previousReverse = impl->padFor(voice).reverse;
        voice.bank = bank; impl->hazards[static_cast<std::size_t>(slot + 1)].store(bank);
        voice.mixMask = impl->maskFor(voice);
        // Reset queued DSP audio on a mute/solo change, so a previous mix cannot
        // leak into the next callback. This can cause a short transition at
        // non-unity pitch, but never defers a stem mute until the next trigger.
        if (voice.mixMask != previousMask || bank->hasStems != previousStems || impl->padFor(voice).reverse != previousReverse)
            voice.processorReady = false;
    }
    struct Controls {
        double speed = 1;
        std::uint64_t attack = 0, release = 0;
        float gain = 1, alpha = 0;
        int firstChannel = 0;
        bool processed = false, filtered = false, lowPass = false;
    };
    std::array<Controls, totalVoices> controls;
    std::array<std::uint64_t, totalVoices> controlSerial {};
    const auto updateControl = [&](int slot) {
        const auto& voice = impl->voices[static_cast<std::size_t>(slot)];
        if (!voice.bank || !voice.bank->source) return;
        controlSerial[static_cast<std::size_t>(slot)] = voice.serial;
        const auto& pad = impl->padFor(voice);
        const auto& project = voice.bank->project;
        auto& control = controls[static_cast<std::size_t>(slot)];
        control.speed = impl->speedFor(voice);
        const double pitch = impl->pitchFor(voice);
        control.processed = project.pitchTimeEnabled && (std::abs(pitch) > 1e-6 || std::abs(control.speed - 1) > 1e-6 || voice.glideRemaining > 0);
        control.attack = static_cast<std::uint64_t>(bounded(pad.attackMs, 0, 10000, 0) * impl->outputRate / 1000);
        control.release = static_cast<std::uint64_t>(bounded(pad.releaseMs, 0, 10000, 0) * impl->outputRate / 1000);
        control.gain = voice.audition ? 1.f : std::pow(10.f, bounded(pad.levelDb, -12, 12, 0) / 20.f) * (project.velocityEnabled ? voice.velocity : 1.f);
        const float filter = voice.audition ? .5f : bounded(pad.filter, 0, 1, .5f);
        control.filtered = std::abs(filter - .5f) > .0001f;
        control.lowPass = filter < .5f;
        if (control.filtered) {
            const double cutoff = control.lowPass ? 80.0 * std::pow(17000.0 / 80.0, filter * 2.0)
                                                   : 80.0 * std::pow(17000.0 / 80.0, (filter - .5) * 2.0);
            control.alpha = static_cast<float>(1 - std::exp(-2 * pi * cutoff / impl->outputRate));
        }
        control.firstChannel = voice.audition ? 0 : std::clamp(pad.outputChannel, 0, 31) * 2;
        if (control.firstChannel + 1 >= info.buffer->getNumChannels()) control.firstChannel = 0;
    };
    for (int slot = 0; slot < totalVoices; ++slot) updateControl(slot);
    struct SchedulerContext { Impl* engine; const Impl::Bank* bank; } scheduler { impl.get(), bank };
    const auto eventCallback = +[](void* opaque, const ScheduledPadEvent& event) {
        const auto& context = *static_cast<SchedulerContext*>(opaque);
        Impl::Command command;
        command.type = event.noteOn ? Impl::CommandType::Trigger : Impl::CommandType::Release;
        command.index = event.pad; command.note = event.semitones; command.velocity = event.velocity;
        command.track = event.track; command.sequenced = true; command.sequenceToken = event.token;
        context.engine->applyCommand(command, context.bank);
    };
    float blockPeak = 0;
    const auto gain = impl->masterGain.load();
    for (int sample = 0; sample < info.numSamples; ++sample) {
        std::array<float, 8> clipsLeft {}, clipsRight {};
        impl->workstation.tick(eventCallback, &scheduler, clipsLeft.data(), clipsRight.data());
        if (studio) for (std::size_t track = 0; track < 8; ++track) {
            impl->buses[track].setSample(0, sample, clipsLeft[track]); impl->buses[track].setSample(1, sample, clipsRight[track]);
        }
        impl->smoothedGain += .001f * (gain - impl->smoothedGain);
        if (studio) impl->masterEnvelope[static_cast<std::size_t>(sample)] = impl->smoothedGain;
        for (int slot = 0; slot < totalVoices; ++slot) {
            auto& voice = impl->voices[static_cast<std::size_t>(slot)];
            if (!voice.bank || !voice.bank->source) continue;
            if (controlSerial[static_cast<std::size_t>(slot)] != voice.serial) updateControl(slot);
            const auto& pad = impl->padFor(voice);
            const auto& project = voice.bank->project;
            const auto& control = controls[static_cast<std::size_t>(slot)];
            if (voice.atEnd) continue;
            if (voice.glideRemaining > 0) { voice.currentPitch += voice.glideStep; if (--voice.glideRemaining == 0) voice.currentPitch = voice.targetPitch; }
            float envelope = voice.audition || control.attack == 0 ? 1.f : static_cast<float>(std::min(1.0, static_cast<double>(voice.age) / static_cast<double>(control.attack)));
            if (!voice.audition && project.playbackMode == CuePlaybackMode::Trigger && !voice.releasing) {
                if (voice.age >= control.attack) impl->beginRelease(voice, bounded(pad.releaseMs, 0, 10000, 0));
            }
            if (voice.releasing) {
                if (voice.releaseAge >= voice.releaseLength) { impl->finishVoice(slot, !voice.audition && project.playbackMode == CuePlaybackMode::Hold); continue; }
                envelope = voice.releaseStart * (1.f - static_cast<float>(voice.releaseAge) / static_cast<float>(voice.releaseLength));
                ++voice.releaseAge;
            }
            if (!impl->inside(voice, voice.position)) {
                const auto endpoint = pad.reverse ? pad.start : std::min(pad.end, voice.bank->source->frames());
                if (voice.audition) { impl->auditionPosition = static_cast<double>(endpoint); if (impl->displayAudition) impl->displayFrame.store(endpoint); impl->finishVoice(slot, false); }
                else if (project.playbackMode == CuePlaybackMode::Hold && voice.held && !voice.releasing) {
                    voice.atEnd = true; if (voice.serial == impl->displayedSerial) impl->displayFrame.store(endpoint);
                } else { if (voice.serial == impl->displayedSerial) impl->displayFrame.store(endpoint); impl->finishVoice(slot, project.playbackMode == CuePlaybackMode::Hold); }
                continue;
            }
            const double speed = !project.pitchTimeEnabled && voice.glideRemaining > 0 ? impl->speedFor(voice) : control.speed;
            const bool processed = control.processed;
            std::array<float, 2> values {};
            if (processed) {
                if (!voice.processedLast) voice.processorReady = false;
                if (!impl->processedSample(slot, values)) {
                    // A processor can return no output briefly while filling its
                    // analysis window. Do not advance the musical playhead.
                    voice.processedLast = true;
                    const auto& processor = impl->processors[static_cast<std::size_t>(slot)];
                    if (processor.final && processor.stretch && processor.stretch->available() <= 0)
                        voice.position = pad.reverse ? static_cast<double>(pad.start) - 1 : static_cast<double>(pad.end);
                    continue;
                }
            } else values = impl->readMixed(voice, voice.position);
            voice.processedLast = processed;
            for (std::size_t channel = 0; channel < 2; ++channel) {
                float value = values[channel];
                if (control.filtered) { voice.filterState[channel] += control.alpha * (value - voice.filterState[channel]); value = control.lowPass ? voice.filterState[channel] : value - voice.filterState[channel]; }
                const int outputChannel = control.firstChannel + static_cast<int>(channel);
                if (studio) {
                    const auto track = static_cast<std::size_t>(voice.track >= 0 ? voice.track : impl->workstation.selectedTrack());
                    impl->buses[track].addSample(static_cast<int>(channel), sample, value * envelope * control.gain);
                } else if (outputChannel < info.buffer->getNumChannels()) info.buffer->addSample(outputChannel, info.startSample + sample, value * envelope * control.gain * impl->smoothedGain);
            }
            const double direction = pad.reverse ? -1 : 1;
            voice.position += direction * voice.bank->source->sampleRate / impl->outputRate * speed;
            ++voice.age;
            if (voice.audition) { impl->auditionPosition = voice.position; if (impl->displayAudition) impl->displayFrame.store(static_cast<std::size_t>(std::clamp(voice.position, 0.0, static_cast<double>(voice.bank->source->frames())))); }
            else if (!impl->displayAudition && voice.serial == impl->displayedSerial) impl->displayFrame.store(static_cast<std::size_t>(std::clamp(voice.position, static_cast<double>(pad.start), static_cast<double>(std::min(pad.end, voice.bank->source->frames())))));
        }
        if (impl->metronome.load(std::memory_order_relaxed) && impl->sourcePlaying.load(std::memory_order_relaxed)
            && bank && bank->source && bank->project.beatgridEnabled) {
            std::size_t markerFrame = 0;
            double bpm = std::isfinite(bank->project.sourceBpm) ? std::clamp(bank->project.sourceBpm, 20.0, 400.0) : 120.0;
            const auto& markers = bank->project.tempoMarkers;
            const auto position = static_cast<std::size_t>(std::max(0.0, impl->auditionPosition));
            const auto marker = std::upper_bound(markers.begin(), markers.end(), position,
                [](std::size_t frame, const TempoMarker& item) { return frame < item.frame; });
            if (marker != markers.begin()) {
                const auto& active = *std::prev(marker);
                markerFrame = active.frame;
                if (std::isfinite(active.bpm)) bpm = std::clamp(active.bpm, 20.0, 400.0);
            }
            const auto beat = static_cast<std::int64_t>(std::floor((impl->auditionPosition - static_cast<double>(markerFrame))
                / (bank->source->sampleRate * 60.0 / bpm)));
            if (beat != impl->metronomeBeat || markerFrame != impl->metronomeMarker) {
                impl->metronomeBeat = beat; impl->metronomeMarker = markerFrame;
                impl->metronomeLength = std::max(1, static_cast<int>(impl->outputRate * .025));
                impl->metronomeRemaining = impl->metronomeLength; impl->metronomePhase = 0;
                impl->metronomeFrequency = beat % 4 == 0 ? 1600 : 1100;
            }
            if (impl->metronomeRemaining > 0) {
                const float amplitude = .2f * static_cast<float>(impl->metronomeRemaining) / static_cast<float>(impl->metronomeLength);
                const float click = amplitude * static_cast<float>(std::sin(impl->metronomePhase)) * impl->smoothedGain;
                impl->metronomePhase += 2 * pi * impl->metronomeFrequency / impl->outputRate;
                --impl->metronomeRemaining;
                for (int channel = 0; channel < std::min(2, info.buffer->getNumChannels()); ++channel)
                    info.buffer->addSample(channel, info.startSample + sample, click);
            }
        } else impl->metronomeRemaining = 0;
        impl->workstation.advance();
    }
    if (studio && studioState) {
        impl->plugins.setTransport(studioState->tempo, studioBlockBeat, impl->workstation.playing());
        const bool anySolo = std::any_of(studioState->tracks.begin(), studioState->tracks.end(), [](const auto& track) { return track.solo; });
        for (std::size_t track = 0; track < 8; ++track) {
            const auto& settings = studioState->tracks[track];
            auto& bus = impl->buses[track];
            impl->effects[track].setSlots(settings.effects);
            impl->effects[track].processStereo(bus.getWritePointer(0), bus.getWritePointer(1), static_cast<std::size_t>(info.numSamples));
            impl->plugins.processStereo(static_cast<int>(track), bus.getWritePointer(0), bus.getWritePointer(1), info.numSamples);
            float fader = std::pow(10.f, settings.gainDb / 20.f);
            if (settings.mute || (anySolo && !settings.solo)) fader = 0;
            if (settings.crossfadeAssignment == CrossfadeAssignment::A) fader *= static_cast<float>(std::cos(studioState->crossfader * pi * .5));
            if (settings.crossfadeAssignment == CrossfadeAssignment::B) fader *= static_cast<float>(std::sin(studioState->crossfader * pi * .5));
            const float targetLeft = fader * std::sqrt(1.f - std::max(0.f, settings.pan));
            const float targetRight = fader * std::sqrt(1.f + std::min(0.f, settings.pan));
            const float smoothing = static_cast<float>(1 - std::exp(-1.0 / (.005 * impl->outputRate)));
            float trackPeak = 0;
            for (int sample = 0; sample < info.numSamples; ++sample) {
                impl->trackLeftGain[track] += smoothing * (targetLeft - impl->trackLeftGain[track]);
                impl->trackRightGain[track] += smoothing * (targetRight - impl->trackRightGain[track]);
                const float left = bus.getSample(0, sample) * impl->trackLeftGain[track], right = bus.getSample(1, sample) * impl->trackRightGain[track];
                trackPeak = std::max({ trackPeak, std::abs(left), std::abs(right) });
                impl->buses[10].addSample(0, sample, left); impl->buses[10].addSample(1, sample, right);
                impl->buses[8].addSample(0, sample, left * settings.sendA); impl->buses[8].addSample(1, sample, right * settings.sendA);
                impl->buses[9].addSample(0, sample, left * settings.sendB); impl->buses[9].addSample(1, sample, right * settings.sendB);
            }
            impl->workstation.setTrackPeak(static_cast<int>(track), trackPeak);
        }
        impl->effects[8].setSlots(studioState->sendAEffects); impl->effects[9].setSlots(studioState->sendBEffects);
        for (std::size_t send = 8; send < 10; ++send) {
            auto& bus = impl->buses[send];
            impl->effects[send].processStereo(bus.getWritePointer(0), bus.getWritePointer(1), static_cast<std::size_t>(info.numSamples));
            impl->buses[10].addFrom(0, 0, bus, 0, 0, info.numSamples); impl->buses[10].addFrom(1, 0, bus, 1, 0, info.numSamples);
        }
        auto& master = impl->buses[10];
        impl->effects[10].setSlots(studioState->masterEffects);
        impl->effects[10].processStereo(master.getWritePointer(0), master.getWritePointer(1), static_cast<std::size_t>(info.numSamples));
        impl->plugins.processStereo(8, master.getWritePointer(0), master.getWritePointer(1), info.numSamples);
        const float masterLevel = std::pow(10.f, studioState->masterGainDb / 20.f);
        for (int channel = 0; channel < std::min(2, info.buffer->getNumChannels()); ++channel)
            for (int sample = 0; sample < info.numSamples; ++sample)
                info.buffer->addSample(channel, info.startSample + sample, master.getSample(channel, sample) * masterLevel * impl->masterEnvelope[static_cast<std::size_t>(sample)]);
    }
    for (int sample = 0; sample < info.numSamples; ++sample) {
        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel) {
            const float raw = info.buffer->getSample(channel, info.startSample + sample);
            const float value = std::isfinite(raw) ? std::clamp(raw, -1.f, 1.f) : 0;
            info.buffer->setSample(channel, info.startSample + sample, value); blockPeak = std::max(blockPeak, std::abs(value));
        }
        const float left = info.buffer->getNumChannels() > 0 ? info.buffer->getSample(0, info.startSample + sample) : 0;
        const float right = info.buffer->getNumChannels() > 1 ? info.buffer->getSample(1, info.startSample + sample) : left;
        impl->capture.push(left, right);
    }
    int count = 0; std::uint32_t mask = 0;
    for (int slot = 0; slot < voiceLimit; ++slot) { const auto& voice = impl->voices[static_cast<std::size_t>(slot)]; if (voice.bank) { ++count; mask |= std::uint32_t(1) << voice.pad; } }
    impl->activeVoices.store(count); impl->activeMask.store(mask); impl->peak.store(blockPeak);
    impl->hazards[0].store(nullptr);
    impl->workstation.endBlock();
}
void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (message.isAllNotesOff() || message.isAllSoundOff()) { stop(); return; }
    if (!message.isNoteOnOrOff()) return;
    const auto note = static_cast<std::size_t>(message.getNoteNumber());
    if (message.isNoteOn()) {
        const int learning = impl->learningPad.exchange(-1);
        if (learning >= 0) { for (auto& mapping : impl->midiMap) if (mapping.load() == learning) mapping.store(-1); impl->midiMap[note].store(learning); impl->learnedPad.store(learning); impl->learnedNote.store(static_cast<int>(note)); }
        const bool keyboard = impl->keyboardMode.load();
        const int pad = keyboard ? impl->keyboardPad.load() : impl->midiMap[note].load();
        if (pad < 0) return;
        const float semitones = keyboard ? std::clamp(static_cast<float>(static_cast<int>(note) - 60), -48.f, 48.f) : 0;
        impl->playingMidiPad[note].store(pad); impl->playingMidiOffset[note].store(semitones);
        triggerPad(pad, semitones, message.getFloatVelocity());
    } else { const int pad = impl->playingMidiPad[note].exchange(-1); if (pad >= 0) releasePad(pad, impl->playingMidiOffset[note].load()); }
}
} // namespace samplify
