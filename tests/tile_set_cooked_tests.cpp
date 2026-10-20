#include "cooked_envelope.hpp"
#include "tile_set_cook.hpp"
#include <iostream>

using namespace forge;
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected cooked Tile Set rejection");
}
Json image(std::uint64_t id, AssetId material, unsigned columns = 4, unsigned rows = 1) {
    return {{"id", id},
            {"name", "Original image"},
            {"material", material},
            {"grid",
             {{"columns", columns},
              {"rows", rows},
              {"margin_x", 0},
              {"margin_y", 0},
              {"spacing_x", 0},
              {"spacing_y", 0}}}};
}
Json tile(std::uint64_t id, std::uint64_t source, unsigned frame) {
    return {{"id", id}, {"name", "Original tile"}, {"image", source}, {"frame", frame}};
}
TileSetSource example() {
    auto source = TileSetSource::create(AssetId::generate());
    const auto material = AssetId::generate();
    source.document["next_id"] = 10;
    source.document["images"] = Json::array({image(5, material, 2, 2), image(1, material)});
    source.document["tiles"] = Json::array({tile(9, 5, 3), tile(7, 1, 2)});
    source.document["opaque"] = "SOURCE_ONLY_TILESET";
    source.document["images"][0]["opaque"] = "SOURCE_ONLY_TILESET";
    source.document["images"][0]["grid"]["opaque"] = "SOURCE_ONLY_TILESET";
    source.document["tiles"][0]["opaque"] = "SOURCE_ONLY_TILESET";
    return source;
}
void values() {
    const auto source = example();
    const auto baseline = source.document;
    const auto bytes = cook_tile_set(source);
    const auto data = decode_cooked_tile_set(bytes, source.asset());
    require(source.document == baseline && source.values().images[0].id == 5 &&
                data.images()[0].id == 1 && data.tiles()[0].id == 7 &&
                data.asset() == source.asset(),
            "Cooking mutated authoring order/values or failed numeric-key ordering");
    require(data.tile(7).image == 1 && data.tile(7).frame == 2 &&
                sprite_grid_region(64, 16, data.image(1).grid, data.tile(7).frame) ==
                    SpritePixelRegion{32, 0, 16, 16},
            "Runtime tile/image/frame lookup differs from shared Sprite grid math");
    require(data.materials() == source.materials() && data.materials().size() == 1 &&
                data.images().size() == 2,
            "Runtime Material deduplication changed image-entry identity");
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(text.find("SOURCE_ONLY_TILESET") == std::string::npos &&
                text.find("next_id") == std::string::npos,
            "Cooked Tile Set retained authoring data");
    auto reordered = source;
    std::reverse(reordered.document["images"].begin(), reordered.document["images"].end());
    std::reverse(reordered.document["tiles"].begin(), reordered.document["tiles"].end());
    reordered.document["next_id"] = 100;
    require(cook_tile_set(reordered) == bytes,
            "Source order/allocator changed runtime cooked bytes");
    const auto copy = data;
    require(copy.images().data() == data.images().data() &&
                copy.tiles().data() == data.tiles().data(),
            "Immutable runtime Tile Set copy duplicated row storage");
    reject([&] { (void)data.tile(1); });
    reject([&] { (void)data.image(7); });
    reject([] { TileSetData{}.validate(); });
    const auto empty = TileSetSource::create(AssetId::generate());
    require(decode_cooked_tile_set(cook_tile_set(empty), empty.asset()).tiles().empty(),
            "Empty Tile Set cooked profile failed");
    auto wide = empty;
    wide.document["next_id"] = UINT64_MAX;
    wide.document["images"] = Json::array({image(UINT64_MAX - 2, AssetId::generate())});
    wide.document["tiles"] = Json::array({tile(UINT64_MAX - 1, UINT64_MAX - 2, 3)});
    const auto restored = decode_cooked_tile_set(cook_tile_set(wide), wide.asset());
    require(restored.tile(UINT64_MAX - 1).image == UINT64_MAX - 2 &&
                restored.image(UINT64_MAX - 2).grid.columns == 4,
            "Cooked Tile Set local keys lost exact uint64 values");
}
void malformed() {
    const auto source = example();
    const auto bytes = cook_tile_set(source);
    const auto envelope = asset_detail::decode_envelope(bytes, tile_set_cooked_magic,
                                                        tile_set_cooked_metadata_limit, 0);
    const auto bad = [&](const auto& mutate) {
        auto metadata = envelope.metadata;
        mutate(metadata);
        const auto candidate = asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                                             tile_set_cooked_metadata_limit);
        reject([&] { (void)decode_cooked_tile_set(candidate, source.asset()); });
    };
    reject([&] { (void)decode_cooked_tile_set(bytes, {}); });
    reject([&] { (void)decode_cooked_tile_set(bytes, AssetId::generate()); });
    for (std::size_t n = 0; n < bytes.size(); ++n)
        reject([&] { (void)decode_cooked_tile_set(std::span(bytes).first(n), source.asset()); });
    auto extra = bytes;
    extra.push_back(std::byte{0});
    reject([&] { (void)decode_cooked_tile_set(extra, source.asset()); });
    bad([](auto& m) { m["next_id"] = 10; });
    bad([](auto& m) { m["images"][0]["id"] = UINT64_MAX; });
    bad([](auto& m) { m["images"][0]["id"] = 7; });
    bad([](auto& m) { m["images"][0]["material"] = nullptr; });
    bad([](auto& m) { m["images"][0]["grid"]["columns"] = 0; });
    bad([](auto& m) { m["images"][0]["grid"]["rows"] = 1.; });
    bad([](auto& m) { m["images"][0]["grid"]["opaque"] = true; });
    bad([](auto& m) { m["images"][0]["opaque"] = true; });
    bad([](auto& m) { m["tiles"][0]["frame"] = 99; });
    bad([](auto& m) { m["tiles"][0]["frame"] = 2.; });
    bad([](auto& m) { m["tiles"][0]["image"] = 123; });
    bad([](auto& m) { m["tiles"][0]["opaque"] = true; });
}
void maximum() {
    auto source = TileSetSource::create(AssetId::generate());
    const auto material = AssetId::generate();
    source.document["next_id"] = tile_set_image_limit + tile_set_definition_limit + 1;
    for (unsigned i = 0; i < tile_set_image_limit; ++i)
        source.document["images"].push_back(image(i + 1, material, 256));
    for (unsigned i = 0; i < tile_set_definition_limit; ++i) {
        auto row = tile(tile_set_image_limit + i + 1, i % 16 + 1, i % 256);
        row["name"] = std::string(128, 'x');
        source.document["tiles"].push_back(std::move(row));
    }
    const auto bytes = cook_tile_set(source);
    const auto data = decode_cooked_tile_set(bytes, source.asset());
    require(data.images().size() == 16 && data.tiles().size() == 4096 &&
                bytes.size() < 1024 * 1024 && data.resident_bytes() < 1024 * 1024,
            "Maximum Tile Set profile exceeds cooked/retained estimate envelope");
    for (unsigned i = 0; i < tile_set_definition_limit; ++i)
        require(data.tile(17 + i).image == i % 16 + 1 && data.tile(17 + i).frame == i % 256,
                "Maximum Tile Set lookups differ from independent numeric-key oracle");
    std::cout << "maximum_tile_set_cooked_bytes=" << bytes.size()
              << " retained_estimate_bytes=" << data.resident_bytes() << '\n';
}
} // namespace
int main() {
    try {
        values();
        malformed();
        maximum();
        std::cout << "Internal cooked Tile Set values, shared source admission and local-key "
                     "lookup passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
