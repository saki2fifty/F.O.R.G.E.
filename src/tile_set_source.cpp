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
TileSetValues decode(const Json& root) {
    require(root.is_object() && root.at("format") == "forge.tile-set" &&
                natural(root.at("version")) == 1,
            "Unsupported Tile Set source format/version");
    TileSetValues result;
    result.asset = root.at("asset_id").get<AssetId>();
    require(bool(result.asset), "Tile Set requires an asset identity");
    result.next_id = natural(root.at("next_id"), UINT64_MAX, true);
    auto rows = tile_detail::decode_tile_set_rows(root.at("images"), root.at("tiles"));
    for (const auto& image : rows.images)
        require(image.id < result.next_id, "Tile Set image ID is unallocated");
    for (const auto& tile : rows.tiles)
        require(tile.id < result.next_id, "Tile Set definition ID is unallocated");
    result.images = std::move(rows.images);
    result.tiles = std::move(rows.tiles);
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
