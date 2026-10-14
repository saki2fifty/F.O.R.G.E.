#include "bounded_json.hpp"
#include "brush_samples.hpp"
#include <algorithm>
#include <cmath>
#include <forge/terrain_source.hpp>
#include <map>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
using Key = std::array<int, 2>;
struct Point {
    std::array<std::array<float, 2>, terrain_layer_limit> layers{};
    float height = 0;
};
using Grid = std::map<Key, Point>;
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
double number(const Json& value, double low, double high) {
    require(value.is_number(), "Terrain value must be numeric");
    const auto result = value.get<double>();
    require(std::isfinite(result) && result >= low && result <= high,
            "Terrain value is outside its finite range");
    return result;
}
float scalar(const Json& value, double low, double high) {
    const auto input = number(value, low, high);
    const auto result = float(input);
    require(std::isfinite(result) && (input == 0 || result != 0),
            "Terrain source value is not finite binary32");
    return result;
}
void fields(const Json& value, std::initializer_list<const char*> names) {
    require(value.is_object(), "Terrain record must be an object");
    for (const auto& [key, item] : value.items())
        require(std::any_of(names.begin(), names.end(), [&](auto name) { return key == name; }),
                "Unknown terrain source field");
}
Key key(const TerrainRaster& raster, const TerrainTile& tile, unsigned x, unsigned z) {
    const auto span = int(raster.side - 1);
    return {tile.coordinate[0] * span + int(x), tile.coordinate[1] * span + int(z)};
}
float coordinate(const TerrainRaster& raster, int value) {
    return float(double(value) * (raster.tile_size / (raster.side - 1)));
}
Grid grid(const TerrainRaster& raster) {
    require(bool(raster.asset) && raster.side >= 2 && raster.side <= 129 &&
                std::isfinite(raster.tile_size) && raster.tile_size >= 1 &&
                raster.tile_size <= 1000 && !raster.tiles.empty() &&
                raster.tiles.size() <= terrain_tile_limit && !raster.layers.empty() &&
                raster.layers.size() <= terrain_layer_limit,
            "Terrain raster settings exceed the heightfield profile");
    std::map<TerrainLayerId, std::size_t> layers;
    for (std::size_t i = 0; i < raster.layers.size(); ++i) {
        const auto& layer = raster.layers[i];
        require(bool(layer.id) && layers.emplace(layer.id, i).second && !layer.name.empty() &&
                    layer.name.size() <= 128 && layer.name.find('\0') == std::string::npos &&
                    std::isfinite(layer.strength) && layer.strength >= 0 && layer.strength <= 1,
                "Terrain layer identity/name/strength is invalid");
    }
    Grid result;
    std::set<TerrainTileId> tiles;
    std::set<Key> positions;
    const auto count = std::size_t(raster.side) * raster.side;
    for (const auto& tile : raster.tiles) {
        require(bool(tile.id) && tiles.insert(tile.id).second &&
                    positions.insert(tile.coordinate).second && tile.coordinate[0] >= -16 &&
                    tile.coordinate[0] <= 16 && tile.coordinate[1] >= -16 &&
                    tile.coordinate[1] <= 16 && tile.layers.size() == layers.size(),
                "Terrain tile identity/coordinate/layers are invalid");
        std::vector<const TerrainLayerSamples*> ordered(layers.size());
        for (const auto& values : tile.layers) {
            const auto found = layers.find(values.layer);
            require(found != layers.end() && !ordered[found->second] &&
                        values.heights.size() == count && values.mask.size() == count,
                    "Terrain tile layer samples are incomplete or repeated");
            ordered[found->second] = &values;
        }
        for (unsigned z = 0; z < raster.side; ++z)
            for (unsigned x = 0; x < raster.side; ++x) {
                Point point;
                double height = 0;
                const auto index = std::size_t(z) * raster.side + x;
                for (std::size_t layer = 0; layer < ordered.size(); ++layer) {
                    const auto h = ordered[layer]->heights[index], m = ordered[layer]->mask[index];
                    require(std::isfinite(h) && std::abs(h) <= 10000 && std::isfinite(m) &&
                                m >= 0 && m <= 1,
                            "Terrain height/mask sample is invalid");
                    point.layers[layer] = {h, m};
                    if (raster.layers[layer].enabled)
                        height += double(h) * m * raster.layers[layer].strength;
                }
                require(std::isfinite(height) && std::abs(height) <= 10000,
                        "Composed terrain height exceeds the admitted range");
                point.height = float(height);
                const auto [at, inserted] =
                    result.emplace(key(raster, tile, x, z), std::move(point));
                if (!inserted) {
                    for (std::size_t layer = 0; layer < ordered.size(); ++layer)
                        require(at->second.layers[layer] ==
                                    std::array{ordered[layer]->heights[index],
                                               ordered[layer]->mask[index]},
                                "Terrain shared layer/mask border samples disagree");
                }
            }
    }
    return result;
}
TerrainRaster decode(const Json& source) {
    fields(source, {"format", "version", "profile", "asset_id", "side", "tile_size", "layers",
                    "tiles", "extensions"});
    require(source.at("format") == "forge.heightfield" &&
                source.at("version").is_number_integer() && source.at("version") == 1 &&
                source.at("profile") == "heightfield_mesh1",
            "Unsupported terrain source version/profile");
    if (source.contains("extensions"))
        require(source.at("extensions").is_object(), "Terrain extensions must be an object");
    TerrainRaster result;
    result.asset = source.at("asset_id").get<AssetId>();
    require(source.at("side").is_number_integer(), "Terrain side must be an integer");
    result.side = unsigned(number(source.at("side"), 2, 129));
    result.tile_size = number(source.at("tile_size"), 1, 1000);
    require(source.at("layers").is_array() && source.at("layers").size() <= terrain_layer_limit &&
                source.at("tiles").is_array() && source.at("tiles").size() <= terrain_tile_limit,
            "Terrain layer/tile count exceeds limits");
    for (const auto& value : source.at("layers")) {
        fields(value, {"id", "name", "enabled", "strength"});
        require(value.at("enabled").is_boolean(), "Terrain layer enabled must be boolean");
        result.layers.push_back({TerrainLayerId::parse(value.at("id").get<std::string>()),
                                 value.at("name").get<std::string>(),
                                 value.at("enabled").get<bool>(),
                                 scalar(value.at("strength"), 0, 1)});
    }
    for (const auto& value : source.at("tiles")) {
        fields(value, {"id", "coordinate", "layers"});
        TerrainTile tile;
        tile.id = TerrainTileId::parse(value.at("id").get<std::string>());
        const auto& position = value.at("coordinate");
        require(position.is_array() && position.size() == 2,
                "Terrain tile coordinate needs two integers");
        for (unsigned axis = 0; axis < 2; ++axis) {
            require(position[axis].is_number_integer(), "Terrain tile coordinate must be integer");
            tile.coordinate[axis] = int(number(position[axis], -16, 16));
        }
        require(value.at("layers").is_array() && value.at("layers").size() <= terrain_layer_limit,
                "Terrain tile layers exceed limits");
        for (const auto& input : value.at("layers")) {
            fields(input, {"layer", "heights", "mask"});
            TerrainLayerSamples samples;
            samples.layer = TerrainLayerId::parse(input.at("layer").get<std::string>());
            for (const auto name : {"heights", "mask"}) {
                const auto& values = input.at(name);
                require(values.is_array() &&
                            values.size() == std::size_t(result.side) * result.side,
                        "Terrain sample count differs from its grid resolution");
                auto& output = std::string_view(name) == "heights" ? samples.heights : samples.mask;
                output.reserve(values.size());
                for (const auto& sample : values)
                    output.push_back(scalar(sample,
                                            name == std::string_view("heights") ? -10000 : 0,
                                            name == std::string_view("heights") ? 10000 : 1));
            }
            tile.layers.push_back(std::move(samples));
        }
        result.tiles.push_back(std::move(tile));
    }
    return result;
}
std::array<float, 3> unit(std::array<double, 3> value) {
    const auto length = std::hypot(value[0], value[1], value[2]);
    require(std::isfinite(length) && length > 0, "Terrain normal/tangent has invalid length");
    return {float(value[0] / length), float(value[1] / length), float(value[2] / length)};
}
} // namespace
void validate_terrain_raster(const TerrainRaster& raster) { (void)grid(raster); }
MeshData terrain_mesh(const TerrainRaster& raster) {
    const auto points = grid(raster);
    MeshData result;
    result.material_slots = 1;
    result.lods.resize(1);
    for (const auto& tile : raster.tiles) {
        MeshPart part;
        part.vertices = raster.side * raster.side;
        std::vector<float> positions, normals, tangents, uv;
        positions.reserve(std::size_t(part.vertices) * 3);
        normals.reserve(positions.capacity());
        tangents.reserve(std::size_t(part.vertices) * 4);
        uv.reserve(std::size_t(part.vertices) * 2);
        for (unsigned z = 0; z < raster.side; ++z)
            for (unsigned x = 0; x < raster.side; ++x) {
                const auto address = key(raster, tile, x, z);
                const auto h = points.at(address).height;
                auto derivative = [&](unsigned axis) {
                    auto low = address, high = address;
                    --low[axis];
                    ++high[axis];
                    if (!points.contains(low))
                        low = address;
                    if (!points.contains(high))
                        high = address;
                    const auto span =
                        double(coordinate(raster, high[axis])) - coordinate(raster, low[axis]);
                    require(span > 0, "Terrain spacing collapsed in float geometry");
                    return (double(points.at(high).height) - points.at(low).height) / span;
                };
                const auto dx = derivative(0), dz = derivative(1);
                positions.insert(positions.end(), {coordinate(raster, address[0]), h,
                                                   coordinate(raster, address[1])});
                const auto normal = unit({-dx, 1, -dz}), tangent = unit({1, dx, 0});
                normals.insert(normals.end(), normal.begin(), normal.end());
                tangents.insert(tangents.end(), tangent.begin(), tangent.end());
                tangents.push_back(-1);
                uv.insert(uv.end(), {float(double(address[0]) / (raster.side - 1)),
                                     float(double(address[1]) / (raster.side - 1))});
            }
        for (unsigned z = 0; z + 1 < raster.side; ++z)
            for (unsigned x = 0; x + 1 < raster.side; ++x) {
                const auto a = z * raster.side + x, b = a + 1, c = a + raster.side, d = c + 1;
                part.indices.insert(part.indices.end(), {a, c, b, b, c, d});
            }
        part.streams = {{"POSITION", 3, std::move(positions)},
                        {"NORMAL", 3, std::move(normals)},
                        {"TANGENT", 4, std::move(tangents)},
                        {"TEXCOORD_0", 2, std::move(uv)}};
        part.bounds = mesh_bounds(part);
        result.lods.front().parts.push_back(std::move(part));
    }
    validate_mesh(result, {32 * 1024 * 1024, 8 * 129 * 129, 8 * 128 * 128 * 6, 8, 4, 0, 1});
    return result;
}
std::optional<double> terrain_height(const TerrainRaster& raster, double x, double z) {
    require(std::isfinite(x) && std::isfinite(z), "Terrain query position is nonfinite");
    const auto points = grid(raster);
    for (const auto& tile : raster.tiles) {
        const auto first = key(raster, tile, 0, 0),
                   last = key(raster, tile, raster.side - 1, raster.side - 1);
        if (x < coordinate(raster, first[0]) || x > coordinate(raster, last[0]) ||
            z < coordinate(raster, first[1]) || z > coordinate(raster, last[1]))
            continue;
        auto cell = [&](double position, unsigned axis) {
            int at = std::clamp(int(std::floor((position - coordinate(raster, first[axis])) /
                                               (raster.tile_size / (raster.side - 1)))),
                                0, int(raster.side - 2));
            while (at > 0 && position < coordinate(raster, first[axis] + at))
                --at;
            while (at + 1 < int(raster.side - 1) &&
                   position > coordinate(raster, first[axis] + at + 1))
                ++at;
            const auto low = coordinate(raster, first[axis] + at),
                       high = coordinate(raster, first[axis] + at + 1);
            return std::pair{first[axis] + at, (position - low) / (double(high) - low)};
        };
        const auto [cx, fx] = cell(x, 0);
        const auto [cz, fz] = cell(z, 1);
        const double a = points.at({cx, cz}).height, b = points.at({cx + 1, cz}).height,
                     c = points.at({cx, cz + 1}).height, d = points.at({cx + 1, cz + 1}).height;
        return fx + fz <= 1 ? a + (b - a) * fx + (c - a) * fz
                            : d + (c - d) * (1 - fx) + (b - d) * (1 - fz);
    }
    return {};
}
TerrainSource TerrainSource::create(AssetId asset, unsigned side, double tile_size) {
    require(bool(asset) && side >= 2 && side <= 129, "Invalid new terrain identity/resolution");
    const auto layer = TerrainLayerId::generate();
    TerrainSource result{
        {{"format", "forge.heightfield"},
         {"version", 1},
         {"profile", "heightfield_mesh1"},
         {"asset_id", asset},
         {"side", side},
         {"tile_size", tile_size},
         {"layers",
          Json::array(
              {{{"id", layer.str()}, {"name", "Height"}, {"enabled", true}, {"strength", 1.}}})},
         {"tiles",
          Json::array(
              {{{"id", TerrainTileId::generate().str()},
                {"coordinate", {0, 0}},
                {"layers", Json::array({{{"layer", layer.str()},
                                         {"heights", std::vector<float>(side * side)},
                                         {"mask", std::vector<float>(side * side, 1)}}})}}})}}};
    result.validate();
    return result;
}
TerrainSource TerrainSource::parse(std::span<const std::byte> bytes) {
    TerrainSource result{
        asset_detail::parse_bounded_json(bytes, terrain_source_byte_limit, 1000000, 16)};
    (void)result.raster();
    return result;
}
AssetId TerrainSource::asset() const { return document.at("asset_id").get<AssetId>(); }
TerrainRaster TerrainSource::raster() const {
    auto result = decode(document);
    validate_terrain_raster(result);
    return result;
}
MeshData TerrainSource::mesh() const { return terrain_mesh(decode(document)); }
void TerrainSource::validate() const {
    const auto text = document.dump();
    (void)parse(std::as_bytes(std::span(text)));
}
namespace {
void replace_raster(TerrainSource& source, const TerrainRaster& raster) {
    // Preserve root metadata without first copying the large arrays that this
    // operation replaces. Complete candidate validation remains mandatory.
    TerrainSource candidate{Json::object()};
    for (const auto& [name, value] : source.document.items())
        if (name != "layers" && name != "tiles")
            candidate.document[name] = value;
    auto& doc = candidate.document;
    doc["side"] = raster.side;
    doc["tile_size"] = raster.tile_size;
    doc["layers"] = Json::array();
    for (const auto& layer : raster.layers)
        doc["layers"].push_back({{"id", layer.id.str()},
                                 {"name", layer.name},
                                 {"enabled", layer.enabled},
                                 {"strength", layer.strength}});
    doc["tiles"] = Json::array();
    for (const auto& tile : raster.tiles) {
        Json layers = Json::array();
        for (const auto& values : tile.layers)
            layers.push_back({{"layer", values.layer.str()},
                              {"heights", values.heights},
                              {"mask", values.mask}});
        doc["tiles"].push_back({{"id", tile.id.str()},
                                {"coordinate", tile.coordinate},
                                {"layers", std::move(layers)}});
    }
    candidate.validate();
    source.document = std::move(candidate.document);
}
std::size_t layer_index(const TerrainRaster& raster, TerrainLayerId id) {
    for (std::size_t i = 0; i < raster.layers.size(); ++i)
        if (raster.layers[i].id == id)
            return i;
    throw std::runtime_error("Terrain layer is not in this source");
}
float interpolate(const std::vector<float>& values, unsigned side, double x, double z) {
    const auto cx = std::min(unsigned(std::floor(x)), side - 2),
               cz = std::min(unsigned(std::floor(z)), side - 2);
    const auto fx = x - cx, fz = z - cz;
    const double a = values[cz * side + cx], b = values[cz * side + cx + 1],
                 c = values[(cz + 1) * side + cx], d = values[(cz + 1) * side + cx + 1];
    // Preserve exact endpoint values, including tiny admitted heights beside
    // large ones; cancellation at x=1 must not break a neighboring tile seam.
    auto mix = [](double a, double b, double f) {
        return f == 0 ? a : f == 1 ? b : a * (1 - f) + b * f;
    };
    return float(mix(mix(a, b, fx), mix(c, d, fx), fz));
}
} // namespace
TerrainTileId TerrainSource::add_tile(std::array<int, 2> position) {
    auto values = decode(document);
    require(values.tiles.size() < terrain_tile_limit, "Terrain tile limit reached");
    require(position[0] >= -16 && position[0] <= 16 && position[1] >= -16 && position[1] <= 16,
            "Terrain tile coordinate exceeds profile");
    for (const auto& tile : values.tiles)
        require(tile.coordinate != position, "Terrain tile coordinate is occupied");
    const auto prior = grid(values);
    TerrainTile tile;
    tile.id = TerrainTileId::generate();
    tile.coordinate = position;
    for (std::size_t layer = 0; layer < values.layers.size(); ++layer) {
        TerrainLayerSamples samples{values.layers[layer].id, {}, {}};
        for (unsigned z = 0; z < values.side; ++z)
            for (unsigned x = 0; x < values.side; ++x) {
                const auto found = prior.find(key(values, tile, x, z));
                const auto sample =
                    found == prior.end() ? std::array<float, 2>{0, 1} : found->second.layers[layer];
                samples.heights.push_back(sample[0]);
                samples.mask.push_back(sample[1]);
            }
        tile.layers.push_back(std::move(samples));
    }
    const auto id = tile.id;
    values.tiles.push_back(std::move(tile));
    replace_raster(*this, values);
    return id;
}
void TerrainSource::remove_tile(TerrainTileId id) {
    auto values = raster();
    const auto count = std::erase_if(values.tiles, [&](const auto& tile) { return tile.id == id; });
    require(count == 1 && !values.tiles.empty(), "Cannot remove absent or last terrain tile");
    replace_raster(*this, values);
}
TerrainLayerId TerrainSource::add_layer(std::string name) {
    auto values = raster();
    require(values.layers.size() < terrain_layer_limit, "Terrain layer limit reached");
    const auto id = TerrainLayerId::generate();
    values.layers.push_back({id, std::move(name), true, 1});
    const auto count = std::size_t(values.side) * values.side;
    for (auto& tile : values.tiles)
        tile.layers.push_back({id, std::vector<float>(count), std::vector<float>(count, 1)});
    replace_raster(*this, values);
    return id;
}
void TerrainSource::remove_layer(TerrainLayerId id) {
    auto values = raster();
    (void)layer_index(values, id);
    require(values.layers.size() > 1, "Cannot remove last terrain height layer");
    std::erase_if(values.layers, [&](const auto& layer) { return layer.id == id; });
    for (auto& tile : values.tiles)
        std::erase_if(tile.layers, [&](const auto& layer) { return layer.layer == id; });
    replace_raster(*this, values);
}
void TerrainSource::set_layer(TerrainLayerId id, bool enabled, float strength) {
    auto values = raster();
    auto& layer = values.layers[layer_index(values, id)];
    layer.enabled = enabled;
    layer.strength = strength;
    replace_raster(*this, values);
}
void TerrainSource::resample(unsigned side) {
    require(side >= 2 && side <= 129, "Terrain resolution must be within 2–129");
    auto values = raster();
    if (values.side == side)
        return;
    for (auto& tile : values.tiles)
        for (auto& layer : tile.layers) {
            std::vector<float> heights, mask;
            heights.reserve(std::size_t(side) * side);
            mask.reserve(std::size_t(side) * side);
            for (unsigned z = 0; z < side; ++z)
                for (unsigned x = 0; x < side; ++x) {
                    const auto px = double(x) * (values.side - 1) / (side - 1),
                               pz = double(z) * (values.side - 1) / (side - 1);
                    heights.push_back(interpolate(layer.heights, values.side, px, pz));
                    mask.push_back(interpolate(layer.mask, values.side, px, pz));
                }
            layer.heights = std::move(heights);
            layer.mask = std::move(mask);
        }
    values.side = side;
    replace_raster(*this, values);
}
void TerrainSource::brush(TerrainLayerId id, std::span<const MeshBrushSample> samples,
                          double radius, TerrainBrush mode, double amount, double target,
                          MeshBrushSymmetry symmetry) {
    brush_detail::validate_brush(samples, radius);
    require((std::uint8_t(symmetry) & ~5u) == 0, "Terrain brush symmetry only supports X/Z");
    require(mode == TerrainBrush::Raise || mode == TerrainBrush::Flatten ||
                mode == TerrainBrush::Smooth || mode == TerrainBrush::Mask,
            "Unknown terrain brush mode");
    require(std::isfinite(amount) && (mode == TerrainBrush::Raise ? std::abs(amount) <= 10000
                                                                  : amount >= 0 && amount <= 1),
            "Terrain brush amount is outside its mode range");
    require(std::isfinite(target) && (mode == TerrainBrush::Mask ? target >= 0 && target <= 1
                                                                 : std::abs(target) <= 10000),
            "Terrain brush target is outside its mode range");
    auto values = decode(document);
    const auto prior = grid(values); // Validates seams, identities and composition once.
    const auto layer = layer_index(values, id);
    std::vector<MeshBrushSample> projected(samples.begin(), samples.end());
    for (auto& sample : projected)
        sample.point[1] = 0;
    std::map<Key, std::array<float, 2>> changed;
    for (const auto& [address, point] : prior) {
        const auto influence = brush_detail::brush_influence(
            {coordinate(values, address[0]), 0, coordinate(values, address[1])}, projected, radius,
            symmetry);
        if (influence == 0)
            continue;
        auto result = point.layers[layer];
        double height = result[0], mask = result[1];
        if (mode == TerrainBrush::Raise)
            height += amount * influence;
        else if (mode == TerrainBrush::Flatten)
            height += (target - height) * amount * influence;
        else if (mode == TerrainBrush::Mask)
            mask += (target - mask) * amount * influence;
        else {
            double sum = 0;
            unsigned count = 0;
            for (const auto offset : std::array<Key, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}}) {
                const auto found = prior.find({address[0] + offset[0], address[1] + offset[1]});
                if (found != prior.end()) {
                    sum += found->second.layers[layer][0];
                    ++count;
                }
            }
            if (count)
                height += (sum / count - height) * amount * influence;
        }
        require(std::isfinite(height) && std::abs(height) <= 10000 && mask >= 0 && mask <= 1,
                "Terrain brush exceeds height/mask range");
        changed.emplace(address, std::array{float(height), float(mask)});
    }
    for (auto& tile : values.tiles) {
        auto found = std::find_if(tile.layers.begin(), tile.layers.end(),
                                  [&](const auto& samples) { return samples.layer == id; });
        for (unsigned z = 0; z < values.side; ++z)
            for (unsigned x = 0; x < values.side; ++x)
                if (const auto at = changed.find(key(values, tile, x, z)); at != changed.end()) {
                    const auto i = std::size_t(z) * values.side + x;
                    found->heights[i] = at->second[0];
                    found->mask[i] = at->second[1];
                }
    }
    replace_raster(*this, values);
}
} // namespace forge
