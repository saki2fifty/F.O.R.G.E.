#include "navigation_asset.hpp"
#include "asset_bytes.hpp"
#include "json_document.hpp"
#include "navigation_layers.hpp"
#include <algorithm>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <set>
namespace forge::navigation_detail {
namespace {
constexpr std::string_view magic = "FORGENAV";
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
void u32(std::vector<std::byte>& out, unsigned n) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(std::byte((n >> (i * 8)) & 255));
}
unsigned u32(std::span<const std::byte> b, std::size_t p) {
    require(p + 4 <= b.size(), "Truncated navigation envelope");
    unsigned v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= std::to_integer<unsigned>(b[p + i]) << (8 * i);
    return v;
}
bool digest(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
} // namespace
NavigationSettings provenance_settings(const nlohmann::json& meta) {
    const auto version = meta.at("version");
    require(version.is_number_integer() &&
                (version == 1 || version == 2 || version == 3 || version == 4),
            "Unsupported navigation envelope version");
    auto settings = meta.at("settings").get<NavigationSettings>();
    require((version == 4) == settings.dynamic_obstacles &&
                (version == 4 || (version == 3) == (settings.plane == NavigationPlane::PlanarXY)),
            "Navigation envelope version/profile/plane mismatch");
    if (version == 1) {
        require(!meta.contains("geometry_version") && !meta.contains("mesh_inputs"),
                "Mesh provenance cannot be downgraded to navigation v1");
    } else {
        require(meta.at("geometry_version").is_number_integer() &&
                    meta.at("geometry_version") ==
                        (settings.plane == NavigationPlane::PlanarXY ? 3 : 2),
                "Unsupported navigation geometry version");
        const auto& inputs = meta.at("mesh_inputs");
        require(inputs.is_array() && inputs.size() <= 32 &&
                    (settings.plane != NavigationPlane::PlanarXY || inputs.empty()),
                "Invalid navigation Mesh inputs for this plane");
        std::set<AssetId> meshes;
        for (const auto& input : inputs) {
            require(input.is_object() && input.size() == 2 &&
                        meshes.insert(input.at("asset_id").get<AssetId>()).second &&
                        digest(input.at("revision").get<std::string>()),
                    "Invalid or repeated navigation Mesh provenance");
        }
    }
    return settings;
}
std::vector<std::byte> envelope(nlohmann::json meta, std::span<const std::byte> tile) {
    // Preserve v1 callers which historically omitted the version field.
    if (!meta.contains("version"))
        meta["version"] = 1;
    const auto settings = provenance_settings(meta);
    if (settings.dynamic_obstacles) {
        const auto layers = decode_layers(tile);
        require(layers.settings == settings, "Navigation layer/settings mismatch");
        meta["bounds"] = layer_properties(layers);
    } else {
        validate_tile(tile, settings.plane);
        meta["bounds"] = tile_properties(tile);
    }
    const unsigned version = meta.value("version", 1u);
    meta["version"] = version;
    meta["recast_revision"] = recast_revision;
    meta["layout"] = settings.dynamic_obstacles ? "le-cells32-tiled-v1"
                                                : "le-polyref32-detour7-static-single-v1";
    meta["tile_sha256"] = asset_detail::content_digest(tile);
    auto text = meta.dump();
    require(text.size() <= 65536, "Navigation metadata exceeds 64 KiB");
    std::vector<std::byte> out;
    for (char c : magic)
        out.push_back(std::byte(c));
    u32(out, version);
    u32(out, unsigned(text.size()));
    u32(out, unsigned(tile.size()));
    auto b = std::as_bytes(std::span(text));
    out.insert(out.end(), b.begin(), b.end());
    out.insert(out.end(), tile.begin(), tile.end());
    return out;
}
Admitted admit(std::span<const std::byte> bytes) {
    require(bytes.size() >= 20 && bytes.size() <= max_nav_bytes + 65556,
            "Invalid navigation envelope size");
    require(
        std::equal(magic.begin(), magic.end(), bytes.begin(),
                   [](char a, std::byte b) { return a == std::to_integer<char>(b); }) &&
            (u32(bytes, 8) == 1 || u32(bytes, 8) == 2 || u32(bytes, 8) == 3 || u32(bytes, 8) == 4),
        "Unsupported navigation envelope");
    const auto n = u32(bytes, 12), t = u32(bytes, 16);
    require(n > 0 && n <= 65536 && t > 0 && t <= max_nav_bytes &&
                std::size_t(n) + t == bytes.size() - 20,
            "Navigation envelope lengths do not match");
    auto meta = detail::parse_json_document(
        std::string_view(reinterpret_cast<const char*>(bytes.data() + 20), n),
        [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if (depth > 16)
                throw std::runtime_error("Navigation metadata nesting limit");
            return true;
        });
    require(meta.is_object() && meta.at("version") == u32(bytes, 8) &&
                meta.at("recast_revision") == recast_revision &&
                meta.at("layout") == (u32(bytes, 8) == 4 ? "le-cells32-tiled-v1"
                                                         : "le-polyref32-detour7-static-single-v1"),
            "Incompatible navigation build provenance");
    (void)meta.at("asset_id").get<AssetId>();
    (void)meta.at("source_scene").get<AssetId>();
    require(digest(meta.at("geometry_sha256").get<std::string>()),
            "Invalid navigation geometry digest");
    const auto settings = provenance_settings(meta);
    const auto& ids = meta.at("sources");
    require(ids.is_array() && !ids.empty() && ids.size() <= 512,
            "Invalid navigation source identities");
    std::set<EntityId> unique;
    for (const auto& id : ids)
        require(unique.insert(id.get<EntityId>()).second, "Repeated navigation source identity");
    auto tile = bytes.subspan(20 + n, t);
    require(meta.at("tile_sha256").get<std::string>() == asset_detail::content_digest(tile),
            "Navigation tile digest mismatch");
    // Structural validation precedes all Detour runtime calls, even if checksums match.
    const auto properties = [&] {
        if (!settings.dynamic_obstacles)
            return tile_properties(tile);
        const auto layers = decode_layers(tile);
        require(layers.settings == settings, "Navigation layer/settings mismatch");
        return layer_properties(layers);
    }();
    require(meta.at("bounds") == properties, "Navmesh bounds/properties mismatch");
    require(
        properties.at("radius") == settings.radius && properties.at("height") == settings.height &&
            properties.at("climb") == settings.climb &&
            std::abs(properties.at("cell_inverse").get<float>() - 1 / settings.cell_size) < .0001f,
        "Navmesh agent settings mismatch");
    auto mesh = std::make_shared<Mesh>(tile, settings.plane, settings.dynamic_obstacles);
    return {std::move(meta), std::move(mesh)};
}
Admitted load(const std::filesystem::path& root, const AssetRecord& record) {
    require(record.type == NavMeshAsset::type &&
                (record.schema_version == 1 || record.schema_version == 2 ||
                 record.schema_version == 3 || record.schema_version == 4),
            "Wrong navigation asset type/version");
    auto bytes =
        asset_detail::read_bytes(ProjectPaths(root).resolve(record.source), max_nav_bytes + 65556);
    require(record.metadata.at("sha256").get<std::string>() == asset_detail::content_digest(bytes),
            "Navigation asset digest mismatch");
    auto loaded = admit(bytes);
    require(record.schema_version == loaded.metadata.at("version"),
            "Navigation schema/envelope version mismatch");
    require(loaded.metadata.at("asset_id").get<AssetId>() == record.id,
            "Navigation asset identity mismatch");
    auto expected = record.metadata;
    expected.erase("sha256");
    require(expected == loaded.metadata, "Navigation catalog/envelope provenance mismatch");
    return loaded;
}
} // namespace forge::navigation_detail
namespace forge {
std::vector<Double3> navigation_triangles(const std::filesystem::path& project,
                                          AssetRef<NavMeshAsset> ref) {
    auto catalog = AssetCatalog::open_project(project);
    auto found = catalog.resolve(ref);
    if (found.state != AssetState::Available)
        throw std::runtime_error(found.diagnostic);
    return navigation_detail::load(project, *found.record).mesh->triangles();
}
} // namespace forge
