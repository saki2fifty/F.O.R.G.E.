#pragma once
#include "authoring.hpp"
#include "document.hpp"
#include "navigation_destination_view.hpp"
#include "navigation_link_value.hpp"
#include "navigation_sources.hpp"
#include "property_drawer.hpp"
#include "spatial_component_value.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/navigation_build.hpp>
#include <forge/render_scene.hpp>
#include <future>
namespace forge {
class NavigationTools {
    friend struct NavigationAutomation;

  public:
    bool pending() const { return job_.valid(); }
    bool overlay_visible() const { return show_; }
    void show_overlay(bool visible) { show_ = visible; }
    std::optional<NavigationDestinationView>
    requested_destination(const Json& source, const std::string& selected) const {
        try {
            if (!selected_ || !metadata_.is_object() ||
                source.at("asset_id").get<AssetId>() != loaded_scene_)
                return {};
            return navigation_destination_view(
                source, selected, selected_,
                metadata_.at("settings").get<NavigationSettings>().plane, loaded_project_);
        } catch (const std::exception&) {
            return {};
        }
    }
    void overlay_control() {
        ImGui::Checkbox("Navigation overlay", &show_);
        FORGE_UI_PROBE("navigation:overlay");
        ui::help("Show navigation triangles, runtime paths and the selected agent's requested "
                 "destination. The cyan straight guide shows input coordinates; the amber "
                 "runtime path shows the calculated route. This does not rebuild navigation.");
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
    void select_profile(Scene& scene, SceneDocument& project, const AssetCatalog& catalog,
                        const std::string& profile) {
        project.check_ownership();
        if (pending())
            throw std::runtime_error("Wait for the current navigation build");
        if (catalog.project_root() != project.project())
            throw std::runtime_error("Navigation profiles belong to another project");
        auto settings = loaded_project_ == project.project() && loaded_scene_ == scene.asset_id()
                            ? settings_
                            : NavigationSettings{};
        settings.profile = profile;
        auto profiles = navigation_profiles(catalog, scene.asset_id());
        AssetId selected;
        Json metadata;
        std::vector<Double3> triangles;
        if (const auto found = profiles.find(profile); found != profiles.end()) {
            selected = found->second;
            metadata = catalog.records().at(selected).metadata;
            settings = metadata.at("settings").get<NavigationSettings>();
            triangles = navigation_triangles(project.project(), {selected});
        }
        validate_navigation_settings(settings);
        // Load/validate before replacing the current view or unfinished settings.
        loaded_project_ = project.project();
        loaded_scene_ = scene.asset_id();
        settings_ = std::move(settings);
        profiles_ = std::move(profiles);
        selected_ = selected;
        metadata_ = std::move(metadata);
        triangles_ = std::move(triangles);
        validation_at_ = {};
        error_.clear();
        status_ = selected_ ? "Navigation profile selected" : "New navigation profile draft";
    }
    void build(Scene& scene, SceneDocument& project, NavigationSettings settings) {
        project.check_ownership();
        if (pending())
            throw std::runtime_error("Wait for the current navigation build");
        validate_navigation_settings(settings);
        const auto catalog = AssetCatalog::open_project(project.project());
        auto profiles = navigation_profiles(catalog, scene.asset_id());
        const auto operation = AssetId::generate().str();
        auto doc = scene.effective_document();
        if (loaded_project_ != project.project() || loaded_scene_ != scene.asset_id() ||
            settings_.profile != settings.profile) {
            selected_ = {};
            metadata_ = nullptr;
            triangles_.clear();
            if (const auto found = profiles.find(settings.profile); found != profiles.end()) {
                selected_ = found->second;
                metadata_ = catalog.records().at(selected_).metadata;
                // Rebuilding can repair an unavailable artifact; its failed view
                // load must not prevent the ordinary worker/publication path.
                try {
                    triangles_ = navigation_triangles(project.project(), {selected_});
                } catch (const std::exception&) {
                    triangles_.clear();
                }
            }
        }
        profiles_ = std::move(profiles);
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
            profiles_.clear();
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
        const bool expanded = ImGui::CollapsingHeader("Navigation");
        FORGE_UI_PROBE("navigation:panel");
        if (!expanded) {
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
                profiles_ = navigation_profiles(*catalog, loaded_scene_);
                if (const auto found = profiles_.find(settings_.profile);
                    found != profiles_.end()) {
                    selected_ = found->second;
                    metadata_ = catalog->records().at(selected_).metadata;
                    if (scene_changed || settings_followed_saved)
                        settings_ = metadata_.at("settings").get<NavigationSettings>();
                    if (scene_changed || selected_ != old_selected || old_metadata != metadata_)
                        triangles_ = navigation_triangles(project.project(), {selected_});
                }
                if (!selected_)
                    triangles_.clear();
            }
        } catch (const std::exception& e) {
            message = e.what();
            ui::report_error("navigation_tools.hpp", message);
        }
        ImGui::BeginDisabled(locked || job_.valid());
        bool new_profile = false;
        const bool choose_profile = ImGui::BeginCombo("Build profile", settings_.profile.c_str());
        FORGE_UI_PROBE("navigation:profile");
        FORGE_UI_PROBE("navigation:profile-selected:" + settings_.profile);
        if (choose_profile) {
            auto names = profiles_;
            names.try_emplace("default", AssetId{});
            names.try_emplace(settings_.profile, AssetId{});
            for (const auto& [name, id] : names) {
                (void)id;
                if (ImGui::Selectable(name.c_str(), settings_.profile == name))
                    try {
                        select_profile(scene, project, *catalog, name);
                    } catch (const std::exception& e) {
                        message = e.what();
                        ui::report_error("navigation_tools.hpp", message);
                    }
                FORGE_UI_PROBE("navigation:profile:" + name);
            }
            if (ImGui::Selectable("New profile..."))
                new_profile = true;
            FORGE_UI_PROBE("navigation:profile-new");
            ImGui::EndCombo();
        }
        if (new_profile)
            ImGui::OpenPopup("New navigation profile");
        if (ImGui::BeginPopup("New navigation profile")) {
            ImGui::InputText("New profile name", profile_name_.data(), profile_name_.size());
            FORGE_UI_PROBE("navigation:new-profile");
            if (ui::button("Use new profile",
                           "Start a named settings draft. Build NavMesh "
                           "publishes it; existing profiles remain available.")) {
                try {
                    if (profiles_.contains(profile_name_.data()))
                        throw std::runtime_error("Profile already exists; select it above");
                    select_profile(scene, project, *catalog, profile_name_.data());
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    message = e.what();
                    ui::report_error("navigation_tools.hpp", message);
                }
            }
            ImGui::TextWrapped("Names use 1-64 ASCII letters, digits, '_' or '-', beginning with "
                               "a letter. Names are case-sensitive.");
            ImGui::EndPopup();
        }
        ui::help("Choose saved settings and navigation by name, or New profile... to start an "
                 "unpublished draft. The default profile retains existing asset identities.");
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
        ui::help("Each build profile owns a separate navigation asset. Changing its plane requires "
                 "an explicit rebuild and keeps that profile's asset identity.");
        ImGui::Checkbox("Dynamic obstacles", &settings_.dynamic_obstacles);
        FORGE_UI_PROBE("navigation:setting:Dynamic obstacles");
        ui::help("Bake bounded tiles for moving Navigation Obstacle cylinders (circles in XY). "
                 "Eight enabled obstacles; each may touch at most eight layers. "
                 "Changing this option requires a rebuild, keeping the asset identity.");
        ImGui::Checkbox("Collect static colliders", &settings_.collect_static_colliders);
        FORGE_UI_PROBE("navigation:setting:Collect static colliders");
        ui::help("Include enabled Static solid Physics Bodies on selected layers. In 3D use "
                 "their physical shapes; in XY add their collision footprints as obstacles to "
                 "walkable Navigation Regions. Explicit navigation markers take precedence, "
                 "including disabled markers as exclusions. Rebuild after changing this option.");
        ImGui::BeginDisabled(!settings_.collect_static_colliders);
        ui::property_label_row("Collision layers", "Match Physics Body layer slots.");
        physics_layer_picker(project.project(), "##navigation_collision_layers",
                             settings_.collision_layers, true);
        ImGui::EndDisabled();
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
            ImGui::TextWrapped(
                "Use Navigation Region 2D rectangles, Points or explicit Collider 2D "
                "sources; enable Obstacle to cut out blocked areas. Visual Z is "
                "preserved. Sprite artwork is independent.");
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
            validation_state_ = selected_ ? "NavMesh ready" : "No NavMesh built for this profile";
            if (selected_)
                try {
                    if (navigation_detail::selected_geometry_digest(
                            *catalog,
                            navigation_detail::capture_native(
                                context_, scene, source_entities_,
                                metadata_.at("settings").get<NavigationSettings>()),
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
        ui::help(
            "Changes to included geometry or these settings require an explicit rebuild. "
            "3D includes shapes/static Mesh leaves; XY includes selected region sources. "
            "Navigation Link connects static walkable parts in the matching plane/profile. "
            "It uses straight Agent travel; physics jumping and moving platforms are unavailable. "
            "Dynamic rebaking is unavailable. Navigation Avoidance "
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
            p = navigation_guide_point(p, plane);
            auto v = project_point(camera, {float(p[0]), float(p[1]), float(p[2])}, size.x, size.y);
            if (!v)
                return {};
            return ImVec2{origin.x + (*v)[0], origin.y + (*v)[1]};
        };
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
        std::size_t drawn_links = 0;
        for (const auto& entity : doc.at("entities")) {
            const auto& components = entity.at("components");
            if (drawn_links >= navigation_detail::navigation_link_limit ||
                !components.contains("forge.navigation_link") ||
                !entity.value("spatial_resolved", false) || !entity.contains("world_affine"))
                continue;
            const auto& data = components.at("forge.navigation_link");
            if (!data.at("enabled").get<bool>() || data.at("plane") != unsigned(overlay_plane) ||
                data.at("profile") != settings_.profile)
                continue;
            try {
                const auto link = navigation_detail::derive_link(
                    entity.at("id").get<EntityId>(), navigation_detail::link_from_value(data),
                    AffineTransform{entity.at("world_affine").get<std::array<double, 12>>()});
                const auto a = overlay({link.start[0], link.start[1], link.start[2]});
                const auto b = overlay({link.end[0], link.end[1], link.end[2]});
                if (!a || !b)
                    continue;
                ++drawn_links;
                const auto color = IM_COL32(210, 150, 255, 255);
                const auto scale = ui::interface_scale;
                draw->AddLine(*a, *b, color, 2 * scale);
                draw->AddCircle(*a, 5 * scale, color, 12, 1.5f * scale);
                draw->AddCircle(*b, 5 * scale, color, 12, 1.5f * scale);
                const auto arrow = [&](ImVec2 from, ImVec2 to) {
                    const float length = std::hypot(to.x - from.x, to.y - from.y);
                    if (length < 12 * scale)
                        return;
                    const float x = (to.x - from.x) / length, y = (to.y - from.y) / length;
                    const ImVec2 tip{from.x + (to.x - from.x) * .7f,
                                     from.y + (to.y - from.y) * .7f};
                    draw->AddTriangleFilled(
                        tip, {tip.x - (x * 8 + y * 4) * scale, tip.y - (y * 8 - x * 4) * scale},
                        {tip.x - (x * 8 - y * 4) * scale, tip.y - (y * 8 + x * 4) * scale}, color);
                };
                arrow(*a, *b);
                if (link.bidirectional)
                    arrow(*b, *a);
                FORGE_UI_RECT_PROBE(
                    ("navigation:link:" + link.entity.str()).c_str(),
                    (ImVec2{std::min(a->x, b->x) - 5 * scale, std::min(a->y, b->y) - 5 * scale}),
                    (ImVec2{std::max(a->x, b->x) + 5 * scale, std::max(a->y, b->y) + 5 * scale}),
                    true);
            } catch (const std::exception&) {
                // Invalid source intent is reported by the shared bake/status validator.
            }
        }
        // This same copied view supplies the Scene target tool; no parallel
        // asset/plane or destination-selection authority exists.
        if (const auto target = requested_destination(doc, selected)) {
            const auto from = overlay(target->origin), to = overlay(target->point);
            if (to) {
                const auto color = target->agent.enabled ? IM_COL32(105, 225, 245, 255)
                                                         : IM_COL32(155, 160, 170, 230);
                const float radius = 6 * ui::interface_scale;
                if (from)
                    draw->AddLine(*from, *to, color, ui::interface_scale);
                draw->AddQuad({to->x, to->y - radius}, {to->x + radius, to->y},
                              {to->x, to->y + radius}, {to->x - radius, to->y}, color,
                              1.5f * ui::interface_scale);
                FORGE_UI_RECT_PROBE(
                    "navigation:requested-destination", (ImVec2{to->x - radius, to->y - radius}),
                    (ImVec2{to->x + radius, to->y + radius}), target->agent.enabled);
                const char* label = target->agent.enabled ? "Requested destination"
                                                          : "Requested destination (disabled)";
                const float margin = 4 * ui::interface_scale;
                const float wrap_width = std::max(1.f, size.x - 2 * margin);
                const auto text_size = ImGui::CalcTextSize(label, nullptr, false, wrap_width);
                if (to->x >= origin.x && to->x <= origin.x + size.x && to->y >= origin.y &&
                    to->y <= origin.y + size.y) {
                    const ImVec2 at{std::clamp(to->x + radius + margin, origin.x + margin,
                                               std::max(origin.x + margin,
                                                        origin.x + size.x - text_size.x - margin)),
                                    std::clamp(to->y + radius + margin, origin.y + margin,
                                               std::max(origin.y + margin,
                                                        origin.y + size.y - text_size.y - margin))};
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, color, label, nullptr,
                                  wrap_width);
                    FORGE_UI_RECT_PROBE("navigation:requested-destination-label", at,
                                        (ImVec2{at.x + text_size.x, at.y + text_size.y}), true);
                }
            }
        }
        for (const auto& e : doc.at("entities"))
            if (e.contains("navigation_debug")) {
                const auto& components = e.at("components");
                if (!selected_ || !components.contains("forge.navigation_agent") ||
                    components.at("forge.navigation_agent").at("navmesh") != Json(selected_))
                    continue;
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
    std::map<std::string, AssetId> profiles_;
    std::array<char, 65> profile_name_{};
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
