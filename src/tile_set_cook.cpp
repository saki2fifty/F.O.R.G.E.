#include "tile_set_cook.hpp"
#include "cooked_envelope.hpp"

namespace forge {
std::vector<std::byte> cook_tile_set(const TileSetSource& source) {
    source.validate();
    auto values = source.values();
    const auto by_id = [](const auto& a, const auto& b) { return a.id < b.id; };
    std::sort(values.images.begin(), values.images.end(), by_id);
    std::sort(values.tiles.begin(), values.tiles.end(), by_id);
    using Json = nlohmann::json;
    Json root{{"asset_id", values.asset}, {"images", Json::array()}, {"tiles", Json::array()}};
    for (const auto& image : values.images) {
        const auto& g = image.grid;
        root["images"].push_back({{"id", image.id},
                                  {"name", image.name},
                                  {"material", image.material},
                                  {"grid",
                                   {{"columns", g.columns},
                                    {"rows", g.rows},
                                    {"margin_x", g.margin_x},
                                    {"margin_y", g.margin_y},
                                    {"spacing_x", g.spacing_x},
                                    {"spacing_y", g.spacing_y}}}});
    }
    for (const auto& tile : values.tiles)
        root["tiles"].push_back(
            {{"id", tile.id}, {"name", tile.name}, {"image", tile.image}, {"frame", tile.frame}});
    return asset_detail::encode_envelope(root, {}, tile_set_cooked_magic,
                                         tile_set_cooked_metadata_limit);
}
} // namespace forge
