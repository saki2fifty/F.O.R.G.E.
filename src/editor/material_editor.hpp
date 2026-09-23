#pragma once
#include "../asset_bytes.hpp"
#include "../material_authoring.hpp"
#include "../material_document.hpp"
#include "../model_render_resource.hpp"
#include "../pbr_material.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <future>
namespace forge {
class MaterialEditor {
  public:
    std::function<void(AssetRef<MaterialAsset>, MaterialResourceData,
                       std::shared_ptr<const AssetCatalog>)>
        update_preview;
    std::function<void(bool)> draw_preview;
    std::function<void()> release_preview;
    ~MaterialEditor() { cancel_.request_stop(); }
    bool close_cancelled = false;
    bool is_open() const { return bool(document_); }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || job_); }
    bool can_undo() const { return document_ && !job_ && document_->can_undo(); }
    bool can_redo() const { return document_ && !job_ && document_->can_redo(); }
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
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    const MaterialDocument* document() const { return document_.get(); }
    void edit(std::uint64_t expected, std::string label,
              const std::function<void(Json&)>& mutation) {
        if (!document_ || job_)
            throw std::runtime_error("Material source is unavailable or publishing");
        const auto before = document_->revision();
        document_->edit(expected, std::move(label), mutation);
        if (document_->revision() != before)
            changed();
    }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            pending_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            report(e.what());
        }
    }
    void asset_catalog_changed(std::shared_ptr<const AssetCatalog> catalog) {
        if (!document_)
            return;
        catalog_ = std::move(catalog);
        invalidate_base();
    }
    void source_published(SceneDocument& project, AssetId id) {
        if (!document_ || document_->source().asset() != id || dirty())
            return;
        auto next =
            std::make_unique<MaterialDocument>(project.writer_guard(), document_->locator());
        document_ = std::move(next);
        invalidate_base(); // Preserve last-good preview/camera while the clean source reloads.
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New material...", "Create a reusable material source. Edit and publish it "
                                          "in the central Material workspace."))
            ImGui::OpenPopup("New material source");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New material source", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("material:new-path");
            ui::help("Choose a new .material.json path inside an existing project folder.");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button("Create", "Create the authored source without replacing existing files. "
                                     "Save in Material to publish it.")) {
                try {
                    auto created = MaterialDocument::create(project.writer_guard(),
                                                            std::filesystem::u8path(path_));
                    load(project, created->locator());
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    report(e.what());
                }
            }
            ImGui::EndDisabled();
            ui::next_text_button("Cancel");
            if (ui::button("Cancel", "Close without creating a material."))
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
                    catalog_ = published_ =
                        std::make_shared<const AssetCatalog>(outcome.publication->catalog);
                    needs_publish_ = false;
                    message = "Material source saved and validated revision published.";
                    error_.clear();
                    invalidate_base();
                } else
                    report("Source was saved; material publication failed: " + outcome.diagnostic);
            }
        if (!document_) {
            if (pending_source_) {
                const auto path = std::exchange(pending_source_, {});
                open(project, *path);
            }
            return;
        }
        const auto wanted = source_context_key();
        if (wanted != requested_context_)
            invalidate_base();
        if (base_job_.valid() &&
            base_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto value = base_job_.get();
                if (!cancel_.stop_requested()) {
                    base_ = std::move(value.base);
                    surface_ = std::move(value.surface);
                    base_ready_ = true;
                    evaluated_ = 0;
                }
            } catch (const std::exception& e) {
                if (!cancel_.stop_requested())
                    report(e.what());
            }
        }
        if (!base_started_ && !base_job_.valid()) {
            requested_context_ = wanted;
            base_started_ = true;
            cancel_ = std::stop_source{};
            if (!document_->source().base() && !material_shader_reference(document_->source())) {
                base_.reset();
                surface_.reset();
                base_ready_ = true;
            } else {
                const auto root = document_->project();
                const auto catalog = catalog_;
                base_job_ =
                    std::async(std::launch::async, [root, catalog, source = document_->source(),
                                                    stop = cancel_.get_token()] {
                        return prepare_material_source(root, *catalog, source, stop);
                    });
            }
        }
        if (base_ready_ && evaluated_ != document_->revision()) {
            evaluated_ = document_->revision();
            try {
                const auto next =
                    resolve_material_source(document_->source(), base_ ? &*base_ : nullptr,
                                            surface_ ? &*surface_->program.surface : nullptr);
                if (update_preview)
                    update_preview({document_->source().asset()},
                                   {next.values, next.textures, surface_}, catalog_);
                resolved_ = next;
                error_.clear();
            } catch (const std::exception& e) {
                report(e.what());
            }
        }
    }
    void draw(SceneDocument& project, bool locked) {
        if (!document_)
            return;
        ui::draft_window_size({920 * ui::interface_scale, 720 * ui::interface_scale});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        const auto title = std::string(dirty() ? "* Material" : "Material") + "###Material";
        bool visible = true;
        if (ImGui::Begin(title.c_str(), &visible)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("material", "Material");
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ui::help("Reusable material source. Its draft and history are independent of the open "
                     "scene.");
            ImGui::BeginDisabled(locked || job_);
            if (ui::button("Save", "Save source, then validate and publish a cooked revision. A "
                                   "failed publication keeps the last-good material."))
                request_save();
            ui::next_text_button("Reload source");
            if (ui::button(
                    "Reload source",
                    "Close through the pending-changes guard and reopen the on-disk source.")) {
                pending_source_ = document_->locator();
                request_close();
            }
            ImGui::EndDisabled();
            const auto draw_fields = [&] {
                if (!document_)
                    return;
                ImGui::BeginDisabled(locked || job_);
                try {
                    fields();
                } catch (const std::exception& e) {
                    report(e.what());
                }
                ImGui::EndDisabled();
            };
            const auto preview = [&](bool stacked) {
                if (document_ && draw_preview)
                    try {
                        draw_preview(stacked);
                    } catch (const std::exception& e) {
                        report(e.what());
                    }
            };
            if (ImGui::GetContentRegionAvail().x >= 760 * ui::interface_scale &&
                ImGui::BeginTable("Material workspace", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, .45f);
                ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, .55f);
                // Reserve only visible status content; a permanent scaled footer
                // otherwise wastes 200 pixels in a clean 200% workspace.
                float footer = !base_ready_ ? ImGui::GetTextLineHeightWithSpacing() : 0.f;
                if (job_)
                    footer += ImGui::GetTextLineHeightWithSpacing() +
                              2 * ImGui::GetFrameHeightWithSpacing();
                if (!error_.empty())
                    footer += ImGui::CalcTextSize(error_.c_str(), nullptr, false,
                                                  std::max(1.f, ImGui::GetContentRegionAvail().x))
                                  .y +
                              ImGui::GetStyle().ItemSpacing.y;
                const auto height = std::max(120.f * ui::interface_scale,
                                             ImGui::GetContentRegionAvail().y - footer -
                                                 ImGui::GetStyle().ItemSpacing.y);
                ImGui::TableNextColumn();
                ImGui::BeginChild("Material properties", {0, height});
                draw_fields();
                ImGui::EndChild();
                ImGui::TableNextColumn();
                ImGui::BeginChild("Material preview", {0, height});
                preview(false);
                ImGui::EndChild();
                ImGui::EndTable();
            } else {
                preview(true);
                draw_fields();
            }
            if (!base_ready_) {
                ImGui::TextUnformatted(base_job_.valid() || !base_started_
                                           ? "Loading material dependencies..."
                                           : "Material dependency unavailable");
                ui::help(
                    "Preparing copied base and Shader revisions on a worker. The previous preview "
                    "stays available.");
            }
            if (job_) {
                for (const auto& job : service_->jobs())
                    if (job.id == job_) {
                        ImGui::TextWrapped("%s", job.stage.c_str());
                        ImGui::ProgressBar(float(job.progress));
                        ui::help("Material validation runs in a bounded CPU job; publication "
                                 "occurs on the project owner.");
                    }
                if (ui::button("Cancel publication", "Keep the saved source and previous published "
                                                     "material; cancel this replacement."))
                    service_->cancel(job_);
            }
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (!visible)
            request_close();
        if (save_ && document_ && !locked && !job_) {
            save_ = false;
            try {
                document_->save();
                auto draft =
                    service_->prepare(document_->locator(), {}, document_->source().asset());
                job_ = service_->submit(
                    std::move(draft),
                    [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
                    [](const auto&, const auto&) {});
                needs_publish_ = true;
                error_.clear();
            } catch (const std::exception& e) {
                report(e.what());
            }
        }
        if (close_ && dirty())
            ImGui::OpenPopup("Pending material changes");
        if (ImGui::BeginPopupModal("Pending material changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!dirty())
                ImGui::CloseCurrentPopup();
            ImGui::TextWrapped(
                "Save and publish this material, discard pending work, or keep editing.");
            ImGui::BeginDisabled(locked || job_);
            if (ui::button("Save", "Save source and continue only after successful publication."))
                request_save();
            ImGui::EndDisabled();
            ui::next_text_button("Discard");
            if (ui::button("Discard",
                           "Discard unsaved source edits and cancel publication. Already saved "
                           "source and prior published assets remain on disk.")) {
                if (job_)
                    service_->cancel(job_);
                ImGui::CloseCurrentPopup();
                finish_close();
            }
            ui::next_text_button("Keep editing");
            if (ui::button("Keep editing",
                           "Cancel the close or project switch and keep this material open.")) {
                close_ = false;
                pending_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
        if (close_ && !dirty())
            finish_close();
    }

  private:
    std::unique_ptr<MaterialDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> pending_source_;
    std::string requested_context_;
    std::optional<MaterialShaderSnapshot> surface_;
    std::optional<ResolvedMaterialSource> base_, resolved_;
    std::uint64_t evaluated_ = 0;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, base_ready_ = false, base_started_ = false;
    bool close_ = false, save_ = false, focus_ = false;
    std::string error_;
    char path_[512] = "Assets/New.material.json";
    std::stop_source cancel_;
    std::future<MaterialSourceContext> base_job_;
    void report(std::string error) {
        error_ = std::move(error);
        if (ui::editor_context) {
            ui::Problem problem{"material", "Error", error_, {}, {}, {}, {}};
            if (document_) {
                problem.asset = document_->source().asset();
                problem.source = path_utf8(document_->locator());
                problem.source_navigation = true;
            }
            ui::editor_context->problems.report(std::move(problem));
        }
    }
    std::string source_context_key() const {
        if (!document_)
            return {};
        const auto& source = document_->source().document;
        return Json{{"base", source.value("base", Json{})},
                    {"shader", source.at("overrides").value("shader", Json{})},
                    {"owns_shader", source.at("overrides").contains("shader")}}
            .dump();
    }
    void invalidate_base() {
        cancel_.request_stop();
        base_.reset();
        surface_.reset();
        base_ready_ = base_started_ = false;
        evaluated_ = 0;
        if (document_)
            requested_context_ = source_context_key();
    }
    void changed() {
        needs_publish_ = true;
        evaluated_ = 0;
    }
    void mutate(std::string label, const std::function<void(Json&)>& fn) {
        try {
            edit(document_->revision(), std::move(label), fn);
        } catch (const std::exception& e) {
            // Keep field failures local: unwinding an open ImGui TreeNode would
            // leave the frame's tree/ID stack unbalanced.
            report(e.what());
        }
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto next = std::make_unique<MaterialDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), material_import_registry(), desktop_texture_target());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    material_source_byte_limit);
        const auto found = catalog->records().find(next->source().asset());
        const bool needs = found == catalog->records().end() ||
                           !found->second.metadata.contains("forge.import") ||
                           found->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(next);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        focus_ = true;
        error_.clear();
        invalidate_base();
    }
    void finish_close() {
        cancel_.request_stop();
        document_.reset();
        resolved_.reset();
        base_.reset();
        surface_.reset();
        close_ = save_ = needs_publish_ = false;
        job_ = 0;
        evaluated_ = 0;
        if (release_preview)
            release_preview();
    }
    void fields();
};
inline void MaterialEditor::fields() {
    const auto source = document_->source();
    const auto& overrides = source.document.at("overrides");
    ui::heading("Material model",
                "Choose a built-in model or a published Shader with a material surface interface.");
    FORGE_UI_PROBE("material:document");
    Json parent = source.base() ? Json(source.base()->id) : Json();
    if (asset_ref_picker(*catalog_, parent, "material", "Base material", false))
        mutate("Change material base", [&](auto& j) { j["base"] = parent; });
    if (ui::button("Revert all overrides",
                   "Remove this material's known override intent and follow its base or shader "
                   "defaults. Source Undo restores it."))
        mutate("Revert material overrides", [](auto& j) {
            for (const auto* key : {"model", "shader", "alpha", "alpha_cutoff", "double_sided",
                                    "depth_test", "depth_write", "parameters", "textures"})
                j["overrides"].erase(key);
        });
    const auto selected_shader = material_shader_reference(source, base_ ? &*base_ : nullptr);
    Json shader_ref = selected_shader ? Json(selected_shader->id) : Json{};
    if (asset_ref_picker(*catalog_, shader_ref, "shader", "Surface Shader", false))
        mutate("Change surface Shader", [&](auto& j) {
            j["version"] = 2;
            j["overrides"]["shader"] = shader_ref;
            j["overrides"]["model"] =
                shader_ref.is_null() ? "forge.gltf.metallic-roughness.v1" : surface_material_model;
        });
    ui::help("Select a published material-surface Shader. Generic stage programs cannot be used as "
             "surfaces. "
             "Clear selects built-in shading; incompatible existing values remain visible errors "
             "until corrected.");
    if (overrides.contains("shader")) {
        if (ui::button("Revert Shader",
                       "Remove only explicit Shader and model selection. Follow the base or "
                       "built-in default; source Undo restores this choice."))
            mutate("Revert surface Shader", [](auto& j) {
                j["overrides"].erase("shader");
                j["overrides"].erase("model");
            });
    }
    const bool custom = bool(selected_shader);
    MaterialData values = resolved_ ? resolved_->values : MaterialData{};
    if (values.model.empty())
        values.model = "forge.gltf.metallic-roughness.v1";
    const auto model = overrides.value("model", values.model);
    constexpr const char* models[]{"forge.gltf.metallic-roughness.v1",
                                   "forge.gltf.specular-glossiness.v1", "forge.gltf.unlit.v1"};
    constexpr const char* names[]{"Metallic / Roughness", "Specular / Glossiness", "Unlit"};
    int selected = 0;
    for (int i = 0; i < 3; ++i)
        if (model == models[i])
            selected = i;
    if (!custom) {
        if (ImGui::Combo("Shader model", &selected, names, 3))
            mutate("Change material model",
                   [&](auto& j) { j["overrides"]["model"] = models[selected]; });
        ui::help(
            "Built-in shading model. Clear the Surface Shader to use this choice. Incompatible "
            "inherited parameters and textures report an error.");
    } else {
        ImGui::TextUnformatted("Custom surface");
        ui::help("Parameters and textures come from the selected Shader interface.");
    }
    const auto revert = [&](const char* key) {
        // The field itself owns its context action. Unowned fields do not consume
        // an entire disabled button row; inherited state remains explicit.
        const bool owns = overrides.contains(key);
        if (ImGui::BeginPopupContextItem(key)) {
            if (ImGui::MenuItem("Revert to inherited / default", nullptr, false, owns))
                mutate(std::string("Revert ") + key, [&](auto& j) { j["overrides"].erase(key); });
            ui::help("Remove only this field's explicit override intent.");
            ImGui::TextUnformatted(owns            ? "Explicit override"
                                   : source.base() ? "Inherited"
                                                   : "Model default");
            ImGui::EndPopup();
        }
        ui::IdScope scope(key);
        if (owns) {
            ui::next_text_button("Revert");
            if (ui::button("Revert", "Remove this field's override and follow the base material or "
                                     "model default again."))
                mutate(std::string("Revert ") + key, [&](auto& j) { j["overrides"].erase(key); });
        } else if (source.base()) {
            ui::next_text_button("Inherited");
            ImGui::TextDisabled("Inherited");
            ui::help("This field follows the base material. Editing it creates an independent "
                     "override, even when the value is equal.");
        }
    };
    revert("model");
    ui::heading("Surface state",
                "Alpha, culling and depth settings affect the actual material pipeline.");
    int alpha = overrides.value("alpha", unsigned(values.alpha));
    if (ImGui::Combo("Alpha", &alpha, "Opaque\0Mask\0Blend\0"))
        mutate("Change alpha mode", [&](auto& j) { j["overrides"]["alpha"] = alpha; });
    ui::help("Opaque writes a solid surface. Mask discards below cutoff. Blend uses sorted "
             "transparent drawing.");
    revert("alpha");
    if (alpha == int(MaterialAlpha::Mask)) {
        float cutoff = overrides.value("alpha_cutoff", values.alpha_cutoff);
        if (ImGui::InputFloat("Alpha cutoff", &cutoff, 0, 0, "%.5g",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            mutate("Change alpha cutoff",
                   [&](auto& j) { j["overrides"]["alpha_cutoff"] = cutoff; });
        ui::help("Fragments below this nonnegative cutoff are discarded. Enter commits one source "
                 "Undo step.");
        revert("alpha_cutoff");
    }
    for (auto [key, label, fallback] :
         {std::tuple{"double_sided", "Two-sided", values.double_sided},
          {"depth_test", "Depth test", values.depth_test},
          {"depth_write", "Depth write", values.depth_write}}) {
        bool value = overrides.value(key, fallback);
        if (ImGui::Checkbox(label, &value))
            mutate(std::string("Change ") + label, [&](auto& j) { j["overrides"][key] = value; });
        ui::help("Explicit material pipeline state. Revert restores inherited state rather than "
                 "guessing a new value.");
        revert(key);
    }
    if (!base_ready_ || (custom && !surface_))
        return;
    MaterialData empty;
    empty.model = model;
    const auto schema = custom ? PbrMaterialProfile{} : prepare_pbr_material(empty);
    const auto defaults =
        custom ? surface_material_defaults(*surface_->program.surface) : schema.values;
    const auto layout =
        custom ? surface_material_layout(*surface_->program.surface) : schema.layout;
    auto effective = custom ? (values.model == surface_material_model ? values : defaults)
                     : values.model == model ? prepare_pbr_material(values).values
                                             : defaults;
    auto parameters = defaults.parameters;
    if (!custom && schema.workflow == PbrWorkflow::MetallicRoughness)
        parameters.try_emplace("attenuationDistance",
                               MaterialParameter{MaterialParameterType::Scalar, {1}});
    const auto label = [](std::string name) {
        std::string out;
        for (char c : name) {
            if (c >= 'A' && c <= 'Z' && !out.empty())
                out += ' ';
            out += c;
        }
        if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
            out[0] -= 'a' - 'A';
        return out;
    };
    ui::heading("Parameters", "Values are linear factors in the selected shader model. Enter "
                              "commits a field. Equal values can remain explicit overrides.");
    const auto group_for = [](std::string_view key) -> std::string_view {
        if (key.starts_with("emissive"))
            return "Emission";
        if (key.starts_with("clearcoat"))
            return "Clearcoat";
        if (key.starts_with("sheen"))
            return "Sheen";
        if (key.starts_with("anisotropy"))
            return "Anisotropy";
        if (key.starts_with("iridescence"))
            return "Iridescence";
        if (key.starts_with("attenuation") || key == "thicknessFactor" ||
            key == "transmissionFactor" || key == "dispersion" || key == "ior")
            return "Transmission and volume";
        if (key.starts_with("specular"))
            return "Specular";
        return "Surface";
    };
    for (const std::string_view group : {"Surface", "Emission", "Specular", "Clearcoat", "Sheen",
                                         "Anisotropy", "Iridescence", "Transmission and volume"}) {
        if ((custom || schema.workflow == PbrWorkflow::Unlit) && group != "Surface")
            continue;
        if (!custom && schema.workflow == PbrWorkflow::SpecularGlossiness && group != "Surface" &&
            group != "Emission" && group != "Specular")
            continue;
        if (!ImGui::CollapsingHeader(group.data(),
                                     group == "Surface" ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            ui::help("Expand this supported material effect's parameters.");
            continue;
        }
        ui::help("Parameters for this supported material effect. Each field retains independent "
                 "override intent.");
        for (const auto& [key, default_parameter] : parameters) {
            if ((custom ? std::string_view("Surface") : group_for(key)) != group)
                continue;
            if (!custom && schema.workflow == PbrWorkflow::Unlit && key != "baseColorFactor")
                continue;
            if (!custom && schema.workflow == PbrWorkflow::SpecularGlossiness &&
                (key == "ior" || key == "dispersion" || key.starts_with("clearcoat") ||
                 key == "metallicFactor"))
                continue;
            ui::IdScope scope(key.c_str());
            const auto text = label(key);
            const bool expanded = ImGui::TreeNode(text.c_str());
            FORGE_UI_PROBE("material:parameter:" + key);
            if (!expanded) {
                ui::help(
                    "Expand to edit this parameter and inspect its independent override intent.");
                continue;
            }
            ui::help(
                "This parameter is part of the selected material model, not a scene component.");
            const bool owns =
                overrides.contains("parameters") && overrides.at("parameters").contains(key);
            ImGui::TextUnformatted(owns            ? "Override"
                                   : source.base() ? "Inherited"
                                                   : "Model default");
            ui::help("Revert removes override intent. Reset explicitly chooses the model default, "
                     "independent of the base.");
            auto parameter = effective.parameters.contains(key) &&
                                     effective.parameters.at(key).type == default_parameter.type
                                 ? effective.parameters.at(key)
                                 : default_parameter;
            if (owns && !overrides.at("parameters").at(key).is_null()) {
                const auto& lanes = overrides.at("parameters").at(key).at("value");
                if (lanes.is_array() && lanes.size() == material_parameter_width(parameter.type))
                    for (unsigned i = 0; i < lanes.size(); ++i)
                        parameter.value[i] = lanes[i].get<float>();
            }
            const auto width = material_parameter_width(parameter.type);
            if (ImGui::InputScalarN("Value", ImGuiDataType_Float, parameter.value.data(),
                                    int(width), nullptr, nullptr, "%.6g",
                                    ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Change " + key, [&](auto& j) {
                    auto& field = j["overrides"]["parameters"][key];
                    if (!field.is_object())
                        field = Json::object();
                    field.update({{"type", unsigned(parameter.type)},
                                  {"value", std::vector<float>(parameter.value.begin(),
                                                               parameter.value.begin() + width)}});
                });
            FORGE_UI_PROBE("material:value:" + key);
            ui::help(
                "Enter commits the typed scalar/vector/linear color. Invalid ranges retain the "
                "previous preview and published material.");
            if (!custom && key == "attenuationDistance" && !effective.parameters.contains(key)) {
                ImGui::TextUnformatted("Currently infinite (model default)");
                ui::help(
                    "Enter a positive distance in metres to enable finite volume attenuation.");
            }
            ImGui::BeginDisabled(!owns);
            if (ui::button("Revert", "Remove this parameter override and follow the base."))
                mutate("Revert " + key, [&](auto& j) { j["overrides"]["parameters"].erase(key); });
            ImGui::EndDisabled();
            ui::next_text_button("Reset to default");
            if (ui::button("Reset to default", "Explicitly use the shader-model default even when "
                                               "the base has another value."))
                mutate("Reset " + key,
                       [&](auto& j) { j["overrides"]["parameters"][key] = nullptr; });
            ImGui::TreePop();
        }
    }
    ui::heading("Texture slots", "Assign compatible Texture assets or drag them from Content. Each "
                                 "slot keeps independent sampling and UV settings.");
    for (const auto& [key, texture_layout] : layout.textures) {
        ui::IdScope scope(key.c_str());
        if (!ImGui::TreeNode(label(key).c_str())) {
            ui::help("Expand to assign or clear this texture and edit its sampling.");
            continue;
        }
        ui::help("Named texture role in the selected material shader.");
        MaterialTextureSlot slot;
        slot.semantic = texture_layout.semantic;
        slot.dimension = texture_layout.dimension;
        if (custom)
            slot = surface_->program.surface->textures.at(key);
        if (values.textures.contains(key) &&
            values.textures.at(key).dimension == texture_layout.dimension &&
            values.textures.at(key).semantic == texture_layout.semantic)
            slot = values.textures.at(key);
        Json ref = resolved_ && resolved_->textures.contains(key)
                       ? Json(resolved_->textures.at(key).id)
                       : Json();
        const bool owns = overrides.contains("textures") && overrides.at("textures").contains(key);
        if (owns) {
            const auto& authored = overrides.at("textures").at(key);
            if (authored.is_null())
                ref = nullptr;
            else {
                ref = authored.at("asset");
                MaterialData projection;
                projection.model = model;
                auto value = material_values_document(projection);
                value["textures"][key] = authored.at("slot");
                slot = material_values_from_document(value).textures.at(key);
            }
        }
        const auto set_slot = [&] {
            try {
                MaterialData projection;
                projection.model = model;
                projection.textures[key] = slot;
                const auto data = material_values_document(projection).at("textures").at(key);
                mutate("Change " + key, [&](auto& j) {
                    auto& field = j["overrides"]["textures"][key];
                    if (ref.is_null())
                        field = nullptr;
                    else {
                        if (!field.is_object())
                            field = Json::object();
                        field["asset"] = ref;
                        if (!field["slot"].is_object())
                            field["slot"] = Json::object();
                        field["slot"].update(data);
                    }
                });
            } catch (const std::exception& e) {
                report(e.what());
            }
        };
        if (asset_ref_picker(*catalog_, ref, "texture", "Texture"))
            set_slot();
        if (!ref.is_null()) {
            bool edited = false;
            if (slot.dimension == TextureDimension::D2 ||
                slot.dimension == TextureDimension::D2Array) {
                edited |= ImGui::InputScalar("UV set", ImGuiDataType_U32, &slot.uv_set, nullptr,
                                             nullptr, "%u", ImGuiInputTextFlags_EnterReturnsTrue);
                ui::help(
                    "Mesh UV channel index. Missing channels keep the last-good draw and report "
                    "an error.");
                edited |= ImGui::InputFloat2("Offset", slot.offset.data(), "%.5g",
                                             ImGuiInputTextFlags_EnterReturnsTrue);
                ui::help("UV offset before sampling; Enter commits.");
                edited |= ImGui::InputFloat2("Scale", slot.scale.data(), "%.5g",
                                             ImGuiInputTextFlags_EnterReturnsTrue);
                ui::help("UV scale can be signed or zero; Enter commits.");
                edited |= ImGui::InputFloat("Rotation (rad)", &slot.rotation, 0, 0, "%.5g",
                                            ImGuiInputTextFlags_EnterReturnsTrue);
                ui::help("UV rotation in radians, applied after scale and before offset.");
            } else {
                ImGui::TextUnformatted("Coordinates supplied by the Shader");
                ui::help("Cube and volume sampling uses explicit Shader coordinates. Planar UV "
                         "overrides are not applied.");
            }
            for (auto [name, mode] : {std::pair{"Wrap U", &slot.sampler.u},
                                      {"Wrap V", &slot.sampler.v},
                                      {"Wrap W", &slot.sampler.w}}) {
                int selected_wrap = int(*mode);
                if (ImGui::Combo(name, &selected_wrap,
                                 "Repeat\0Mirror repeat\0Clamp edge\0Clamp border\0")) {
                    *mode = TextureWrap(selected_wrap);
                    edited = true;
                }
                ui::help("Sampler addressing for this material slot.");
            }
            for (auto [name, filter] : {std::pair{"Minification", &slot.sampler.min},
                                        {"Magnification", &slot.sampler.mag},
                                        {"Mip filtering", &slot.sampler.mip}}) {
                int selected_filter = int(*filter);
                if (ImGui::Combo(name, &selected_filter, "Nearest\0Linear\0")) {
                    *filter = TextureFilter(selected_filter);
                    edited = true;
                }
                ui::help("Choose nearest or linear sampling for this slot. Anisotropic sampling "
                         "requires all three filters to be linear.");
            }
            edited |=
                ImGui::InputScalar("Anisotropy", ImGuiDataType_U32, &slot.sampler.anisotropy,
                                   nullptr, nullptr, "%u", ImGuiInputTextFlags_EnterReturnsTrue);
            ui::help("1 disables anisotropy. Higher values request sharper oblique sampling with "
                     "linear filters. The active device's limit is checked before rendering.");
            edited |= ImGui::InputFloat("LOD bias", &slot.sampler.lod_bias, 0, 0, "%.4g",
                                        ImGuiInputTextFlags_EnterReturnsTrue);
            ui::help("Offset the sampled mip level; invalid device/profile bounds are diagnosed.");
            edited |= ImGui::InputFloat("Minimum LOD", &slot.sampler.min_lod, 0, 0, "%.4g",
                                        ImGuiInputTextFlags_EnterReturnsTrue);
            ui::help("Minimum allowed mip level for this texture slot.");
            edited |= ImGui::InputFloat("Maximum LOD", &slot.sampler.max_lod, 0, 0, "%.4g",
                                        ImGuiInputTextFlags_EnterReturnsTrue);
            ui::help("Maximum allowed mip level; must be at least the minimum.");
            if (slot.sampler.u == TextureWrap::ClampBorder ||
                slot.sampler.v == TextureWrap::ClampBorder ||
                slot.sampler.w == TextureWrap::ClampBorder) {
                edited |= ImGui::InputFloat4("Border RGBA", slot.sampler.border.data(), "%.4g",
                                             ImGuiInputTextFlags_EnterReturnsTrue);
                ui::help("Linear border color sampled outside the texture when Clamp border is "
                         "selected.");
            }
            if (edited)
                set_slot();
        }
        ImGui::BeginDisabled(!owns);
        if (ui::button("Revert",
                       "Remove this texture override or explicit clear and follow the base again."))
            mutate("Revert " + key, [&](auto& j) { j["overrides"]["textures"].erase(key); });
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
}
} // namespace forge
