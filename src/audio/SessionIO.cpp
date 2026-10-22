#include "SessionIO.h"
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
    put(root, "format", "MP Audio Samplify"); put(root, "version", 1);
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
    juce::Array<juce::var> pads;
    for (const auto& pad : p.pads) {
        auto v = object();
        put(v, "start", static_cast<juce::int64>(pad.start)); put(v, "end", static_cast<juce::int64>(pad.end));
        put(v, "levelDb", pad.levelDb); put(v, "pitchSemitones", pad.pitchSemitones);
        put(v, "detuneCents", pad.detuneCents); put(v, "timeRatio", pad.timeRatio);
        put(v, "filter", pad.filter); put(v, "attackMs", pad.attackMs); put(v, "releaseMs", pad.releaseMs);
        put(v, "reverse", pad.reverse);
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
    if (text(root, "format") != "MP Audio Samplify" || number(root, "version", 1, 1) != 1) bad("unsupported format");
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
    p.projectBpm = number(v, "projectBpm", 30, 300); p.sourceBpm = number(v, "sourceBpm", 30, 300);
    p.projectKey = text(v, "projectKey").toStdString(); p.sourceKey = text(v, "sourceKey").toStdString();
    p.mono = boolean(v, "mono"); p.quantize = boolean(v, "quantize");
    p.syncTempo = boolean(v, "syncTempo"); p.syncKey = boolean(v, "syncKey");
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
        pad.levelDb = static_cast<float>(number(a, "levelDb", -60, 12));
        pad.pitchSemitones = static_cast<float>(number(a, "pitchSemitones", -24, 24));
        pad.detuneCents = static_cast<float>(number(a, "detuneCents", -100, 100));
        pad.timeRatio = static_cast<float>(number(a, "timeRatio", .25, 4));
        pad.filter = static_cast<float>(number(a, "filter", 0, 1));
        pad.attackMs = static_cast<float>(number(a, "attackMs", 0, 2000));
        pad.releaseMs = static_cast<float>(number(a, "releaseMs", 0, 5000));
        pad.reverse = boolean(a, "reverse");
        const auto mute = a.getProperty("stemMute", juce::var());
        if (!mute.isArray() || mute.size() != 4) bad("expected four stem mute states");
        for (int j = 0; j < 4; ++j) {
            if (!mute[j].isBool()) bad("stem mute must be true or false");
            pad.stemMute[static_cast<std::size_t>(j)] = static_cast<bool>(mute[j]);
        }
    }
    return s;
}
} // namespace samplify
