#include "foliage_collision.hpp"
#include "asset_bytes.hpp"
#include "authored_component.hpp"
#include "collision_selection.hpp"
#include "mesh_scatter_authoring.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/project.hpp>
#include <forge/runtime_world.hpp>
#include <set>

namespace forge {
namespace {
void require(bool condition, const char* code, const char* message) {
    if (!condition)
        throw AuthoringError(code, message);
}
std::string digest(const Json& value) {
    const auto text = value.dump();
    return asset_detail::content_digest(std::as_bytes(std::span(text)));
}
Json settings_json(const FoliageCollisionSettings& value) {
    return {{"radius", value.radius},
            {"offset", {{"x", value.offset[0]}, {"y", value.offset[1]}, {"z", value.offset[2]}}},
            {"mask", value.mask},
            {"include_sensors", value.include_sensors},
            {"surface", value.surface ? Json(value.surface) : Json()},
            {"ignored", value.ignored}};
}
void validate(const FoliageCollisionSettings& value) {
    require(std::isfinite(value.radius) && value.radius >= .001 && value.radius <= 100,
            "invalid_arguments", "Foliage clearance sphere radius must be 0.001 to 100 meters");
    for (const auto coordinate : value.offset)
        require(std::isfinite(coordinate) && std::abs(coordinate) <= 1000, "invalid_arguments",
                "Foliage clearance world offset must be within +/-1000 meters");
    const std::set<EntityId> distinct(value.ignored.begin(), value.ignored.end());
    require(value.ignored.size() <= physics_query_exclusion_limit - 2 &&
                distinct.size() == value.ignored.size(),
            "invalid_arguments", "Use at most 14 distinct additional ignored foliage objects");
    for (auto id : value.ignored)
        require(bool(id), "invalid_arguments",
                "Ignored foliage objects require complete entity IDs");
}
} // namespace
struct FoliageCollisionPreparation::Impl {
    struct Work {
        // Retire the native world/loaders before releasing project ownership.
        std::shared_ptr<const ProjectLease> lease;
        std::filesystem::path project;
        AssetId scene;
        std::uint64_t revision = 0;
        Json arguments, snapshot, authored_types, project_settings, catalog_document;
        std::shared_ptr<const AssetCatalog> catalog;
        FoliageCollisionSettings config;
        std::string fingerprint, operation, snapshot_digest;
        std::vector<LocalTranslation> origins;
        std::vector<bool> mask;
        std::vector<EntityRef> exclusions;
        unsigned cursor = 0, queried = 0, retained = 0;
        std::chrono::steady_clock::time_point deadline;
        std::unique_ptr<RuntimeWorld> world;
    };
    std::unique_ptr<Work> work;
    Json status{{"state", "idle"}};
    bool busy() const {
        return work && (status.at("state") == "preparing" || status.at("state") == "querying");
    }
    void finish(const char* state, const std::string& message) {
        if (work)
            work->world.reset();
        status["state"] = state;
        status["message"] = message;
        status.erase("result");
        work.reset();
    }
    void current_assets() const {
        work->lease->check();
        require(AssetCatalog::open_project(work->project).document() == work->catalog_document &&
                    ProjectSettings(work->project).document() == work->project_settings,
                "stale_assets",
                "Project settings or published assets changed during foliage clearance");
    }
};
FoliageCollisionPreparation::FoliageCollisionPreparation() : impl_(std::make_unique<Impl>()) {}
FoliageCollisionPreparation::~FoliageCollisionPreparation() = default;
bool FoliageCollisionPreparation::busy() const { return impl_->busy(); }
Json FoliageCollisionPreparation::state() const { return impl_->status; }
Json FoliageCollisionPreparation::read(const Scene& scene, const std::filesystem::path& project) {
    poll(scene, project);
    if (impl_->work && impl_->status.at("state") == "ready") {
        try {
            impl_->current_assets();
        } catch (const std::exception& error) {
            impl_->finish("failed", error.what());
        }
    }
    return state();
}
void FoliageCollisionPreparation::cancel(std::string reason) {
    if (impl_->work)
        impl_->finish("cancelled", reason);
}
Json FoliageCollisionPreparation::start(std::shared_ptr<const ProjectLease> lease,
                                        const Scene& scene, Json arguments,
                                        FoliageCollisionSettings config) {
    require(bool(lease), "project_unavailable",
            "Open a project before preparing foliage clearance");
    lease->check();
    validate(config);
    const auto revision = scene.revision();
    const auto settings = detail::mesh_scatter_settings(arguments);
    const auto placements = detail::mesh_scatter_layout(settings);
    (void)preview_authoring(
        scene, Json::array({{{"operation", "mesh.scatter"}, {"arguments", arguments}}}));
    const auto snapshot = scene.snapshot();
    const auto authored_types = scene.authored_types();
    auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(lease->root()));
    const Json project_settings = ProjectSettings(lease->root()).document();
    const auto fingerprint = digest(Json{{"project", path_utf8(lease->root())},
                                         {"scene_revision", revision},
                                         {"snapshot", snapshot},
                                         {"authored_types", authored_types},
                                         {"arguments", arguments},
                                         {"settings", settings_json(config)},
                                         {"catalog", catalog->document()},
                                         {"project_settings", project_settings}});
    if (impl_->work && impl_->work->fingerprint == fingerprint &&
        (impl_->busy() || impl_->status.at("state") == "ready"))
        return state();
    require(!impl_->busy(), "foliage_collision_busy",
            "Cancel or finish the active foliage clearance first");
    auto next = std::make_unique<Impl::Work>();
    next->lease = std::move(lease);
    next->project = next->lease->root();
    next->scene = scene.asset_id();
    next->revision = revision;
    next->arguments = std::move(arguments);
    next->snapshot = snapshot;
    next->authored_types = authored_types;
    next->snapshot_digest = digest(snapshot);
    next->project_settings = project_settings;
    next->catalog_document = catalog->document();
    next->catalog = std::move(catalog);
    next->config = std::move(config);
    next->fingerprint = fingerprint;
    next->operation = AssetId::generate().str();
    next->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::set<EntityId> ignored(next->config.ignored.begin(), next->config.ignored.end());
    ignored.insert(next->arguments.at("entity").get<EntityId>());
    if (next->config.surface)
        ignored.insert(next->config.surface);
    for (const auto id : ignored)
        next->exclusions.push_back({next->scene, id});
    next->mask.assign(placements.size(), true);
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (next->arguments.contains("placement_mask"))
            next->mask[i] = next->arguments.at("placement_mask")[i].get<bool>();
        const double height = next->arguments.contains("world_heights")
                                  ? next->arguments.at("world_heights")[i].get<double>()
                                  : settings.center[1];
        LocalTranslation origin{
            settings.center[0] + placements[i].offset[0] + next->config.offset[0],
            height + next->config.offset[1],
            settings.center[2] + placements[i].offset[1] + next->config.offset[2]};
        for (const auto coordinate : {origin.x, origin.y, origin.z})
            require(std::isfinite(coordinate) && std::abs(coordinate) <= 1e9, "invalid_arguments",
                    "Clearance candidate origins must fit physics +/-1 billion meters");
        next->origins.push_back(origin);
    }
    require(scene.revision() == revision, "stale_revision",
            "Scene changed while starting foliage clearance");
    impl_->status = {{"state", "preparing"},
                     {"operation", next->operation},
                     {"scene", next->scene},
                     {"scene_revision", revision},
                     {"candidate_count", placements.size()},
                     {"checked_count", 0},
                     {"retained_count", 0},
                     {"profile", "world-sphere-v1"},
                     {"message", "Preparing collision assets"}};
    impl_->work = std::move(next);
    return state();
}
void FoliageCollisionPreparation::poll(const Scene& scene, const std::filesystem::path& project) {
    if (!impl_->work)
        return;
    auto& work = *impl_->work;
    if (project != work.project || scene.asset_id() != work.scene ||
        scene.revision() != work.revision) {
        impl_->finish("cancelled", "Source Scene or project changed");
        return;
    }
    if (!impl_->busy())
        return;
    try {
        work.lease->check();
        require(std::chrono::steady_clock::now() <= work.deadline, "foliage_collision_timeout",
                "Foliage clearance exceeded its 30-second deadline");
        if (!work.world) {
            work.world = std::make_unique<RuntimeWorld>(
                std::vector<EngineModule>{detail::copied_authoring_module(
                    work.authored_types, role_mask(WorldRole::Runtime))},
                ProjectSettings(work.project, work.project_settings).physics(), std::nullopt,
                work.project, false);
            work.world->scene.restore_snapshot(work.snapshot);
            work.snapshot = Json();
            work.authored_types = Json();
            return;
        }
        if (impl_->status.at("state") == "preparing") {
            if (!work.world->prepare_physics_assets())
                return;
            work.world->physics()->synchronize(0);
            impl_->current_assets();
            const auto native = work.world->physics()->status();
            for (const auto& resource : native.at("collision_resources")) {
                const auto asset = resource.at("asset").get<AssetId>();
                require(resource.at("ready") == true &&
                            resource.at("revision") ==
                                selected_collision_revision(*work.catalog, {asset}),
                        "stale_assets",
                        "Native collision differs from the captured published selection");
            }
            impl_->status["state"] = "querying";
            impl_->status["message"] = "Checking foliage clearance";
        }
        PhysicsOverlap query;
        query.dimensions = {work.config.radius, 0, 0};
        const PhysicsQueryFilter filter{work.config.mask, work.config.include_sensors,
                                        work.exclusions};
        unsigned batch = 0;
        while (work.cursor < work.origins.size() && batch < 16) {
            const auto index = work.cursor++;
            if (!work.mask[index])
                continue;
            query.origin = work.origins[index];
            work.mask[index] = !work.world->physics()->overlap_any(query, filter);
            work.retained += work.mask[index];
            ++work.queried;
            ++batch;
        }
        impl_->status["checked_count"] = work.queried;
        impl_->status["retained_count"] = work.retained;
        if (work.cursor != work.origins.size())
            return;
        impl_->current_assets();
        require(work.retained != 0, "clearance_excluded",
                "No foliage candidates pass collision clearance; no objects were created");
        auto resolved = work.arguments;
        resolved["placement_mask"] = work.mask;
        (void)preview_authoring(
            scene, Json::array({{{"operation", "mesh.scatter"}, {"arguments", resolved}}}));
        impl_->status["result"] = {{"arguments", std::move(resolved)},
                                   {"physics_tick", work.world->physics()->status().at("tick")},
                                   {"scene_revision", work.revision},
                                   {"candidate_count", work.origins.size()},
                                   {"retained_count", work.retained},
                                   {"source_digest", work.snapshot_digest},
                                   {"catalog_digest", digest(work.catalog_document)},
                                   {"settings", settings_json(work.config)}};
        impl_->status["state"] = "ready";
        impl_->status["message"] = "Foliage clearance ready";
        work.world.reset();
    } catch (const std::exception& error) {
        impl_->finish("failed", error.what());
    }
}
} // namespace forge
