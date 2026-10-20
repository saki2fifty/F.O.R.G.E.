#include "tile_set_data.hpp"
#include "cooked_envelope.hpp"
#include <map>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::uint64_t natural(const Json& j, std::uint64_t maximum = UINT64_MAX, bool nonzero = false) {
    require(j.is_number_unsigned() || (j.is_number_integer() && j.get<std::int64_t>() >= 0),
            "Tile Set value must be an unsigned integer");
    const auto value = j.get<std::uint64_t>();
    require(value <= maximum && (!nonzero || value), "Tile Set integer outside range");
    return value;
}
std::string name(const Json& j) {
    const auto& text = j.get_ref<const std::string&>();
    require(!text.empty() && text.size() <= 128 &&
                std::none_of(text.begin(), text.end(),
                             [](unsigned char c) { return c < 32 || c == 127; }),
            "Tile Set name is empty, too long or contains controls");
    return text;
}
template <class Row> const Row& find(const std::vector<Row>& rows, std::uint64_t key) {
    const auto found = std::lower_bound(rows.begin(), rows.end(), key,
                                        [](const auto& row, auto id) { return row.id < id; });
    require(found != rows.end() && found->id == key, "Tile Set local key was not found");
    return *found;
}
} // namespace
TileSetRows tile_detail::decode_tile_set_rows(const Json& images, const Json& tiles) {
    require(images.is_array() && images.size() <= tile_set_image_limit && tiles.is_array() &&
                tiles.size() <= tile_set_definition_limit,
            "Tile Set exceeds image/definition limits");
    TileSetRows result;
    std::set<std::uint64_t> keys;
    std::map<std::uint64_t, std::uint64_t> frames;
    for (const auto& row : images) {
        require(row.is_object(), "Tile Set image must be an object");
        TileSetImage image;
        image.id = natural(row.at("id"), UINT64_MAX - 1, true);
        require(keys.insert(image.id).second, "Duplicate Tile Set local key");
        image.name = name(row.at("name"));
        image.material = row.at("material").get<AssetRef<MaterialAsset>>();
        require(bool(image.material.id), "Tile Set image needs a Material reference");
        const auto& grid = row.at("grid");
        require(grid.is_object(), "Tile Set image grid must be an object");
        image.grid = {std::uint32_t(natural(grid.at("columns"), UINT32_MAX, true)),
                      std::uint32_t(natural(grid.at("rows"), UINT32_MAX, true)),
                      std::uint32_t(natural(grid.at("margin_x"), UINT32_MAX)),
                      std::uint32_t(natural(grid.at("margin_y"), UINT32_MAX)),
                      std::uint32_t(natural(grid.at("spacing_x"), UINT32_MAX)),
                      std::uint32_t(natural(grid.at("spacing_y"), UINT32_MAX))};
        const auto count = std::uint64_t(image.grid.columns) * image.grid.rows;
        require(count <= UINT32_MAX, "Tile Set grid exceeds its frame-index range");
        frames.emplace(image.id, count);
        result.images.push_back(std::move(image));
    }
    for (const auto& row : tiles) {
        require(row.is_object(), "Tile Set definition must be an object");
        TileSetDefinition tile;
        tile.id = natural(row.at("id"), UINT64_MAX - 1, true);
        require(keys.insert(tile.id).second, "Duplicate Tile Set local key");
        tile.name = name(row.at("name"));
        tile.image = natural(row.at("image"), UINT64_MAX, true);
        const auto image = frames.find(tile.image);
        require(image != frames.end(), "Tile Set definition refers to a missing image");
        tile.frame = std::uint32_t(natural(row.at("frame"), UINT32_MAX));
        require(tile.frame < image->second, "Tile Set definition frame is outside its grid");
        result.tiles.push_back(std::move(tile));
    }
    return result;
}
TileSetData decode_cooked_tile_set(std::span<const std::byte> bytes, AssetId expected) {
    const auto envelope = asset_detail::decode_envelope(bytes, tile_set_cooked_magic,
                                                        tile_set_cooked_metadata_limit, 0);
    const auto& root = envelope.metadata;
    require(root.is_object() && root.size() == 3 && bool(expected) &&
                root.at("asset_id").get<AssetId>() == expected,
            "Cooked Tile Set identity/metadata mismatch");
    auto rows = tile_detail::decode_tile_set_rows(root.at("images"), root.at("tiles"));
    for (const auto& row : root.at("images"))
        require(row.size() == 4 && row.at("grid").size() == 6,
                "Cooked Tile Set image contains unsupported fields");
    for (const auto& row : root.at("tiles"))
        require(row.size() == 4, "Cooked Tile Set definition contains unsupported fields");
    const auto by_id = [](const auto& a, const auto& b) { return a.id < b.id; };
    std::sort(rows.images.begin(), rows.images.end(), by_id);
    std::sort(rows.tiles.begin(), rows.tiles.end(), by_id);
    TileSetData data;
    data.asset_ = expected;
    data.rows_ = std::make_shared<const TileSetRows>(std::move(rows));
    return data;
}
void TileSetData::validate() const {
    require(bool(asset_) && bool(rows_), "Cooked Tile Set is uninitialized or was moved from");
}
AssetId TileSetData::asset() const {
    validate();
    return asset_;
}
const std::vector<TileSetImage>& TileSetData::images() const {
    validate();
    return rows_->images;
}
const std::vector<TileSetDefinition>& TileSetData::tiles() const {
    validate();
    return rows_->tiles;
}
const TileSetImage& TileSetData::image(std::uint64_t key) const { return find(images(), key); }
const TileSetDefinition& TileSetData::tile(std::uint64_t key) const { return find(tiles(), key); }
std::vector<AssetRef<MaterialAsset>> TileSetData::materials() const {
    std::set<AssetRef<MaterialAsset>> unique;
    for (const auto& image : images())
        unique.insert(image.material);
    return {unique.begin(), unique.end()};
}
std::size_t TileSetData::resident_bytes() const {
    validate();
    auto bytes = sizeof(*this) + sizeof(TileSetRows) + 64 +
                 rows_->images.capacity() * sizeof(TileSetImage) +
                 rows_->tiles.capacity() * sizeof(TileSetDefinition);
    for (const auto& image : rows_->images)
        bytes += image.name.capacity() + 1;
    for (const auto& tile : rows_->tiles)
        bytes += tile.name.capacity() + 1;
    return bytes;
}
} // namespace forge
