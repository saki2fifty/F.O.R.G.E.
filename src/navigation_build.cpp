#include "asset_bytes.hpp"
#include "asset_worker.hpp"
#include "model_render_resource.hpp"
#include "navigation_asset.hpp"
#include "navigation_sources.hpp"
#include "navigation_tile_geometry.hpp"
#include "tile_asset_resources.hpp"
#include "tile_map_variants.hpp"
#include "worker_staging.hpp"
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
namespace forge {
namespace {
using namespace navigation_detail;
using namespace asset_detail;
using Json = nlohmann::json;
std::string index_digest(const std::filesystem::path& index) {
    return std::filesystem::exists(index) ? content_digest(read_bytes(index, max_asset_index_bytes))
                                          : "absent";
}
void write(const std::filesystem::path& p, std::span<const std::byte> data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out ||
        !out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size())) ||
        !out.flush())
        throw std::runtime_error("Cannot stage navigation candidate");
}
} // namespace
struct NavigationCandidate::Impl {
    std::filesystem::path root;
    WorkerStaging work;
    AssetCatalog catalog;
    std::string baseline, geometry_digest;
    AssetRecord record;
    bool published = false;
    explicit Impl(const std::filesystem::path& project)
        : root(ProjectPaths(project).root()), work(root, "navigation"), catalog(root) {
        baseline = index_digest(AssetCatalog::project_index(root));
        catalog = AssetCatalog::open_project(root);
        if (index_digest(AssetCatalog::project_index(root)) != baseline)
            throw std::runtime_error("Catalog changed during navigation preparation");
    }
};
NavigationCandidate::NavigationCandidate(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
NavigationCandidate::NavigationCandidate(NavigationCandidate&&) noexcept = default;
NavigationCandidate& NavigationCandidate::operator=(NavigationCandidate&&) noexcept = default;
NavigationCandidate::~NavigationCandidate() = default;
std::map<std::string, AssetId> navigation_profiles(const AssetCatalog& catalog, AssetId scene) {
    std::map<std::string, AssetId> profiles;
    for (const auto& [id, record] : catalog.records())
        if (record.type == NavMeshAsset::type &&
            record.metadata.value("source_scene", std::string{}) == scene.str()) {
            // Identity selection validates only the name. A valid rebuild must
            // still be able to repair damaged numeric settings on this asset.
            NavigationSettings settings;
            settings.profile = record.metadata.at("settings").value("profile", settings.profile);
            validate_navigation_settings(settings);
            if (!profiles.emplace(settings.profile, id).second)
                throw std::runtime_error("Multiple navigation assets use profile '" +
                                         settings.profile + "' in this Scene");
        }
    return profiles;
}
NavigationCandidate prepare_navigation(const std::filesystem::path& project, const Json& doc,
                                       NavigationSettings settings,
                                       const std::filesystem::path& worker,
                                       std::stop_token cancel) {
    validate_navigation_settings(settings);
    auto c = std::make_unique<NavigationCandidate::Impl>(project);
    const auto source = capture(doc, settings.plane, settings.profile);
    const auto profiles = navigation_profiles(c->catalog, source.scene);
    const auto previous = profiles.find(settings.profile);
    const auto id = previous == profiles.end() ? AssetId::generate() : previous->second;
    const auto catalog = std::make_shared<const AssetCatalog>(c->catalog);
    const auto inputs = mesh_inputs(
        source, [&](AssetId id) { return asset_detail::selected_mesh_revision(*catalog, {id}); });
    ResourcePool<MeshAsset> meshes({1, max_mesh_inputs, max_mesh_inputs, mesh_bytes});
    std::map<AssetId, ResourceLease<MeshAsset>> selected;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (auto id : source.meshes) {
        auto ticket = asset_detail::request_model_mesh(meshes, c->root, catalog, {id}, mesh_limits);
        while (!cancel.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            if (meshes.wait(ticket, std::chrono::milliseconds(10)))
                break;
            if (ticket.inspect().state == ResourceState::Failed)
                throw std::runtime_error(ticket.inspect().diagnostic);
        }
        if (cancel.stop_requested())
            throw std::runtime_error("Navigation build cancelled");
        auto lease = meshes.acquire(ticket);
        if (!lease)
            throw std::runtime_error("Navigation Mesh admission timed out");
        selected.emplace(id, std::move(lease));
    }
    const auto tiles =
        tile_inputs(source, [&](AssetId id) { return selected_tile_map_revision(*catalog, {id}); });
    std::unique_ptr<ResourcePool<TileMapAsset>> maps;
    std::unique_ptr<ResourcePool<TileSetAsset>> definitions;
    if (!source.maps.empty()) {
        const ResourcePoolLimits limits{1, max_tile_inputs, max_tile_inputs, mesh_bytes};
        maps = std::make_unique<ResourcePool<TileMapAsset>>(limits);
        definitions = std::make_unique<ResourcePool<TileSetAsset>>(limits);
    }
    struct SelectedMap {
        ResourceLease<TileMapAsset> map;
        ResourceLease<TileSetAsset> definition;
        std::shared_ptr<const TileMapData> cells;
        std::shared_ptr<const TileSetData> values;
        std::shared_ptr<const TileMapVariants> variants;
    };
    std::map<AssetId, SelectedMap> selected_maps;
    std::map<AssetId, ResourceLease<TileSetAsset>> selected_definitions;
    std::uint64_t retained = 0;
    const auto acquire = [&](auto& pool, const auto& ticket) {
        while (!cancel.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            if (pool.wait(ticket, std::chrono::milliseconds(10)))
                break;
            if (ticket.inspect().state == ResourceState::Failed)
                throw std::runtime_error(ticket.inspect().diagnostic);
        }
        if (cancel.stop_requested())
            throw std::runtime_error("Navigation build cancelled");
        auto lease = pool.acquire(ticket);
        if (!lease)
            throw std::runtime_error("Navigation Tile Map admission timed out");
        return lease;
    };
    for (auto id : source.maps) {
        auto map = acquire(*maps, request_tile_map_compact(*maps, c->root, catalog, {id}));
        auto cells = std::make_shared<const TileMapData>(map.get());
        const auto set_id = cells->tile_set();
        if (!selected_definitions.contains(set_id)) {
            auto definition =
                acquire(*definitions, request_tile_set(*definitions, c->root, catalog, {set_id}));
            retained += definition->resident_bytes();
            selected_definitions.emplace(set_id, std::move(definition));
        }
        const TileMapData* copies[]{&map.get(), cells.get()};
        retained += TileMapData::resident_bytes(copies);
        if (retained > mesh_bytes)
            throw std::runtime_error("Navigation Tile Map inputs exceed64MiB retained data");
        auto definition = selected_definitions.at(set_id);
        auto values = std::make_shared<const TileSetData>(definition.get());
        auto variants = resolve_tile_variants(cells, values);
        if (const TileMapVariants* variant = variants.get())
            retained += TileMapVariants::resident_bytes({&variant, 1});
        if (retained > mesh_bytes)
            throw std::runtime_error("Navigation Tile Map variants exceed64MiB retained data");
        selected_maps.emplace(id,
                              SelectedMap{std::move(map), std::move(definition), std::move(cells),
                                          std::move(values), std::move(variants)});
    }
    const auto& resources = settings.plane == NavigationPlane::PlanarXY ? tiles : inputs;
    auto g = geometry(
        source, resources,
        [&](AssetId id) -> const MeshResourceData& { return selected.at(id).get(); },
        [&](const Surface& surface) {
            const auto& value = selected_maps.at(surface.collider->map);
            return tile_collider_outlines(*value.cells, *value.values, value.variants.get(),
                                          surface.collider->layer, surface.transform,
                                          surface.region->obstacle);
        });
    c->geometry_digest = g.digest;
    const unsigned geometry_version =
        settings.plane == NavigationPlane::PlanarXY ? (collider_sources(source) ? 5 : 3) : 2;
    const unsigned version =
        !g.connections.empty() ? 6 : (settings.dynamic_obstacles ? 4 : geometry_version);
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
    if (geometry_version == 5)
        meta["tile_inputs"] = tiles;
    Json input = {{"metadata", meta}, {"vertices", g.vertices}};
    if (settings.plane == NavigationPlane::PlanarXY)
        input["obstacles"] = g.obstacles;
    auto text = input.dump();
    write(c->work.path / "source.json", std::as_bytes(std::span(text)));
    try {
        run_worker(WorkerKind::Navigation, worker, c->work.path, cancel, {}, c->work.owner.get());
    } catch (const std::exception&) {
        if (std::filesystem::is_regular_file(c->work.path / "error.txt")) {
            auto error = read_bytes(c->work.path / "error.txt", 8192);
            throw std::runtime_error(
                std::string(reinterpret_cast<const char*>(error.data()), error.size()));
        }
        throw;
    }
    auto data = read_bytes(c->work.path / "navigation.fnav", max_nav_bytes + 65556);
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
    c->record = {id,
                 NavMeshAsset::type,
                 std::filesystem::path("Assets/Navigation/Generated") / id.str() /
                     (AssetId::generate().str() + ".fnav"),
                 version,
                 {g.scene},
                 admitted.metadata};
    c->record.metadata["sha256"] = content_digest(data);
    c->record.dependency_edges = {
        {g.scene, "scene", AssetDependencyKind::Build, "navigation.scene", g.digest}};
    for (const auto& input : inputs) {
        const auto mesh = input.at("asset_id").get<AssetId>();
        c->record.dependencies.push_back(mesh);
        c->record.dependency_edges.push_back({mesh, MeshAsset::type, AssetDependencyKind::Build,
                                              "navigation.mesh", input.at("revision")});
    }
    for (const auto& input : tiles) {
        const auto map = input.at("asset_id").get<AssetId>();
        c->record.dependencies.push_back(map);
        c->record.dependency_edges.push_back({map, TileMapAsset::type, AssetDependencyKind::Build,
                                              "navigation.tile_map", input.at("revision")});
    }
    if (cancel.stop_requested())
        throw std::runtime_error("Navigation build cancelled");
    return NavigationCandidate(std::move(c));
}
AssetRecord NavigationCandidate::publish(const Json& current) {
    if (!impl_ || impl_->published)
        throw std::runtime_error("Navigation candidate already consumed");
    auto& c = *impl_;
    auto index = AssetCatalog::project_index(c.root);
    if (index_digest(index) != c.baseline ||
        navigation_geometry_digest(c.root, current, c.record.metadata) != c.geometry_digest)
        throw std::runtime_error("Navigation source/catalog changed; candidate discarded");
    auto data = read_bytes(c.work.path / "navigation.fnav", max_nav_bytes + 65556);
    if (content_digest(data) != c.record.metadata.at("sha256").get<std::string>())
        throw std::runtime_error("Navigation candidate changed before publication");
    (void)admit(data);
    ProjectPaths paths(c.root);
    auto destination = paths.resolve(c.record.source);
    std::filesystem::create_directories(destination.parent_path());
    if (std::filesystem::exists(destination))
        throw std::runtime_error("Navigation artifact revision already exists");
    std::filesystem::rename(c.work.path / "navigation.fnav", destination);
    try {
        if (c.catalog.records().contains(c.record.id))
            c.catalog.replace(c.record);
        else
            c.catalog.add(c.record);
        c.catalog.save(index);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        throw;
    }
    c.published = true;
    return c.record;
}
namespace {
bool mesh_provenance(const Json& meta, const Sources& source) {
    // Legacy digest-only callers predate settings metadata.
    if (meta.at("version") == 1) {
        if (source.plane != NavigationPlane::Spatial3D)
            throw std::runtime_error("Legacy navigation requires spatial sources");
        return false;
    }
    const auto settings = provenance_settings(meta);
    if (settings.plane != source.plane)
        throw std::runtime_error("Navigation source/provenance plane mismatch");
    return settings.plane == NavigationPlane::Spatial3D;
}
} // namespace
std::string navigation_detail::selected_geometry_digest(const std::filesystem::path& project,
                                                        const Sources& source, const Json& meta) {
    if (!mesh_provenance(meta, source) && source.maps.empty())
        return collider_sources(source) ? source_digest(source, Json::array())
                                        : geometry(source).digest;
    auto catalog = AssetCatalog::open_project(project);
    return selected_geometry_digest(catalog, source, meta);
}
std::string navigation_detail::selected_geometry_digest(const AssetCatalog& catalog,
                                                        const Sources& source, const Json& meta) {
    if (!mesh_provenance(meta, source)) {
        const auto tiles = tile_inputs(
            source, [&](AssetId id) { return selected_tile_map_revision(catalog, {id}); });
        return collider_sources(source) ? source_digest(source, tiles) : geometry(source).digest;
    }
    auto inputs = mesh_inputs(
        source, [&](AssetId id) { return asset_detail::selected_mesh_revision(catalog, {id}); });
    return source_digest(source, inputs);
}
std::string navigation_geometry_digest(const std::filesystem::path& project, const Json& doc,
                                       const Json& meta) {
    if (meta.at("version") == 1) {
        const auto settings = provenance_settings(meta);
        return geometry(capture(doc, settings.plane, settings.profile)).digest;
    }
    const auto settings = provenance_settings(meta);
    return selected_geometry_digest(project, capture(doc, settings.plane, settings.profile), meta);
}
} // namespace forge
