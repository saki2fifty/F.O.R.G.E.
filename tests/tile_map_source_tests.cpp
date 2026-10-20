#include "tile_map_document.hpp"
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected Tile Map source rejection");
}
TileMapValue paint(const TileMapValue& value, std::initializer_list<TileMapCellEdit> writes) {
    return value.paint({writes.begin(), writes.size()});
}
TileMapValue example() {
    auto value = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto root =
        Json::parse(paint(value, {{1, {-1, -1}, {UINT64_MAX, 7}}, {1, {32, 32}, {2, 0}}}).dump());
    root["opaque/~root"] = {{"wide", UINT64_MAX}, {"data", {true, nullptr, "keep"}}};
    root["layers"][0]["opaque_layer"] = {4, "keep"};
    root["chunks"]["1:-1:-1"]["opaque_chunk"] = {1, 2, 3};
    root["chunks"]["1:-1:-1"]["cells"][1023]["opaque_cell"] = {{"wide", UINT64_MAX}};
    return TileMapValue::parse(std::move(root));
}
TileMapValue roundtrip(const TileMapValue& value) {
    const auto text = value.dump();
    return TileMapSource::parse(std::as_bytes(std::span(text))).document;
}
void values_and_paint() {
    reject([] { TileMapValue{}.validate(); });
    reject([] { (void)TileMapValue::create({}, AssetId::generate()); });
    reject([] { (void)TileMapValue::create(AssetId::generate(), {}); });
    const auto source = example();
    require(roundtrip(source) == source && source.occupied() == 2 && source.chunk_count() == 2,
            "Tile Map source roundtrip changed identity, values or opaque data");
    require(source.cell(1, {-1, -1}) == TileMapCell{UINT64_MAX, 7} &&
                source.cell(1, {0, 0}) == TileMapCell{},
            "Tile Map wide tile key/negative/empty query differs");
    const auto baseline = source.dump();
    const auto changed =
        paint(source, {{1, {-1, -1}, {42, 3}}, {1, {-33, 32}, {7, 1}}, {1, {-33, 32}, {8, 2}}});
    require(source.dump() == baseline && changed.cell(1, {-33, 32}) == TileMapCell{8, 2},
            "Paint changed immutable baseline or lost duplicate-write ordering");
    auto json = Json::parse(changed.dump());
    require(json["chunks"]["1:-1:-1"]["cells"][1023]["opaque_cell"]["wide"] == UINT64_MAX &&
                json["chunks"]["1:-1:-1"]["opaque_chunk"] == Json({1, 2, 3}) &&
                json["opaque/~root"] == Json::parse(baseline)["opaque/~root"],
            "Painting lost unrecognized root/chunk/cell data");
    require(paint(source, {{1, {-1, -1}, {UINT64_MAX, 7}}}) == source &&
                paint(source, {{1, {9000, -9000}, {}}}) == source,
            "No-op paint/empty erase changed source value");
    const auto erased = paint(source, {{1, {-1, -1}, {}}, {1, {32, 32}, {}}});
    require(erased.occupied() == 0 && erased.chunk_count() == 1 &&
                Json::parse(erased.dump())["chunks"]["1:-1:-1"]["opaque_chunk"] == Json({1, 2, 3}),
            "Erase did not prune plain empty chunks or preserve opaque empty chunks");
    reject([&] { (void)paint(source, {{1, {0, 0}, {1, 0}}, {99, {1, 1}, {2, 0}}}); });
    reject([&] { (void)paint(source, {{1, {0, 0}, {1, 0}}, {1, {1, 1}, {2, 8}}}); });
    reject([&] { (void)paint(source, {{1, {0, 0}, {0, 1}}}); });
    reject([&] { (void)source.cell(99, {}); });
    std::vector<TileMapCellEdit> too_many(tile_map_edit_limit + 1, {1, {}, {1, 0}});
    reject([&] { (void)source.paint(too_many); });
    require(source.dump() == baseline, "Rejected paint batch changed adopted source");
    auto metadata = source.metadata();
    metadata["tile_set"] = AssetId::generate();
    reject([&] { (void)source.with_metadata(metadata); });
    require(erased.with_metadata(metadata).tile_set() != source.tile_set(),
            "Empty map cannot select a different Tile Set");
    const auto far =
        paint(source, {{1, {INT32_MIN, INT32_MAX}, {123, 0}}, {1, {16777216, 16777217}, {124, 0}}});
    require(roundtrip(far).cell(1, {INT32_MIN, INT32_MAX}) == TileMapCell{123, 0} &&
                roundtrip(far).cell(1, {16777216, 16777217}) == TileMapCell{124, 0},
            "Full signed/far local addresses lost exact cells");
}
void malformed() {
    const auto baseline = Json::parse(example().dump());
    const auto bad = [&](const auto& mutate) {
        auto json = baseline;
        mutate(json);
        reject([&] { (void)TileMapValue::parse(json); });
        const auto bytes = json.dump();
        reject([&] { (void)TileMapSource::parse(std::as_bytes(std::span(bytes))); });
    };
    bad([](auto& j) { j["format"] = "other"; });
    bad([](auto& j) { j["version"] = 1.; });
    bad([](auto& j) { j["asset_id"] = nullptr; });
    bad([](auto& j) { j["tile_set"] = nullptr; });
    bad([](auto& j) { j["next_id"] = 0; });
    bad([](auto& j) { j["next_id"] = 1; });
    bad([](auto& j) { j["cell_size"] = {0, 1}; });
    bad([](auto& j) { j["cell_size"] = {1e7, 1}; });
    bad([](auto& j) { j["cell_size"] = {"1", 1}; });
    bad([](auto& j) { j["layers"] = Json::array(); });
    bad([](auto& j) { j["layers"].push_back(j["layers"][0]); });
    bad([](auto& j) { j["layers"][0]["id"] = 1.; });
    bad([](auto& j) { j["layers"][0]["name"] = "invalid\nname"; });
    bad([](auto& j) { j["layers"][0]["visible"] = 1; });
    bad([](auto& j) { j["layers"][0]["order"] = UINT64_MAX; });
    bad([](auto& j) { j["chunks"]["1:-1:-1"]["cells"].erase(0); });
    bad([](auto& j) { j["chunks"]["1:-1:-1"]["cells"][1023]["tile"] = 0; });
    bad([](auto& j) { j["chunks"]["1:-1:-1"]["cells"][1023]["tile"] = -1; });
    bad([](auto& j) { j["chunks"]["1:-1:-1"]["cells"][1023]["tile"] = 1.; });
    bad([](auto& j) { j["chunks"]["1:-1:-1"]["cells"][1023]["flags"] = 8; });
    for (const auto* key : {"01:-1:-1", "1:-01:-1", "1:-0:-1", "1:0:", "1:0:0:0", "2:0:0",
                            "1:2147483647:0", "1:0:-2147483648"}) {
        bad([&](auto& j) {
            auto row = std::move(j["chunks"]["1:-1:-1"]);
            j["chunks"].erase("1:-1:-1");
            j["chunks"][key] = std::move(row);
        });
    }
    auto invalid_utf8 = baseline;
    invalid_utf8["layers"][0]["name"] = std::string(1, char(0xff));
    reject([&] { (void)TileMapValue::parse(std::move(invalid_utf8)); });
    auto duplicate = baseline.dump();
    duplicate.insert(1, "\"version\":1,");
    reject([&] { (void)TileMapSource::parse(std::as_bytes(std::span(duplicate))); });
    auto deep = baseline;
    Json nested = true;
    for (unsigned i = 0; i < 20; ++i)
        nested = {{"next", std::move(nested)}};
    deep["opaque_deep"] = std::move(nested);
    reject([&] { (void)TileMapValue::parse(std::move(deep)); });
}
void sparse_and_layer_limits() {
    const auto empty = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto metadata = empty.metadata();
    for (std::uint64_t id = 2; id <= tile_map_layer_limit; ++id)
        metadata["layers"].push_back(
            {{"id", id}, {"name", "Original layer"}, {"visible", true}, {"order", 0}});
    metadata["next_id"] = tile_map_layer_limit + 1;
    const auto layers = empty.with_metadata(metadata);
    require(roundtrip(layers).layers().size() == tile_map_layer_limit,
            "Maximum layer profile did not roundtrip");
    metadata["layers"].push_back(
        {{"id", tile_map_layer_limit + 1}, {"name", "Too many"}, {"visible", true}, {"order", 0}});
    metadata["next_id"] = tile_map_layer_limit + 2;
    reject([&] { (void)empty.with_metadata(metadata); });
    auto root = Json::parse(empty.dump());
    const Json chunk{{"cells", std::vector<Json>(tile_chunk_cells)}};
    for (std::size_t i = 0; i < tile_map_chunk_limit; ++i)
        root["chunks"]["1:" + std::to_string(i) + ":0"] = chunk;
    const auto sparse = TileMapValue::parse(std::move(root));
    require(sparse.chunk_count() == tile_map_chunk_limit && sparse.occupied() == 0,
            "Sparse chunk-capacity fixture differs");
    reject([&] { (void)paint(sparse, {{1, {-1, -1}, {1, 0}}}); });
    auto wide = empty.metadata();
    wide["next_id"] = UINT64_MAX;
    wide["layers"][0]["id"] = UINT64_MAX - 1;
    const auto value =
        paint(empty.with_metadata(wide), {{UINT64_MAX - 1, {-1, -1}, {UINT64_MAX, 0}}});
    const auto restored = roundtrip(value);
    require(restored.next_id() == UINT64_MAX &&
                restored.cell(UINT64_MAX - 1, {-1, -1}) == TileMapCell{UINT64_MAX, 0},
            "Wide layer/chunk/tile identity lost exact uint64 values");
}
TileMapValue dense() {
    auto root = Json::parse(TileMapValue::create(AssetId::generate(), AssetId::generate()).dump());
    for (int cy = -8; cy < 8; ++cy)
        for (int cx = -8; cx < 8; ++cx) {
            auto& chunk = root["chunks"]["1:" + std::to_string(cx) + ":" + std::to_string(cy)];
            chunk = {{"cells", std::vector<Json>(tile_chunk_cells, {{"tile", 1}, {"flags", 0}})}};
        }
    return TileMapValue::parse(std::move(root));
}
std::string bytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void history_and_limits(const std::filesystem::path& root) {
    const auto baseline = dense();
    require(baseline.occupied() == tile_map_cell_limit && baseline.chunk_count() == 256,
            "Dense first-profile fixture did not reach its cell capacity");
    reject([&] { (void)paint(baseline, {{1, {9999, 9999}, {1, 0}}}); });
    auto lease = std::make_shared<ProjectLease>(root);
    auto doc = TileMapDocument::create(lease, "Original.tilemap.json", [&](AssetId id) {
        auto metadata = baseline.metadata();
        metadata["asset_id"] = id;
        return TileMapSource{baseline.with_metadata(std::move(metadata))};
    });
    const auto file = root / "Original.tilemap.json";
    const auto disk = bytes(file), id = doc->source().asset().str();
    const auto initial = doc->source().document;
    for (unsigned step = 0; step < 16; ++step) {
        std::vector<TileMapCellEdit> writes;
        for (int y = -4; y < 4; ++y)
            for (int x = -4; x < 4; ++x)
                writes.push_back({1, {x + int(step), y}, {2 + step, step % 8}});
        doc->edit_source(doc->revision(), "Paint stroke", [&](auto& candidate) {
            candidate.document = candidate.document.paint(writes);
        });
        require(TileMapValue::history_weight(initial, doc->source().document) < 256 * 1024,
                "Paint history retained a whole dense map instead of changed chunks");
    }
    require(bytes(file) == disk, "Painting wrote source bytes before explicit Save");
    const auto final = doc->source().document;
    for (unsigned i = 0; i < 16; ++i) {
        require(doc->can_undo(), "Dense Tile Map lost bounded stroke history");
        doc->undo();
    }
    require(doc->source().document == initial && !doc->dirty(),
            "Dense Undo did not restore saved map");
    const auto revision = doc->revision();
    doc->edit_source(revision, "No-op", [](auto& candidate) {
        candidate.document = paint(candidate.document, {{1, {}, {1, 0}}});
    });
    require(doc->revision() == revision && doc->can_redo(), "No-op paint cleared Redo");
    for (unsigned i = 0; i < 16; ++i) {
        require(doc->can_redo(), "Dense Tile Map lost Redo history");
        doc->redo();
    }
    require(doc->source().document == final && doc->dirty(),
            "Dense Redo differs from painted state");
    doc->save();
    TileMapDocument reopened(lease, "Original.tilemap.json");
    require(reopened.source().asset().str() == id && reopened.source().document == final,
            "Dense Tile Map Save/reopen changed identity or cells");
    auto metadata = final.metadata();
    metadata["next_id"] = 3;
    metadata["layers"].push_back(
        {{"id", 2}, {"name", "Second layer"}, {"visible", true}, {"order", 1}});
    doc->edit_source(doc->revision(), "Add layer", [&](auto& candidate) {
        candidate.document = candidate.document.with_metadata(metadata);
    });
    doc->undo();
    require(doc->source().document.next_id() == 3 && doc->source().document.layers().size() == 1,
            "Layer Undo reused a previously allocated ID");
    reject([&] {
        doc->edit_source(doc->revision(), "Reuse layer ID", [&](auto& candidate) {
            candidate.document = candidate.document.with_metadata(metadata);
        });
    });
    require(doc->can_redo(), "Rejected layer reuse cleared Redo");
    doc->redo();
    doc->edit_source(doc->revision(), "Paint second instance layer", [](auto& candidate) {
        // Move an occupied cell to the new layer without exceeding total capacity.
        candidate.document = paint(candidate.document, {{1, {}, {}}, {2, {}, {99, 0}}});
    });
    require(doc->source().document.cell(2, {}) == TileMapCell{99, 0} &&
                doc->source().document.cell(1, {}) == TileMapCell{} && final.cell(1, {}).tile != 0,
            "Layer query isolation or immutable prior state changed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a dedicated Tile Map test directory");
        values_and_paint();
        malformed();
        sparse_and_layer_limits();
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        history_and_limits(root);
        std::filesystem::remove_all(root);
        std::cout << "Internal Tile Map source, immutable paint, dense history, identity and "
                     "source limits passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
