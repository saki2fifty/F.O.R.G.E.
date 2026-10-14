#pragma once
#include "../terrain_authoring.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>
namespace forge {
struct TerrainPreviewSettings {
    TerrainTileId tile;
    double radius = 2;
    bool brushing = false;
};
class TerrainEditor {
  public:
    std::function<std::optional<MeshBrushSample>(const TerrainSource&, std::uint64_t,
                                                 std::shared_ptr<const AssetCatalog>,
                                                 TerrainPreviewSettings)>
        draw_preview;
    std::function<void()> release_preview;
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const {
        return document_ && (document_->dirty() || needs_publish_ || pending() || bool(stroke_));
    }
    bool stroke_active() const { return bool(stroke_); }
    bool can_undo() const { return document_ && !pending() && !stroke_ && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && !stroke_ && document_->can_redo(); }
    const TerrainDocument* document() const { return document_.get(); }
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
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& error) {
            error_ = error.what();
            open_error_ = true;
        }
    }
    void catalog_changed(std::shared_ptr<const AssetCatalog> value) { catalog_ = std::move(value); }
    void source_published(SceneDocument& project, AssetId id) {
        if (document_ && document_->source().asset() == id && !dirty())
            load(project, document_->locator());
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button(
                "New terrain...",
                "Create layered heightfield source; Save / Publish makes it a placeable Mesh."))
            ImGui::OpenPopup("New terrain");
        FORGE_UI_PROBE("terrain:new");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("terrain:new_path");
            ui::help(
                "Choose a new .terrain.json file in the project. Existing files are protected.");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button("Create", "Start with one flat 16m tile and a height layer.")) {
                try {
                    auto created = TerrainDocument::create(project.writer_guard(),
                                                           std::filesystem::u8path(path_));
                    const auto locator = created->locator();
                    created.reset();
                    load(project, locator);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            FORGE_UI_PROBE("terrain:create");
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
                    message = "Terrain saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            const auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (locked)
            cancel_stroke();
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Terrain###Terrain", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close unavailable terrain source."))
                        request_close();
                }
                ImGui::End();
            }
            return;
        }
        if (save_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Terrain###Terrain", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("terrain", path_utf8(document_->locator()));
        if (stroke_ &&
            (!expanded || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
             ImGui::IsKeyPressed(ImGuiKey_Escape)))
            cancel_stroke();
        if (expanded && document_) {
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending() || bool(stroke_));
            if (ui::button("Save / Publish", "Save layered source and cook; failures retain the "
                                             "last selected working Mesh."))
                request_save();
            FORGE_UI_PROBE("terrain:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo one source edit or complete brush stroke."))
                undo();
            FORGE_UI_PROBE("terrain:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Restore a source edit or complete stroke."))
                redo();
            FORGE_UI_PROBE("terrain:redo");
            ImGui::EndDisabled();
            draw_fields();
            ImGui::BeginDisabled(needs_publish_ || document_->dirty() || !ui::editor_context ||
                                 ui::editor_context->selection.entity().empty());
            if (ui::button("Assign to selected Mesh Renderer",
                           "Assign the published terrain Mesh in one Scene Undo step.")) {
                try {
                    const auto id = ui::editor_context->selection.entity();
                    const auto actor = scene.entity(id);
                    if (!actor || !actor.has<MeshRenderer>())
                        throw std::runtime_error(
                            "Select an object with Mesh Renderer in Hierarchy.");
                    authoring_command(scene, "property.set",
                                      {{"entity", id},
                                       {"component", "forge.mesh_renderer"},
                                       {"field", "mesh"},
                                       {"value", document_->source().asset()}});
                    error_.clear();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            FORGE_UI_PROBE("terrain:attach");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ui::help("Heightfield tiles share raw layer and mask borders. Raise amount is meters; "
                     "Flatten edits the selected layer's raw height. Mouse paints; middle mouse "
                     "orbits; wheel zooms. Release applies one Undo step; Escape cancels.");
            if (draw_preview) {
                try {
                    const auto sample = draw_preview(
                        stroke_ ? stroke_->preview : document_->source(), preview_revision_,
                        catalog_, {selected_tile_, radius_, tool_ != 0 && !locked && !pending()});
                    handle_pointer(sample, locked || pending());
                } catch (const std::exception& error) {
                    cancel_stroke();
                    error_ = error.what();
                }
            }
            if (stroke_)
                ImGui::TextUnformatted(
                    stroke_->samples.size() == 256
                        ? "Stroke limit reached. Release and begin another stroke."
                        : "Painting preview. Release to apply; Escape to cancel.");
            if (pending())
                ImGui::TextUnformatted("Publishing Terrain...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source needs Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        if (close_ && document_ && !ImGui::IsPopupOpen("Unsaved Terrain"))
            ImGui::OpenPopup("Unsaved Terrain");
        if (ImGui::BeginPopupModal("Unsaved Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save / Publish before closing? Discard closes the draft; already "
                               "saved files remain on disk.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Publish, then close after success.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("terrain:close_save");
            ImGui::SameLine();
            if (ui::button("Discard",
                           "Close unsaved edits and retain the last selected runtime Mesh.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("terrain:close_discard");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Keep this source document open.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("terrain:close_cancel");
            ImGui::EndPopup();
        }
        ImGui::End();
    }

  private:
    struct Stroke {
        std::vector<MeshBrushSample> samples;
        TerrainSource base, preview;
        TerrainLayerId layer;
        std::uint64_t revision;
        TerrainBrush tool;
        double radius, amount, target;
        MeshBrushSymmetry symmetry;
    };
    std::unique_ptr<TerrainDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::shared_ptr<const TerrainRaster> raster_;
    std::uint64_t raster_revision_ = 0;
    std::optional<std::filesystem::path> next_source_;
    std::optional<Stroke> stroke_;
    TerrainTileId selected_tile_;
    TerrainLayerId selected_layer_;
    AssetJobId job_ = 0;
    std::uint64_t preview_revision_ = 1;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false,
         mirror_x_ = false, mirror_z_ = false;
    int tool_ = 0;
    std::array<int, 2> tile_position_{1, 0};
    double radius_ = 2, raise_ = 1, opacity_ = .5, flatten_ = 0, mask_ = 0;
    char path_[256] = "Assets/NewTerrain.terrain.json", layer_name_[129] = "Detail";
    std::string error_;
    void changed() {
        ++preview_revision_;
        error_.clear();
    }
    void cancel_stroke() {
        if (stroke_) {
            stroke_.reset();
            ++preview_revision_;
        }
    }
    void finish_close() {
        cancel_stroke();
        if (release_preview)
            release_preview();
        document_.reset();
        raster_.reset();
        raster_revision_ = 0;
        service_.reset();
        catalog_.reset();
        job_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = false;
        selected_layer_ = {};
        selected_tile_ = {};
    }
    void load(SceneDocument& project, const std::filesystem::path& path) {
        auto document = std::make_unique<TerrainDocument>(project.writer_guard(), path);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), terrain_import_registry(), desktop_texture_target());
        const auto found = catalog->records().find(document->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(path),
                                                    terrain_source_byte_limit);
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
    template <class F> void mutate(std::string label, F action) {
        try {
            document_->edit(document_->revision(), std::move(label), [&](Json& draft) {
                TerrainSource source{draft};
                action(source);
                draft = std::move(source.document);
            });
            changed();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    std::shared_ptr<const TerrainRaster> source_raster() {
        if (!raster_ || raster_revision_ != document_->revision()) {
            auto next = std::make_shared<const TerrainRaster>(document_->source().raster());
            raster_ = std::move(next);
            raster_revision_ = document_->revision();
        }
        return raster_;
    }
    void draw_fields() {
        const auto tile_snapshot = source_raster();
        const auto& values = *tile_snapshot;
        if (std::none_of(values.tiles.begin(), values.tiles.end(),
                         [&](const auto& t) { return t.id == selected_tile_; }))
            selected_tile_ = values.tiles.front().id;
        if (std::none_of(values.layers.begin(), values.layers.end(),
                         [&](const auto& l) { return l.id == selected_layer_; }))
            selected_layer_ = values.layers.front().id;
        ImGui::SeparatorText("Tiles and resolution");
        auto side = values.side;
        if (ImGui::InputScalar("Samples / side", ImGuiDataType_U32, &side, nullptr, nullptr, "%u",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            mutate("Resample terrain", [=](auto& source) { source.resample(side); });
        FORGE_UI_PROBE("terrain:side");
        auto size = values.tile_size;
        if (ImGui::InputDouble("Tile size (m)", &size, 0, 0, "%.5g",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            mutate("Resize terrain tiles",
                   [=](auto& source) { source.document["tile_size"] = size; });
        FORGE_UI_PROBE("terrain:tile_size");
        std::string selected;
        for (const auto& tile : values.tiles)
            if (tile.id == selected_tile_)
                selected = "Tile " + std::to_string(tile.coordinate[0]) + ", " +
                           std::to_string(tile.coordinate[1]);
        if (ImGui::BeginCombo("Selected tile", selected.c_str())) {
            for (const auto& tile : values.tiles) {
                const auto label = "Tile " + std::to_string(tile.coordinate[0]) + ", " +
                                   std::to_string(tile.coordinate[1]) + "###" + tile.id.str();
                if (ImGui::Selectable(label.c_str(), tile.id == selected_tile_))
                    selected_tile_ = tile.id;
                FORGE_UI_PROBE("terrain:tile:" + tile.id.str());
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("terrain:tile");
        ImGui::InputInt2("New tile X / Z", tile_position_.data());
        FORGE_UI_PROBE("terrain:tile_position");
        ImGui::BeginDisabled(values.tiles.size() == terrain_tile_limit);
        if (ui::button("Add tile", "Add sparse tile; inherit every shared raw border/mask sample."))
            mutate("Add terrain tile",
                   [&](auto& source) { selected_tile_ = source.add_tile(tile_position_); });
        FORGE_UI_PROBE("terrain:add_tile");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(values.tiles.size() == 1);
        if (ui::button("Remove tile", "Remove selected tile; at least one remains."))
            mutate("Remove terrain tile",
                   [&](auto& source) { source.remove_tile(selected_tile_); });
        FORGE_UI_PROBE("terrain:remove_tile");
        ImGui::EndDisabled();
        // Re-read after a successful structural edit so no iterator refers to an
        // abandoned layer/tile and this frame shows the admitted current draft.
        const auto layer_snapshot = source_raster();
        const auto& layers = layer_snapshot->layers;
        if (std::none_of(layers.begin(), layers.end(),
                         [&](const auto& l) { return l.id == selected_layer_; }))
            selected_layer_ = layers.front().id;
        const auto at = std::find_if(layers.begin(), layers.end(),
                                     [&](const auto& l) { return l.id == selected_layer_; });
        ImGui::SeparatorText("Height layers");
        if (ImGui::BeginCombo("Selected layer", at->name.c_str())) {
            for (const auto& layer : layers) {
                const auto label = layer.name + "###" + layer.id.str();
                if (ImGui::Selectable(label.c_str(), layer.id == selected_layer_))
                    selected_layer_ = layer.id;
                FORGE_UI_PROBE("terrain:layer:" + layer.id.str());
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("terrain:layer");
        const auto current = std::find_if(layers.begin(), layers.end(),
                                          [&](const auto& l) { return l.id == selected_layer_; });
        bool enabled = current->enabled;
        float strength = current->strength;
        if (ImGui::Checkbox("Layer enabled", &enabled))
            mutate("Enable terrain layer", [=, this](auto& source) {
                source.set_layer(selected_layer_, enabled, strength);
            });
        FORGE_UI_PROBE("terrain:enabled");
        if (ImGui::InputFloat("Layer strength", &strength, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            mutate("Terrain layer strength", [=, this](auto& source) {
                source.set_layer(selected_layer_, enabled, strength);
            });
        FORGE_UI_PROBE("terrain:strength");
        ImGui::InputText("New layer name", layer_name_, sizeof(layer_name_));
        FORGE_UI_PROBE("terrain:layer_name");
        ImGui::BeginDisabled(layers.size() == terrain_layer_limit);
        if (ui::button("Add layer", "Add a zero-height layer with full mask."))
            mutate("Add terrain layer",
                   [&](auto& source) { selected_layer_ = source.add_layer(layer_name_); });
        FORGE_UI_PROBE("terrain:add_layer");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(layers.size() == 1);
        if (ui::button("Remove layer", "Remove selected layer and its samples; one layer remains."))
            mutate("Remove terrain layer",
                   [&](auto& source) { source.remove_layer(selected_layer_); });
        FORGE_UI_PROBE("terrain:remove_layer");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Brush");
        constexpr const char* tools[] = {"Select tile", "Raise / lower", "Flatten layer",
                                         "Smooth layer", "Paint layer mask"};
        if (ImGui::BeginCombo("Tool", tools[tool_])) {
            for (int i = 0; i < 5; ++i) {
                if (ImGui::Selectable(tools[i], tool_ == i))
                    tool_ = i;
                FORGE_UI_PROBE("terrain:tool:" + std::to_string(i));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("terrain:tool");
        ImGui::InputDouble("Radius (m)", &radius_, 0, 0, "%.4g");
        FORGE_UI_PROBE("terrain:radius");
        if (tool_ == 1) {
            ImGui::InputDouble("Raise / lower (m)", &raise_, 0, 0, "%.4g");
            FORGE_UI_PROBE("terrain:raise");
        } else if (tool_ > 1) {
            ImGui::InputDouble("Opacity", &opacity_, 0, 0, "%.4g");
            FORGE_UI_PROBE("terrain:opacity");
        }
        if (tool_ == 2) {
            ImGui::InputDouble("Layer height target (m)", &flatten_, 0, 0, "%.4g");
            FORGE_UI_PROBE("terrain:flatten");
        }
        if (tool_ == 4) {
            ImGui::InputDouble("Mask target", &mask_, 0, 0, "%.4g");
            FORGE_UI_PROBE("terrain:mask");
        }
        ImGui::Checkbox("Mirror X", &mirror_x_);
        FORGE_UI_PROBE("terrain:mirror_x");
        ImGui::SameLine();
        ImGui::Checkbox("Mirror Z", &mirror_z_);
        FORGE_UI_PROBE("terrain:mirror_z");
    }
    void handle_pointer(const std::optional<MeshBrushSample>& point, bool locked) {
        if (locked || close_) {
            cancel_stroke();
            return;
        }
        if (!stroke_ && point && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (tool_ == 0) {
                const auto values = document_->source().raster();
                for (const auto& tile : values.tiles) {
                    // Select the actual packed Mesh extent, including rounding
                    // at nonintegral tile boundaries, just as picking does.
                    const auto span = int(values.side - 1);
                    const auto coordinate = [&](int value) {
                        return double(float(double(value) * (values.tile_size / span)));
                    };
                    const double x = coordinate(tile.coordinate[0] * span),
                                 z = coordinate(tile.coordinate[1] * span),
                                 end_x = coordinate((tile.coordinate[0] + 1) * span),
                                 end_z = coordinate((tile.coordinate[1] + 1) * span);
                    if (point->point[0] >= x && point->point[0] <= end_x && point->point[2] >= z &&
                        point->point[2] <= end_z) {
                        selected_tile_ = tile.id;
                        break;
                    }
                }
            } else {
                const auto symmetry = (mirror_x_ ? MeshBrushSymmetry::X : MeshBrushSymmetry::None) |
                                      (mirror_z_ ? MeshBrushSymmetry::Z : MeshBrushSymmetry::None);
                stroke_ = Stroke{{},
                                 document_->source(),
                                 document_->source(),
                                 selected_layer_,
                                 document_->revision(),
                                 TerrainBrush(tool_ - 1),
                                 radius_,
                                 tool_ == 1 ? raise_ : opacity_,
                                 tool_ == 2   ? flatten_
                                 : tool_ == 4 ? mask_
                                              : 0,
                                 symmetry};
            }
        }
        if (!stroke_)
            return;
        if (document_->revision() != stroke_->revision) {
            cancel_stroke();
            return;
        }
        if (point && ImGui::IsMouseDown(ImGuiMouseButton_Left) && stroke_->samples.size() < 256) {
            auto& samples = stroke_->samples;
            const auto previous_count = samples.size();
            if (samples.empty())
                samples.push_back(*point);
            else {
                const auto from = samples.back();
                const auto distance =
                    std::hypot(point->point[0] - from.point[0], point->point[2] - from.point[2]);
                if (distance > 0) {
                    const auto spacing = std::max(.005, stroke_->radius * .25);
                    const auto steps = std::min<std::size_t>(
                        std::size_t(std::ceil(distance / spacing)), 256 - samples.size());
                    for (std::size_t i = 1; i <= steps; ++i) {
                        const auto fraction = double(i) / std::ceil(distance / spacing);
                        MeshBrushSample sample;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            sample.point[axis] =
                                std::lerp(from.point[axis], point->point[axis], fraction);
                        sample.pressure = std::lerp(from.pressure, point->pressure, fraction);
                        samples.push_back(sample);
                    }
                }
            }
            if (samples.size() != previous_count) {
                stroke_->preview = stroke_->base;
                stroke_->preview.brush(stroke_->layer, samples, stroke_->radius, stroke_->tool,
                                       stroke_->amount, stroke_->target, stroke_->symmetry);
                ++preview_revision_;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto stroke = std::move(*stroke_);
            stroke_.reset();
            if (!stroke.samples.empty()) {
                try {
                    document_->edit(stroke.revision, "Terrain brush stroke",
                                    [&](auto& draft) { draft = stroke.preview.document; });
                    changed();
                } catch (const std::exception& error) {
                    error_ = error.what();
                    ++preview_revision_;
                }
            }
        }
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_terrain_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            error_.clear();
        } catch (const std::exception& error) {
            needs_publish_ = true;
            close_ = false;
            error_ = error.what();
        }
    }
};
} // namespace forge
