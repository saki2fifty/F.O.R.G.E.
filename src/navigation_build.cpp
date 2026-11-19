#include "asset_bytes.hpp"
#include "collision_selection.hpp"
#include "model_render_resource.hpp"
#include "native_io_path.hpp"
#include "navigation_asset.hpp"
#include "navigation_bake.hpp"
#include "navigation_sources.hpp"
#include "navigation_tile_geometry.hpp"
#include "tile_asset_resources.hpp"
#include "tile_map_variants.hpp"
#include "worker_staging.hpp"
#include <algorithm>
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
namespace forge {
namespace {
using namespace navigation_detail;
using namespace asset_detail;
using Json = nlohmann::json;
std::string index_digest(const std::filesystem::path& index) {
    return std::filesystem::exists(index) ? content_digest(read_bytes(index, max_asset_index_bytes))
                                          : "absent";
}

} // namespace
struct NavigationCandidate::Impl {
    std::filesystem::path root;
    std::unique_ptr<NavigationBake> bake;
    AssetCatalog catalog;
    std::string baseline, geometry_digest;
    AssetRecord record;
    bool published = false;
    explicit Impl(const std::filesystem::path& project)
        : root(ProjectPaths(project).root()), catalog(root) {
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
    const auto source = capture(doc, settings);
    const auto profiles = navigation_profiles(c->catalog, source.scene);
    const auto previous = profiles.find(settings.profile);
    const auto id = previous == profiles.end() ? AssetId::generate() : previous->second;
    const auto catalog = std::make_shared<const AssetCatalog>(c->catalog);
    const auto inputs = mesh_inputs(
        source, [&](AssetId id) { return asset_detail::selected_mesh_revision(*catalog, {id}); });
    ResourcePool<MeshAsset> meshes({1, max_mesh_inputs, max_mesh_inputs, mesh_bytes});
    std::map<AssetId, ResourceLease<MeshAsset>> selected;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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
            throw std::runtime_error("Navigation source resource admission timed out");
        return lease;
    };
    std::uint64_t geometry_retained = 0;
    for (auto id : source.meshes) {
        auto lease = acquire(
            meshes, asset_detail::request_model_mesh(meshes, c->root, catalog, {id}, mesh_limits));
        geometry_retained += lease->resident_bytes();
        selected.emplace(id, std::move(lease));
    }
    const auto collision_provenance = collision_inputs(
        source, [&](AssetId id) { return selected_collision_revision(*catalog, {id}); });
    std::unique_ptr<ResourcePool<CollisionAsset>> collisions;
    std::map<AssetId, ResourceLease<CollisionAsset>> selected_collisions;
    if (!source.collisions.empty())
        collisions = std::make_unique<ResourcePool<CollisionAsset>>(
            ResourcePoolLimits{1, max_mesh_inputs, max_mesh_inputs, mesh_bytes});
    for (auto id : source.collisions) {
        auto lease = acquire(*collisions, request_collision(*collisions, c->root, catalog, {id}));
        geometry_retained += lease->resident_bytes;
        if (geometry_retained > mesh_bytes)
            throw std::runtime_error("Navigation Mesh/Collision inputs exceed64MiB retained data");
        selected_collisions.emplace(id, std::move(lease));
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
        },
        [&](AssetId id) { return snapshot_physics_debug_collision(selected_collisions.at(id)); },
        collision_provenance);
    c->geometry_digest = g.digest;
    auto metadata =
        navigation_bake_metadata(id, source, g, settings, inputs, tiles, collision_provenance);
    const auto version = metadata.at("version").get<unsigned>();
    c->bake = prepare_navigation_bake(g, std::move(metadata), worker, c->root, cancel);
    const auto& admitted = c->bake->data;
    c->record = {id,
                 NavMeshAsset::type,
                 std::filesystem::path("Assets/Navigation/Generated") / id.str() /
                     (AssetId::generate().str() + ".fnav"),
                 version,
                 {g.scene},
                 admitted.metadata};
    c->record.metadata["sha256"] = c->bake->artifact_digest;
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
    for (const auto& input : collision_provenance) {
        const auto collision = input.at("asset_id").get<AssetId>();
        c->record.dependencies.push_back(collision);
        c->record.dependency_edges.push_back({collision, CollisionAsset::type,
                                              AssetDependencyKind::Build, "navigation.collision",
                                              input.at("revision")});
    }
    if (cancel.stop_requested())
        throw std::runtime_error("Navigation build cancelled");
    return NavigationCandidate(std::move(c));
}
AssetRecord NavigationCandidate::publish(const Json& current) {
    if (!impl_ || !impl_->bake || impl_->published)
        throw std::runtime_error("Navigation candidate already consumed");
    auto& c = *impl_;
    auto index = AssetCatalog::project_index(c.root);
    if (index_digest(index) != c.baseline ||
        navigation_geometry_digest(c.root, current, c.record.metadata) != c.geometry_digest)
        throw std::runtime_error("Navigation source/catalog changed; candidate discarded");
    auto data = read_bytes(c.bake->staging.path / "navigation.fnav", max_nav_bytes + 65556);
    if (content_digest(data) != c.record.metadata.at("sha256").get<std::string>())
        throw std::runtime_error("Navigation candidate changed before publication");
    (void)admit(data);
    ProjectPaths paths(c.root);
    auto destination = paths.resolve(c.record.source);
    const auto native_destination = native_io_path(destination);
    std::filesystem::create_directories(native_destination.parent_path());
    if (std::filesystem::exists(native_destination))
        throw std::runtime_error("Navigation artifact revision already exists");
    std::filesystem::rename(native_io_path(c.bake->staging.path / "navigation.fnav"),
                            native_destination);
    try {
        if (c.catalog.records().contains(c.record.id))
            c.catalog.replace(c.record);
        else
            c.catalog.add(c.record);
        c.catalog.save(index);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(native_destination, ec);
        throw;
    }
    c.published = true;
    return c.record;
}
std::string navigation_geometry_digest(const std::filesystem::path& project, const Json& doc,
                                       const Json& meta) {
    if (meta.at("version") == 1) {
        const auto settings = provenance_settings(meta);
        return geometry(capture(doc, settings.plane, settings.profile)).digest;
    }
    const auto settings = provenance_settings(meta);
    return selected_geometry_digest(project, capture(doc, settings), meta);
}
} // namespace forge
