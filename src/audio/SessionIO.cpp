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
std::string longText(const juce::var& o, const char* key, std::size_t maximum) {
    const auto value = o.getProperty(key, juce::var());
    if (!value.isString()) bad(juce::String(key) + " must be text");
    auto result = value.toString().toStdString();
    if (result.size() > maximum || result.find('\0') != std::string::npos) bad(juce::String(key) + " exceeds text bounds");
    return result;
}
int integer(const juce::var& o, const char* key, int low, int high) {
    const auto value = number(o, key, low, high);
    if (value != std::floor(value)) bad(juce::String(key) + " must be an integer");
    return static_cast<int>(value);
}
WorkstationId identifier(const juce::var& o, const char* key, bool allowZero = false) {
    const auto value = number(o, key, allowZero ? 0 : 1, 4294967295.0);
    if (value != std::floor(value)) bad(juce::String(key) + " must be an integer ID");
    return static_cast<WorkstationId>(value);
}
juce::var array(const juce::var& o, const char* key, int maximum, int exact = -1) {
    const auto value = o.getProperty(key, juce::var());
    if (!value.isArray() || value.size() > maximum || (exact >= 0 && value.size() != exact))
        bad(juce::String(key) + " has an invalid array size");
    return value;
}
juce::String storePath(const juce::File& f, const juce::File& directory) {
    if (f == juce::File()) return {};
    return f.isAChildOf(directory) ? f.getRelativePathFrom(directory) : f.getFullPathName();
}
juce::File resolve(const juce::String& path, const juce::File& directory) {
    if (path.isEmpty()) return {};
    return juce::File::isAbsolutePath(path) ? juce::File(path) : directory.getChildFile(path);
}

constexpr const char* fxNames[] { "None", "Equalizer", "Compressor", "Saturator", "BitCrusher", "Chorus", "Delay",
    "Reverb", "Filter", "Tremolo", "Limiter", "StereoWidth", "Transient", "Flanger", "Phaser", "Tape" };
juce::var storeFx(const std::array<FxSlot, workstationFxCount>& rack) {
    juce::Array<juce::var> values;
    for (const auto& slot : rack) {
        auto value = object();
        put(value, "type", fxNames[static_cast<std::size_t>(slot.type)]); put(value, "bypass", slot.bypass); put(value, "mix", slot.mix);
        juce::Array<juce::var> parameters;
        for (const auto parameter : slot.parameters) parameters.add(parameter);
        put(value, "parameters", parameters); values.add(value);
    }
    return values;
}
void readFx(const juce::var& parent, const char* key, std::array<FxSlot, workstationFxCount>& rack) {
    const auto values = array(parent, key, 4, 4);
    for (int i = 0; i < 4; ++i) {
        if (!values[i].isObject()) bad("effect slot must be an object");
        const auto name = text(values[i], "type"); int type = -1;
        for (int j = 0; j < static_cast<int>(std::size(fxNames)); ++j) if (name == fxNames[j]) type = j;
        if (type < 0) bad("unknown effect type");
        auto& slot = rack[static_cast<std::size_t>(i)]; slot.type = static_cast<FxType>(type);
        slot.bypass = boolean(values[i], "bypass"); slot.mix = static_cast<float>(number(values[i], "mix", 0, 1));
        const auto parameters = array(values[i], "parameters", 6, 6);
        for (int p = 0; p < 6; ++p) {
            const auto n = parameters[p];
            if (!(n.isDouble() || n.isInt() || n.isInt64())) bad("effect parameter must be numeric");
            const auto value = static_cast<double>(n);
            if (!std::isfinite(value) || value < 0 || value > 1) bad("effect parameter out of range");
            slot.parameters[static_cast<std::size_t>(p)] = static_cast<float>(value);
        }
    }
}
juce::var storePlugins(const std::array<HostedPluginSlot, workstationFxCount>& rack) {
    juce::Array<juce::var> values;
    for (const auto& slot : rack) {
        auto value = object();
        put(value, "name", juce::String(slot.name)); put(value, "format", juce::String(slot.format));
        put(value, "identifier", juce::String(slot.identifier)); put(value, "manufacturer", juce::String(slot.manufacturer));
        put(value, "xmlDescription", juce::String(slot.xmlDescription)); put(value, "stateBase64", juce::String(slot.stateBase64));
        put(value, "bypass", slot.bypass); values.add(value);
    }
    return values;
}
void readPlugins(const juce::var& parent, const char* key, std::array<HostedPluginSlot, workstationFxCount>& rack) {
    const auto values = array(parent, key, 4, 4);
    for (int i = 0; i < 4; ++i) {
        if (!values[i].isObject()) bad("plugin slot must be an object");
        auto& slot = rack[static_cast<std::size_t>(i)];
        slot.name = longText(values[i], "name", 256); slot.format = longText(values[i], "format", 256);
        slot.identifier = longText(values[i], "identifier", 4096); slot.manufacturer = longText(values[i], "manufacturer", 256);
        slot.xmlDescription = longText(values[i], "xmlDescription", 65536);
        slot.stateBase64 = longText(values[i], "stateBase64", 4 * ((workstationMaxPluginStateBytes + 2) / 3));
        slot.bypass = boolean(values[i], "bypass");
        if (!slot.xmlDescription.empty()) {
            const auto xml = juce::parseXML(juce::String(slot.xmlDescription));
            if (!xml || !xml->hasTagName("PLUGIN")) bad("plugin descriptor must be PluginDescription XML");
        }
    }
}
const char* kindName(ClipKind kind) { return kind == ClipKind::Pattern ? "Pattern" : kind == ClipKind::Audio ? "Audio" : "Empty"; }
ClipKind readKind(const juce::var& value, bool allowEmpty) {
    const auto kind = text(value, "kind");
    if (kind == "Pattern") return ClipKind::Pattern;
    if (kind == "Audio") return ClipKind::Audio;
    if (kind == "Empty" && allowEmpty) return ClipKind::Empty;
    bad("unknown clip kind");
}
juce::var storeWorkstation(const WorkstationState& state, const juce::File& directory) {
    auto result = object();
    put(result, "tempo", state.tempo); put(result, "masterGainDb", state.masterGainDb); put(result, "crossfader", state.crossfader);
    put(result, "recordQuantizeBeats", state.recordQuantizeBeats); put(result, "launchQuantizeBeats", state.launchQuantizeBeats);
    put(result, "arrangementLoop", state.arrangementLoop); put(result, "loopStartBeat", state.loopStartBeat); put(result, "loopEndBeat", state.loopEndBeat);
    put(result, "selectedPattern", state.selectedPattern); put(result, "selectedTrack", state.selectedTrack);
    put(result, "masterEffects", storeFx(state.masterEffects)); put(result, "sendAEffects", storeFx(state.sendAEffects)); put(result, "sendBEffects", storeFx(state.sendBEffects));
    put(result, "masterPlugins", storePlugins(state.masterPlugins));
    juce::Array<juce::var> tracks, patterns, clips, launches, assets;
    for (const auto& track : state.tracks) {
        auto value = object(); put(value, "name", juce::String(track.name)); put(value, "gainDb", track.gainDb); put(value, "pan", track.pan);
        put(value, "mute", track.mute); put(value, "solo", track.solo); put(value, "sendA", track.sendA); put(value, "sendB", track.sendB);
        put(value, "crossfadeAssignment", track.crossfadeAssignment == CrossfadeAssignment::A ? "A" : track.crossfadeAssignment == CrossfadeAssignment::B ? "B" : "None");
        put(value, "effects", storeFx(track.effects)); put(value, "plugins", storePlugins(track.plugins)); tracks.add(value);
    }
    for (const auto& pattern : state.patterns) {
        auto value = object(); put(value, "id", static_cast<juce::int64>(pattern.id)); put(value, "name", juce::String(pattern.name)); put(value, "lengthBeats", pattern.lengthBeats);
        juce::Array<juce::var> notes;
        for (const auto& note : pattern.notes) {
            auto n = object(); put(n, "id", static_cast<juce::int64>(note.id)); put(n, "pad", note.pad);
            put(n, "beat", note.beat); put(n, "duration", note.duration); put(n, "velocity", note.velocity); put(n, "semitones", note.semitones); notes.add(n);
        }
        put(value, "notes", notes); patterns.add(value);
    }
    for (const auto& clip : state.arrangement) {
        auto value = object(); put(value, "id", static_cast<juce::int64>(clip.id)); put(value, "track", clip.track); put(value, "kind", kindName(clip.kind));
        put(value, "contentId", static_cast<juce::int64>(clip.contentId)); put(value, "startBeat", clip.startBeat); put(value, "lengthBeats", clip.lengthBeats);
        put(value, "offsetBeats", clip.offsetBeats); put(value, "loop", clip.loop); put(value, "gain", clip.gain); clips.add(value);
    }
    for (const auto& slot : state.launchSlots) {
        auto value = object(); put(value, "kind", kindName(slot.kind)); put(value, "contentId", static_cast<juce::int64>(slot.contentId)); put(value, "loop", slot.loop); launches.add(value);
    }
    for (const auto& asset : state.audioAssets) {
        auto value = object(); put(value, "id", static_cast<juce::int64>(asset.id)); put(value, "name", juce::String(asset.name));
        put(value, "filePath", storePath(resolve(juce::String(asset.filePath), directory), directory));
        put(value, "sourceBpm", asset.sourceBpm); put(value, "lengthBeats", asset.lengthBeats); assets.add(value);
    }
    put(result, "tracks", tracks); put(result, "patterns", patterns); put(result, "arrangement", clips);
    put(result, "launchSlots", launches); put(result, "audioAssets", assets);
    return result;
}
WorkstationState readWorkstation(const juce::var& parent, const juce::File& directory) {
    const auto value = parent.getProperty("workstation", juce::var());
    if (!value.isObject()) bad("missing workstation");
    WorkstationState state;
    state.tempo = number(value, "tempo", 20, 999); state.masterGainDb = static_cast<float>(number(value, "masterGainDb", -60, 12)); state.crossfader = static_cast<float>(number(value, "crossfader", 0, 1));
    state.recordQuantizeBeats = number(value, "recordQuantizeBeats", 0, 16); state.launchQuantizeBeats = number(value, "launchQuantizeBeats", 0, 64);
    state.arrangementLoop = boolean(value, "arrangementLoop"); state.loopStartBeat = number(value, "loopStartBeat", 0, workstationMaxBeat); state.loopEndBeat = number(value, "loopEndBeat", 0, workstationMaxBeat);
    state.selectedPattern = integer(value, "selectedPattern", 0, 15); state.selectedTrack = integer(value, "selectedTrack", 0, 7);
    readFx(value, "masterEffects", state.masterEffects); readFx(value, "sendAEffects", state.sendAEffects); readFx(value, "sendBEffects", state.sendBEffects); readPlugins(value, "masterPlugins", state.masterPlugins);
    const auto tracks = array(value, "tracks", 8, 8);
    for (int i = 0; i < 8; ++i) {
        const auto v = tracks[i]; if (!v.isObject()) bad("track must be an object");
        auto& track = state.tracks[static_cast<std::size_t>(i)]; track.name = longText(v, "name", 256);
        track.gainDb = static_cast<float>(number(v, "gainDb", -60, 12)); track.pan = static_cast<float>(number(v, "pan", -1, 1));
        track.mute = boolean(v, "mute"); track.solo = boolean(v, "solo"); track.sendA = static_cast<float>(number(v, "sendA", 0, 1)); track.sendB = static_cast<float>(number(v, "sendB", 0, 1));
        const auto crossfade = text(v, "crossfadeAssignment");
        if (crossfade != "None" && crossfade != "A" && crossfade != "B") bad("unknown crossfade assignment");
        track.crossfadeAssignment = crossfade == "A" ? CrossfadeAssignment::A : crossfade == "B" ? CrossfadeAssignment::B : CrossfadeAssignment::None;
        readFx(v, "effects", track.effects); readPlugins(v, "plugins", track.plugins);
    }
    const auto patterns = array(value, "patterns", 16, 16);
    for (int i = 0; i < 16; ++i) {
        const auto v = patterns[i]; if (!v.isObject()) bad("pattern must be an object");
        auto& pattern = state.patterns[static_cast<std::size_t>(i)]; pattern.id = identifier(v, "id"); pattern.name = longText(v, "name", 256); pattern.lengthBeats = number(v, "lengthBeats", 4, 64);
        const auto notes = array(v, "notes", static_cast<int>(workstationMaxNotes)); pattern.notes.reserve(static_cast<std::size_t>(notes.size()));
        for (int n = 0; n < notes.size(); ++n) {
            const auto vnote = notes[n]; if (!vnote.isObject()) bad("pattern note must be an object");
            PatternNote note; note.id = identifier(vnote, "id"); note.pad = integer(vnote, "pad", 0, 31);
            note.beat = number(vnote, "beat", 0, 64); note.duration = number(vnote, "duration", 0, 64);
            note.velocity = static_cast<float>(number(vnote, "velocity", 0, 1)); note.semitones = static_cast<float>(number(vnote, "semitones", -48, 48)); pattern.notes.push_back(note);
        }
    }
    const auto assets = array(value, "audioAssets", static_cast<int>(workstationMaxAssets));
    for (int i = 0; i < assets.size(); ++i) {
        const auto v = assets[i]; if (!v.isObject()) bad("audio asset must be an object");
        AudioAsset asset; asset.id = identifier(v, "id"); asset.name = longText(v, "name", 256);
        asset.filePath = resolve(juce::String(longText(v, "filePath", 4096)), directory).getFullPathName().toStdString();
        asset.sourceBpm = number(v, "sourceBpm", 20, 999); asset.lengthBeats = number(v, "lengthBeats", 0, workstationMaxBeat); state.audioAssets.push_back(std::move(asset));
    }
    const auto clips = array(value, "arrangement", static_cast<int>(workstationMaxClips));
    for (int i = 0; i < clips.size(); ++i) {
        const auto v = clips[i]; if (!v.isObject()) bad("arrangement clip must be an object");
        ArrangementClip clip; clip.id = identifier(v, "id"); clip.track = integer(v, "track", 0, 7); clip.kind = readKind(v, false); clip.contentId = identifier(v, "contentId");
        clip.startBeat = number(v, "startBeat", 0, workstationMaxBeat); clip.lengthBeats = number(v, "lengthBeats", 0, workstationMaxBeat); clip.offsetBeats = number(v, "offsetBeats", 0, workstationMaxBeat);
        clip.loop = boolean(v, "loop"); clip.gain = static_cast<float>(number(v, "gain", 0, 4)); state.arrangement.push_back(clip);
    }
    const auto launches = array(value, "launchSlots", 32, 32);
    for (int i = 0; i < 32; ++i) {
        const auto v = launches[i]; if (!v.isObject()) bad("launch slot must be an object");
        auto& slot = state.launchSlots[static_cast<std::size_t>(i)]; slot.kind = readKind(v, true); slot.contentId = identifier(v, "contentId", true); slot.loop = boolean(v, "loop");
    }
    try { validateWorkstation(state); } catch (const std::exception& e) { bad(juce::String("workstation: ") + e.what()); }
    return state;
}
}

void saveSession(const juce::File& file, const Session& s) {
    validateWorkstation(s.workstation);
    auto root = object();
    put(root, "format", "MP Audio Samplify"); put(root, "version", 3);
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
    put(root, "workstation", storeWorkstation(s.workstation, dir));
    // Round-trip validation prevents persisting state this version cannot restore.
    juce::TemporaryFile temporary(file);
    const auto encoded = juce::JSON::toString(root, false);
    if (encoded.getNumBytesAsUTF8() > workstationMaxSessionBytes)
        throw std::runtime_error("Session exceeds the 64 MiB file limit; no plugin state was truncated");
    if (!temporary.getFile().replaceWithText(encoded))
        throw std::runtime_error("Could not write session file");
    (void) loadSession(temporary.getFile());
    if (!temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Could not replace session file");
}

Session loadSession(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > static_cast<juce::int64>(workstationMaxSessionBytes)) bad("file missing or exceeds 64 MiB");
    juce::var root;
    const auto parsed = juce::JSON::parse(file.loadFileAsString(), root);
    if (parsed.failed() || !root.isObject()) bad("expected a JSON object");
    if (text(root, "format") != "MP Audio Samplify") bad("unsupported format");
    const auto version = static_cast<int>(number(root, "version", 1, 3));
    if (number(root, "version", 1, 3) != version) bad("fractional format version");
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
    if (version >= 2) {
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
        if (version >= 2) {
            pad.hasEnd = boolean(a, "hasEnd"); pad.favorite = boolean(a, "favorite");
            pad.name = text(a, "name").toStdString();
            const auto colour = number(a, "colour", 0, 4294967295.0);
            const auto route = number(a, "outputChannel", 0, 31);
            if (colour != std::floor(colour) || route != std::floor(route)) bad("color and output channel must be integers");
            pad.colour = static_cast<std::uint32_t>(colour); pad.outputChannel = static_cast<int>(route);
        }
    }
    if (version >= 3) s.workstation = readWorkstation(root, dir);
    else s.workstation.tempo = p.projectBpm;
    return s;
}
} // namespace samplify
