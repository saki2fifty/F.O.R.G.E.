#pragma once
#include "../collision_authoring.hpp"
#include "../collision_document.hpp"
#include "../model_render_resource.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
namespace forge {
// One authored document/history owner; cooked publication uses the shared importer.
class CollisionEditor {
  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_); }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const CollisionDocument* document() const { return document_.get(); }
    void undo() {
        if (can_undo()) {
            document_->undo();
            needs_publish_ = true;
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            needs_publish_ = true;
        }
    }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void asset_catalog_changed(std::shared_ptr<const AssetCatalog> c) { catalog_ = std::move(c); }
    void source_published(SceneDocument& project, AssetId id) {
        if (document_ && !dirty() && document_->source().asset() == id)
            load(project, document_->locator());
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void from_mesh(const AssetRecord& mesh) {
        if (mesh.type != "mesh" || dirty())
            throw std::runtime_error(
                "Save or discard the current Collision document, then select a Mesh.");
        creation_mesh_ = mesh.id;
        creation_kind_ = 1;
        create_popup_ = true;
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New collision...",
                       "Create reusable collision geometry in the central Collision document.")) {
            creation_mesh_ = {};
            creation_kind_ = 0;
            create_popup_ = true;
        }
        ImGui::EndDisabled();
        if (std::exchange(create_popup_, false))
            ImGui::OpenPopup("New collision asset");
        if (ImGui::BeginPopupModal("New collision asset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("collision:path");
            ui::help("New .collision.json file inside this project. Save in Collision to validate "
                     "and publish.");
            const char* kinds[] = {"Box", "Convex Hull", "Static Triangle Mesh"};
            ImGui::Combo("Geometry", &creation_kind_, kinds, 3);
            ui::help("Convex hulls wrap the selected Mesh. Triangle meshes retain concave level "
                     "geometry and require Static bodies.");
            if (creation_mesh_)
                ImGui::TextWrapped("Source Mesh: %s", creation_mesh_.str().c_str());
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button(
                    "Create",
                    "Create a source file. Save in Collision to cook and publish the asset.")) {
                try {
                    auto created = CollisionDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_), [&](AssetId id) {
                            if (creation_kind_ == 0)
                                return CollisionSource::create(id, CollisionKind::Box);
                            return CollisionSource::from_mesh(
                                id,
                                creation_kind_ == 1 ? CollisionKind::ConvexHull
                                                    : CollisionKind::TriangleMesh,
                                {creation_mesh_ ? creation_mesh_ : engine_primitive(0).id});
                        });
                    load(project, created->locator());
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ui::next_text_button("Cancel");
            if (ui::button("Cancel", "Close without creating a collision source."))
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
            next_.reset();
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
                    message = "Collision source saved and validated collision published.";
                } else
                    error_ = "Source saved; collision publication failed: " + result.diagnostic;
            }
        if (save_ && document_ && !pending()) {
            save_ = false;
            try {
                document_->save();
                auto request =
                    service_->prepare(document_->locator(), {}, document_->source().asset());
                job_ = service_->submit(
                    std::move(request),
                    [](auto& c, const auto& p, const auto&) {
                        prepare_collision_publication(c, p);
                    },
                    [](const auto&, const auto&) {});
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_) {
            const auto source = std::exchange(next_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument&, bool locked) {
        if (!document_)
            return;
        ui::draft_window_size({850 * ui::interface_scale, 680 * ui::interface_scale});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        bool visible = true;
        const auto title = std::string(dirty() ? "* Collision" : "Collision") + "###Collision";
        if (ImGui::Begin(title.c_str(), &visible)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("collision", "Collision");
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ui::help("Collision is independent of rendered Mesh geometry. Save/history belong to "
                     "this asset, not the Scene.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Save source and validate a replacement. Failed cooking retains "
                                   "the last usable collision."))
                request_save();
            FORGE_UI_PROBE("collision:save");
            ui::next_text_button("Copy AssetId");
            if (ui::button("Copy AssetId", "Copy this collision asset's durable identity."))
                ImGui::SetClipboardText(document_->source().asset().str().c_str());
            try {
                fields();
                geometry_selection();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Preparing collision...");
            if (!error_.empty())
                ui::field_error(error_);
            if (close_ && dirty())
                ImGui::OpenPopup("Collision has changes");
            if (ImGui::BeginPopupModal("Collision has changes", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Save and publish this collision before closing?");
                ImGui::BeginDisabled(locked || pending());
                if (ui::button("Save and close", "Close after successful publication."))
                    request_save();
                ui::next_text_button("Discard draft");
                if (ui::button(
                        "Discard draft",
                        "Discard unsaved edits. Already saved source/publications remain.")) {
                    finish_close();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ui::next_text_button("Cancel");
                if (ui::button("Cancel", "Keep this document open.")) {
                    close_ = false;
                    next_.reset();
                    close_cancelled = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();
        if (!visible)
            request_close();
    }

  private:
    std::unique_ptr<CollisionDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_;
    std::uint64_t job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, focus_ = false;
    std::string error_;
    bool create_popup_ = false;
    int creation_kind_ = 0;
    AssetId creation_mesh_;
    ResourcePool<MeshAsset> meshes_{{1, 8, 8, 128ull * 1024 * 1024}};
    ResourceTicket mesh_ticket_;
    AssetId mesh_selection_;
    std::string member_selection_;
    std::string mesh_reviewed_revision_;
    bool mesh_review_checked_ = false, mesh_revision_changed_ = false;
    std::uint64_t selection_revision_ = 0;
    unsigned lod_selection_ = 0;
    std::set<unsigned> part_selection_;
    bool all_parts_ = true;
    char path_[512] = "Assets/collision.collision.json";
    void finish_close() {
        if (mesh_selection_)
            meshes_.unload({mesh_selection_});
        mesh_selection_ = {};
        member_selection_.clear();
        document_.reset();
        close_ = save_ = needs_publish_ = false;
        job_ = 0;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto next = std::make_unique<CollisionDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), collision_import_registry(), collision_import_target());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    CollisionDocumentTraits::byte_limit);
        const auto found = catalog->records().find(next->source().asset());
        const bool needs = found == catalog->records().end() ||
                           !found->second.metadata.contains("forge.import") ||
                           found->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        document_ = std::move(next);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        focus_ = true;
        error_.clear();
        close_ = save_ = false;
    }
    void edit(Json candidate) {
        try {
            document_->edit(document_->revision(), "Edit collision",
                            [&](Json& value) { value = std::move(candidate); });
            needs_publish_ = true;
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    static void prune(Json& draft) {
        std::set<std::string> retained;
        std::vector<std::string> pending{draft["root"].get<std::string>()};
        while (!pending.empty()) {
            auto id = pending.back();
            pending.pop_back();
            if (!retained.insert(id).second)
                continue;
            for (const auto& node : draft["nodes"])
                if (node["id"] == id)
                    for (const auto& child : node["children"])
                        pending.push_back(child.get<std::string>());
        }
        std::erase_if(draft["nodes"].get_ref<Json::array_t&>(), [&](const Json& n) {
            return !retained.contains(n["id"].get<std::string>());
        });
    }
    void geometry_selection() {
        meshes_.pump();
        if (member_selection_.empty())
            return;
        ImGui::OpenPopup("Collision mesh geometry");
        ui::draft_window_size({520 * ui::interface_scale, 430 * ui::interface_scale});
        if (!ImGui::BeginPopupModal("Collision mesh geometry", nullptr, ImGuiWindowFlags_None))
            return;
        const auto selected = meshes_.acquire(mesh_ticket_);
        if (!selected) {
            const auto info = mesh_ticket_.inspect();
            ImGui::TextWrapped("%s", info.diagnostic.empty() ? "Loading selected Mesh revision..."
                                                             : info.diagnostic.c_str());
        } else {
            const auto& lods = selected->mesh.lods;
            if (!mesh_review_checked_) {
                mesh_review_checked_ = true;
                mesh_revision_changed_ =
                    !all_parts_ && mesh_reviewed_revision_ != selected.identity().revision;
                if (mesh_revision_changed_)
                    part_selection_.clear();
                if (lod_selection_ < lods.size())
                    std::erase_if(part_selection_, [&](unsigned part) {
                        return part >= lods[lod_selection_].parts.size() ||
                               lods[lod_selection_].parts[part].topology != MeshTopology::Triangles;
                    });
            }
            if (mesh_revision_changed_)
                ImGui::TextWrapped("The Mesh revision changed. Review and select the intended "
                                   "parts again; old ordinals have not been reused.");
            ImGui::TextWrapped("Mesh revision: %.12s", selected.identity().revision.c_str());
            const auto label = "LOD " + std::to_string(lod_selection_);
            if (ImGui::BeginCombo("Geometry detail", label.c_str())) {
                for (unsigned i = 0; i < lods.size(); ++i)
                    if (ImGui::Selectable(("LOD " + std::to_string(i)).c_str(),
                                          i == lod_selection_)) {
                        lod_selection_ = i;
                        part_selection_.clear();
                        all_parts_ = true;
                    }
                ImGui::EndCombo();
            }
            ui::help("Use one imported level of detail for collision. No animated pose or morph "
                     "deformation is baked.");
            ImGui::Checkbox("All parts", &all_parts_);
            ui::help("All parts follows Mesh reimports. A specific part selection is bound to this "
                     "revision and must be deliberately reselected after reimport.");
            if (lod_selection_ < lods.size()) {
                const auto& parts = lods[lod_selection_].parts;
                ImGui::BeginChild("Mesh parts", {0, 180 * ui::interface_scale},
                                  ImGuiChildFlags_Borders);
                ImGui::BeginDisabled(all_parts_);
                for (unsigned i = 0; i < parts.size(); ++i) {
                    const auto& part = parts[i];
                    bool chosen = part_selection_.contains(i);
                    const auto title = "Part " + std::to_string(i) + " / material slot " +
                                       std::to_string(part.material_slot) + " / " +
                                       std::to_string(part.vertices) + " vertices";
                    ImGui::BeginDisabled(part.topology != MeshTopology::Triangles);
                    if (ImGui::Checkbox(title.c_str(), &chosen)) {
                        if (chosen)
                            part_selection_.insert(i);
                        else
                            part_selection_.erase(i);
                    }
                    ui::help("Select triangle geometry by its ordinal in this exact cooked Mesh "
                             "revision. Points and lines cannot form collision surfaces.");
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
                ImGui::EndChild();
            }
            const bool valid =
                lod_selection_ < lods.size() && (all_parts_ || !part_selection_.empty());
            ImGui::BeginDisabled(!valid);
            if (ui::button("Use geometry", "Store this geometry selection as one undoable document "
                                           "edit. Save to validate and cook.")) {
                auto draft = document_->source().document;
                try {
                    if (document_->revision() != selection_revision_)
                        throw std::runtime_error("Collision source changed while selecting "
                                                 "geometry; reopen the selection.");
                    auto member = std::find_if(
                        draft["nodes"].begin(), draft["nodes"].end(),
                        [&](const Json& node) { return node.at("id") == member_selection_; });
                    if (member == draft["nodes"].end() || !member->contains("source") ||
                        member->at("source").at("mesh") != mesh_selection_.str())
                        throw std::runtime_error(
                            "Collision Mesh selection no longer matches the source.");
                    auto& source = (*member)["source"];
                    source["lod"] = lod_selection_;
                    source["parts"] = all_parts_ ? Json("all") : Json(part_selection_);
                    source.erase("revision");
                    if (!all_parts_)
                        source["revision"] = selected.identity().revision;
                    edit(std::move(draft));
                    member_selection_.clear();
                    meshes_.unload({mesh_selection_});
                    mesh_selection_ = {};
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
        }
        if (ui::button("Cancel geometry", "Keep the previous geometry selection.")) {
            member_selection_.clear();
            meshes_.unload({mesh_selection_});
            mesh_selection_ = {};
            ImGui::CloseCurrentPopup();
        }
        if (!error_.empty())
            ui::field_error(error_);
        ImGui::EndPopup();
    }
    void fields() {
        auto draft = document_->source().document;
        bool changed = false;
        ui::heading("Shapes", "Local collision transforms belong to the asset. Triangle-mesh "
                              "children require a Static body.");
        for (std::size_t i = 0; i < draft["nodes"].size(); ++i) {
            auto& node = draft["nodes"][i];
            ui::IdScope scope(node["id"].get<std::string>().c_str());
            const auto kind = node["kind"].get<std::string>();
            ImGui::Text("%s%s", node["id"] == draft["root"] ? "Root / " : "Child / ", kind.c_str());
            ui::help("Stable child identity is retained when dimensions and local pose change.");
            if (ImGui::BeginCombo("Shape", kind.c_str())) {
                for (auto name : {"box", "sphere", "capsule", "cylinder", "convex_hull",
                                  "triangle_mesh", "compound"}) {
                    if (ImGui::Selectable(name, kind == name)) {
                        node["kind"] = name;
                        node.erase("source");
                        node["children"] = Json::array();
                        node["dimensions"] = std::string_view(name) == "box"      ? Json{1, 1, 1}
                                             : std::string_view(name) == "sphere" ? Json{.5, 0, 0}
                                             : (std::string_view(name) == "capsule" ||
                                                std::string_view(name) == "cylinder")
                                                 ? Json{.5, 1, 0}
                                                 : Json{0, 0, 0};
                        if (std::string_view(name) == "convex_hull" ||
                            std::string_view(name) == "triangle_mesh")
                            node["source"] = {{"mesh", engine_primitive(0).id},
                                              {"lod", 0},
                                              {"parts", "all"},
                                              {"degenerate", "reject"}};
                        if (std::string_view(name) == "compound") {
                            auto child = CollisionSource::create(document_->source().asset(),
                                                                 CollisionKind::Box)
                                             .document["nodes"][0];
                            node["children"].push_back(child["id"]);
                            draft["nodes"].push_back(std::move(child));
                        }
                        prune(draft);
                        changed = true;
                        break;
                    }
                }
                ImGui::EndCombo();
            }
            ui::help("Changing shape replaces this shape's geometry and children; document Undo "
                     "restores them.");
            if (changed)
                break; // Array edits invalidate the current node reference.
            if (node["id"] != draft["root"]) {
                const auto member = node["id"];
                auto parent = std::find_if(draft["nodes"].begin(), draft["nodes"].end(),
                                           [&](const Json& candidate) {
                                               const auto& children = candidate.at("children");
                                               return std::find(children.begin(), children.end(),
                                                                member) != children.end();
                                           });
                const bool removable =
                    parent != draft["nodes"].end() && parent->at("children").size() > 1;
                ImGui::BeginDisabled(!removable);
                if (ui::button("Remove child",
                               "Remove this member and its children. A compound must retain at "
                               "least one child. Undo restores the removed members.")) {
                    auto& children = (*parent)["children"];
                    children.erase(std::find(children.begin(), children.end(), member));
                    prune(draft);
                    changed = true;
                }
                ImGui::EndDisabled();
                if (changed)
                    break;
            }
            if (kind == "compound" &&
                ui::button(
                    "Add box child",
                    "Add a stable collision member, then edit its shape and local pose below.")) {
                auto child =
                    CollisionSource::create(document_->source().asset(), CollisionKind::Box)
                        .document["nodes"][0];
                node["children"].push_back(child["id"]);
                draft["nodes"].push_back(std::move(child));
                changed = true;
                break;
            }
            for (auto field : {"translation", "rotation", "scale", "dimensions"}) {
                auto values = node[field].get<std::vector<float>>();
                if (field == std::string_view("rotation")) {
                    auto angles = rotation_to_euler({values[0], values[1], values[2], values[3]});
                    float degrees[]{float(angles[0]), float(angles[1]), float(angles[2])};
                    if (ImGui::InputFloat3("Rotation", degrees)) {
                        const auto q = rotation_from_euler({degrees[0], degrees[1], degrees[2]});
                        node[field] = {q.x, q.y, q.z, q.w};
                        changed = true;
                    }
                    ui::help("Local rotation in degrees. Stored as a normalized quaternion.");
                } else if (field == std::string_view("dimensions")) {
                    bool edited = false;
                    if (kind == "box")
                        edited = ImGui::InputFloat3("Size (m)", values.data());
                    else if (kind == "sphere")
                        edited = ImGui::InputFloat("Radius (m)", &values[0]);
                    else if (kind == "capsule" || kind == "cylinder") {
                        edited = ImGui::InputFloat("Radius (m)", &values[0]);
                        ui::help("Radius of the circular cross-section in meters.");
                        edited |= ImGui::InputFloat("Straight height (m)", &values[1]);
                    }
                    if (kind == "box" || kind == "sphere" || kind == "capsule" ||
                        kind == "cylinder")
                        ui::help("Full box size or solid radius/height in meters. Capsule height "
                                 "excludes its rounded caps.");
                    if (edited) {
                        node[field] = values;
                        changed = true;
                    }
                } else {
                    if (ImGui::InputFloat3(field, values.data())) {
                        node[field] = values;
                        changed = true;
                    }
                    ui::help(field == std::string_view("dimensions")
                                 ? "Box: full XYZ size. Sphere: radius,0,0. Capsule/cylinder: "
                                   "radius,straight height,0. Geometry/compound: zero."
                                 : "Collision-local transform; does not modify entity or "
                                   "render-mesh transforms.");
                }
            }
            if (node.contains("source")) {
                auto mesh = node["source"]["mesh"];
                if (asset_ref_picker(*catalog_, mesh, "mesh", "Source Mesh")) {
                    node["source"]["mesh"] = mesh;
                    node["source"]["parts"] = "all";
                    node["source"].erase("revision");
                    changed = true;
                }
                bool remove = node["source"]["degenerate"] == "remove";
                if (ImGui::Checkbox("Remove degenerate triangles", &remove)) {
                    node["source"]["degenerate"] = remove ? "remove" : "reject";
                    changed = true;
                }
                ui::help(
                    "Explicitly discard zero-area triangles during cooking; default rejects them.");
                const auto& selection = node["source"];
                ImGui::TextWrapped("LOD %u / %s", selection.at("lod").get<unsigned>(),
                                   selection.at("parts").is_string()
                                       ? "all parts"
                                       : "selected parts (revision-bound)");
                if (ui::button("Choose mesh geometry...",
                               "Inspect the prepared Mesh and choose a level of detail or specific "
                               "triangle parts.")) {
                    if (changed)
                        edit(draft);
                    meshes_.pump();
                    mesh_selection_ = selection.at("mesh").get<AssetId>();
                    mesh_ticket_ = asset_detail::request_model_mesh(meshes_, document_->project(),
                                                                    catalog_, {mesh_selection_});
                    member_selection_ = node.at("id").get<std::string>();
                    selection_revision_ = document_->revision();
                    mesh_reviewed_revision_ = selection.value("revision", std::string{});
                    mesh_review_checked_ = mesh_revision_changed_ = false;
                    lod_selection_ = selection.at("lod").get<unsigned>();
                    all_parts_ = selection.at("parts").is_string();
                    part_selection_.clear();
                    if (!all_parts_)
                        part_selection_ = selection.at("parts").get<std::set<unsigned>>();
                    return;
                }
            }
        }
        if (changed)
            edit(std::move(draft));
    }
};
} // namespace forge
