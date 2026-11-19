#include "navigation_bake.hpp"
#include "asset_bytes.hpp"
#include "asset_worker.hpp"
#include "native_io_path.hpp"
#include <fstream>

namespace forge::navigation_detail {
namespace {
using Json = nlohmann::json;
using namespace asset_detail;
void write(const std::filesystem::path& p, std::span<const std::byte> data) {
    std::ofstream out(native_io_path(p), std::ios::binary | std::ios::trunc);
    if (!out ||
        !out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size())) ||
        !out.flush())
        throw std::runtime_error("Cannot stage navigation candidate");
}
} // namespace
nlohmann::json navigation_bake_metadata(AssetId id, const Sources& source, const Geometry& g,
                                        const NavigationSettings& settings, const Json& inputs,
                                        const Json& tiles, const Json& collision_provenance) {
    const unsigned geometry_version =
        settings.plane == NavigationPlane::PlanarXY ? (collider_sources(source) ? 5 : 3) : 2;
    const bool arcs =
        std::ranges::any_of(g.connections, [](const auto& link) { return link.arc_height != 0; });
    const unsigned version = g.classified                        ? 9
                             : arcs                              ? 8
                             : settings.collect_static_colliders ? 7
                             : !g.connections.empty()
                                 ? 6
                                 : (settings.dynamic_obstacles ? 4 : geometry_version);
    Json meta = {{"asset_id", id},
                 {"version", version},
                 {"geometry_version", geometry_version},
                 {"mesh_inputs", inputs},
                 {"source_scene", g.scene},
                 {"sources", g.sources},
                 {"geometry_sha256", g.digest},
                 {"settings", settings}};
    if (!g.connections.empty())
        meta["links"] = navigation_link_values(g.connections, settings.plane);
    if (g.classified)
        meta["area_sources"] = area_sources(source);
    if (geometry_version == 5)
        meta["tile_inputs"] = tiles;
    if (settings.collect_static_colliders && settings.plane == NavigationPlane::Spatial3D)
        meta["collision_inputs"] = collision_provenance;
    return meta;
}
std::unique_ptr<NavigationBake> prepare_navigation_bake(const Geometry& g, Json meta,
                                                        const std::filesystem::path& worker,
                                                        const std::filesystem::path& staging_root,
                                                        std::stop_token cancel) {
    using namespace asset_detail;
    const auto settings = provenance_settings(meta);
    auto result = std::make_unique<NavigationBake>(staging_root);
    Json input = {{"metadata", meta}, {"vertices", g.vertices}};
    if (g.classified)
        input["areas"] = g.areas;
    if (settings.plane == NavigationPlane::PlanarXY)
        input["obstacles"] = g.obstacles;
    auto text = input.dump();
    write(result->staging.path / "source.json", std::as_bytes(std::span(text)));
    try {
        run_worker(WorkerKind::Navigation, worker, result->staging.path, cancel, {},
                   result->staging.owner.get());
    } catch (const std::exception&) {
        if (std::filesystem::is_regular_file(result->staging.path / "error.txt")) {
            auto error = read_bytes(result->staging.path / "error.txt", 8192);
            throw std::runtime_error(
                std::string(reinterpret_cast<const char*>(error.data()), error.size()));
        }
        throw;
    }
    auto data = read_bytes(result->staging.path / "navigation.fnav", max_nav_bytes + 65556);
    auto admitted = admit(data);
    for (const auto& [key, value] : meta.items())
        if (admitted.metadata.at(key) != value)
            throw std::runtime_error("Navigation worker provenance mismatch");
    Query proof(admitted.mesh);
    auto triangles = admitted.mesh->triangles();
    if (triangles.size() < 3)
        throw std::runtime_error("Navigation contains no triangles");
    Double3 center{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            center[j] += triangles[i][j] / 3;
    if (proof.project(center).status != NavStatus::Success ||
        proof.path(center, center).status != NavStatus::Success)
        throw std::runtime_error("Navigation runtime query proof failed");
    result->artifact_digest = content_digest(data);
    result->data = std::move(admitted);
    return result;
}
} // namespace forge::navigation_detail
