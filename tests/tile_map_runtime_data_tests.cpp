#include "cooked_envelope.hpp"
#include "tile_map_view.hpp"
#include <iostream>

using namespace forge;
int main() {
    try {
        const auto id = AssetId::parse("c473e29a-b537-4f18-8de2-561bda973604");
        const auto tile_set = AssetId::parse("9cf0f8e9-77b2-42d2-93f9-354da275a9b6");
        // One hand-authored binary record: layer1, chunk(-1,0), slot3, tile9,
        // flags2. This consumer links no Tile Map source/cooker/document library.
        std::vector<std::byte> payload;
        for (const auto scalar : {1u, 0u, UINT32_MAX, 0u, 1u, 3u, 9u, 0u, 2u})
            asset_detail::cooked_write32(payload, scalar);
        const nlohmann::json metadata{
            {"asset_id", id},
            {"tile_set", tile_set},
            {"cell_size", {1., 1.}},
            {"chunk_count", 1},
            {"cell_count", 1},
            {"layers",
             {{{"id", 1}, {"name", "Original runtime layer"}, {"visible", true}, {"order", 0}}}}};
        const auto bytes = asset_detail::encode_envelope(metadata, payload, tile_map_cooked_magic,
                                                         tile_map_cooked_metadata_limit);
        const auto data = decode_cooked_tile_map(bytes, id);
        if (data.cell(1, {-29, 0}) != TileMapCell{9, 2} || data.asset() != id ||
            data.tile_set() != tile_set)
            throw std::runtime_error(
                "Cooked-only runtime cell/identity differs from binary fixture");
        Camera camera;
        camera.projection = std::uint32_t(CameraProjection::Orthographic);
        camera.orthographic_height = 40;
        AffineTransform world;
        world.m[3] = -16;
        world.m[7] = 16;
        world.m[11] = -5;
        const std::array<CameraView, 1> cameras{camera_view(camera, world, 400, 400)};
        unsigned chunks = 0;
        visit_visible_tile_chunks(data, {}, cameras, [&](const TileMapChunkView& view) {
            if (view.key != TileMapChunkKey{1, {-1, 0}} || view.cells[3] != TileMapCell{9, 2})
                throw std::runtime_error("Cooked-only runtime immutable view differs");
            ++chunks;
        });
        if (chunks != 1)
            throw std::runtime_error("Cooked-only runtime did not project exactly one chunk");
        std::cout << "Cooked-only runtime Tile Map consumer and camera view passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
