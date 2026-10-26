#pragma once
#include "authoring.hpp"
#include "document.hpp"
#include "navigation_sources.hpp"
#include "property_drawer.hpp"
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/navigation_build.hpp>
#include <future>
namespace forge {
class NavigationTools {
    friend struct NavigationAutomation;

  public:
    bool pending() const { return job_.valid(); }
    void overlay_control() {
        ImGui::Checkbox("Navigation overlay", &show_);
        ui::help("Show or hide navigation triangles and paths in the viewport; this does not "
                 "rebuild navigation.");
    }
    NavigationTools(WorldContext& context, std::filesystem::path worker)
        : context_(context),
          source_entities_(context.world()
                               .query_builder<const PersistentEntityId>()
                               .cache_kind(flecs::QueryCacheNone)
                               .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                               .build()),
          worker_(std::move(worker)) {}
    ~NavigationTools() { cancel_.request_stop(); }
    void build(Scene& scene, SceneDocument& project, NavigationSettings settings) {
        project.check_ownership();
        if (pending())
            throw std::runtime_error("Wait for the current navigation build");
        validate_navigation_settings(settings);
        const auto operation = AssetId::generate().str();
        auto doc = scene.effective_document();
        if (loaded_project_ != project.project() || loaded_scene_ != scene.asset_id()) {
            selected_ = {};
            metadata_ = nullptr;
            triangles_.clear();
        }
        job_project_ = loaded_project_ = project.project();
        job_scene_ = loaded_scene_ = scene.asset_id();
        settings_ = settings;
        cancel_ = std::stop_source{};
        const auto root = job_project_, tool = worker_;
        const auto token = cancel_.get_token();
        job_ = std::async(std::launch::async, [root, tool, doc = std::move(doc), settings, token] {
            return prepare_navigation(root, doc, settings, tool, token);
        });
        operation_ = operation;
        status_ = "Building navigation...";
        error_.clear();
    }
    void poll(Scene& scene, SceneDocument& project, bool playing, std::string& message) {
        // Content can be hidden while the viewport still draws this cache. Scene
        // UUIDs can also be identical in two independently copied projects.
        if (loaded_scene_ &&
            (loaded_project_ != project.project() || loaded_scene_ != scene.asset_id())) {
            loaded_project_.clear();
            loaded_scene_ = selected_ = {};
            observed_catalog_.reset();
            metadata_ = nullptr;
            triangles_.clear();
            settings_ = {};
            validation_at_ = {};
            validation_state_.clear();
            status_.clear();
            error_.clear();
        }
        if (!job_.valid())
            return;
        if (project.project() != job_project_ || scene.asset_id() != job_scene_ || playing)
            cancel_.request_stop();
        if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto candidate = job_.get();
            if (cancel_.stop_requested())
                throw std::runtime_error("Navigation build cancelled");
            project.check_ownership();
            auto record = candidate.publish(scene.effective_document());
            selected_ = record.id;
            metadata_ = record.metadata;
            validation_at_ = {};
            triangles_ = navigation_triangles(project.project(), {record.id});
            show_ = true;
            message = "Navigation built. Add a Navigation Agent, choose this navmesh and enter its "
                      "destination.";
            status_ = message;
            error_.clear();
        } catch (const std::exception& e) {
            message = e.what();
            status_ = "Navigation build failed; previous asset retained";
            error_ = message;
            ui::report_error("navigation_tools.hpp", message);
        }
    }
    void content(Scene& scene, SceneDocument& project, bool locked, std::string& message,
                 std::shared_ptr<const AssetCatalog> catalog) {
        if (!ImGui::CollapsingHeader("Navigation")) {
            ui::help(
                "Build static 3D navigation from shapes/Meshes or XY navigation from regions.");
            return;
        }
        ui::help("Asset publication is separate from scene Undo. Failed or stale builds retain the "
                 "previous navmesh.");
        if (!catalog || catalog->project_root() != project.project()) {
            ImGui::TextWrapped("Loading project assets for navigation...");
            return;
        }
        const bool catalog_changed = observed_catalog_.lock() != catalog;
        if (catalog_changed) {
            observed_catalog_ = catalog;
            validation_at_ = {};
        }
        try {
            const bool scene_changed =
                loaded_project_ != project.project() || loaded_scene_ != scene.asset_id();
            if (scene_changed || catalog_changed) {
                const auto old_selected = selected_;
                const auto old_metadata = metadata_;
                const bool settings_followed_saved =
                    settings_ == (metadata_.is_object() && metadata_.contains("settings")
                                      ? metadata_.at("settings").get<NavigationSettings>()
                                      : NavigationSettings{});
                loaded_project_ = project.project();
                loaded_scene_ = scene.asset_id();
                if (scene_changed)
                    settings_ = {};
                metadata_ = nullptr;
                validation_at_ = {};
                selected_ = {};
                for (const auto& [id, record] : catalog->records())
                    if (record.type == NavMeshAsset::type &&
                        record.metadata.value("source_scene", std::string{}) ==
                            loaded_scene_.str()) {
                        selected_ = id;
                        metadata_ = record.metadata;
                        if (scene_changed || settings_followed_saved)
                            settings_ = metadata_.at("settings").get<NavigationSettings>();
                        if (scene_changed || selected_ != old_selected || old_metadata != metadata_)
                            triangles_ = navigation_triangles(project.project(), {id});
                        break;
                    }
                if (!selected_)
                    triangles_.clear();
            }
        } catch (const std::exception& e) {
            message = e.what();
            ui::report_error("navigation_tools.hpp", message);
        }
        ImGui::BeginDisabled(locked || job_.valid());
        constexpr const char* planes[]{"3D (XZ ground)", "2D (XY)"};
        const bool choose_plane = ImGui::BeginCombo("Plane", planes[unsigned(settings_.plane)]);
        FORGE_UI_PROBE("navigation:setting:Plane");
        if (choose_plane) {
            for (unsigned i = 0; i < 2; ++i) {
                if (ImGui::Selectable(planes[i], unsigned(settings_.plane) == i))
                    settings_.plane = NavigationPlane(i);
                FORGE_UI_PROBE("navigation:plane:" + std::to_string(i));
            }
            ImGui::EndCombo();
        }
        ui::help("One navigation asset/profile per Scene. Changing plane requires an explicit "
                 "rebuild and keeps the asset identity.");
        auto field = [](const char* label, float& value, const char* help) {
            ImGui::InputFloat(label, &value, 0, 0, "%.3f");
            FORGE_UI_PROBE("navigation:setting:" + std::string(label));
            ui::help(help);
        };
        field("Agent radius", settings_.radius,
              "Body clearance in metres, .05 to 5. This navmesh has one agent profile.");
        if (settings_.plane == NavigationPlane::Spatial3D) {
            field("Agent height", settings_.height, "Standing clearance in metres, .2 to 10.");
            field("Maximum climb", settings_.climb,
                  "Allowed step height, 0 to 2 metres and below agent height.");
            field("Maximum slope", settings_.slope, "Walkable slope limit, 0 to 60 degrees.");
        }
        field("Cell size", settings_.cell_size,
              "Horizontal voxel size, .05 to 1 metre. Maximum grid is 512 by 512 cells.");
        if (settings_.plane == NavigationPlane::Spatial3D)
            field(
                "Cell height", settings_.cell_height,
                "Vertical voxel size, .025 to .5 metre; at least two cells of standing clearance.");
        else
            ImGui::TextWrapped("Use Navigation Region 2D rectangles for walkable areas; enable "
                               "Obstacle to cut out blocked areas. Visual Z is preserved. "
                               "Sprites, colliders and TileMaps are not extracted automatically.");
        if (ui::button("Build NavMesh", "Build in a bounded worker, validate and prove queries, "
                                        "then publish one immutable asset revision.")) {
            try {
                build(scene, project, settings_);
                message = status_;
            } catch (const std::exception& e) {
                message = e.what();
                ui::report_error("navigation_tools.hpp", message);
            }
        }
        ImGui::EndDisabled();
        if (job_.valid() &&
            ui::button("Cancel navigation build",
                       "Discard the unpublished candidate and retain the last good asset."))
            cancel_.request_stop();
        ImGui::TextWrapped(
            "Show the navigation overlay through Scene > View > Navigation overlay.");
        ui::help("Presentation controls live beside the viewport; this section builds the asset.");
        if (validation_at_ == std::chrono::steady_clock::time_point{} ||
            std::chrono::steady_clock::now() - validation_at_ >= std::chrono::milliseconds(250) ||
            validated_scene_revision_ != scene.revision() || validated_settings_ != settings_) {
            auto profile = context_.services().profile("editor", "NavigationStatus");
            validation_at_ = std::chrono::steady_clock::now();
            validated_scene_revision_ = scene.revision();
            validated_settings_ = settings_;
            validation_state_ = selected_ ? "NavMesh ready" : "No NavMesh built for this scene";
            if (selected_)
                try {
                    if (navigation_detail::selected_geometry_digest(
                            *catalog,
                            navigation_detail::capture_native(
                                context_, scene, source_entities_,
                                metadata_.at("settings").get<NavigationSettings>().plane),
                            metadata_) != metadata_.at("geometry_sha256").get<std::string>() ||
                        Json(settings_) != metadata_.at("settings"))
                        validation_state_ = "Navigation is stale — rebuild before Play";
                } catch (const std::exception&) {
                    validation_state_ = "Navigation is stale — check included geometry";
                }
        }
        // Advisory UI status is bounded to four catalog checks/second. Publication
        // and fixed runtime validation always recheck their exact captured inputs.
        ImGui::TextWrapped("%s", validation_state_.c_str());
        FORGE_UI_PROBE("navigation:status:" + validation_state_);
        ui::help("Changes to included geometry or these settings require an explicit rebuild. "
                 "3D includes shapes/static Mesh leaves; XY includes rectangular regions. "
                 "Dynamic rebaking and platformer links are unavailable. Navigation Avoidance "
                 "provides optional local steering for nonphysics agents.");
    }
    template <class View>
    void draw(const Json& doc, const View& camera, ImVec2 origin, ImVec2 size,
              const std::string& selected = {}) const {
        if (!doc.contains("asset_id"))
            return;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        const auto overlay_plane = metadata_.is_object()
                                       ? metadata_.at("settings").get<NavigationSettings>().plane
                                       : settings_.plane;
        auto projected = [&](Double3 p, NavigationPlane plane) -> std::optional<ImVec2> {
            p[plane == NavigationPlane::PlanarXY ? 2 : 1] += .02;
            auto v = project_point(camera, {float(p[0]), float(p[1]), float(p[2])}, size.x, size.y);
            if (!v)
                return {};
            return ImVec2{origin.x + (*v)[0], origin.y + (*v)[1]};
        };
        if (!selected.empty())
            for (const auto& row : doc.at("entities"))
                if (row.at("id") == selected && row.value("spatial_resolved", false) &&
                    row.at("components").contains("forge.navigation_region_2d")) {
                    try {
                        const auto& value = row.at("components").at("forge.navigation_region_2d");
                        const NavigationRegion2D region{
                            value.at("enabled").get<bool>(), value.at("obstacle").get<bool>(),
                            value.at("width").get<float>(), value.at("height").get<float>()};
                        const AffineTransform transform{
                            row.at("world_affine").get<std::array<double, 12>>()};
                        const auto quad = navigation_detail::region_quad(region, transform);
                        const auto color = !region.enabled   ? IM_COL32(140, 140, 140, 200)
                                           : region.obstacle ? IM_COL32(240, 145, 65, 240)
                                                             : IM_COL32(70, 220, 175, 240);
                        for (unsigned i = 0; i < 4; ++i) {
                            const auto a = projected(quad[i], NavigationPlane::PlanarXY);
                            const auto b = projected(quad[(i + 1) % 4], NavigationPlane::PlanarXY);
                            if (a && b)
                                draw->AddLine(*a, *b, color, 2);
                        }
                        FORGE_UI_PROBE("navigation:region-guide");
                    } catch (const std::exception&) {
                        // Invalid/unresolved sources have no substitute guide geometry.
                    }
                    break;
                }
        if (!show_ || doc.at("asset_id").get<AssetId>() != loaded_scene_) {
            draw->PopClipRect();
            return;
        }
        const auto overlay = [&](Double3 p) { return projected(p, overlay_plane); };
        for (std::size_t i = 0; i + 2 < triangles_.size(); i += 3) {
            auto a = overlay(triangles_[i]), b = overlay(triangles_[i + 1]),
                 c = overlay(triangles_[i + 2]);
            if (a && b && c) {
                draw->AddTriangleFilled(*a, *b, *c, IM_COL32(45, 180, 175, 45));
                draw->AddTriangle(*a, *b, *c, IM_COL32(80, 205, 190, 100));
            }
        }
        for (const auto& e : doc.at("entities"))
            if (e.contains("navigation_debug")) {
                const auto& path = e.at("navigation_debug").at("path");
                std::optional<ImVec2> previous;
                for (const auto& p : path) {
                    auto current = overlay(p.get<Double3>());
                    if (previous && current)
                        draw->AddLine(*previous, *current, IM_COL32(245, 210, 80, 255), 2);
                    previous = current;
                }
                if (!path.empty()) {
                    auto a = overlay(path.front().get<Double3>()),
                         b = overlay(path.back().get<Double3>());
                    if (a)
                        draw->AddCircleFilled(*a, 4, IM_COL32(90, 230, 140, 255));
                    if (b)
                        draw->AddCircleFilled(*b, 4, IM_COL32(245, 180, 70, 255));
                }
            }
        draw->PopClipRect();
    }

  private:
    WorldContext& context_;
    flecs::query<const PersistentEntityId> source_entities_;
    std::weak_ptr<const AssetCatalog> observed_catalog_;
    std::filesystem::path worker_, job_project_, loaded_project_;
    AssetId job_scene_, loaded_scene_, selected_;
    NavigationSettings settings_;
    Json metadata_;
    std::vector<Double3> triangles_;
    bool show_ = false;
    std::stop_source cancel_;
    std::future<NavigationCandidate> job_;
    std::chrono::steady_clock::time_point validation_at_{};
    std::uint64_t validated_scene_revision_ = 0;
    NavigationSettings validated_settings_;
    std::string validation_state_;
    std::string operation_, status_, error_;
};
} // namespace forge
