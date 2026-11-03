#pragma once
#include "../render_texture_authoring.hpp"
#include "../texture_authoring.hpp"
#include "editor_state.hpp"
#include "source_document_panel.hpp"

namespace forge {
class RenderTextureEditor : public SourceDocumentPanel<RenderTextureDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    RenderTextureEditor()
        : SourceDocumentPanel("Render texture", render_texture_import_registry,
                              prepare_render_texture_publication, desktop_texture_target()) {}
    bool has_unapplied_edits() const { return size_dirty_; }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New render texture...", "Create a saved camera output configuration."))
            ImGui::OpenPopup("New render texture");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New render texture", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ImGui::BeginDisabled(locked);
            if (ui::button("Create", "Create a 512 by 512 linear HDR camera output asset.")) {
                try {
                    auto created = RenderTextureDocument::create(project.writer_guard(),
                                                                 std::filesystem::u8path(path_));
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating an asset."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void draw(SceneDocument&, Scene&, bool locked) {
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Render Texture###Render Texture", &open_error_))
                    ui::field_error(error_);
                ImGui::End();
            }
            return;
        }
        if (save_ && !locked && !pending()) {
            save_ = false;
            if (flush_gesture())
                publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Render Texture###Render Texture", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("render_texture",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped("Linear HDR camera output. Assign this asset to a Camera, then "
                               "select it in a mesh Material's 2D Color slot.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish",
                           "Apply dimensions, save and publish for Game and export."))
                request_save();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Cancel unapplied dimensions or undo the last source edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo the last source edit."))
                redo();
            ImGui::EndDisabled();
            ImGui::SeparatorText("Output image");
            bool resized = false;
            resized |= ImGui::InputInt("Width", &size_[0], 0, 0);
            if (ImGui::IsItemActive())
                size_input_id_ = ImGui::GetActiveID();
            FORGE_UI_PROBE("render-texture:width");
            resized |= ImGui::InputInt("Height", &size_[1], 0, 0);
            if (ImGui::IsItemActive())
                size_input_id_ = ImGui::GetActiveID();
            FORGE_UI_PROBE("render-texture:height");
            if (resized) {
                if (!size_dirty_)
                    size_revision_ = document_->revision();
                const auto saved = document_->source().compiled();
                size_dirty_ = size_[0] != int(saved.width) || size_[1] != int(saved.height);
            }
            ImGui::TextWrapped(
                "1 to 2048 pixels per dimension. Linear RGBA16 float, opaque, "
                "one image level. Display exposure is applied by the consuming view.");
            ImGui::BeginDisabled(!size_dirty_);
            if (ui::button("Apply dimensions", "Record both dimensions as one source Undo step."))
                (void)flush_gesture();
            FORGE_UI_PROBE("render-texture:apply");
            ImGui::SameLine();
            if (ui::button("Cancel dimensions", "Restore dimensions from the current source."))
                cancel_gesture();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing render texture...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        draw_close_dialog("Pending render texture changes", locked);
    }

  private:
    char path_[256] = "Assets/Camera.rendertexture.json";
    int size_[2] = {512, 512};
    bool size_dirty_ = false;
    std::uint64_t size_revision_ = 0;
    ImGuiID size_input_id_ = 0;
    bool gesture_dirty() const override { return size_dirty_; }
    void reconcile() {
        size_dirty_ = false;
        if (document_) {
            const auto value = document_->source().compiled();
            size_[0] = int(value.width);
            size_[1] = int(value.height);
            size_revision_ = document_->revision();
        }
    }
    void cancel_gesture() override {
        if (auto* context = ImGui::GetCurrentContext(); context && size_input_id_) {
            if (ImGui::GetActiveID() == size_input_id_)
                ImGui::ClearActiveID();
            // The pinned input backend retains one final commit after blur.
            // Cancel only this owned draft, so Undo cannot reapply its old text.
            if (context->InputTextDeactivatedState.ID == size_input_id_)
                context->InputTextDeactivatedState.ClearFreeMemory();
        }
        size_input_id_ = 0;
        reconcile();
    }
    void source_opened() override { reconcile(); }
    void changed() override {
        SourceDocumentPanel::changed();
        reconcile();
    }
    bool flush_gesture() override {
        if (!size_dirty_)
            return true;
        const int width = size_[0], height = size_[1];
        return edit_source(size_revision_, "Resize render texture", [=](Json& d) {
            d["width"] = width;
            d["height"] = height;
        });
    }
};
} // namespace forge
