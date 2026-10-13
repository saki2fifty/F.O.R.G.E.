#include "asset_bytes.hpp"
#include "asset_worker.hpp"
#include "model_render_resource.hpp"
#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
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
    std::filesystem::path root, staging;
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
        staging =
            ProjectPaths(root).resolve(".forge/navigation-staging/" + AssetId::generate().str());
        std::filesystem::create_directories(staging);
    }
    ~Impl() {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
    }
};
NavigationCandidate::NavigationCandidate(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
NavigationCandidate::NavigationCandidate(NavigationCandidate&&) noexcept = default;
NavigationCandidate& NavigationCandidate::operator=(NavigationCandidate&&) noexcept = default;
NavigationCandidate::~NavigationCandidate() = default;
NavigationCandidate prepare_navigation(const std::filesystem::path& project, const Json& doc,
                                       NavigationSettings settings,
                                       const std::filesystem::path& worker,
                                       std::stop_token cancel) {
    validate_navigation_settings(settings);
    auto c = std::make_unique<NavigationCandidate::Impl>(project);
    const auto source = capture(doc);
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
    auto g = geometry(source, inputs,
                      [&](AssetId id) -> const MeshResourceData& { return selected.at(id).get(); });
    c->geometry_digest = g.digest;
    auto id = AssetId::generate();
    bool found = false;
    for (const auto& [key, record] : c->catalog.records())
        if (record.type == NavMeshAsset::type &&
            record.metadata.value("source_scene", std::string{}) == g.scene.str()) {
            if (found)
                throw std::runtime_error(
                    "Multiple navigation assets for this scene require explicit selection");
            id = key;
            found = true;
        }
    Json meta = {{"asset_id", id},
                 {"version", 2},
                 {"geometry_version", 2},
                 {"mesh_inputs", inputs},
                 {"source_scene", g.scene},
                 {"sources", g.sources},
                 {"geometry_sha256", g.digest},
                 {"settings", settings}};
    Json input = {{"metadata", meta}, {"vertices", g.vertices}};
    auto text = input.dump();
    write(c->staging / "source.json", std::as_bytes(std::span(text)));
    try {
        run_worker(WorkerKind::Navigation, worker, c->staging, cancel);
    } catch (const std::exception&) {
        if (std::filesystem::is_regular_file(c->staging / "error.txt")) {
            auto error = read_bytes(c->staging / "error.txt", 8192);
            throw std::runtime_error(
                std::string(reinterpret_cast<const char*>(error.data()), error.size()));
        }
        throw;
    }
    auto data = read_bytes(c->staging / "navigation.fnav", max_nav_bytes + 65556);
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
                 2,
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
    auto data = read_bytes(c.staging / "navigation.fnav", max_nav_bytes + 65556);
    if (content_digest(data) != c.record.metadata.at("sha256").get<std::string>())
        throw std::runtime_error("Navigation candidate changed before publication");
    (void)admit(data);
    ProjectPaths paths(c.root);
    auto destination = paths.resolve(c.record.source);
    std::filesystem::create_directories(destination.parent_path());
    if (std::filesystem::exists(destination))
        throw std::runtime_error("Navigation artifact revision already exists");
    std::filesystem::rename(c.staging / "navigation.fnav", destination);
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
std::string navigation_geometry_digest(const std::filesystem::path& project, const Json& doc,
                                       const Json& meta) {
    if (meta.at("version") == 1)
        return navigation_geometry_digest(doc);
    if (meta.at("version") != 2 || meta.at("geometry_version") != 2)
        throw std::runtime_error("Unsupported navigation geometry provenance version");
    auto source = capture(doc);
    auto catalog = AssetCatalog::open_project(project);
    auto inputs = mesh_inputs(
        source, [&](AssetId id) { return asset_detail::selected_mesh_revision(catalog, {id}); });
    return source_digest(source, inputs);
}
} // namespace forge
