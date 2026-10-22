#include "audio/SessionIO.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace samplify;
struct Checks {
    int count = 0, failures = 0;
    void check(bool condition, const char* text) {
        ++count;
        if (!condition) { ++failures; std::cerr << "FAIL: " << text << '\n'; }
    }
    template<class Fn> void rejects(Fn&& fn, const char* text) {
        bool rejected = false;
        try { fn(); } catch (const std::exception&) { rejected = true; }
        check(rejected, text);
    }
    template<class Fn> void section(const char* name, Fn&& fn) {
        const auto before = failures;
        try { fn(); } catch (const std::exception& error) {
            ++failures; std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
        std::cout << (before == failures ? "PASS: " : "FAIL: ") << name << '\n';
    }
};
struct TemporaryDirectory {
    juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("samplify-workstation-session", {}, false);
    TemporaryDirectory() { if (path.createDirectory().failed()) throw std::runtime_error("Cannot create test directory"); }
    ~TemporaryDirectory() { path.deleteRecursively(); }
};

bool same(const FxSlot& a, const FxSlot& b) {
    return a.type == b.type && a.bypass == b.bypass && a.mix == b.mix && a.parameters == b.parameters;
}
bool same(const HostedPluginSlot& a, const HostedPluginSlot& b) {
    return a.name == b.name && a.format == b.format && a.identifier == b.identifier
        && a.manufacturer == b.manufacturer && a.xmlDescription == b.xmlDescription
        && a.stateBase64 == b.stateBase64 && a.bypass == b.bypass;
}
template<class Range> bool sameRack(const Range& a, const Range& b) {
    return std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) { return same(x, y); });
}
bool same(const PatternNote& a, const PatternNote& b) {
    return a.id == b.id && a.pad == b.pad && a.beat == b.beat && a.duration == b.duration
        && a.velocity == b.velocity && a.semitones == b.semitones;
}

HostedPluginSlot pluginFixture() {
    juce::PluginDescription description;
    description.name = "Unavailable Fixture Insert";
    description.descriptiveName = description.name;
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/UnavailableFixture.vst3";
    description.manufacturerName = "Fixture Audio";
    description.version = "1.2.3";
    description.uniqueId = 0x123456;
    description.numInputChannels = description.numOutputChannels = 2;
    HostedPluginSlot slot;
    slot.name = description.name.toStdString();
    slot.format = description.pluginFormatName.toStdString();
    slot.identifier = description.fileOrIdentifier.toStdString();
    slot.manufacturer = description.manufacturerName.toStdString();
    slot.xmlDescription = description.createXml()->toString().toStdString();
    slot.stateBase64 = "AAECAwQF";
    slot.bypass = true;
    return slot;
}

Session fixture(const juce::File& directory) {
    Session s;
    s.source = directory.getChildFile("source.wav");
    s.stems[0] = directory.getChildFile("stems/vocals.wav");
    s.project.projectBpm = 137.5;
    s.project.sourceBpm = 99;
    s.project.mono = false;
    s.project.playbackMode = CuePlaybackMode::Trigger;
    s.project.pads[0].start = 101;
    s.project.pads[0].end = 9101;
    s.project.pads[0].speedRatio = 1.5f;
    s.project.pads[0].favorite = true;
    s.project.pads[0].name = "Persistent cue";
    auto& w = s.workstation;
    w.masterGainDb = -3.5f; w.crossfader = .27f; w.tempo = 137.5;
    w.recordQuantizeBeats = .125; w.launchQuantizeBeats = 2;
    w.arrangementLoop = true; w.loopStartBeat = 4; w.loopEndBeat = 28;
    w.selectedPattern = 5; w.selectedTrack = 6;
    for (std::size_t i = 0; i < w.tracks.size(); ++i) {
        auto& track = w.tracks[i];
        track.name = i == 0 ? "Drums Δ" : "Bus " + std::to_string(i + 1);
        track.gainDb = -2.f - static_cast<float>(i);
        track.pan = static_cast<float>(i) / 7.f * 2 - 1;
        track.mute = i % 2 != 0; track.solo = i == 4;
        track.sendA = static_cast<float>(i) / 8.f;
        track.sendB = 1.f - static_cast<float>(i) / 8.f;
        track.crossfadeAssignment = i % 3 == 0 ? CrossfadeAssignment::A
            : i % 3 == 1 ? CrossfadeAssignment::B : CrossfadeAssignment::None;
        for (std::size_t j = 0; j < track.effects.size(); ++j) {
            auto& fx = track.effects[j];
            fx.type = static_cast<FxType>(1 + (i + j) % 15);
            fx.mix = .2f + .1f * static_cast<float>(j); fx.bypass = j == 2;
            fx.parameters = {.1f, .2f, .3f, .4f, .5f, .6f};
        }
    }
    for (std::size_t i = 0; i < w.patterns.size(); ++i) {
        auto& pattern = w.patterns[i];
        pattern.name = "Phrase " + std::to_string(i + 1);
        pattern.lengthBeats = static_cast<double>((i + 1) * 4);
        pattern.notes = {{1, static_cast<int>(i), 0, .25, .6f, -7.f},
                         {2, 31 - static_cast<int>(i), pattern.lengthBeats - .5, .25, .8f, 5.f}};
    }
    w.audioAssets = {{91, "Frozen Δ", directory.getChildFile("assets/frozen-take.wav").getFullPathName().toStdString(), 99, 8},
                     {92, "External take", directory.getParentDirectory().getChildFile("external-take.wav").getFullPathName().toStdString(), 128, 16}};
    w.arrangement = {{301, 3, ClipKind::Pattern, w.patterns[4].id, 2, 4, 1, false, .7f},
                     {302, 6, ClipKind::Audio, 91, 10, 6, .5, true, 1.4f}};
    w.launchSlots[3 * workstationSceneCount + 2] = {ClipKind::Pattern, w.patterns[1].id, true};
    w.launchSlots[6 * workstationSceneCount + 1] = {ClipKind::Audio, 91, false};
    for (std::size_t i = 0; i < workstationFxCount; ++i) {
        w.masterEffects[i] = w.tracks[0].effects[i];
        w.sendAEffects[i] = w.tracks[1].effects[i];
        w.sendBEffects[i] = w.tracks[2].effects[i];
    }
    w.tracks[2].plugins[1] = pluginFixture();
    w.masterPlugins[0] = pluginFixture();
    validateWorkstation(w);
    return s;
}

void roundTrip(Checks& c, const juce::File& directory) {
    const auto saved = fixture(directory);
    const auto file = directory.getChildFile("project.samplify");
    saveSession(file, saved);
    const auto loaded = loadSession(file);
    const auto& a = saved.workstation;
    const auto& b = loaded.workstation;
    c.check(b.tempo == a.tempo && b.masterGainDb == a.masterGainDb && b.crossfader == a.crossfader
        && b.recordQuantizeBeats == a.recordQuantizeBeats && b.launchQuantizeBeats == a.launchQuantizeBeats
        && b.arrangementLoop && b.loopStartBeat == 4 && b.loopEndBeat == 28
        && b.selectedPattern == 5 && b.selectedTrack == 6, "Session v3 restores workstation transport, loop, selection and global mixer");
    bool tracks = true;
    for (std::size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& x = a.tracks[i]; const auto& y = b.tracks[i];
        tracks &= x.name == y.name && x.gainDb == y.gainDb && x.pan == y.pan && x.mute == y.mute
            && x.solo == y.solo && x.sendA == y.sendA && x.sendB == y.sendB
            && x.crossfadeAssignment == y.crossfadeAssignment && sameRack(x.effects, y.effects)
            && sameRack(x.plugins, y.plugins);
    }
    c.check(tracks, "Session v3 restores all eight track mixer controls, effect racks and hosted insert states");
    bool patterns = true;
    for (std::size_t i = 0; i < a.patterns.size(); ++i) {
        const auto& x = a.patterns[i]; const auto& y = b.patterns[i];
        patterns &= x.id == y.id && x.name == y.name && x.lengthBeats == y.lengthBeats
            && x.notes.size() == y.notes.size()
            && std::equal(x.notes.begin(), x.notes.end(), y.notes.begin(), [](const auto& n, const auto& m) { return same(n, m); });
    }
    c.check(patterns, "Session v3 restores sixteen piano-roll patterns and every note value");
    bool clips = a.arrangement.size() == b.arrangement.size();
    for (std::size_t i = 0; clips && i < a.arrangement.size(); ++i) {
        const auto& x = a.arrangement[i]; const auto& y = b.arrangement[i];
        clips &= x.id == y.id && x.track == y.track && x.kind == y.kind && x.contentId == y.contentId
            && x.startBeat == y.startBeat && x.lengthBeats == y.lengthBeats && x.offsetBeats == y.offsetBeats
            && x.loop == y.loop && x.gain == y.gain;
    }
    c.check(clips, "Session v3 restores arrangement placement, content references, offsets, loops and gain");
    bool launches = true;
    for (std::size_t i = 0; i < a.launchSlots.size(); ++i)
        launches &= a.launchSlots[i].kind == b.launchSlots[i].kind && a.launchSlots[i].contentId == b.launchSlots[i].contentId
            && a.launchSlots[i].loop == b.launchSlots[i].loop;
    c.check(launches, "Session v3 restores all clip-launch scene assignments");
    bool assets = a.audioAssets.size() == b.audioAssets.size();
    for (std::size_t i = 0; assets && i < a.audioAssets.size(); ++i) {
        const auto& x = a.audioAssets[i]; const auto& y = b.audioAssets[i];
        assets &= x.id == y.id && x.name == y.name && x.filePath == y.filePath
            && x.sourceBpm == y.sourceBpm && x.lengthBeats == y.lengthBeats;
    }
    c.check(assets, "Session v3 resolves both relative frozen audio paths and external absolute paths");
    c.check(sameRack(a.masterEffects, b.masterEffects) && sameRack(a.sendAEffects, b.sendAEffects)
        && sameRack(a.sendBEffects, b.sendBEffects) && sameRack(a.masterPlugins, b.masterPlugins),
        "Session v3 restores send/master effect and hosted plugin racks");
    c.check(loaded.source == saved.source && loaded.stems == saved.stems
        && loaded.project.pads[0].name == saved.project.pads[0].name && loaded.project.pads[0].favorite,
        "Session v3 keeps sampler source, stems and cue metadata alongside workstation state");
    const auto json = juce::JSON::parse(file.loadFileAsString());
    c.check(static_cast<int>(json.getProperty("version", 0)) == 3 && json.getProperty("workstation", {}).isObject(),
        "Saved document identifies schema v3 and includes a workstation object");
    c.check(!file.loadFileAsString().contains(directory.getChildFile("assets/frozen-take.wav").getFullPathName())
        && file.loadFileAsString().contains("frozen-take.wav"), "Contained audio assets are persisted with portable relative paths");
    const auto again = directory.getChildFile("roundtrip.samplify");
    saveSession(again, loaded);
    c.check(again.loadFileAsString() == file.loadFileAsString(), "Complete v3 document roundtrip is deterministic without silently dropping fields");
}

void malformed(Checks& c, const juce::File& directory) {
    const auto baseline = directory.getChildFile("baseline.samplify");
    saveSession(baseline, fixture(directory));
    const auto text = baseline.loadFileAsString();
    const auto bad = directory.getChildFile("malformed.samplify");
    const auto reject = [&](const char* description, auto&& mutate) {
        auto json = juce::JSON::parse(text);
        mutate(json);
        bad.replaceWithText(juce::JSON::toString(json));
        c.rejects([&] { (void)loadSession(bad); }, description);
    };
    reject("V3 document missing workstation state is rejected", [](auto& json) { json.getDynamicObject()->removeProperty("workstation"); });
    reject("Track pan outside normalized bounds is rejected", [](auto& json) { json["workstation"]["tracks"][0].getDynamicObject()->setProperty("pan", 2.0); });
    reject("Pattern note referencing pad32 is rejected", [](auto& json) { json["workstation"]["patterns"][0]["notes"][0].getDynamicObject()->setProperty("pad", 32); });
    reject("Zero note duration is rejected", [](auto& json) { json["workstation"]["patterns"][0]["notes"][0].getDynamicObject()->setProperty("duration", 0); });
    reject("Arrangement reference to missing content is rejected", [](auto& json) { json["workstation"]["arrangement"][0].getDynamicObject()->setProperty("contentId", 999999); });
    reject("Unknown effect type is rejected", [](auto& json) { json["workstation"]["tracks"][0]["effects"][0].getDynamicObject()->setProperty("type", 999); });
    reject("Oversized normalized FX parameter is rejected", [](auto& json) { json["workstation"]["tracks"][0]["effects"][0]["parameters"].getArray()->set(0, 4.0); });
    reject("Malformed hosted plugin Base64 state is rejected", [](auto& json) { json["workstation"]["tracks"][2]["plugins"][1].getDynamicObject()->setProperty("stateBase64", "AB=="); });
    reject("Plugin state exceeding decoded four-MiB limit is rejected", [](auto& json) {
        const std::string oversized(4 * ((workstationMaxPluginStateBytes + 2) / 3) + 4, 'A');
        json["workstation"]["tracks"][2]["plugins"][1].getDynamicObject()->setProperty("stateBase64", juce::String(oversized));
    });
    reject("Malformed plugin descriptor XML is rejected", [](auto& json) {
        json["workstation"]["masterPlugins"][0].getDynamicObject()->setProperty("xmlDescription", "<PLUGIN><broken");
    });
    reject("Pattern note count exceeding persisted bound is rejected", [](auto& json) {
        json["workstation"]["patterns"][0]["notes"].getArray()->insertMultiple(0, juce::var(), static_cast<int>(workstationMaxNotes + 1));
    });
    reject("Duplicate pattern IDs are rejected", [](auto& json) { json["workstation"]["patterns"][1].getDynamicObject()->setProperty("id", 1); });
    reject("Reversed arrangement loop is rejected", [](auto& json) { json["workstation"].getDynamicObject()->setProperty("loopEndBeat", 1); });
    reject("Fractional selected track is rejected", [](auto& json) { json["workstation"].getDynamicObject()->setProperty("selectedTrack", 2.5); });
    reject("Incomplete track array is rejected", [](auto& json) { json["workstation"]["tracks"].getArray()->removeLast(); });

    auto invalid = fixture(directory);
    invalid.workstation.arrangement[0].contentId = 999999;
    c.rejects([&] { saveSession(baseline, invalid); }, "Saving invalid workstation cross-references rejects before replacing project");
    c.check(baseline.loadFileAsString() == text, "Rejected workstation save leaves previous complete project intact");
}

void migrations(Checks& c, const juce::File& directory) {
    const auto saved = fixture(directory);
    const auto file = directory.getChildFile("new.samplify");
    saveSession(file, saved);
    const auto current = file.loadFileAsString();
    const auto legacyFile = directory.getChildFile("legacy.samplify");
    for (const int version : {1, 2}) {
        auto json = juce::JSON::parse(current);
        json.getDynamicObject()->setProperty("version", version);
        json.getDynamicObject()->removeProperty("workstation");
        if (version == 1) {
            auto project = json["project"];
            for (const auto* field : {"playbackMode", "velocityEnabled", "pitchTimeEnabled", "keyboardMode", "globalPitchSemitones",
                                     "globalDetuneCents", "glideMs", "glideMode", "stemMute", "tempoMarkers", "beatgridEnabled"})
                project.getDynamicObject()->removeProperty(field);
            for (auto& pad : *project["pads"].getArray()) {
                auto* object = pad.getDynamicObject();
                object->setProperty("timeRatio", 2.0); object->setProperty("releaseMs", 150);
                for (const auto* field : {"speedRatio", "hasEnd", "favorite", "name", "colour", "outputChannel"}) object->removeProperty(field);
            }
        }
        legacyFile.replaceWithText(juce::JSON::toString(json));
        const auto loaded = loadSession(legacyFile);
        c.check(loaded.source == saved.source && loaded.project.projectBpm == saved.project.projectBpm
            && loaded.project.pads[0].start == 101 && loaded.project.pads[0].end == 9101,
            "V1/V2 migration preserves existing sampler source, tempo and cue boundaries");
        c.check(loaded.workstation.tracks.size() == 8 && loaded.workstation.patterns.size() == 16
            && loaded.workstation.patterns[0].notes.empty() && loaded.workstation.arrangement.empty()
            && loaded.workstation.audioAssets.empty(), "V1/V2 migration creates an empty usable workstation instead of fabricating musical content");
        c.check(loaded.workstation.tempo == saved.project.projectBpm,
            "Legacy session migration initializes workstation tempo from existing sampler tempo");
        if (version == 1) c.check(std::abs(loaded.project.pads[0].speedRatio - .5f) < 1e-6f,
            "V1 duration multiplier still migrates to reciprocal cue speed when adding workstation schema");
        else c.check(loaded.project.pads[0].name == "Persistent cue" && loaded.project.pads[0].favorite,
            "V2 migration keeps cue names and favorites");
        const auto upgraded = directory.getChildFile("upgraded.samplify");
        saveSession(upgraded, loaded);
        c.check(static_cast<int>(juce::JSON::parse(upgraded.loadFileAsString())["version"]) == 3,
            "Legacy project resaves as complete schema v3");
    }
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Checks checks;
    try {
        TemporaryDirectory directory;
        checks.section("V3 workstation roundtrip", [&] { roundTrip(checks, directory.path); });
        checks.section("Malformed workstation persistence", [&] { malformed(checks, directory.path); });
        checks.section("V1/V2 workstation migration", [&] { migrations(checks, directory.path); });
    } catch (const std::exception& error) { std::cerr << "Test setup failed: " << error.what() << '\n'; return 1; }
    std::cout << checks.count << " checks, " << checks.failures << " failures\n";
    return checks.failures ? 1 : 0;
}
