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
std::vector<std::string> name_labels(Bytes bytes, std::span<const unsigned> ids) {
    std::vector<std::string> result(ids.size());
    if (bytes.size() < 6)
        return result;
    const auto version = word(bytes, 0, 2), count = word(bytes, 2, 2), storage = word(bytes, 4, 2);
    if (version > 1 || count > 512 || 6 + count * 12 > bytes.size())
        return result;
    auto records_end = std::size_t(6 + count * 12);
    unsigned languages = 0;
    if (version == 1) {
        if (records_end + 2 > bytes.size())
            return result;
        languages = word(bytes, records_end, 2);
        if (languages > 32 || records_end + 2 + languages * 4 > bytes.size())
            return result;
        records_end += 2 + languages * 4;
    }
    if (storage < records_end || storage > bytes.size())
        return result;
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
    std::vector<Candidate> candidates(ids.size());
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
    for (std::size_t i = 0; i < ids.size(); ++i)
        result[i] =
            candidates[std::size_t(std::find(ids.begin(), ids.end(), ids[i]) - ids.begin())].value;
    return result;
}
void names(Bytes bytes, nlohmann::json& result) {
    constexpr std::array ids{1u, 2u, 4u, 6u, 16u, 17u};
    const auto candidates = name_labels(bytes, ids);
    const std::array<const char*, 4> keys{"family", "style", "full_name", "postscript_name"};
    for (unsigned i = 0; i < keys.size(); ++i) {
        const auto& value = i < 2 && !candidates[i + 4].empty() ? candidates[i + 4] : candidates[i];
        if (!value.empty())
            result[keys[i]] = value;
    }
}
void axes(Bytes bytes, const std::optional<Bytes>& name, nlohmann::json& result) {
    if (bytes.size() < 16 || word(bytes, 0, 4) != 0x00010000 || word(bytes, 6, 2) != 2)
        return;
    const auto offset = std::size_t(word(bytes, 4, 2));
    const auto count = word(bytes, 8, 2), size = word(bytes, 10, 2);
    const auto instances = word(bytes, 12, 2), instance_size = word(bytes, 14, 2);
    if (!count || count > variation_axis_limit || size != 20 || offset < 16 ||
        offset > bytes.size() || count * size > bytes.size() - offset ||
        (instance_size != count * 4 + 4 && instance_size != count * 4 + 6) ||
        std::size_t(instances) * instance_size > bytes.size() - offset - count * size)
        return;
    const auto fixed = [&](std::size_t at) {
        const auto raw = word(bytes, at, 4);
        const auto value = raw <= 0x7fffffff ? std::int64_t(raw) : std::int64_t(raw) - 0x100000000;
        return double(value) / 65536.;
    };
    auto recorded = nlohmann::json::array();
    std::vector<unsigned> ids;
    for (unsigned i = 0; i < count; ++i) {
        const auto at = offset + i * size;
        std::string tag;
        for (unsigned c = 0; c < 4; ++c) {
            const auto value = word(bytes, at + c, 1);
            if (value < 32 || value > 126)
                return;
            tag.push_back(char(value));
        }
        if (std::ranges::any_of(recorded, [&](const auto& axis) { return axis.at("tag") == tag; }))
            return;
        const auto minimum = fixed(at + 4), standard = fixed(at + 8), maximum = fixed(at + 12);
        const auto flags = word(bytes, at + 16, 2), id = word(bytes, at + 18, 2);
        if (minimum > standard || standard > maximum || flags > 1 || id < 256 || id > 32767)
            return;
        recorded.push_back({{"tag", tag},
                            {"minimum", minimum},
                            {"default", standard},
                            {"maximum", maximum},
                            {"hidden", bool(flags & 1)}});
        ids.push_back(id);
    }
    if (name) {
        const auto labels = name_labels(*name, ids);
        for (std::size_t i = 0; i < labels.size(); ++i)
            if (!labels[i].empty())
                recorded[i]["name"] = labels[i];
    }
    result["axes"] = std::move(recorded);
}
void colors(Bytes layers, Bytes palette, nlohmann::json& result) {
    if (layers.size() < 14 || word(layers, 0, 2) != 0 || palette.size() < 12 ||
        word(palette, 0, 2) > 1)
        return;
    const auto bases = word(layers, 2, 2), base_offset = word(layers, 4, 4),
               layer_offset = word(layers, 8, 4), count = word(layers, 12, 2);
    const auto entries = word(palette, 2, 2), palettes = word(palette, 4, 2),
               records = word(palette, 6, 2), color_offset = word(palette, 8, 4);
    if (!bases || !count || base_offset < 14 || base_offset > layers.size() ||
        bases * 6 > layers.size() - base_offset || layer_offset < 14 ||
        layer_offset > layers.size() || count * 4 > layers.size() - layer_offset || !entries ||
        !palettes || palettes * 2 > palette.size() - 12 || color_offset < 12 + palettes * 2 ||
        color_offset > palette.size() || records * 4 > palette.size() - color_offset)
        return;
    for (unsigned i = 0; i < palettes; ++i) {
        const auto start = word(palette, 12 + 2 * i, 2);
        if (start > records || entries > records - start)
            return;
    }
    unsigned previous = 0;
    for (unsigned i = 0; i < bases; ++i) {
        const auto at = base_offset + i * 6;
        const auto glyph = word(layers, at, 2), start = word(layers, at + 2, 2),
                   length = word(layers, at + 4, 2);
        if ((i && glyph <= previous) || !length || start > count || length > count - start)
            return;
        previous = glyph;
    }
    bool foreground = false;
    for (unsigned i = 0; i < count; ++i) {
        const auto color = word(layers, layer_offset + i * 4 + 2, 2);
        if (color == 0xffff)
            foreground = true;
        else if (color >= entries)
            return;
    }
    result["color"] = {{"format", "COLRv0"},   {"palettes", palettes}, {"palette_entries", entries},
                       {"base_glyphs", bases}, {"layers", count},      {"foreground", foreground}};
}
} // namespace
unsigned face_count(Bytes bytes) { return unsigned(directories(bytes).size()); }
nlohmann::json describe(Bytes bytes) {
    const auto faces = directories(bytes);
    nlohmann::json result{{"faces", faces.size()}, {"font_faces", nlohmann::json::array()}};
    unsigned recorded_axes = 0;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        nlohmann::json face{{"index", i}};
        const auto name = table(bytes, faces[i], 0x6e616d65);
        if (name)
            names(*name, face);
        if (const auto variation = table(bytes, faces[i], 0x66766172))
            axes(*variation, name, face);
        const auto color = table(bytes, faces[i], 0x434f4c52);
        const auto palette = table(bytes, faces[i], 0x4350414c);
        if (color && palette)
            colors(*color, *palette, face);
        if (const auto recorded = face.find("axes"); recorded != face.end()) {
            if (recorded->size() > axis_metadata_limit - recorded_axes) {
                face.erase("axes");
                face["axes_omitted"] = true;
            } else
                recorded_axes += unsigned(recorded->size());
        }
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
