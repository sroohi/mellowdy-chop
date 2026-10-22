#include "audio/AudioEngine.h"
#include "dsp/Effects.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

namespace heapProbe {
thread_local bool enabled = false;
thread_local std::size_t cppNew = 0, cppDelete = 0, cAllocate = 0, cFree = 0;
void reset() { cppNew = cppDelete = cAllocate = cFree = 0; }
}
void* operator new(std::size_t n) {
    if (heapProbe::enabled) ++heapProbe::cppNew;
    if (auto* p = std::malloc(std::max<std::size_t>(1, n))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if (heapProbe::enabled && p) ++heapProbe::cppDelete; std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void* operator new(std::size_t n, std::align_val_t a) {
    if (heapProbe::enabled) ++heapProbe::cppNew;
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(a), std::max<std::size_t>(1, n)) == 0) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new(n, a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new(n, a); } catch (...) { return nullptr; } }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { ::operator delete(p); }
#if defined(SAMPLIFY_WRAP_C_ALLOCATIONS)
extern "C" {
void* __real_malloc(std::size_t) noexcept;
void* __real_calloc(std::size_t, std::size_t) noexcept;
void* __real_realloc(void*, std::size_t) noexcept;
void __real_free(void*) noexcept;
int __real_posix_memalign(void**, std::size_t, std::size_t) noexcept;
void* __real_aligned_alloc(std::size_t, std::size_t) noexcept;
void* __wrap_malloc(std::size_t n) noexcept { if (heapProbe::enabled) ++heapProbe::cAllocate; return __real_malloc(n); }
void* __wrap_calloc(std::size_t n, std::size_t s) noexcept { if (heapProbe::enabled) ++heapProbe::cAllocate; return __real_calloc(n, s); }
void* __wrap_realloc(void* p, std::size_t n) noexcept { if (heapProbe::enabled) ++heapProbe::cAllocate; return __real_realloc(p, n); }
void __wrap_free(void* p) noexcept { if (heapProbe::enabled && p) ++heapProbe::cFree; __real_free(p); }
int __wrap_posix_memalign(void** p, std::size_t a, std::size_t n) noexcept { if (heapProbe::enabled) ++heapProbe::cAllocate; return __real_posix_memalign(p, a, n); }
void* __wrap_aligned_alloc(std::size_t a, std::size_t n) noexcept { if (heapProbe::enabled) ++heapProbe::cAllocate; return __real_aligned_alloc(a, n); }
}
#endif

namespace {
using namespace samplify;
constexpr double pi = 3.14159265358979323846;
struct Checks {
    int count = 0, failures = 0;
    void check(bool condition, const char* text) {
        ++count;
        if (!condition) { ++failures; std::cerr << "FAIL: " << text << '\n'; }
    }
    template<class Fn> void section(const char* title, Fn&& fn) {
        const int before = failures;
        try { fn(); } catch (const std::exception& e) { ++failures; std::cerr << "FAIL: " << title << ": " << e.what() << '\n'; }
        std::cout << (before == failures ? "PASS: " : "FAIL: ") << title << '\n';
    }
};

AudioData constant(float value = .2f, std::size_t frames = 64000, double rate = 8000) {
    AudioData a; a.sampleRate = rate; a.channels.assign(2, std::vector<float>(frames, value)); return a;
}
Project projectFor(const AudioData& a) {
    Project p; p.mono = false; p.sourceBpm = p.projectBpm = 120;
    for (auto& pad : p.pads) { pad.start = 0; pad.end = a.frames(); pad.attackMs = pad.releaseMs = 0; }
    return p;
}
void block(AudioEngine& engine, juce::AudioBuffer<float>& output, int count = -1) {
    engine.getNextAudioBlock(juce::AudioSourceChannelInfo(&output, 0, count < 0 ? output.getNumSamples() : count));
}
void settle(AudioEngine& engine, int blocks = 4, int size = 256) {
    juce::AudioBuffer<float> output(2, size);
    for (int i = 0; i < blocks; ++i) block(engine, output);
}
AudioData render(AudioEngine& engine, std::size_t frames, double rate = 8000) {
    AudioData a; a.sampleRate = rate; a.channels.assign(2, std::vector<float>(frames));
    juce::AudioBuffer<float> output(2, 256);
    for (std::size_t i = 0; i < frames; i += 256) {
        const int n = static_cast<int>(std::min<std::size_t>(256, frames - i));
        block(engine, output, n);
        for (int ch = 0; ch < 2; ++ch) std::copy_n(output.getReadPointer(ch), n, a.channels[static_cast<std::size_t>(ch)].begin() + static_cast<std::ptrdiff_t>(i));
    }
    return a;
}
double rms(const std::vector<float>& a) {
    double energy = 0; for (const auto value : a) energy += value * value;
    return a.empty() ? 0 : std::sqrt(energy / static_cast<double>(a.size()));
}
float peak(const std::vector<float>& a, std::size_t begin, std::size_t end) {
    float p = 0; for (auto i = begin; i < std::min(end, a.size()); ++i) p = std::max(p, std::abs(a[i])); return p;
}
void prepare(AudioEngine& engine, const AudioData& source, const WorkstationState& w, int blockSize = 256) {
    engine.setSource(std::make_shared<AudioData>(source));
    engine.preparePads(projectFor(source));
    engine.setMasterGain(1);
    engine.prepareToPlay(blockSize, source.sampleRate);
    engine.setWorkstationState(w);
    settle(engine, 48, blockSize); // Settle master control smoothing while stopped.
}

struct TimedEvent { std::uint64_t frame = 0; ScheduledPadEvent event; };
struct EventLog {
    std::array<TimedEvent, 256> events;
    std::size_t count = 0;
    std::size_t stopCount = 0;
    int lastStoppedTrack = -1;
    std::uint64_t frame = 0;
    static void receive(void* opaque, const ScheduledPadEvent& event) {
        auto& log = *static_cast<EventLog*>(opaque);
        if (event.pad < 0) { ++log.stopCount; log.lastStoppedTrack = event.track; return; }
        if (log.count < log.events.size()) log.events[log.count++] = {log.frame, event};
    }
};
void ticks(WorkstationRuntime& runtime, EventLog& log, std::size_t frames) {
    for (std::size_t done = 0; done < frames;) {
        runtime.beginBlock();
        const auto n = std::min<std::size_t>(256, frames - done);
        for (std::size_t i = 0; i < n; ++i) {
            std::array<float, 8> left {}, right {};
            runtime.tick(EventLog::receive, &log, left.data(), right.data());
            runtime.advance();
            ++log.frame;
        }
        runtime.endBlock(); done += n;
    }
}

void scheduler(Checks& c) {
    WorkstationState state; state.tempo = 120; state.selectedTrack = 2;
    state.patterns[0].lengthBeats = 4;
    state.patterns[0].notes = {{1, 3, .125, .25, .7f, 5}};
    WorkstationRuntime runtime; runtime.prepare(8000, 256); runtime.setState(state); runtime.setEnabled(true);
    runtime.play(WorkstationPlayMode::Pattern);
    EventLog log; ticks(runtime, log, 2000);
    c.check(log.count == 2 && log.events[0].frame == 500 && log.events[0].event.noteOn
        && log.events[1].frame == 1500 && !log.events[1].event.noteOn,
        "Pattern note on/off is scheduled at exact sample positions inside arbitrary blocks");
    c.check(log.events[0].event.track == 2 && log.events[0].event.pad == 3
        && log.events[0].event.semitones == 5 && log.events[0].event.velocity == .7f
        && log.events[0].event.token == log.events[1].event.token, "Sequencer preserves track, pitch, velocity and matching note token");
    runtime.stop(); ticks(runtime, log, 1);
    state.patterns[0].notes[0].beat = 3.875;
    runtime.setState(state); runtime.seek(0); runtime.play(WorkstationPlayMode::Pattern);
    EventLog loop; ticks(runtime, loop, 17000);
    c.check(loop.count == 2 && loop.events[0].frame == 15500 && loop.events[1].frame == 16500,
        "Note duration crossing pattern loop retains its scheduled release after loop boundary");

    runtime.stop(); ticks(runtime, log, 1);
    state.patterns[0].notes[0].beat = .125;
    state.arrangement = {{1, 5, ClipKind::Pattern, state.patterns[0].id, 1, 2, .125, false, .8f}};
    runtime.setState(state); runtime.seek(0); runtime.play(WorkstationPlayMode::Arrangement);
    EventLog arrangement; ticks(runtime, arrangement, 6000);
    c.check(arrangement.count == 2 && arrangement.events[0].frame == 4000 && arrangement.events[0].event.track == 5
        && arrangement.events[1].frame == 5000, "Arrangement clip offset launches its pattern at sample-accurate timeline position");
    runtime.stop(); ticks(runtime, arrangement, 1);
    c.check(!runtime.snapshot().playing, "Transport Stop halts sequencer state");

    WorkstationState performance; performance.tempo = 120; performance.launchQuantizeBeats = 1;
    performance.patterns[0].lengthBeats = 4; performance.patterns[0].notes = {{1, 0, 0, .25, 1, 0}};
    performance.launchSlots[3 * workstationSceneCount + 2] = {ClipKind::Pattern, performance.patterns[0].id, true};
    WorkstationRuntime clips; clips.prepare(8000, 256); clips.setState(performance); clips.setEnabled(true);
    clips.play(WorkstationPlayMode::Performance); EventLog launches; ticks(clips, launches, 1000);
    clips.launch(3, 2); ticks(clips, launches, 2000);
    c.check(clips.snapshot().pendingScene[3] == 2 && clips.snapshot().activeScene[3] == -1,
        "Quantized clip launch remains pending before target beat");
    ticks(clips, launches, 1001);
    c.check(launches.count == 1 && launches.events[0].frame == 4000 && clips.snapshot().activeScene[3] == 2,
        "Performance clip launch starts on exact next quantization beat");
    const auto stopsBefore = launches.stopCount;
    clips.stopTrack(3); ticks(clips, launches, 1);
    c.check(clips.snapshot().activeScene[3] == -1 && launches.stopCount > stopsBefore && launches.lastStoppedTrack == 3,
        "Stopping clip track releases its scheduled note and clears scene state");

    WorkstationState oneShot; oneShot.launchQuantizeBeats = 0;
    oneShot.patterns[0].lengthBeats = 4; oneShot.patterns[0].notes = {{1, 0, 3.75, .5, 1, 0}};
    oneShot.launchSlots[0] = {ClipKind::Pattern, oneShot.patterns[0].id, false};
    WorkstationRuntime shot; shot.prepare(8000, 256); shot.setState(oneShot); shot.setEnabled(true);
    shot.play(WorkstationPlayMode::Performance); shot.launch(0, 0);
    EventLog once; ticks(shot, once, 20000);
    c.check(once.count == 2 && once.events[0].frame == 15000 && once.events[0].event.noteOn
        && once.events[1].frame == 17000 && !once.events[1].event.noteOn,
        "One-shot pattern releases a note crossing its end without repeating the note");

    WorkstationState record; record.recordQuantizeBeats = 0;
    WorkstationRuntime recorder; recorder.prepare(8000, 256); recorder.setState(record); recorder.setEnabled(true);
    recorder.play(WorkstationPlayMode::Pattern); recorder.setRecording(true);
    EventLog recorded; ticks(recorder, recorded, 800);
    recorder.beginBlock(); recorder.recordNoteOn(42, 0, 2, 0, .8f); recorder.endBlock();
    ticks(recorder, recorded, 800);
    recorder.seek(3); ticks(recorder, recorded, 1);
    RecordedWorkstationNote finished;
    c.check(recorder.popRecordedNote(finished) && std::abs(finished.note.beat - .2) < 1e-8
        && std::abs(finished.note.duration - .2) < 1e-8,
        "Seeking during recording finalizes held note at previous transport position");

    WorkstationState editing; editing.patterns[0].lengthBeats = 4;
    editing.patterns[0].notes = {{1, 0, 0, 4, 1, 0}};
    WorkstationRuntime edited; edited.prepare(8000, 256); edited.setState(editing); edited.setEnabled(true);
    edited.play(WorkstationPlayMode::Pattern); EventLog deletion; ticks(edited, deletion, 1000);
    const auto beforeDeletion = deletion.stopCount;
    editing.patterns[0].notes.clear(); edited.setState(editing); ticks(edited, deletion, 1);
    c.check(deletion.count == 1 && deletion.stopCount > beforeDeletion,
        "Deleting a held piano-roll note releases stale playback when edited pattern is published");
    editing.patterns[0].notes = {{1, 0, 0, 4, 1, 0}};
    WorkstationRuntime mixed; mixed.prepare(8000, 256); mixed.setState(editing); mixed.setEnabled(true);
    mixed.play(WorkstationPlayMode::Pattern); EventLog mixerEdit; ticks(mixed, mixerEdit, 1000);
    const auto stopsBeforeGain = mixerEdit.stopCount;
    editing.tracks[0].gainDb = -6; mixed.setState(editing); ticks(mixed, mixerEdit, 1);
    c.check(mixerEdit.count == 1 && mixerEdit.stopCount == stopsBeforeGain,
        "Mixer-only publication leaves held sequencer note running without retrigger or track stop");
    WorkstationState loopRecord; loopRecord.recordQuantizeBeats = 0;
    loopRecord.arrangementLoop = true; loopRecord.loopStartBeat = 0; loopRecord.loopEndBeat = 1;
    WorkstationRuntime loopingRecorder; loopingRecorder.prepare(8000, 256); loopingRecorder.setState(loopRecord);
    loopingRecorder.setEnabled(true); loopingRecorder.play(WorkstationPlayMode::Arrangement); loopingRecorder.setRecording(true);
    EventLog loopRecording; ticks(loopingRecorder, loopRecording, 3000);
    loopingRecorder.beginBlock(); loopingRecorder.recordNoteOn(71, 0, 1, 0, .9f); loopingRecorder.endBlock();
    ticks(loopingRecorder, loopRecording, 2000);
    loopingRecorder.beginBlock(); loopingRecorder.recordNoteOff(71); loopingRecorder.endBlock();
    RecordedWorkstationNote acrossLoop;
    c.check(loopingRecorder.popRecordedNote(acrossLoop) && std::abs(acrossLoop.note.beat - .75) < 1e-8
        && std::abs(acrossLoop.note.duration - .5) < 1e-8,
        "Held-note recording measures continuous duration across arrangement loop wrap");
}

void patternAudio(Checks& c) {
    const auto source = constant();
    WorkstationState w; w.tempo = 120; w.selectedTrack = 2;
    w.patterns[0].lengthBeats = 4; w.patterns[0].notes = {{1, 0, .125, .125, 1, 0}};
    AudioEngine engine; prepare(engine, source, w);
    engine.playWorkstation(WorkstationPlayMode::Pattern);
    const auto audio = render(engine, 1800);
    c.check(peak(audio.channels[0], 0, 500) == 0 && audio.channels[0][500] > .05f,
        "Pattern scheduler launches audible sampler output at exact in-block sample");
    c.check(peak(audio.channels[0], 500, 1000) > .1f && peak(audio.channels[0], 1000, 1800) < .000001f,
        "Pattern note-off controls real sampler gate at exact duration");
    engine.stopWorkstation(); settle(engine);
    c.check(!engine.workstationSnapshot().playing && peak(render(engine, 256).channels[0], 0, 256) == 0,
        "Workstation Stop silences scheduled sampler voices");

    w.selectedTrack = 0; w.launchQuantizeBeats = 1;
    w.patterns[0].notes[0].beat = 0;
    w.launchSlots[0] = {ClipKind::Pattern, w.patterns[0].id, true};
    engine.setWorkstationState(w); engine.seekWorkstation(0); engine.playWorkstation(WorkstationPlayMode::Performance);
    (void)render(engine, 1000);
    engine.launchClip(0, 0);
    const auto launch = render(engine, 3500);
    c.check(peak(launch.channels[0], 0, 3000) == 0 && launch.channels[0][3000] > .05f,
        "Clip quantization controls actual audio onset independently of callback block size");
    engine.stopClipTrack(0); settle(engine);
    c.check(engine.workstationSnapshot().activeScene[0] == -1 && peak(render(engine, 256).channels[0], 0, 256) == 0,
        "Stop Track silences its launched sampler content");
}

void mixer(Checks& c) {
    const auto source = constant(.2f);
    WorkstationState w; w.tempo = 120;
    w.patterns[0].lengthBeats = 4; w.patterns[0].notes = {{1, 0, 0, 4, 1, 0}};
    w.arrangement = {{1, 0, ClipKind::Pattern, 1, 0, 4, 0, false, 1}, {2, 1, ClipKind::Pattern, 1, 0, 4, 0, false, 1}};
    AudioEngine engine; prepare(engine, source, w); engine.playWorkstation(WorkstationPlayMode::Arrangement);
    settle(engine); const auto both = render(engine, 256);
    c.check(std::abs(both.channels[0].back() - .4f) < .001f, "Independent tracks sum through workstation master bus");
    w.tracks[0].mute = true; engine.setWorkstationState(w); settle(engine);
    c.check(std::abs(render(engine, 256).channels[0].back() - .2f) < .001f, "Live track mute removes only its own sounding track");
    w.tracks[0].mute = false; w.tracks[0].solo = true; engine.setWorkstationState(w); settle(engine);
    c.check(std::abs(render(engine, 256).channels[0].back() - .2f) < .001f, "Track Solo excludes other tracks from audible master mix");
    w.tracks[0].pan = -1; engine.setWorkstationState(w); settle(engine); const auto left = render(engine, 256);
    c.check(rms(left.channels[0]) > .1 && rms(left.channels[1]) < .00001, "Hard-left track pan routes actual stereo output");
    w.tracks[0].pan = 0; w.tracks[0].gainDb = -6.0206f; engine.setWorkstationState(w); settle(engine);
    c.check(std::abs(render(engine, 256).channels[0].back() - .1f) < .001f, "Track dB gain changes sounding audio without retriggering notes");
    w.masterGainDb = -6.0206f; engine.setWorkstationState(w); settle(engine);
    c.check(std::abs(render(engine, 256).channels[0].back() - .05f) < .001f, "Master dB gain applies after track gain");
    w.masterGainDb = 0; w.tracks[0].gainDb = 0; w.tracks[0].solo = false;
    w.tracks[0].pan = -1; w.tracks[1].pan = 1;
    w.tracks[0].crossfadeAssignment = CrossfadeAssignment::A; w.tracks[1].crossfadeAssignment = CrossfadeAssignment::B;
    w.crossfader = 0; engine.setWorkstationState(w); settle(engine); const auto crossA = render(engine, 256);
    w.crossfader = 1; engine.setWorkstationState(w); settle(engine); const auto crossB = render(engine, 256);
    c.check(rms(crossA.channels[0]) > .1 && rms(crossA.channels[1]) < .00001
        && rms(crossB.channels[1]) > .1 && rms(crossB.channels[0]) < .00001,
        "Crossfader endpoints select assigned A/B tracks with correct stereo routing");
    engine.stopWorkstation(); settle(engine);
}

void effectRouting(Checks& c) {
    auto source = constant(0, 384000, 48000);
    for (auto& channel : source.channels)
        for (std::size_t i = 0; i < channel.size(); ++i) channel[i] = .2f * static_cast<float>(std::sin(2 * pi * 8000 * i / source.sampleRate));
    WorkstationState w; w.patterns[0].lengthBeats = 4; w.patterns[0].notes = {{1, 0, 0, 4, 1, 0}};
    AudioEngine engine; prepare(engine, source, w); engine.playWorkstation(WorkstationPlayMode::Pattern);
    settle(engine); const auto dry = render(engine, 4096, 48000);
    auto filter = defaultFxSlot(FxType::Filter); filter.mix = 1;
    filter.parameters[0] = fxParameterNormalized(FxType::Filter, 0, 1000); filter.parameters[2] = 0;
    w.tracks[0].effects[0] = filter; engine.setWorkstationState(w); settle(engine);
    const auto filtered = render(engine, 4096, 48000);
    c.check(rms(filtered.channels[0]) < rms(dry.channels[0]) * .3, "Track FX rack processes the sounding sampler path");
    w.tracks[0].effects[0].bypass = true; engine.setWorkstationState(w); settle(engine);
    const auto bypass = render(engine, 4096, 48000);
    c.check(std::abs(rms(bypass.channels[0]) / rms(dry.channels[0]) - 1) < .03,
        "Bypassing track FX restores dry sampler signal");
    engine.stopWorkstation(); settle(engine);

    auto impulse = constant(0); impulse.channels[0][0] = impulse.channels[1][0] = .5f;
    WorkstationState send; send.patterns[0].lengthBeats = 4; send.patterns[0].notes = {{1, 0, 0, .5, 1, 0}};
    auto delay = defaultFxSlot(FxType::Delay); delay.mix = 1;
    delay.parameters[0] = fxParameterNormalized(FxType::Delay, 0, 100);
    delay.parameters[1] = delay.parameters[2] = delay.parameters[3] = delay.parameters[5] = 0;
    send.sendAEffects[0] = delay;
    AudioEngine sendEngine; prepare(sendEngine, impulse, send); sendEngine.playWorkstation(WorkstationPlayMode::Pattern);
    const auto noSend = render(sendEngine, 1600);
    sendEngine.stopWorkstation(); settle(sendEngine);
    send.tracks[0].sendA = 1; sendEngine.setWorkstationState(send); sendEngine.seekWorkstation(0);
    sendEngine.playWorkstation(WorkstationPlayMode::Pattern); const auto withSend = render(sendEngine, 1600);
    c.check(peak(noSend.channels[0], 750, 850) < .000001f && peak(withSend.channels[0], 780, 850) > .1f,
        "Track send routes impulse through delayed return into master bus");
    sendEngine.stopWorkstation(); settle(sendEngine);
}

void recordingAndFreeze(Checks& c) {
    auto source = constant(.2f);
    WorkstationState w; w.selectedPattern = 7; w.selectedTrack = 4; w.recordQuantizeBeats = .25;
    AudioEngine engine; prepare(engine, source, w);
    engine.seekWorkstation(1); engine.playWorkstation(WorkstationPlayMode::Pattern); engine.setPatternRecording(true);
    engine.triggerPad(2, 7, .6f); (void)render(engine, 1000); engine.releasePad(2); settle(engine);
    RecordedWorkstationNote note;
    c.check(engine.popRecordedNote(note) && note.patternIndex == 7 && note.track == 4 && note.note.pad == 2
        && note.note.semitones == 7 && note.note.velocity == .6f && std::abs(note.note.beat - 1) < 1e-9
        && std::abs(note.note.duration - .25) < 1e-9, "Pattern recording captures applied note, track, velocity, pitch and quantized duration");
    engine.triggerPad(3, -5, .4f); (void)render(engine, 1000); engine.setPatternRecording(false); settle(engine);
    c.check(engine.popRecordedNote(note) && note.note.pad == 3 && note.note.duration > 0,
        "Ending recording completes a still-held input note");
    c.check(!engine.popRecordedNote(note), "Recorded-note queue does not duplicate already completed input notes");
    engine.stop(); engine.stopWorkstation(); settle(engine);

    auto envelopeProject = projectFor(source);
    envelopeProject.pads[0].attackMs = 500; envelopeProject.pads[0].releaseMs = 100;
    w.selectedPattern = 0; w.selectedTrack = 0;
    w.patterns[0].lengthBeats = 4; w.patterns[0].notes = {{1, 0, .25, .125, 1, 0}};
    engine.preparePads(envelopeProject); engine.setWorkstationState(w);
    const auto releaseDuringAttack = engine.bouncePattern(0, 0);
    c.check(releaseDuringAttack.valid() && releaseDuringAttack.frames() >= 2600
        && peak(releaseDuringAttack.channels[0], 1000, 2300) > .015f
        && peak(releaseDuringAttack.channels[0], 1000, 2300) < .04f
        && std::abs(releaseDuringAttack.channels[0][1500] - releaseDuringAttack.channels[0][1499]) < .001f
        && peak(releaseDuringAttack.channels[0], 2301, 2600) < .000001f,
        "Freeze note-off during attack releases from current envelope level without jumping to full gain");
    auto monoProject = projectFor(source); monoProject.mono = true; monoProject.pads[1].levelDb = -6.0206f;
    w.patterns[0].notes = {{1, 0, 0, 2, 1, 0}, {2, 1, .5, .5, 1, 0}};
    engine.preparePads(monoProject); engine.setWorkstationState(w);
    const auto monoFreeze = engine.bouncePattern(0, 0);
    c.check(monoFreeze.valid() && monoFreeze.frames() >= 6500 && std::abs(monoFreeze.channels[0][2500] - .1f) < .002f
        && peak(monoFreeze.channels[0], 4500, 6500) < .000001f,
        "Freeze honors mono cue choke and does not resume an older overlapping note");
    engine.preparePads(projectFor(source));

    w.selectedPattern = 0; w.selectedTrack = 0;
    w.patterns[0].lengthBeats = 4; w.patterns[0].notes = {{1, 0, .25, .5, 1, 0}};
    engine.setWorkstationState(w);
    const auto bounce = engine.bouncePattern(0, 0);
    c.check(bounce.valid() && std::abs(static_cast<double>(bounce.frames()) - 16000) <= 1
        && peak(bounce.channels[0], 0, 1000) < .000001f && peak(bounce.channels[0], 1000, 3000) > .05f,
        "Pattern freeze renders real sampler audio with pattern timeline and full pattern length");
    w.audioAssets = {{91, "Frozen phrase", "/tmp/samplify-frozen-fixture.wav", 120, 4}};
    w.arrangement = {{1, 0, ClipKind::Audio, 91, 0, 4, 0, false, 1}};
    engine.setAudioAsset(91, std::make_shared<AudioData>(bounce)); engine.setWorkstationState(w);
    engine.setSource(std::make_shared<AudioData>(constant(.9f))); engine.preparePads(projectFor(source));
    engine.seekWorkstation(0); engine.playWorkstation(WorkstationPlayMode::Arrangement);
    const auto frozen = render(engine, 3500);
    c.check(peak(frozen.channels[0], 0, 1000) < .000001f && peak(frozen.channels[0], 1000, 3000) > .05f
        && peak(frozen.channels[0], 1000, 3000) < .4f,
        "Frozen clip retains its independent PCM after original sampler source changes");
    engine.stopWorkstation(); settle(engine);
    engine.removeAudioAsset(91); engine.seekWorkstation(0); engine.playWorkstation(WorkstationPlayMode::Arrangement);
    c.check(peak(render(engine, 3500).channels[0], 0, 3500) < .000001f, "Removed audio asset cannot keep playing a stale retained clip");
    engine.stopWorkstation(); settle(engine);
}

struct TemporaryDirectory {
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("samplify-workstation-audio", {}, false);
    TemporaryDirectory() { if (path.createDirectory().failed()) throw std::runtime_error("Cannot create capture test directory"); }
    ~TemporaryDirectory() { path.deleteRecursively(); }
};
void capture(Checks& c) {
    TemporaryDirectory directory; const auto source = constant(.25f);
    WorkstationState w; AudioEngine engine; prepare(engine, source, w);
    engine.triggerPad(0); settle(engine);
    const auto file = directory.path.getChildFile("captured-master.wav");
    engine.startMasterCapture(file);
    const auto expected = render(engine, 2560);
    engine.stopMasterCapture(); settle(engine);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (engine.captureSnapshot().finalizing && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto state = engine.captureSnapshot();
    c.check(!state.recording && !state.finalizing && !state.overflow && state.error == CaptureError::None,
        "Master capture finalizes asynchronous disk writer without losing frames");
    const auto recorded = AudioEngine::readAudio(file);
    bool sameAudio = recorded.valid() && recorded.frames() == expected.frames() && recorded.channels.size() == 2;
    for (std::size_t ch = 0; sameAudio && ch < 2; ++ch)
        for (std::size_t i = 0; i < recorded.frames(); ++i) sameAudio &= std::abs(recorded.channels[ch][i] - expected.channels[ch][i]) < .000002f;
    c.check(sameAudio, "Master capture contains the exact audio heard during recording with original stereo sample count");
    engine.stop(); settle(engine);
}

void callbackHeap(Checks& c) {
    TemporaryDirectory directory;
    const auto source = constant(.1f);
    WorkstationState w; w.patterns[0].lengthBeats = 4;
    w.patterns[0].notes = {{1, 0, 0, .25, 1, 0}, {2, 1, .125, .25, .7f, 7}};
    w.tracks[0].sendA = .3f; w.sendAEffects[0] = defaultFxSlot(FxType::Delay);
    w.tracks[0].effects[0] = defaultFxSlot(FxType::Filter);
    w.masterEffects[0] = defaultFxSlot(FxType::Limiter);
    AudioEngine engine; prepare(engine, source, w);
    juce::AudioBuffer<float> output(2, 256);
    const auto probe = [&](int blocks) {
        heapProbe::reset();
        struct Scope { Scope() { heapProbe::enabled = true; } ~Scope() { heapProbe::enabled = false; } };
        { const Scope scope; for (int i = 0; i < blocks; ++i) block(engine, output); }
        std::cout << "Workstation callback heap: C++ " << heapProbe::cppNew << '/' << heapProbe::cppDelete
                  << ", linked C " << heapProbe::cAllocate << '/' << heapProbe::cFree << '\n';
        return heapProbe::cppNew == 0 && heapProbe::cppDelete == 0 && heapProbe::cAllocate == 0 && heapProbe::cFree == 0;
    };
    engine.playWorkstation(WorkstationPlayMode::Pattern);
    c.check(probe(16), "Sequencing, pitched pads, track FX, sends and master FX perform no intercepted callback heap calls");
    w.tracks[0].pan = .6f; w.tracks[0].gainDb = -4; w.tracks[0].sendA = .6f;
    engine.setWorkstationState(w);
    c.check(probe(8), "Publishing live mixer state performs no intercepted callback allocation or deallocation");
    engine.startMasterCapture(directory.path.getChildFile("heap-guard-capture.wav"));
    engine.setPatternRecording(true); engine.triggerPad(2);
    c.check(probe(8), "Note recording and master capture enqueue actual audio without intercepted callback heap calls");
    engine.releasePad(2); engine.setPatternRecording(false);
    c.check(probe(4), "Completing recorded notes performs no intercepted callback heap calls");
    engine.stopMasterCapture();
    engine.stopWorkstation();
    c.check(probe(4), "Stopping sequenced playback performs no intercepted callback heap calls");
}
} // namespace

int main(int argc, char** argv) {
    const std::string only = argc == 3 && std::string(argv[1]) == "--only" ? argv[2] : "";
    if (argc != 1 && only.empty()) { std::cerr << "Usage: workstation_audio_tests [--only scheduler|audio|mixer|fx|record|capture|heap]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI juceInitialiser; Checks checks; bool selected = false;
    const auto run = [&](const char* id, const char* title, auto&& fn) { if (!only.empty() && only != id) return; selected = true; checks.section(title, fn); };
    run("scheduler", "Sample-accurate workstation scheduler", [&] { scheduler(checks); });
    run("audio", "Scheduled sampler and clip-launch audio", [&] { patternAudio(checks); });
    run("mixer", "Eight-track mixer routing", [&] { mixer(checks); });
    run("fx", "Track and send FX integration", [&] { effectRouting(checks); });
    run("record", "Note recording and frozen assets", [&] { recordingAndFreeze(checks); });
    run("capture", "Asynchronous master audio capture", [&] { capture(checks); });
    run("heap", "Workstation callback heap guards", [&] { callbackHeap(checks); });
    if (!selected) { std::cerr << "Unknown section " << only << '\n'; return 2; }
    std::cout << checks.count << " checks, " << checks.failures << " failures\n";
    return checks.failures ? 1 : 0;
}
