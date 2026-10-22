#include "dsp/Effects.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
std::atomic<bool> trackAllocations { false };
std::atomic<std::size_t> allocations { 0 }, deallocations { 0 };
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void releaseMemory(void* memory) noexcept { std::free(memory); }
}

void* operator new(std::size_t size) {
    if (auto* memory = std::malloc(std::max<std::size_t>(1, size))) {
        if (trackAllocations.load(std::memory_order_relaxed)) ++allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept {
    if (memory && trackAllocations.load(std::memory_order_relaxed)) ++deallocations;
    releaseMemory(memory);
}
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

using namespace samplify;
namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0, failures = 0;
void check(bool success, const std::string& message) {
    ++checks;
    if (!success) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void set(FxSlot& slot, std::size_t index, float actual) {
    slot.parameters[index] = fxParameterNormalized(slot.type, index, actual);
}
std::array<FxSlot, 4> single(FxSlot slot) { return { slot, {}, {}, {} }; }
struct Stereo {
    std::vector<float> left, right;
    explicit Stereo(std::size_t samples) : left(samples), right(samples) {}
    void process(FxRack& rack, std::size_t block = 257) {
        for (std::size_t start = 0; start < left.size(); start += block)
            rack.processStereo(left.data() + start, right.data() + start, std::min(block, left.size() - start));
    }
};
Stereo sine(double hz, double seconds = .5, double rate = 48000, double amplitude = .4) {
    Stereo result(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < result.left.size(); ++i)
        result.left[i] = result.right[i] = static_cast<float>(amplitude * std::sin(2 * pi * hz * i / rate));
    return result;
}
double rms(const std::vector<float>& values, std::size_t begin = 0, std::size_t end = 0) {
    if (!end) end = values.size();
    if (end <= begin) return 0;
    double energy = 0;
    for (auto i = begin; i < end; ++i) energy += static_cast<double>(values[i]) * values[i];
    return std::sqrt(energy / (end - begin));
}
double maximum(const std::vector<float>& values) {
    double peak = 0;
    for (float value : values) peak = std::max(peak, static_cast<double>(std::abs(value)));
    return peak;
}
double difference(const std::vector<float>& a, const std::vector<float>& b) {
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += std::abs(a[i] - b[i]);
    return sum / a.size();
}
bool finite(const Stereo& audio) {
    auto bounded = [](float x) { return std::isfinite(x) && std::abs(x) <= 32; };
    return std::all_of(audio.left.begin(), audio.left.end(), bounded)
        && std::all_of(audio.right.begin(), audio.right.end(), bounded);
}
double gainAt(FxSlot slot, double hz, double rate = 48000) {
    FxRack rack; rack.prepare(rate, 257); rack.setSlots(single(slot));
    auto audio = sine(hz, .5, rate);
    const auto start = audio.left.size() / 2;
    const auto input = rms(audio.left, start);
    audio.process(rack);
    return rms(audio.left, start) / input;
}

void metadataAndPresets() {
    const auto definitions = fxDefinitions();
    check(definitions.size() == 16, "registry contains Off and fifteen effects");
    bool valid = true;
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const auto& definition = definitions[i];
        valid &= static_cast<std::size_t>(definition.type) == i && definition.name[0] != 0
            && definition.parameterCount >= 0 && definition.parameterCount <= 6;
        const auto slot = defaultFxSlot(definition.type);
        for (int parameter = 0; parameter < definition.parameterCount; ++parameter) {
            const auto index = static_cast<std::size_t>(parameter);
            const auto& p = definition.parameters[index];
            valid &= p.name[0] && p.minimum < p.maximum && p.defaultValue >= p.minimum && p.defaultValue <= p.maximum;
            valid &= std::abs(fxParameterValue(definition.type, index, slot.parameters[index]) - p.defaultValue)
                < std::max(.001f, std::abs(p.defaultValue) * .00001f);
            for (float n : { 0.f, .2f, .5f, .9f, 1.f })
                valid &= std::abs(fxParameterNormalized(definition.type, index,
                    fxParameterValue(definition.type, index, n)) - n) < .00001f;
        }
    }
    check(valid, "all parameter metadata, defaults, physical units and normalized round trips are valid");
    check(fxDefinition(static_cast<FxType>(999)).type == FxType::None, "unknown effect type safely resolves to Off");
    const auto presets = fxPresets();
    std::array<int, 3> categories {};
    bool usable = presets.size() >= 20;
    for (const auto& preset : presets) {
        ++categories[static_cast<std::size_t>(preset.category)];
        usable &= preset.name[0] != 0 && preset.slots.front().type != FxType::None;
        for (const auto& slot : preset.slots) {
            usable &= slot.mix >= 0 && slot.mix <= 1 && std::isfinite(slot.mix);
            for (float p : slot.parameters) usable &= std::isfinite(p) && p >= 0 && p <= 1;
        }
    }
    check(usable && std::all_of(categories.begin(), categories.end(), [](int n) { return n >= 6; }),
        "26 musical presets contain valid populated chains in all three categories");
}

void unityAndControl() {
    FxRack rack;
    auto input = sine(1234);
    auto dry = input;
    dry.process(rack);
    check(dry.left == input.left && dry.right == input.right, "unprepared rack passes finite audio unchanged");
    rack.prepare(48000, 64);
    dry = input; dry.process(rack);
    check(dry.left == input.left && dry.right == input.right, "four Off slots are exact unity");
    auto slot = defaultFxSlot(FxType::Saturator); slot.bypass = true;
    rack.setSlots(single(slot)); dry = input; dry.process(rack);
    check(dry.left == input.left && dry.right == input.right, "effect bypass is exact stereo unity");
    slot.bypass = false; slot.mix = 0;
    rack.setSlots(single(slot)); dry = input; dry.process(rack);
    check(dry.left == input.left && dry.right == input.right, "zero wet amount is exact stereo unity");
    rack.processStereo(nullptr, nullptr, 400);
    rack.processStereo(dry.left.data(), dry.right.data(), 0);
    auto width = defaultFxSlot(FxType::StereoWidth); set(width, 0, 100);
    rack.setSlots(single(width));
    Stereo dc(12000); std::fill(dc.left.begin(), dc.left.end(), .5f); std::fill(dc.right.begin(), dc.right.end(), .25f);
    dc.process(rack);
    set(width, 1, 100); rack.setSlots(single(width)); dc.process(rack);
    float largestJump = 0;
    for (std::size_t i = 1; i < dc.left.size(); ++i) largestJump = std::max(largestJump, std::abs(dc.left[i] - dc.left[i - 1]));
    check(largestJump < .002 && dc.left.back() < .0001f, "parameter changes smooth over time and reach their targets");
}

void spectralAndDynamics() {
    auto eq = defaultFxSlot(FxType::Equalizer);
    check(std::abs(gainAt(eq, 1000) - 1) < .00001, "flat EQ is unity");
    set(eq, 0, -12);
    check(gainAt(eq, 35) < .3 && gainAt(eq, 2000) > .95, "low shelf attenuates bass while preserving midrange");
    eq = defaultFxSlot(FxType::Equalizer); set(eq, 2, -12); set(eq, 5, 3000);
    check(gainAt(eq, 12000) < .3 && gainAt(eq, 100) > .95, "high shelf attenuates treble while preserving bass");
    eq = defaultFxSlot(FxType::Equalizer); set(eq, 1, 9);
    check(gainAt(eq, 1000) > 2.7 && gainAt(eq, 100) < 1.1, "mid peak boosts the chosen band");
    auto filter = defaultFxSlot(FxType::Filter); set(filter, 0, 1000);
    check(gainAt(filter, 8000) < .02 && gainAt(filter, 100) > .99, "low-pass filter rejects high frequencies");
    set(filter, 2, 2);
    check(gainAt(filter, 100) < .02 && gainAt(filter, 8000) > .99, "high-pass mode rejects low frequencies");
    set(filter, 2, 1);
    check(gainAt(filter, 1000) > .99 && gainAt(filter, 100) < .15, "band-pass mode passes its centre frequency");

    auto compressor = defaultFxSlot(FxType::Compressor);
    set(compressor, 0, -24); set(compressor, 1, 4); set(compressor, 2, .1f);
    set(compressor, 4, 0); set(compressor, 5, 0);
    FxRack rack; rack.prepare(48000, 256); rack.setSlots(single(compressor));
    Stereo dc(12000); std::fill(dc.left.begin(), dc.left.end(), .5f); std::fill(dc.right.begin(), dc.right.end(), .25f);
    dc.process(rack);
    const auto expected = std::pow(10., (-24 + (20 * std::log10(.5) + 24) / 4) / 20);
    check(std::abs(dc.left.back() - expected) < .0001, "compressor steady gain matches threshold and ratio");
    check(std::abs(dc.right.back() * 2 - dc.left.back()) < .000001, "compressor stereo linking preserves channel ratios");
    compressor = defaultFxSlot(FxType::Compressor); set(compressor, 0, -48); set(compressor, 1, 1); set(compressor, 4, 0);
    check(std::abs(gainAt(compressor, 1000) - 1) < .00001, "compressor ratio one and no makeup are unity");

    auto saturator = defaultFxSlot(FxType::Saturator); set(saturator, 0, 18); set(saturator, 1, 18000); set(saturator, 3, 0);
    rack.setSlots(single(saturator)); auto audio = sine(500, 1); audio.process(rack);
    double harmonic = 0, fundamental = 0;
    for (std::size_t i = 24000; i < audio.left.size(); ++i) {
        harmonic += audio.left[i] * std::sin(2 * pi * 1500 * i / 48000);
        fundamental += audio.left[i] * std::sin(2 * pi * 500 * i / 48000);
    }
    check(std::abs(harmonic / fundamental) > .1, "saturator generates measurable odd harmonic colour");
    auto crusher = defaultFxSlot(FxType::BitCrusher); set(crusher, 0, 4); set(crusher, 1, 8000);
    rack.setSlots(single(crusher)); audio = sine(731, .1); audio.process(rack);
    int repeated = 0; bool quantized = true;
    for (std::size_t i = 1; i < audio.left.size(); ++i) {
        repeated += audio.left[i] == audio.left[i - 1];
        quantized &= std::abs(audio.left[i] * 8 - std::round(audio.left[i] * 8)) < .00001;
    }
    check(quantized && repeated > static_cast<int>(audio.left.size() * .75), "bit crusher quantizes amplitude and holds reduced-rate samples");
    auto transient = defaultFxSlot(FxType::Transient); set(transient, 0, 100);
    rack.setSlots(single(transient)); Stereo impulse(64); impulse.left[0] = impulse.right[0] = .2f; impulse.process(rack);
    check(impulse.left.front() > .4f && finite(impulse), "transient shaper increases attack of a measured impulse");
}

void delayAndReverb() {
    for (double rate : { 8000., 44100., 48000., 96000. }) {
        FxRack rack; rack.prepare(rate, 256);
        auto delay = defaultFxSlot(FxType::Delay); delay.mix = 1;
        set(delay, 0, 100); set(delay, 1, 0); set(delay, 2, 0); set(delay, 4, 20);
        rack.setSlots(single(delay));
        Stereo impulse(static_cast<std::size_t>(rate * .25)); impulse.left[0] = 1; impulse.process(rack);
        const auto found = std::find_if(impulse.left.begin(), impulse.left.end(), [](float x) { return std::abs(x) > .01f; });
        const auto frame = static_cast<std::size_t>(found - impulse.left.begin());
        check(std::abs(static_cast<double>(frame) - rate * .1) <= 1 && maximum(impulse.right) == 0,
            "delay produces the specified milliseconds and preserves stereo isolation at " + std::to_string(static_cast<int>(rate)) + " Hz");
        rack.reset(); Stereo silence(impulse.left.size()); silence.process(rack);
        check(maximum(silence.left) == 0, "logical delay reset removes old buffer history at " + std::to_string(static_cast<int>(rate)) + " Hz");
    }
    FxRack rack; rack.prepare(48000, 256);
    auto delay = defaultFxSlot(FxType::Delay); delay.mix = 1;
    set(delay, 0, 50); set(delay, 1, 50); set(delay, 2, 0); set(delay, 3, 100); set(delay, 4, 20);
    rack.setSlots(single(delay)); Stereo ping(9000); ping.left[0] = 1; ping.process(rack);
    check(maximum(ping.right) > .35 && maximum(ping.left) > .9, "ping-pong feedback crosses the stereo channels");
    delay.bypass = true; rack.setSlots(single(delay));
    delay.bypass = false; rack.setSlots(single(delay));
    Stereo silence(9000); silence.process(rack);
    check(maximum(silence.left) == 0 && maximum(silence.right) == 0, "bypass transitions cannot revive stale delay tails");
    auto reverb = defaultFxSlot(FxType::Reverb); reverb.mix = 1;
    set(reverb, 0, .45f); set(reverb, 2, 10); set(reverb, 5, 20);
    rack.setSlots(single(reverb)); Stereo room(96000); room.left[0] = room.right[0] = 1; room.process(rack);
    check(maximum(room.left) > .03 && rms(room.left, 4800, 14400) > .0001, "reverb creates a sustained diffuse impulse tail");
    check(rms(room.left, 4800, 14400) > rms(room.left, 48000, 72000) * 100, "short room decay loses energy over time");
    check(difference(room.left, room.right) > .00001, "reverb decorrelates stereo reflections");
    rack.reset(); Stereo clean(12000); clean.process(rack);
    check(maximum(clean.left) == 0 && maximum(clean.right) == 0, "reverb reset removes all comb and allpass history");
}

void modulationWidthAndLimiting() {
    FxRack rack; rack.prepare(48000, 257);
    auto tremolo = defaultFxSlot(FxType::Tremolo); set(tremolo, 0, 2); set(tremolo, 1, 100); set(tremolo, 3, 180);
    rack.setSlots(single(tremolo)); Stereo dc(48000);
    std::fill(dc.left.begin(), dc.left.end(), 1); std::fill(dc.right.begin(), dc.right.end(), 1); dc.process(rack);
    bool complementary = true;
    for (std::size_t i = 0; i < dc.left.size(); ++i) complementary &= std::abs(dc.left[i] + dc.right[i] - 1) < .00001;
    check(complementary && *std::min_element(dc.left.begin(), dc.left.end()) < .00001f && maximum(dc.left) > .9999,
        "tremolo covers full depth and stereo phase produces complementary modulation");
    auto width = defaultFxSlot(FxType::StereoWidth); set(width, 0, 0);
    rack.setSlots(single(width)); auto stereo = sine(500);
    for (std::size_t i = 0; i < stereo.right.size(); ++i) stereo.right[i] = .2f * std::sin(static_cast<float>(2 * pi * 700 * i / 48000));
    stereo.process(rack);
    check(stereo.left == stereo.right, "zero stereo width folds output to mono");
    set(width, 0, 200); rack.setSlots(single(width)); rack.reset();
    Stereo side(12000); std::fill(side.left.begin(), side.left.end(), .1f); std::fill(side.right.begin(), side.right.end(), -.1f);
    side.process(rack);
    check(std::abs(side.left.back() - .2) < .002 && std::abs(side.right.back() + .2) < .002, "double width doubles the side signal");

    for (FxType type : { FxType::Chorus, FxType::Flanger, FxType::Phaser, FxType::Tape }) {
        rack.setSlots(single(defaultFxSlot(type)));
        auto audio = sine(430, 1); const auto dry = audio;
        audio.process(rack);
        check(finite(audio) && difference(audio.left, dry.left) > .01,
            std::string(fxDefinition(type).name) + " audibly processes a real signal");
        if (type != FxType::Tape)
            check(difference(audio.left, audio.right) > .001, std::string(fxDefinition(type).name) + " generates stereo modulation");
    }
    auto tape = defaultFxSlot(FxType::Tape); set(tape, 3, -40);
    rack.setSlots(single(tape)); rack.reset(); Stereo hiss(12000); hiss.process(rack);
    check(rms(hiss.left, 6000) > .001, "tape hiss setting produces actual low-level noise");

    auto limiter = defaultFxSlot(FxType::Limiter); set(limiter, 0, -1); set(limiter, 2, 6);
    rack.setSlots(single(limiter)); auto loud = sine(451, 1, 48000, 5);
    for (std::size_t i = 0; i < loud.right.size(); ++i) loud.right[i] = loud.left[i] * .5f;
    loud.left[10] = std::numeric_limits<float>::infinity(); loud.right[20] = std::numeric_limits<float>::quiet_NaN();
    loud.process(rack);
    const auto ceiling = std::pow(10., -.05);
    check(finite(loud) && maximum(loud.left) <= ceiling + .000001 && maximum(loud.left) > ceiling - .001,
        "limiter enforces the configured peak ceiling and handles nonfinite input");
    bool linked = true;
    for (std::size_t i = 100; i < loud.left.size(); ++i) linked &= std::abs(loud.left[i] - loud.right[i] * 2) < .00001;
    check(linked, "limiter stereo gain linking preserves the stereo image");
    Stereo recovery(48000); recovery.process(rack);
    Stereo quiet(400); std::fill(quiet.left.begin(), quiet.left.end(), .1f); std::fill(quiet.right.begin(), quiet.right.end(), .1f);
    quiet.process(rack);
    check(quiet.left.back() > .199f, "limiter releases its attenuation after a loud passage");
}

void allPresetsAndRealtimeSafety() {
    FxRack rack; rack.prepare(48000, 64);
    bool presetsSound = true, masteringSafe = true;
    for (const auto& preset : fxPresets()) {
        rack.setSlots(preset.slots); rack.reset();
        auto audio = sine(440, .3, 48000, 1.4);
        for (std::size_t i = 0; i < audio.left.size(); ++i) {
            audio.left[i] += .15f * std::sin(static_cast<float>(2 * pi * 3700 * i / 48000));
            audio.right[i] *= .7f;
        }
        const auto original = audio.left;
        audio.process(rack);
        presetsSound &= finite(audio) && rms(audio.left, audio.left.size() / 2) > .00001
            && difference(audio.left, original) > .0001;
        if (preset.category == FxPresetCategory::Mastering)
            masteringSafe &= maximum(audio.left) <= 1.000001 && maximum(audio.right) <= 1.000001;
    }
    check(presetsSound, "every musical preset produces finite non-silent processed audio");
    check(masteringSafe, "every mastering preset applies a final peak ceiling");
    bool extremes = true;
    for (double rate : { 8000., 192000. }) {
        rack.prepare(rate, 64);
        for (const auto& definition : fxDefinitions()) for (float edge : { 0.f, 1.f }) {
            auto slot = defaultFxSlot(definition.type); slot.mix = 1; slot.parameters.fill(edge);
            rack.setSlots(single(slot)); rack.reset();
            auto audio = sine(1733, .1, rate, 2);
            audio.left[0] = std::numeric_limits<float>::quiet_NaN(); audio.right[1] = std::numeric_limits<float>::infinity();
            audio.process(rack);
            extremes &= finite(audio);
        }
    }
    check(extremes, "all effects remain finite at parameter extremes and 8/192 kHz sample rates");
    rack.prepare(48000, 64);
    const auto presets = fxPresets();
    std::array<float, 128> left {}, right {};
    for (std::size_t i = 0; i < left.size(); ++i) left[i] = right[i] = .2f * std::sin(static_cast<float>(i));
    allocations = 0; deallocations = 0; trackAllocations = true;
    for (const auto& definition : fxDefinitions()) {
        auto slot = defaultFxSlot(definition.type);
        rack.setSlots(single(slot)); rack.processStereo(left.data(), right.data(), left.size());
        slot.parameters.fill(1); rack.setSlots(single(slot)); rack.processStereo(left.data(), right.data(), left.size());
        slot.bypass = true; rack.setSlots(single(slot)); rack.reset();
    }
    for (const auto& preset : presets) {
        rack.setSlots(preset.slots); rack.processStereo(left.data(), right.data(), left.size()); rack.reset();
    }
    trackAllocations = false;
    check(allocations == 0 && deallocations == 0, "setSlots, processing, bypass and reset allocate/free no memory across all effects and presets");
    std::cout << "Realtime allocation audit: " << allocations << " new, " << deallocations << " delete\n";
}
} // namespace

int main() {
    metadataAndPresets(); unityAndControl(); spectralAndDynamics(); delayAndReverb();
    modulationWidthAndLimiting(); allPresetsAndRealtimeSafety();
    std::cout << "Effects: " << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
