#include "navigation_asset.hpp"
#include "asset_bytes.hpp"
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
std::vector<std::byte> envelope(nlohmann::json meta, std::span<const std::byte> tile) {
    validate_tile(tile);
    meta["bounds"] = tile_properties(tile);
    meta["version"] = 1;
    meta["recast_revision"] = recast_revision;
    meta["layout"] = "le-polyref32-detour7-static-single-v1";
    meta["tile_sha256"] = asset_detail::content_digest(tile);
    auto text = meta.dump();
    require(text.size() <= 65536, "Navigation metadata exceeds 64 KiB");
    std::vector<std::byte> out;
    for (char c : magic)
        out.push_back(std::byte(c));
    u32(out, 1);
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
    require(std::equal(magic.begin(), magic.end(), bytes.begin(),
                       [](char a, std::byte b) { return a == std::to_integer<char>(b); }) &&
                u32(bytes, 8) == 1,
            "Unsupported navigation envelope");
    const auto n = u32(bytes, 12), t = u32(bytes, 16);
    require(n > 0 && n <= 65536 && t > 0 && t <= max_nav_bytes &&
                std::size_t(n) + t == bytes.size() - 20,
            "Navigation envelope lengths do not match");
    auto meta =
        nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + n,
                              [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                                  if (depth > 16)
                                      throw std::runtime_error("Navigation metadata nesting limit");
                                  return true;
                              });
    require(meta.is_object() && meta.at("version") == 1 &&
                meta.at("recast_revision") == recast_revision &&
                meta.at("layout") == "le-polyref32-detour7-static-single-v1",
            "Incompatible navigation build provenance");
    (void)meta.at("asset_id").get<AssetId>();
    (void)meta.at("source_scene").get<AssetId>();
    require(digest(meta.at("geometry_sha256").get<std::string>()),
            "Invalid navigation geometry digest");
    (void)meta.at("settings").get<NavigationSettings>();
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
    auto properties = tile_properties(tile);
    require(meta.at("bounds") == properties, "Navmesh bounds/properties mismatch");
    auto settings = meta.at("settings").get<NavigationSettings>();
    require(
        properties.at("radius") == settings.radius && properties.at("height") == settings.height &&
            properties.at("climb") == settings.climb &&
            std::abs(properties.at("cell_inverse").get<float>() - 1 / settings.cell_size) < .0001f,
        "Navmesh agent settings mismatch");
    auto mesh = std::make_shared<Mesh>(tile);
    return {std::move(meta), std::move(mesh)};
}
Admitted load(const std::filesystem::path& root, const AssetRecord& record) {
    require(record.type == NavMeshAsset::type && record.schema_version == 1,
            "Wrong navigation asset type/version");
    auto bytes =
        asset_detail::read_bytes(ProjectPaths(root).resolve(record.source), max_nav_bytes + 65556);
    require(record.metadata.at("sha256").get<std::string>() == asset_detail::content_digest(bytes),
            "Navigation asset digest mismatch");
    auto loaded = admit(bytes);
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
