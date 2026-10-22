#include "CueEditing.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace samplify {
namespace {
std::vector<TempoMarker> markers(const Project& p, std::size_t frames) {
    std::vector<TempoMarker> result;
    for (const auto& m : p.tempoMarkers)
        if (m.frame < frames && std::isfinite(m.bpm) && m.bpm >= 20 && m.bpm <= 999) result.push_back(m);
    std::stable_sort(result.begin(), result.end(), [](auto a, auto b) { return a.frame < b.frame; });
    result.erase(std::unique(result.begin(), result.end(), [](auto a, auto b) { return a.frame == b.frame; }), result.end());
    if (result.size() > 128) result.resize(128);
    if (result.empty())
        result.insert(result.begin(), {0, std::isfinite(p.sourceBpm) ? std::clamp(p.sourceBpm, 20.0, 999.0) : 120});
    return result;
}
Pad at(const Project& p, std::size_t frame, double rate, std::size_t frames) {
    Pad cue;
    cue.start = snapCueFrame(p, frame, rate, frames);
    cue.end = frames;
    cue.hasEnd = false;
    cue.colour = cueColour(cue.start, frames);
    return cue;
}
}

std::uint32_t cueColour(std::size_t frame, std::size_t frames) {
    const double fraction = frames ? static_cast<double>(frame) / static_cast<double>(frames) : 0;
    constexpr std::uint32_t colours[] {0xffcc3629, 0xffdf492d, 0xffc8822a, 0xffb6a444, 0xff8da83f, 0xff71a740};
    return colours[std::min(5, static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 6))];
}

std::size_t snapCueFrame(const Project& p, std::size_t frame, double rate, std::size_t frames) {
    if (!frames) return 0;
    frame = std::min(frame, frames - 1);
    if (!p.quantize || !p.beatgridEnabled || !std::isfinite(rate) || rate <= 0) return frame;
    const auto grid = markers(p, frames);
    if (frame < grid.front().frame) return frame;
    auto active = grid.front();
    std::size_t end = frames - 1;
    for (std::size_t i = 0; i < grid.size(); ++i) {
        if (grid[i].frame <= frame) { active = grid[i]; end = i + 1 < grid.size() ? grid[i + 1].frame : frames - 1; }
    }
    const double step = std::max(1.0, rate * 60 / active.bpm / 8);
    const double snapped = active.frame + std::round(static_cast<double>(frame - active.frame) / step) * step;
    return std::min(end, static_cast<std::size_t>(std::llround(std::clamp(snapped, 0.0, static_cast<double>(frames - 1)))));
}

std::vector<std::size_t> projectBeatFrames(const Project& p, double rate, std::size_t frames, double subdivision) {
    std::vector<std::size_t> beats;
    if (!p.beatgridEnabled || !frames || !std::isfinite(rate) || rate <= 0 || !std::isfinite(subdivision) || subdivision <= 0) return beats;
    const auto grid = markers(p, frames);
    for (std::size_t i = 0; i < grid.size() && beats.size() < 1000000; ++i) {
        const auto end = i + 1 < grid.size() ? grid[i + 1].frame : frames;
        const double step = std::max(1.0, rate * 60 / grid[i].bpm * subdivision);
        for (double f = static_cast<double>(grid[i].frame); f < end && beats.size() < 1000000; f += step) {
            const auto frame = static_cast<std::size_t>(std::llround(f));
            if (frame < frames && (beats.empty() || frame > beats.back())) beats.push_back(frame);
        }
    }
    return beats;
}

void assignCueAt(Project& p, int index, std::size_t frame, double rate, std::size_t frames) {
    if (index < 0 || index >= 32 || !frames) return;
    p.pads[static_cast<std::size_t>(index)] = at(p, frame, rate, frames);
}

void moveCue(Project& p, int from, int to, bool copy) {
    if (from < 0 || from >= 32 || to < 0 || to >= 32 || from == to || p.pads[static_cast<std::size_t>(from)].empty()) return;
    if (copy) p.pads[static_cast<std::size_t>(to)] = p.pads[static_cast<std::size_t>(from)];
    else std::swap(p.pads[static_cast<std::size_t>(from)], p.pads[static_cast<std::size_t>(to)]);
}

void autoSetCues(Project& p, const AudioData& audio, const Analysis& analysis, ChopMode mode,
                std::size_t playhead, double beatsPerSlice, int keyStep, std::uint32_t seed, int selectedCue) {
    if (!audio.valid() || mode == ChopMode::SeratoDJ) return;
    const auto frames = audio.frames();
    playhead = std::min(playhead, frames - 1);
    std::mt19937 random(seed);
    const auto original = p.pads;
    const auto base = original[static_cast<std::size_t>(std::clamp(selectedCue, 0, 31))];
    if (mode == ChopMode::KeyShift && base.empty()) return;
    auto beats = projectBeatFrames(p, audio.sampleRate, frames);
    std::vector<std::size_t> candidates;
    if (mode == ChopMode::Transients) {
        // Independent energy/contrast ranking identifies useful regions; it is
        // not Serato's private candidate-selection algorithm.
        std::vector<std::pair<double, std::size_t>> scored;
        const auto window = static_cast<std::size_t>(audio.sampleRate * .08);
        for (const auto onset : analysis.onsets) {
            if (onset >= frames) continue;
            double energy = 0, previous = 0;
            const auto count = std::min(window, frames - onset);
            for (std::size_t i = 0; i < count; i += 8) energy += std::abs(audio.channels.front()[onset + i]);
            for (std::size_t i = onset > window ? onset - window : 0; i < onset; i += 8) previous += std::abs(audio.channels.front()[i]);
            scored.emplace_back(energy + std::max(0.0, energy - previous), onset);
        }
        std::sort(scored.begin(), scored.end(), [](auto a, auto b) { return a.first > b.first; });
        if (scored.size() > 128) scored.resize(128);
        for (const auto& [score, position] : scored) {
            (void) score;
            const auto beat = std::lower_bound(beats.begin(), beats.end(), position);
            if (beat != beats.end()) candidates.push_back(*beat);
        }
        if (candidates.empty()) candidates = beats;
    }
    const auto grid = markers(p, frames);
    double bpm = p.sourceBpm;
    for (const auto& m : grid) if (m.frame <= playhead) bpm = m.bpm;
    const double sliceFrames = audio.sampleRate * 60 / std::clamp(bpm, 20.0, 999.0)
        * std::clamp(beatsPerSlice, 1.0 / 16, 16.0);
    for (int index = 0; index < 32; ++index) {
        auto& cue = p.pads[static_cast<std::size_t>(index)];
        if (cue.favorite) continue;
        if (mode == ChopMode::KeyShift) {
            cue = base; cue.favorite = false;
            const int step = keyStep == 2 ? 2 : 1;
            const int relative = (index - std::clamp(selectedCue, 0, 31)) * step;
            const int offset = ((relative + 12) % 25 + 25) % 25 - 12;
            cue.pitchSemitones = std::clamp(base.pitchSemitones + static_cast<float>(offset), -24.0f, 24.0f);
            continue;
        }
        if (mode == ChopMode::Grid) {
            const double start = playhead + index * sliceFrames;
            if (start >= frames) { cue = {}; continue; }
            cue = at(p, static_cast<std::size_t>(start), audio.sampleRate, frames);
            cue.hasEnd = true;
            const auto rawEnd = static_cast<std::size_t>(std::min(static_cast<double>(frames), start + sliceFrames));
            cue.end = rawEnd == frames ? frames : snapCueFrame(p, rawEnd, audio.sampleRate, frames);
            cue.end = std::max(cue.start + 1, cue.end);
            continue;
        }
        std::size_t position = 0;
        if (mode == ChopMode::Transients && !candidates.empty()) position = candidates[random() % candidates.size()];
        else position = static_cast<std::size_t>(random()) % frames;
        cue = at(p, position, audio.sampleRate, frames);
    }
    p.padCount = 32;
}
} // namespace samplify
