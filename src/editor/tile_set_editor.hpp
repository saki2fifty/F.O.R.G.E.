#pragma once
#include "../texture_authoring.hpp"
#include "../tile_asset_authoring.hpp"
#include "../tile_set_document.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <charconv>

namespace forge {
// Central Tile Set source UI. The existing document owns source/history/Save;
// the importer owns publication, and DocumentWorkspace borrows this adapter.
class TileSetEditor {
    friend struct AssetDocumentAutomation;

  public:
    explicit TileSetEditor(std::filesystem::path worker) : worker_(std::move(worker)) {}
    std::function<void(const TileSetSource&, std::uint64_t, std::shared_ptr<const AssetCatalog>,
                       std::uint64_t, double)>
        draw_preview;
    std::function<void()> release_preview;
    bool close_cancelled = false;
    bool is_open() const { return document_ || open_error_; }
    bool pending() const { return job_ != 0; }
    bool has_unapplied_edits() const {
        return collision_dirty_ || animation_dirty_ || autotile_dirty_;
    }
    bool dirty() const {
        return document_ &&
               (document_->dirty() || needs_publish_ || pending() || has_unapplied_edits());
    }
    bool can_undo() const {
        return document_ && !pending() && !has_unapplied_edits() && document_->can_undo();
    }
    bool can_redo() const {
        return document_ && !pending() && !has_unapplied_edits() && document_->can_redo();
    }
    const TileSetDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
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
    void catalog_changed(std::shared_ptr<const AssetCatalog> value) { catalog_ = std::move(value); }
    void source_published(SceneDocument& project, AssetId id) {
        if (document_ && document_->source().asset() == id && !dirty())
            load(project, document_->locator());
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
        if (ui::button("New Tile Set...", "Define reusable tiles from ordinary image Materials."))
            ImGui::OpenPopup("New Tile Set");
        FORGE_UI_PROBE("tile_set:new");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New Tile Set", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("tile_set:new_path");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button("Create",
                           "Create a .tileset.json source; existing files are protected.")) {
                try {
                    auto created = TileSetDocument::create(project.writer_guard(),
                                                           std::filesystem::u8path(path_));
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            FORGE_UI_PROBE("tile_set:create");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating source."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
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
                    message = "Tile Set saved and published.";
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
        const bool expanded = ImGui::Begin("Tile Set###TileSet", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("tile_set", path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            ImGui::BeginDisabled(has_unapplied_edits());
            if (ui::button(
                    "Save / Publish",
                    "Save definitions and publish; failures retain the last working selection."))
                request_save();
            FORGE_UI_PROBE("tile_set:save");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo the last Tile Set source edit."))
                undo();
            FORGE_UI_PROBE("tile_set:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Restore a Tile Set source edit."))
                redo();
            FORGE_UI_PROBE("tile_set:redo");
            ImGui::EndDisabled();
            if (collision_dirty_) {
                ImGui::TextWrapped(
                    "Unapplied collision edits: review them in Inspector before continuing.");
                if (ui::button("Review collision edits",
                               "Select the tile whose collision profile is being edited.")) {
                    selected_tile_ = collision_tile_;
                    select("tile", collision_tile_);
                }
                FORGE_UI_PROBE("tile_set:collision_review");
            }
            if (animation_dirty_) {
                ImGui::TextWrapped(
                    "Unapplied animation edits: review them in Inspector before continuing.");
                if (ui::button("Review animation edits",
                               "Select the tile whose animation is being edited.")) {
                    selected_tile_ = animation_tile_;
                    select("tile", animation_tile_);
                }
                FORGE_UI_PROBE("tile_set:animation_review");
            }
            if (autotile_dirty_) {
                ImGui::TextWrapped(
                    "Unapplied neighbor rules: review them in Inspector before continuing.");
                if (ui::button("Review neighbor rules",
                               "Select the tile whose neighbor rules are being edited.")) {
                    selected_tile_ = autotile_tile_;
                    select("tile", autotile_tile_);
                }
                FORGE_UI_PROBE("tile_set:autotile_review");
            }
            ImGui::BeginDisabled(has_unapplied_edits());
            draw_lists();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ui::help("Select an image or tile to edit it in Inspector. Use an unlit Material with "
                     "one color image. Grid margins and spacing are pixels.");
            if (draw_preview) {
                try {
                    if (ui::button(preview_playing_ ? "Pause preview" : "Play preview",
                                   "Preview the applied tile sequence; static tiles stay still.")) {
                        preview_playing_ = !preview_playing_;
                        preview_last_time_ = -1;
                    }
                    FORGE_UI_PROBE("tile_set:preview_play");
                    ImGui::SameLine();
                    if (ui::button("Restart preview",
                                   "Return the asset preview to the first frame.")) {
                        preview_seconds_ = 0;
                        preview_last_time_ = -1;
                    }
                    FORGE_UI_PROBE("tile_set:preview_restart");
                    const auto now = ImGui::GetTime();
                    if (preview_playing_ && preview_last_time_ >= 0)
                        preview_seconds_ += std::clamp(now - preview_last_time_, 0., .25);
                    preview_last_time_ = now;
                    draw_preview(document_->source(), preview_revision_, catalog_, selected_tile_,
                                 preview_seconds_);
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            if (pending())
                ImGui::TextUnformatted("Publishing Tile Set...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source needs Save / Publish.");
        }
        if (expanded && !error_.empty())
            ui::field_error(error_);
        draw_close(locked);
        ImGui::End();
    }
    void inspect(const std::string& key, bool locked) {
        if (!document_)
            return;
        const auto split = key.find('/');
        if (split == std::string::npos)
            return;
        std::uint64_t id = 0;
        const auto parsed = std::from_chars(key.data() + split + 1, key.data() + key.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size())
            return;
        if (has_unapplied_edits() &&
            (key.substr(0, split) != "tile" || id != (collision_dirty_   ? collision_tile_
                                                      : animation_dirty_ ? animation_tile_
                                                                         : autotile_tile_))) {
            ImGui::TextWrapped(
                "Apply or discard the pending tile profile before inspecting another definition.");
            return;
        }
        const auto snapshot = values();
        ImGui::BeginDisabled(locked || pending());
        if (key.substr(0, split) == "image") {
            const auto row = std::find_if(snapshot->images.begin(), snapshot->images.end(),
                                          [=](const auto& value) { return value.id == id; });
            if (row != snapshot->images.end())
                image_fields(*row);
            else {
                clear_selection();
                ImGui::TextUnformatted("This image was removed.");
            }
        } else if (key.substr(0, split) == "tile") {
            const auto row = std::find_if(snapshot->tiles.begin(), snapshot->tiles.end(),
                                          [=](const auto& value) { return value.id == id; });
            if (row != snapshot->tiles.end())
                tile_fields(*row, *snapshot);
            else {
                clear_selection();
                ImGui::TextUnformatted("This tile was removed.");
            }
        }
        ImGui::EndDisabled();
        if (!error_.empty())
            ui::field_error(error_);
    }

  private:
    std::filesystem::path worker_;
    std::unique_ptr<TileSetDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::shared_ptr<const TileSetValues> values_;
    std::uint64_t values_revision_ = 0, selected_image_ = 0, selected_tile_ = 0;
    std::uint64_t preview_revision_ = 1; // Survives a same-source document reload.
    AssetJobId job_ = 0;
    std::optional<std::filesystem::path> next_source_;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    Json new_material_ = nullptr;
    TileCollisionProfile collision_draft_;
    std::vector<TileCollisionContour> collision_contours_draft_;
    std::size_t collision_contour_ = 0, collision_loop_ = 0;
    std::uint64_t collision_tile_ = 0, collision_revision_ = 0;
    bool collision_dirty_ = false;
    TileAnimation animation_draft_;
    std::uint64_t animation_tile_ = 0, animation_revision_ = 0;
    bool animation_dirty_ = false;
    TileAutotile autotile_draft_;
    std::uint64_t autotile_tile_ = 0, autotile_revision_ = 0;
    bool autotile_dirty_ = false;
    bool preview_playing_ = false;
    double preview_seconds_ = 0, preview_last_time_ = -1;
    char path_[256] = "Assets/NewTiles.tileset.json";
    std::string error_;
    void changed() {
        ++preview_revision_;
        values_.reset();
        values_revision_ = 0;
        error_.clear();
        collision_revision_ = 0;
        collision_dirty_ = false;
        animation_revision_ = 0;
        animation_dirty_ = false;
        autotile_revision_ = 0;
        autotile_dirty_ = false;
        preview_seconds_ = 0;
        preview_last_time_ = -1;
    }
    std::shared_ptr<const TileSetValues> values() {
        if (!values_ || values_revision_ != document_->revision()) {
            values_ = std::make_shared<const TileSetValues>(document_->source().values());
            values_revision_ = document_->revision();
        }
        return values_;
    }
    void select(const char* kind, std::uint64_t id) {
        if (ui::editor_context)
            ui::editor_context->selection.select_document_item("tile_set", std::string(kind) + "/" +
                                                                               std::to_string(id));
    }
    void clear_selection() {
        if (ui::editor_context && ui::editor_context->selection.document() == "tile_set")
            ui::editor_context->selection.clear();
    }
    void finish_close() {
        clear_selection();
        if (release_preview)
            release_preview();
        document_.reset();
        service_.reset();
        catalog_.reset();
        values_.reset();
        values_revision_ = selected_image_ = selected_tile_ = 0;
        job_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = collision_dirty_ = animation_dirty_ = false;
        autotile_dirty_ = false;
        autotile_tile_ = autotile_revision_ = 0;
        autotile_draft_ = {};
        collision_tile_ = collision_revision_ = 0;
        animation_tile_ = animation_revision_ = 0;
        animation_draft_ = {};
        preview_playing_ = false;
        preview_seconds_ = 0;
        preview_last_time_ = -1;
    }
    void load(SceneDocument& project, const std::filesystem::path& path) {
        auto document = std::make_unique<TileSetDocument>(project.writer_guard(), path);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), tile_asset_import_registry(worker_), desktop_texture_target());
        const auto found = catalog->records().find(document->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(path),
                                                    tile_set_source_byte_limit);
        const bool needs = found == catalog->records().end() ||
                           !found->second.metadata.contains("forge.import") ||
                           found->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(document);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        changed();
    }
    template <class F> bool mutate(std::string label, F action) {
        try {
            document_->edit(document_->revision(), std::move(label), action);
            changed();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    static std::uint64_t allocate(Json& source) {
        const auto id = source.at("next_id").get<std::uint64_t>();
        if (id == UINT64_MAX)
            throw std::runtime_error("Tile Set key sequence is exhausted");
        source["next_id"] = id + 1;
        return id;
    }
    static Json& row(Json& source, const char* kind, std::uint64_t id) {
        for (auto& value : source.at(kind))
            if (value.at("id") == id)
                return value;
        throw std::runtime_error("Selected Tile Set entry no longer exists");
    }
    void draw_lists() {
        const auto snapshot = values();
        ImGui::SeparatorText("Images");
        for (const auto& image : snapshot->images) {
            const auto label = image.name + "###image/" + std::to_string(image.id);
            if (ImGui::Selectable(label.c_str(), selected_image_ == image.id)) {
                selected_image_ = image.id;
                select("image", image.id);
            }
            FORGE_UI_PROBE("tile_set:image:" + std::to_string(image.id));
        }
        asset_ref_picker(*catalog_, new_material_, MaterialAsset::type, "New image Material");
        ImGui::BeginDisabled(new_material_.is_null() ||
                             snapshot->images.size() == tile_set_image_limit);
        if (ui::button("Add image", "Add a Material image with a full-image 1 x 1 grid.")) {
            std::uint64_t id = 0;
            if (mutate("Add Tile Set image", [&](auto& source) {
                    id = allocate(source);
                    source["images"].push_back({{"id", id},
                                                {"name", "Image"},
                                                {"material", new_material_},
                                                {"grid",
                                                 {{"columns", 1},
                                                  {"rows", 1},
                                                  {"margin_x", 0},
                                                  {"margin_y", 0},
                                                  {"spacing_x", 0},
                                                  {"spacing_y", 0}}}});
                })) {
                selected_image_ = id;
                select("image", id);
            }
        }
        FORGE_UI_PROBE("tile_set:add_image");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Tiles");
        if (ImGui::BeginListBox("##tiles", ImVec2(-1, 160 * ui::interface_scale))) {
            ImGuiListClipper clipper;
            clipper.Begin(int(snapshot->tiles.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& tile = snapshot->tiles[std::size_t(i)];
                    const auto label = tile.name + "###tile/" + std::to_string(tile.id);
                    if (ImGui::Selectable(label.c_str(), tile.id == selected_tile_)) {
                        selected_tile_ = tile.id;
                        select("tile", tile.id);
                    }
                    FORGE_UI_PROBE("tile_set:tile:" + std::to_string(tile.id));
                }
            ImGui::EndListBox();
        }
        const bool image_exists =
            std::any_of(snapshot->images.begin(), snapshot->images.end(),
                        [&](const auto& image) { return image.id == selected_image_; });
        ImGui::BeginDisabled(!image_exists || snapshot->tiles.size() == tile_set_definition_limit);
        if (ui::button("Add tile", "Create one tile from frame zero of the selected image.")) {
            std::uint64_t id = 0;
            if (mutate("Add Tile Set tile", [&](auto& source) {
                    id = allocate(source);
                    source["tiles"].push_back(
                        {{"id", id}, {"name", "Tile"}, {"image", selected_image_}, {"frame", 0}});
                })) {
                selected_tile_ = id;
                select("tile", id);
            }
        }
        FORGE_UI_PROBE("tile_set:add_tile");
        ImGui::EndDisabled();
    }
    void name_field(const char* kind, std::uint64_t id, const std::string& name) {
        std::array<char, 129> text{};
        std::copy_n(name.begin(), std::min(name.size(), text.size() - 1), text.begin());
        if (ImGui::InputText("Name", text.data(), text.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            mutate("Rename Tile Set entry",
                   [&](auto& source) { row(source, kind, id)["name"] = text.data(); });
        FORGE_UI_PROBE("tile_set:name:" + std::string(kind));
    }
    void image_fields(const TileSetImage& image) {
        ImGui::SeparatorText("Tile image");
        name_field("images", image.id, image.name);
        Json material = image.material.id;
        if (asset_ref_picker(*catalog_, material, MaterialAsset::type, "Material"))
            mutate("Change tile image Material",
                   [&](auto& source) { row(source, "images", image.id)["material"] = material; });
        struct Field {
            const char* key;
            const char* label;
            std::uint32_t value;
        };
        const std::array<Field, 6> fields{{{"columns", "Columns", image.grid.columns},
                                           {"rows", "Rows", image.grid.rows},
                                           {"margin_x", "Margin X (px)", image.grid.margin_x},
                                           {"margin_y", "Margin Y (px)", image.grid.margin_y},
                                           {"spacing_x", "Spacing X (px)", image.grid.spacing_x},
                                           {"spacing_y", "Spacing Y (px)", image.grid.spacing_y}}};
        for (const auto& field : fields) {
            auto value = field.value;
            if (ImGui::InputScalar(field.label, ImGuiDataType_U32, &value, nullptr, nullptr, "%u",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Change tile image grid", [&](auto& source) {
                    row(source, "images", image.id)["grid"][field.key] = value;
                });
            FORGE_UI_PROBE("tile_set:grid:" + std::string(field.key));
        }
        if (ui::button("Remove image and its tiles",
                       "Remove this image and its definitions in one Undo step. Maps using those "
                       "keys need replacement or erase."))
            mutate("Remove Tile Set image", [&](auto& source) {
                std::erase_if(source["images"].template get_ref<Json::array_t&>(),
                              [&](const auto& value) { return value.at("id") == image.id; });
                std::erase_if(source["tiles"].template get_ref<Json::array_t&>(),
                              [&](const auto& value) { return value.at("image") == image.id; });
            });
        FORGE_UI_PROBE("tile_set:remove_image");
    }
    void tile_fields(const TileSetDefinition& tile, const TileSetValues& source_values) {
        ImGui::BeginDisabled(has_unapplied_edits());
        ImGui::SeparatorText("Tile definition");
        name_field("tiles", tile.id, tile.name);
        const auto image = std::find_if(source_values.images.begin(), source_values.images.end(),
                                        [&](const auto& value) { return value.id == tile.image; });
        if (ImGui::BeginCombo("Image", image == source_values.images.end() ? "Missing"
                                                                           : image->name.c_str())) {
            for (const auto& value : source_values.images) {
                const auto label = value.name + "###image/" + std::to_string(value.id);
                if (ImGui::Selectable(label.c_str(), value.id == tile.image))
                    mutate("Change tile image", [&](auto& source) {
                        row(source, "tiles", tile.id)["image"] = value.id;
                        row(source, "tiles", tile.id)["frame"] = 0;
                    });
            }
            ImGui::EndCombo();
        }
        auto frame = tile.frame;
        if (ImGui::InputScalar("Frame (zero based)", ImGuiDataType_U32, &frame, nullptr, nullptr,
                               "%u", ImGuiInputTextFlags_EnterReturnsTrue))
            mutate("Change tile frame",
                   [&](auto& source) { row(source, "tiles", tile.id)["frame"] = frame; });
        FORGE_UI_PROBE("tile_set:frame");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(animation_dirty_ || autotile_dirty_);
        collision_fields(tile);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(collision_dirty_ || autotile_dirty_);
        animation_fields(tile);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(collision_dirty_ || animation_dirty_);
        autotile_fields(tile);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(has_unapplied_edits());
        if (ui::button("Remove tile", "Remove this definition. Existing map cells keep the "
                                      "unresolved key until replaced or erased."))
            mutate("Remove Tile Set tile", [&](auto& source) {
                std::erase_if(source["tiles"].template get_ref<Json::array_t&>(),
                              [&](const auto& value) { return value.at("id") == tile.id; });
            });
        FORGE_UI_PROBE("tile_set:remove_tile");
        ImGui::EndDisabled();
    }
    void autotile_fields(const TileSetDefinition& tile) {
        if (autotile_tile_ != tile.id || autotile_revision_ != document_->revision()) {
            autotile_draft_ = tile.autotile ? *tile.autotile : TileAutotile{};
            autotile_tile_ = tile.id;
            autotile_revision_ = document_->revision();
            autotile_dirty_ = false;
        }
        if (!ImGui::CollapsingHeader("Tile neighbor rules", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (ImGui::Checkbox("Enabled###TileAutotileEnabled", &autotile_draft_.enabled)) {
            if (autotile_draft_.enabled && autotile_draft_.rules.empty())
                autotile_draft_.rules.push_back({0, 0, tile.id});
            autotile_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:autotile_enabled");
        const auto data = values();
        for (std::size_t i = 0; i < autotile_draft_.rules.size(); ++i) {
            ImGui::PushID(int(i));
            auto& rule = autotile_draft_.rules[i];
            ImGui::Separator();
            ImGui::Text("Rule %zu", i + 1);
            const auto target =
                std::find_if(data->tiles.begin(), data->tiles.end(),
                             [&](const auto& value) { return value.id == rule.tile; });
            if (ImGui::BeginCombo("Select tile", target == data->tiles.end()
                                                     ? "Missing tile"
                                                     : target->name.c_str())) {
                for (const auto& value : data->tiles) {
                    if (value.id != tile.id && value.autotile && value.autotile->enabled)
                        continue;
                    const auto label = value.name + "###target/" + std::to_string(value.id);
                    if (ImGui::Selectable(label.c_str(), rule.tile == value.id)) {
                        rule.tile = value.id;
                        autotile_dirty_ = true;
                    }
                    FORGE_UI_PROBE("tile_set:autotile_target:" + std::to_string(i) + ":" +
                                   std::to_string(value.id));
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("tile_set:autotile_target:" + std::to_string(i));
            if (ImGui::BeginTable("Neighbors", 3, ImGuiTableFlags_SizingStretchSame)) {
                constexpr int compass[]{7, 0, 1, 6, -1, 2, 5, 4, 3};
                constexpr const char* directions[]{"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
                for (const auto direction : compass) {
                    ImGui::TableNextColumn();
                    if (direction < 0) {
                        ImGui::TextUnformatted("This tile");
                        continue;
                    }
                    const auto bit = std::uint8_t(1u << direction);
                    const auto state = !(rule.mask & bit) ? 0 : (rule.same & bit) ? 1 : 2;
                    const auto* condition = state == 0 ? "Any" : state == 1 ? "Same" : "Different";
                    auto label = std::string(directions[direction]) + ": " + condition;
                    if (ImGui::CalcTextSize(label.c_str()).x +
                            2 * ImGui::GetStyle().FramePadding.x >
                        ImGui::GetContentRegionAvail().x)
                        label = std::string(directions[direction]) + "\n" + condition;
                    if (ImGui::Button(label.c_str(), {-FLT_MIN, 0})) {
                        if (state == 0) {
                            rule.mask |= bit;
                            rule.same |= bit;
                        } else if (state == 1)
                            rule.same &= ~bit;
                        else {
                            rule.mask &= ~bit;
                            rule.same &= ~bit;
                        }
                        autotile_dirty_ = true;
                    }
                    FORGE_UI_PROBE("tile_set:autotile_neighbor:" + std::to_string(i) + ":" +
                                   std::to_string(direction));
                }
                ImGui::EndTable();
            }
            bool remove = false;
            if (ImGui::BeginTable("Rule order", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button("Move up", {-FLT_MIN, 0})) {
                    std::swap(autotile_draft_.rules[i], autotile_draft_.rules[i - 1]);
                    autotile_dirty_ = true;
                }
                FORGE_UI_PROBE("tile_set:autotile_up:" + std::to_string(i));
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(i + 1 == autotile_draft_.rules.size());
                if (ImGui::Button("Move down", {-FLT_MIN, 0})) {
                    std::swap(autotile_draft_.rules[i], autotile_draft_.rules[i + 1]);
                    autotile_dirty_ = true;
                }
                FORGE_UI_PROBE("tile_set:autotile_down:" + std::to_string(i));
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(autotile_draft_.enabled && autotile_draft_.rules.size() == 1);
                remove = ImGui::Button("Remove", {-FLT_MIN, 0});
                FORGE_UI_PROBE("tile_set:autotile_remove:" + std::to_string(i));
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            ImGui::PopID();
            if (remove) {
                autotile_draft_.rules.erase(autotile_draft_.rules.begin() + i);
                autotile_dirty_ = true;
                break;
            }
        }
        ImGui::BeginDisabled(autotile_draft_.rules.size() >= tile_neighbor_rule_limit);
        if (ui::button("Add rule", "Append a rule. Rules are tested from top to bottom.")) {
            autotile_draft_.rules.push_back({0, 0, tile.id});
            autotile_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:autotile_add");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!autotile_dirty_);
        if (ui::button("Apply neighbor rules", "Validate and apply all rules in one Undo step.")) {
            const auto profile = tile_detail::tile_autotile_json(autotile_draft_);
            mutate("Change tile neighbor rules", [&](auto& source) {
                TileSetSource candidate{source};
                candidate.set_autotile(tile.id, profile);
                source = std::move(candidate.document);
            });
        }
        FORGE_UI_PROBE("tile_set:autotile_apply");
        if (ui::button("Discard neighbor edits", "Keep the applied neighbor rules.")) {
            autotile_draft_ = tile.autotile ? *tile.autotile : TileAutotile{};
            autotile_dirty_ = false;
            error_.clear();
        }
        FORGE_UI_PROBE("tile_set:autotile_discard");
        ImGui::EndDisabled();
        if (autotile_dirty_)
            ImGui::TextWrapped("Unapplied neighbor rules: Apply or Discard before Save, Undo or "
                               "selecting another tile.");
        ui::help("Click each neighbor to cycle Any, Same and Different. Same means this logical "
                 "tile ID in the same layer; "
                 "Different includes empty cells. Conditions follow the painted cell's "
                 "orientation. First matching rule wins; "
                 "otherwise this tile's own appearance and collision apply. Targets may animate "
                 "and choose their own collision.");
    }
    void animation_fields(const TileSetDefinition& tile) {
        if (animation_tile_ != tile.id || animation_revision_ != document_->revision()) {
            animation_draft_ = tile.animation ? *tile.animation : TileAnimation{};
            animation_tile_ = tile.id;
            animation_revision_ = document_->revision();
            animation_dirty_ = false;
        }
        ImGui::SeparatorText("Tile animation");
        if (ImGui::Checkbox("Enabled###TileAnimationEnabled", &animation_draft_.enabled)) {
            if (animation_draft_.enabled && animation_draft_.frames.empty())
                animation_draft_.frames.push_back({tile.frame, .1});
            animation_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:animation_enabled");
        if (ImGui::BeginTable("TileAnimationFrames", 3,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed,
                                    2.5f * ImGui::GetFontSize());
            ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Duration (s)", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < animation_draft_.frames.size(); ++i) {
                auto& entry = animation_draft_.frames[i];
                ImGui::PushID(int(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", unsigned(i + 1));
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                auto frame = entry.frame;
                if (ImGui::InputScalar("##frame", ImGuiDataType_U32, &frame, nullptr, nullptr, "%u",
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
                    entry.frame = frame;
                    animation_dirty_ = true;
                }
                FORGE_UI_PROBE("tile_set:animation_frame:" + std::to_string(i));
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1);
                auto seconds = entry.seconds;
                if (ImGui::InputScalar("##duration", ImGuiDataType_Double, &seconds, nullptr,
                                       nullptr, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                    entry.seconds = seconds;
                    animation_dirty_ = true;
                }
                FORGE_UI_PROBE("tile_set:animation_duration:" + std::to_string(i));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(animation_draft_.frames.size() >= tile_animation_frame_limit);
        if (ui::button("Add frame", "Append another image-grid frame to the sequence.")) {
            auto entry = animation_draft_.frames.empty() ? TileAnimationFrame{tile.frame, .1}
                                                         : animation_draft_.frames.back();
            if (!animation_draft_.frames.empty()) {
                const auto data = values();
                const auto image =
                    std::find_if(data->images.begin(), data->images.end(),
                                 [&](const auto& row) { return row.id == tile.image; });
                if (image != data->images.end())
                    entry.frame =
                        std::uint32_t((std::uint64_t(entry.frame) + 1) %
                                      (std::uint64_t(image->grid.columns) * image->grid.rows));
            }
            animation_draft_.frames.push_back(entry);
            animation_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:animation_add");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(animation_draft_.frames.empty() ||
                             (animation_draft_.enabled && animation_draft_.frames.size() == 1));
        if (ui::button("Remove last frame", "Remove the final frame from this sequence.")) {
            animation_draft_.frames.pop_back();
            animation_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:animation_remove");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!animation_dirty_);
        if (ui::button("Apply animation",
                       "Validate and apply the complete sequence in one Undo step.")) {
            const auto profile = tile_detail::tile_animation_json(animation_draft_);
            mutate("Change tile animation", [&](auto& source) {
                TileSetSource candidate{source};
                candidate.set_animation(tile.id, profile);
                source = std::move(candidate.document);
            });
        }
        FORGE_UI_PROBE("tile_set:animation_apply");
        if (ui::button("Discard animation edits", "Keep the applied animation sequence.")) {
            animation_draft_ = tile.animation ? *tile.animation : TileAnimation{};
            animation_dirty_ = false;
            error_.clear();
        }
        FORGE_UI_PROBE("tile_set:animation_discard");
        ImGui::EndDisabled();
        if (animation_dirty_)
            ImGui::TextWrapped("Unapplied animation edits: Apply or Discard before Save, Undo or "
                               "selecting another tile.");
        ui::help("Frames loop in their listed order. Use 1 to 64 frames with durations from "
                 "0.001 to 600 seconds. The Frame above is used when animation is off. "
                 "All cells share the runtime timeline. Animation changes appearance; the "
                 "tile's collision profile stays fixed.");
    }
    void collision_point_fields(std::vector<std::array<double, 2>>& points) {
        ImGui::TextWrapped("Local XY: X right, Y up, from 0 to 1. Enter 3 to 8 points "
                           "counterclockwise. Press Enter to finish each coordinate edit.");
        const float side = std::min(160.f * ui::interface_scale, ImGui::GetContentRegionAvail().x);
        const auto origin = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        const auto point = [&](const std::array<double, 2>& p) {
            return ImVec2{origin.x + float(p[0]) * side, origin.y + (1 - float(p[1])) * side};
        };
        draw->AddRect(origin, {origin.x + side, origin.y + side}, IM_COL32(100, 110, 120, 255));
        const auto draw_loop = [&](const auto& loop, ImU32 color) {
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto valid = [](const auto& p) {
                    return std::all_of(p.begin(), p.end(), [](double v) {
                        return std::isfinite(v) && v >= 0 && v <= 1;
                    });
                };
                if (!valid(loop[i]) || !valid(loop[(i + 1) % loop.size()]))
                    continue;
                const auto a = point(loop[i]);
                const auto b = point(loop[(i + 1) % loop.size()]);
                draw->AddLine(a, b, color, &loop == &points ? 3 : 1.5f);
                if (&loop != &points)
                    continue;
                draw->AddCircleFilled(a, 3, IM_COL32(245, 210, 90, 255));
                draw->AddText({a.x + 4, a.y + 3}, IM_COL32_WHITE, std::to_string(i + 1).c_str());
            }
        };
        if (collision_draft_.kind == TileCollisionKind::Contours) {
            for (const auto& region : collision_contours_draft_) {
                draw_loop(region.points, IM_COL32(90, 210, 245, 255));
                for (const auto& hole : region.holes)
                    draw_loop(hole, IM_COL32(240, 150, 90, 255));
            }
        } else {
            draw_loop(points, IM_COL32(90, 210, 245, 255));
        }
        ImGui::Dummy({side, side + 16 * ui::interface_scale});
        if (ImGui::BeginTable("Collision points", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Point", ImGuiTableColumnFlags_WidthFixed,
                                    40 * ui::interface_scale);
            ImGui::TableSetupColumn("X");
            ImGui::TableSetupColumn("Y");
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < points.size(); ++i) {
                ImGui::PushID(int(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u", unsigned(i + 1));
                for (unsigned axis = 0; axis < 2; ++axis) {
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    auto value = points[i][axis];
                    if (ImGui::InputDouble(axis ? "##Y" : "##X", &value, 0, 0, "%.4f",
                                           ImGuiInputTextFlags_EnterReturnsTrue)) {
                        points[i][axis] = value;
                        collision_dirty_ = true;
                    }
                    FORGE_UI_PROBE("tile_set:collision_point:" + std::to_string(i) +
                                   (axis ? ":y" : ":x"));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(points.size() >= tile_collision_vertex_limit);
        if (ui::button("Add point", "Append a point on the closing edge, then move it off that "
                                    "edge before Apply.")) {
            const auto a = points.back(), b = points.front();
            points.push_back({(a[0] + b[0]) / 2, (a[1] + b[1]) / 2});
            collision_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:collision_add");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(points.size() <= 3);
        if (ui::button("Remove last point", "Remove the last vertex from the unapplied polygon.")) {
            points.pop_back();
            collision_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:collision_remove");
        ImGui::EndDisabled();
    }
    void contour_fields() {
        ImGui::TextWrapped(
            "Up to 4 separate solid contours with up to 4 holes each. "
            "All loops use counterclockwise XY points. Holes must stay strictly "
            "inside their contour; boundaries must not touch. Apply validates the complete shape.");
        if (ImGui::BeginCombo("Contour", std::to_string(collision_contour_ + 1).c_str())) {
            for (std::size_t i = 0; i < collision_contours_draft_.size(); ++i) {
                if (ImGui::Selectable(std::to_string(i + 1).c_str(), collision_contour_ == i)) {
                    collision_contour_ = i;
                    collision_loop_ = 0;
                }
                FORGE_UI_PROBE("tile_set:contour:" + std::to_string(i));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_set:contour");
        ImGui::BeginDisabled(collision_contours_draft_.size() >= tile_collision_contour_limit);
        if (ui::button("Add contour",
                       "Append a solid island; edit its points to avoid other filled areas.")) {
            collision_contours_draft_.push_back(
                {{{.375, .375}, {.625, .375}, {.625, .625}, {.375, .625}}, {}});
            collision_contour_ = collision_contours_draft_.size() - 1;
            collision_loop_ = 0;
            collision_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:contour_add");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(collision_contours_draft_.size() <= 1);
        if (ui::button("Remove contour",
                       "Remove the selected contour and its holes from this draft.")) {
            collision_contours_draft_.erase(collision_contours_draft_.begin() + collision_contour_);
            collision_contour_ = collision_loop_ = 0;
            collision_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:contour_remove");
        ImGui::EndDisabled();
        auto& region = collision_contours_draft_.at(collision_contour_);
        const auto loop_label =
            collision_loop_ ? "Hole " + std::to_string(collision_loop_) : "Outer boundary";
        if (ImGui::BeginCombo("Loop", loop_label.c_str())) {
            for (std::size_t i = 0; i <= region.holes.size(); ++i) {
                const auto label = i ? "Hole " + std::to_string(i) : "Outer boundary";
                if (ImGui::Selectable(label.c_str(), collision_loop_ == i))
                    collision_loop_ = i;
                FORGE_UI_PROBE("tile_set:contour_loop:" + std::to_string(i));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_set:contour_loop");
        ImGui::BeginDisabled(region.holes.size() >= tile_collision_hole_limit);
        if (ui::button(
                "Add hole",
                "Append a central void; edit its points to stay inside the outer boundary.")) {
            region.holes.push_back({{.25, .25}, {.75, .25}, {.75, .75}, {.25, .75}});
            collision_loop_ = region.holes.size();
            collision_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:hole_add");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(collision_loop_ == 0);
        if (ui::button("Remove hole", "Remove the selected hole from this draft.")) {
            region.holes.erase(region.holes.begin() + collision_loop_ - 1);
            collision_loop_ = 0;
            collision_dirty_ = true;
        }
        FORGE_UI_PROBE("tile_set:hole_remove");
        ImGui::EndDisabled();
        collision_point_fields(collision_loop_ ? region.holes.at(collision_loop_ - 1)
                                               : region.points);
    }
    void collision_fields(const TileSetDefinition& tile) {
        if (collision_tile_ != tile.id || collision_revision_ != document_->revision()) {
            collision_draft_ = tile.collision;
            collision_contours_draft_ = tile.collision.contours
                                            ? tile.collision.contours->contours
                                            : std::vector<TileCollisionContour>{};
            collision_contour_ = collision_loop_ = 0;
            collision_tile_ = tile.id;
            collision_revision_ = document_->revision();
            collision_dirty_ = false;
        }
        ImGui::SeparatorText("Tile collision");
        const char* labels[]{"Full cell", "None", "Convex polygon", "Polygon (decomposed)",
                             "Contours / holes"};
        if (ImGui::BeginCombo("Shape", labels[unsigned(collision_draft_.kind)])) {
            for (unsigned i = 0; i < std::size(labels); ++i) {
                if (ImGui::Selectable(labels[i], unsigned(collision_draft_.kind) == i)) {
                    collision_draft_.kind = TileCollisionKind(i);
                    if (i == 4 && collision_contours_draft_.empty())
                        collision_contours_draft_.push_back({{{0, 0}, {1, 0}, {1, 1}, {0, 1}}, {}});
                    if ((i == 2 || i == 3) && collision_draft_.points.empty())
                        collision_draft_.points = {{0, 0}, {1, 0}, {1, 1}};
                    collision_dirty_ = true;
                }
                FORGE_UI_PROBE("tile_set:collision_kind:" + std::to_string(i));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_set:collision_kind");
        if (collision_draft_.kind == TileCollisionKind::Contours) {
            contour_fields();
        } else if (collision_draft_.kind == TileCollisionKind::Convex ||
                   collision_draft_.kind == TileCollisionKind::Polygon) {
            collision_point_fields(collision_draft_.points);
        }
        ImGui::BeginDisabled(!collision_dirty_);
        if (ui::button("Apply collision",
                       "Validate and apply the complete profile in one Undo step.")) {
            auto profile = collision_draft_;
            if (profile.kind != TileCollisionKind::Convex &&
                profile.kind != TileCollisionKind::Polygon)
                profile.points.clear();
            mutate("Change tile collision", [&](auto& source) {
                TileSetSource candidate{source};
                candidate.set_collision(tile.id, profile.kind == TileCollisionKind::Contours
                                                     ? tile_detail::tile_collision_contours_json(
                                                           collision_contours_draft_)
                                                     : tile_detail::tile_collision_json(profile));
                source = std::move(candidate.document);
            });
        }
        FORGE_UI_PROBE("tile_set:collision_apply");
        if (ui::button("Discard collision edits", "Keep the applied source profile.")) {
            collision_draft_ = tile.collision;
            collision_contours_draft_ = tile.collision.contours
                                            ? tile.collision.contours->contours
                                            : std::vector<TileCollisionContour>{};
            collision_contour_ = collision_loop_ = 0;
            collision_dirty_ = false;
            error_.clear();
        }
        FORGE_UI_PROBE("tile_set:collision_discard");
        ImGui::EndDisabled();
        if (collision_dirty_)
            ImGui::TextWrapped("Unapplied collision edits: Apply or Discard before Save, Undo or "
                               "selecting another tile.");
        ui::help(
            "Collision is used by Tile Map Collider 2D on the selected layer. Full cell merges "
            "with neighboring full cells; None is decorative. Cell flips transform polygons. "
            "Custom polygons support Static/Kinematic bodies and sensors, without one-way "
            "collision.");
    }
    void publish() {
        try {
            if (has_unapplied_edits())
                throw std::runtime_error(
                    "Apply or discard tile profile edits before Save / Publish.");
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_tile_set_publication(candidate, plan);
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
        if (close_ && document_ && !ImGui::IsPopupOpen("Unsaved Tile Set"))
            ImGui::OpenPopup("Unsaved Tile Set");
        if (ImGui::BeginPopupModal("Unsaved Tile Set", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save / Publish before closing? Discard closes the draft; already "
                               "saved files remain on disk.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Publish and close after success.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("tile_set:close_save");
            ImGui::SameLine();
            if (ui::button("Discard",
                           "Close unsaved changes and keep the last working publication.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("tile_set:close_discard");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Keep the document open.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("tile_set:close_cancel");
            ImGui::EndPopup();
        }
    }
};
} // namespace forge
