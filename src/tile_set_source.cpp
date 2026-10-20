#include "tile_set_source.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::uint64_t natural(const Json& value, std::uint64_t maximum = UINT64_MAX, bool nonzero = false) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Tile Set value must be an unsigned integer");
    const auto number = value.get<std::uint64_t>();
    require(number <= maximum && (!nonzero || number != 0), "Tile Set integer outside range");
    return number;
}
std::string name(const Json& value) {
    require(value.is_string(), "Tile Set name must be a string");
    const auto& text = value.get_ref<const std::string&>();
    require(!text.empty() && text.size() <= 128 &&
                std::none_of(text.begin(), text.end(),
                             [](unsigned char c) { return c < 32 || c == 127; }),
            "Tile Set name is empty, too long or contains controls");
    return text;
}
TileSetValues decode(const Json& root) {
    require(root.is_object() && root.at("format") == "forge.tile-set" &&
                natural(root.at("version")) == 1,
            "Unsupported Tile Set source format/version");
    TileSetValues result;
    result.asset = root.at("asset_id").get<AssetId>();
    require(bool(result.asset), "Tile Set requires an asset identity");
    result.next_id = natural(root.at("next_id"), UINT64_MAX, true);
    const auto& images = root.at("images");
    const auto& tiles = root.at("tiles");
    require(images.is_array() && images.size() <= tile_set_image_limit && tiles.is_array() &&
                tiles.size() <= tile_set_definition_limit,
            "Tile Set exceeds image/definition limits");
    std::set<std::uint64_t> identities;
    std::map<std::uint64_t, std::uint64_t> frame_counts;
    for (const auto& row : images) {
        require(row.is_object(), "Tile Set image must be an object");
        TileSetImage image;
        image.id = natural(row.at("id"), UINT64_MAX, true);
        require(image.id < result.next_id && identities.insert(image.id).second,
                "Tile Set image ID is duplicated or unallocated");
        image.name = name(row.at("name"));
        image.material = row.at("material").get<AssetRef<MaterialAsset>>();
        require(bool(image.material.id), "Tile Set image needs a Material reference");
        const auto& grid = row.at("grid");
        require(grid.is_object(), "Tile Set grid must be an object");
        image.grid = {std::uint32_t(natural(grid.at("columns"), UINT32_MAX, true)),
                      std::uint32_t(natural(grid.at("rows"), UINT32_MAX, true)),
                      std::uint32_t(natural(grid.at("margin_x"), UINT32_MAX)),
                      std::uint32_t(natural(grid.at("margin_y"), UINT32_MAX)),
                      std::uint32_t(natural(grid.at("spacing_x"), UINT32_MAX)),
                      std::uint32_t(natural(grid.at("spacing_y"), UINT32_MAX))};
        const auto count = std::uint64_t(image.grid.columns) * image.grid.rows;
        require(count <= UINT32_MAX, "Tile Set grid exceeds its frame-index range");
        frame_counts.emplace(image.id, count);
        result.images.push_back(std::move(image));
    }
    for (const auto& row : tiles) {
        require(row.is_object(), "Tile Set definition must be an object");
        TileSetDefinition tile;
        tile.id = natural(row.at("id"), UINT64_MAX, true);
        require(tile.id < result.next_id && identities.insert(tile.id).second,
                "Tile Set definition ID is duplicated or unallocated");
        tile.name = name(row.at("name"));
        tile.image = natural(row.at("image"), UINT64_MAX, true);
        const auto source = frame_counts.find(tile.image);
        require(source != frame_counts.end(), "Tile Set definition refers to a missing image");
        tile.frame = std::uint32_t(natural(row.at("frame"), UINT32_MAX));
        require(tile.frame < source->second, "Tile Set definition frame is outside its grid");
        result.tiles.push_back(std::move(tile));
    }
    return result;
}
void validate_structure(const Json& document) {
    const auto text = document.dump();
    (void)asset_detail::parse_bounded_json(std::as_bytes(std::span(text)),
                                           tile_set_source_byte_limit, 100000, 16);
}
} // namespace
TileSetSource TileSetSource::create(AssetId id) {
    require(bool(id), "Tile Set requires an asset identity");
    return {{{"format", "forge.tile-set"},
             {"version", 1},
             {"asset_id", id},
             {"next_id", 1},
             {"images", Json::array()},
             {"tiles", Json::array()}}};
}
TileSetSource TileSetSource::parse(std::span<const std::byte> bytes) {
    TileSetSource source{
        asset_detail::parse_bounded_json(bytes, tile_set_source_byte_limit, 100000, 16)};
    (void)decode(source.document);
    return source;
}
AssetId TileSetSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void TileSetSource::validate() const {
    (void)decode(document);
    validate_structure(document);
}
TileSetValues TileSetSource::values() const { return decode(document); }
std::vector<AssetRef<MaterialAsset>> TileSetSource::materials() const {
    std::set<AssetRef<MaterialAsset>> unique;
    for (const auto& image : values().images)
        unique.insert(image.material);
    return {unique.begin(), unique.end()};
}
} // namespace forge
