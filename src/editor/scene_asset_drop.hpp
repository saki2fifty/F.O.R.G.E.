#pragma once
#include "../model_placement.hpp"
#include "authoring.hpp"
#include "content.hpp"
#include <future>
namespace forge {
// Deferred viewport intent: no scene mutation during a draw using cached scene
// references. Model preparation owns copied IDs/revisions, never a Flecs world.
class SceneAssetDrop {
  public:
    ~SceneAssetDrop() { stop_.request_stop(); }
    bool busy() const { return pending_.has_value() || job_.valid(); }
    void cancel() { stop_.request_stop(); }
    static bool supported(std::string_view type) {
        return type == "model" || type == "mesh" || type == "prefab" || type == "scene";
    }
    void queue(const AssetRecord& asset, const Scene& scene, const SceneDocument& document,
               LocalTranslation position) {
        if (busy() || !supported(asset.type))
            throw std::runtime_error(
                "Finish the active placement or drop a Model, Mesh, Prefab or Scene.");
        document.check_ownership();
        stop_ = std::stop_source{};
        pending_ = Pending{
            asset,   document.project(), document.generation(), scene.asset_id(), scene.revision(),
            position};
    }
    void poll(Scene& scene, EditorFiles& files, ui::EditorSelection& selection, bool locked,
              std::string& message) {
        if (!busy())
            return;
        try {
            if (!pending_ || stop_.stop_requested() || locked ||
                files.document.project() != pending_->project ||
                files.document.generation() != pending_->document ||
                scene.asset_id() != pending_->scene || scene.revision() != pending_->revision) {
                stop_.request_stop();
                // Drain a cancelled worker before accepting another placement.
                if (job_.valid() &&
                    job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return;
                if (job_.valid()) {
                    try {
                        (void)job_.get();
                    } catch (const std::exception&) {
                    }
                }
                throw std::runtime_error(
                    "Asset placement cancelled: its scene changed or editing became unavailable.");
            }
            files.document.check_ownership();
            const auto request = *pending_;
            if (request.asset.type == "model") {
                if (!job_.valid()) {
                    const auto stop = stop_.get_token();
                    job_ = std::async(std::launch::async, [request, stop] {
                        const auto catalog = AssetCatalog::open_project(request.project);
                        auto source = asset_detail::load_model_selection(request.project, catalog,
                                                                         request.asset.id, stop);
                        asset_detail::ModelPlacementOptions options;
                        options.name = asset_display(request.asset);
                        options.transform.translation = request.position;
                        return asset_detail::prepare_model_placement(source, request.scene,
                                                                     request.revision, options);
                    });
                    message = "Preparing model placement...";
                    return;
                }
                if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return;
                const auto candidate = job_.get();
                const auto catalog = AssetCatalog::open_project(request.project);
                selection.select_entity(
                    asset_detail::instantiate_model(scene, catalog, candidate).str());
            } else if (request.asset.type == "mesh") {
                const auto catalog = AssetCatalog::open_project(request.project);
                selection.select_entity(
                    asset_detail::instantiate_mesh(scene, catalog, {request.asset.id},
                                                   request.position, asset_display(request.asset))
                        .str());
            } else if (request.asset.type == "prefab") {
                selection.select_entity(
                    asset_detail::instantiate_prefab_at(scene, request.asset.id, request.position)
                        .str());
            } else {
                pending_.reset(); // The Open workflow must not see our own busy guard.
                files.request(
                    {EditorFiles::Command::OpenScene, request.project / request.asset.source, {}});
                message = "Opening dropped scene...";
                return;
            }
            pending_.reset();
            message = "Asset placed. Undo removes this placement.";
        } catch (const std::exception& e) {
            pending_.reset();
            message = e.what();
            ui::report_error("asset_placement", message);
        }
    }
    // Called immediately after the viewport's interactive item. Pinned ImGui
    // BeginDragDropTarget uses LastItemData; submitting other controls first loses it.
    void target(Scene& scene, EditorFiles& files, ContentBrowser& content,
                const EditorCamera& camera, ImVec2 origin, ImVec2 size, bool allowed) {
        if (!allowed || busy() || !ImGui::BeginDragDropTarget())
            return;
        if (const auto* payload = ImGui::AcceptDragDropPayload(
                "FORGE_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            try {
                if (payload->DataSize != 37)
                    throw std::runtime_error("Invalid asset drag identity");
                const auto id =
                    AssetId::parse(std::string(static_cast<const char*>(payload->Data), 36));
                auto* asset = content.resolve_record(files, id);
                if (!asset || !supported(asset->type)) {
                    ImGui::SetTooltip("Drop a Model, Mesh, Prefab or Scene. Assign materials and "
                                      "textures through compatible Inspector fields.");
                } else if (payload->IsDelivery()) {
                    const auto mouse = ImGui::GetIO().MousePos;
                    const auto ray =
                        view_ray(camera, mouse.x - origin.x, mouse.y - origin.y, size.x, size.y);
                    // A stable camera-facing plane through the orbit target works
                    // for top/front/side views without a horizon-plane singularity.
                    const auto depth = camera.distance / dot(ray, camera.forward());
                    const auto eye = camera.eye();
                    queue(*asset, scene, files.document,
                          {eye[0] + ray[0] * depth, eye[1] + ray[1] * depth,
                           eye[2] + ray[2] * depth});
                } else {
                    ImGui::SetTooltip(asset->type == "scene"
                                          ? "Open scene (asks about unsaved changes)"
                                          : "Place here — one scene Undo step");
                }
            } catch (const std::exception& e) {
                ui::report_error("asset_placement", e.what());
            }
        }
        ImGui::EndDragDropTarget();
    }
    void controls() {
        if (!busy())
            return;
        if (ui::button("Cancel placement",
                       "Cancel pending asset loading without changing the scene."))
            cancel();
    }

  private:
    struct Pending {
        AssetRecord asset;
        std::filesystem::path project;
        std::uint64_t document;
        AssetId scene;
        std::uint64_t revision;
        LocalTranslation position;
    };
    std::optional<Pending> pending_;
    std::stop_source stop_;
    std::future<asset_detail::ModelPlacementCandidate> job_;
};
} // namespace forge
