#include "tile_asset_worker.hpp"
#include "asset_bytes.hpp"
#include "tile_map_cook.hpp"
#include "tile_set_cook.hpp"
#include <algorithm>

namespace forge::asset_detail {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
const ArtifactFile& input(const ImportProcessRequest& request, const char* name) {
    require(std::count_if(request.inputs.begin(), request.inputs.end(),
                          [&](const auto& row) { return row.name == name; }) == 1,
            "Tile worker input is missing or duplicated");
    return *std::find_if(request.inputs.begin(), request.inputs.end(),
                         [&](const auto& row) { return row.name == name; });
}
} // namespace
WorkerLimits tile_worker_limits() {
    WorkerLimits limits;
    limits.memory_bytes = 1024ull * 1024 * 1024;
    limits.file_bytes = tile_map_source_byte_limit + 24;
    limits.total_bytes = 128ull * 1024 * 1024;
    limits.seconds = 120;
    limits.cpu_seconds = 110;
    limits.files = 8;
    limits.cancellation_grace_ms = 250;
    return limits;
}
std::vector<ArtifactFile> execute_tile_recipe(const ImportProcessRequest& request,
                                              std::stop_token stop) {
    require(!stop.stop_requested(), "Tile asset cook cancelled");
    const auto& payload = request.payload;
    const auto recipe = payload.at("recipe").get<std::string>();
    const bool map = recipe == tile_map_importer_id;
    require(map || recipe == tile_set_importer_id, "Unsupported fixed tile worker recipe");
    require(payload.is_object() && payload.size() == (map ? 5u : 4u) &&
                payload.at("revision") ==
                    (map ? tile_map_recipe_revision() : tile_set_recipe_revision()) &&
                request.inputs.size() == (map ? 2u : 1u),
            "Tile worker recipe version/fields/input count mismatch");
    const auto expected = payload.at("asset_id").get<AssetId>();
    require(bool(expected), "Tile worker needs an expected root identity");
    const auto& source = input(request, "source.bin");
    require(payload.at("source_digest") == content_digest(source.bytes),
            "Tile worker source snapshot digest mismatch");
    if (!map) {
        const auto authored = TileSetSource::parse(source.bytes);
        require(authored.asset() == expected, "Tile Set worker source identity mismatch");
        require(!stop.stop_requested(), "Tile Set cook cancelled");
        auto cooked = cook_tile_set(authored);
        (void)decode_cooked_tile_set(cooked, expected);
        require(!stop.stop_requested(), "Tile Set cook cancelled before publication");
        return {{tile_set_artifact_file, std::move(cooked)}};
    }
    const auto authored = TileMapSource::parse(source.bytes);
    require(authored.asset() == expected, "Tile Map worker source identity mismatch");
    const auto& definitions = input(request, tile_set_artifact_file);
    require(payload.at("tile_set_digest") == content_digest(definitions.bytes),
            "Tile Map worker Tile Set snapshot digest mismatch");
    const auto data = decode_cooked_tile_set(definitions.bytes, authored.document.tile_set());
    authored.document.for_each_chunk([&](auto, auto cells, auto) {
        require(!stop.stop_requested(), "Tile Map reference validation cancelled");
        for (const auto& cell : cells)
            if (cell.tile)
                (void)data.tile(cell.tile);
    });
    auto cooked = cook_tile_map(authored.document);
    (void)decode_cooked_tile_map(cooked, expected);
    require(!stop.stop_requested(), "Tile Map cook cancelled before publication");
    return {{tile_map_artifact_file, std::move(cooked)}};
}
} // namespace forge::asset_detail
