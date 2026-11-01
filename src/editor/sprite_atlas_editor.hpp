#pragma once
#include "../sprite_atlas_document.hpp"
#include "../sprite_atlas_settings.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"

namespace forge {
// UI adapter for the existing source/history and import/publication owners.
// Generated frame rectangles never become a second editable source document.
class SpriteAtlasEditor {
    friend struct AssetDocumentAutomation;

  public:
    explicit SpriteAtlasEditor(std::filesystem::path worker) : worker_(std::move(worker)) {}
    bool close_cancelled = false;
    bool is_open() const { return document_ || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const SpriteAtlasDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void catalog_changed(std::shared_ptr<const AssetCatalog> value) { catalog_ = std::move(value); }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (can_undo()) {
            document_->undo();
            changed();
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& path) {
        if (dirty()) {
            next_source_ = path;
            request_close();
            return;
        }
        try {
            load(project, path);
        } catch (const std::exception& e) {
            error_ = e.what();
            open_error_ = true;
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New Sprite atlas...",
                       "Keep reusable image inputs and packing settings in an atlas source."))
            ImGui::OpenPopup("New Sprite atlas");
        FORGE_UI_PROBE("sprite_atlas:new");
        ImGui::EndDisabled();
        if (!ImGui::BeginPopupModal("New Sprite atlas", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::InputText("Project file", path_, sizeof(path_));
        FORGE_UI_PROBE("sprite_atlas:new_path");
        const auto catalog = AssetCatalog::open_project(project.project());
        const auto allowed = image_choices(catalog);
        asset_ref_picker(catalog, new_image_, TextureAsset::type, "First image", false, {},
                         &allowed);
        FORGE_UI_PROBE("sprite_atlas:new_image");
        ImGui::BeginDisabled(locked || dirty() || new_image_.is_null() ||
                             !allowed.contains(new_image_.get<AssetId>()));
        if (ui::button("Create",
                       "Create the source with one input. Add more images in its editor.")) {
            try {
                const std::array inputs{new_image_.get<AssetId>()};
                auto created = SpriteAtlasDocument::create(
                    project.writer_guard(), std::filesystem::u8path(path_),
                    [&](AssetId id) { return SpriteAtlasRecipe::create(id, inputs); });
                const auto path = created->locator();
                created.reset();
                load(project, path);
                ImGui::CloseCurrentPopup();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        FORGE_UI_PROBE("sprite_atlas:create");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Cancel", "Close without creating an atlas."))
            ImGui::CloseCurrentPopup();
        if (!error_.empty())
            ui::field_error(error_);
        ImGui::EndPopup();
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            published_.reset();
            finish_close();
            next_source_.reset();
        }
        if (service_)
            for (auto& result : service_->poll()) {
                if (result.job.id != job_)
                    continue;
                job_ = 0;
                if (result.published) {
                    catalog_ = published_ =
                        std::make_shared<const AssetCatalog>(result.publication->catalog);
                    needs_publish_ = false;
                    error_.clear();
                    message = "Sprite atlas saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            const auto path = std::exchange(next_source_, {});
            open(project, *path);
        }
    }
    void draw(SceneDocument&, bool locked) {
        if (!is_open())
            return;
        if (save_ && document_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Sprite atlas###SpriteAtlas", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("sprite_atlas",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::TextWrapped("Frame keys follow their images when the atlas is rebuilt. Enable "
                               "Texture frames on each Sprite using this atlas's image material.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Save the recipe and rebuild its Texture. Failure "
                                             "keeps the last working atlas."))
                request_save();
            FORGE_UI_PROBE("sprite_atlas:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo the last atlas source edit."))
                undo();
            FORGE_UI_PROBE("sprite_atlas:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Restore an atlas source edit."))
                redo();
            FORGE_UI_PROBE("sprite_atlas:redo");
            ImGui::EndDisabled();
            inputs();
            settings();
            ImGui::EndDisabled();
        }
        if (expanded && !error_.empty())
            ui::field_error(error_);
        if (expanded && pending())
            ImGui::TextUnformatted("Building atlas...");
        draw_close(locked);
        ImGui::End();
    }

  private:
    std::filesystem::path worker_;
    std::unique_ptr<SpriteAtlasDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    AssetJobId job_ = 0;
    std::optional<std::filesystem::path> next_source_;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    char path_[256] = "Assets/NewSpriteAtlas.atlas.json";
    Json new_image_ = nullptr, add_image_ = nullptr;
    std::string error_;
    void changed() { error_.clear(); }
    static std::set<AssetId> image_choices(const AssetCatalog& catalog) {
        std::set<AssetId> result;
        for (const auto& [id, record] : catalog.records())
            if (record.type == TextureAsset::type && !record.subasset &&
                record.metadata.contains("forge.import"))
                result.insert(id);
        return result;
    }
    template <class F> void mutate(const char* label, F edit) {
        try {
            const auto revision = document_->revision();
            document_->edit_source(revision, label, edit);
            if (document_->revision() != revision)
                changed();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void inputs() {
        ImGui::SeparatorText("Input images");
        auto allowed = image_choices(*catalog_);
        allowed.erase(document_->source().asset());
        const auto rows = document_->source().inputs();
        for (const auto& row : rows)
            allowed.erase(row.texture);
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            ui::IdScope scope(std::to_string(row.id).c_str());
            ImGui::Text("Frame key %u", row.id);
            Json texture = row.texture;
            allowed.insert(row.texture);
            if (asset_ref_picker(*catalog_, texture, TextureAsset::type, "Image", false, {},
                                 &allowed)) {
                if (!texture.is_null())
                    mutate("Replace atlas image", [&](auto& source) {
                        source.replace_image(row.id, texture.template get<AssetId>());
                    });
                else
                    error_ = "Use Remove image to retire this frame key.";
            }
            allowed.erase(row.texture);
            FORGE_UI_PROBE("sprite_atlas:image:" + std::to_string(row.id));
            ImGui::BeginDisabled(index == 0);
            if (ui::button("Move up", "Change input order without changing this frame key."))
                reorder(index, index - 1);
            FORGE_UI_PROBE("sprite_atlas:up:" + std::to_string(row.id));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(index + 1 == rows.size());
            if (ui::button("Move down", "Change input order without changing this frame key."))
                reorder(index, index + 1);
            FORGE_UI_PROBE("sprite_atlas:down:" + std::to_string(row.id));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(rows.size() == 1);
            if (ui::button("Remove image",
                           "Retire this frame key. Sprites using it need an explicit replacement."))
                mutate("Remove atlas image", [&](auto& source) { source.erase_image(row.id); });
            FORGE_UI_PROBE("sprite_atlas:remove:" + std::to_string(row.id));
            ImGui::EndDisabled();
        }
        asset_ref_picker(*catalog_, add_image_, TextureAsset::type, "Add image", false, {},
                         &allowed);
        FORGE_UI_PROBE("sprite_atlas:add_image");
        ImGui::BeginDisabled(add_image_.is_null() || rows.size() == sprite_frame_region_limit);
        if (ui::button("Add", "Allocate a new stable frame key for this image."))
            mutate("Add atlas image",
                   [&](auto& source) { source.add_image(add_image_.template get<AssetId>()); });
        FORGE_UI_PROBE("sprite_atlas:add");
        ImGui::EndDisabled();
    }
    void reorder(std::size_t a, std::size_t b) {
        auto rows = document_->source().inputs();
        if (a >= rows.size() || b >= rows.size())
            return;
        std::swap(rows[a], rows[b]);
        std::vector<std::uint32_t> order;
        for (const auto& row : rows)
            order.push_back(row.id);
        mutate("Reorder atlas images", [&](auto& source) { source.reorder(order); });
    }
    void settings() {
        ImGui::SeparatorText("Packing");
        auto values = document_->source().settings();
        int extent = int(values.max_size), padding = int(values.padding);
        bool edited = false;
        if (ImGui::InputInt("Maximum side (pixels)", &extent, 1, 16,
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            values.max_size = unsigned(extent);
            edited = true;
        }
        FORGE_UI_PROBE("sprite_atlas:max_size");
        if (ImGui::InputInt("Padding (pixels)", &padding, 1, 4,
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            values.padding = unsigned(padding);
            edited = true;
        }
        FORGE_UI_PROBE("sprite_atlas:padding");
        edited |= ImGui::Checkbox("Extrude edges", &values.extrude);
        FORGE_UI_PROBE("sprite_atlas:extrude");
        edited |= ImGui::Checkbox("Trim transparent edges", &values.trim_transparent);
        FORGE_UI_PROBE("sprite_atlas:trim");
        edited |= ImGui::Checkbox("Allow quarter turns", &values.allow_rotation);
        FORGE_UI_PROBE("sprite_atlas:rotation");
        if (edited)
            mutate("Change atlas packing", [&](auto& source) {
                source.document["settings"] = asset_detail::sprite_atlas_settings(values);
            });
        ImGui::TextWrapped("Press Enter to apply numeric settings. At most 512 distinct published "
                           "Color images and 4096 pixels per atlas side are supported. Sources "
                           "must provide sRGB RGBA8 pixels.");
    }
    void finish_close() {
        document_.reset();
        service_.reset();
        catalog_.reset();
        job_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = false;
        add_image_ = nullptr;
    }
    void load(SceneDocument& project, const std::filesystem::path& path) {
        auto document = std::make_unique<SpriteAtlasDocument>(project.writer_guard(), path);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), texture_import_registry(worker_), desktop_texture_target());
        const auto found = catalog->records().find(document->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(path),
                                                    sprite_atlas_recipe_byte_limit);
        const bool needs = found == catalog->records().end() ||
                           !found->second.metadata.contains("forge.import") ||
                           found->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(document);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        error_.clear();
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_texture_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            error_.clear();
        } catch (const std::exception& e) {
            needs_publish_ = true;
            close_ = false;
            error_ = e.what();
        }
    }
    void draw_close(bool locked) {
        if (close_ && document_ && !ImGui::IsPopupOpen("Unsaved Sprite atlas"))
            ImGui::OpenPopup("Unsaved Sprite atlas");
        if (!ImGui::BeginPopupModal("Unsaved Sprite atlas", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextWrapped("Save / Publish before closing? Discard closes this draft; saved source "
                           "files remain on disk.");
        ImGui::BeginDisabled(locked || pending());
        if (ui::button("Save / Publish", "Publish and close after success.")) {
            request_save();
            ImGui::CloseCurrentPopup();
        }
        FORGE_UI_PROBE("sprite_atlas:close_save");
        ImGui::SameLine();
        if (ui::button("Discard", "Close unsaved edits and keep the last working publication.")) {
            finish_close();
            ImGui::CloseCurrentPopup();
        }
        FORGE_UI_PROBE("sprite_atlas:close_discard");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Cancel", "Keep the atlas open.")) {
            close_ = false;
            next_source_.reset();
            close_cancelled = true;
            ImGui::CloseCurrentPopup();
        }
        FORGE_UI_PROBE("sprite_atlas:close_cancel");
        ImGui::EndPopup();
    }
};
} // namespace forge
