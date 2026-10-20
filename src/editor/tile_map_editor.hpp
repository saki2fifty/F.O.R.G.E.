#pragma once
#include "../texture_authoring.hpp"
#include "../tile_asset_authoring.hpp"
#include "../tile_asset_resources.hpp"
#include "../tile_map_cook.hpp"
#include "../tile_map_paint.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>

namespace forge {
class TileMapEditor {
  public:
    explicit TileMapEditor(std::filesystem::path worker) : worker_(std::move(worker)) {}
    std::function<std::optional<TileCellAddress>(std::shared_ptr<const TileMapData>,
                                                 std::shared_ptr<const TileSetData>,
                                                 std::shared_ptr<const AssetCatalog>, bool, bool)>
        draw_preview;
    std::function<void()> release_preview;
    bool close_cancelled = false;
    bool is_open() const { return document_ || open_error_; }
    bool pending() const { return job_ != 0; }
    bool stroke_active() const { return bool(stroke_); }
    bool dirty() const {
        return document_ && (document_->dirty() || needs_publish_ || pending() || stroke_);
    }
    bool can_undo() const { return document_ && !pending() && !stroke_ && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && !stroke_ && document_->can_redo(); }
    const TileMapDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() {
        cancel_stroke();
        save_ = true;
    }
    void request_close() {
        cancel_stroke();
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
    void catalog_changed(std::shared_ptr<const AssetCatalog> catalog) {
        if (catalog_ == catalog)
            return;
        catalog_ = std::move(catalog);
        requested_set_ = {};
    }
    void source_published(SceneDocument& project, AssetId id) {
        if (document_ && document_->source().asset() == id && !dirty())
            load(project, document_->locator());
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New Tile Map...", "Create a sparse map from a published Tile Set.")) {
            catalog_ =
                std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
            ImGui::OpenPopup("New Tile Map");
        }
        FORGE_UI_PROBE("tile_map:new");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New Tile Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("tile_map:new_path");
            if (catalog_)
                asset_ref_picker(*catalog_, new_set_, TileSetAsset::type, "Tile Set", false);
            ui::help(
                "Save / Publish a Tile Set first. Each map cell uses its local tile definitions.");
            ImGui::BeginDisabled(locked || dirty() || new_set_.is_null());
            if (ui::button("Create",
                           "Create an empty .tilemap.json source; existing files are protected.")) {
                try {
                    const auto set = new_set_.get<AssetId>();
                    auto created = TileMapDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_),
                        [=](AssetId id) { return TileMapSource{TileMapValue::create(id, set)}; });
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            FORGE_UI_PROBE("tile_map:create");
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
                    message = "Tile Map saved and published.";
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
        if (document_)
            update_definitions();
    }
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (!is_open())
            return;
        if (locked)
            cancel_stroke();
        if (save_ && document_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Tile Map###TileMap", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("tile_map", path_utf8(document_->locator()));
        if (stroke_ && (!expanded || ImGui::GetIO().AppFocusLost ||
                        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape)))
            cancel_stroke();
        if (expanded && document_) {
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending() || stroke_active());
            if (ui::button("Save / Publish",
                           "Save cells and cook; failures retain the last working map."))
                request_save();
            FORGE_UI_PROBE("tile_map:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo one edit or complete stroke."))
                undo();
            FORGE_UI_PROBE("tile_map:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Restore one map edit or stroke."))
                redo();
            FORGE_UI_PROBE("tile_map:redo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Map settings", "Inspect the Tile Set and cell dimensions."))
                select("map");
            FORGE_UI_PROBE("tile_map:settings");
            layers();
            palette();
            ImGui::BeginDisabled(document_->dirty() || needs_publish_ || !ui::editor_context ||
                                 ui::editor_context->selection.entity().empty());
            if (ui::button("Assign to selected Tile Map Renderer",
                           "Assign the published map in one Scene Undo step.")) {
                try {
                    const auto id = ui::editor_context->selection.entity();
                    const auto actor = scene.entity(id);
                    if (!actor || !actor.has<TileMapRenderer>())
                        throw std::runtime_error(
                            "Select an entity with Tile Map Renderer in Hierarchy.");
                    authoring_command(scene, "property.set",
                                      {{"entity", id},
                                       {"component", "forge.tile_map_renderer"},
                                       {"field", "map"},
                                       {"value", document_->source().asset()}});
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            FORGE_UI_PROBE("tile_map:assign");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ui::help("Paint or erase with the left mouse button. Release applies one Undo step; "
                     "Escape cancels. Middle mouse pans; wheel zooms.");
            // Loading diagnostics and gesture status must not move the grid
            // beneath a captured pointer. Long diagnostics scroll in this area.
            ImGui::BeginChild("Map status", {0, ImGui::GetTextLineHeightWithSpacing() * 3});
            if (!definition_error_.empty())
                ui::field_error(definition_error_);
            if (!error_.empty())
                ui::field_error(error_);
            if (stroke_)
                ImGui::TextUnformatted(
                    full_ ? "Stroke limit reached. Release, then begin another stroke."
                          : "Stroke preview: release to apply; Escape cancels.");
            else if (pending())
                ImGui::TextUnformatted("Publishing Tile Map...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source needs Save / Publish.");
            else
                ImGui::TextUnformatted("Ready"); // Keep the canvas fixed when a stroke starts.
            ImGui::EndChild();
            if (draw_preview) {
                try {
                    prepare_preview();
                    const bool can_paint = !locked && !pending() &&
                                           (tool_ != 1 || (definitions_current_ && brush_tile_));
                    const auto point =
                        draw_preview(cells_, definitions_, catalog_, stroke_active(), can_paint);
                    handle_pointer(point, locked || pending());
                } catch (const std::exception& e) {
                    cancel_stroke();
                    error_ = e.what();
                }
            }
        }
        draw_close(locked);
        ImGui::End();
    }
    void inspect(const std::string& key, bool locked) {
        if (!document_)
            return;
        ImGui::BeginDisabled(locked || pending() || stroke_active());
        if (key == "map")
            map_fields();
        else if (key == "layer")
            layer_fields();
        else if (key == "cell" && selected_cell_) {
            const auto at = *selected_cell_;
            const auto value = document_->source().document.cell(layer_, at);
            ImGui::Text("Cell: %d, %d", at.x, at.y);
            if (value.tile) {
                if (definitions_) {
                    const auto row =
                        std::find_if(definitions_->tiles().begin(), definitions_->tiles().end(),
                                     [&](const auto& tile) { return tile.id == value.tile; });
                    ImGui::TextWrapped("Tile: %s", row == definitions_->tiles().end()
                                                       ? "Missing definition"
                                                       : row->name.c_str());
                } else
                    ImGui::TextUnformatted("Tile Set unavailable");
                ImGui::Text("Orientation: %u", value.flags);
                if (ui::button("Erase cell", "Erase this cell with one Undo step."))
                    mutate("Erase tile cell", [&](auto& source) {
                        const std::array<TileMapCellEdit, 1> edit{{{layer_, at, {}}}};
                        source.document = source.document.paint(edit);
                    });
                FORGE_UI_PROBE("tile_map:erase_cell");
            } else
                ImGui::TextUnformatted("Empty cell");
        }
        ImGui::EndDisabled();
        if (!error_.empty())
            ui::field_error(error_);
    }

  private:
    std::filesystem::path worker_;
    std::unique_ptr<TileMapDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    ResourcePool<TileSetAsset> definition_pool_{{1, 64, 256, 16 * 1024 * 1024}};
    ResourceTicket definition_ticket_;
    AssetId requested_set_;
    std::shared_ptr<const TileSetData> definitions_;
    std::shared_ptr<const TileMapData> cells_, baseline_cells_;
    std::unique_ptr<TileMapPaintStroke> stroke_;
    std::uint64_t cells_revision_ = 0, layer_ = 1, brush_tile_ = 0;
    std::uint32_t flags_ = 0;
    int tool_ = 1; // Select, Paint, Erase.
    AssetJobId job_ = 0;
    std::optional<std::filesystem::path> next_source_;
    std::optional<TileCellAddress> selected_cell_;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false,
         definitions_current_ = false, full_ = false;
    Json new_set_ = nullptr;
    char path_[256] = "Assets/NewMap.tilemap.json";
    std::string error_, definition_error_;
    void select(const char* key) {
        if (ui::editor_context)
            ui::editor_context->selection.select_document_item("tile_map", key);
    }
    void cancel_stroke() {
        if (!stroke_)
            return;
        stroke_->cancel();
        stroke_.reset();
        cells_ = std::move(baseline_cells_);
        full_ = false;
    }
    void changed() {
        cells_revision_ = 0;
        error_.clear();
    }
    void prepare_preview() {
        if (cells_ && cells_revision_ == document_->revision())
            return;
        cells_ = std::make_shared<const TileMapData>(decode_cooked_tile_map(
            cook_tile_map(document_->source().document), document_->source().asset()));
        cells_revision_ = document_->revision();
    }
    void finish_close() {
        cancel_stroke();
        if (ui::editor_context && ui::editor_context->selection.document() == "tile_map")
            ui::editor_context->selection.clear();
        if (release_preview)
            release_preview();
        document_.reset();
        service_.reset();
        catalog_.reset();
        definitions_.reset();
        cells_.reset();
        definition_ticket_ = {};
        requested_set_ = {};
        selected_cell_.reset();
        cells_revision_ = brush_tile_ = 0;
        layer_ = 1;
        job_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = definitions_current_ = full_ = false;
        definition_error_.clear();
    }
    void load(SceneDocument& project, const std::filesystem::path& path) {
        auto document = std::make_unique<TileMapDocument>(project.writer_guard(), path);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), tile_asset_import_registry(worker_), desktop_texture_target());
        const auto found = catalog->records().find(document->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(path),
                                                    tile_map_source_byte_limit);
        const bool needs = found == catalog->records().end() ||
                           !found->second.metadata.contains("forge.import") ||
                           found->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(document);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        layer_ = document_->source().document.layers().front().id;
        changed();
        prepare_preview();
        update_definitions();
    }
    void update_definitions() {
        definition_pool_.pump();
        definition_pool_.collect();
        const auto set = document_->source().document.tile_set();
        if (requested_set_ != set) {
            cancel_stroke();
            definition_ticket_ = {};
            if (definitions_ && definitions_->asset() != set)
                definitions_.reset();
            definitions_current_ = false;
            definition_error_.clear();
            requested_set_ = set;
            try {
                definition_ticket_ =
                    request_tile_set(definition_pool_, document_->project(), catalog_, {set});
            } catch (const std::exception& e) {
                definition_error_ = e.what();
            }
        }
        if (!definition_ticket_ || definitions_current_)
            return;
        const auto lease = definition_pool_.acquire(definition_ticket_);
        if (!lease) {
            const auto state = definition_ticket_.inspect();
            if (resource_detail::terminal(state.state))
                definition_error_ = "Tile Set load failed: " + state.diagnostic;
            return;
        }
        using Retained = std::pair<ResourceLease<TileSetAsset>, TileSetData>;
        auto retained = std::make_shared<const Retained>(lease, lease.get());
        definitions_ = std::shared_ptr<const TileSetData>(retained, &retained->second);
        definitions_current_ = true;
        definition_error_.clear();
        if (std::none_of(definitions_->tiles().begin(), definitions_->tiles().end(),
                         [&](const auto& tile) { return tile.id == brush_tile_; }))
            brush_tile_ = definitions_->tiles().empty() ? 0 : definitions_->tiles().front().id;
    }
    template <class F> bool mutate(std::string label, F action) {
        try {
            auto next = document_->source();
            action(next);
            next.validate();
            if (!document_->retains_history(next))
                throw std::runtime_error(
                    "This edit cannot retain Undo and Redo. Erase in smaller strokes first.");
            document_->edit_source(document_->revision(), std::move(label),
                                   [&](auto& source) { source = std::move(next); });
            changed();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void handle_pointer(std::optional<TileCellAddress> point, bool locked) {
        if (locked || close_) {
            cancel_stroke();
            return;
        }
        if (!stroke_ && point && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (tool_ == 0) {
                selected_cell_ = *point;
                select("cell");
                return;
            }
            if (tool_ == 1 && (!definitions_current_ || !definitions_ || !brush_tile_))
                return;
            baseline_cells_ = cells_;
            if (tool_ == 2)
                stroke_ = std::make_unique<TileMapPaintStroke>(*document_, layer_);
            else
                stroke_ = std::make_unique<TileMapPaintStroke>(*document_, *definitions_, layer_,
                                                               TileMapCell{brush_tile_, flags_});
            full_ = false;
        }
        if (!stroke_)
            return;
        if (point && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            full_ = !stroke_->sample(*point);
            if (!stroke_->delta().empty()) {
                if (tool_ == 2)
                    cells_ = std::make_shared<const TileMapData>(
                        cells_->with_erased_cells(stroke_->delta()));
                else
                    cells_ = std::make_shared<const TileMapData>(
                        cells_->with_cells(stroke_->delta(), *definitions_));
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            stroke_->commit();
            stroke_.reset();
            baseline_cells_.reset();
            cells_revision_ = document_->revision();
            full_ = false;
            error_.clear();
        }
    }
    void layers() {
        const auto& rows = document_->source().document.layers();
        if (std::none_of(rows.begin(), rows.end(),
                         [&](const auto& layer) { return layer.id == layer_; }))
            layer_ = rows.front().id;
        const auto row = std::find_if(rows.begin(), rows.end(),
                                      [&](const auto& layer) { return layer.id == layer_; });
        if (ImGui::BeginCombo("Layer", row->name.c_str())) {
            for (const auto& layer : rows) {
                const auto label = layer.name + "###" + std::to_string(layer.id);
                if (ImGui::Selectable(label.c_str(), layer.id == layer_)) {
                    layer_ = layer.id;
                    selected_cell_.reset();
                    select("layer");
                }
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_map:layer");
        ImGui::SameLine();
        if (ui::button("Layer settings", "Inspect the active layer's name, visibility and order."))
            select("layer");
        ImGui::BeginDisabled(rows.size() == tile_map_layer_limit);
        if (ui::button("Add layer", "Add a separate ordered cell layer in one Undo step.")) {
            const auto id = document_->source().document.next_id();
            if (mutate("Add map layer", [&](auto& source) {
                    auto meta = source.document.metadata();
                    if (id == UINT64_MAX)
                        throw std::runtime_error("Map layer key sequence is exhausted");
                    meta["next_id"] = id + 1;
                    meta["layers"].push_back({{"id", id},
                                              {"name", "Layer " + std::to_string(id)},
                                              {"visible", true},
                                              {"order", 0}});
                    source.document = source.document.with_metadata(std::move(meta));
                })) {
                layer_ = id;
                selected_cell_.reset();
                select("layer");
            }
        }
        FORGE_UI_PROBE("tile_map:add_layer");
        ImGui::EndDisabled();
    }
    void palette() {
        if (ImGui::BeginCombo("Tool", tool_ == 0 ? "Select" : tool_ == 1 ? "Paint" : "Erase")) {
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(i == 0 ? "Select" : i == 1 ? "Paint" : "Erase", i == tool_))
                    tool_ = i;
                FORGE_UI_PROBE("tile_map:tool:" + std::to_string(i));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_map:tool");
        if (definitions_ && ImGui::BeginCombo("Tile", brush_name().c_str())) {
            for (const auto& tile : definitions_->tiles()) {
                const auto label = tile.name + "###" + std::to_string(tile.id);
                if (ImGui::Selectable(label.c_str(), tile.id == brush_tile_))
                    brush_tile_ = tile.id;
                FORGE_UI_PROBE("tile_map:tile:" + std::to_string(tile.id));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("tile_map:tile");
        static constexpr const char* names[]{"Default",
                                             "Mirror horizontal",
                                             "Mirror vertical",
                                             "Rotate 180 degrees",
                                             "Mirror diagonal",
                                             "Rotate 90 degrees",
                                             "Rotate 270 degrees",
                                             "Mirror opposite diagonal"};
        if (ImGui::BeginCombo("Orientation", names[flags_])) {
            for (unsigned i = 0; i < 8; ++i)
                if (ImGui::Selectable(names[i], i == flags_))
                    flags_ = i;
            ImGui::EndCombo();
        }
    }
    std::string brush_name() const {
        if (definitions_)
            for (const auto& tile : definitions_->tiles())
                if (tile.id == brush_tile_)
                    return tile.name;
        return "No tile selected";
    }
    void map_fields() {
        ImGui::SeparatorText("Map settings");
        Json set = document_->source().document.tile_set();
        ImGui::BeginDisabled(document_->source().document.occupied() != 0);
        if (asset_ref_picker(*catalog_, set, TileSetAsset::type, "Tile Set", false))
            mutate("Change empty map Tile Set", [&](auto& source) {
                auto meta = source.document.metadata();
                meta["tile_set"] = set;
                source.document = source.document.with_metadata(std::move(meta));
            });
        ImGui::EndDisabled();
        ui::help("Tile keys belong to their Set. Erase the map or create a new map before choosing "
                 "a different Set.");
        const auto size = document_->source().document.cell_size();
        for (unsigned axis = 0; axis < 2; ++axis) {
            auto value = size[axis];
            if (ImGui::InputDouble(axis ? "Cell height (m)" : "Cell width (m)", &value, 0, 0,
                                   "%.6g", ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Resize map cells", [&](auto& source) {
                    auto meta = source.document.metadata();
                    meta["cell_size"][axis] = value;
                    source.document = source.document.with_metadata(std::move(meta));
                });
            FORGE_UI_PROBE(axis ? "tile_map:cell_height" : "tile_map:cell_width");
        }
    }
    void layer_fields() {
        const auto& rows = document_->source().document.layers();
        const auto row = std::find_if(rows.begin(), rows.end(),
                                      [&](const auto& value) { return value.id == layer_; });
        if (row == rows.end())
            return;
        const auto value = *row; // The document may replace its metadata during any edit below.
        std::array<char, 129> name{};
        std::copy_n(value.name.begin(), std::min(value.name.size(), name.size() - 1), name.begin());
        const auto edit = [&](const char* field, const Json& value) {
            mutate("Edit map layer", [&](auto& source) {
                auto meta = source.document.metadata();
                for (auto& layer : meta["layers"])
                    if (layer["id"] == layer_)
                        layer[field] = value;
                source.document = source.document.with_metadata(std::move(meta));
            });
        };
        if (ImGui::InputText("Name", name.data(), name.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            edit("name", name.data());
        auto visible = value.visible;
        if (ImGui::Checkbox("Visible", &visible))
            edit("visible", visible);
        auto order = value.order;
        if (ImGui::InputScalar("Order", ImGuiDataType_S32, &order, nullptr, nullptr, "%d",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            edit("order", order);
        FORGE_UI_PROBE("tile_map:layer_order");
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_tile_map_publication(candidate, plan);
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
        if (close_ && document_ && !ImGui::IsPopupOpen("Unsaved Tile Map"))
            ImGui::OpenPopup("Unsaved Tile Map");
        if (ImGui::BeginPopupModal("Unsaved Tile Map", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save / Publish before closing? Discard closes the draft; already "
                               "saved files remain on disk.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Publish and close after success.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("tile_map:close_save");
            ImGui::SameLine();
            if (ui::button("Discard",
                           "Discard unsaved edits and keep the last working publication.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("tile_map:close_discard");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Keep this map open.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("tile_map:close_cancel");
            ImGui::EndPopup();
        }
    }
};
} // namespace forge
