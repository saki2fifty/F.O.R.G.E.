#pragma once
#include "../bounded_json.hpp"
#include "../material_graph_document.hpp"
#include "../shader_pipeline.hpp"
#include "document.hpp"
#include "material_graph_canvas.hpp"
#include <forge/material_resource.hpp>
#include <forge/material_source.hpp>
#include <future>
namespace forge {
class MaterialGraphEditor {
  public:
    std::function<void(MaterialResourceData, std::shared_ptr<const AssetCatalog>)> update_preview;
    std::function<void(bool)> draw_preview;
    std::function<void()> release_preview;
    std::function<bool(const std::filesystem::path&)> publish;
    std::function<bool()> publication_busy;
    std::function<std::string()> publication_error;
    MaterialGraphEditor(std::filesystem::path worker,
                        std::function<asset_detail::ShaderCompilerProfile()> compiler)
        : worker_(std::move(worker)), compiler_(std::move(compiler)) {}
    ~MaterialGraphEditor() { cancel_.request_stop(); }
    bool close_cancelled = false;
    MaterialGraphCanvas canvas;
    std::optional<GraphFunctionId> function_;
    bool extract_ = false;
    static bool source_is_graph(const std::filesystem::path& root,
                                const std::filesystem::path& source) {
        const auto bytes =
            asset_detail::read_bytes(ProjectPaths(root).resolve(source), 1024 * 1024);
        const auto j = asset_detail::parse_bounded_json(bytes, 1024 * 1024);
        return j.value("format", std::string{}) == "forge.shader" && j.value("version", 0) == 3;
    }
    void preview_material(const ResolvedMaterialSource& source) {
        if (document_ && source.shader && source.shader->id == document_->source().asset()) {
            preview_source_ = source;
            changed();
        } else
            pending_preview_ = source;
    }
    bool is_open() const { return bool(document_); }
    bool pending() const { return wants_preview_ || preview_job_.valid() || publishing_; }
    bool dirty() const {
        return document_ &&
               (document_->dirty() || canvas.draft_dirty() || needs_publish_ || publishing_);
    }
    bool can_undo() const {
        return document_ && !publishing_ && (canvas.draft_dirty() || document_->can_undo());
    }
    bool can_redo() const {
        return document_ && !publishing_ && !canvas.draft_dirty() && document_->can_redo();
    }
    const MaterialGraphDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_.empty() ? preview_error_ : error_; }
    bool preview_ready() const { return preview_ready_; }
    bool preview_current() const {
        return preview_ready_ && ready_generation_ == generation_ && !wants_preview_ &&
               !preview_job_.valid();
    }
    void undo() {
        if (can_undo()) {
            flush();
            document_->undo();
            function_.reset();
            canvas.reset();
            changed();
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            function_.reset();
            canvas.reset();
            changed();
        }
    }
    bool inspect(std::string_view key) const {
        if (!document_)
            return false;
        ImGui::TextUnformatted("Material graph");
        ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
        ui::help("The graph owns its source and history. Edit selected node properties in the "
                 "graph workspace.");
        ImGui::TextWrapped("%s", diagnostic().empty()
                                     ? (dirty() ? "Unpublished changes" : "Published source")
                                     : diagnostic().c_str());
        for (const auto& n : view_graph().at("nodes"))
            if (n.at("id").get<std::string>() == key) {
                ImGui::Separator();
                ImGui::TextWrapped("Node: %s", n.at("type").get<std::string>().c_str());
                ImGui::TextWrapped("ID: %s", std::string(key).c_str());
                return true;
            }
        return key.empty();
    }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty()) {
            cancel_.request_stop();
            // Release preview textures in the next poll, after the current
            // frame's ImGui draw list has been submitted.
        }
    }
    void asset_catalog_changed(std::shared_ptr<const AssetCatalog> catalog) {
        catalog_ = std::move(catalog);
    }
    void source_published(AssetId id) {
        if (!document_ || document_->source().asset() != id)
            return;
        if (publishing_) {
            publishing_ = needs_publish_ = false;
            error_.clear();
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (document_ && document_->project() == project.project() &&
            document_->locator() == ProjectPaths::normalize(source)) {
            focus_ = true;
            return;
        }
        if (document_) {
            pending_source_ = source;
            request_close();
            return;
        }
        if (preview_job_.valid()) {
            pending_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty() || publishing_);
        if (ui::button("New material graph...", "Create a typed material graph. Save and compile "
                                                "it, then choose it in a material's Shader field."))
            ImGui::OpenPopup("New material graph");
        FORGE_UI_PROBE("graph:new");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New material graph", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Graph file", path_, sizeof(path_));
            FORGE_UI_PROBE("graph:new-path");
            ui::help("A new .shader.json source inside an existing project folder. Material graphs "
                     "use the existing Shader asset and publication pipeline.");
            ImGui::BeginDisabled(locked || dirty() || preview_job_.valid());
            if (ui::button("Create graph", "Create the source and open its graph workspace; never "
                                           "overwrite existing files.")) {
                try {
                    auto created = MaterialGraphDocument::create(project.writer_guard(),
                                                                 std::filesystem::u8path(path_));
                    const auto source = created->locator();
                    created.reset();
                    open(project, source);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            FORGE_UI_PROBE("graph:create");
            ImGui::EndDisabled();
            ui::next_text_button("Cancel");
            if (ui::button("Cancel", "Close without creating an asset."))
                ImGui::CloseCurrentPopup();
            if (!diagnostic().empty())
                ui::field_error(diagnostic());
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            cancel_.request_stop();
            close_ = true;
        }
        if (preview_job_.valid() &&
            preview_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto program = preview_job_.get();
                if (document_ && requested_generation_ == generation_ &&
                    !cancel_.stop_requested()) {
                    auto values = surface_material_defaults(*program.surface);
                    MaterialTextureBindings bindings;
                    if (preview_source_) {
                        for (const auto& [key, value] : preview_source_->values.parameters)
                            if (values.parameters.contains(key) &&
                                values.parameters.at(key).type == value.type)
                                values.parameters[key] = value;
                        for (const auto& [key, ref] : preview_source_->textures)
                            if (values.textures.contains(key))
                                bindings[key] = ref;
                        values.alpha = preview_source_->values.alpha;
                        values.alpha_cutoff = preview_source_->values.alpha_cutoff;
                        values.double_sided = preview_source_->values.double_sided;
                    }
                    for (const auto& [key, ref] : preview_textures_)
                        if (values.textures.contains(key))
                            bindings[key] = ref;
                    MaterialResourceData data{std::move(values), std::move(bindings),
                                              MaterialShaderSnapshot{{document_->source().asset()},
                                                                     program.build_key,
                                                                     std::move(program)}};
                    validate_render_material(data);
                    if (update_preview)
                        update_preview(std::move(data), catalog_);
                    preview_ready_ = true;
                    ready_generation_ = requested_generation_;
                    preview_error_.clear();
                }
            } catch (const std::exception& e) {
                if (!cancel_.stop_requested())
                    preview_error_ = e.what();
            }
        }
        if (publishing_ && publication_busy && !publication_busy()) {
            publishing_ = false;
            if (needs_publish_ && publication_error)
                error_ = "Source saved; graph publication failed: " + publication_error();
        }
        if (!document_)
            return;
        if (save_ && !publishing_) {
            save_ = false;
            try {
                flush();
                document_->save();
                needs_publish_ = true;
                if (publish && publish(document_->locator())) {
                    publishing_ = true;
                    error_.clear();
                    message = "Graph source saved; compiling a publication candidate.";
                } else
                    error_ =
                        "Graph source saved; publication did not start. " +
                        (publication_error && !publication_error().empty()
                             ? publication_error()
                             : "Finish the current Shader import task, then Save & Compile again.");
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        if (close_ && !dirty() && !preview_job_.valid()) {
            const auto next = std::exchange(pending_source_, {});
            try {
                if (next)
                    load(project, *next);
                else
                    finish_close();
            } catch (const std::exception& e) {
                close_ = false;
                error_ = e.what();
            }
            return;
        }
        if (wants_preview_ && !preview_job_.valid() &&
            std::chrono::steady_clock::now() - edited_ >= std::chrono::milliseconds(300)) {
            wants_preview_ = false;
            cancel_ = std::stop_source{};
            requested_generation_ = generation_;
            auto source = document_->source();
            source.document["graph"] = preview_graph();
            const auto root = document_->project(), worker = worker_;
            asset_detail::ShaderCompilerProfile compiler;
            try {
                compiler = compiler_();
            } catch (const std::exception& e) {
                preview_error_ = e.what();
                return;
            }
            const auto stop = cancel_.get_token();
            preview_job_ = std::async(
                std::launch::async, [root, worker, compiler, source = std::move(source), stop] {
                    source.validate();
                    const auto graph = compile_material_graph(source.document.at("graph"));
                    asset_detail::ShaderSnapshot snapshot;
                    snapshot.document = source.document;
                    snapshot.program = shader_program_source(source.document);
                    snapshot.sources.emplace("engine/forge.graph.hlsl", graph.source);
                    auto files = asset_detail::run_import_process(
                        worker, root, asset_detail::shader_process_request(snapshot, {}, compiler),
                        asset_detail::shader_worker_limits(), stop);
                    if (files.size() != 1 || files.front().name != "program.shader")
                        throw std::runtime_error("Unexpected graph preview output");
                    return decode_shader(files.front().bytes);
                });
        }
    }
    void draw(SceneDocument& project, bool locked) {
        if (!document_)
            return;
        ui::draft_window_size({1100 * ui::interface_scale, 760 * ui::interface_scale});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        bool visible = true;
        const auto title =
            std::string(dirty() ? "* Material Graph" : "Material Graph") + "###Material Graph";
        if (ImGui::Begin(title.c_str(), &visible)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("material.graph", "Material Graph");
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ui::help("Graph source and history belong to this asset, independently of material "
                     "instance values and scene Undo.");
            ImGui::BeginDisabled(locked || publishing_);
            if (ui::button("Save & Compile",
                           "Save graph source and publish only a validated shader candidate. "
                           "Failures preserve the previous cooked graph and materials."))
                request_save();
            FORGE_UI_PROBE("graph:save");
            ImGui::EndDisabled();
            ui::next_text_button("Refresh preview");
            if (ui::button("Refresh preview",
                           "Compile the current unsaved graph in an isolated worker. Preview never "
                           "publishes or edits the scene."))
                changed();
            ui::next_text_button("Close graph");
            if (ui::button("Close graph",
                           "Close this document after resolving unsaved graph work."))
                visible = false;
            FORGE_UI_PROBE("graph:close");
            ImGui::SameLine();
            ImGui::TextUnformatted(wants_preview_ || preview_job_.valid() ? "Compiling preview..."
                                   : preview_current()                    ? "Preview ready"
                                   : preview_ready_ ? "Previous preview retained"
                                                    : "Preparing preview");
            ui::help("The last usable preview remains visible while compilation or resource "
                     "preparation runs.");
            if (!diagnostic().empty()) {
                ui::field_error(diagnostic());
                if (ui::button("Go to error node",
                               "Select the node named by the validation/compiler diagnostic, "
                               "including a function interior."))
                    navigate_error();
                FORGE_UI_PROBE("graph:go-error");
            }
            const auto available = ImGui::GetContentRegionAvail();
            const bool stacked = available.x < 720 * ui::interface_scale;
            if (draw_preview) {
                ImGui::BeginChild("Graph preview",
                                  {stacked
                                       ? available.x
                                       : std::min(320 * ui::interface_scale, available.x * .28f),
                                   stacked ? std::min(220 * ui::interface_scale, available.y * .35f)
                                           : available.y},
                                  ImGuiChildFlags_Borders);
                if (catalog_) {
                    for (const auto& node : document_->source().document.at("graph").at("nodes")) {
                        if (!node.at("type").get<std::string>().starts_with("texture"))
                            continue;
                        const auto& d = node.at("data");
                        if (!d.contains("key") || !d.at("key").is_string())
                            continue;
                        const auto key = d.at("key").get<std::string>();
                        Json ref = preview_textures_.contains(key)
                                       ? Json(preview_textures_.at(key).id)
                                   : preview_source_ && preview_source_->textures.contains(key)
                                       ? Json(preview_source_->textures.at(key).id)
                                       : Json{};
                        ui::IdScope scope(key.c_str());
                        const auto label = d.contains("label") && d.at("label").is_string()
                                               ? d.at("label").get<std::string>()
                                               : key;
                        if (asset_ref_picker(*catalog_, ref, "texture", label.c_str(), false)) {
                            if (ref.is_null())
                                preview_textures_.erase(key);
                            else
                                preview_textures_[key] = {ref.get<AssetId>()};
                            changed();
                        }
                        ui::help("Preview texture only. Save persistent texture assignments in the "
                                 "material document.");
                    }
                }
                draw_preview(stacked);
                ImGui::EndChild();
                if (!stacked)
                    ImGui::SameLine();
            }
            ImGui::BeginDisabled(locked || publishing_);
            if (function_) {
                if (ui::button("Back to surface",
                               "Return to the surface graph. Function edits share this Shader "
                               "document's Undo and publication boundary.")) {
                    flush();
                    function_.reset();
                    canvas.reset();
                }
                FORGE_UI_PROBE("graph:back-surface");
                ImGui::SameLine();
            }
            std::optional<GraphFunctionId> next_function;
            std::optional<Json> next_call;
            std::string function_label = "Select a reusable function";
            if (function_ && document_->source().document.at("graph").contains("functions"))
                for (const auto& definition :
                     document_->source().document.at("graph").at("functions"))
                    if (definition.at("id").get<GraphFunctionId>() == *function_)
                        function_label = definition.at("label").get<std::string>();
            const bool functions_menu =
                document_->source().document.at("graph").contains("functions") &&
                ImGui::BeginCombo("Functions", function_label.c_str());
            if (document_->source().document.at("graph").contains("functions"))
                FORGE_UI_PROBE("graph:functions");
            if (functions_menu) {
                for (const auto& f : document_->source().document.at("graph").at("functions")) {
                    ui::IdScope scope(f.at("id").get<std::string>().c_str());
                    const auto label = f.at("label").get<std::string>();
                    if (ImGui::Selectable(("Edit " + label).c_str())) {
                        next_function = f.at("id").get<GraphFunctionId>();
                    }
                    FORGE_UI_PROBE("graph:function-edit:" + f.at("id").get<std::string>());
                    ui::help("Edit this function's interior. All calls use the same definition on "
                             "the next valid compilation.");
                    if (!function_ && ImGui::Selectable(("Add call: " + label).c_str())) {
                        next_call = material_graph_function_call(f, {100, 100});
                    }
                }
                ImGui::EndCombo();
            }
            if (function_) {
                for (const auto& f : document_->source().document.at("graph").at("functions"))
                    if (f.at("id").get<GraphFunctionId>() == *function_) {
                        char label[513]{};
                        const auto name = f.at("label").get<std::string>();
                        std::copy(name.begin(), name.end(), label);
                        if (ImGui::InputText("Function name", label, sizeof(label),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                            function_name_ = label;
                        FORGE_UI_PROBE("graph:function-name");
                        ui::help("Rename on Enter. Function identity, ports, callers and material "
                                 "bindings remain stable.");
                        break;
                    }
            }
            ImGui::EndDisabled();
            FORGE_UI_PROBE("graph:functions");
            if (function_name_) {
                const auto id = *function_;
                const auto name = std::exchange(function_name_, {}).value();
                document_->edit(document_->revision(), "Rename material function",
                                [&](Json& source) {
                                    for (auto& f : source["graph"]["functions"])
                                        if (f.at("id").get<GraphFunctionId>() == id)
                                            f["label"] = name;
                                });
                needs_publish_ = true;
                changed();
            }
            if (next_function) {
                flush();
                function_ = next_function;
                canvas.reset();
            }
            if (next_call) {
                const auto call = *next_call;
                edit_callback()("Add function call",
                                [call](Json& g) { g["nodes"].push_back(call); });
            }
            ImGui::BeginChild("Graph authoring", {}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar);
            canvas.function_label = [this](GraphFunctionId id) {
                for (const auto& f : document_->source().document.at("graph").at("functions"))
                    if (f.at("id").get<GraphFunctionId>() == id)
                        return f.at("label").get<std::string>();
                return std::string("Missing function");
            };
            canvas.function_scope = bool(function_);
            canvas.extract_function = function_
                                          ? std::function<void()>{}
                                          : std::function<void()>{[this] { extract_ = true; }};
            canvas.copy_selection = [this](const Json& g, const std::set<GraphNodeId>& ids) {
                auto view = g;
                if (document_->source().document.at("graph").contains("functions"))
                    view["functions"] = document_->source().document.at("graph").at("functions");
                return copy_material_graph_selection(view, ids);
            };
            const auto old_selection = canvas.selection;
            canvas.draw(view_graph(), document_->revision(), edit_callback(),
                        locked || publishing_);
            if (ui::editor_context) {
                auto& selection = ui::editor_context->selection;
                const auto key =
                    canvas.selection.size() == 1 ? canvas.selection.begin()->str() : "";
                const bool owns_selection = selection.kind() == ui::SelectionKind::DocumentItem &&
                                            selection.document() == "material.graph";
                if ((owns_selection || canvas.selection != old_selection ||
                     ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) &&
                    (!owns_selection || selection.member() != key))
                    selection.select_document_item("material.graph", key);
            }
            if (canvas.take_preview_change())
                changed();
            ImGui::EndChild();
            if (extract_) {
                extract_ = false;
                flush();
                const auto ids = canvas.selection;
                edit_callback()("Extract material function", [ids](Json& g) {
                    extract_material_graph_function(
                        g, ids,
                        "Function " + std::to_string(g.contains("functions")
                                                         ? g.at("functions").size() + 1
                                                         : 1));
                });
                canvas.reset();
            }
        }
        ImGui::End();
        if (!visible)
            request_close();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending graph changes");
        if (ImGui::BeginPopupModal("Pending graph changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save and compile graph edits, discard unsaved work, or keep "
                               "editing. Saved sources and previous publications remain on disk.");
            ImGui::BeginDisabled(locked || publishing_);
            if (ui::button("Save", "Save source; close only after a successful publication."))
                request_save();
            ImGui::EndDisabled();
            ui::next_text_button("Discard");
            ImGui::BeginDisabled(publishing_);
            if (ui::button("Discard", "Discard unsaved graph drafts and cancel preview work; "
                                      "already saved source is retained.")) {
                cancel_.request_stop();
                close_ = true;
                needs_publish_ = false;
                publishing_ = false;
                if (document_->dirty()) {
                    try {
                        document_ = std::make_unique<MaterialGraphDocument>(project.writer_guard(),
                                                                            document_->locator());
                    } catch (const std::exception& e) {
                        error_ = e.what();
                        close_ = false;
                    }
                }
                canvas.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ui::next_text_button("Keep editing");
            if (ui::button("Keep editing",
                           "Cancel closing or switching projects; retain all graph drafts.")) {
                close_ = false;
                pending_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            if (!diagnostic().empty())
                ui::field_error(diagnostic());
            ImGui::EndPopup();
        }
    }

  private:
    std::unique_ptr<MaterialGraphDocument> document_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::filesystem::path worker_;
    std::function<asset_detail::ShaderCompilerProfile()> compiler_;
    std::future<ShaderData> preview_job_;
    std::stop_source cancel_;
    bool wants_preview_ = false, preview_ready_ = false, save_ = false, close_ = false,
         focus_ = false, publishing_ = false, needs_publish_ = false;
    uint64_t generation_ = 0, requested_generation_ = 0, ready_generation_ = 0;
    std::chrono::steady_clock::time_point edited_;
    std::optional<std::filesystem::path> pending_source_;
    std::optional<std::string> function_name_;
    std::string error_, preview_error_;
    MaterialTextureBindings preview_textures_;
    std::optional<ResolvedMaterialSource> preview_source_, pending_preview_;
    char path_[512] = "Assets/SurfaceGraph.shader.json";
    MaterialGraphCanvas::Edit edit_callback() {
        return [this](std::string label, const std::function<void(Json&)>& f) {
            try {
                document_->edit(document_->revision(), std::move(label), [&](Json& source) {
                    if (!function_)
                        f(source["graph"]);
                    else {
                        bool found = false;
                        for (auto& function : source["graph"]["functions"])
                            if (function.at("id").get<GraphFunctionId>() == *function_) {
                                auto body = function.at("graph");
                                body["functions"] = source.at("graph").at("functions");
                                f(body);
                                auto definitions = body.at("functions");
                                body.erase("functions");
                                for (auto& definition : definitions)
                                    if (definition.at("id").get<GraphFunctionId>() == *function_)
                                        definition["graph"] = body;
                                source["graph"]["functions"] = std::move(definitions);
                                found = true;
                                break;
                            }
                        if (!found)
                            throw std::runtime_error(
                                "Selected function no longer exists; return to the surface graph.");
                    }
                });
                needs_publish_ = true;
                changed();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        };
    }
    void navigate_error() {
        flush();
        const auto& g = document_->source().document.at("graph");
        const auto locate = [&](const Json& nodes) {
            for (const auto& n : nodes)
                if (diagnostic().find(n.at("id").get<std::string>()) != std::string::npos) {
                    canvas.selection = {n.at("id").get<GraphNodeId>()};
                    return true;
                }
            return false;
        };
        canvas.reset();
        function_.reset();
        if (locate(g.at("nodes")))
            return;
        if (g.contains("functions"))
            for (const auto& f : g.at("functions"))
                if (locate(f.at("graph").at("nodes"))) {
                    function_ = f.at("id").get<GraphFunctionId>();
                    return;
                }
    }
    const Json& view_graph() const {
        const auto& root = document_->source().document.at("graph");
        if (function_ && root.contains("functions"))
            for (const auto& f : root.at("functions"))
                if (f.at("id").get<GraphFunctionId>() == *function_)
                    return f.at("graph");
        return root;
    }
    Json preview_graph() const {
        auto root = document_->source().document.at("graph");
        if (function_ && root.contains("functions")) {
            for (auto& f : root["functions"])
                if (f.at("id").get<GraphFunctionId>() == *function_)
                    f["graph"] = canvas.preview_graph(f.at("graph"));
        } else
            root = canvas.preview_graph(root);
        return root;
    }
    void flush() { canvas.flush(edit_callback()); }
    void changed() {
        ++generation_;
        edited_ = std::chrono::steady_clock::now();
        wants_preview_ = true;
        cancel_.request_stop();
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto next = std::make_unique<MaterialGraphDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        const auto preview = pending_preview_;
        finish_close();
        document_ = std::move(next);
        catalog_ = std::move(catalog);
        if (preview && preview->shader && preview->shader->id == document_->source().asset())
            preview_source_ = preview;
        canvas.reset();
        focus_ = true;
        changed();
    }
    void finish_close() {
        cancel_.request_stop();
        document_.reset();
        function_.reset();
        preview_source_.reset();
        pending_preview_.reset();
        preview_textures_.clear();
        canvas.reset();
        save_ = close_ = focus_ = publishing_ = needs_publish_ = wants_preview_ = preview_ready_ =
            false;
        error_.clear();
        preview_error_.clear();
        if (release_preview)
            release_preview();
    }
};
} // namespace forge
