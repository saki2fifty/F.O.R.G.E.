#include "navigation_layers.hpp"
#include "json_document.hpp"
#include "navigation_mesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace forge::navigation_detail {
namespace {
constexpr std::string_view magic = "FNTILE04";
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
bool near(float a, float b) { return std::abs(double(a) - b) <= .001; }
} // namespace
void validate_layers(const HeightfieldLayers& data) {
    validate_navigation_settings(data.settings);
    require(data.settings.dynamic_obstacles, "Tiled navigation requires the dynamic profile");
    require(!data.layers.empty() && data.layers.size() <= max_layers,
            "Navigation requires 1-64 heightfield layers");
    for (unsigned axis = 0; axis < 3; ++axis)
        require(std::isfinite(data.minimum[axis]) && std::isfinite(data.maximum[axis]) &&
                    data.minimum[axis] >= -4096 && data.maximum[axis] <= 4096 &&
                    data.maximum[axis] > data.minimum[axis],
                "Invalid navigation layer bounds");
    const auto cs = data.settings.cell_size, ch = data.settings.cell_height;
    for (unsigned axis : {0u, 2u})
        require((data.maximum[axis] - data.minimum[axis]) / cs <= 512.01f,
                "Navigation layer grid exceeds 512 cells");
    require((data.maximum[1] - data.minimum[1]) / ch < 8191,
            "Navigation layer vertical range exceeds supported voxels");
    std::set<std::tuple<unsigned, unsigned, unsigned>> unique;
    std::map<std::pair<unsigned, unsigned>, unsigned> columns;
    std::optional<float> altitude;
    constexpr unsigned grid = layer_width * layer_width;
    constexpr int dx[]{-1, 0, 1, 0}, dz[]{0, 1, 0, -1};
    for (const auto& layer : data.layers) {
        require(layer.x < 16 && layer.z < 16 && layer.index < 4 &&
                    unique.emplace(layer.x, layer.z, layer.index).second &&
                    ++columns[{layer.x, layer.z}] <= 4,
                "Invalid or repeated navigation layer coordinate");
        require(layer.hmin <= layer.hmax && layer.hmax < 8191 && layer.hmax - layer.hmin < 255 &&
                    layer.minx <= layer.maxx && layer.maxx < layer_width &&
                    layer.minz <= layer.maxz && layer.maxz < layer_width,
                "Invalid navigation layer extents");
        for (unsigned axis = 0; axis < 3; ++axis)
            require(std::isfinite(layer.minimum[axis]) && std::isfinite(layer.maximum[axis]) &&
                        layer.minimum[axis] >= -4096 && layer.maximum[axis] <= 4096 &&
                        layer.maximum[axis] >= layer.minimum[axis],
                    "Invalid navigation tile bounds");
        for (unsigned axis : {0u, 2u}) {
            const auto coordinate = axis == 0 ? layer.x : layer.z;
            const auto expected = data.minimum[axis] + coordinate * layer_width * cs;
            require(near(layer.minimum[axis], expected) &&
                        near(layer.maximum[axis], expected + layer_width * cs) &&
                        expected < data.maximum[axis],
                    "Navigation layer is not anchored to its grid");
        }
        require(near(layer.minimum[1], data.minimum[1] + layer.hmin * ch) &&
                    near(layer.maximum[1], data.minimum[1] + layer.hmax * ch),
                "Navigation layer height bounds mismatch");
        unsigned occupied = 0;
        for (unsigned i = 0; i < grid; ++i) {
            const auto height = layer.cells[i], area = layer.cells[grid + i],
                       con = layer.cells[grid * 2 + i];
            require(area == 0 || area == 63, "Unsupported navigation layer area");
            if (height == 255) {
                require(area == 0 && con == 0, "Empty navigation cell contains connections");
                continue;
            }
            const int x = int(i % layer_width), z = int(i / layer_width);
            require(area == 63, "Occupied navigation cell has no walkable area");
            require(unsigned(x) >= layer.minx && unsigned(x) <= layer.maxx &&
                        unsigned(z) >= layer.minz && unsigned(z) <= layer.maxz,
                    "Navigation cell exceeds its layer rectangle");
            require(height <= layer.hmax - layer.hmin,
                    "Navigation cell exceeds its layer height bounds");
            require((con & (con >> 4)) == 0, "Navigation cell mixes local and portal connections");
            ++occupied;
            const auto y = layer.minimum[1] + height * ch;
            if (data.settings.plane == NavigationPlane::PlanarXY) {
                if (!altitude)
                    altitude = y;
                require(near(*altitude, y), "XY navigation layers must be flat");
            }
            for (unsigned d = 0; d < 4; ++d) {
                if (!(con & (1u << d)))
                    continue;
                const auto nx = x + dx[d], nz = z + dz[d];
                require(nx >= 0 && nz >= 0 && nx < int(layer_width) && nz < int(layer_width),
                        "Navigation connection leaves its cell array");
                const auto neighbour = unsigned(nx + nz * int(layer_width));
                require(layer.cells[neighbour] != 255 && layer.cells[grid + neighbour] == 63 &&
                            (layer.cells[grid * 2 + neighbour] & (1u << ((d + 2) % 4))),
                        "Navigation connection has no reciprocal occupied neighbour");
            }
        }
        require(occupied != 0, "Empty navigation heightfield layer");
    }
}
std::vector<std::byte> encode_layers(const HeightfieldLayers& data) {
    validate_layers(data);
    Json description = {{"settings", data.settings},
                        {"minimum", data.minimum},
                        {"maximum", data.maximum},
                        {"layers", Json::array()}};
    for (const auto& layer : data.layers)
        description["layers"].push_back({{"x", layer.x},
                                         {"z", layer.z},
                                         {"index", layer.index},
                                         {"minimum", layer.minimum},
                                         {"maximum", layer.maximum},
                                         {"hmin", layer.hmin},
                                         {"hmax", layer.hmax},
                                         {"minx", layer.minx},
                                         {"maxx", layer.maxx},
                                         {"minz", layer.minz},
                                         {"maxz", layer.maxz}});
    const auto text = description.dump();
    require(text.size() <= 65536, "Navigation layer description exceeds 64 KiB");
    std::vector<std::byte> out;
    for (auto c : magic)
        out.push_back(std::byte(c));
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(std::byte((text.size() >> (8 * i)) & 255));
    const auto bytes = std::as_bytes(std::span(text));
    out.insert(out.end(), bytes.begin(), bytes.end());
    for (const auto& layer : data.layers) {
        const auto cells = std::as_bytes(std::span(layer.cells));
        out.insert(out.end(), cells.begin(), cells.end());
    }
    require(out.size() <= max_nav_bytes, "Navigation layers exceed 4 MiB");
    return out;
}
HeightfieldLayers decode_layers(std::span<const std::byte> bytes) {
    require(bytes.size() >= 12 && bytes.size() <= max_nav_bytes &&
                std::equal(magic.begin(), magic.end(), bytes.begin(),
                           [](char a, std::byte b) { return a == std::to_integer<char>(b); }),
            "Unsupported navigation layers");
    unsigned size = 0;
    for (unsigned i = 0; i < 4; ++i)
        size |= std::to_integer<unsigned>(bytes[8 + i]) << (8 * i);
    require(size > 0 && size <= 65536 && size <= bytes.size() - 12,
            "Truncated navigation layer description");
    const auto meta = detail::parse_json_document(
        std::string_view(reinterpret_cast<const char*>(bytes.data() + 12), size),
        [](int depth, Json::parse_event_t, Json&) {
            if (depth > 8)
                throw std::runtime_error("Navigation layer description nesting limit");
            return true;
        });
    const auto& entries = meta.at("layers");
    require(meta.is_object() && meta.size() == 4 && entries.is_array() && !entries.empty() &&
                entries.size() <= max_layers &&
                bytes.size() - 12 - size == entries.size() * sizeof(HeightfieldLayer::cells),
            "Navigation layer counts/lengths do not match");
    HeightfieldLayers out;
    out.settings = meta.at("settings").get<NavigationSettings>();
    out.minimum = meta.at("minimum").get<std::array<float, 3>>();
    out.maximum = meta.at("maximum").get<std::array<float, 3>>();
    auto at = std::size_t(12 + size);
    for (const auto& entry : entries) {
        require(entry.is_object() && entry.size() == 11, "Invalid navigation layer descriptor");
        HeightfieldLayer layer;
        const auto number = [&](const char* key) {
            const auto& v = entry.at(key);
            require(v.is_number_integer() && v >= 0 && v <= 8191,
                    "Navigation layer descriptor requires bounded integers");
            return v.get<unsigned>();
        };
        layer.x = number("x");
        layer.z = number("z");
        layer.index = number("index");
        layer.hmin = number("hmin");
        layer.hmax = number("hmax");
        layer.minx = number("minx");
        layer.maxx = number("maxx");
        layer.minz = number("minz");
        layer.maxz = number("maxz");
        layer.minimum = entry.at("minimum").get<std::array<float, 3>>();
        layer.maximum = entry.at("maximum").get<std::array<float, 3>>();
        std::memcpy(layer.cells.data(), bytes.data() + at, layer.cells.size());
        at += layer.cells.size();
        out.layers.push_back(std::move(layer));
    }
    validate_layers(out);
    return out;
}
Json layer_properties(const HeightfieldLayers& data) {
    return {{"minimum", data.minimum},        {"maximum", data.maximum},
            {"height", data.settings.height}, {"radius", data.settings.radius},
            {"climb", data.settings.climb},   {"cell_inverse", 1 / data.settings.cell_size}};
}
} // namespace forge::navigation_detail
