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
#include <map>
#include <optional>
#include <set>
#include <utility>

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
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (can_undo()) {
            document_->undo();
            reconcile_selection();
            needs_publish_ = true;
        }
    }
    void redo() {
        if (can_redo()) {
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
                       "Create a cube Mesh source for modeling and UV editing."))
            ImGui::OpenPopup("New editable Mesh");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New editable Mesh", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ui::help("Choose a new .mesh.json file inside your project.");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button("Create",
                           "Create a cube source; Save & Publish makes it usable in scenes.")) {
                try {
                    auto created = EditableMeshDocument::create(project.writer_guard(),
                                                                std::filesystem::u8path(path_));
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
                    model_workspace(locked);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("UV")) {
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
    std::string error_;
    SelectionMode mode_ = SelectionMode::Face;
    std::set<std::uint32_t> vertices_;
    std::pair<std::uint32_t, std::uint32_t> edge_{};
    std::uint32_t face_ = 13;
    std::set<std::size_t> uv_corners_;
    float translation_[3]{}, rotation_[3]{}, scale_[3]{1, 1, 1};
    float extrusion_ = 0.5f;
    float uv_translation_[2]{}, uv_angle_ = 0, uv_scale_ = 1;
    bool preview_transform_ = false, preview_extrude_ = false, preview_uv_ = false;
    std::string model_preview_key_, uv_preview_key_, model_preview_error_, uv_preview_error_;
    std::optional<EditableMeshSource> model_preview_, uv_preview_;
    float yaw_ = 40, pitch_ = 25;

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
        uv_corners_.clear();
        reconcile_selection();
        preview_transform_ = preview_extrude_ = preview_uv_ = false;
        error_.clear();
        model_preview_key_.clear();
        uv_preview_key_.clear();
        model_preview_.reset();
        uv_preview_.reset();
    }
    void finish_close() {
        document_.reset();
        service_.reset();
        model_preview_.reset();
        uv_preview_.reset();
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
            uv_corners_.clear();
        } else
            std::erase_if(uv_corners_,
                          [&](auto corner) { return corner >= selected->at("corners").size(); });
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
        if (ImGui::Combo("Select", &mode, labels, 3))
            mode_ = static_cast<SelectionMode>(mode);
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
                    if (ImGui::Selectable(label.c_str(), face_ == key))
                        face_ = key;
                }
        }
        ImGui::EndChild();
        ui::help("Select a face, edge or vertex. Ctrl-click adds vertices to the selection.");
    }
    void model_workspace(bool locked) {
        ImGui::SliderFloat("Orbit yaw", &yaw_, -180, 180);
        ImGui::SliderFloat("Orbit pitch", &pitch_, -75, 75);
        const auto& source = document_->source();
        if (preview_extrude_ || preview_transform_) {
            const auto ids = selected_vertices();
            const auto key =
                Json::array(
                    {document_->revision(), preview_extrude_, static_cast<int>(mode_), face_, ids,
                     std::array<float, 3>{translation_[0], translation_[1], translation_[2]},
                     std::array<float, 3>{rotation_[0], rotation_[1], rotation_[2]},
                     std::array<float, 3>{scale_[0], scale_[1], scale_[2]}, extrusion_})
                    .dump();
            if (key != model_preview_key_) {
                model_preview_key_ = key;
                model_preview_ = source;
                model_preview_error_.clear();
                try {
                    if (preview_extrude_ && mode_ == SelectionMode::Face)
                        (void)model_preview_->extrude_face(face_, extrusion_);
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
        draw_model_wireframe(model_preview_ ? *model_preview_ : source);
        selection_list();
        ImGui::BeginDisabled(locked || pending());
        ImGui::SeparatorText("Transform selection");
        ImGui::InputFloat3("Move", translation_);
        ImGui::InputFloat3("Rotate (deg)", rotation_);
        ImGui::InputFloat3("Scale", scale_);
        ImGui::Checkbox("Preview transform", &preview_transform_);
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
        ImGui::SeparatorText("Extrude face");
        ImGui::InputFloat("Distance", &extrusion_);
        ImGui::Checkbox("Preview extrusion", &preview_extrude_);
        ImGui::BeginDisabled(mode_ != SelectionMode::Face);
        if (ui::button("Extrude selected face", "Add a cap and side walls as one Undo step.")) {
            std::uint32_t cap = 0;
            mutate("Extrude face", [&](EditableMeshSource& source) {
                cap = source.extrude_face(face_, extrusion_);
            });
            if (cap) {
                face_ = cap;
                reconcile_selection();
                uv_corners_.clear();
            }
            preview_extrude_ = false;
        }
        ImGui::EndDisabled();
        if (ui::button("Cancel extrusion", "Discard the extrusion preview."))
            preview_extrude_ = false;
        ImGui::EndDisabled();
    }
    void uv_workspace(bool locked) {
        const auto& source = document_->source();
        const Json* selected = nullptr;
        for (const auto& candidate : source.document.at("faces"))
            if (candidate.at("id").get<std::uint32_t>() == face_)
                selected = &candidate;
        if (!selected) {
            ImGui::TextUnformatted("Select a face in Model to edit its UVs.");
            return;
        }
        ImGui::Text("Face %u | each corner has an independent UV", face_);
        if (ImGui::BeginChild("UV corners", {0, 120 * ui::interface_scale},
                              ImGuiChildFlags_Borders)) {
            for (std::size_t i = 0; i < selected->at("corners").size(); ++i) {
                const auto& point = selected->at("corners")[i].at("uv");
                const auto label = "Corner " + std::to_string(i + 1) + "  (" +
                                   std::to_string(point[0].get<double>()) + ", " +
                                   std::to_string(point[1].get<double>()) + ")";
                if (ImGui::Selectable(label.c_str(), uv_corners_.contains(i))) {
                    if (!ImGui::GetIO().KeyCtrl)
                        uv_corners_.clear();
                    if (!uv_corners_.erase(i))
                        uv_corners_.insert(i);
                }
            }
        }
        ImGui::EndChild();
        if (preview_uv_) {
            const auto ids = selected_uv_corners(selected->at("corners").size());
            const auto key =
                Json::array({document_->revision(), face_, ids,
                             std::array<float, 2>{uv_translation_[0], uv_translation_[1]},
                             uv_angle_, uv_scale_})
                    .dump();
            if (key != uv_preview_key_) {
                uv_preview_key_ = key;
                uv_preview_ = source;
                uv_preview_error_.clear();
                try {
                    uv_preview_->transform_uv(face_, ids, {uv_translation_[0], uv_translation_[1]},
                                              uv_angle_ * 3.14159265358979323846 / 180.0,
                                              uv_scale_);
                } catch (const std::exception& e) {
                    uv_preview_error_ = e.what();
                    uv_preview_.reset();
                }
            }
        } else {
            uv_preview_key_.clear();
            uv_preview_.reset();
            uv_preview_error_.clear();
        }
        if (!uv_preview_error_.empty())
            ImGui::TextWrapped("UV preview unavailable: %s", uv_preview_error_.c_str());
        draw_uv_wireframe(uv_preview_ ? *uv_preview_ : source);
        ImGui::BeginDisabled(locked || pending());
        ImGui::InputFloat2("Move UV", uv_translation_);
        ImGui::InputFloat("Rotate UV (deg)", &uv_angle_);
        ImGui::InputFloat("Scale UV", &uv_scale_);
        ImGui::Checkbox("Preview UV", &preview_uv_);
        if (ui::button("Apply UV transform",
                       "Move, rotate or scale chosen face corners as one Undo step.")) {
            const auto ids = selected_uv_corners(selected->at("corners").size());
            mutate("Transform UV", [&, ids](EditableMeshSource& mesh) {
                mesh.transform_uv(face_, ids, {uv_translation_[0], uv_translation_[1]},
                                  uv_angle_ * 3.14159265358979323846 / 180.0, uv_scale_);
            });
            preview_uv_ = false;
        }
        if (ui::button("Planar project face",
                       "Project this face along its dominant normal axis.")) {
            mutate("Project face UV",
                   [&](EditableMeshSource& mesh) { mesh.project_face_uv(face_); });
            preview_uv_ = false;
        }
        if (ui::button("Cancel UV preview",
                       "Discard pending UV values without editing the Mesh.")) {
            uv_translation_[0] = uv_translation_[1] = uv_angle_ = 0;
            uv_scale_ = 1;
            preview_uv_ = false;
        }
        ImGui::EndDisabled();
    }
    std::vector<std::size_t> selected_uv_corners(std::size_t count) const {
        if (!uv_corners_.empty())
            return {uv_corners_.begin(), uv_corners_.end()};
        std::vector<std::size_t> all;
        for (std::size_t i = 0; i < count; ++i)
            all.push_back(i);
        return all;
    }
    void draw_model_wireframe(const EditableMeshSource& source) {
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(180.f, available.x), 240.f * ui::interface_scale};
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Mesh preview", size);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(14, 34, 46, 255));
        const double yaw = yaw_ * 3.14159265358979323846 / 180.0;
        const double pitch = pitch_ * 3.14159265358979323846 / 180.0;
        std::map<std::uint32_t, ImVec2> projected;
        float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
        for (const auto& vertex : source.document.at("vertices")) {
            const auto& p = vertex.at("position");
            const double x = p[0].get<double>(), y = p[1].get<double>(), z = p[2].get<double>();
            const auto horizontal = std::cos(yaw) * x - std::sin(yaw) * z;
            const auto depth = std::sin(yaw) * x + std::cos(yaw) * z;
            const auto vertical = std::cos(pitch) * y - std::sin(pitch) * depth;
            const auto key = vertex.at("id").get<std::uint32_t>();
            projected[key] = {static_cast<float>(horizontal), static_cast<float>(-vertical)};
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
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
                for (const auto& polygon : source.document.at("faces")) {
                    const auto& corners = polygon.at("corners");
                    bool inside = false;
                    for (std::size_t i = 0, j = corners.size() - 1; i < corners.size(); j = i++) {
                        const auto a = projected.at(corners[i].at("vertex").get<std::uint32_t>());
                        const auto b = projected.at(corners[j].at("vertex").get<std::uint32_t>());
                        if ((a.y > mouse.y) != (b.y > mouse.y) &&
                            mouse.x < (b.x - a.x) * (mouse.y - a.y) / (b.y - a.y) + a.x)
                            inside = !inside;
                    }
                    if (inside)
                        face_ = polygon.at("id").get<std::uint32_t>();
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
    void draw_uv_wireframe(const EditableMeshSource& source) {
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(180.f, available.x), 230.f * ui::interface_scale};
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("UV preview", size);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(20, 30, 41, 255));
        const auto span = std::min(size.x, size.y) - 36.f;
        const auto start =
            ImVec2{origin.x + (size.x - span) * .5f, origin.y + (size.y - span) * .5f};
        draw->AddRect(start, {start.x + span, start.y + span}, IM_COL32(97, 128, 145, 255));
        for (const auto& polygon : source.document.at("faces"))
            if (polygon.at("id").get<std::uint32_t>() == face_) {
                std::vector<ImVec2> points;
                for (const auto& corner : polygon.at("corners")) {
                    const auto& uv = corner.at("uv");
                    points.push_back({start.x + float(uv[0].get<double>()) * span,
                                      start.y + float(uv[1].get<double>()) * span});
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const auto mouse = ImGui::GetMousePos();
                    float nearest = 100.f;
                    std::optional<std::size_t> hit;
                    for (std::size_t i = 0; i < points.size(); ++i) {
                        const auto dx = mouse.x - points[i].x, dy = mouse.y - points[i].y;
                        const auto distance = dx * dx + dy * dy;
                        if (distance < nearest) {
                            nearest = distance;
                            hit = i;
                        }
                    }
                    if (hit) {
                        if (!ImGui::GetIO().KeyCtrl)
                            uv_corners_.clear();
                        if (!uv_corners_.erase(*hit))
                            uv_corners_.insert(*hit);
                    }
                }
                draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                                  IM_COL32(255, 195, 79, 255), ImDrawFlags_Closed, 2.f);
                for (std::size_t i = 0; i < points.size(); ++i)
                    draw->AddCircleFilled(points[i], uv_corners_.contains(i) ? 5.f : 3.f,
                                          uv_corners_.contains(i) ? IM_COL32(255, 226, 136, 255)
                                                                  : IM_COL32(164, 215, 228, 255));
                break;
            }
        draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(181, 216, 224, 255),
                      "Face-corner UVs (0,0 at top left)");
    }
};
} // namespace forge
