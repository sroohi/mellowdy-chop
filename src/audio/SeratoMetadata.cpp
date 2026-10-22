#include "SeratoMetadata.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

// The public binary format and precedence are documented by the Mixxx readers:
// https://github.com/mixxxdj/mixxx/blob/main/src/track/serato/markers2.cpp
// https://github.com/mixxxdj/mixxx/blob/main/src/track/serato/markers.cpp
// https://github.com/mixxxdj/mixxx/blob/main/src/track/serato/tags.cpp
// Format/container reference: https://github.com/Holzhaus/serato-tags
// This independent reader has no Qt/TagLib dependency and never modifies tags.

namespace samplify {
namespace {
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
constexpr std::size_t maximumMetadataBytes = 16u * 1024u * 1024u;
constexpr std::size_t maximumObjectBytes = 8u * 1024u * 1024u;
constexpr int maximumEntries = 4096;

struct Cue {
    std::uint32_t milliseconds = 0;
    std::uint32_t rgb = 0;
    std::string name;
};
using Cues = std::array<std::optional<Cue>, 32>;
struct Tags {
    Cues modern;
    std::optional<std::array<std::optional<Cue>, 5>> legacy;
};

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
std::uint32_t le32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16)
        | (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
}
std::optional<std::uint32_t> syncsafe(const std::uint8_t* p) {
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return {};
    return (static_cast<std::uint32_t>(p[0]) << 21) | (static_cast<std::uint32_t>(p[1]) << 14)
        | (static_cast<std::uint32_t>(p[2]) << 7) | p[3];
}
bool literal(View data, std::size_t at, std::string_view wanted) {
    return at <= data.size() && wanted.size() <= data.size() - at
        && std::equal(wanted.begin(), wanted.end(), data.begin() + static_cast<std::ptrdiff_t>(at));
}
Bytes read(juce::InputStream& stream, std::uint64_t offset, std::size_t count,
           std::size_t budget = maximumMetadataBytes) {
    if (count > budget || offset > static_cast<std::uint64_t>(std::numeric_limits<juce::int64>::max())) return {};
    if (!stream.setPosition(static_cast<juce::int64>(offset))) return {};
    Bytes result(count);
    if (stream.read(result.data(), static_cast<int>(count)) != static_cast<int>(count)) return {};
    return result;
}
Bytes unsynchronise(View in) {
    Bytes out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        out.push_back(in[i]);
        if (in[i] == 0xff && i + 1 < in.size() && in[i + 1] == 0) ++i;
    }
    return out;
}

std::optional<Bytes> base64(View in) {
    std::string encoded;
    encoded.reserve(in.size());
    bool padded = false, nul = false;
    for (auto c : in) {
        if (c == 0) { nul = true; continue; }
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (nul) return {};
        if (c == '=') { padded = true; continue; }
        if (padded) return {};
        const bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '+' || c == '/';
        if (!valid) return {};
        encoded.push_back(static_cast<char>(c));
    }
    // Serato occasionally removes one extra final Base64 character; the
    // documented repair restores a zero-valued final sextet before decoding.
    if (encoded.size() % 4 == 1) encoded.push_back('A');
    Bytes decoded;
    decoded.reserve(encoded.size() * 3 / 4);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const auto c : encoded) {
        const int value = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26
            : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : 63;
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    return decoded;
}

std::optional<Cues> markers2(View outer) {
    if (outer.size() < 3 || outer[0] != 1 || outer[1] != 1) return {};
    const auto decoded = base64(outer.subspan(2));
    if (!decoded || decoded->size() < 3 || (*decoded)[0] != 1 || (*decoded)[1] != 1) return {};
    const View data(*decoded);
    Cues cues;
    std::size_t at = 2;
    bool terminated = false;
    for (int entries = 0; entries < maximumEntries && at < data.size(); ++entries) {
        const auto nameEnd = std::find(data.begin() + static_cast<std::ptrdiff_t>(at), data.end(), 0);
        if (nameEnd == data.end()) return {};
        const auto nameLength = static_cast<std::size_t>(nameEnd - data.begin()) - at;
        if (nameLength == 0) {
            terminated = true;
            if (!std::all_of(nameEnd, data.end(), [](auto b) { return b == 0; })) return {};
            break;
        }
        if (nameLength > 32) return {};
        const std::string type(reinterpret_cast<const char*>(data.data() + at), nameLength);
        at += nameLength + 1;
        if (data.size() - at < 4) return {};
        const auto length = static_cast<std::size_t>(be32(data.data() + at));
        at += 4;
        if (length > data.size() - at) return {};
        const auto entry = data.subspan(at, length);
        at += length;
        // Loops/Flips/BPMLOCK/COLOR are separate DJ metadata, not hotcue pads.
        if (type != "CUE") continue;
        if (entry.size() < 13 || entry[0] != 0 || entry[6] != 0 || entry[10] != 0 || entry[11] != 0)
            return {};
        const auto label = entry.subspan(12);
        const auto end = std::find(label.begin(), label.end(), 0);
        if (end == label.end() || end + 1 != label.end() || label.size() > 1025) return {};
        const auto lengthUtf8 = static_cast<int>(label.size() - 1);
        if (!juce::CharPointer_UTF8::isValidString(reinterpret_cast<const char*>(label.data()), lengthUtf8)) return {};
        const auto index = static_cast<std::size_t>(entry[1]);
        if (index >= cues.size()) continue;
        Cue cue;
        cue.milliseconds = be32(entry.data() + 2);
        cue.rgb = (static_cast<std::uint32_t>(entry[7]) << 16) | (static_cast<std::uint32_t>(entry[8]) << 8) | entry[9];
        cue.name.assign(reinterpret_cast<const char*>(label.data()), static_cast<std::size_t>(lengthUtf8));
        // A zero-position black hotcue is the known empty-slot representation.
        if (cue.milliseconds == 0 && cue.rgb == 0) continue;
        cues[index] = std::move(cue);
    }
    if (!terminated) return {};
    return cues;
}

std::uint32_t serato24(const std::uint8_t* p) {
    const auto c = (p[3] & 0x7f) | ((p[2] & 1) << 7);
    const auto b = ((p[2] & 0x7f) >> 1) | ((p[1] & 3) << 6);
    const auto a = ((p[1] & 0x7f) >> 2) | ((p[0] & 7) << 5);
    return (static_cast<std::uint32_t>(a) << 16) | (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(c);
}
std::optional<std::array<std::optional<Cue>, 5>> legacy(View data) {
    if (data.size() != 6 + 14 * 22 + 4 || data[0] != 2 || data[1] != 5 || be32(data.data() + 2) != 14) return {};
    std::array<std::optional<Cue>, 5> result;
    for (std::size_t i = 0; i < 14; ++i) {
        const auto e = data.subspan(6 + i * 22, 22);
        if (!literal(e, 10, std::string_view("\0\x7f\x7f\x7f\x7f\x7f", 6))) return {};
        if ((i < 5 && e[20] != 0 && e[20] != 1) || (i >= 5 && e[20] != 3)) return {};
        if (e[0] != 0 && e[0] != 0x7f) return {};
        if (e[5] != 0 && e[5] != 0x7f) return {};
        if (e[0] == 0x7f && be32(e.data() + 1) != 0x7f7f7f7f) return {};
        if (e[5] == 0x7f && be32(e.data() + 6) != 0x7f7f7f7f) return {};
        if (i < 5 && e[21] != 0) return {};
        if (i < 5 && e[0] == 0) {
            Cue cue;
            cue.milliseconds = serato24(e.data() + 1);
            cue.rgb = serato24(e.data() + 16);
            if (cue.milliseconds != 0 || cue.rgb != 0) result[i] = std::move(cue);
        }
    }
    return result;
}

// GEOB MIME is always ASCII, but filename/description use the ID3 text encoding.
// Only ASCII descriptions are relevant; Unicode filenames are skipped safely.
std::optional<std::string> zeroText(View data, std::size_t& at, std::uint8_t encoding, bool& littleEndian) {
    if (at >= data.size()) return {};
    std::string out;
    if (encoding == 0 || encoding == 3) {
        const auto end = std::find(data.begin() + static_cast<std::ptrdiff_t>(at), data.end(), 0);
        if (end == data.end()) return {};
        const auto length = static_cast<std::size_t>(end - data.begin()) - at;
        if (length > 4096) return {};
        out.assign(reinterpret_cast<const char*>(data.data() + at), length);
        at += length + 1;
        return out;
    }
    if (encoding != 1 && encoding != 2) return {};
    if (encoding == 2) littleEndian = false;
    if (data.size() - at >= 2) {
        if (data[at] == 0xff && data[at + 1] == 0xfe) { littleEndian = true; at += 2; }
        else if (data[at] == 0xfe && data[at + 1] == 0xff) { littleEndian = false; at += 2; }
    }
    while (data.size() - at >= 2 && out.size() <= 4096) {
        const auto first = data[at], second = data[at + 1]; at += 2;
        const auto value = littleEndian ? static_cast<unsigned>(first) | (static_cast<unsigned>(second) << 8)
            : (static_cast<unsigned>(first) << 8) | second;
        if (value == 0) return out;
        out.push_back(value < 128 ? static_cast<char>(value) : '?');
    }
    return {};
}
void object(View bytes, Tags& tags) {
    if (bytes.empty()) return;
    const auto encoding = bytes[0];
    std::size_t at = 1;
    bool little = true;
    const auto mime = zeroText(bytes, at, 0, little);
    if (!mime || *mime != "application/octet-stream") return;
    if (!zeroText(bytes, at, encoding, little)) return;
    const auto description = zeroText(bytes, at, encoding, little);
    if (!description) return;
    if (*description == "Serato Markers2") {
        if (auto cues = markers2(bytes.subspan(at))) tags.modern = std::move(*cues);
    } else if (*description == "Serato Markers_") {
        if (auto cues = legacy(bytes.subspan(at))) tags.legacy = std::move(*cues);
    }
}

bool id3Body(juce::InputStream& input, std::uint64_t start, std::uint64_t end,
             int version, std::uint8_t flags, Tags& tags) {
    auto at = start;
    if ((flags & 0x40) != 0) {
        if (version == 2) return false; // ID3v2.2 compressed tag.
        const auto sizeBytes = read(input, at, 4);
        if (sizeBytes.size() != 4) return false;
        const auto size = version == 4 ? syncsafe(sizeBytes.data()) : std::optional<std::uint32_t>(be32(sizeBytes.data()));
        if (!size || *size < 6u) return false;
        const std::uint64_t skip = *size + (version == 4 ? 0u : 4u);
        if (skip > end - at) return false;
        at += skip;
    }
    const auto headerSize = version == 2 ? 6u : 10u;
    for (int entry = 0; entry < maximumEntries && at <= end && end - at >= headerSize; ++entry) {
        const auto header = read(input, at, headerSize);
        if (header.size() != headerSize) return false;
        if (header[0] == 0) return true; // ID3 padding.
        const auto idLength = version == 2 ? 3u : 4u;
        if (!std::all_of(header.begin(), header.begin() + idLength,
                [](auto c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); })) return false;
        const auto size = version == 2 ? std::optional<std::uint32_t>((static_cast<std::uint32_t>(header[3]) << 16)
                | (static_cast<std::uint32_t>(header[4]) << 8) | header[5])
            : version == 4 ? syncsafe(header.data() + 4) : std::optional<std::uint32_t>(be32(header.data() + 4));
        if (!size) return false;
        at += headerSize;
        if (*size > end - at) return false;
        const bool geob = literal(header, 0, version == 2 ? "GEO" : "GEOB");
        const auto formatFlags = version == 2 ? std::uint8_t(0) : header[9];
        const bool compressedOrEncrypted = version == 4 ? (formatFlags & 0x0c) != 0 : (formatFlags & 0xc0) != 0;
        if (geob && !compressedOrEncrypted && *size <= maximumObjectBytes) {
            auto payload = read(input, at, *size, maximumObjectBytes);
            if (payload.size() != *size) return false;
            if (version == 4 && ((formatFlags & 2) || (flags & 0x80))) payload = unsynchronise(payload);
            std::size_t prefix = (version == 4 ? (formatFlags & 0x40) : (formatFlags & 0x20)) ? 1u : 0u;
            if (version == 4 && (formatFlags & 1)) prefix += 4;
            if (prefix <= payload.size()) object(View(payload).subspan(prefix), tags);
        }
        at += *size;
    }
    if (at == end) return true;
    if (at < end && end - at < headerSize) {
        const auto padding = read(input, at, static_cast<std::size_t>(end - at));
        return padding.size() == end - at && std::all_of(padding.begin(), padding.end(), [](auto c) { return c == 0; });
    }
    return false;
}

void id3(juce::InputStream& input, std::uint64_t start, std::uint64_t containerEnd, Tags& tags) {
    if (start > containerEnd || containerEnd - start < 10) return;
    const auto header = read(input, start, 10);
    if (header.size() != 10 || !literal(header, 0, "ID3") || header[3] < 2 || header[3] > 4) return;
    const auto size = syncsafe(header.data() + 6);
    if (!size || *size > containerEnd - start - 10) return;
    auto bodyEnd = start + 10 + *size;
    if (header[3] == 4 && (header[5] & 0x10)) {
        if (*size < 10) return;
        const auto footer = read(input, bodyEnd - 10, 10);
        if (footer.size() != 10 || !literal(footer, 0, "3DI")
            || !std::equal(footer.begin() + 3, footer.end(), header.begin() + 3)) return;
        bodyEnd -= 10;
    }
    auto candidate = tags;
    bool valid = false;
    if (header[3] < 4 && (header[5] & 0x80)) {
        const auto body = read(input, start + 10, *size);
        if (body.size() != *size) return;
        const auto decoded = unsynchronise(body);
        juce::MemoryInputStream memory(decoded.data(), decoded.size(), false);
        valid = id3Body(memory, 0, decoded.size(), header[3], static_cast<std::uint8_t>(header[5] & ~0x80u), candidate);
    } else valid = id3Body(input, start + 10, bodyEnd, header[3], header[5], candidate);
    if (valid) tags = std::move(candidate);
}

void chunked(juce::InputStream& input, View header, std::uint64_t fileEnd, Tags& tags) {
    const bool aiff = literal(header, 0, "FORM") && (literal(header, 8, "AIFF") || literal(header, 8, "AIFC"));
    const bool wave = literal(header, 0, "RIFF") && literal(header, 8, "WAVE");
    if (!aiff && !wave) return;
    const std::uint64_t end = 8u + (aiff ? be32(header.data() + 4) : le32(header.data() + 4));
    if (end < 12 || end > fileEnd) return;
    std::uint64_t at = 12;
    for (int chunk = 0; chunk < maximumEntries && at <= end && end - at >= 8; ++chunk) {
        const auto h = read(input, at, 8);
        if (h.size() != 8) return;
        const auto size = aiff ? be32(h.data() + 4) : le32(h.data() + 4);
        at += 8;
        if (size > end - at) return;
        if (literal(h, 0, "ID3 ") || literal(h, 0, "id3 ")) id3(input, at, at + size, tags);
        at += static_cast<std::uint64_t>(size) + (size & 1);
    }
}

void vorbis(View comment, Tags& tags) {
    if (comment.size() < 8) return;
    const auto vendorSize = static_cast<std::size_t>(le32(comment.data()));
    if (vendorSize > comment.size() - 8) return;
    std::size_t at = 4 + vendorSize;
    const auto entries = le32(comment.data() + at); at += 4;
    if (entries > maximumEntries) return;
    auto candidate = tags;
    for (std::uint32_t i = 0; i < entries; ++i) {
        if (comment.size() - at < 4) return;
        const auto size = static_cast<std::size_t>(le32(comment.data() + at)); at += 4;
        if (size > comment.size() - at) return;
        const auto field = comment.subspan(at, size); at += size;
        const auto equal = std::find(field.begin(), field.end(), '=');
        if (equal == field.end()) continue;
        const auto keySize = static_cast<std::size_t>(equal - field.begin());
        if (keySize > 64) continue;
        constexpr std::string_view name = "SERATO_MARKERS_V2";
        if (keySize != name.size() || !std::equal(field.begin(), equal, name.begin(), [](auto byte, char wanted) {
                const auto upper = byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte;
                return upper == wanted;
            })) continue;
        const auto decoded = base64(field.subspan(keySize + 1));
        if (!decoded || decoded->size() > maximumObjectBytes) continue;
        Bytes geob(1, 0); // FLAC stores the GEOB object without the encoding byte.
        geob.insert(geob.end(), decoded->begin(), decoded->end());
        object(geob, candidate);
    }
    if (at == comment.size()) tags = std::move(candidate);
}
void flac(juce::InputStream& input, std::uint64_t fileEnd, Tags& tags) {
    std::uint64_t at = 4;
    for (int block = 0; block < maximumEntries && at <= fileEnd && fileEnd - at >= 4; ++block) {
        const auto h = read(input, at, 4);
        if (h.size() != 4) return;
        const auto size = (static_cast<std::uint32_t>(h[1]) << 16) | (static_cast<std::uint32_t>(h[2]) << 8) | h[3];
        at += 4;
        if (size > fileEnd - at) return;
        if ((h[0] & 0x7f) == 4) {
            const auto blockData = read(input, at, size);
            if (blockData.size() != size) return;
            vorbis(blockData, tags);
        }
        at += size;
        if (h[0] & 0x80) return;
    }
}

std::uint32_t displayedColour(std::uint32_t rgb) {
    // Serato DJ Pro's 18 saved-to-display hotcue palette mappings; arbitrary
    // custom RGB values remain unchanged. The alpha component is always opaque.
    constexpr std::array<std::uint32_t, 18> saved {0xcc0000,0xcc4400,0xcc8800,0xcccc00,0x88cc00,0x44cc00,
        0x00cc00,0x00cc44,0x00cc88,0x00cccc,0x0088cc,0x0044cc,0x0000cc,0x4400cc,0x8800cc,0xcc00cc,0xcc0088,0xcc0044};
    constexpr std::array<std::uint32_t, 18> shown {0xc02626,0xdb4e27,0xf8821a,0xfac313,0x4eb648,0x006838,
        0x1fad26,0x8dc63f,0x2b3673,0x1dbebd,0x0f88ca,0x16308b,0x173ba2,0x5c3f97,0x6823b6,0xce359e,0xdc1d49,0xc71136};
    const auto found = std::find(saved.begin(), saved.end(), rgb);
    return 0xff000000u | (found == saved.end() ? rgb : shown[static_cast<std::size_t>(found - saved.begin())]);
}
} // namespace

std::vector<Pad> readSeratoCues(const juce::File& file, double sampleRate, std::size_t totalFrames) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || totalFrames == 0) return {};
    try {
        juce::FileInputStream stream(file);
        if (!stream.openedOk() || stream.getTotalLength() < 10) return {};
        const auto fileEnd = static_cast<std::uint64_t>(stream.getTotalLength());
        const auto header = read(stream, 0, static_cast<std::size_t>(std::min<std::uint64_t>(12, fileEnd)));
        Tags tags;
        if (literal(header, 0, "ID3")) id3(stream, 0, fileEnd, tags);
        else if (literal(header, 0, "fLaC")) flac(stream, fileEnd, tags);
        else chunked(stream, header, fileEnd, tags);
        if (tags.legacy) {
            for (std::size_t i = 0; i < tags.legacy->size(); ++i) {
                const auto& older = (*tags.legacy)[i];
                if (!older) tags.modern[i].reset();
                else {
                    const auto name = tags.modern[i] ? tags.modern[i]->name : std::string();
                    tags.modern[i] = *older;
                    tags.modern[i]->name = name;
                }
            }
        }
        std::vector<Pad> result;
        for (std::size_t i = 0; i < tags.modern.size(); ++i) {
            if (!tags.modern[i]) continue;
            const auto& cue = *tags.modern[i];
            const auto exact = static_cast<long double>(cue.milliseconds) * sampleRate / 1000;
            if (!std::isfinite(exact) || exact < 0 || exact >= static_cast<long double>(totalFrames)) continue;
            const auto frame = static_cast<std::size_t>(std::floor(exact + .5L));
            if (frame >= totalFrames) continue;
            if (result.size() <= i) result.resize(i + 1);
            auto& pad = result[i];
            pad.start = frame; pad.end = totalFrames; pad.hasEnd = false;
            pad.name = cue.name; pad.colour = displayedColour(cue.rgb);
        }
        return result;
    } catch (const std::exception&) { return {}; }
}

} // namespace samplify
