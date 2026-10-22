#include "SessionIO.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace samplify {
namespace {
using Object = juce::DynamicObject;
juce::var object() { return juce::var(new Object()); }
void put(juce::var& v, const char* key, const juce::var& value) { v.getDynamicObject()->setProperty(key, value); }
[[noreturn]] void bad(const juce::String& detail) { throw std::runtime_error(("Invalid session: " + detail).toStdString()); }
double number(const juce::var& o, const char* key, double low, double high) {
    const auto v = o.getProperty(key, juce::var());
    if (!(v.isDouble() || v.isInt() || v.isInt64())) bad(juce::String(key) + " must be a number");
    const double n = static_cast<double>(v);
    if (!std::isfinite(n) || n < low || n > high) bad(juce::String(key) + " out of range");
    return n;
}
bool boolean(const juce::var& o, const char* key) {
    const auto v = o.getProperty(key, juce::var());
    if (!v.isBool()) bad(juce::String(key) + " must be true or false");
    return static_cast<bool>(v);
}
juce::String text(const juce::var& o, const char* key) {
    const auto v = o.getProperty(key, juce::var());
    if (!v.isString() || v.toString().length() > 4096) bad(juce::String(key) + " must be text");
    return v.toString();
}
juce::String storePath(const juce::File& f, const juce::File& directory) {
    if (f == juce::File()) return {};
    return f.isAChildOf(directory) ? f.getRelativePathFrom(directory) : f.getFullPathName();
}
juce::File resolve(const juce::String& path, const juce::File& directory) {
    if (path.isEmpty()) return {};
    return juce::File::isAbsolutePath(path) ? juce::File(path) : directory.getChildFile(path);
}
}

void saveSession(const juce::File& file, const Session& s) {
    auto root = object();
    put(root, "format", "MP Audio Samplify"); put(root, "version", 2);
    const auto dir = file.getParentDirectory();
    put(root, "source", storePath(s.source, dir));
    juce::Array<juce::var> stems;
    for (const auto& stem : s.stems) stems.add(storePath(stem, dir));
    put(root, "stems", stems);
    const auto& p = s.project;
    auto project = object();
    put(project, "projectBpm", p.projectBpm); put(project, "sourceBpm", p.sourceBpm);
    put(project, "projectKey", juce::String(p.projectKey)); put(project, "sourceKey", juce::String(p.sourceKey));
    put(project, "mono", p.mono); put(project, "quantize", p.quantize);
    put(project, "syncTempo", p.syncTempo); put(project, "syncKey", p.syncKey); put(project, "padCount", p.padCount);
    put(project, "playbackMode", p.playbackMode == CuePlaybackMode::Hold ? "Hold" : "Trigger");
    put(project, "velocityEnabled", p.velocityEnabled); put(project, "pitchTimeEnabled", p.pitchTimeEnabled);
    put(project, "keyboardMode", p.keyboardMode);
    put(project, "globalPitchSemitones", p.globalPitchSemitones); put(project, "globalDetuneCents", p.globalDetuneCents);
    put(project, "glideMs", p.glideMs); put(project, "glideMode", p.glideMode == GlideMode::Trigger ? "Trigger" : "Sustain");
    juce::Array<juce::var> globalMute, tempoMarkers;
    for (bool b : p.stemMute) globalMute.add(b);
    put(project, "stemMute", globalMute);
    for (const auto& marker : p.tempoMarkers) {
        auto m = object(); put(m, "frame", static_cast<juce::int64>(marker.frame)); put(m, "bpm", marker.bpm); tempoMarkers.add(m);
    }
    put(project, "tempoMarkers", tempoMarkers);
    put(project, "beatgridEnabled", p.beatgridEnabled);
    juce::Array<juce::var> pads;
    for (const auto& pad : p.pads) {
        auto v = object();
        put(v, "start", static_cast<juce::int64>(pad.start)); put(v, "end", static_cast<juce::int64>(pad.end));
        put(v, "levelDb", pad.levelDb); put(v, "pitchSemitones", pad.pitchSemitones);
        put(v, "detuneCents", pad.detuneCents); put(v, "speedRatio", pad.speedRatio);
        put(v, "filter", pad.filter); put(v, "attackMs", pad.attackMs); put(v, "releaseMs", pad.releaseMs);
        put(v, "reverse", pad.reverse);
        put(v, "hasEnd", pad.hasEnd); put(v, "favorite", pad.favorite);
        put(v, "name", juce::String::fromUTF8(pad.name.c_str()));
        put(v, "colour", static_cast<juce::int64>(pad.colour)); put(v, "outputChannel", pad.outputChannel);
        juce::Array<juce::var> mute;
        for (const bool b : pad.stemMute) mute.add(b);
        put(v, "stemMute", mute); pads.add(v);
    }
    put(project, "pads", pads); put(root, "project", project);
    // Round-trip validation prevents persisting state this version cannot restore.
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(root, false)))
        throw std::runtime_error("Could not write session file");
    (void) loadSession(temporary.getFile());
    if (!temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Could not replace session file");
}

Session loadSession(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > 2 * 1024 * 1024) bad("file missing or exceeds 2 MB");
    juce::var root;
    const auto parsed = juce::JSON::parse(file.loadFileAsString(), root);
    if (parsed.failed() || !root.isObject()) bad("expected a JSON object");
    if (text(root, "format") != "MP Audio Samplify") bad("unsupported format");
    const auto version = static_cast<int>(number(root, "version", 1, 2));
    if (number(root, "version", 1, 2) != version) bad("fractional format version");
    Session s;
    const auto dir = file.getParentDirectory();
    s.source = resolve(text(root, "source"), dir);
    const auto stems = root.getProperty("stems", juce::var());
    if (!stems.isArray() || stems.size() != 4) bad("expected four stem paths");
    for (int i = 0; i < 4; ++i) {
        if (!stems[i].isString()) bad("stem path must be text");
        s.stems[static_cast<std::size_t>(i)] = resolve(stems[i].toString(), dir);
    }
    const auto v = root.getProperty("project", juce::var());
    if (!v.isObject()) bad("missing project");
    auto& p = s.project;
    p.projectBpm = number(v, "projectBpm", 20, 999); p.sourceBpm = number(v, "sourceBpm", 20, 999);
    p.projectKey = text(v, "projectKey").toStdString(); p.sourceKey = text(v, "sourceKey").toStdString();
    p.mono = boolean(v, "mono"); p.quantize = boolean(v, "quantize");
    p.syncTempo = boolean(v, "syncTempo"); p.syncKey = boolean(v, "syncKey");
    auto muteArray = [](const juce::var& value, std::array<bool, 4>& target) {
        if (!value.isArray() || value.size() != 4) bad("expected four stem mute states");
        for (int i = 0; i < 4; ++i) {
            if (!value[i].isBool()) bad("stem mute must be true or false");
            target[static_cast<std::size_t>(i)] = static_cast<bool>(value[i]);
        }
    };
    if (version == 2) {
        const auto mode = text(v, "playbackMode"), glideMode = text(v, "glideMode");
        if (mode != "Hold" && mode != "Trigger") bad("unknown cue playback mode");
        if (glideMode != "Trigger" && glideMode != "Sustain") bad("unknown glide mode");
        p.playbackMode = mode == "Hold" ? CuePlaybackMode::Hold : CuePlaybackMode::Trigger;
        p.glideMode = glideMode == "Trigger" ? GlideMode::Trigger : GlideMode::Sustain;
        p.velocityEnabled = boolean(v, "velocityEnabled"); p.pitchTimeEnabled = boolean(v, "pitchTimeEnabled");
        p.keyboardMode = boolean(v, "keyboardMode");
        p.beatgridEnabled = boolean(v, "beatgridEnabled");
        p.globalPitchSemitones = static_cast<float>(number(v, "globalPitchSemitones", -60, 60));
        p.globalDetuneCents = static_cast<float>(number(v, "globalDetuneCents", -100, 100));
        p.glideMs = static_cast<float>(number(v, "glideMs", 0, 5000));
        muteArray(v.getProperty("stemMute", juce::var()), p.stemMute);
        const auto grid = v.getProperty("tempoMarkers", juce::var());
        if (!grid.isArray() || grid.size() > 128) bad("expected at most 128 tempo markers");
        std::size_t previous = 0;
        for (int i = 0; i < grid.size(); ++i) {
            if (!grid[i].isObject()) bad("tempo marker must be an object");
            const double n = number(grid[i], "frame", 0, 2000000000.0);
            if (n != std::floor(n) || (i > 0 && n <= static_cast<double>(previous))) bad("tempo markers must have unique ordered integer frames");
            previous = static_cast<std::size_t>(n);
            p.tempoMarkers.push_back({previous, number(grid[i], "bpm", 20, 999)});
        }
    } else {
        s.migrationNotice = "Legacy session migrated: duration ratios converted to speed; cue levels below -12 dB normalized to the supported range.";
    }
    const auto count = number(v, "padCount", 16, 32);
    if (count != 16 && count != 32) bad("padCount must be 16 or 32");
    p.padCount = static_cast<int>(count);
    const auto pads = v.getProperty("pads", juce::var());
    if (!pads.isArray() || pads.size() != 32) bad("expected 32 pad slots");
    for (int i = 0; i < 32; ++i) {
        const auto a = pads[i];
        if (!a.isObject()) bad("pad must be an object");
        auto& pad = p.pads[static_cast<std::size_t>(i)];
        auto frame = [&](const char* key) {
            const auto n = number(a, key, 0, 2000000000.0);
            if (n != std::floor(n)) bad("fractional frame offset");
            return static_cast<std::size_t>(n);
        };
        pad.start = frame("start"); pad.end = frame("end");
        if (pad.end < pad.start) bad("cue end precedes cue start");
        pad.levelDb = static_cast<float>(number(a, "levelDb", version == 1 ? -60 : -12, 12));
        if (version == 1) pad.levelDb = std::max(-12.0f, pad.levelDb);
        pad.pitchSemitones = static_cast<float>(number(a, "pitchSemitones", -24, 24));
        pad.detuneCents = static_cast<float>(number(a, "detuneCents", -100, 100));
        pad.speedRatio = static_cast<float>(version == 1 ? 1.0 / number(a, "timeRatio", .25, 4) : number(a, "speedRatio", .25, 4));
        pad.filter = static_cast<float>(number(a, "filter", 0, 1));
        pad.attackMs = static_cast<float>(number(a, "attackMs", 0, 10000));
        pad.releaseMs = static_cast<float>(number(a, "releaseMs", 0, 10000));
        pad.reverse = boolean(a, "reverse");
        muteArray(a.getProperty("stemMute", juce::var()), pad.stemMute);
        if (version == 2) {
            pad.hasEnd = boolean(a, "hasEnd"); pad.favorite = boolean(a, "favorite");
            pad.name = text(a, "name").toStdString();
            const auto colour = number(a, "colour", 0, 4294967295.0);
            const auto route = number(a, "outputChannel", 0, 31);
            if (colour != std::floor(colour) || route != std::floor(route)) bad("color and output channel must be integers");
            pad.colour = static_cast<std::uint32_t>(colour); pad.outputChannel = static_cast<int>(route);
        }
    }
    return s;
}
} // namespace samplify
