#pragma once
#include "../asset_bytes.hpp"
#include "../editable_mesh_authoring.hpp"
#include "../editable_mesh_document.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace forge {
class EditableMeshEditor {
  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_); }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || job_); }
    bool can_undo() const { return document_ && !job_ && document_->can_undo(); }
    bool can_redo() const { return document_ && !job_ && document_->can_redo(); }
    const EditableMeshDocument* document() const { return document_.get(); }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        cancel_stroke();
        cancel_uv_drag();
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (can_undo()) {
            cancel_stroke();
            cancel_uv_drag();
            document_->undo();
            reconcile_selection();
            needs_publish_ = true;
        }
    }
    void redo() {
        if (can_redo()) {
            cancel_stroke();
            cancel_uv_drag();
            document_->redo();
            reconcile_selection();
            needs_publish_ = true;
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            pending_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
            ui::report_error("editable_mesh/open", error_);
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New editable Mesh...",
                       "Create a cube or smooth sphere for modeling, sculpting and UV editing."))
            ImGui::OpenPopup("New editable Mesh");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New editable Mesh", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            const char* shapes[] = {"Cube", "Sculpt sphere"};
            ImGui::Combo("Start shape", &start_shape_, shapes, 2);
            ui::help("Choose a new .mesh.json file inside your project.");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button(
                    "Create",
                    "Create the selected source; Save & Publish makes it usable in scenes.")) {
                try {
                    auto created = EditableMeshDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_), [this](AssetId id) {
                            return start_shape_ == 1 ? EditableMeshSource::create_sculpt_sphere(id)
                                                     : EditableMeshSource::create_cube(id);
                        });
                    load(project, created->locator());
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                    ui::report_error("editable_mesh/create", error_);
                }
            }
            ImGui::EndDisabled();
            ui::next_text_button("Cancel");
            if (ui::button("Cancel", "Close without creating a Mesh source."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            finish_close();
            service_.reset();
            published_.reset();
            pending_source_.reset();
        }
        if (service_)
            for (auto& outcome : service_->poll()) {
                if (outcome.job.id != job_)
                    continue;
                job_ = 0;
                if (outcome.published) {
                    published_ = std::make_shared<const AssetCatalog>(outcome.publication->catalog);
                    needs_publish_ = false;
                    error_.clear();
                    message = "Editable Mesh saved and published.";
                } else {
                    error_ = "Mesh source saved; publication failed: " + outcome.diagnostic;
                    ui::report_error("editable_mesh/publish", error_);
                }
            }
        if (!document_ && pending_source_) {
            const auto source = std::exchange(pending_source_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument& project, bool locked) {
        if (!document_)
            return;
        if (document_->project() != project.project()) {
            request_close();
            return;
        }
        bool visible = true;
        const auto title = std::string(dirty() ? "* Editable Mesh###Editable Mesh"
                                               : "Editable Mesh###Editable Mesh");
        const bool expanded = ImGui::Begin(title.c_str(), &visible);
        if (!visible)
            request_close();
        if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("editable_mesh", "Editable Mesh");
        if (expanded) {
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ImGui::TextDisabled("Shared Mesh asset; edits publish a new cooked revision.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save & Publish", "Save the authored source and replace the selected "
                                             "cooked Mesh after validation."))
                request_save();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo one accepted Mesh or UV edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo one accepted Mesh or UV edit."))
                redo();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing candidate Mesh...");
            if (needs_publish_ && !document_->dirty() && !pending())
                ImGui::TextUnformatted("Save & Publish to update Scene and exported games.");
            if (ImGui::BeginTabBar("Mesh workspaces")) {
                if (ImGui::BeginTabItem("Model")) {
                    cancel_uv_drag();
                    model_workspace(locked);
                    ImGui::EndTabItem();
                }
                const auto uv_tab = ImGui::BeginTabItem("UV");
                FORGE_UI_PROBE("mesh:uv_tab");
                if (uv_tab) {
                    uv_workspace(locked);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (save_ && document_ && !locked && !pending()) {
            save_ = false;
            try {
                document_->save();
                auto draft =
                    service_->prepare(document_->locator(), {}, document_->source().asset());
                job_ = service_->submit(
                    std::move(draft),
                    [](auto& c, const auto& p, const auto&) {
                        prepare_editable_mesh_publication(c, p);
                    },
                    [](const auto&, const auto&) {});
                needs_publish_ = true;
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                ui::report_error("editable_mesh/save", error_);
            }
        }
        if (close_ && dirty())
            ImGui::OpenPopup("Pending Mesh changes");
        if (ImGui::BeginPopupModal("Pending Mesh changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "Save and publish this Mesh, discard unsaved edits, or keep editing.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Save the Mesh source and publish its cooked revision."))
                request_save();
            ImGui::EndDisabled();
            ui::next_text_button("Discard");
            if (ui::button("Discard",
                           "Close the draft; already saved source and last good asset remain.")) {
                ImGui::CloseCurrentPopup();
                finish_close();
            }
            ui::next_text_button("Keep editing");
            if (ui::button("Keep editing", "Cancel close and retain this Mesh draft.")) {
                close_ = false;
                pending_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (close_ && !dirty())
            finish_close();
    }

  private:
    enum class SelectionMode { Vertex, Edge, Face };
    std::unique_ptr<EditableMeshDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> published_;
    std::optional<std::filesystem::path> pending_source_;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false;
    char path_[1024] = "Assets/NewMesh.mesh.json";
    int start_shape_ = 0;
    std::string error_;
    SelectionMode mode_ = SelectionMode::Face;
    std::set<std::uint32_t> vertices_;
    std::pair<std::uint32_t, std::uint32_t> edge_{};
    std::uint32_t face_ = 13;
    std::set<MeshUvCorner> uv_selection_;
    bool uv_initialized_ = false, uv_drag_ = false;
    std::uint64_t uv_revision_ = std::numeric_limits<std::uint64_t>::max(), uv_drag_revision_ = 0;
    std::vector<std::uint32_t> uv_topology_;
    std::vector<MeshUvCorner> uv_drag_selection_;
    struct UvDragPoint {
        MeshUvCorner corner;
        std::array<double, 2> original{}, value{};
    };
    std::vector<UvDragPoint> uv_drag_points_;
    std::array<double, 2> uv_drag_center_{};
    bool uv_drag_valid_ = false;

    std::array<double, 2> uv_center_{.5, .5}, uv_drag_start_{}, uv_drag_delta_{};
    double uv_zoom_ = 1;
    int uv_mode_ = 0; // Corner, face, connected island.
    float uv_padding_ = .02f;
    float translation_[3]{}, rotation_[3]{}, scale_[3]{1, 1, 1};
    float extrusion_ = 0.5f, inset_ = 0.1f;
    float uv_translation_[2]{}, uv_angle_ = 0, uv_scale_[2]{1, 1};
    bool preview_transform_ = false, preview_extrude_ = false, preview_inset_ = false,
         preview_uv_ = false;
    std::string model_preview_key_, uv_preview_key_, model_preview_error_, uv_preview_error_;
    std::optional<EditableMeshSource> model_preview_, uv_preview_;
    float yaw_ = 40, pitch_ = 25;
    int brush_mode_ = 0; // 0 = Select, 1 = Sculpt, 2 = Paint
    bool stroke_active_ = false;
    float sculpt_radius_ = .25f, sculpt_strength_ = .05f;
    float paint_color_[3]{.9f, .2f, .1f}, paint_opacity_ = .8f;
    std::vector<MeshBrushSample> stroke_;
    bool brush_mirror_[3]{};
    struct BrushGestureSettings {
        double radius = 0, strength = 0, opacity = 0;
        std::array<double, 4> color{};
        MeshBrushSymmetry symmetry = MeshBrushSymmetry::None;
        std::uint64_t revision = 0;
        int mode = 0;
    } stroke_settings_;
    MeshBrushSymmetry brush_symmetry() const {
        unsigned bits = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (brush_mirror_[axis])
                bits |= 1u << axis;
        return static_cast<MeshBrushSymmetry>(bits);
    }
    void cancel_stroke() {
        stroke_active_ = false;
        stroke_.clear();
        stroke_settings_ = {};
    }
    void begin_stroke(MeshBrushSample point) {
        stroke_active_ = true;
        stroke_.assign(1, point);
        stroke_settings_ = {
            sculpt_radius_,   sculpt_strength_,
            paint_opacity_,   {paint_color_[0], paint_color_[1], paint_color_[2], 1},
            brush_symmetry(), document_->revision(),
            brush_mode_};
    }

    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto next = std::make_unique<EditableMeshDocument>(project.writer_guard(), source);
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), editable_mesh_import_registry(), desktop_texture_target());
        const auto catalog = AssetCatalog::open_project(project.project());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    editable_mesh_source_byte_limit);
        const auto at = catalog.records().find(next->source().asset());
        const bool needs = at == catalog.records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(next);
        service_ = std::move(service);
        needs_publish_ = needs;
        face_ = 13;
        vertices_.clear();
        edge_ = {};
        reset_uv_scope();
        reconcile_selection();
        preview_transform_ = preview_extrude_ = preview_inset_ = preview_uv_ = false;
        cancel_stroke();
        error_.clear();
        model_preview_key_.clear();
        uv_preview_key_.clear();
        model_preview_.reset();
        uv_preview_.reset();
    }
    void finish_close() {
        reset_uv_scope();
        uv_topology_.clear();
        uv_revision_ = std::numeric_limits<std::uint64_t>::max();
        uv_center_ = {.5, .5};
        uv_zoom_ = 1;
        document_.reset();
        service_.reset();
        model_preview_.reset();
        uv_preview_.reset();
        cancel_stroke();
        model_preview_key_.clear();
        uv_preview_key_.clear();
        needs_publish_ = save_ = close_ = false;
        job_ = 0;
    }
    template <class F> void mutate(std::string label, F operation) {
        if (!document_ || pending())
            return;
        try {
            const auto previous = document_->revision();
            document_->edit(previous, std::move(label), [&](Json& draft) {
                EditableMeshSource source{draft};
                operation(source);
                draft = std::move(source.document);
            });
            if (document_->revision() != previous)
                needs_publish_ = true;
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
            ui::report_error("editable_mesh/edit", error_);
        }
    }
    void reconcile_selection() {
        if (!document_)
            return;
        std::set<std::uint32_t> existing;
        for (const auto& vertex : document_->source().document.at("vertices"))
            existing.insert(vertex.at("id").get<std::uint32_t>());
        std::erase_if(vertices_, [&](auto key) { return !existing.contains(key); });
        if (!edges().contains(edge_))
            edge_ = {};
        const auto& faces = document_->source().document.at("faces");
        const auto selected = std::find_if(faces.begin(), faces.end(), [&](const Json& face) {
            return face.at("id").get<std::uint32_t>() == face_;
        });
        if (selected == faces.end()) {
            face_ = faces.front().at("id").get<std::uint32_t>();
            reset_uv_scope();
        }
        sync_uv_selection();
    }
    std::vector<std::uint32_t> selected_vertices() const {
        if (mode_ == SelectionMode::Vertex)
            return {vertices_.begin(), vertices_.end()};
        if (mode_ == SelectionMode::Edge && edge_.first && edge_.second)
            return {edge_.first, edge_.second};
        if (mode_ == SelectionMode::Face)
            for (const auto& candidate : document_->source().document.at("faces"))
                if (candidate.at("id").get<std::uint32_t>() == face_) {
                    std::vector<std::uint32_t> result;
                    for (const auto& corner : candidate.at("corners"))
                        result.push_back(corner.at("vertex").get<std::uint32_t>());
                    return result;
                }
        return {};
    }
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges() const {
        std::set<std::pair<std::uint32_t, std::uint32_t>> result;
        for (const auto& face : document_->source().document.at("faces")) {
            const auto& corners = face.at("corners");
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const auto a = corners[i].at("vertex").get<std::uint32_t>();
                const auto b = corners[(i + 1) % corners.size()].at("vertex").get<std::uint32_t>();
                result.insert(std::minmax(a, b));
            }
        }
        return result;
    }
    void selection_list() {
        const char* labels[] = {"Vertex", "Edge", "Face"};
        int mode = static_cast<int>(mode_);
        if (ImGui::Combo("Select", &mode, labels, 3)) {
            mode_ = static_cast<SelectionMode>(mode);
            if (mode_ != SelectionMode::Face)
                preview_extrude_ = preview_inset_ = false;
        }
        if (ImGui::BeginChild("Mesh elements", {0, 125 * ui::interface_scale},
                              ImGuiChildFlags_Borders)) {
            if (mode_ == SelectionMode::Vertex) {
                for (const auto& vertex : document_->source().document.at("vertices")) {
                    const auto key = vertex.at("id").get<std::uint32_t>();
                    const auto label = "Vertex " + std::to_string(key);
                    if (ImGui::Selectable(label.c_str(), vertices_.contains(key))) {
                        if (!ImGui::GetIO().KeyCtrl)
                            vertices_.clear();
                        if (!vertices_.erase(key))
                            vertices_.insert(key);
                    }
                }
            } else if (mode_ == SelectionMode::Edge) {
                for (const auto key : edges()) {
                    const auto label =
                        "Edge " + std::to_string(key.first) + "–" + std::to_string(key.second);
                    if (ImGui::Selectable(label.c_str(), edge_ == key))
                        edge_ = key;
                }
            } else
                for (const auto& candidate : document_->source().document.at("faces")) {
                    const auto key = candidate.at("id").get<std::uint32_t>();
                    const auto label = "Face " + std::to_string(key);
                    if (ImGui::Selectable(label.c_str(), face_ == key) && face_ != key) {
                        face_ = key;
                        reset_uv_scope();
                    }
                }
        }
        ImGui::EndChild();
        ui::help("Select a face, edge or vertex. Ctrl-click adds vertices to the selection.");
    }
    void model_workspace(bool locked) {
        ImGui::SliderFloat("Orbit yaw", &yaw_, -180, 180);
        ImGui::SliderFloat("Orbit pitch", &pitch_, -75, 75);
        ImGui::BeginDisabled(locked || pending());
        ImGui::BeginDisabled(stroke_active_);
        const char* brush_modes[] = {"Select", "Sculpt", "Paint"};
        if (ImGui::Combo("Tool", &brush_mode_, brush_modes, 3)) {
            cancel_stroke();
            if (brush_mode_)
                preview_transform_ = preview_extrude_ = preview_inset_ = false;
        }
        FORGE_UI_PROBE("mesh:brush_tool");
        if (brush_mode_) {
            ImGui::SliderFloat("Brush radius", &sculpt_radius_, .02f, 1.f);
            if (brush_mode_ == 1)
                ImGui::SliderFloat("Brush strength", &sculpt_strength_, -.2f, .2f);
            else {
                ImGui::ColorEdit3("Paint color", paint_color_);
                ImGui::SliderFloat("Paint opacity", &paint_opacity_, 0.f, 1.f);
            }
            ImGui::Checkbox("Mirror X", &brush_mirror_[0]);
            FORGE_UI_PROBE("mesh:brush_mirror_x");
            ImGui::SameLine();
            ImGui::Checkbox("Mirror Y", &brush_mirror_[1]);
            FORGE_UI_PROBE("mesh:brush_mirror_y");
            ImGui::SameLine();
            ImGui::Checkbox("Mirror Z", &brush_mirror_[2]);
            FORGE_UI_PROBE("mesh:brush_mirror_z");
            ImGui::TextDisabled(
                "Mirror about Mesh local-origin planes. Overlapping stamps do not add strength.");
            ImGui::TextDisabled("Drag on the Mesh; release to apply one Undo step. Esc cancels.");
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            preview_transform_ = preview_extrude_ = preview_inset_ = false;
        const auto& source = document_->source();
        if (preview_extrude_ || preview_inset_ || preview_transform_) {
            const auto ids = selected_vertices();
            const auto key =
                Json::array(
                    {document_->revision(), preview_extrude_, preview_inset_,
                     static_cast<int>(mode_), face_, ids,
                     std::array<float, 3>{translation_[0], translation_[1], translation_[2]},
                     std::array<float, 3>{rotation_[0], rotation_[1], rotation_[2]},
                     std::array<float, 3>{scale_[0], scale_[1], scale_[2]}, extrusion_, inset_})
                    .dump();
            if (key != model_preview_key_) {
                model_preview_key_ = key;
                model_preview_ = source;
                model_preview_error_.clear();
                try {
                    if (preview_extrude_ && mode_ == SelectionMode::Face)
                        (void)model_preview_->extrude_face(face_, extrusion_);
                    else if (preview_inset_ && mode_ == SelectionMode::Face)
                        (void)model_preview_->inset_face(face_, inset_);
                    else
                        model_preview_->transform_vertices(
                            ids, {translation_[0], translation_[1], translation_[2]},
                            {rotation_[0], rotation_[1], rotation_[2]},
                            {scale_[0], scale_[1], scale_[2]});
                } catch (const std::exception& e) {
                    model_preview_error_ = e.what();
                    model_preview_.reset();
                }
            }
        } else {
            model_preview_key_.clear();
            model_preview_.reset();
            model_preview_error_.clear();
        }
        if (!model_preview_error_.empty())
            ImGui::TextWrapped("Preview unavailable: %s", model_preview_error_.c_str());
        draw_model_wireframe(model_preview_ ? *model_preview_ : source, locked);
        selection_list();
        ImGui::BeginDisabled(locked || pending());
        ImGui::SeparatorText("Shading");
        const bool smooth = document_->source().document.value("smooth", false);
        ImGui::TextUnformatted(smooth ? "Smooth normals" : "Flat normals");
        ImGui::BeginDisabled(stroke_active_ || preview_transform_ || preview_inset_ ||
                             preview_extrude_);
        ImGui::BeginDisabled(!smooth);
        if (ui::button("Shade flat", "Use each face normal without changing the Mesh geometry."))
            mutate("Shade flat", [](auto& source) { source.set_smooth_shading(false); });
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(smooth);
        if (ui::button("Shade smooth", "Average incident face normals at shared source vertices."))
            mutate("Shade smooth", [](auto& source) { source.set_smooth_shading(true); });
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ui::help("The wireframe shows geometry. Save & Publish to see shading in Scene, Play and "
                 "export. Smooth shading keeps the same silhouette.");
        ImGui::SeparatorText("Triangulate");
        const bool polygon_face =
            std::any_of(document_->source().document.at("faces").begin(),
                        document_->source().document.at("faces").end(), [&](const Json& face) {
                            return face.at("id") == face_ && face.at("corners").size() > 3;
                        });
        const bool polygon_mesh =
            std::any_of(document_->source().document.at("faces").begin(),
                        document_->source().document.at("faces").end(),
                        [](const Json& face) { return face.at("corners").size() > 3; });
        ImGui::BeginDisabled(stroke_active_ || preview_transform_ || preview_inset_ ||
                             preview_extrude_);
        ImGui::BeginDisabled(mode_ != SelectionMode::Face || !polygon_face);
        if (ui::button("Triangulate face",
                       "Split the selected polygon into its ordered triangles as one Undo step.")) {
            const auto revision = document_->revision();
            mutate("Triangulate face", [&](EditableMeshSource& source) {
                source.triangulate_faces(std::array{face_});
            });
            if (document_->revision() != revision) {
                reset_uv_scope();
                reconcile_selection();
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!polygon_mesh);
        if (ui::button("Triangulate Mesh",
                       "Split every polygon into its ordered triangles as one Undo step.")) {
            const auto revision = document_->revision();
            mutate("Triangulate Mesh", [](EditableMeshSource& source) {
                std::vector<std::uint32_t> faces;
                for (const auto& face : source.document.at("faces"))
                    faces.push_back(face.at("id").get<std::uint32_t>());
                source.triangulate_faces(faces);
            });
            if (document_->revision() != revision) {
                reset_uv_scope();
                reconcile_selection();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ui::help("Keeps positions, colors and corner UVs. Smooth normals are recalculated from the "
                 "new topology.");
        ImGui::SeparatorText("Transform selection");
        ImGui::InputFloat3("Move", translation_);
        ImGui::InputFloat3("Rotate (deg)", rotation_);
        ImGui::InputFloat3("Scale", scale_);
        if (ImGui::Checkbox("Preview transform", &preview_transform_) && preview_transform_)
            preview_extrude_ = preview_inset_ = false;
        if (ui::button("Apply transform",
                       "Move, rotate and scale selected elements as one Undo step.")) {
            const auto ids = selected_vertices();
            mutate("Mesh transform", [&, ids](EditableMeshSource& source) {
                source.transform_vertices(ids, {translation_[0], translation_[1], translation_[2]},
                                          {rotation_[0], rotation_[1], rotation_[2]},
                                          {scale_[0], scale_[1], scale_[2]});
            });
            preview_transform_ = false;
        }
        if (ui::button("Cancel transform",
                       "Discard pending transform values without editing the Mesh.")) {
            std::fill(std::begin(translation_), std::end(translation_), 0);
            std::fill(std::begin(rotation_), std::end(rotation_), 0);
            std::fill(std::begin(scale_), std::end(scale_), 1);
            preview_transform_ = false;
        }
        ImGui::SeparatorText("Inset face");
        ImGui::InputFloat("Inset distance", &inset_);
        FORGE_UI_PROBE("mesh:inset_distance");
        ImGui::BeginDisabled(mode_ != SelectionMode::Face);
        if (ImGui::Checkbox("Preview inset", &preview_inset_) && preview_inset_)
            preview_transform_ = preview_extrude_ = false;
        FORGE_UI_PROBE("mesh:preview_inset");
        if (ui::button("Inset selected face",
                       "Create a border and keep the inner face selected as one Undo step.")) {
            mutate("Inset face",
                   [&](EditableMeshSource& source) { (void)source.inset_face(face_, inset_); });
            preview_inset_ = false;
            reset_uv_scope();
        }
        ImGui::EndDisabled();
        if (ui::button("Cancel inset", "Discard the inset preview without editing the Mesh."))
            preview_inset_ = false;
        ui::help("Inset uses a positive edge distance. Extrude the inner face afterward to raise "
                 "or recess it.");
        ImGui::SeparatorText("Extrude face");
        ImGui::InputFloat("Extrude distance", &extrusion_);
        FORGE_UI_PROBE("mesh:extrude_distance");
        if (ImGui::Checkbox("Preview extrusion", &preview_extrude_) && preview_extrude_)
            preview_transform_ = preview_inset_ = false;
        FORGE_UI_PROBE("mesh:preview_extrude");
        ImGui::TextDisabled("Positive distance raises; negative distance recesses.");
        ImGui::BeginDisabled(mode_ != SelectionMode::Face);
        if (ui::button("Extrude selected face", "Add a cap and side walls as one Undo step.")) {
            std::uint32_t cap = 0;
            mutate("Extrude face", [&](EditableMeshSource& source) {
                cap = source.extrude_face(face_, extrusion_);
            });
            if (cap) {
                face_ = cap;
                reconcile_selection();
                reset_uv_scope();
            }
            preview_extrude_ = false;
        }
        ImGui::EndDisabled();
        if (ui::button("Cancel extrusion", "Discard the extrusion preview."))
            preview_extrude_ = false;
        ImGui::EndDisabled();
    }
    void cancel_uv_drag() {
        if (!uv_drag_)
            return;
        uv_drag_ = false;
        uv_drag_selection_.clear();
        uv_drag_points_.clear();
        uv_drag_center_ = {};
        uv_drag_valid_ = false;
        uv_drag_delta_ = {};
        uv_preview_.reset();
        uv_preview_key_.clear();
        uv_preview_error_.clear();
    }
    void prepare_uv_drag() {
        uv_drag_points_.clear();
        uv_drag_points_.reserve(uv_drag_selection_.size());
        for (const auto& face : document_->source().document.at("faces")) {
            const auto id = face.at("id").get<std::uint32_t>();
            const auto& corners = face.at("corners");
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const MeshUvCorner corner{id, i};
                if (uv_selection_.contains(corner)) {
                    const auto value = corners[i].at("uv").get<std::array<double, 2>>();
                    uv_drag_points_.push_back({corner, value, value});
                }
            }
        }
        std::sort(uv_drag_points_.begin(), uv_drag_points_.end(),
                  [](const auto& a, const auto& b) { return a.corner < b.corner; });
        uv_drag_center_ = {};
        for (const auto& point : uv_drag_points_) {
            uv_drag_center_[0] += point.original[0];
            uv_drag_center_[1] += point.original[1];
        }
        if (!uv_drag_points_.empty()) {
            uv_drag_center_[0] /= uv_drag_points_.size();
            uv_drag_center_[1] /= uv_drag_points_.size();
        }
        uv_drag_valid_ = false;
    }
    void evaluate_uv_drag(std::array<double, 2> delta) {
        uv_drag_valid_ = true;
        uv_preview_error_.clear();
        for (auto& point : uv_drag_points_) {
            // Match the source transform's canonical mean/translation arithmetic.
            // Derived scalar values never replace the authored document here.
            for (unsigned axis = 0; axis < 2; ++axis) {
                point.value[axis] = uv_drag_center_[axis] +
                                    (point.original[axis] - uv_drag_center_[axis]) + delta[axis];
                if (!std::isfinite(point.value[axis]) || std::abs(point.value[axis]) > 100000) {
                    uv_drag_valid_ = false;
                    uv_preview_error_ = "UV preview exceeds finite source coordinate bounds.";
                }
            }
        }
    }
    void reset_uv_scope() {
        cancel_uv_drag();
        uv_selection_.clear();
        uv_initialized_ = false;
    }
    void sync_uv_selection() {
        if (!document_ || uv_revision_ == document_->revision())
            return;
        std::vector<std::uint32_t> topology;
        for (const auto& face : document_->source().document.at("faces")) {
            topology.push_back(face.at("id").get<std::uint32_t>());
            topology.push_back(std::uint32_t(face.at("corners").size()));
            for (const auto& corner : face.at("corners"))
                topology.push_back(corner.at("vertex").get<std::uint32_t>());
        }
        if (topology != uv_topology_) {
            reset_uv_scope();
            uv_topology_ = std::move(topology);
        }
        uv_revision_ = document_->revision();
    }
    std::vector<MeshUvCorner> selected_uv_corners() const {
        return {uv_selection_.begin(), uv_selection_.end()};
    }
    void select_uv_face(std::uint32_t wanted, bool add = false, bool island = false) {
        cancel_uv_drag();
        if (!add)
            uv_selection_.clear();
        if (island) {
            const auto connected = document_->source().uv_island(wanted);
            uv_selection_.insert(connected.begin(), connected.end());
        } else
            for (const auto& face : document_->source().document.at("faces"))
                if (face.at("id").get<std::uint32_t>() == wanted)
                    for (std::size_t i = 0; i < face.at("corners").size(); ++i)
                        uv_selection_.insert({wanted, i});
        uv_initialized_ = true;
        face_ = wanted;
    }
    void frame_uv(bool tile) {
        if (tile) {
            uv_center_ = {.5, .5};
            uv_zoom_ = 1;
            return;
        }
        if (uv_selection_.empty())
            return;
        std::array<double, 2> minimum{100000, 100000}, maximum{-100000, -100000};
        for (const auto& face : document_->source().document.at("faces"))
            for (std::size_t i = 0; i < face.at("corners").size(); ++i)
                if (uv_selection_.contains({face.at("id").get<std::uint32_t>(), i}))
                    for (unsigned axis = 0; axis < 2; ++axis) {
                        const auto value = face.at("corners")[i].at("uv")[axis].get<double>();
                        minimum[axis] = std::min(minimum[axis], value);
                        maximum[axis] = std::max(maximum[axis], value);
                    }
        for (unsigned axis = 0; axis < 2; ++axis)
            uv_center_[axis] = (minimum[axis] + maximum[axis]) * .5;
        const auto extent = std::max(maximum[0] - minimum[0], maximum[1] - minimum[1]);
        uv_zoom_ = std::clamp(extent > 0 ? .9 / extent : 1., 1e-6, 1e6);
    }
    void uv_workspace(bool locked) {
        cancel_stroke();
        sync_uv_selection();
        if (!uv_initialized_)
            select_uv_face(face_);
        if (locked || pending() || (uv_drag_ && uv_drag_revision_ != document_->revision()))
            cancel_uv_drag();
        const auto& source = document_->source();
        ImGui::Text("Face %u | %zu selected UV corners", face_, uv_selection_.size());
        ImGui::Combo("Pick UV", &uv_mode_, "Corner\0Face\0Island\0");
        FORGE_UI_PROBE("mesh:uv_mode");
        if (ui::button("Select face", "Select every UV corner of the active Model face."))
            select_uv_face(face_);
        ImGui::SameLine();
        if (ui::button("Select island",
                       "Select connected UV faces without crossing authored seams."))
            select_uv_face(face_, false, true);
        ImGui::SameLine();
        if (ui::button("Frame selection", "Center and fit the selected UV region in this view."))
            frame_uv(false);
        ImGui::SameLine();
        if (ui::button("Frame tile", "Restore a view of the [0,1] texture tile."))
            frame_uv(true);
        if (ImGui::BeginChild("UV corners", {0, 100 * ui::interface_scale},
                              ImGuiChildFlags_Borders))
            for (const auto& face : source.document.at("faces"))
                if (face.at("id").get<std::uint32_t>() == face_)
                    for (std::size_t i = 0; i < face.at("corners").size(); ++i) {
                        const auto& point = face.at("corners")[i].at("uv");
                        const auto label = "Corner " + std::to_string(i + 1) + "  (" +
                                           std::to_string(point[0].get<double>()) + ", " +
                                           std::to_string(point[1].get<double>()) + ")";
                        const MeshUvCorner corner{face_, i};
                        const auto clicked =
                            ImGui::Selectable(label.c_str(), uv_selection_.contains(corner));
                        FORGE_UI_PROBE("mesh:uv_corner:" + std::to_string(face_) + ":" +
                                       std::to_string(i));
                        if (clicked) {
                            cancel_uv_drag();
                            if (!ImGui::GetIO().KeyCtrl)
                                uv_selection_.clear();
                            if (!uv_selection_.erase(corner))
                                uv_selection_.insert(corner);
                            uv_initialized_ = true;
                        }
                    }
        ImGui::EndChild();
        if (preview_uv_ && !uv_drag_) {
            const auto ids = selected_uv_corners();
            auto selection = Json::array();
            for (const auto id : ids)
                selection.push_back({id.face, id.corner});
            const auto key =
                Json::array({document_->revision(), selection,
                             std::array<float, 2>{uv_translation_[0], uv_translation_[1]},
                             uv_angle_, std::array<float, 2>{uv_scale_[0], uv_scale_[1]}})
                    .dump();
            if (key != uv_preview_key_) {
                uv_preview_key_ = key;
                uv_preview_ = source;
                uv_preview_error_.clear();
                try {
                    uv_preview_->transform_uv(ids, {uv_translation_[0], uv_translation_[1]},
                                              uv_angle_ * std::numbers::pi / 180.,
                                              {uv_scale_[0], uv_scale_[1]});
                } catch (const std::exception& e) {
                    uv_preview_error_ = e.what();
                    uv_preview_.reset();
                }
            }
        } else if (!uv_drag_) {
            uv_preview_key_.clear();
            uv_preview_.reset();
            uv_preview_error_.clear();
        }
        draw_uv_wireframe(uv_preview_ ? *uv_preview_ : source, locked || pending());
        if (!uv_preview_error_.empty())
            ImGui::TextWrapped("UV preview unavailable: %s", uv_preview_error_.c_str());
        ImGui::TextDisabled(
            "Wheel: zoom | Middle drag: pan | Ctrl-click: toggle | Drag: move | Escape: cancel");
        if (locked || pending())
            ImGui::TextUnformatted(
                "READ ONLY — UV edits are unavailable during Play or publication.");
        else if (preview_uv_)
            ImGui::TextUnformatted("NUMERIC PREVIEW — Apply or Cancel before dragging.");
        else if (uv_drag_)
            ImGui::TextUnformatted("DRAG PREVIEW — release to accept; Escape restores the source.");
        ImGui::BeginDisabled(locked || pending() || uv_drag_ || uv_selection_.empty());
        ImGui::InputFloat2("Move UV", uv_translation_);
        FORGE_UI_PROBE("mesh:uv_move");
        ImGui::InputFloat("Rotate UV (deg)", &uv_angle_);
        ImGui::InputFloat2("Scale UV", uv_scale_);
        FORGE_UI_PROBE("mesh:uv_scale");
        ImGui::Checkbox("Preview UV", &preview_uv_);
        FORGE_UI_PROBE("mesh:uv_preview");
        if (ui::button("Apply UV transform",
                       "Transform selected corners around one shared pivot as one Undo step.")) {
            const auto ids = selected_uv_corners();
            mutate("Transform UV", [&, ids](EditableMeshSource& mesh) {
                mesh.transform_uv(ids, {uv_translation_[0], uv_translation_[1]},
                                  uv_angle_ * std::numbers::pi / 180.,
                                  {uv_scale_[0], uv_scale_[1]});
            });
            preview_uv_ = false;
        }
        ImGui::BeginDisabled(preview_uv_);
        if (ui::button("Mirror U",
                       "Reflect selected UVs horizontally around their common center.")) {
            const auto ids = selected_uv_corners();
            mutate("Mirror UV U", [&](auto& mesh) { mesh.transform_uv(ids, {}, 0, {-1, 1}); });
        }
        ImGui::SameLine();
        if (ui::button("Mirror V", "Reflect selected UVs vertically around their common center.")) {
            const auto ids = selected_uv_corners();
            mutate("Mirror UV V", [&](auto& mesh) { mesh.transform_uv(ids, {}, 0, {1, -1}); });
        }
        ImGui::SliderFloat("Tile padding", &uv_padding_, 0, .49f, "%.3f");
        if (ui::button("Fit selected UVs",
                       "Fit this selection into one tile, keeping its proportions and seams.")) {
            const auto ids = selected_uv_corners();
            mutate("Fit UV selection", [&](auto& mesh) { mesh.fit_uv(ids, uv_padding_); });
        }
        if (ui::button("Planar project face",
                       "Project only the active Model face along its dominant normal axis.")) {
            mutate("Project face UV", [&](auto& mesh) { mesh.project_face_uv(face_); });
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (ui::button("Cancel UV preview",
                       "Discard the drag or numeric preview without editing the Mesh.")) {
            cancel_uv_drag();
            uv_translation_[0] = uv_translation_[1] = uv_angle_ = 0;
            uv_scale_[0] = uv_scale_[1] = 1;
            preview_uv_ = false;
        }
    }
    void draw_model_wireframe(const EditableMeshSource& source, bool locked) {
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(180.f, available.x), 240.f * ui::interface_scale};
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Mesh preview", size);
        FORGE_UI_PROBE("mesh:model_canvas");
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(14, 34, 46, 255));
        const double yaw = yaw_ * 3.14159265358979323846 / 180.0;
        const double pitch = pitch_ * 3.14159265358979323846 / 180.0;
        std::map<std::uint32_t, ImVec2> projected;
        std::map<std::uint32_t, double> depth_by_vertex;
        std::map<std::uint32_t, std::array<double, 3>> local_vertices;
        float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
        for (const auto& vertex : source.document.at("vertices")) {
            const auto& p = vertex.at("position");
            const double x = p[0].get<double>(), y = p[1].get<double>(), z = p[2].get<double>();
            const auto horizontal = std::cos(yaw) * x - std::sin(yaw) * z;
            const auto depth = std::sin(yaw) * x + std::cos(yaw) * z;
            const auto vertical = std::cos(pitch) * y - std::sin(pitch) * depth;
            const auto key = vertex.at("id").get<std::uint32_t>();
            projected[key] = {static_cast<float>(horizontal), static_cast<float>(-vertical)};
            depth_by_vertex[key] = std::cos(pitch) * depth + std::sin(pitch) * y;
            local_vertices[key] = {x, y, z};
            min_x = std::min(min_x, projected[key].x);
            min_y = std::min(min_y, projected[key].y);
            max_x = std::max(max_x, projected[key].x);
            max_y = std::max(max_y, projected[key].y);
        }
        const auto fit = std::min((size.x - 40) / std::max(0.01f, max_x - min_x),
                                  (size.y - 40) / std::max(0.01f, max_y - min_y));
        for (auto& [key, point] : projected) {
            (void)key;
            point.x = origin.x + size.x * 0.5f + (point.x - (min_x + max_x) * 0.5f) * fit;
            point.y = origin.y + size.y * 0.5f + (point.y - (min_y + max_y) * 0.5f) * fit;
        }
        if (brush_mode_) {
            const auto mouse = ImGui::GetMousePos();
            if (ImGui::IsItemHovered()) {
                draw->AddCircle(mouse, std::max(3.f, sculpt_radius_ * fit),
                                brush_mode_ == 1 ? IM_COL32(111, 231, 198, 235)
                                                 : IM_COL32(255, 170, 95, 235),
                                0, 1.5f);
            }
            const auto hit = [&]() -> std::optional<MeshBrushSample> {
                float closest = std::max(144.f, sculpt_radius_ * sculpt_radius_ * fit * fit);
                double front_depth = -std::numeric_limits<double>::infinity();
                std::optional<MeshBrushSample> result;
                for (const auto& [key, point] : projected) {
                    const auto dx = point.x - mouse.x, dy = point.y - mouse.y;
                    const auto distance = dx * dx + dy * dy;
                    if (distance < closest - 4 || (std::abs(distance - closest) <= 4 &&
                                                   depth_by_vertex.at(key) > front_depth)) {
                        closest = distance;
                        front_depth = depth_by_vertex.at(key);
                        result = MeshBrushSample{local_vertices.at(key), 1};
                    }
                }
                return result;
            };
            if (ImGui::IsItemHovered()) {
                if (const auto hover = hit();
                    hover && brush_symmetry() != MeshBrushSymmetry::None) {
                    const auto axes = static_cast<unsigned>(brush_symmetry());
                    std::vector<std::array<double, 3>> shown{hover->point};
                    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
                    for (unsigned reflection = 1; reflection < 8; ++reflection) {
                        if (reflection & ~axes)
                            continue;
                        auto point = hover->point;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            if (reflection & (1u << axis))
                                point[axis] = -point[axis];
                        if (std::find(shown.begin(), shown.end(), point) != shown.end())
                            continue;
                        shown.push_back(point);
                        const auto horizontal = std::cos(yaw) * point[0] - std::sin(yaw) * point[2];
                        const auto depth = std::sin(yaw) * point[0] + std::cos(yaw) * point[2];
                        const auto vertical = std::cos(pitch) * point[1] - std::sin(pitch) * depth;
                        const ImVec2 stamp{origin.x + size.x * .5f +
                                               float(horizontal - (min_x + max_x) * .5f) * fit,
                                           origin.y + size.y * .5f +
                                               float(-vertical - (min_y + max_y) * .5f) * fit};
                        draw->AddCircle(stamp, std::max(3.f, sculpt_radius_ * fit),
                                        brush_mode_ == 1 ? IM_COL32(111, 231, 198, 150)
                                                         : IM_COL32(245, 167, 109, 150),
                                        0, 1.5f);
                    }
                    draw->PopClipRect();
                }
            }
            if (stroke_active_ &&
                (locked || pending() || stroke_settings_.revision != document_->revision() ||
                 stroke_settings_.mode != brush_mode_ || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                 !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
                cancel_stroke();
            }
            if (!stroke_active_ && !locked && !pending() && ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (auto point = hit()) {
                    begin_stroke(*point);
                }
            } else if (stroke_active_ &&
                       (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) &&
                       ImGui::IsItemHovered()) {
                if (auto point = hit(); point && stroke_.size() < 256) {
                    const auto from = stroke_.back().point;
                    double distance_sq = 0;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        distance_sq += std::pow(point->point[axis] - from[axis], 2);
                    const auto distance = std::sqrt(distance_sq);
                    const auto spacing = std::max(.005, stroke_settings_.radius * .25);
                    if (distance >= spacing) {
                        const auto count = std::min<std::size_t>(
                            256 - stroke_.size(), std::size_t(std::ceil(distance / spacing)));
                        for (std::size_t step = 1; step <= count; ++step) {
                            auto position = from;
                            const auto fraction = double(step) / count;
                            for (unsigned axis = 0; axis < 3; ++axis)
                                position[axis] += (point->point[axis] - from[axis]) * fraction;
                            stroke_.push_back({position, 1});
                        }
                    }
                }
            }
            if (stroke_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                auto samples = std::exchange(stroke_, {});
                const auto settings = stroke_settings_;
                cancel_stroke();
                if (settings.mode == 1)
                    mutate("Sculpt stroke", [samples = std::move(samples),
                                             settings](EditableMeshSource& mesh) {
                        mesh.sculpt(samples, settings.radius, settings.strength, settings.symmetry);
                    });
                else
                    mutate("Paint stroke",
                           [samples = std::move(samples), settings](EditableMeshSource& mesh) {
                               mesh.paint_color(samples, settings.radius, settings.color,
                                                settings.opacity, settings.symmetry);
                           });
            }
        }
        // Topology previews introduce temporary element IDs. Select them only after acceptance.
        if (!brush_mode_ && !preview_extrude_ && !preview_inset_ && ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto mouse = ImGui::GetMousePos();
            auto distance_sq = [&](ImVec2 point) {
                const auto dx = point.x - mouse.x, dy = point.y - mouse.y;
                return dx * dx + dy * dy;
            };
            if (mode_ == SelectionMode::Vertex) {
                float nearest = 100.f;
                std::uint32_t hit = 0;
                for (const auto& [key, point] : projected)
                    if (distance_sq(point) < nearest) {
                        nearest = distance_sq(point);
                        hit = key;
                    }
                if (hit) {
                    if (!ImGui::GetIO().KeyCtrl)
                        vertices_.clear();
                    if (!vertices_.erase(hit))
                        vertices_.insert(hit);
                }
            } else if (mode_ == SelectionMode::Edge) {
                float nearest = 64.f;
                for (const auto key : edges()) {
                    const auto a = projected.at(key.first), b = projected.at(key.second);
                    const auto dx = b.x - a.x, dy = b.y - a.y;
                    const auto length_sq = dx * dx + dy * dy;
                    const auto t =
                        length_sq > 0
                            ? std::clamp(((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / length_sq,
                                         0.f, 1.f)
                            : 0.f;
                    const auto hit = ImVec2{a.x + t * dx, a.y + t * dy};
                    if (distance_sq(hit) < nearest) {
                        nearest = distance_sq(hit);
                        edge_ = key;
                    }
                }
            } else {
                double nearest_depth = -std::numeric_limits<double>::infinity();
                for (const auto& polygon : source.document.at("faces")) {
                    const auto& corners = polygon.at("corners");
                    bool inside = false;
                    double depth = 0;
                    for (std::size_t i = 0, j = corners.size() - 1; i < corners.size(); j = i++) {
                        const auto vertex = corners[i].at("vertex").get<std::uint32_t>();
                        const auto a = projected.at(vertex);
                        const auto b = projected.at(corners[j].at("vertex").get<std::uint32_t>());
                        depth += depth_by_vertex.at(vertex);
                        if ((a.y > mouse.y) != (b.y > mouse.y) &&
                            mouse.x < (b.x - a.x) * (mouse.y - a.y) / (b.y - a.y) + a.x)
                            inside = !inside;
                    }
                    depth /= corners.size();
                    if (inside && depth > nearest_depth) {
                        nearest_depth = depth;
                        const auto key = polygon.at("id").get<std::uint32_t>();
                        if (face_ != key) {
                            face_ = key;
                            reset_uv_scope();
                        }
                    }
                }
            }
        }
        for (const auto& polygon : source.document.at("faces")) {
            std::vector<ImVec2> loop;
            for (const auto& corner : polygon.at("corners"))
                loop.push_back(projected.at(corner.at("vertex").get<std::uint32_t>()));
            const bool selected =
                polygon.at("id").get<std::uint32_t>() == face_ && mode_ == SelectionMode::Face;
            draw->AddConvexPolyFilled(loop.data(), static_cast<int>(loop.size()),
                                      selected ? IM_COL32(176, 132, 42, 105)
                                               : IM_COL32(59, 117, 145, 34));
            draw->AddPolyline(loop.data(), static_cast<int>(loop.size()),
                              selected ? IM_COL32(247, 193, 76, 255) : IM_COL32(125, 184, 206, 185),
                              ImDrawFlags_Closed, selected ? 2.5f : 1.2f);
        }
        if (mode_ == SelectionMode::Vertex)
            for (const auto& [key, point] : projected)
                draw->AddCircleFilled(point, vertices_.contains(key) ? 5.f : 3.f,
                                      vertices_.contains(key) ? IM_COL32(255, 198, 72, 255)
                                                              : IM_COL32(164, 215, 228, 255));
        if (mode_ == SelectionMode::Edge && edge_.first && edge_.second &&
            projected.contains(edge_.first) && projected.contains(edge_.second))
            draw->AddLine(projected.at(edge_.first), projected.at(edge_.second),
                          IM_COL32(255, 198, 72, 255), 3.0f);
        draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(181, 216, 224, 255),
                      "Mesh source preview (orbit with sliders)");
    }
    void draw_uv_wireframe(const EditableMeshSource& source, bool locked) {
        const ImVec2 size{std::max(100.f, ImGui::GetContentRegionAvail().x),
                          230.f * ui::interface_scale};
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("UV canvas", size,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        FORGE_UI_PROBE("mesh:uv_canvas");
        const bool hovered = ImGui::IsItemHovered();
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        const auto mouse = ImGui::GetMousePos();
        const double base =
            std::max(16., double(std::min(size.x, size.y) - 36 * ui::interface_scale));
        const std::array<double, 2> middle{origin.x + size.x * .5, origin.y + size.y * .5};
        auto to_uv = [&](ImVec2 point) {
            return std::array<double, 2>{uv_center_[0] + (point.x - middle[0]) / (base * uv_zoom_),
                                         uv_center_[1] + (point.y - middle[1]) / (base * uv_zoom_)};
        };
        if (hovered && !uv_drag_) {
            if (ImGui::GetIO().MouseWheel != 0) {
                const auto anchor = to_uv(mouse);
                uv_zoom_ =
                    std::clamp(uv_zoom_ * std::pow(1.2, ImGui::GetIO().MouseWheel), 1e-6, 1e6);
                uv_center_ = {anchor[0] - (mouse.x - middle[0]) / (base * uv_zoom_),
                              anchor[1] - (mouse.y - middle[1]) / (base * uv_zoom_)};
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                uv_center_[0] -= ImGui::GetIO().MouseDelta.x / (base * uv_zoom_);
                uv_center_[1] -= ImGui::GetIO().MouseDelta.y / (base * uv_zoom_);
            }
            for (auto& value : uv_center_)
                value = std::clamp(value, -200000., 200000.);
        }
        auto screen = [&](std::array<double, 2> uv) {
            return std::array<double, 2>{middle[0] + (uv[0] - uv_center_[0]) * base * uv_zoom_,
                                         middle[1] + (uv[1] - uv_center_[1]) * base * uv_zoom_};
        };
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(20, 30, 41, 255));
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        // Clip original double-precision segments before conversion to draw floats.
        auto line = [&](std::array<double, 2> a, std::array<double, 2> b, ImU32 color,
                        float width) {
            const auto dx = b[0] - a[0], dy = b[1] - a[1];
            double low = 0, high = 1;
            const std::array<double, 4> p{-dx, dx, -dy, dy},
                q{a[0] - origin.x, origin.x + size.x - a[0], a[1] - origin.y,
                  origin.y + size.y - a[1]};
            for (unsigned i = 0; i < 4; ++i) {
                if (p[i] == 0) {
                    if (q[i] < 0)
                        return;
                } else {
                    const auto t = q[i] / p[i];
                    if (p[i] < 0)
                        low = std::max(low, t);
                    else
                        high = std::min(high, t);
                    if (low > high)
                        return;
                }
            }
            draw->AddLine({float(a[0] + low * dx), float(a[1] + low * dy)},
                          {float(a[0] + high * dx), float(a[1] + high * dy)}, color, width);
        };
        const auto tile0 = screen({0, 0}), tile1 = screen({1, 1});
        line(tile0, {tile1[0], tile0[1]}, IM_COL32(97, 128, 145, 255), 1);
        line({tile1[0], tile0[1]}, tile1, IM_COL32(97, 128, 145, 255), 1);
        line(tile1, {tile0[0], tile1[1]}, IM_COL32(97, 128, 145, 255), 1);
        line({tile0[0], tile1[1]}, tile0, IM_COL32(97, 128, 145, 255), 1);
        std::optional<MeshUvCorner> hit;
        double nearest = std::pow(9 * ui::interface_scale, 2);
        std::optional<std::uint32_t> face_hit;
        for (const auto& face : source.document.at("faces")) {
            const auto id = face.at("id").get<std::uint32_t>();
            const auto& corners = face.at("corners");
            std::vector<std::array<double, 2>> points;
            for (std::size_t i = 0; i < corners.size(); ++i) {
                auto uv = corners[i].at("uv").get<std::array<double, 2>>();
                if (uv_drag_valid_) {
                    const MeshUvCorner corner{id, i};
                    const auto at =
                        std::lower_bound(uv_drag_points_.begin(), uv_drag_points_.end(), corner,
                                         [](const auto& point, MeshUvCorner wanted) {
                                             return point.corner < wanted;
                                         });
                    if (at != uv_drag_points_.end() && at->corner == corner)
                        uv = at->value;
                }
                points.push_back(screen(uv));
            }
            bool inside = false;
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                const auto a = points[i], b = points[j];
                if ((a[1] > mouse.y) != (b[1] > mouse.y) &&
                    mouse.x < (b[0] - a[0]) * (mouse.y - a[1]) / (b[1] - a[1]) + a[0])
                    inside = !inside;
            }
            if (inside && (!face_hit || id == face_ || (*face_hit != face_ && id < *face_hit)))
                face_hit = id;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto a = points[i], b = points[(i + 1) % points.size()];
                const MeshUvCorner ref{id, i};
                const bool selected = uv_selection_.contains(ref);
                line(a, b, selected ? IM_COL32(255, 195, 79, 255) : IM_COL32(87, 126, 149, 180),
                     selected ? 2.f : 1.f);
                if (a[0] >= origin.x && a[0] <= origin.x + size.x && a[1] >= origin.y &&
                    a[1] <= origin.y + size.y)
                    draw->AddCircleFilled(
                        {float(a[0]), float(a[1])}, (selected ? 4.f : 2.f) * ui::interface_scale,
                        selected ? IM_COL32(255, 226, 136, 255) : IM_COL32(164, 215, 228, 255));
                const auto dx = mouse.x - a[0], dy = mouse.y - a[1], distance = dx * dx + dy * dy;
                if (distance < nearest ||
                    (hit && distance == nearest && id == face_ && hit->face != face_)) {
                    nearest = distance;
                    hit = ref;
                }
            }
        }
        draw->PopClipRect();
        draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(181, 216, 224, 255),
                      "UV tile (0,0 at top left) | gold = selected");
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const bool add = ImGui::GetIO().KeyCtrl;
            if (uv_mode_ == 0 && hit) {
                if (add) {
                    if (!uv_selection_.erase(*hit))
                        uv_selection_.insert(*hit);
                } else if (!uv_selection_.contains(*hit)) {
                    uv_selection_.clear();
                    uv_selection_.insert(*hit);
                }
                face_ = hit->face;
                uv_initialized_ = true;
            } else if (uv_mode_ != 0 && (hit || face_hit)) {
                const auto selected = hit ? hit->face : *face_hit;
                select_uv_face(selected, add, uv_mode_ == 2);
            } else if (!hit && !face_hit && !add) {
                uv_selection_.clear();
                uv_initialized_ = true;
            }
            if (!locked && !preview_uv_ && !add && !uv_selection_.empty() &&
                (hit || (uv_mode_ != 0 && face_hit))) {
                uv_drag_ = true;
                uv_drag_start_ = to_uv(mouse);
                uv_drag_delta_ = {};
                uv_drag_revision_ = document_->revision();
                uv_drag_selection_ = selected_uv_corners();
                prepare_uv_drag();
            }
        }
        if (uv_drag_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancel_uv_drag();
            return;
        }
        const auto drag_pixels =
            ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2 * ui::interface_scale);
        const auto position = to_uv(mouse);
        const auto dx = (position[0] - uv_drag_start_[0]) * base * uv_zoom_;
        const auto dy = (position[1] - uv_drag_start_[1]) * base * uv_zoom_;
        const auto threshold = 2 * ui::interface_scale;
        // ImGui updates maximum drag distance only while the button is held.
        // Include release-only movement and refresh a drag that returns to its start.
        const bool released_move = ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                                   dx * dx + dy * dy >= threshold * threshold;
        if (uv_drag_ && ImGui::IsMousePosValid(&mouse) &&
            (drag_pixels.x != 0 || drag_pixels.y != 0 || uv_drag_valid_ ||
             !uv_preview_error_.empty() || released_move)) {
            uv_drag_delta_ = {position[0] - uv_drag_start_[0], position[1] - uv_drag_start_[1]};
            const auto key = Json::array({uv_drag_revision_, uv_drag_delta_}).dump();
            if (key != uv_preview_key_) {
                uv_preview_key_ = key;
                evaluate_uv_drag(uv_drag_delta_);
            }
        }
        if (uv_drag_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const auto selection = uv_drag_selection_;
            const auto delta = uv_drag_delta_;
            const bool accept = uv_drag_valid_ && uv_preview_error_.empty() &&
                                uv_drag_revision_ == document_->revision();
            cancel_uv_drag();
            if (accept)
                mutate("Move UV corners",
                       [&](auto& mesh) { mesh.transform_uv(selection, delta, 0, {1, 1}); });
        }
    }
};
} // namespace forge
