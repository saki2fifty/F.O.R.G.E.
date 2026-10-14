#include <cmath>
#include <forge/terrain_source.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected terrain input rejection");
}
void near(double a, double b, double tolerance = 1e-6) {
    require(std::abs(a - b) <= tolerance, "Terrain differs from independent numeric expectation");
}
const std::vector<float>& stream(const MeshPart& part, std::string_view semantic) {
    for (const auto& value : part.streams)
        if (value.semantic == semantic)
            return std::get<std::vector<float>>(value.values);
    throw std::runtime_error("Expected terrain mesh stream");
}
TerrainRaster plane() {
    auto source = TerrainSource::create(AssetId::generate(), 3, 2);
    auto result = source.raster();
    for (unsigned z = 0; z < 3; ++z)
        for (unsigned x = 0; x < 3; ++x)
            result.tiles[0].layers[0].heights[z * 3 + x] = float(x + 2 * z);
    return result;
}
void geometry() {
    const auto raster = plane();
    const auto mesh = terrain_mesh(raster);
    require(mesh.lods.size() == 1 && mesh.lods[0].parts.size() == 1 && mesh.material_slots == 1,
            "Terrain did not use one ordinary mesh/material path");
    const auto& part = mesh.lods[0].parts[0];
    const auto& p = stream(part, "POSITION");
    const auto& n = stream(part, "NORMAL");
    const auto& t = stream(part, "TANGENT");
    const auto& uv = stream(part, "TEXCOORD_0");
    require(part.vertices == 9 && part.indices.size() == 24, "Terrain grid topology count wrong");
    const auto normal_length = std::sqrt(6.);
    for (unsigned z = 0; z < 3; ++z)
        for (unsigned x = 0; x < 3; ++x) {
            const auto i = z * 3 + x;
            near(p[i * 3], x);
            near(p[i * 3 + 1], x + 2 * z);
            near(p[i * 3 + 2], z);
            near(n[i * 3], -1 / normal_length);
            near(n[i * 3 + 1], 1 / normal_length);
            near(n[i * 3 + 2], -2 / normal_length);
            near(t[i * 4], 1 / std::sqrt(2.));
            near(t[i * 4 + 1], 1 / std::sqrt(2.));
            near(t[i * 4 + 2], 0);
            near(t[i * 4 + 3], -1);
            near(uv[i * 2], x * .5);
            near(uv[i * 2 + 1], z * .5);
        }
    for (std::size_t i = 0; i < part.indices.size(); i += 3) {
        const auto a = part.indices[i] * 3, b = part.indices[i + 1] * 3,
                   c = part.indices[i + 2] * 3;
        const auto upward =
            (p[b + 2] - p[a + 2]) * (p[c] - p[a]) - (p[b] - p[a]) * (p[c + 2] - p[a + 2]);
        require(upward > 0, "Terrain triangle faces below its surface");
    }
    auto bytes = encode_mesh(mesh);
    require(encode_mesh(decode_mesh(bytes)) == bytes, "Terrain mesh codec lost geometry");
    for (double z : {0., .25, 1., 1.75, 2.})
        for (double x : {0., .25, 1., 1.75, 2.})
            near(*terrain_height(raster, x, z), x + 2 * z);
    require(!terrain_height(raster, -.01, 1) && !terrain_height(raster, 1, 2.01),
            "Terrain query crossed outer boundary");
    auto triangle = TerrainSource::create(AssetId::generate(), 2, 1).raster();
    triangle.tiles[0].layers[0].heights = {0, 1, 2, 4};
    near(*terrain_height(triangle, .5, .5), 1.5); // bilinear would incorrectly give 1.75
    near(*terrain_height(triangle, .75, .75), 2.75);
    reject([&] { terrain_height(raster, std::numeric_limits<double>::infinity(), 0); });
}
void layers_and_seams() {
    auto raster = plane();
    auto tile = raster.tiles.front();
    tile.id = TerrainTileId::generate();
    tile.coordinate = {1, 0};
    for (unsigned z = 0; z < 3; ++z)
        for (unsigned x = 0; x < 3; ++x)
            tile.layers[0].heights[z * 3 + x] = float(2 + x + 2 * z);
    raster.tiles.push_back(tile);
    const auto mesh = terrain_mesh(raster);
    for (const auto name : {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0"}) {
        const auto& a = stream(mesh.lods[0].parts[0], name);
        const auto& b = stream(mesh.lods[0].parts[1], name);
        const unsigned width = std::string_view(name) == "TANGENT"      ? 4
                               : std::string_view(name) == "TEXCOORD_0" ? 2
                                                                        : 3;
        for (unsigned z = 0; z < 3; ++z)
            for (unsigned c = 0; c < width; ++c)
                require(a[(z * 3 + 2) * width + c] == b[z * 3 * width + c],
                        "Terrain seam geometry is discontinuous");
    }
    near(*terrain_height(raster, 2, 1), 4);
    auto layer = TerrainLayer{TerrainLayerId::generate(), "Detail", true, .5f};
    raster.layers.push_back(layer);
    for (auto& at : raster.tiles)
        at.layers.push_back({layer.id, std::vector<float>(9, 4), std::vector<float>(9, .5f)});
    near(*terrain_height(raster, 1, 1), 4);
    raster.layers[1].enabled = false;
    near(*terrain_height(raster, 1, 1), 3);
    auto bad = raster;
    bad.tiles[1].layers[1].heights[0] = 1; // disabled source data still owns the border
    reject([&] { terrain_mesh(bad); });
    bad = raster;
    bad.tiles[1].layers[1].mask[0] = .75f;
    reject([&] { validate_terrain_raster(bad); });
    bad = raster;
    bad.tiles[1].layers[0].layer = layer.id;
    reject([&] { validate_terrain_raster(bad); });
    bad = raster;
    bad.tiles[1].coordinate = {0, 0};
    reject([&] { validate_terrain_raster(bad); });
    bad = raster;
    bad.tiles[1].id = bad.tiles[0].id;
    reject([&] { validate_terrain_raster(bad); });
    raster.tiles.pop_back();
    tile = raster.tiles.front();
    tile.id = TerrainTileId::generate();
    tile.coordinate = {1, 1};
    tile.layers[0].heights.assign(9, 6); // diagonal corner alone is also shared
    raster.tiles.push_back(tile);
    validate_terrain_raster(raster);
    raster.tiles[1].layers[0].heights[0] = 5;
    reject([&] { validate_terrain_raster(raster); });
}
void float_boundaries() {
    auto raster = TerrainSource::create(AssetId::generate(), 17, 1.3).raster();
    raster.tiles[0].coordinate = {-2, 3};
    const auto mesh = terrain_mesh(raster);
    const auto& p = stream(mesh.lods[0].parts[0], "POSITION");
    for (unsigned i = 0; i < 17 * 17; ++i) {
        near(*terrain_height(raster, p[i * 3], p[i * 3 + 2]), 0);
    }
    require(!terrain_height(raster, 0, 0), "Sparse terrain query filled an absent tile");
    raster.tile_size = 1000;
    raster.tiles[0].coordinate = {16, -16};
    raster.side = 129;
    raster.tiles[0].layers[0].heights.assign(129 * 129, 0);
    raster.tiles[0].layers[0].mask.assign(129 * 129, 1);
    require(terrain_mesh(raster).lods[0].parts[0].vertices == 129 * 129,
            "Bounded maximum resolution rejected");
}
void edits_and_brushes() {
    auto source = TerrainSource::create(AssetId::generate(), 3, 2);
    source.document["extensions"] = {{"author", {{"opaque", 7}}}};
    const auto root = source.asset();
    const auto tile = source.raster().tiles[0].id;
    const auto layer = source.raster().layers[0].id;
    const std::array<MeshBrushSample, 1> center{{{{1, 17, 1}, .5}}};
    source.brush(layer, center, 1, TerrainBrush::Raise, 4);
    near(source.raster().tiles[0].layers[0].heights[4], 2);
    near(source.raster().tiles[0].layers[0].heights[3], 0);
    // The projected footprint ignores Y, and repeated identical samples don't
    // amplify a recorded stroke when cursor delivery density changes.
    auto duplicated = TerrainSource::create(AssetId::generate(), 3, 2);
    const auto dlayer = duplicated.raster().layers[0].id;
    const std::array<MeshBrushSample, 2> dense{{center[0], center[0]}};
    duplicated.brush(dlayer, dense, 1, TerrainBrush::Raise, 4);
    require(duplicated.raster().tiles[0].layers[0].heights ==
                source.raster().tiles[0].layers[0].heights,
            "Terrain stroke density changed its strength");
    source.brush(layer, center, 1, TerrainBrush::Flatten, 1, 6);
    near(source.raster().tiles[0].layers[0].heights[4], 4);
    source.brush(layer, center, 1, TerrainBrush::Smooth, 1);
    near(source.raster().tiles[0].layers[0].heights[4], 2);
    source.brush(layer, center, 1, TerrainBrush::Mask, 1, 0);
    near(source.raster().tiles[0].layers[0].mask[4], .5);
    near(*terrain_height(source.raster(), 1, 1), 1);
    const auto adjoining = source.add_tile({1, 0});
    const std::array<MeshBrushSample, 1> seam{{{{2, 0, 1}, 1}}};
    source.brush(layer, seam, .5, TerrainBrush::Raise, 3);
    auto values = source.raster();
    near(values.tiles[0].layers[0].heights[5], 3);
    require(values.tiles[0].layers[0].heights[5] == values.tiles[1].layers[0].heights[3],
            "Stroke tore tile seam");
    source.resample(5);
    values = source.raster();
    require(values.tiles[0].id == tile && values.tiles[1].id == adjoining &&
                values.layers[0].id == layer && source.asset() == root,
            "Resampling changed terrain identities");
    require(source.document.at("extensions").at("author").at("opaque") == 7,
            "Edit discarded opaque source data");
    near(*terrain_height(values, 2, 1), 3);
    source.resample(3);
    near(source.raster().tiles[0].layers[0].heights[5], 3);
    const auto detail = source.add_layer("Detail");
    source.brush(detail, seam, .5, TerrainBrush::Raise, 2);
    near(*terrain_height(source.raster(), 2, 1), 5);
    source.set_layer(detail, true, .5f);
    near(*terrain_height(source.raster(), 2, 1), 4);
    source.set_layer(detail, false, .5f);
    near(*terrain_height(source.raster(), 2, 1), 3);
    const auto prior = source.document;
    for (const auto action : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        reject([&] {
            switch (action) {
            case 0:
                source.add_tile({1, 0});
                break;
            case 1:
                source.add_tile({17, 0});
                break;
            case 2:
                source.remove_tile(TerrainTileId::generate());
                break;
            case 3:
                source.remove_layer(TerrainLayerId::generate());
                break;
            case 4:
                source.resample(130);
                break;
            case 5:
                source.set_layer(layer, true, 1.1f);
                break;
            case 6:
                source.brush(layer, seam, 0, TerrainBrush::Raise, 1);
                break;
            case 7:
                source.brush(layer, seam, 1, TerrainBrush::Mask, 1, 2);
                break;
            case 8:
                source.brush(layer, seam, .5, TerrainBrush::Raise, 10000);
                break;
            }
        });
        require(source.document == prior, "Rejected terrain edit changed the document");
    }
    auto no_pressure = seam;
    no_pressure[0].pressure = 0;
    source.brush(layer, no_pressure, 1, TerrainBrush::Raise, 100);
    require(source.document == prior, "Zero pressure changed terrain");
    source.remove_layer(detail);
    source.remove_tile(adjoining);
    const auto one = source.document;
    reject([&] { source.remove_tile(tile); });
    reject([&] { source.remove_layer(layer); });
    require(source.document == one, "Removing last layer/tile changed terrain");
    // Exact endpoint resampling retains tiny raw samples instead of cancelling
    // them beside a large adjacent sample on only one side of the shared edge.
    source.document["tiles"][0]["layers"][0]["heights"] =
        std::vector<float>{10000, 10000, 1e-30f, 10000, 10000, 1e-30f, 10000, 10000, 1e-30f};
    source.add_tile({1, 0});
    source.resample(5);
    values = source.raster();
    require(values.tiles[0].layers[0].heights[4] == 1e-30f &&
                values.tiles[1].layers[0].heights[0] == 1e-30f,
            "Resampling lost exact small endpoint or seam correspondence");
    auto mirrored = TerrainSource::create(AssetId::generate(), 3, 2);
    mirrored.add_tile({-1, 0});
    const auto mlayer = mirrored.raster().layers[0].id;
    mirrored.brush(mlayer, std::array<MeshBrushSample, 1>{{{{1, 0, 1}, 1}}}, .5,
                   TerrainBrush::Raise, 2, 0, MeshBrushSymmetry::X);
    near(*terrain_height(mirrored.raster(), 1, 1), 2);
    near(*terrain_height(mirrored.raster(), -1, 1), 2);
    auto invalid = center;
    invalid[0].pressure = 1.1;
    reject([&] { mirrored.brush(mlayer, invalid, 1, TerrainBrush::Raise, 1); });
}
void source_admission() {
    auto source = TerrainSource::create(AssetId::generate(), 3, 2);
    const auto original = source.document;
    auto parse = [](const Json& doc) {
        auto text = doc.dump();
        return TerrainSource::parse(std::as_bytes(std::span(text)));
    };
    require(parse(original).document == original, "Terrain source round trip changed durable data");
    auto extended = original;
    extended["extensions"] = {{"vendor", {{"opaque", 42}}}};
    require(parse(extended).document == extended, "Terrain opaque extensions were lost");
    for (const auto field : {"version", "side"}) {
        auto bad = original;
        bad[field] = 1.5;
        reject([&] { parse(bad); });
        bad[field] = true;
        reject([&] { parse(bad); });
    }
    for (const auto field : {"format", "profile", "asset_id"}) {
        auto bad = original;
        bad[field] = "invalid";
        reject([&] { parse(bad); });
    }
    for (const auto side : {0, 1, 130}) {
        auto bad = original;
        bad["side"] = side;
        reject([&] { parse(bad); });
    }
    auto bad = original;
    bad["tiles"][0]["coordinate"][0] = .25;
    reject([&] { parse(bad); });
    bad = original;
    bad["tiles"][0]["coordinate"][0] = 17;
    reject([&] { parse(bad); });
    bad = original;
    bad["tiles"][0]["layers"][0]["heights"].erase(0);
    reject([&] { parse(bad); });
    bad = original;
    bad["layers"][0]["enabled"] = 1;
    reject([&] { parse(bad); });
    bad = original;
    bad["tiles"][0]["layers"][0]["mask"][0] = -1;
    reject([&] { parse(bad); });
    bad = original;
    bad["tiles"][0]["layers"][0]["heights"][0] = 10001;
    reject([&] { parse(bad); });
    bad = original;
    bad["layers"][0]["strength"] = 1.001;
    reject([&] { parse(bad); });
    bad = original;
    bad["layers"] = Json::array();
    reject([&] { parse(bad); });
    bad = original;
    bad["tiles"] = Json::array();
    reject([&] { parse(bad); });
    bad = original;
    bad["unknown"] = 0;
    reject([&] { parse(bad); });
    bad = original;
    bad["tiles"][0]["unknown"] = 0;
    reject([&] { parse(bad); });
    auto duplicate = original.dump();
    duplicate.insert(1, "\"side\":3,");
    reject([&] { TerrainSource::parse(std::as_bytes(std::span(duplicate))); });
    auto huge = std::string(terrain_source_byte_limit + 1, ' ');
    reject([&] { TerrainSource::parse(std::as_bytes(std::span(huge))); });
    bad = original;
    Json nested = 0;
    for (unsigned i = 0; i < 20; ++i)
        nested = Json::array({nested});
    bad["extensions"] = {{"nested", nested}};
    reject([&] { parse(bad); });
    auto raster = plane();
    raster.layers[0].strength = std::numeric_limits<float>::quiet_NaN();
    reject([&] { validate_terrain_raster(raster); });
    raster = plane();
    raster.tiles[0].layers[0].heights[0] = std::numeric_limits<float>::infinity();
    reject([&] { validate_terrain_raster(raster); });
    raster = plane();
    auto layer = raster.layers[0];
    layer.id = TerrainLayerId::generate();
    raster.layers.push_back(layer);
    raster.tiles[0].layers.push_back(
        {layer.id, std::vector<float>(9, 10000), std::vector<float>(9, 1)});
    reject([&] { validate_terrain_raster(raster); });
}
} // namespace
int main() {
    try {
        geometry();
        layers_and_seams();
        float_boundaries();
        source_admission();
        edits_and_brushes();
        std::cout << "Terrain source/geometry passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
