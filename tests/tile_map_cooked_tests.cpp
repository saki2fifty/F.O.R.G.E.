#include "cooked_envelope.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_source_view.hpp"
#include <iostream>
#include <set>

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
    throw std::runtime_error("Expected malformed cooked Tile Map rejection");
}
TileMapValue example() {
    auto source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    const std::array<TileMapCellEdit, 2> cells{
        {{1, {-1, -1}, {UINT64_MAX, 7}}, {1, {32, 32}, {0x0102030405060708, 0}}}};
    auto root = Json::parse(source.paint(cells).dump());
    root["opaque_root"] = "AUTHORING_ONLY_NEVER_COOK";
    root["layers"][0]["opaque_layer"] = "AUTHORING_ONLY_NEVER_COOK";
    root["chunks"]["1:-1:-1"]["opaque_chunk"] = "AUTHORING_ONLY_NEVER_COOK";
    root["chunks"]["1:-1:-1"]["cells"][1023]["opaque_cell"] = "AUTHORING_ONLY_NEVER_COOK";
    root["chunks"]["1:8:8"] = {{"cells", std::vector<Json>(1024)},
                               {"opaque_empty", "AUTHORING_ONLY_NEVER_COOK"}};
    return TileMapValue::parse(std::move(root));
}
void set32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(at + i) = std::byte((value >> (i * 8)) & 255);
}
auto envelope(std::span<const std::byte> bytes) {
    return asset_detail::decode_envelope(bytes, tile_map_cooked_magic,
                                         tile_map_cooked_metadata_limit,
                                         tile_map_cooked_payload_limit);
}
void roundtrip_and_view() {
    const auto empty = TileMapValue::create(AssetId::generate(), AssetId::generate());
    const auto empty_data = decode_cooked_tile_map(cook_tile_map(empty), empty.asset());
    require(empty_data.chunk_count() == 0 && empty_data.occupied() == 0 &&
                empty_data.cell(1, {}) == TileMapCell{},
            "Empty cooked Tile Map admission differs");
    const auto source = example();
    const auto bytes = cook_tile_map(source);
    const auto data = decode_cooked_tile_map(bytes, source.asset());
    require(data.asset() == source.asset() && data.tile_set() == source.tile_set() &&
                data.cell_size() == source.cell_size() && data.layers() == source.layers() &&
                data.occupied() == 2 && data.chunk_count() == 2 && source.chunk_count() == 3,
            "Cooked map did not preserve runtime values or omit opaque empty source chunks");
    require(data.cell(1, {-1, -1}) == TileMapCell{UINT64_MAX, 7} &&
                data.cell(1, {32, 32}) == TileMapCell{0x0102030405060708, 0} &&
                data.cell(1, {}) == TileMapCell{},
            "Cooked cell address/value query differs from source");
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(text.find("AUTHORING_ONLY_NEVER_COOK") == std::string::npos &&
                !envelope(bytes).metadata.contains("next_id"),
            "Cooked runtime artifact retained authoring payload/allocator metadata");
    const auto saved = source.dump(2);
    require(cook_tile_map(TileMapSource::parse(std::as_bytes(std::span(saved))).document) == bytes,
            "Source formatting/reopen changed deterministic cooked output");
    const auto payload = envelope(bytes).payload;
    require(payload.size() == 72 && payload[0] == std::byte{1} && payload[8] == std::byte{255} &&
                payload[11] == std::byte{255} && payload[20] == std::byte{255} &&
                payload[21] == std::byte{3} && payload[60] == std::byte{8} &&
                payload[67] == std::byte{1},
            "Cooked payload does not follow independent little-endian record examples");
    Camera camera;
    camera.projection = std::uint32_t(CameraProjection::Orthographic);
    camera.orthographic_height = 20;
    AffineTransform camera_world;
    camera_world.m[3] = camera_world.m[7] = -16;
    camera_world.m[11] = -5;
    const std::array<CameraView, 1> cameras{camera_view(camera, camera_world, 400, 400)};
    std::set<TileMapChunkKey> source_keys, cooked_keys;
    visit_visible_tile_chunks(source, {}, cameras,
                              [&](const auto& chunk) { source_keys.insert(chunk.key); });
    visit_visible_tile_chunks(data, {}, cameras,
                              [&](const auto& chunk) { cooked_keys.insert(chunk.key); });
    require(source_keys == cooked_keys && cooked_keys.size() == 1,
            "Authoring/runtime data did not share the same read-only culling view");
    auto copy = data;
    std::map<TileMapChunkKey, const TileMapCell*> pointers;
    data.for_each_chunk([&](auto key, auto cells, auto) { pointers[key] = cells.data(); });
    copy.for_each_chunk([&](auto key, auto cells, auto) {
        require(pointers.at(key) == cells.data(), "Immutable runtime copy duplicated cell storage");
    });
    reject([&] { (void)data.cell(2, {}); });
    reject([] { TileMapData{}.validate(); });
    auto moved = std::move(copy);
    moved.validate();
    reject([&] { copy.validate(); });
}
void malformed() {
    const auto source = example();
    const auto bytes = cook_tile_map(source);
    const auto bad = [&](const auto& mutation) {
        auto changed = bytes;
        mutation(changed);
        reject([&] { (void)decode_cooked_tile_map(changed, source.asset()); });
    };
    reject([&] { (void)decode_cooked_tile_map(bytes, {}); });
    reject([&] { (void)decode_cooked_tile_map(bytes, AssetId::generate()); });
    for (std::size_t i = 0; i < bytes.size(); ++i)
        reject([&] { (void)decode_cooked_tile_map(std::span(bytes).first(i), source.asset()); });
    bad([](auto& b) { b[0] = std::byte{0}; });
    bad([](auto& b) { set32(b, 8, 0); });
    bad([](auto& b) { set32(b, 8, 2); });
    bad([](auto& b) { set32(b, 20, 1); });
    bad([](auto& b) { b.push_back(std::byte{0}); });
    const auto parsed = envelope(bytes);
    const auto offset = std::size_t(parsed.payload.data() - bytes.data());
    bad([&](auto& b) { set32(b, offset + 20, 1024); });
    bad([&](auto& b) { set32(b, offset + 32, 8); });
    bad([&](auto& b) { set32(b, offset, 0); });
    bad([&](auto& b) { set32(b, offset + 8, INT32_MAX); });
    bad([&](auto& b) { set32(b, offset + 16, 0); });
    bad([&](auto& b) { set32(b, offset + 16, 2); });
    bad([&](auto& b) {
        set32(b, offset + 24, 0);
        set32(b, offset + 28, 0);
    });
    bad([&](auto& b) {
        for (unsigned i = 0; i < 16; ++i)
            b[offset + 36 + i] = b[offset + i];
    });
    const auto metadata_bad = [&](const auto& mutation) {
        auto metadata = parsed.metadata;
        mutation(metadata);
        const auto encoded = asset_detail::encode_envelope(
            metadata, parsed.payload, tile_map_cooked_magic, tile_map_cooked_metadata_limit);
        reject([&] { (void)decode_cooked_tile_map(encoded, source.asset()); });
    };
    metadata_bad([](auto& m) { m["cell_count"] = 3; });
    metadata_bad([](auto& m) { m["chunk_count"] = 1; });
    metadata_bad([](auto& m) { m["cell_count"] = 2.; });
    metadata_bad([](auto& m) { m["tile_set"] = nullptr; });
    metadata_bad([](auto& m) { m["cell_size"] = {0, 1}; });
    metadata_bad([](auto& m) { m["layers"][0]["id"] = UINT64_MAX; });
    metadata_bad([](auto& m) { m["layers"][0]["order"] = UINT64_MAX; });
    metadata_bad([](auto& m) { m["layers"][0]["visible"] = 1; });
    metadata_bad([](auto& m) { m["layers"][0]["opaque"] = "reject"; });
    metadata_bad([](auto& m) { m["opaque"] = "reject"; });
}
void maximum() {
    auto root = Json::parse(TileMapValue::create(AssetId::generate(), AssetId::generate()).dump());
    Json chunk{{"cells", std::vector<Json>(1024)}};
    for (unsigned i = 0; i < 256; ++i)
        chunk["cells"][i] = {{"tile", i + 1}, {"flags", i % 8}};
    for (int x = -512; x < 512; ++x)
        root["chunks"]["1:" + std::to_string(x) + ":0"] = chunk;
    const auto source = TileMapValue::parse(std::move(root));
    const auto bytes = cook_tile_map(source);
    const auto data = decode_cooked_tile_map(bytes, source.asset());
    require(data.chunk_count() == 1024 && data.occupied() == 262144 &&
                envelope(bytes).payload.size() == tile_map_cooked_payload_limit &&
                bytes.size() < 5 * 1024 * 1024 && data.resident_bytes() < 17 * 1024 * 1024,
            "Maximum-profile cooked bytes/resident estimate exceeds its measured envelope");
    for (int x = -512; x < 512; ++x)
        require(data.cell(1, {x * 32, 0}) == TileMapCell{1, 0} &&
                    data.cell(1, {x * 32 + 31, 7}) == TileMapCell{256, 7} &&
                    data.cell(1, {x * 32, 8}) == TileMapCell{},
                "Maximum-profile runtime cells differ from independent address/value oracle");
    std::cout << "maximum_cooked_bytes=" << bytes.size()
              << " retained_estimate_bytes=" << data.resident_bytes() << '\n';
}
} // namespace
int main() {
    try {
        roundtrip_and_view();
        malformed();
        maximum();
        std::cout << "Internal cooked Tile Map values, binary admission and shared view passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
