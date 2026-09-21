#pragma once
#include "asset_scene_preview.hpp"
#include "editor_state.hpp"
#include "icons.hpp"
namespace forge {
class ModelViewer {
  public:
    ModelViewer(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                const std::filesystem::path& project, std::shared_ptr<MeshResourceHost> host)
        : preview_(presentation, context, project, std::move(host)) {}
    const std::filesystem::path& project() const { return preview_.project(); }
    bool ready() const { return preview_.output() && !preview_.pending(); }
    const std::string& error() const { return preview_.error(); }
    std::string loading_state() const {
        return "last UI frame=" + std::to_string(last_frame_) + "; " + preview_.loading_state();
    }
    void draw(std::shared_ptr<const AssetCatalog> catalog, AssetId asset, bool draft = false) {
        if (last_frame_ + 1 != ImGui::GetFrameCount())
            orbiting_ = false;
        last_frame_ = ImGui::GetFrameCount();
        if (asset != selected_) {
            selected_ = asset;
            source_scene_.reset();
            orbiting_ = false;
        }
        if (!catalog || !catalog->records().contains(asset) ||
            !catalog->records().at(asset).metadata.contains("forge.import")) {
            ImGui::TextWrapped("Import this asset successfully to prepare its 3D preview.");
            ui::help("The preview reads admitted cooked content. Unpublished source files are not "
                     "parsed on the UI thread.");
            return;
        }
        preview_.select(std::move(catalog), asset, source_scene_);
        preview_.pump();
        ui::heading("3D preview",
                    "Read-only published asset inspection using the Scene/Game renderer. "
                    "It does not place entities or modify an authored scene.");
        if (draft) {
            ImGui::TextWrapped(
                "Preview shows the published asset. Import settings have not been applied.");
            ui::help("Import / Reimport validates and publishes the complete replacement before "
                     "the preview updates.");
        }
        if (const auto* data = preview_.data()) {
            ImGui::TextWrapped("%zu source nodes | %zu visible mesh placements", data->nodes,
                               data->scene.meshes.size());
            ui::help("Mesh inspection retains the source model's joint scope and shows this mesh's "
                     "placements in the selected source scene. This is the authored rest pose, "
                     "without autoplay.");
            if (data->source_scene_count > 1) {
                ui::property_label_row("Preview scene",
                                       "Choose one scene to inspect. Placement has its own "
                                       "explicit source-scene selection below.");
                const auto selected = source_scene_.value_or(data->source_scene);
                const auto label = "Scene " + std::to_string(selected + 1);
                if (ImGui::BeginCombo("##source-scene", label.c_str())) {
                    ImGuiListClipper clip;
                    clip.Begin(int(data->source_scene_count));
                    clip.IncludeItemByIndex(int(selected));
                    while (clip.Step())
                        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                            const auto name = "Scene " + std::to_string(i + 1);
                            if (ImGui::Selectable(name.c_str(), unsigned(i) == selected))
                                source_scene_ = unsigned(i);
                            ui::help("Inspect this source scene. No asset or scene is saved.");
                        }
                    ImGui::EndCombo();
                }
                ui::help("Preview defaults to the source's declared scene, or the first scene if "
                         "none is declared.");
            }
        }
        if (ui::button(
                "Frame view",
                "Fit the actual rendered mesh bounds, including static skin and morph defaults."))
            preview_.frame_view();
        const bool lighting = ImGui::TreeNode("Preview lighting");
        ui::help("Personal exposure, lighting and background. These values are not authored into "
                 "the asset or scene.");
        if (lighting) {
            ui::property_label_row("Exposure (stops)",
                                   "Display brightness after the shared HDR renderer.");
            ImGui::SliderFloat("##exposure", &preview_.exposure, -10, 10);
            ui::help("Exposure changes the preview, not the source.");
            ImGui::Checkbox("Use source lights", &preview_.source_lights);
            ui::help("Use punctual lights from the selected model scene. If it has none, use the "
                     "preview key light.");
            ui::property_label_row(
                "Key light (lux)",
                "Directional light used when source lighting is disabled or absent.");
            ImGui::SliderFloat("##key", &preview_.light_intensity, 0, 20);
            ui::help("Preview-only directional light intensity.");
            ImGui::ColorEdit3("Background", preview_.background.data(), ImGuiColorEditFlags_Float);
            ui::help("Preview camera background; does not edit model material colors.");
            ImGui::TreePop();
        }
        const float h = std::clamp(ImGui::GetContentRegionAvail().y - 60 * ui::interface_scale,
                                   96 * ui::interface_scale, 400 * ui::interface_scale);
        const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), h};
        auto* output = preview_.render(std::min(2048u, std::max(1u, unsigned(size.x))),
                                       std::min(2048u, std::max(1u, unsigned(size.y))));
        if (output) {
            ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(output)}, size);
            const bool hovered = ImGui::IsItemHovered();
            ui::help("Middle mouse orbits. Shift+middle mouse pans. Scroll zooms. These gestures "
                     "only affect the preview camera.");
            const auto& io = ImGui::GetIO();
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                ImGui::SetWindowFocus();
                orbiting_ = true;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || io.AppFocusLost)
                orbiting_ = false;
            if (orbiting_) {
                if (io.KeyShift)
                    preview_.camera.pan(io.MouseDelta.x, io.MouseDelta.y, size.y);
                else
                    preview_.camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)
                    preview_.changed_view();
            }
            if (hovered && !io.KeyCtrl && io.MouseWheel != 0) {
                preview_.camera.zoom(io.MouseWheel);
                preview_.changed_view();
            }
        }
        if (preview_.pending()) {
            ImGui::TextUnformatted("Preparing 3D preview...");
            ui::help("Metadata and render resources prepare asynchronously. A previous complete "
                     "image stays visible during replacement.");
        }
        if (!preview_.error().empty()) {
            ui::field_error(preview_.error());
            if (ui::button(
                    "Retry preview",
                    "Retry immutable asset preparation without changing or reimporting the asset."))
                preview_.retry();
        }
    }

  private:
    AssetScenePreview preview_;
    AssetId selected_;
    std::optional<unsigned> source_scene_;
    bool orbiting_ = false;
    int last_frame_ = -2;
};
class AssetViewerDocument {
  public:
    AssetViewerDocument(DiligentPresentation& presentation, Diligent::IDeviceContext* context)
        : presentation_(presentation), context_(context) {}
    std::function<void(AssetId)> open_source;
    void open(const std::filesystem::path& project, const AssetRecord& asset) {
        project_ = project;
        asset_ = asset.id;
        title_ = asset.type == "material" ? "Material preview"
                 : asset.type == "mesh"   ? "Mesh"
                                          : "Model";
        open_ = focus_ = true;
    }
    bool is_open() const { return open_; }
    bool ready() const { return viewer_ && viewer_->ready(); }
    std::string error() const { return viewer_ ? viewer_->error() : std::string{}; }
    void close() { open_ = false; }
    void draw(const std::filesystem::path& project, std::shared_ptr<MeshResourceHost> host) {
        if (project != project_) {
            open_ = false;
            return;
        }
        if (!open_ || !host)
            return;
        if (!viewer_ || viewer_->project() != project)
            viewer_ = std::make_unique<ModelViewer>(presentation_, context_, project, host);
        ui::draft_window_size({720 * ui::interface_scale, 660 * ui::interface_scale});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        if (ImGui::Begin((title_ + "###Asset viewer").c_str(), &open_)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("asset_viewer", title_);
            if (open_source &&
                ui::button(
                    "Open source import",
                    "Open the owning model import document. Generated members remain read-only."))
                open_source(asset_);
            viewer_->draw(host->catalog(), asset_);
        }
        ImGui::End();
    }
    void after_submission(const std::filesystem::path& project) {
        if (!open_ || project != project_) {
            open_ = false;
            viewer_.reset();
        }
    }

  private:
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    AssetId asset_;
    std::string title_;
    bool open_ = false, focus_ = false;
    std::unique_ptr<ModelViewer> viewer_;
};
} // namespace forge
