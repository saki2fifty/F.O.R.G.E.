#pragma once
#include "authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/navigation_build.hpp>
#include <future>
namespace forge {
class NavigationTools {
  public:
    void overlay_control() {
        ImGui::Checkbox("Navigation overlay", &show_);
        ui::help("Show or hide navigation triangles and paths in the viewport; this does not "
                 "rebuild navigation.");
    }
    explicit NavigationTools(std::filesystem::path worker) : worker_(std::move(worker)) {}
    ~NavigationTools() { cancel_.request_stop(); }
    void poll(Scene& scene, SceneDocument& project, bool playing, std::string& message) {
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
            triangles_ = navigation_triangles(project.project(), {record.id});
            show_ = true;
            message = "Navigation built. Add a Navigation Agent, choose this navmesh and enter its "
                      "destination.";
        } catch (const std::exception& e) {
            message = e.what();
            ui::report_error("navigation_tools.hpp", message);
        }
    }
    void content(Scene& scene, SceneDocument& project, bool locked, std::string& message) {
        if (!ImGui::CollapsingHeader("Navigation")) {
            ui::help("Build and inspect a static navmesh from marked scene primitives.");
            return;
        }
        ui::help("Asset publication is separate from scene Undo. Failed or stale builds retain the "
                 "previous navmesh.");
        try {
            if (loaded_project_ != project.project() || loaded_scene_ != scene.asset_id()) {
                loaded_project_ = project.project();
                loaded_scene_ = scene.asset_id();
                settings_ = {};
                metadata_ = nullptr;
                selected_ = {};
                triangles_.clear();
                auto catalog = AssetCatalog::open_project(project.project());
                for (const auto& [id, record] : catalog.records())
                    if (record.type == NavMeshAsset::type &&
                        record.metadata.value("source_scene", std::string{}) ==
                            loaded_scene_.str()) {
                        selected_ = id;
                        metadata_ = record.metadata;
                        settings_ = metadata_.at("settings").get<NavigationSettings>();
                        triangles_ = navigation_triangles(project.project(), {id});
                        break;
                    }
            }
        } catch (const std::exception& e) {
            message = e.what();
            ui::report_error("navigation_tools.hpp", message);
        }
        ImGui::BeginDisabled(locked || job_.valid());
        auto field = [](const char* label, float& value, const char* help) {
            ImGui::InputFloat(label, &value, 0, 0, "%.3f");
            ui::help(help);
        };
        field("Agent radius", settings_.radius,
              "Body clearance in metres, .05 to 5. This navmesh has one agent profile.");
        field("Agent height", settings_.height, "Standing clearance in metres, .2 to 10.");
        field("Maximum climb", settings_.climb,
              "Allowed step height, 0 to 2 metres and below agent height.");
        field("Maximum slope", settings_.slope, "Walkable slope limit, 0 to 60 degrees.");
        field("Cell size", settings_.cell_size,
              "Horizontal voxel size, .05 to 1 metre. Maximum grid is 512 by 512 cells.");
        field("Cell height", settings_.cell_height,
              "Vertical voxel size, .025 to .5 metre; at least two cells of standing clearance.");
        if (ui::button("Build NavMesh", "Build in a bounded worker, validate and prove queries, "
                                        "then publish one immutable asset revision.")) {
            try {
                project.check_ownership();
                job_project_ = project.project();
                job_scene_ = scene.asset_id();
                cancel_ = std::stop_source{};
                auto root = job_project_, tool = worker_;
                auto doc = scene.effective_document();
                auto settings = settings_;
                auto token = cancel_.get_token();
                job_ = std::async(std::launch::async,
                                  [root, tool, doc = std::move(doc), settings, token] {
                                      return prepare_navigation(root, doc, settings, tool, token);
                                  });
                message = "Building navigation...";
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
        std::string state = selected_ ? "NavMesh ready" : "No NavMesh built for this scene";
        if (selected_)
            try {
                if (navigation_geometry_digest(scene.effective_document()) !=
                        metadata_.at("geometry_sha256").get<std::string>() ||
                    Json(settings_) != metadata_.at("settings"))
                    state = "Navigation is stale — rebuild before Play";
            } catch (const std::exception&) {
                state = "Navigation is stale — check included geometry";
            }
        ImGui::TextWrapped("%s", state.c_str());
        ui::help("Changes to included geometry or these settings require an explicit rebuild. "
                 "Dynamic obstacles and crowd avoidance are not supported.");
    }
    void draw(const Json& doc, const EditorCamera& camera, ImVec2 origin, ImVec2 size) const {
        if (!show_)
            return;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        auto projected = [&](Double3 p) -> std::optional<ImVec2> {
            auto v = project_point(camera, {float(p[0]), float(p[1] + .02), float(p[2])}, size.x,
                                   size.y);
            if (!v)
                return {};
            return ImVec2{origin.x + (*v)[0], origin.y + (*v)[1]};
        };
        for (std::size_t i = 0; i + 2 < triangles_.size(); i += 3) {
            auto a = projected(triangles_[i]), b = projected(triangles_[i + 1]),
                 c = projected(triangles_[i + 2]);
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
                    auto current = projected(p.get<Double3>());
                    if (previous && current)
                        draw->AddLine(*previous, *current, IM_COL32(245, 210, 80, 255), 2);
                    previous = current;
                }
                if (!path.empty()) {
                    auto a = projected(path.front().get<Double3>()),
                         b = projected(path.back().get<Double3>());
                    if (a)
                        draw->AddCircleFilled(*a, 4, IM_COL32(90, 230, 140, 255));
                    if (b)
                        draw->AddCircleFilled(*b, 4, IM_COL32(245, 180, 70, 255));
                }
            }
        draw->PopClipRect();
    }

  private:
    std::filesystem::path worker_, job_project_, loaded_project_;
    AssetId job_scene_, loaded_scene_, selected_;
    NavigationSettings settings_;
    Json metadata_;
    std::vector<Double3> triangles_;
    bool show_ = false;
    std::stop_source cancel_;
    std::future<NavigationCandidate> job_;
};
} // namespace forge
