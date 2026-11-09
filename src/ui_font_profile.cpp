#include "ui_font_profile.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace forge::ui_font_detail {
namespace {
using Bytes = std::span<const std::byte>;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::uint32_t word(Bytes bytes, std::size_t at, unsigned size) {
    require(at <= bytes.size() && size <= bytes.size() - at, "Truncated UI font field");
    std::uint32_t result = 0;
    for (unsigned i = 0; i < size; ++i)
        result = (result << 8) | std::to_integer<unsigned>(bytes[at + i]);
    return result;
}
struct Directory {
    std::size_t offset;
    unsigned tables;
};
std::vector<Directory> directories(Bytes bytes) {
    require(bytes.size() >= 12 && bytes.size() <= file_byte_limit, "Invalid UI font size");
    std::vector<Directory> result;
    const auto directory = [&](std::size_t start) {
        require(start <= bytes.size() && 12 <= bytes.size() - start,
                "Truncated UI font table directory");
        const auto signature = word(bytes, start, 4);
        require(signature == 0x00010000 || signature == 0x4f54544f,
                "UI font requires TrueType/OpenType outlines");
        const auto count = word(bytes, start + 4, 2);
        require(count > 0 && count <= 128 && 12 + count * 16 <= bytes.size() - start,
                "Invalid UI font table directory");
        for (unsigned i = 0; i < count; ++i) {
            const auto at = start + 12 + i * 16;
            const auto offset = word(bytes, at + 8, 4), length = word(bytes, at + 12, 4);
            require(offset <= bytes.size() && length <= bytes.size() - offset,
                    "Truncated UI font table");
        }
        result.push_back({start, count});
    };
    if (word(bytes, 0, 4) != 0x74746366) {
        directory(0);
        return result;
    }
    const auto version = word(bytes, 4, 4), count = word(bytes, 8, 4);
    require(version == 0x00010000 || version == 0x00020000,
            "Unsupported UI font collection version");
    require(count > 0 && count <= face_limit, "UI font collection requires 1..32 faces");
    const auto header = std::size_t(12 + 4 * count + (version == 0x00020000 ? 12 : 0));
    require(header <= bytes.size(), "Truncated UI font collection header");
    if (version == 0x00020000) {
        const auto at = 12 + 4 * count;
        const auto tag = word(bytes, at, 4), length = word(bytes, at + 4, 4),
                   offset = word(bytes, at + 8, 4);
        require((tag == 0 && length == 0 && offset == 0) ||
                    (tag == 0x44534947 && length > 0 && offset >= header &&
                     offset <= bytes.size() && length == bytes.size() - offset),
                "Invalid UI font collection signature span");
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto offset = word(bytes, 12 + i * 4, 4);
        require(offset >= header && offset % 4 == 0, "Invalid UI font collection directory offset");
        directory(offset);
    }
    return result;
}
std::optional<Bytes> table(Bytes bytes, Directory directory, std::uint32_t tag) {
    std::optional<Bytes> found;
    for (unsigned i = 0; i < directory.tables; ++i) {
        const auto at = directory.offset + 12 + i * 16;
        if (word(bytes, at, 4) == tag) {
            if (found)
                return {}; // Ambiguous optional data must not invent a face label.
            found = bytes.subspan(word(bytes, at + 8, 4), word(bytes, at + 12, 4));
        }
    }
    return found;
}
constexpr std::size_t label_bytes = 64;
std::optional<std::string> label(Bytes bytes, bool unicode) {
    if (bytes.empty() || bytes.size() > label_bytes * 2 || (unicode && bytes.size() % 2))
        return {};
    std::string result;
    for (std::size_t at = 0; at < bytes.size();) {
        auto scalar = word(bytes, at, unicode ? 2 : 1);
        at += unicode ? 2 : 1;
        if (unicode && scalar >= 0xd800 && scalar <= 0xdbff) {
            if (at == bytes.size())
                return {};
            const auto low = word(bytes, at, 2);
            if (low < 0xdc00 || low > 0xdfff)
                return {};
            at += 2;
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
        } else if (scalar >= 0xdc00 && scalar <= 0xdfff)
            return {};
        if (scalar < 32 || scalar == 127 || (!unicode && scalar >= 128))
            return {};
        if (scalar < 0x80)
            result.push_back(char(scalar));
        else {
            if (scalar >= 0x10000) {
                result.push_back(char(0xf0 | scalar >> 18));
                result.push_back(char(0x80 | ((scalar >> 12) & 63)));
            } else if (scalar >= 0x800)
                result.push_back(char(0xe0 | scalar >> 12));
            else
                result.push_back(char(0xc0 | scalar >> 6));
            if (scalar >= 0x800)
                result.push_back(char(0x80 | ((scalar >> 6) & 63)));
            result.push_back(char(0x80 | (scalar & 63)));
        }
        if (result.size() > label_bytes)
            return {};
    }
    return result;
}
void names(Bytes bytes, nlohmann::json& result) {
    if (bytes.size() < 6)
        return;
    const auto version = word(bytes, 0, 2), count = word(bytes, 2, 2), storage = word(bytes, 4, 2);
    if (version > 1 || count > 512 || 6 + count * 12 > bytes.size())
        return;
    auto records_end = std::size_t(6 + count * 12);
    unsigned languages = 0;
    if (version == 1) {
        if (records_end + 2 > bytes.size())
            return;
        languages = word(bytes, records_end, 2);
        if (languages > 32 || records_end + 2 + languages * 4 > bytes.size())
            return;
        records_end += 2 + languages * 4;
    }
    if (storage < records_end || storage > bytes.size())
        return;
    const auto string = [&](std::size_t at, bool unicode) -> std::optional<std::string> {
        const auto length = word(bytes, at, 2), offset = word(bytes, at + 2, 2);
        if (offset > bytes.size() - storage || length > bytes.size() - storage - offset)
            return {};
        return label(bytes.subspan(storage + offset, length), unicode);
    };
    struct Candidate {
        unsigned score = std::numeric_limits<unsigned>::max();
        std::string value;
    };
    constexpr std::array ids{1u, 2u, 4u, 6u, 16u, 17u};
    std::array<Candidate, ids.size()> candidates;
    for (unsigned i = 0; i < count; ++i) {
        const auto at = 6 + i * 12;
        const auto platform = word(bytes, at, 2), encoding = word(bytes, at + 2, 2),
                   language = word(bytes, at + 4, 2), id = word(bytes, at + 6, 2);
        const auto target = std::find(ids.begin(), ids.end(), id);
        if (target == ids.end())
            continue;
        const bool unicode = (platform == 0 && encoding <= 4) ||
                             (platform == 3 && (encoding == 0 || encoding == 1 || encoding == 10));
        const bool ascii = platform == 1 && encoding == 0;
        if (!unicode && !ascii)
            continue;
        unsigned score = platform == 0 ? 20 : platform == 3 ? 30 : 40;
        if (platform == 3 && language < 0x8000 && (language & 0x3ff) == 9)
            score = language == 0x409 ? 0 : 1;
        if (language >= 0x8000) {
            if (version != 1 || language - 0x8000 >= languages)
                continue;
            const auto tag = string(6 + count * 12 + 2 + (language - 0x8000) * 4, true);
            if (!tag)
                continue;
            auto normalized = *tag;
            for (auto& c : normalized)
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
            if (normalized == "en" || normalized.starts_with("en-"))
                score = 2;
        }
        auto& selected = candidates[std::size_t(target - ids.begin())];
        if (score >= selected.score)
            continue;
        if (const auto value = string(at + 8, unicode)) {
            selected.score = score;
            selected.value = *value;
        }
    }
    const std::array<const char*, 4> keys{"family", "style", "full_name", "postscript_name"};
    for (unsigned i = 0; i < keys.size(); ++i) {
        const auto& value = i < 2 && !candidates[i + 4].value.empty() ? candidates[i + 4].value
                                                                      : candidates[i].value;
        if (!value.empty())
            result[keys[i]] = value;
    }
}
} // namespace
unsigned face_count(Bytes bytes) { return unsigned(directories(bytes).size()); }
nlohmann::json describe(Bytes bytes) {
    const auto faces = directories(bytes);
    nlohmann::json result{{"faces", faces.size()}, {"font_faces", nlohmann::json::array()}};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        nlohmann::json face{{"index", i}};
        if (const auto name = table(bytes, faces[i], 0x6e616d65))
            names(*name, face);
        if (const auto os2 = table(bytes, faces[i], 0x4f532f32); os2 && os2->size() >= 6) {
            const auto weight = word(*os2, 4, 2);
            if (weight >= 1 && weight <= 1000)
                face["weight"] = weight;
        }
        result["font_faces"].push_back(std::move(face));
    }
    return result;
}
} // namespace forge::ui_font_detail
