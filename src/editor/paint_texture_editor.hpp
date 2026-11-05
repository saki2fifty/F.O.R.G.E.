#pragma once
#include "../color_transfer.hpp"
#include "../paint_texture_copy.hpp"
#include "../paint_texture_document.hpp"
#include "../paint_texture_task.hpp"
#include "../texture_authoring.hpp"
#include "../texture_importer.hpp"
#include "editor_state.hpp"
#include "property_drawer.hpp"
#include "source_document_panel.hpp"
#include "ui_probe.hpp"
#include <cmath>

namespace forge {
struct PaintCanvasImage {
    ImTextureID image = 0;
    bool current = false;
    std::string diagnostic;
};
class PaintTextureEditor : public SourceDocumentPanel<PaintTextureDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    explicit PaintTextureEditor(std::filesystem::path worker)
        : SourceDocumentPanel(
              "Paint texture", [worker] { return texture_import_registry(worker); },
              prepare_texture_publication, desktop_texture_target()),
          worker_(std::move(worker)) {}
    std::function<PaintCanvasImage(const PaintTextureValue&, std::uint64_t, std::uint64_t, unsigned,
                                   unsigned)>
        draw_preview;
    std::function<void()> release_preview;
    static bool handles(const AssetRecord& record) {
        if (record.subasset || record.type != TextureAsset::type)
            return false;
        const auto imported = record.metadata.find("forge.import");
        return (imported != record.metadata.end() && imported->is_object() &&
                imported->value("importer", std::string{}) ==
                    asset_detail::paint_texture_importer_id) ||
               path_utf8(record.source).ends_with(PaintTextureDocumentTraits::suffix);
    }
    bool stroke_active() const { return gesture_dirty(); }
    void prepare_stroke(std::vector<PaintPoint> points, PaintBrush brush, std::uint64_t revision) {
        if (!document_ || pending() || document_->revision() != revision)
            throw std::runtime_error("Paint stroke source revision is no longer available");
        task_.start(document_->source().document, std::move(points), brush, document_->session(),
                    revision);
    }
    void prepare_copy(AssetId image, std::uint64_t revision) {
        if (!document_ || pending() || document_->revision() != revision)
            throw std::runtime_error("Pixel copy source revision is no longer available");
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(document_->project()));
        const auto root = document_->project();
        const auto semantic = document_->source().document.semantic();
        task_.copy_pixels(
            document_->source().document,
            [root, catalog = std::move(catalog), image, semantic,
             worker = worker_](std::stop_token stop) {
                return load_paint_texture_pixels(root, *catalog, {image}, semantic, worker, stop);
            },
            document_->session(), revision);
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New paint texture...", "Create an editable 2D RGBA8 texture source."))
            ImGui::OpenPopup("New paint texture");
        FORGE_UI_PROBE("paint-texture:new");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New paint texture", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("paint-texture:new-path");
            ImGui::InputInt("Width", &new_size_[0], 0, 0);
            FORGE_UI_PROBE("paint-texture:new-width");
            ImGui::InputInt("Height", &new_size_[1], 0, 0);
            FORGE_UI_PROBE("paint-texture:new-height");
            ImGui::Combo("Interpretation", &new_semantic_,
                         "Color (sRGB storage)\0Data (raw channels)\0");
            ui::help("1 to 1024 pixels per dimension. Canvas size and interpretation are fixed at "
                     "creation.");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button("Create",
                           "Create a transparent source; existing files are protected.")) {
                try {
                    if (new_size_[0] < 1 || new_size_[1] < 1 || new_size_[0] > 1024 ||
                        new_size_[1] > 1024)
                        throw std::runtime_error("Canvas dimensions must be 1 to 1024");
                    auto created = Document::create(
                        project.writer_guard(), std::filesystem::u8path(path_), [&](AssetId id) {
                            return PaintTextureSource{PaintTextureValue::create(
                                id, unsigned(new_size_[0]), unsigned(new_size_[1]),
                                new_semantic_ == 0 ? TextureSemantic::Color
                                                   : TextureSemantic::Data)};
                        });
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            FORGE_UI_PROBE("paint-texture:create");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a source."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        SourceDocumentPanel::poll(project, message);
        if (auto result = task_.poll()) {
            points_.clear();
            if (!document_ || result->session != document_->session() ||
                result->revision != document_->revision()) {
                error_ = "Prepared pixel edit discarded because the source document changed.";
                return;
            }
            if (!result->error.empty()) {
                error_ = std::move(result->error);
                save_ = close_ = false;
                return;
            }
            apply(result->revision, std::move(result->label),
                  [&](auto& source) { source.document = std::move(result->value); });
        }
    }
    void draw(SceneDocument&, Scene&, bool locked) {
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Paint Texture###Paint Texture", &open_error_))
                    ui::field_error(error_);
                ImGui::End();
            }
            return;
        }
        if (locked)
            cancel_gesture();
        if (save_ && !locked && !pending() && flush_gesture()) {
            save_ = false;
            publish();
        }
        ui::draft_window_size({850 * ui::interface_scale, 720 * ui::interface_scale});
        bool opened = true;
        const bool expanded = ImGui::Begin("Paint Texture###Paint Texture", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("paint_texture",
                                                    path_utf8(document_->locator()));
        if (gesture_dirty() && ImGui::IsKeyPressed(ImGuiKey_Escape) &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            cancel_gesture();
        if (!task_.pending() && !points_.empty() &&
            (!expanded || ImGui::GetIO().AppFocusLost ||
             !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
             ImGui::IsKeyPressed(ImGuiKey_Escape)))
            cancel_gesture();
        if (expanded && document_) {
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            const auto& canvas_value = document_->source().document;
            ImGui::Text("%u x %u | %s", canvas_value.width(), canvas_value.height(),
                        canvas_value.semantic() == TextureSemantic::Color ? "Color" : "Data");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish",
                           "Save the source and publish for materials, Game and export."))
                request_save();
            FORGE_UI_PROBE("paint-texture:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Cancel a prepared stroke, or undo one completed stroke/fill."))
                undo();
            FORGE_UI_PROBE("paint-texture:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Restore one source edit."))
                redo();
            FORGE_UI_PROBE("paint-texture:redo");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(stroke_active());
            ImGui::Checkbox("Erase", &brush_.erase);
            ui::help("Color erase reduces alpha. Data erase blends each channel toward the canvas "
                     "background.");
            float rgba[4];
            for (unsigned i = 0; i < 4; ++i)
                rgba[i] = float(brush_.color[i]);
            if (ImGui::ColorEdit4(document_->source().document.semantic() == TextureSemantic::Color
                                      ? "Brush (linear RGB)"
                                      : "Brush (raw channels)",
                                  rgba, ImGuiColorEditFlags_Float))
                for (unsigned i = 0; i < 4; ++i)
                    brush_.color[i] = std::clamp(double(rgba[i]), 0., 1.);
            float radius = float(brush_.radius), hardness = float(brush_.hardness),
                  opacity = float(brush_.opacity);
            if (ImGui::SliderFloat("Radius (pixels)", &radius, .5f, 256.f))
                brush_.radius = radius;
            FORGE_UI_PROBE("paint-texture:radius");
            if (ImGui::SliderFloat("Hardness", &hardness, 0, 1))
                brush_.hardness = hardness;
            if (ImGui::SliderFloat("Opacity", &opacity, 0, 1))
                brush_.opacity = opacity;
            if (ui::button("Fill canvas", "Replace every pixel with the brush color in one Undo "
                                          "step; erase mode is ignored.")) {
                PaintPixel pixel;
                const bool color =
                    document_->source().document.semantic() == TextureSemantic::Color;
                for (unsigned i = 0; i < 4; ++i)
                    pixel[i] = std::uint8_t(std::lround(
                        (color && i < 3 ? linear_to_srgb(brush_.color[i]) : brush_.color[i]) *
                        255));
                apply(document_->revision(), "Fill paint texture",
                      [&](auto& source) { source.document = source.document.fill(pixel); });
            }
            FORGE_UI_PROBE("paint-texture:fill");
            ImGui::SameLine();
            if (ui::button("Fit canvas", "Fit the source image inside the canvas."))
                fit_ = true;
            ImGui::SameLine();
            if (ui::button("1:1 pixels", "One source pixel per screen pixel; scroll to pan.")) {
                fit_ = false;
                zoom_ = 1;
            }
            if (ImGui::SliderFloat("Zoom", &zoom_, .125f, 8.f, "%.3fx"))
                fit_ = false;
            const bool copy_panel = ImGui::TreeNode("Copy published pixels");
            FORGE_UI_PROBE("paint-texture:copy-panel");
            if (ImGui::IsItemToggledOpen() && copy_panel) {
                try {
                    catalog_ = std::make_shared<const AssetCatalog>(
                        AssetCatalog::open_project(document_->project()));
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            if (copy_panel) {
                if (catalog_)
                    asset_ref_picker(*catalog_, copy_texture_, TextureAsset::type,
                                     "Published image", false);
                ImGui::TextWrapped("Replaces this canvas from the published base image in one Undo "
                                   "step. Source dimensions and Color/Data usage must match. The "
                                   "selected asset stays unchanged.");
                ImGui::BeginDisabled(copy_texture_.is_null());
                if (ui::button(
                        "Copy pixels",
                        "Prepare stored byte samples or unsigned LDR block samples; signed/HDR, "
                        "higher precision and premultiplied color are unsupported.")) {
                    try {
                        prepare_copy(copy_texture_.get<AssetId>(), document_->revision());
                    } catch (const std::exception& error) {
                        error_ = error.what();
                    }
                }
                FORGE_UI_PROBE("paint-texture:copy");
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::TextWrapped("Left drag paints; release applies one Undo step. Escape cancels. "
                               "Stroke guide shows the path until pixels are prepared.");
            if (task_.pending())
                ImGui::TextUnformatted("Preparing pixels... Undo cancels.");
            else if (pending())
                ImGui::TextUnformatted("Publishing texture...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
            canvas(locked || pending() || task_.pending());
        }
        ImGui::End();
        draw_close_dialog("Pending paint texture changes", locked || stroke_active());
    }

  private:
    char path_[256] = "Assets/Painted.paint-texture.json";
    int new_size_[2]{256, 256}, new_semantic_ = 0;
    std::filesystem::path worker_;
    Json copy_texture_ = nullptr;
    PaintBrush brush_;
    std::vector<PaintPoint> points_;
    PaintTextureTask task_;
    std::uint64_t stroke_revision_ = 0;
    bool fit_ = true;
    float zoom_ = 1;
    bool gesture_dirty() const override { return !points_.empty() || task_.pending(); }
    void cancel_gesture() override {
        task_.cancel();
        points_.clear();
    }
    void view_closed() override {
        if (release_preview)
            release_preview();
    }
    void source_opened() override { fit_ = true; }
    bool flush_gesture() override {
        if (task_.pending())
            return false;
        if (points_.empty())
            return true;
        try {
            prepare_stroke(points_, brush_, stroke_revision_);
            return false;
        } catch (const std::exception& error) {
            error_ = error.what();
            points_.clear();
            save_ = close_ = false;
            return false;
        }
    }
    template <class F> void apply(std::uint64_t revision, std::string label, F mutation) {
        try {
            document_->edit_source(revision, std::move(label), [&](auto& source) {
                mutation(source);
                if (!document_->retains_history(source))
                    throw std::runtime_error("Edit exceeds Undo budget; use a shorter stroke.");
            });
            if (document_->revision() != revision)
                changed();
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
            save_ = close_ = false;
        }
    }
    void canvas(bool locked) {
        const auto& value = document_->source().document;
        if (ImGui::BeginChild("paint-canvas", {0, 0}, ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            const auto available = ImGui::GetContentRegionAvail();
            const float factor = fit_ ? std::max(.001f, std::min(available.x / value.width(),
                                                                 available.y / value.height()))
                                      : zoom_;
            const ImVec2 size{std::max(1.f, value.width() * factor),
                              std::max(1.f, value.height() * factor)};
            const auto origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##paint-image", size);
            FORGE_UI_PROBE("paint-texture:canvas");
            const bool hovered = ImGui::IsItemHovered();
            auto* draw = ImGui::GetWindowDrawList();
            const auto clip_min = draw->GetClipRectMin(), clip_max = draw->GetClipRectMax();
            const ImVec2 minimum{std::max(origin.x, clip_min.x), std::max(origin.y, clip_min.y)};
            const ImVec2 maximum{std::min(origin.x + size.x, clip_max.x),
                                 std::min(origin.y + size.y, clip_max.y)};
            // Keep one bounded complete image so crop/zoom changes cannot alter
            // paint coordinates. The draw list clips presentation to this child.
            PaintCanvasImage image;
            if (draw_preview && maximum.x > minimum.x && maximum.y > minimum.y)
                image = draw_preview(value, document_->session(), document_->revision(),
                                     std::min(2048u, std::max(1u, unsigned(size.x))),
                                     std::min(2048u, std::max(1u, unsigned(size.y))));
            if (image.image)
                draw->AddImage(ImTextureRef{image.image}, origin,
                               {origin.x + size.x, origin.y + size.y});
            if (!image.diagnostic.empty())
                ui::field_error(image.diagnostic);
            auto& io = ImGui::GetIO();
            if (!locked && image.current && hovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                stroke_revision_ = document_->revision();
                points_.clear();
                error_.clear();
            }
            const bool starts =
                !locked && image.current && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (!locked && (starts || !points_.empty()) &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const PaintPoint point{(io.MousePos.x - origin.x) / factor,
                                       (io.MousePos.y - origin.y) / factor};
                if (points_.empty() ||
                    std::hypot(point.x - points_.back().x, point.y - points_.back().y) >= .25) {
                    if (points_.size() == paint_texture_stroke_point_limit) {
                        cancel_gesture();
                        error_ = "Stroke exceeds 256 path points; use a shorter stroke.";
                    } else
                        points_.push_back(point);
                }
            }
            if (!locked && !points_.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                (void)flush_gesture();
            if (!points_.empty()) {
                const auto screen = [&](PaintPoint p) {
                    return ImVec2{origin.x + float(p.x) * factor, origin.y + float(p.y) * factor};
                };
                const ImU32 guide = IM_COL32(255, 190, 60, 160);
                for (std::size_t i = 1; i < points_.size(); ++i)
                    draw->AddLine(screen(points_[i - 1]), screen(points_[i]), guide, 2);
                draw->AddCircle(screen(points_.back()), float(brush_.radius) * factor, guide, 0,
                                1.5f);
            } else if (!locked && hovered)
                draw->AddCircle(io.MousePos, float(brush_.radius) * factor,
                                IM_COL32(255, 255, 255, 220), 0, 1.5f);
        }
        ImGui::EndChild();
    }
};
} // namespace forge
