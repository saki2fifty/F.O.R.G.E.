#include "tile_set_data.hpp"
#include "cooked_envelope.hpp"
#include "tile_animation.hpp"
#include "tile_polygon_decomposition.hpp"
#include <cmath>
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
TileCollisionProfile collision(const Json& value, std::uint32_t profile_version) {
    require(value.is_object(), "Tile collision must be an object");
    const auto& kind = value.at("kind");
    TileCollisionProfile result;
    if (kind == "full_cell" || kind == "none") {
        require(!value.contains("points"), "Only polygon tile collision accepts points");
        result.kind = kind == "none" ? TileCollisionKind::None : TileCollisionKind::FullCell;
        return result;
    }
    require(kind == "convex" || (kind == "polygon" && profile_version >= 3),
            "Unknown or unsupported tile collision kind for this profile version");
    result.kind = kind == "convex" ? TileCollisionKind::Convex : TileCollisionKind::Polygon;
    const auto& points = value.at("points");
    require(points.is_array() && points.size() >= 3 && points.size() <= tile_collision_vertex_limit,
            "Polygon tile collision requires3..8 points");
    for (const auto& row : points) {
        require(row.is_array() && row.size() == 2, "Tile collision point requires XY");
        std::array<double, 2> point;
        for (unsigned axis = 0; axis < 2; ++axis) {
            require(row[axis].is_number(), "Tile collision coordinates must be numbers");
            point[axis] = row[axis].get<double>();
            require(std::isfinite(point[axis]) && point[axis] >= 0 && point[axis] <= 1,
                    "Tile collision coordinates must be finite in0..1");
        }
        result.points.push_back(point);
    }
    if (result.kind == TileCollisionKind::Convex)
        require(tile_detail::tile_collision_winding<double>(result.points) == 1,
                "Tile collision points must form a strict counterclockwise convex contour");
    else
        (void)tile_detail::tile_polygon_triangles(result.points);
    return result;
}
std::shared_ptr<const TileAnimation> animation(const Json& value, std::uint64_t image_frames) {
    require(value.is_object() && value.at("enabled").is_boolean(),
            "Tile animation requires an enabled boolean");
    auto result = std::make_shared<TileAnimation>();
    result->enabled = value.at("enabled").get<bool>();
    const auto& frames = value.at("frames");
    require(frames.is_array() && frames.size() <= tile_animation_frame_limit &&
                (!result->enabled || !frames.empty()),
            "Enabled tile animation needs1..64frames");
    result->frames.reserve(frames.size());
    for (const auto& row : frames) {
        require(row.is_object() && row.at("seconds").is_number(),
                "Tile animation frame needs a numeric duration");
        result->frames.push_back(
            {std::uint32_t(natural(row.at("frame"), UINT32_MAX)), row.at("seconds").get<double>()});
    }
    if (!result->frames.empty())
        result->duration = tile_detail::tile_animation_duration(result->frames, image_frames);
    return result;
}
} // namespace
Json tile_detail::tile_autotile_json(const TileAutotile& value) {
    Json rows = Json::array();
    for (const auto& rule : value.rules)
        rows.push_back({{"mask", rule.mask}, {"same", rule.same}, {"tile", rule.tile}});
    return {{"enabled", value.enabled}, {"rules", std::move(rows)}};
}
Json tile_detail::tile_collision_json(const TileCollisionProfile& value) {
    if (value.kind == TileCollisionKind::Convex || value.kind == TileCollisionKind::Polygon)
        return {{"kind", value.kind == TileCollisionKind::Convex ? "convex" : "polygon"},
                {"points", value.points}};
    require(value.kind == TileCollisionKind::FullCell || value.kind == TileCollisionKind::None,
            "Unknown typed tile collision kind");
    return {{"kind", value.kind == TileCollisionKind::None ? "none" : "full_cell"}};
}
Json tile_detail::tile_animation_json(const TileAnimation& value) {
    Json rows = Json::array();
    for (const auto& frame : value.frames)
        rows.push_back({{"frame", frame.frame}, {"seconds", frame.seconds}});
    return {{"enabled", value.enabled}, {"frames", std::move(rows)}};
}
Json tile_detail::tile_definition_json(const TileSetDefinition& value) {
    Json row{{"id", value.id},
             {"name", value.name},
             {"image", value.image},
             {"frame", value.frame},
             {"collision", tile_collision_json(value.collision)}};
    if (value.animation && (value.animation->enabled || !value.animation->frames.empty()))
        row["animation"] = tile_animation_json(*value.animation);
    if (value.autotile && (value.autotile->enabled || !value.autotile->rules.empty()))
        row["autotile"] = tile_autotile_json(*value.autotile);
    return row;
}
TileSetRows tile_detail::decode_tile_set_rows(const Json& images, const Json& tiles,
                                              std::uint32_t profile_version) {
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
        if (profile_version >= 2 && row.contains("collision"))
            tile.collision = collision(row.at("collision"), profile_version);
        if (profile_version >= 4 && row.contains("animation"))
            tile.animation = animation(row.at("animation"), image->second);
        if (profile_version >= 5 && row.contains("autotile")) {
            const auto& value = row.at("autotile");
            require(value.is_object() && value.at("enabled").is_boolean() &&
                        value.at("rules").is_array() &&
                        value.at("rules").size() <= tile_neighbor_rule_limit,
                    "Tile neighbor rules require enabled and bounded rules");
            auto config = std::make_shared<TileAutotile>();
            config->enabled = value.at("enabled").get<bool>();
            require(!config->enabled || !value.at("rules").empty(),
                    "Enabled neighbor rules require at least one row");
            config->rules.reserve(value.at("rules").size());
            for (const auto& entry : value.at("rules")) {
                require(entry.is_object(), "Tile neighbor rule must be an object");
                config->rules.push_back({std::uint8_t(natural(entry.at("mask"), 255)),
                                         std::uint8_t(natural(entry.at("same"), 255)),
                                         natural(entry.at("tile"), UINT64_MAX - 1, true)});
            }
            config->lookup = tile_detail::compile_tile_neighbors(config->rules);
            result.has_autotiles |= config->enabled;
            tile.autotile = std::move(config);
        }
        result.tiles.push_back(std::move(tile));
    }
    if (profile_version >= 5 &&
        std::any_of(result.tiles.begin(), result.tiles.end(), [](const auto& tile) {
            return tile.autotile && !tile.autotile->rules.empty();
        })) {
        std::map<std::uint64_t, const TileSetDefinition*> definitions;
        for (const auto& tile : result.tiles)
            definitions.emplace(tile.id, &tile);
        for (const auto& tile : result.tiles)
            if (tile.autotile)
                for (const auto& rule : tile.autotile->rules) {
                    const auto target = definitions.find(rule.tile);
                    require(target != definitions.end(), "Neighbor rule refers to a missing tile");
                    require(rule.tile == tile.id || !target->second->autotile ||
                                !target->second->autotile->enabled,
                            "Neighbor rule cannot select another enabled rule tile");
                }
    }
    return result;
}
TileSetData decode_cooked_tile_set(std::span<const std::byte> bytes, AssetId expected,
                                   std::uint32_t expected_version) {
    const auto envelope = asset_detail::decode_envelope(
        bytes, tile_set_cooked_magic, tile_set_cooked_metadata_limit, 0, tile_set_cooked_version);
    const auto& root = envelope.metadata;
    require(!expected_version || envelope.version == expected_version,
            "Cooked Tile Set version differs from its selected artifact");
    require(root.is_object() && root.size() == 3 && bool(expected) &&
                root.at("asset_id").get<AssetId>() == expected,
            "Cooked Tile Set identity/metadata mismatch");
    auto rows =
        tile_detail::decode_tile_set_rows(root.at("images"), root.at("tiles"), envelope.version);
    for (const auto& row : root.at("images"))
        require(row.size() == 4 && row.at("grid").size() == 6,
                "Cooked Tile Set image contains unsupported fields");
    for (const auto& row : root.at("tiles")) {
        const bool animated = envelope.version >= 4 && row.contains("animation");
        const bool autotile = envelope.version >= 5 && row.contains("autotile");
        require(row.size() ==
                    (envelope.version >= 2 ? 5u : 4u) + unsigned(animated) + unsigned(autotile),
                "Cooked Tile Set definition contains unsupported fields");
        if (envelope.version >= 2) {
            const auto& profile = row.at("collision");
            require(
                profile.size() ==
                    ((profile.at("kind") == "convex" || profile.at("kind") == "polygon") ? 2u : 1u),
                "Cooked tile collision contains unsupported fields");
        }
        if (animated) {
            const auto& value = row.at("animation");
            require(value.size() == 2, "Cooked tile animation contains unsupported fields");
            for (const auto& frame : value.at("frames"))
                require(frame.size() == 2,
                        "Cooked tile animation frame contains unsupported fields");
        }
        if (autotile) {
            const auto& value = row.at("autotile");
            require(value.size() == 2, "Cooked tile neighbor rules contain unsupported fields");
            for (const auto& rule : value.at("rules"))
                require(rule.size() == 3, "Cooked tile neighbor rule contains unsupported fields");
        }
    }
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
bool TileSetData::has_autotiles() const {
    validate();
    return rows_->has_autotiles;
}
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
        bytes +=
            tile.name.capacity() + 1 +
            tile.collision.points.capacity() * sizeof(std::array<double, 2>) +
            (tile.animation ? sizeof(TileAnimation) + 64 +
                                  tile.animation->frames.capacity() * sizeof(TileAnimationFrame)
                            : 0) +
            (tile.autotile ? sizeof(TileAutotile) + 64 +
                                 tile.autotile->rules.capacity() * sizeof(TileNeighborRule)
                           : 0);
    return bytes;
}
} // namespace forge
