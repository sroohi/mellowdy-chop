#include "core/CueEditing.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>

using namespace samplify;

namespace {
int checks = 0;
int failures = 0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << description << '\n'; }
}

AudioData fixture(std::size_t frames = 400000) {
    AudioData result;
    result.sampleRate = 1000;
    result.channels = { std::vector<float>(frames, 0.1f) };
    return result;
}

bool same(const Pad& a, const Pad& b) {
    return a.start == b.start && a.end == b.end && a.levelDb == b.levelDb
        && a.pitchSemitones == b.pitchSemitones && a.detuneCents == b.detuneCents
        && a.speedRatio == b.speedRatio && a.filter == b.filter && a.attackMs == b.attackMs
        && a.releaseMs == b.releaseMs && a.reverse == b.reverse && a.hasEnd == b.hasEnd
        && a.favorite == b.favorite && a.name == b.name && a.colour == b.colour
        && a.outputChannel == b.outputChannel && a.stemMute == b.stemMute;
}

Pad markedCue() {
    Pad pad;
    pad.start = 123; pad.end = 999; pad.levelDb = -8;
    pad.pitchSemitones = 3; pad.detuneCents = 17; pad.speedRatio = 1.5f;
    pad.filter = .75f; pad.attackMs = 20; pad.releaseMs = 450;
    pad.reverse = true; pad.hasEnd = true; pad.favorite = true;
    pad.name = "Keep this cue"; pad.colour = 0xff23c1d2; pad.outputChannel = 6;
    pad.stemMute = {true, false, true, false};
    return pad;
}

void manualAndGrid() {
    Project p;
    const auto audio = fixture();
    assignCueAt(p, 7, 1711, audio.sampleRate, audio.frames());
    check(p.pads[7].start == 1711 && p.pads[7].end == audio.frames() && !p.pads[7].hasEnd,
          "Manual cue starts at playhead and has no set end before file end");
    check(p.pads[7].colour == cueColour(1711, audio.frames()), "Manual cue color represents source position");
    const auto before = p.pads;
    assignCueAt(p, -1, 5, audio.sampleRate, audio.frames());
    assignCueAt(p, 32, 5, audio.sampleRate, audio.frames());
    bool unchanged = true;
    for (std::size_t i = 0; i < p.pads.size(); ++i) unchanged &= same(p.pads[i], before[i]);
    check(unchanged, "Invalid cue indices cannot alter cue state");

    constexpr double lengths[] {1.0 / 16, 1.0 / 8, .25, .5, 1, 2, 4, 8, 16};
    for (const double beats : lengths) {
        Project grid;
        grid.sourceBpm = 120;
        grid.pads[5] = markedCue();
        const auto favorite = grid.pads[5];
        constexpr std::size_t playhead = 1234;
        autoSetCues(grid, audio, {}, ChopMode::Grid, playhead, beats);
        bool positions = grid.padCount == 32;
        for (std::size_t i = 0; i < grid.pads.size(); ++i) {
            if (i == 5) continue;
            const auto& cue = grid.pads[i];
            const double start = playhead + i * 500 * beats;
            positions &= !cue.empty() && cue.hasEnd && std::abs(static_cast<double>(cue.start) - start) < 1.1
                && std::abs(static_cast<double>(cue.end) - (start + 500 * beats)) < 1.1;
        }
        check(positions, "Slicer makes 32 consecutive fixed-length cues from playhead across 1/16–16 beat sizes");
        check(same(grid.pads[5], favorite), "Slicer preserves favorite cue and every parameter");
    }

    Project shortGrid;
    const auto shortAudio = fixture(1000);
    autoSetCues(shortGrid, shortAudio, {}, ChopMode::Grid, 750, 1);
    check(shortGrid.pads[0].start == 750 && shortGrid.pads[0].end == 1000,
          "Slicer final cue clamps at source boundary");
    check(shortGrid.pads[1].empty() && shortGrid.pads[31].empty(), "Slicer cannot invent cues beyond source end");
}

void quantizationAndTempoMarkers() {
    Project p;
    p.sourceBpm = 120;
    p.quantize = true;
    check(snapCueFrame(p, 640, 1000, 5000) == 625, "Quantize snaps cue editing to nearest eighth of a beat");
    p.quantize = false;
    check(snapCueFrame(p, 640, 1000, 5000) == 640, "Quantize disabled preserves exact cue frame");
    p.quantize = true;
    p.tempoMarkers = {{1003, 60}, {2503, 120}};
    check(snapCueFrame(p, 1069, 1000, 5000) == 1128,
          "Piecewise quantize anchors to edited tempo marker instead of global frame zero");
    check(std::abs(static_cast<double>(snapCueFrame(p, 2580, 1000, 5000)) - 2565.5) <= .5,
          "Quantize adopts tempo and phase of following tempo marker");
    const auto beats = projectBeatFrames(p, 1000, 5000);
    const std::vector<std::size_t> expected {1003, 2003, 2503, 3003, 3503, 4003, 4503};
    check(beats == expected, "Beat grid follows tempo marker anchors and local BPM segments");
    Project delayedGrid;
    delayedGrid.quantize = true;
    delayedGrid.tempoMarkers = {{10000, 120}};
    const auto delayedBeats = projectBeatFrames(delayedGrid, 1000, 50000);
    check(!delayedBeats.empty() && delayedBeats.front() == 10000,
          "Set Grid Start anchors first beat at selected position without inventing a prefix grid");
    check(snapCueFrame(delayedGrid, 500, 1000, 50000) == 500,
          "Cue edits before first tempo anchor stay unsnapped without unsigned underflow");
    check(snapCueFrame(delayedGrid, 11013, 1000, 50000) == 11000,
          "Cue edits after delayed grid start snap relative to its phase");
    assignCueAt(p, 0, 1069, 1000, 5000);
    check(p.pads[0].start == 1128 && !p.pads[0].hasEnd, "New cue creation uses quantized playhead position");
    check(snapCueFrame(p, 999999, 1000, 5000) < 5000, "Quantized frames remain within source");
    check(projectBeatFrames(p, 0, 5000).empty() && projectBeatFrames(p, 1000, 0).empty(), "Invalid grid input yields no beat markers");
    check(snapCueFrame(p, 55, std::numeric_limits<double>::quiet_NaN(), 100) == 55,
          "Invalid sample rate cannot produce undefined quantized frame");
    p.beatgridEnabled = false;
    p.tempoMarkers.clear();
    check(projectBeatFrames(p, 1000, 5000).empty(), "Clearing beatgrid prevents automatic fallback marker regeneration");
    check(snapCueFrame(p, 640, 1000, 5000) == 640,
          "Cleared beatgrid leaves cue edits unsnapped even when Quantize remains enabled");
    assignCueAt(p, 3, 640, 1000, 5000);
    check(p.pads[3].start == 640 && !p.pads[3].hasEnd, "Cue creation respects cleared beatgrid state");
}

void moveAndCopy() {
    Project p;
    p.pads[0] = markedCue();
    const auto original = p.pads[0];
    moveCue(p, 0, 4, false);
    check(p.pads[0].empty() && same(p.pads[4], original), "Moving cue to empty pad carries name, color, parameters and output route");
    p.pads[2] = original;
    p.pads[2].name = "Swap destination";
    p.pads[2].outputChannel = 2;
    const auto destination = p.pads[2];
    moveCue(p, 4, 2, false);
    check(same(p.pads[2], original) && same(p.pads[4], destination), "Moving onto occupied pad swaps complete cue state");
    moveCue(p, 2, 9, true);
    check(same(p.pads[2], original) && same(p.pads[9], original), "Option-copy duplicates full cue and keeps source");
    moveCue(p, 4, 9, true);
    check(same(p.pads[9], destination), "Copy onto occupied destination overwrites its complete cue state");
    const auto before = p.pads;
    moveCue(p, -1, 0, false); moveCue(p, 0, 90, true); moveCue(p, 1, 2, false);
    bool unchanged = true;
    for (std::size_t i = 0; i < p.pads.size(); ++i) unchanged &= same(p.pads[i], before[i]);
    check(unchanged, "Invalid move/copy and empty source cannot modify cues");
}

void randomAndFind() {
    const auto audio = fixture();
    Analysis a;
    a.bpm = 120;
    a.onsets = {1000, 5210, 12700, 39000, 115000, 219000};
    Project randomA;
    randomA.pads[3] = markedCue();
    auto randomB = randomA;
    autoSetCues(randomA, audio, a, ChopMode::Random, 2000, 1, 1, 123);
    autoSetCues(randomB, audio, a, ChopMode::Random, 2000, 1, 1, 123);
    bool allOpen = true, reproducible = true, unsorted = false;
    for (std::size_t i = 0; i < 32; ++i) {
        reproducible &= same(randomA.pads[i], randomB.pads[i]);
        if (i == 3) continue;
        allOpen &= !randomA.pads[i].empty() && !randomA.pads[i].hasEnd && randomA.pads[i].end == audio.frames();
        if (i > 0 && i != 4) unsorted |= randomA.pads[i].start < randomA.pads[i - 1].start;
    }
    check(allOpen && randomA.padCount == 32, "Set Random creates 32 independent open-ended cues instead of partitioning audio");
    check(unsorted, "Set Random does not force cue positions into source order");
    check(reproducible, "Set Random is reproducible with explicit seed");
    check(same(randomA.pads[3], markedCue()), "Set Random never overwrites favorite cue");
    autoSetCues(randomB, audio, a, ChopMode::Random, 2000, 1, 1, 124);
    check(randomB.pads[0].start != randomA.pads[0].start, "New random seed provides another set of cue points");
    Project quantized;
    quantized.quantize = true;
    autoSetCues(quantized, audio, a, ChopMode::Random, 0, 1, 1, 123);
    bool snapped = true;
    for (const auto& cue : quantized.pads)
        snapped &= std::abs(cue.start / 62.5 - std::round(cue.start / 62.5)) < .01;
    check(snapped, "Quantized random cue creation aligns all starts to eighth-beat edit grid");

    Project found;
    found.pads[11] = markedCue();
    autoSetCues(found, audio, a, ChopMode::Transients, 0, 1, 1, 777);
    bool findMusical = true;
    const auto beatFrames = projectBeatFrames(found, audio.sampleRate, audio.frames());
    for (std::size_t i = 0; i < 32; ++i) {
        if (i == 11) continue;
        findMusical &= !found.pads[i].empty() && !found.pads[i].hasEnd
            && std::binary_search(beatFrames.begin(), beatFrames.end(), found.pads[i].start);
    }
    check(findMusical, "Find Samples chooses on-beat regions while retaining open cue lengths");
    check(same(found.pads[11], markedCue()), "Find Samples preserves favorites and their processing parameters");
}

void keyShiftAndUnavailableMetadata() {
    const auto audio = fixture();
    for (const int step : {1, 2}) {
        Project p;
        p.pads[7] = markedCue(); p.pads[7].favorite = false;
        p.pads[19] = markedCue();
        const auto base = p.pads[7];
        autoSetCues(p, audio, {}, ChopMode::KeyShift, 0, 1, step, 42, 7);
        bool duplicates = true, boundedPitch = true;
        std::set<int> pitches;
        for (std::size_t i = 0; i < 32; ++i) {
            if (i == 19) continue;
            auto copy = p.pads[i]; copy.pitchSemitones = base.pitchSemitones;
            duplicates &= same(copy, base);
            boundedPitch &= std::abs(p.pads[i].pitchSemitones - base.pitchSemitones) <= 12.0f
                && p.pads[i].pitchSemitones >= -24 && p.pads[i].pitchSemitones <= 24;
            pitches.insert(static_cast<int>(p.pads[i].pitchSemitones));
        }
        check(duplicates, "Key Shift duplicates selected cue region, processing, name, color and routing");
        check(boundedPitch && pitches.size() > 8, "Key Shift distributes pitch increments within one octave of selected root");
        check(p.pads[7].pitchSemitones == base.pitchSemitones, "Selected cue pitch remains Key Shift root");
        check(p.pads[8].pitchSemitones == base.pitchSemitones + step, "Following Key Shift pad increments by requested one/two semitones");
        check(same(p.pads[19], markedCue()), "Key Shift preserves other favorite cues");
    }

    Project noMetadata;
    noMetadata.pads[2] = markedCue();
    const auto before = noMetadata.pads;
    autoSetCues(noMetadata, audio, {}, ChopMode::SeratoDJ, 0);
    bool unchanged = true;
    for (std::size_t i = 0; i < 32; ++i) unchanged &= same(before[i], noMetadata.pads[i]);
    check(unchanged, "Unavailable Serato DJ metadata leaves cues unchanged");
}
} // namespace

int main() {
    manualAndGrid(); quantizationAndTempoMarkers(); moveAndCopy(); randomAndFind(); keyShiftAndUnavailableMetadata();
    std::cout << checks - failures << '/' << checks << " cue editing checks passed\n";
    return failures == 0 ? 0 : 1;
}
