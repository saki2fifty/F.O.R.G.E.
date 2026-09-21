#pragma once
#include "../texture_asset_preview.hpp"
#include "editor_state.hpp"
#include "icons.hpp"
namespace forge {
class TextureViewer {
  public:
    TextureViewer(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                  const std::filesystem::path& project)
        : preview_(presentation, context, project) {}
    const std::filesystem::path& project() const { return preview_.project(); }
    bool ready() const { return preview_.data() && !preview_.pending(); }
    const std::string& error() const { return preview_.error(); }
    void draw(std::shared_ptr<const AssetCatalog> catalog, AssetRef<TextureAsset> asset,
              bool draft) {
        ui::heading("Texture preview", "View the last published texture. Settings below affect "
                                       "only this view, not the source, import or scene.");
        if (asset != selected_) {
            selected_ = asset;
            semantic_ = 0;
            zoom_ = 1;
            fit_ = true;
        }
        ui::property_label_row("Variant");
        ImGui::Combo("##Variant", &semantic_, "Automatic color\0Color\0Data\0Normal\0HDR color\0");
        ui::help("Choose an imported semantic variant. Missing variants report an error; the "
                 "viewer never substitutes a differently interpreted texture.");
        std::optional<TextureSemantic> semantic;
        if (semantic_)
            semantic = TextureSemantic(semantic_ - 1);
        if (!catalog || !catalog->records().contains(asset.id)) {
            ui::field_error("The selected texture is not available in this project catalog.");
            return;
        }
        if (!catalog->records().at(asset.id).metadata.contains("forge.import")) {
            ImGui::TextWrapped("Import this texture to prepare its preview.");
            ui::help("Preview consumes a published cooked asset; it never decodes source files on "
                     "the UI thread.");
            return;
        }
        preview_.select(std::move(catalog), asset, semantic);
        preview_.pump();
        if (draft) {
            ImGui::TextWrapped(
                "Preview shows the published asset; import settings are not yet applied.");
            ui::help(
                "Import / Reimport publishes a validated candidate before this preview changes.");
        }
        if (const auto* data = preview_.data()) {
            auto& settings = preview_.settings;
            ImGui::TextWrapped("%u x %u x %u | %u layers | %u mips | %.2f MiB", data->width,
                               data->height, data->depth, data->layers, data->mips,
                               data->byte_size() / (1024.0 * 1024.0));
            ui::help(
                "Dimensions and payload size of the selected cooked variant. Array layers "
                "count cubes, not individual cube faces. Payload size is not driver VRAM usage.");
            const bool controls = ImGui::TreeNode("View controls");
            ui::help("Mip, layer, face, slice, channels, exposure, filtering and transparency. "
                     "These are personal viewing controls, not import settings.");
            if (controls) {
                int mip = int(settings.mip);
                ui::property_label_row("Mip");
                if (ImGui::SliderInt("##Mip", &mip, 0, int(data->mips) - 1)) {
                    settings.mip = unsigned(mip);
                    settings.depth =
                        std::min(settings.depth, std::max(1u, data->depth >> settings.mip) - 1);
                }
                ui::help("Select an authored/cooked mip level; the preview does not generate "
                         "missing mips.");
                if (data->layers > 1) {
                    int layer = int(settings.layer);
                    ui::property_label_row("Array layer");
                    if (ImGui::SliderInt("##Array layer", &layer, 0, int(data->layers) - 1))
                        settings.layer = unsigned(layer);
                    ui::help("Inspect one array layer, or one cube in a cube array.");
                }
                if (data->dimension == TextureDimension::Cube ||
                    data->dimension == TextureDimension::CubeArray) {
                    int face = int(settings.face);
                    ui::property_label_row("Cube face");
                    if (ImGui::Combo("##Cube face", &face, "+X\0-X\0+Y\0-Y\0+Z\0-Z\0"))
                        settings.face = unsigned(face);
                    ui::help("Show the selected cube face in its stored orientation.");
                }
                if (data->dimension == TextureDimension::D3) {
                    int depth = int(settings.depth);
                    ui::property_label_row("Depth slice");
                    if (ImGui::SliderInt("##Depth slice", &depth, 0,
                                         int(std::max(1u, data->depth >> settings.mip)) - 1))
                        settings.depth = unsigned(depth);
                    ui::help("Sample the center of one depth slice at the selected mip.");
                }
                int channel = int(settings.channel), display = int(settings.display);
                ui::property_label_row("Channels");
                if (ImGui::Combo("##Channels", &channel, "RGBA\0RGB\0Red\0Green\0Blue\0Alpha\0"))
                    settings.channel = TexturePreviewChannel(channel);
                ui::help(
                    "Isolated channels display sampled linear values as grayscale. Color views "
                    "apply exactly one output sRGB transfer. Alpha is never gamma-decoded.");
                ui::property_label_row("Display");
                if (ImGui::Combo("##Display", &display, "Data\0Color\0HDR (PBR Neutral)\0"))
                    settings.display = TexturePreviewDisplay(display);
                ui::help(
                    "Data shows sampled values directly. Color encodes linear RGB for display. "
                    "HDR applies the renderer's pinned PBR Neutral tone mapper first.");
                ui::property_label_row("Exposure (stops)");
                ImGui::SliderFloat("##Exposure (stops)", &settings.exposure, -20, 20);
                ui::help("Multiply sampled values by two per stop; source data remains unchanged.");
                ImGui::Checkbox("Checkerboard", &settings.checker);
                ui::help("Composite RGBA over a checkerboard using the imported alpha convention.");
                ImGui::Checkbox("Nearest pixels", &settings.nearest);
                ui::help("Point filtering reveals texels; disabled uses linear filtering.");
                ImGui::Checkbox("Signed values to 0..1", &settings.signed_values);
                ui::help("Display signed RGB values using value / 2 + 0.5. Defaults on for signed "
                         "formats. Alpha keeps its original unsigned interpretation.");
                ImGui::TreePop();
            }
            if (ui::button("Fit image", "Fit the selected mip inside the preview area."))
                fit_ = true;
            ui::next_text_button("1:1 pixels");
            if (ui::button("1:1 pixels",
                           "One cooked texel per screen pixel; scroll to see the rest.")) {
                fit_ = false;
                zoom_ = 1;
            }
            ui::property_label_row("Image zoom");
            if (ImGui::SliderFloat("##Image zoom", &zoom_, .125f, 8.f, "%.3fx"))
                fit_ = false;
            ui::help("Scale the image only. Ctrl+Plus/Minus still changes the complete editor UI.");
            const float w = float(std::max(1u, data->width >> settings.mip));
            const float h = float(std::max(1u, data->height >> settings.mip));
            const float image_height =
                std::clamp(ImGui::GetContentRegionAvail().y - 45 * ui::interface_scale,
                           96 * ui::interface_scale, 350 * ui::interface_scale);
            if (ImGui::BeginChild("texture-image", {0, image_height}, ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
                const auto available = ImGui::GetContentRegionAvail();
                const float factor = fit_ ? std::min(available.x / w, available.y / h) : zoom_;
                const ImVec2 size{std::max(1.f, w * factor), std::max(1.f, h * factor)};
                const auto origin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(size);
                ui::help("Published texture preview. Scrollbars pan a zoomed image. Only the "
                         "visible region is rendered, bounded to 2048 pixels per axis.");
                auto* draw = ImGui::GetWindowDrawList();
                const auto clip_min = draw->GetClipRectMin(), clip_max = draw->GetClipRectMax();
                const ImVec2 minimum{std::max(origin.x, clip_min.x),
                                     std::max(origin.y, clip_min.y)};
                const ImVec2 maximum{std::min(origin.x + size.x, clip_max.x),
                                     std::min(origin.y + size.y, clip_max.y)};
                if (maximum.x > minimum.x && maximum.y > minimum.y) {
                    settings.region = {
                        (minimum.x - origin.x) / size.x, (minimum.y - origin.y) / size.y,
                        (maximum.x - origin.x) / size.x, (maximum.y - origin.y) / size.y};
                    auto* output = preview_.render(
                        std::min(2048u, std::max(1u, unsigned(maximum.x - minimum.x))),
                        std::min(2048u, std::max(1u, unsigned(maximum.y - minimum.y))));
                    if (output)
                        draw->AddImage(ImTextureRef{reinterpret_cast<ImTextureID>(output)}, minimum,
                                       maximum);
                }
            }
            ImGui::EndChild();
        }
        if (preview_.pending()) {
            ImGui::TextUnformatted("Preparing texture preview...");
            ui::help("Verified cooked data loads on a resource worker; the previous good revision "
                     "remains visible during replacement.");
        }
        if (!preview_.error().empty())
            ui::field_error(preview_.error());
    }

  private:
    TextureAssetPreview preview_;
    AssetRef<TextureAsset> selected_;
    int semantic_ = 0;
    bool fit_ = true;
    float zoom_ = 1;
};
// Generated texture members are read-only views of their owning model's cook;
// they must never be sent to the standalone image importer as if the glTF were an image.
class TextureViewerDocument {
  public:
    TextureViewerDocument(DiligentPresentation& presentation, Diligent::IDeviceContext* context)
        : presentation_(presentation), context_(context) {}
    void open(const std::filesystem::path& project, AssetId asset) {
        project_ = project;
        asset_ = {asset};
        open_ = focus_ = true;
    }
    bool is_open() const { return open_; }
    void close() { open_ = false; }
    void draw(const std::filesystem::path& project, std::shared_ptr<const AssetCatalog> catalog) {
        if (project != project_) {
            open_ = false;
            return;
        }
        if (!open_)
            return;
        if (!viewer_ || viewer_->project() != project)
            viewer_ = std::make_unique<TextureViewer>(presentation_, context_, project);
        ui::draft_window_size({680 * ui::interface_scale, 620 * ui::interface_scale});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        if (ImGui::Begin("Texture###Texture viewer", &open_)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("texture_viewer", "Texture");
            viewer_->draw(std::move(catalog), asset_, false);
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
    AssetRef<TextureAsset> asset_;
    bool open_ = false, focus_ = false;
    std::unique_ptr<TextureViewer> viewer_;
};
} // namespace forge
