#include "tile_set_document.hpp"
#include <cstdint>
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
    throw std::runtime_error("Expected Tile Set rejection");
}
Json image(std::uint64_t id, AssetId material) {
    return {{"id", id},
            {"name", "Original image"},
            {"material", material},
            {"grid",
             {{"columns", 4},
              {"rows", 1},
              {"margin_x", 0},
              {"margin_y", 0},
              {"spacing_x", 0},
              {"spacing_y", 0}}}};
}
Json tile(std::uint64_t id, std::uint64_t source, std::uint32_t frame) {
    return {{"id", id}, {"name", "Original tile"}, {"image", source}, {"frame", frame}};
}
TileSetSource example() {
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 4;
    source.document["images"].push_back(image(1, AssetId::generate()));
    source.document["tiles"] = Json::array({tile(2, 1, 0), tile(3, 1, 3)});
    source.document["opaque/~root"] = {{"wide", UINT64_MAX}, {"payload", {true, nullptr, "keep"}}};
    source.document["images"][0]["unknown_image"] = {1, 2, 3};
    source.document["images"][0]["grid"]["unknown_grid"] = "keep";
    source.document["tiles"][0]["unknown_tile"] = {false, "keep"};
    return source;
}
TileSetSource roundtrip(const TileSetSource& source) {
    const auto text = source.document.dump();
    return TileSetSource::parse(std::as_bytes(std::span(text)));
}
void source_values() {
    reject([] { (void)TileSetSource::create({}); });
    auto empty = TileSetSource::create(AssetId::generate());
    empty.validate();
    require(empty.values().images.empty() && empty.values().tiles.empty(), "Empty Tile Set failed");
    const auto source = example();
    source.validate();
    const auto restored = roundtrip(source);
    require(restored.asset() == source.asset() && restored.document == source.document,
            "Tile Set roundtrip changed logical ID or opaque data");
    const auto values = restored.values();
    require(values.next_id == 4 && values.images.size() == 1 && values.tiles.size() == 2 &&
                values.images[0].id == 1 && values.tiles[1].id == 3 && values.tiles[1].frame == 3,
            "Tile Set numeric definition projection differs");
    require(sprite_grid_region(64, 16, values.images[0].grid, values.tiles[1].frame) ==
                SpritePixelRegion{48, 0, 16, 16},
            "Tile Set layout does not reuse Sprite row-major pixel math");
    require(restored.materials() == std::vector{values.images[0].material},
            "Typed Tile Set Material references differ");
    auto duplicated = source;
    duplicated.document["images"].push_back(image(4, values.images[0].material.id));
    duplicated.document["tiles"].push_back(tile(5, 4, 1));
    duplicated.document["next_id"] = 6;
    duplicated.validate();
    require(duplicated.materials().size() == 1 && duplicated.values().images.size() == 2,
            "Material dependency deduplication collapsed source identities");
    auto wide = source;
    wide.document["next_id"] = UINT64_MAX;
    wide.document["images"][0]["id"] = UINT64_MAX - 2;
    wide.document["tiles"] = Json::array({tile(UINT64_MAX - 1, UINT64_MAX - 2, 0)});
    const auto decoded_wide = roundtrip(wide).values();
    require(decoded_wide.images[0].id == UINT64_MAX - 2 &&
                decoded_wide.tiles[0].id == UINT64_MAX - 1 && decoded_wide.next_id == UINT64_MAX,
            "Tile Set IDs passed through lossy floating-point conversion");
    auto maximum = empty;
    auto large_image = image(1, AssetId::generate());
    large_image["grid"]["columns"] = tile_set_definition_limit;
    maximum.document["images"].push_back(std::move(large_image));
    maximum.document["next_id"] = tile_set_definition_limit + 2;
    for (std::uint32_t i = 0; i < tile_set_definition_limit; ++i)
        maximum.document["tiles"].push_back(tile(i + 2, 1, i));
    maximum.validate();
    require(roundtrip(maximum).values().tiles.size() == tile_set_definition_limit,
            "Maximum Tile Set definition profile failed");
    maximum.document["tiles"].push_back(tile(tile_set_definition_limit + 2, 1, 0));
    maximum.document["next_id"] = tile_set_definition_limit + 3;
    reject([&] { maximum.validate(); });
    auto images = empty;
    for (std::uint64_t i = 1; i <= tile_set_image_limit; ++i)
        images.document["images"].push_back(image(i, AssetId::generate()));
    images.document["next_id"] = tile_set_image_limit + 1;
    images.validate();
    require(roundtrip(images).values().images.size() == tile_set_image_limit,
            "Maximum Tile Set image profile failed");
    images.document["images"].push_back(image(tile_set_image_limit + 1, AssetId::generate()));
    images.document["next_id"] = tile_set_image_limit + 2;
    reject([&] { images.validate(); });
}
void bad_values() {
    const auto baseline = example();
    const auto test = [&](const auto& mutation) {
        auto source = baseline;
        mutation(source.document);
        reject([&] { source.validate(); });
        reject([&] { (void)roundtrip(source); });
    };
    test([](auto& j) { j["version"] = 2; });
    test([](auto& j) { j["version"] = 1.; });
    test([](auto& j) { j["format"] = "other"; });
    test([](auto& j) { j["asset_id"] = nullptr; });
    test([](auto& j) { j["next_id"] = 0; });
    test([](auto& j) { j["next_id"] = -1; });
    test([](auto& j) { j["next_id"] = 4.; });
    test([](auto& j) { j["images"][0]["id"] = 0; });
    test([](auto& j) { j["images"][0]["material"] = nullptr; });
    test([](auto& j) { j["images"][0]["grid"]["columns"] = 0; });
    test([](auto& j) { j["images"][0]["grid"]["margin_x"] = -1; });
    test([](auto& j) { j["images"][0]["grid"]["spacing_y"] = 1.; });
    test([](auto& j) { j["images"][0]["grid"]["columns"] = UINT64_MAX; });
    test([](auto& j) {
        j["images"][0]["grid"]["columns"] = UINT32_MAX;
        j["images"][0]["grid"]["rows"] = 2;
    });
    test([](auto& j) { j["tiles"][0]["id"] = 1; });
    test([](auto& j) { j["tiles"][0]["id"] = 3; });
    test([](auto& j) { j["tiles"][0]["id"] = 4; });
    test([](auto& j) { j["tiles"][0]["image"] = 99; });
    test([](auto& j) { j["tiles"][0]["frame"] = 4; });
    test([](auto& j) { j["tiles"][0]["frame"] = -1; });
    test([](auto& j) { j["tiles"][0]["name"] = ""; });
    test([](auto& j) { j["tiles"][0]["name"] = std::string(129, 'x'); });
    test([](auto& j) { j["tiles"][0]["name"] = "line\nfeed"; });
    test([](auto& j) { j["opaque_large"] = std::string(tile_set_source_byte_limit, 'x'); });
    test([](auto& j) {
        auto nested = Json(1);
        for (int i = 0; i < 20; ++i)
            nested = {{"x", std::move(nested)}};
        j["opaque_deep"] = std::move(nested);
    });
    test([](auto& j) { j["opaque_many"] = Json(std::vector<int>(100001)); });
    auto text = baseline.document.dump();
    text.insert(1, "\"format\":\"forge.tile-set\",");
    reject([&] { (void)TileSetSource::parse(std::as_bytes(std::span(text))); });
    auto invalid_utf8 = baseline;
    invalid_utf8.document["tiles"][0]["name"] = std::string(1, char(0xff));
    reject([&] { invalid_utf8.validate(); });
}
void document(const std::filesystem::path& root) {
    auto lease = std::make_shared<ProjectLease>(root);
    auto doc = TileSetDocument::create(lease, "Original.tileset.json");
    const auto id = doc->source().asset();
    const auto material = AssetId::generate();
    doc->edit(doc->revision(), "Create original definition", [&](auto& j) {
        j["next_id"] = 3;
        j["images"].push_back(image(1, material));
        j["tiles"].push_back(tile(2, 1, 0));
        j["opaque/~root"] = {{"wide", UINT64_MAX}};
    });
    doc->undo();
    require(doc->source().values().next_id == 3 && doc->source().values().tiles.empty(),
            "Tile Set Undo reused allocated source IDs");
    const auto revision = doc->revision();
    reject([&] {
        doc->edit(revision, "Reuse ID",
                  [&](auto& j) { j["images"].push_back(image(1, material)); });
    });
    require(doc->revision() == revision && doc->can_redo(),
            "Rejected Tile Set edit changed history");
    doc->redo();
    reject([&] {
        doc->edit(doc->revision(), "Change ID kind", [&](auto& j) {
            j["tiles"].clear();
            j["images"].push_back(image(2, material));
        });
    });
    reject([&] {
        doc->edit(doc->revision(), "Backwards allocator", [](auto& j) { j["next_id"] = 2; });
    });
    doc->edit(doc->revision(), "Rename", [](auto& j) { j["tiles"][0]["name"] = "Renamed"; });
    require(doc->source().values().tiles[0].id == 2, "Rename changed Tile Set identity");
    doc->save();
    TileSetDocument reopened(lease, "Original.tileset.json");
    require(reopened.source().asset() == id && reopened.source().document == doc->source().document,
            "Tile Set Save/reopen changed identity, values or opaque fields");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a dedicated Tile Set test directory");
        source_values();
        bad_values();
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        document(root);
        std::filesystem::remove_all(root);
        std::cout << "Internal Tile Set source values, limits, identity/history and opaque "
                     "preservation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
