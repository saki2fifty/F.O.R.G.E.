#pragma once
#include "../model_authoring.hpp"
#include "../model_importer.hpp"
#include "../model_placement.hpp"
#include "../texture_authoring.hpp"
#include "asset_import_editor.hpp"
#include "property_drawer.hpp"
#include <future>
namespace forge {
class ModelImportEditor : public AssetImportEditor {
  public:
#ifdef FORGE_UI_FIXTURE
    bool fixture_open_variant = false;
#endif
    std::function<bool()> placement_allowed;
    std::function<void(SceneDocument&, bool)> draw_preview;
    ModelImportEditor(std::filesystem::path worker, std::filesystem::path converter, Scene& scene,
                      ui::EditorSelection& selection)
        : AssetImportEditor(profile(std::move(worker), std::move(converter))), scene_(scene),
          selection_(selection) {
        draw_extension = [this](SceneDocument& document, bool locked) {
            if (draw_preview)
                draw_preview(document, dirty());
            draw_model(document, locked);
        };
    }
    ~ModelImportEditor() { cancel_.request_stop(); }
    bool placement_ready() const { return selected_.has_value() && !job_.valid(); }
    EntityId place(SceneDocument& document,
                   const asset_detail::ModelPlacementOptions& options = {}) {
        if (placement_allowed && !placement_allowed())
            throw std::runtime_error(
                "Finish Play or the active scene operation before placing a model.");
        if (!is_open() || !placement_ready() || dirty() || pending() ||
            document.project() != project_)
            throw std::runtime_error(
                "Finish importing and loading the selected model before placement.");
        document.check_ownership();
        const auto catalog = AssetCatalog::open_project(document.project());
        const auto candidate = asset_detail::prepare_model_placement(*selected_, scene_.asset_id(),
                                                                     scene_.revision(), options);
        const auto root = asset_detail::instantiate_model(scene_, catalog, candidate);
        selection_.select_entity(root.str());
        return root;
    }
    void poll(SceneDocument& document, std::string& message) {
        AssetImportEditor::poll(document, message);
        const bool changed = project_ != document.project() ||
                             generation_ != selection_generation() || asset_ != selected_asset() ||
                             !is_open();
        if (changed) {
            cancel_.request_stop();
            selected_.reset();
            project_ = document.project();
            generation_ = selection_generation();
            asset_ = selected_asset();
            started_ = false;
            error_.clear();
            source_scene_ = -1;
            clip_ = {};
            variant_ = {};
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto selected = job_.get();
                if (!cancel_.stop_requested() && is_open() && job_generation_ == generation_ &&
                    selected.owner == asset_)
                    selected_ = std::move(selected);
            } catch (const std::exception& e) {
                if (!cancel_.stop_requested())
                    error_ = e.what();
            }
        }
        if (is_open() && asset_ && !started_ && !job_.valid()) {
            started_ = true;
            try {
                auto catalog = AssetCatalog::open_project(project_);
                if (!catalog.records().contains(asset_))
                    return; // Import first.
                cancel_ = std::stop_source{};
                job_generation_ = generation_;
                const auto project = project_;
                const auto asset = asset_;
                job_ = std::async(std::launch::async, [project, asset, catalog = std::move(catalog),
                                                       stop = cancel_.get_token()] {
                    auto selected =
                        asset_detail::load_model_selection(project, catalog, asset, stop);
                    // Placement needs validated metadata/bindings, not retained geometry bytes.
                    selected.artifact.reset();
                    selected.file_indices.clear();
                    return selected;
                });
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
    }

  private:
    Scene& scene_;
    ui::EditorSelection& selection_;
    std::filesystem::path project_;
    AssetId asset_, clip_, variant_;
    std::uint64_t generation_ = 0, job_generation_ = 0;
    bool started_ = false;
    int source_scene_ = -1;
    char name_[512] = "Model";
    std::string error_;
    std::optional<asset_detail::ModelSelection> selected_;
    std::stop_source cancel_;
    std::future<asset_detail::ModelSelection> job_;
    static ImportEditorProfile profile(std::filesystem::path worker,
                                       std::filesystem::path converter) {
        return {"model_import",
                "Model import",
                "Assets/model.glb",
                "Project-contained .gltf or .glb with contained buffers/images. Native preparation "
                "runs in a separate worker; animation uses the packaged official Ozz converter.",
                "Import one complete model family. Its meshes, materials, textures and animation "
                "retain typed logical asset identities.",
                "Import model...",
                desktop_texture_target(),
                [worker = std::move(worker), converter = std::move(converter)] {
                    auto registry = std::make_shared<AssetImporterRegistry>();
                    registry->add(asset_detail::model_importer(worker, converter));
                    registry->seal();
                    return registry;
                },
                [](auto& candidate, const auto& plan, const auto& catalog, auto decisions) {
                    prepare_model_publication(candidate, plan, catalog, decisions);
                }};
    }
    void draw_model(SceneDocument& document, bool locked) {
        locked = locked || (placement_allowed && !placement_allowed());
        ui::heading(
            "Place in scene",
            "Creates ordinary authored entities using the validated imported hierarchy. One scene "
            "Undo removes the whole placement; import publication has its own ownership.");
        if (job_.valid()) {
            ImGui::TextUnformatted("Loading model metadata...");
            ui::help(
                "Validates the selected immutable model revision without blocking the editor.");
            return;
        }
        if (!error_.empty())
            ui::field_error(error_);
        if (!selected_) {
            ImGui::TextWrapped("Import this source successfully before placing it.");
            ui::help("A failed import does not create entities or replace the previous usable "
                     "model family.");
            return;
        }
        const auto& hierarchy = selected_->index.hierarchy;
        const auto& scenes = hierarchy.at("scenes");
        const auto& nodes = hierarchy.at("nodes");
        ImGui::Text("%zu source nodes; %zu source scenes", nodes.size(), scenes.size());
        ui::help("Source nodes become ordinary entities. Meshes, materials, skeletons and clips "
                 "remain assets referenced by components.");
        ImGui::BeginDisabled(locked);
        ui::property_label_row("Name", "Name for the new model root in Hierarchy.");
        ImGui::InputText("##model_name", name_, sizeof(name_));
        ui::help("The root and all children receive new EntityIds for each placement.");
        const auto preview = source_scene_ < 0 ? std::string("Source default")
                                               : "Scene " + std::to_string(source_scene_ + 1);
        ui::property_label_row("Source scene", "Choose which glTF scene to instantiate; a model "
                                               "can contain several independent scene roots.");
        if (ImGui::BeginCombo("##source_scene", preview.c_str())) {
            if (ImGui::Selectable("Source default", source_scene_ < 0))
                source_scene_ = -1;
            for (unsigned i = 0; i < scenes.size(); ++i)
                if (ImGui::Selectable(("Scene " + std::to_string(i + 1)).c_str(),
                                      source_scene_ == int(i)))
                    source_scene_ = int(i);
            ImGui::EndCombo();
        }
        ui::help("If the source has several scenes and no default, choose one explicitly. A source "
                 "with no scenes places its root nodes.");
        ui::property_label_row(
            "Animation", "Choose a clip explicitly, or place the source pose without autoplay.");
        const auto clip_name = clip_ ? selected_->member(clip_).identity.display_name
                                     : std::string("None — source pose");
        if (ImGui::BeginCombo("##clip", clip_name.c_str())) {
            if (ImGui::Selectable("None — source pose", !clip_))
                clip_ = {};
            if (hierarchy.contains("animation"))
                for (const auto& address : hierarchy.at("animation").at("clips")) {
                    const auto id = selected_->bindings.at(address.get<std::string>());
                    const auto& member = selected_->member(id);
                    ImGui::PushID(id.str().c_str());
                    if (ImGui::Selectable(member.identity.display_name.c_str(), clip_ == id))
                        clip_ = id;
                    ui::help("Add an Animator using this model's matching skeleton and clip. "
                             "Playback begins when you press Play.");
                    ImGui::PopID();
                }
            ImGui::EndCombo();
        }
        ui::help("No clip is chosen automatically. Static skin and morph defaults remain available "
                 "without an Animator.");
        ui::property_label_row("Material variant",
                               "Choose one imported material set for every placed mesh. Unmapped "
                               "surfaces use their original material.");
        const auto variant_name = variant_ ? selected_->member(variant_).identity.display_name
                                           : std::string("Default materials");
#ifdef FORGE_UI_FIXTURE
        if (fixture_open_variant) {
            ImGui::SetScrollHereY(.5f);
            ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID("##material_variant")),
                               ImGuiPopupFlags_None);
        }
#endif
        if (ImGui::BeginCombo("##material_variant", variant_name.c_str())) {
#ifdef FORGE_UI_FIXTURE
            fixture_open_variant = false;
#endif
            if (ImGui::Selectable("Default materials", !variant_))
                variant_ = {};
            for (const auto& member : selected_->index.members) {
                if (!member.material_variant)
                    continue;
                const auto id = selected_->bindings.at(member.identity.address);
                ui::IdScope scope(id.str().c_str());
                if (ImGui::Selectable(member.identity.display_name.c_str(), variant_ == id))
                    variant_ = id;
                ui::help("This is a stable imported material-set asset. Switching it reuses the "
                         "same geometry.");
            }
            ImGui::EndCombo();
        }
        ui::help("Change the placed model later from its root Inspector, or override individual "
                 "mesh selections. Source materials stay unchanged.");
        const bool needs_scene =
            source_scene_ < 0 && scenes.size() > 1 && hierarchy.at("default_scene").is_null();
        if (needs_scene)
            ui::field_error(
                "This model has no default scene. Choose a source scene before placing it.");
        ImGui::BeginDisabled(needs_scene || name_[0] == '\0');
        if (ui::button("Place model",
                       "Create the selected source scene at the world origin. This is one scene "
                       "Undo operation and does not modify the imported asset.")) {
            try {
                asset_detail::ModelPlacementOptions options;
                options.name = name_;
                options.clip = {clip_};
                options.material_variant = {variant_};
                if (source_scene_ >= 0)
                    options.source_scene = std::uint32_t(source_scene_);
                (void)place(document, options);
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                ui::report_error("model_placement", error_);
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (ImGui::TreeNode("Source hierarchy")) {
            // Flat parent-labelled list avoids UI recursion proportional to source depth.
            ImGuiListClipper clipper;
            clipper.Begin(int(nodes.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& node = nodes[i];
                    const auto parent =
                        node.at("parent").is_null()
                            ? std::string("root")
                            : "parent " + std::to_string(node.at("parent").get<unsigned>() + 1);
                    ImGui::Text("%d  %s  (%s)", i + 1,
                                node.value("name", std::string("Node")).c_str(), parent.c_str());
                    ui::help("Imported source structure; edit the placed entities from Hierarchy "
                             "and Inspector.");
                }
            ImGui::TreePop();
        }
        ui::help("Inspect source node names and parent indices without creating a second editable "
                 "hierarchy.");
    }
};
} // namespace forge
