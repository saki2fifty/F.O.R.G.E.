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
                natural(root.at("version")) >= 1 &&
                natural(root.at("version")) <= tile_set_source_version,
            "Unsupported Tile Set source format/version");
    TileSetValues result;
    result.asset = root.at("asset_id").get<AssetId>();
    require(bool(result.asset), "Tile Set requires an asset identity");
    result.next_id = natural(root.at("next_id"), UINT64_MAX, true);
    auto rows = tile_detail::decode_tile_set_rows(root.at("images"), root.at("tiles"),
                                                  root.at("version").get<std::uint32_t>());
    for (const auto& image : rows.images)
        require(image.id < result.next_id, "Tile Set image ID is unallocated");
    for (const auto& tile : rows.tiles)
        require(tile.id < result.next_id, "Tile Set definition ID is unallocated");
    result.images = std::move(rows.images);
    result.tiles = std::move(rows.tiles);
    return result;
}
// Enabled indexed profiles share the source transaction and extension ownership.
void set_indexed_profile(TileSetSource& source, std::uint64_t tile, const Json& value,
                         const char* field, const char* entries, std::uint32_t version,
                         std::size_t limit) {
    source.validate();
    if (!value.is_object() || !value.contains("enabled") || !value.at("enabled").is_boolean() ||
        !value.contains(entries) || !value.at(entries).is_array() ||
        value.at(entries).size() > limit)
        throw std::runtime_error(std::string("Tile ") + field + " requires enabled and bounded " +
                                 entries + " values");
    auto candidate = source;
    candidate.document["version"] =
        std::max(source.document.at("version").get<std::uint32_t>(), version);
    source.validate_upgrade(candidate);
    auto& rows = candidate.document.at("tiles");
    const auto at = std::find_if(rows.begin(), rows.end(),
                                 [&](const auto& row) { return row.at("id") == tile; });
    if (at == rows.end())
        throw std::runtime_error(std::string("Tile ") + field + " definition is unavailable");
    auto merged = at->value(field, Json::object());
    const auto prior = merged.value(entries, Json::array());
    merged.update(value);
    // Rows have positional ownership; removing a row explicitly removes its extensions.
    auto& changed = merged.at(entries);
    for (std::size_t i = 0; i < changed.size(); ++i) {
        if (!changed[i].is_object())
            throw std::runtime_error(std::string("Tile ") + field + " row must be an object");
        auto row = i < prior.size() ? prior[i] : Json::object();
        row.update(changed[i]);
        changed[i] = std::move(row);
    }
    (*at)[field] = std::move(merged);
    candidate.validate();
    source.document.swap(candidate.document);
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
void TileSetSource::validate_upgrade(const TileSetSource& next) const {
    require(next.document.at("version") >= document.at("version"),
            "Tile Set source format cannot downgrade during an edit");
    if (document.at("version") == 1 && next.document.at("version") >= 2)
        for (const auto& row : document.at("tiles"))
            require(
                !row.contains("collision"),
                "Legacy collision extension conflicts with Tile Set version2; source unchanged");
    if (document.at("version") < 4 && next.document.at("version") >= 4)
        for (const auto& row : document.at("tiles"))
            require(
                !row.contains("animation"),
                "Legacy animation extension conflicts with Tile Set version4; source unchanged");
    if (document.at("version") < 5 && next.document.at("version") >= 5)
        for (const auto& row : document.at("tiles"))
            require(!row.contains("autotile"),
                    "Legacy neighbor extension conflicts with Tile Set version5; source unchanged");
}
void TileSetSource::set_collision(std::uint64_t tile, const Json& value) {
    validate();
    require(value.is_object() && value.contains("kind"), "Tile collision requires a kind");
    auto candidate = *this;
    candidate.document["version"] = std::max(document.at("version").get<std::uint32_t>(),
                                             value.at("kind") == "polygon" ? 3u : 2u);
    validate_upgrade(candidate);
    auto& rows = candidate.document.at("tiles");
    const auto at = std::find_if(rows.begin(), rows.end(),
                                 [&](const auto& row) { return row.at("id") == tile; });
    require(at != rows.end(), "Tile collision definition is unavailable");
    auto merged = at->value("collision", Json::object());
    merged.erase("kind");
    merged.erase("points");
    merged.update(value);
    (*at)["collision"] = std::move(merged);
    candidate.validate();
    document.swap(candidate.document);
}
void TileSetSource::set_animation(std::uint64_t tile, const Json& value) {
    set_indexed_profile(*this, tile, value, "animation", "frames", 4, tile_animation_frame_limit);
}
void TileSetSource::set_autotile(std::uint64_t tile, const Json& value) {
    set_indexed_profile(*this, tile, value, "autotile", "rules", 5, tile_neighbor_rule_limit);
}
std::vector<AssetRef<MaterialAsset>> TileSetSource::materials() const {
    std::set<AssetRef<MaterialAsset>> unique;
    for (const auto& image : values().images)
        unique.insert(image.material);
    return {unique.begin(), unique.end()};
}
} // namespace forge
