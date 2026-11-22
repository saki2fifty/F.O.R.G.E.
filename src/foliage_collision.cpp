#include "foliage_collision.hpp"
#include "asset_bytes.hpp"
#include "authored_component.hpp"
#include "collision_selection.hpp"
#include "mesh_scatter_authoring.hpp"
#include "render_bounds.hpp"
#include "spatial_document.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/mesh_resource.hpp>
#include <forge/project.hpp>
#include <forge/runtime_world.hpp>
#include <limits>
#include <set>
#include <type_traits>
#include <variant>

namespace forge {
std::span<const FoliageClearanceChoice> foliage_clearance_choices() {
    static constexpr FoliageClearanceChoice choices[]{
        {FoliageClearanceProfile::WorldSphere, "world-sphere-v1", "World sphere", false, false},
        {FoliageClearanceProfile::MeshBounds, "mesh-bounds-v1", "Template Mesh bounds", true,
         false},
        {FoliageClearanceProfile::CircleXY, "world-circle-xy-v1", "World circle (XY)", false, true},
        {FoliageClearanceProfile::MeshBoundsXY, "mesh-bounds-xy-v1", "Template Mesh bounds (XY)",
         true, true}};
    return choices;
}
const FoliageClearanceChoice& foliage_clearance_choice(FoliageClearanceProfile profile) {
    for (const auto& choice : foliage_clearance_choices())
        if (choice.profile == profile)
            return choice;
    throw AuthoringError("invalid_arguments", "Unknown foliage clearance profile");
}
FoliageClearanceProfile foliage_clearance_profile(std::string_view key) {
    for (const auto& choice : foliage_clearance_choices())
        if (key == choice.key)
            return choice.profile;
    throw AuthoringError("invalid_arguments", "Unknown foliage clearance profile");
}
Json foliage_collision_settings(const FoliageCollisionSettings& value) {
    return {{"radius", value.radius},
            {"offset", {{"x", value.offset[0]}, {"y", value.offset[1]}, {"z", value.offset[2]}}},
            {"mask", value.mask},
            {"include_sensors", value.include_sensors},
            {"avoid_new_overlaps", value.avoid_new_overlaps},
            {"profile", foliage_clearance_choice(value.profile).key},
            {"padding", value.padding},
            {"surface", value.surface ? Json(value.surface) : Json()},
            {"ignored", value.ignored}};
}
namespace {
void require(bool condition, const char* code, const char* message) {
    if (!condition)
        throw AuthoringError(code, message);
}
std::string digest(const Json& value) {
    const auto text = value.dump();
    return asset_detail::content_digest(std::as_bytes(std::span(text)));
}
void validate(const FoliageCollisionSettings& value) {
    (void)foliage_clearance_choice(value.profile);
    require(std::isfinite(value.padding) && value.padding >= 0 && value.padding <= 100,
            "invalid_arguments", "Foliage Mesh bounds padding must be 0 to 100 meters");
    require(std::isfinite(value.radius) && value.radius >= .001 && value.radius <= 100,
            "invalid_arguments", "Foliage clearance radius must be 0.001 to 100 meters");
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
template <class Query>
Query bounds_query(const RenderBounds& bounds, const FoliageCollisionSettings& config) {
    Query box;
    box.shape = Query::Shape::Box;
    Double3 center{};
    for (unsigned axis = 0; axis < box.dimensions.size(); ++axis) {
        center[axis] = bounds.minimum[axis] + (bounds.maximum[axis] - bounds.minimum[axis]) * .5;
        const double half =
            std::max(center[axis] - bounds.minimum[axis], bounds.maximum[axis] - center[axis]);
        const double size = std::max(.001, 2 * (half + config.padding));
        float rounded = float(size);
        if (double(rounded) < size)
            rounded = std::nextafter(rounded, std::numeric_limits<float>::infinity());
        box.dimensions[axis] = rounded;
        center[axis] += config.offset[axis];
        require(std::isfinite(center[axis]) && std::abs(center[axis]) <= 1e9 &&
                    std::isfinite(box.dimensions[axis]) && box.dimensions[axis] <= 10000,
                "invalid_arguments", "Mesh clearance bounds exceed physics limits");
    }
    if constexpr (std::is_same_v<Query, PhysicsOverlap2D>)
        box.origin = {center[0], center[1]};
    else
        box.origin = {center[0], center[1], center[2]};
    return box;
}
Double3 query_position(const PhysicsOverlap& query) {
    return {query.origin.x, query.origin.y, query.origin.z};
}
const Double2& query_position(const PhysicsOverlap2D& query) { return query.origin; }
template <class Query> bool boxes_overlap(const Query& first, const Query& second) {
    const auto a = query_position(first), b = query_position(second);
    for (unsigned axis = 0; axis < first.dimensions.size(); ++axis)
        if (std::abs(a[axis] - b[axis]) >= .5 * (first.dimensions[axis] + second.dimensions[axis]))
            return false;
    return true;
}
} // namespace
MeshBounds foliage_template_bounds(const MeshResourceData& data) {
    MeshLimits limits;
    limits.bytes = 8 * 1024 * 1024;
    limits.vertices = 16384;
    limits.indices = 49152;
    limits.parts = 32;
    validate_mesh(data.mesh, limits);
    require(data.mesh.morph_names.empty() && data.mesh.morph_defaults.empty(),
            "unsupported_template",
            "Mesh bounds clearance requires a static Mesh without morph targets");
    if (data.model)
        require(
            std::none_of(data.model->nodes.begin(), data.model->nodes.end(),
                         [](const auto& node) { return node.skin || !node.morph_weights.empty(); }),
            "unsupported_template", "Mesh bounds clearance does not sample deformed Model nodes");
    std::optional<MeshBounds> local;
    for (const auto& lod : data.mesh.lods)
        for (const auto& part : lod.parts) {
            require(part.joint_palette.empty() && part.morph_targets.empty(),
                    "unsupported_template",
                    "Mesh bounds clearance does not sample skin or morph deformation");
            require(std::none_of(part.streams.begin(), part.streams.end(),
                                 [](const auto& stream) {
                                     return stream.semantic.starts_with("JOINTS_") ||
                                            stream.semantic.starts_with("WEIGHTS_");
                                 }),
                    "unsupported_template",
                    "Mesh bounds clearance does not sample skin attributes");
            if (!local)
                local = part.bounds;
            else
                for (unsigned axis = 0; axis < 3; ++axis) {
                    local->minimum[axis] =
                        std::min(local->minimum[axis], part.bounds.minimum[axis]);
                    local->maximum[axis] =
                        std::max(local->maximum[axis], part.bounds.maximum[axis]);
                }
        }
    require(bool(local), "invalid_template", "Mesh bounds clearance needs geometry");
    return *local;
}
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
        std::string fingerprint, operation, snapshot_digest, command;
        std::vector<std::size_t> dab_counts;
        std::vector<LocalTranslation> origins;
        // Transient preparation inputs, discarded once the same ordered query
        // sequence is ready. Neither is a persistent geometry or placement owner.
        std::vector<LocalTransform> placements;
        std::variant<std::vector<PhysicsOverlap>, std::vector<PhysicsOverlap2D>> boxes;
        AssetId prototype_mesh;
        std::uint64_t mesh_token = 0;
        std::vector<bool> mask;
        std::vector<EntityRef> exclusions;
        unsigned cursor = 0, queried = 0, retained = 0, new_overlaps = 0;
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
    const std::string command = arguments.contains("dabs") ? "mesh.stroke" : "mesh.scatter";
    (void)preview_authoring(scene,
                            Json::array({{{"operation", command}, {"arguments", arguments}}}));
    const auto snapshot = scene.snapshot();
    const auto authored_types = scene.authored_types();
    auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(lease->root()));
    const Json project_settings = ProjectSettings(lease->root()).document();
    const auto fingerprint = digest(Json{{"project", path_utf8(lease->root())},
                                         {"scene_revision", revision},
                                         {"snapshot", snapshot},
                                         {"authored_types", authored_types},
                                         {"arguments", arguments},
                                         {"settings", foliage_collision_settings(config)},
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
    next->command = command;
    next->snapshot = snapshot;
    next->authored_types = authored_types;
    next->snapshot_digest = digest(snapshot);
    next->project_settings = project_settings;
    next->catalog_document = catalog->document();
    next->catalog = std::move(catalog);
    next->config = std::move(config);
    LocalTransform prototype;
    const auto& choice = foliage_clearance_choice(next->config.profile);
    if (choice.xy)
        next->boxes.emplace<std::vector<PhysicsOverlap2D>>();
    if (choice.mesh_bounds) {
        const auto effective = scene.effective_document();
        for (const auto& row : effective.at("entities"))
            if (row.at("id") == next->arguments.at("entity")) {
                const auto& components = row.at("components");
                const auto& renderer = components.at("forge.mesh_renderer");
                require(!renderer.contains("wind") || !renderer.at("wind").value("enabled", false),
                        "unsupported_template", "Mesh bounds clearance does not sample wind");
                prototype = detail::read_local(components);
                next->prototype_mesh = renderer.at("mesh").get<AssetId>();
            }
        require(bool(next->prototype_mesh), "invalid_arguments",
                "Mesh bounds clearance requires a selected Mesh template");
    }
    next->fingerprint = fingerprint;
    next->operation = AssetId::generate().str();
    next->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::set<EntityId> ignored(next->config.ignored.begin(), next->config.ignored.end());
    ignored.insert(next->arguments.at("entity").get<EntityId>());
    if (next->config.surface)
        ignored.insert(next->config.surface);
    for (const auto id : ignored)
        next->exclusions.push_back({next->scene, id});
    const auto append_dab = [&](const Json& dab) {
        const auto settings = detail::mesh_scatter_settings(dab);
        const auto placements = detail::mesh_scatter_layout(settings);
        require(next->origins.size() + placements.size() <= detail::mesh_scatter_limit,
                "limit_exceeded", "Foliage clearance allows at most 512 total candidates");
        next->dab_counts.push_back(placements.size());
        if (choice.mesh_bounds) {
            auto candidates = detail::mesh_scatter_candidates(prototype, dab);
            for (auto& candidate : candidates) {
                candidate.transform.translation.x += settings.center[0];
                candidate.transform.translation.y += settings.center[1];
                candidate.transform.translation.z += settings.center[2];
                next->placements.push_back(candidate.transform);
            }
        }
        for (std::size_t i = 0; i < placements.size(); ++i) {
            next->mask.push_back(!dab.contains("placement_mask") ||
                                 dab.at("placement_mask")[i].get<bool>());
            const double height = dab.contains("world_heights")
                                      ? dab.at("world_heights")[i].get<double>()
                                      : settings.center[1];
            LocalTranslation origin{
                settings.center[0] + placements[i].offset[0] + next->config.offset[0],
                height + next->config.offset[1],
                settings.center[2] + placements[i].offset[1] + next->config.offset[2]};
            const Double3 coordinates{origin.x, origin.y, origin.z};
            for (unsigned axis = 0; axis < (choice.xy ? 2u : 3u); ++axis)
                require(std::isfinite(coordinates[axis]) && std::abs(coordinates[axis]) <= 1e9,
                        "invalid_arguments",
                        "Clearance candidate origins must fit physics +/-1 billion meters");
            next->origins.push_back(origin);
        }
    };
    if (command == "mesh.stroke") {
        for (const auto& value : next->arguments.at("dabs")) {
            auto dab = value;
            dab["entity"] = next->arguments.at("entity");
            dab["name"] = next->arguments.at("name");
            append_dab(dab);
        }
    } else
        append_dab(next->arguments);
    require(scene.revision() == revision, "stale_revision",
            "Scene changed while starting foliage clearance");
    impl_->status = {{"state", "preparing"},
                     {"operation", next->operation},
                     {"scene", next->scene},
                     {"scene_revision", revision},
                     {"authoring_operation", command},
                     {"candidate_count", next->origins.size()},
                     {"checked_count", 0},
                     {"retained_count", 0},
                     {"new_overlap_count", 0},
                     {"profile", foliage_collision_settings(next->config).at("profile")},
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
            if (work.prototype_mesh)
                work.mesh_token = work.world->engine.services().resources()->request(
                    RuntimeResourceKind::Mesh, work.prototype_mesh);
            work.snapshot = Json();
            work.authored_types = Json();
            return;
        }
        if (impl_->status.at("state") == "preparing") {
            if (!work.world->prepare_physics_assets())
                return;
            if (work.mesh_token) {
                const auto resources = work.world->engine.services().resources();
                const auto state = resources->inspect(work.mesh_token);
                require(state.diagnostic.empty(), "invalid_template", state.diagnostic.c_str());
                if (state.state == "loading" || state.state == "queued" ||
                    state.state == "dependency pending")
                    return;
                const auto mesh = resources->mesh(work.mesh_token);
                require(state.state == "ready" && bool(mesh) &&
                            state.requested_revision == state.retained_revision,
                        "invalid_template", "Mesh bounds clearance needs a ready selected Mesh");
                const auto local = foliage_template_bounds(mesh.get());
                std::visit(
                    [&](auto& boxes) {
                        using Query = typename std::decay_t<decltype(boxes)>::value_type;
                        boxes.reserve(work.placements.size());
                        for (std::size_t index = 0; index < work.placements.size(); ++index) {
                            if (!work.mask[index]) {
                                boxes.emplace_back();
                                continue;
                            }
                            const auto bounds =
                                transform_bounds(local, affine_transform(work.placements[index]));
                            boxes.push_back(bounds_query<Query>(bounds, work.config));
                        }
                    },
                    work.boxes);
                work.placements.clear();
                resources->release(work.mesh_token);
                work.mesh_token = 0;
            }
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
        const auto& choice = foliage_clearance_choice(work.config.profile);
        const PhysicsQueryFilter filter{work.config.mask, work.config.include_sensors,
                                        work.exclusions};
        std::visit(
            [&](const auto& boxes) {
                using Query = typename std::decay_t<decltype(boxes)>::value_type;
                constexpr bool xy = std::is_same_v<Query, PhysicsOverlap2D>;
                Query query;
                query.dimensions[0] = work.config.radius;
                unsigned batch = 0;
                while (work.cursor < work.origins.size() && batch < 16) {
                    const auto index = work.cursor++;
                    if (!work.mask[index])
                        continue;
                    const auto& origin = work.origins[index];
                    if (choice.mesh_bounds)
                        query = boxes[index];
                    else if constexpr (xy)
                        query.origin = {origin.x, origin.y};
                    else
                        query.origin = origin;
                    if constexpr (xy)
                        work.mask[index] = !work.world->physics()->overlap_any_2d(query, filter);
                    else
                        work.mask[index] = !work.world->physics()->overlap_any(query, filter);
                    if (work.mask[index] && work.config.avoid_new_overlaps) {
                        // Earlier true entries are the retained ordered subset, across dabs.
                        for (unsigned previous = 0; previous < index; ++previous) {
                            if (!work.mask[previous])
                                continue;
                            bool overlaps;
                            if (choice.mesh_bounds)
                                overlaps = boxes_overlap(query, boxes[previous]);
                            else {
                                const auto& other = work.origins[previous];
                                const auto x = origin.x - other.x, y = origin.y - other.y;
                                const double distance =
                                    xy ? std::hypot(x, y) : std::hypot(x, y, origin.z - other.z);
                                overlaps = distance < 2 * work.config.radius;
                            }
                            if (overlaps) {
                                work.mask[index] = false;
                                ++work.new_overlaps;
                                break;
                            }
                        }
                    }
                    work.retained += work.mask[index];
                    ++work.queried;
                    ++batch;
                }
            },
            work.boxes);
        impl_->status["checked_count"] = work.queried;
        impl_->status["retained_count"] = work.retained;
        impl_->status["new_overlap_count"] = work.new_overlaps;
        if (work.cursor != work.origins.size())
            return;
        impl_->current_assets();
        require(work.retained != 0, "clearance_excluded",
                "No foliage candidates pass collision clearance; no objects were created");
        auto resolved = work.arguments;
        Json retained_dabs = Json::array();
        if (work.command == "mesh.stroke") {
            resolved["dabs"] = Json::array();
            std::size_t offset = 0;
            for (std::size_t dab = 0; dab < work.dab_counts.size(); ++dab) {
                Json selected = Json::array();
                bool retained = false;
                for (std::size_t i = offset; i < offset + work.dab_counts[dab]; ++i) {
                    selected.push_back(bool(work.mask[i]));
                    retained = retained || work.mask[i];
                }
                offset += work.dab_counts[dab];
                if (!retained)
                    continue;
                auto arguments = work.arguments.at("dabs")[dab];
                arguments["placement_mask"] = std::move(selected);
                resolved["dabs"].push_back(std::move(arguments));
                retained_dabs.push_back(dab);
            }
        } else
            resolved["placement_mask"] = work.mask;
        (void)preview_authoring(
            scene, Json::array({{{"operation", work.command}, {"arguments", resolved}}}));
        impl_->status["result"] = {{"arguments", std::move(resolved)},
                                   {"authoring_operation", work.command},
                                   {"physics_tick", work.world->physics()->status().at("tick")},
                                   {"scene_revision", work.revision},
                                   {"candidate_count", work.origins.size()},
                                   {"retained_count", work.retained},
                                   {"new_overlap_count", work.new_overlaps},
                                   {"source_digest", work.snapshot_digest},
                                   {"catalog_digest", digest(work.catalog_document)},
                                   {"settings", foliage_collision_settings(work.config)}};
        if (work.command == "mesh.stroke") {
            impl_->status["result"]["original_dab_count"] = work.dab_counts.size();
            impl_->status["result"]["retained_dab_indices"] = std::move(retained_dabs);
        }
        impl_->status["state"] = "ready";
        impl_->status["message"] = "Foliage clearance ready";
        work.world.reset();
    } catch (const std::exception& error) {
        impl_->finish("failed", error.what());
    }
}
} // namespace forge
