#include "audio/SeratoMetadata.h"
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
int checks = 0, failures = 0;
void check(bool okay, const char* what) {
    ++checks;
    if (!okay) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
void add(Bytes& bytes, std::string_view text) { bytes.insert(bytes.end(), text.begin(), text.end()); }
void be(Bytes& bytes, std::uint32_t value, int count = 4) {
    for (int i = count - 1; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void le(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void sync(Bytes& bytes, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>((value >> (7 * i)) & 0x7f));
}
std::string encoded(const Bytes& bytes) { return juce::Base64::toBase64(bytes.data(), bytes.size()).toStdString(); }
Bytes decode(std::string_view text) {
    juce::MemoryOutputStream output;
    if (!juce::Base64::convertFromBase64(output, juce::String::fromUTF8(text.data(), static_cast<int>(text.size()))))
        throw std::runtime_error("Invalid fixture Base64");
    const auto* first = static_cast<const std::uint8_t*>(output.getData());
    return { first, first + output.getDataSize() };
}

Bytes cue(int index, std::uint32_t milliseconds, std::uint32_t rgb, std::string_view name) {
    Bytes entry {0, static_cast<std::uint8_t>(index)};
    be(entry, milliseconds); entry.push_back(0); be(entry, rgb, 3); be(entry, 0, 2);
    add(entry, name); entry.push_back(0);
    Bytes result; add(result, std::string_view("CUE\0", 4)); be(result, static_cast<std::uint32_t>(entry.size()));
    result.insert(result.end(), entry.begin(), entry.end());
    return result;
}
Bytes modern(const std::vector<Bytes>& entries) {
    Bytes data {1,1};
    for (const auto& e : entries) data.insert(data.end(), e.begin(), e.end());
    data.push_back(0);
    Bytes out {1,1}; add(out, encoded(data)); return out;
}
Bytes object(std::string_view description, const Bytes& data, bool utf16 = false) {
    Bytes result {static_cast<std::uint8_t>(utf16 ? 1 : 0)};
    result.reserve(64 + description.size() * 2 + data.size());
    add(result, "application/octet-stream"); result.push_back(0);
    if (utf16) {
        result.insert(result.end(), {0xff,0xfe,0,0,0xff,0xfe});
        for (const auto c : description) { result.push_back(static_cast<std::uint8_t>(c)); result.push_back(0); }
        result.insert(result.end(), {0,0});
    } else { result.push_back(0); add(result, description); result.push_back(0); }
    result.insert(result.end(), data.begin(), data.end()); return result;
}
Bytes unsync(const Bytes& bytes) {
    Bytes result;
    for (auto byte : bytes) { result.push_back(byte); if (byte == 0xff) result.push_back(0); }
    return result;
}
Bytes frame(const Bytes& payload, int version = 4, std::uint8_t flags = 0) {
    Bytes result; add(result, version == 2 ? "GEO" : "GEOB");
    if (version == 2) be(result, static_cast<std::uint32_t>(payload.size()), 3);
    else {
        if (version == 4) sync(result, static_cast<std::uint32_t>(payload.size()));
        else be(result, static_cast<std::uint32_t>(payload.size()));
        result.push_back(0); result.push_back(flags);
    }
    result.insert(result.end(), payload.begin(), payload.end()); return result;
}
Bytes id3(const std::vector<Bytes>& frames, int version = 4, bool extended = false, bool globalUnsync = false, int padding = 0) {
    Bytes body;
    if (extended) {
        if (version == 4) { sync(body, 6); body.insert(body.end(), {1,0}); }
        else { be(body, 6); body.insert(body.end(), 6, 0); }
    }
    for (const auto& f : frames) body.insert(body.end(), f.begin(), f.end());
    body.insert(body.end(), static_cast<std::size_t>(padding), 0);
    if (globalUnsync) body = unsync(body);
    Bytes result; result.reserve(10 + body.size()); add(result, "ID3"); result.insert(result.end(), {static_cast<std::uint8_t>(version),0,
        static_cast<std::uint8_t>((extended ? 0x40 : 0) | (globalUnsync ? 0x80 : 0))});
    sync(result, static_cast<std::uint32_t>(body.size())); result.insert(result.end(), body.begin(), body.end()); return result;
}
Bytes container(const Bytes& tag, bool aiff, bool oddChunk = false) {
    Bytes body; add(body, aiff ? "AIFF" : "WAVE");
    if (oddChunk) {
        add(body, "JUNK"); if (aiff) be(body, 1); else le(body, 1); body.insert(body.end(), {1,0});
    }
    add(body, aiff ? "ID3 " : "id3 ");
    if (aiff) be(body, static_cast<std::uint32_t>(tag.size())); else le(body, static_cast<std::uint32_t>(tag.size()));
    body.insert(body.end(), tag.begin(), tag.end()); if (tag.size() & 1) body.push_back(0);
    Bytes result; add(result, aiff ? "FORM" : "RIFF");
    if (aiff) be(result, static_cast<std::uint32_t>(body.size())); else le(result, static_cast<std::uint32_t>(body.size()));
    result.insert(result.end(), body.begin(), body.end()); return result;
}
Bytes flac(const Bytes& tag) {
    const auto geob = object("Serato Markers2", tag);
    Bytes withoutEncoding(geob.begin() + 1, geob.end());
    std::string field = "SERATO_MARKERS_V2=" + encoded(withoutEncoding);
    Bytes comment; le(comment, 4); add(comment, "test"); le(comment, 1); le(comment, static_cast<std::uint32_t>(field.size())); add(comment, field);
    Bytes result; add(result, "fLaC"); result.insert(result.end(), {0,0,0,34}); result.insert(result.end(), 34, 0);
    result.push_back(0x84); be(result, static_cast<std::uint32_t>(comment.size()), 3);
    result.insert(result.end(), comment.begin(), comment.end()); return result;
}
void seratoInteger(Bytes& data, std::size_t at, std::uint32_t value) {
    const auto a = (value >> 16) & 0xff, b = (value >> 8) & 0xff, c = value & 0xff;
    data[at] = static_cast<std::uint8_t>(a >> 5);
    data[at + 1] = static_cast<std::uint8_t>(((b >> 6) | (a << 2)) & 0x7f);
    data[at + 2] = static_cast<std::uint8_t>(((c >> 7) | (b << 1)) & 0x7f);
    data[at + 3] = static_cast<std::uint8_t>(c & 0x7f);
}
Bytes emptyLegacy() {
    Bytes data {2,5}; be(data, 14);
    for (int i = 0; i < 14; ++i) {
        data.insert(data.end(), 10, 0x7f); data.push_back(0); data.insert(data.end(), 5, 0x7f);
        data.insert(data.end(), {0,0,0,0,static_cast<std::uint8_t>(i < 5 ? 0 : 3),0});
    }
    data.insert(data.end(), 4, 0); return data;
}

struct Files {
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("samplify-metadata", {}, false);
    Files() { if (directory.createDirectory().failed()) throw std::runtime_error("Cannot create test directory"); }
    ~Files() { directory.deleteRecursively(); }
    juce::File write(const char* name, const Bytes& bytes) const {
        const auto file = directory.getChildFile(name);
        if (!file.replaceWithData(bytes.data(), bytes.size())) throw std::runtime_error("Cannot write fixture");
        return file;
    }
};

void realFixtureTests(Files& files) {
    // Metadata-only fixtures generated by Serato and published by Jan Holthuis,
    // Holzhaus/serato-tags, CC BY-SA 4.0. No source music is copied.
    // https://creativecommons.org/licenses/by-sa/4.0/
    // https://github.com/Holzhaus/serato-tags/tree/main/data/id3/
    // hotcue-positions-00m00s0-03m38s4-01m00s0-00m00s1-00m01s0
    const auto older = decode("AgUAAAAOAAAAAAB/f39/fwB/f39/fwYwAAABAAAADSpYf39/f38Af39/f38GMhAAAQAAAANUZH9/f39/AH9/f39/AAABTAEAAAAAAGx/f39/fwB/f39/fwYzGAABAAAAAAd3f39/f38Af39/f38AAxgAAQB/f39/f39/f39/AH9/f39/AAAAAAMAf39/f39/f39/fwB/f39/fwAAAAADAH9/f39/f39/f38Af39/f38AAAAAAwB/f39/f39/f39/AH9/f39/AAAAAAMAf39/f39/f39/fwB/f39/fwAAAAADAH9/f39/f39/f38Af39/f38AAAAAAwB/f39/f39/f39/AH9/f39/AAAAAAMAf39/f39/f39/fwB/f39/fwAAAAADAH9/f39/f39/f38Af39/f38AAAAAAwAHf39/");
    const auto newer = decode("AQFBUUZEVDB4UFVnQUFBQUFFQVAvLy8wTlZSUUFBQUFBTkFBQUFBQUFBQU13QUFBQUFBRU5WUlFBQUFBQU5BQUVBQTFWWUFNeUkKQUFBQUFFTlZSUUFBQUFBTkFBSUFBT3BrQUFBQXpBQUFBRU5WUlFBQUFBQU5BQU1BQUFCc0FNek1BQUFBQUVOVlJRQUFBQUFOCkFBUUFBQVAzQUFETUFBQUFBRUpRVFV4UFEwc0FBQUFBQVFBQQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    constexpr double rate = 44100;
    constexpr std::size_t length = 44100u * 300u;
    const std::array<std::uint32_t,5> milliseconds {0,218456,60004,108,1015};
    const std::array<std::uint32_t,5> colors {0xffc02626,0xfff8821a,0xff173ba2,0xfffac313,0xff1fad26};
    for (int version : {2,3,4}) {
        const auto f = files.write("real-modern.mp3", id3({frame(object("Serato Markers2", newer), version)}, version));
        const auto cues = samplify::readSeratoCues(f, rate, length);
        check(cues.size() == 5, "published Serato Markers2 fixture imports all five real cues in ID3v2.2/3/4");
        bool correct = cues.size() == 5;
        for (std::size_t i = 0; correct && i < cues.size(); ++i)
            correct = cues[i].start == static_cast<std::size_t>(std::llround(milliseconds[i] * rate / 1000))
                && cues[i].colour == colors[i] && cues[i].end == length && !cues[i].hasEnd;
        check(correct, "published cue times, palette colors, indices and no-end semantics match");
    }
    const auto f = files.write("real-legacy.aiff", container(id3({frame(object("Serato Markers_", older))}), true));
    const auto cues = samplify::readSeratoCues(f, rate, length);
    check(cues.size() == 5 && cues[1].start == 9633910 && cues[4].start == 44762, "published legacy serato32 positions decode to frame precision");
}

void containerTests(Files& files) {
    const auto tag = modern({cue(0,1000,0x123456,"Intro"), cue(7,2000,0xcc0000,"Verse \xce\xb1")});
    const auto frames = id3({frame(object("Serato Markers2", tag))});
    for (const auto& [name, bytes] : std::vector<std::pair<const char*,Bytes>> {
            {"sparse.mp3",frames}, {"sparse.wav",container(frames,false,true)},
            {"sparse.aiff",container(frames,true,true)}, {"sparse.flac",flac(tag)}}) {
        const auto cues = samplify::readSeratoCues(files.write(name,bytes),48000,48000 * 10);
        check(cues.size() == 8 && cues[0].start == 48000 && cues[7].start == 96000,
              "MP3/WAV/AIFF/FLAC containers preserve sparse hotcue slots and sample-rate conversion");
        check(cues.size() == 8 && cues[3].empty() && cues[0].name == "Intro" && cues[7].name == "Verse \xce\xb1"
                  && cues[0].colour == 0xff123456 && cues[7].colour == 0xffc02626,
              "UTF-8 labels, custom RGB, mapped palette and empty slots survive container parsing");
    }
    for (int version : {3,4}) {
        auto bytes = id3({frame(object("Serato Markers2",tag),version)},version,true,false,3);
        check(samplify::readSeratoCues(files.write("extended.mp3",bytes),44100,441000).size()==8,
              "ID3 extended header and short trailing padding are parsed");
    }
    auto utf16 = object("Serato Markers2",tag,true);
    auto bytes = id3({frame(utf16,3)},3,false,true);
    check(samplify::readSeratoCues(files.write("unsync-v3.mp3",bytes),44100,441000).size()==8,
          "ID3v2.3 global unsynchronisation and UTF-16 description are supported");
    bytes = id3({frame(unsync(utf16),4,2)});
    check(samplify::readSeratoCues(files.write("unsync-v4.mp3",bytes),44100,441000).size()==8,
          "ID3v2.4 per-frame unsynchronisation is supported");
    bytes = frames;
    bytes[5] = 0x10;
    Bytes footerSize; sync(footerSize,static_cast<std::uint32_t>(bytes.size()));
    std::copy(footerSize.begin(),footerSize.end(),bytes.begin()+6);
    Bytes footer(bytes.begin(),bytes.begin()+10); footer[0]='3'; footer[1]='D'; footer[2]='I';
    bytes.insert(bytes.end(),footer.begin(),footer.end());
    check(samplify::readSeratoCues(files.write("footer.mp3",bytes),44100,441000).size()==8,
          "ID3v2.4 footer is verified and excluded from frame parsing");

    // This real Serato encoding quirk loses the last Base64 sextet of zero.
    auto chopped = modern({cue(0,1000,0x123456,"A")});
    while (chopped.back()=='=') chopped.pop_back();
    check((chopped.size()-2)%4==2, "fixture exercises the documented chopped Base64 case");
    chopped.pop_back();
    check(samplify::readSeratoCues(files.write("chopped.mp3",id3({frame(object("Serato Markers2",chopped))})),44100,441000).size()==1,
          "Serato's shortened Base64 final block is repaired");

    auto legacy = emptyLegacy();
    legacy[6] = 0; legacy[26] = 1; seratoInteger(legacy,7,1500); seratoInteger(legacy,22,0xcccc00);
    const auto newer = modern({cue(0,1000,0xcc0000,"Keep label"),cue(1,1000,0xcc0000,"Legacy cleared"),cue(6,3000,0x123456,"Extra")});
    const auto merged = samplify::readSeratoCues(files.write("precedence.mp3",id3({
        frame(object("Serato Markers_",legacy)),frame(object("Serato Markers2",newer))})),48000,480000);
    check(merged.size()==7 && merged[0].start==72000 && merged[0].colour==0xfffac313 && merged[0].name=="Keep label",
          "legacy positions/colors override newer metadata while preserving modern labels");
    check(merged.size()==7 && merged[1].empty() && merged[6].name=="Extra",
          "legacy unset first-five slots override newer cues while newer additional cues remain");

    // Seek over audio instead of allocating or reading the complete sample data.
    const auto sparse = files.directory.getChildFile("large-audio.wav");
    constexpr std::uint32_t audioBytes = 64u*1024u*1024u;
    Bytes head; add(head,"RIFF"); le(head,4+8+audioBytes+8+static_cast<std::uint32_t>(frames.size())+(frames.size()&1));
    add(head,"WAVEdata"); le(head,audioBytes);
    auto out=sparse.createOutputStream(); out->write(head.data(),head.size());
    out->setPosition(head.size()+audioBytes); Bytes chunk; add(chunk,"id3 "); le(chunk,static_cast<std::uint32_t>(frames.size()));
    out->write(chunk.data(),chunk.size()); out->write(frames.data(),frames.size()); if(frames.size()&1)out->writeByte(0); out.reset();
    check(samplify::readSeratoCues(sparse,44100,441000).size()==8,"large WAV audio chunk is skipped to discover following cue metadata");
}

void malformedTests(Files& files) {
    const auto good = modern({cue(0,1000,0xcc0000,"One")});
    check(samplify::readSeratoCues(files.directory.getChildFile("missing.mp3"),44100,441000).empty(),"missing file has no fake cues");
    check(samplify::readSeratoCues(files.write("unrelated.bin",Bytes(100,'x')),44100,441000).empty(),"unrecognized file has no fake cues");
    check(samplify::readSeratoCues(files.write("empty.mp3",id3({})),44100,441000).empty(),"valid tag without markers leaves Serato DJ mode unavailable");
    auto bad = id3({frame(object("Serato Markers2",good))}); bad.resize(bad.size()-2);
    check(samplify::readSeratoCues(files.write("truncated.mp3",bad),44100,441000).empty(),"truncated ID3 declared tag size is rejected");
    bad=id3({frame(object("Serato Markers2",good))}); bad[6]=0x80;
    check(samplify::readSeratoCues(files.write("non-syncsafe.mp3",bad),44100,441000).empty(),"invalid syncsafe length is rejected before allocation");
    bad=id3({frame(object("Serato Markers2",good))}); std::fill(bad.begin()+14,bad.begin()+18,0x7f);
    check(samplify::readSeratoCues(files.write("huge-frame.mp3",bad),44100,441000).empty(),"oversized GEOB frame cannot allocate beyond file bounds");
    Bytes wrong {1,1,'@','@'};
    check(samplify::readSeratoCues(files.write("invalid-b64.mp3",id3({frame(object("Serato Markers2",wrong))})),44100,441000).empty(),"malformed Base64 produces no cues");
    auto entry=cue(0,1000,0xcc0000,"One"); std::fill(entry.begin()+4,entry.begin()+8,0xff);
    check(samplify::readSeratoCues(files.write("invalid-entry.mp3",id3({frame(object("Serato Markers2",modern({entry})))})),44100,441000).empty(),"oversized marker entry is bounded");
    auto invalidUtf8=modern({cue(0,1000,0xcc0000,std::string(1,static_cast<char>(0xff)))});
    check(samplify::readSeratoCues(files.write("invalid-name.mp3",id3({frame(object("Serato Markers2",invalidUtf8))})),44100,441000).empty(),"invalid UTF-8 marker label is ignored safely");
    const auto bounds=modern({cue(31,1000,0x123456,"Last"),cue(32,1000,0x123456,"Outside"),cue(2,0xffffffff,0x123456,"Too late")});
    const auto high=samplify::readSeratoCues(files.write("bounds.mp3",id3({frame(object("Serato Markers2",bounds))})),44100,441000);
    check(high.size()==32 && !high[31].empty() && high[2].empty(),"hotcue indices and timestamps are constrained to supported slots/source bounds");
    const auto file=files.write("good.mp3",id3({frame(object("Serato Markers2",good))}));
    check(samplify::readSeratoCues(file,std::numeric_limits<double>::quiet_NaN(),441000).empty(),"non-finite sample rate is rejected");
    check(samplify::readSeratoCues(file,44100,0).empty(),"zero-length source has no usable cues");
    check(samplify::readSeratoCues(files.write("compressed.mp3",id3({frame(object("Serato Markers2",good),4,8)})),44100,441000).empty(),"unsupported compressed object is not misinterpreted as raw metadata");
    bad=flac(good); bad[54]=2;
    check(samplify::readSeratoCues(files.write("truncated-comments.flac",bad),44100,441000).empty(),
          "malformed FLAC comment list does not expose partially parsed cues");

    bool safe=true;
    std::uint32_t random=42;
    const auto pristine=id3({frame(object("Serato Markers2",good))});
    for(int iteration=0;iteration<128;++iteration) {
        bad=pristine;
        random=random*1664525u+1013904223u;
        if(iteration%4==0)bad.resize(random%bad.size());
        else for(int i=0;i<4;++i) {
            random=random*1664525u+1013904223u; const auto at=random%bad.size();
            random=random*1664525u+1013904223u; bad[at]=static_cast<std::uint8_t>(random>>24);
        }
        const auto cues=samplify::readSeratoCues(files.write("mutated.mp3",bad),44100,441000);
        safe=safe && cues.size()<=32 && std::all_of(cues.begin(),cues.end(),[](const auto& pad) {
            return pad.empty() || (pad.start<441000 && pad.end==441000 && !pad.hasEnd);
        });
    }
    check(safe,"mutated metadata remains bounded and never produces out-of-source cues");
}
}

int main() {
    try { Files files; realFixtureTests(files); containerTests(files); malformedTests(files); }
    catch(const std::exception& e) { ++failures; std::cerr << "Unexpected metadata test error: " << e.what() << '\n'; }
    std::cout << checks-failures << '/' << checks << " Serato metadata checks passed\n";
    return failures ? 1 : 0;
}
