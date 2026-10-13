#pragma once
#include "../animation_blend_space_authoring.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <functional>
namespace forge {
class AnimationBlendSpaceEditor {
  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const AnimationBlendSpaceDocument* document() const { return document_.get(); }
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
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
            open_error_ = true;
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New animation blend space...",
                       "Create compatible clips along one numeric animation axis."))
            ImGui::OpenPopup("New animation blend space");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New animation blend space", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create the source document; choose an imported skeleton and clips.")) {
                try {
                    auto created = AnimationBlendSpaceDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_));
                    const auto source = created->locator();
                    created.reset();
                    load(project, source);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a file."))
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
                    published_ = std::make_shared<const AssetCatalog>(result.publication->catalog);
                    catalog_ = published_;
                    needs_publish_ = false;
                    error_.clear();
                    message = "Animation blend space saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument& project, Scene& scene, bool locked,
              const std::function<void()>& preview = {}) {
        (void)project;
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Animation Blend Space###Animation Blend Space", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close the failed source document."))
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
        const bool expanded =
            ImGui::Begin("Animation Blend Space###Animation Blend Space", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_blend_space",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped(
                "Place clips along one numeric axis. Animator Blend position mixes the two "
                "neighbors at the same cycle phase; positions beyond the ends clamp.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button(
                    "Save / Publish",
                    "Save and cook. Failed publication retains the last working blend space."))
                request_save();
            FORGE_UI_PROBE("animation_blend_space:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo the blend-space source edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo the blend-space source edit."))
                redo();
            ImGui::EndDisabled();
            auto draft = document_->source().document;
            Json skeleton = draft.at("skeleton");
            if (asset_ref_picker(*catalog_, skeleton, SkeletonAsset::type, "Skeleton", false))
                mutate("Choose blend-space skeleton", [&](Json& d) { d["skeleton"] = skeleton; });
            double cycle = draft.at("cycle_seconds");
            if (ImGui::InputDouble("Cycle (seconds)", &cycle, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Set blend-space cycle", [&](Json& d) { d["cycle_seconds"] = cycle; });
            FORGE_UI_PROBE("animation_blend_space:cycle");
            ui::help("All clips share this cycle; their individual durations may differ. Press "
                     "Enter to commit.");
            ImGui::SeparatorText("Samples on the axis");
            axis(draft);
            ImGui::BeginDisabled(draft.at("samples").size() >= 64);
            if (ui::button("Add sample",
                           "Add a clip point with a unique position; choose its clip below.")) {
                const auto id = AssetId::generate();
                mutate("Add blend-space sample", [&](Json& d) {
                    double position = 0;
                    while (std::any_of(
                        d["samples"].begin(), d["samples"].end(), [&](const auto& sample) {
                            return sample.at("position").template get<double>() == position;
                        }))
                        ++position;
                    d["samples"].push_back({{"id", id}, {"clip", nullptr}, {"position", position}});
                });
                selected_ = id;
            }
            FORGE_UI_PROBE("animation_blend_space:add_sample");
            ImGui::EndDisabled();
            if (ImGui::BeginChild("Blend samples", {0, 190}, ImGuiChildFlags_Borders)) {
                ImGuiListClipper clipper;
                clipper.Begin(int(draft.at("samples").size()),
                              ImGui::GetTextLineHeightWithSpacing());
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const auto& sample = draft.at("samples")[std::size_t(i)];
                        const auto id = sample.at("id").get<AssetId>();
                        auto label = std::to_string(sample.at("position").get<double>()) + " | ";
                        if (sample.at("clip").is_null())
                            label += "Choose a clip";
                        else {
                            const auto found =
                                catalog_->records().find(sample.at("clip").get<AssetId>());
                            label += found == catalog_->records().end()
                                         ? "Missing clip"
                                         : asset_display(found->second);
                            if (found != catalog_->records().end() &&
                                !asset_picker_scope_matches(found->second, owner_))
                                label += " (different model)";
                        }
                        if (ImGui::Selectable((label + "##" + id.str()).c_str(), selected_ == id))
                            selected_ = id;
                        FORGE_UI_PROBE("animation_blend_space:sample:" + id.str());
                    }
            }
            ImGui::EndChild();
            for (const auto& sample : draft.at("samples"))
                if (selected_ && sample.at("id") == Json(selected_)) {
                    double position = sample.at("position");
                    if (ImGui::InputDouble("Sample position", &position, 0, 0, "%.3f",
                                           ImGuiInputTextFlags_EnterReturnsTrue))
                        mutate_entry("Move blend-space sample", "position", position);
                    FORGE_UI_PROBE("animation_blend_space:position");
                    auto clip = sample.at("clip");
                    if (asset_ref_picker(*catalog_, clip, AnimationClipAsset::type, "Sample clip",
                                         false, owner_))
                        mutate_entry("Choose blend-space clip", "clip", clip);
                    if (ui::button(
                            "Remove sample",
                            "Remove this clip point; neighboring samples fill the interval.")) {
                        mutate("Remove blend-space sample", [&](Json& d) {
                            std::erase_if(
                                d["samples"].get_ref<Json::array_t&>(),
                                [&](const auto& e) { return e.at("id") == Json(selected_); });
                        });
                        selected_ = {};
                    }
                    FORGE_UI_PROBE("animation_blend_space:remove_sample");
                    break;
                }
            ImGui::SeparatorText("Scene object");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty());
            if (ui::button(
                    "Attach to selected Animator",
                    "Assign this blend space and clear incompatible modes in one Scene Undo step."))
                attach(scene);
            FORGE_UI_PROBE("animation_blend_space:attach");
            ImGui::SameLine();
            if (preview &&
                ui::button("Open Animation Preview", "Preview the selected object's numeric axis "
                                                     "without changing its authored settings."))
                preview();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing animation blend space...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending animation blend space changes");
        if (ImGui::BeginPopupModal("Pending animation blend space changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save and publish, discard the draft, or keep editing?");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Publish before closing.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::button("Discard", "Close without saving this draft.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Keep editing", "Cancel closing.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    std::unique_ptr<AnimationBlendSpaceDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_source_;
    std::optional<AssetId> owner_;
    AssetJobId job_ = 0;
    AssetId selected_;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    char path_[256] = "Assets/Motion.blendspace.json";
    std::string error_;
    void axis(const Json& draft) {
        const auto count = draft.at("samples").size();
        if (!count)
            return;
        double minimum = 1000000, maximum = -1000000;
        for (const auto& sample : draft.at("samples")) {
            minimum = std::min(minimum, sample.at("position").get<double>());
            maximum = std::max(maximum, sample.at("position").get<double>());
        }
        const auto at = ImGui::GetCursorScreenPos();
        const float width = std::max(1.f, ImGui::GetContentRegionAvail().x - 24);
        auto* draw = ImGui::GetWindowDrawList();
        const float y = at.y + 22;
        draw->AddLine({at.x + 12, y}, {at.x + 12 + width, y}, IM_COL32(95, 150, 180, 255), 2);
        for (const auto& sample : draft.at("samples")) {
            const double position = sample.at("position");
            const float fraction =
                maximum > minimum ? float((position - minimum) / (maximum - minimum)) : .5f;
            const float x = at.x + 12 + width * fraction;
            const bool selected = selected_ && sample.at("id") == Json(selected_);
            draw->AddCircleFilled({x, y}, selected ? 6 : 4,
                                  selected ? IM_COL32(245, 190, 60, 255)
                                           : IM_COL32(120, 210, 240, 255));
        }
        ImGui::Dummy({width + 24, 42});
        ImGui::Text("Axis: %.3f to %.3f", minimum, maximum);
    }
    void finish_close() {
        document_.reset();
        service_.reset();
        catalog_.reset();
        owner_ = AssetId{};
        job_ = 0;
        selected_ = {};
        needs_publish_ = save_ = close_ = open_error_ = false;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto doc = std::make_unique<AnimationBlendSpaceDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(project.writer_guard(),
                                                            animation_blend_space_import_registry(),
                                                            desktop_texture_target());
        const auto at = catalog->records().find(doc->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    animation_blend_space_byte_limit);
        const bool needs = at == catalog->records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(doc);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        error_.clear();
        refresh_owner();
        if (!document_->source().document.at("samples").empty())
            selected_ = document_->source().document.at("samples").front().at("id").get<AssetId>();
    }
    void refresh_owner() {
        owner_ = AssetId{};
        const auto& skeleton = document_->source().document.at("skeleton");
        if (skeleton.is_null())
            return;
        try {
            const auto id = skeleton.get<AssetId>();
            const auto resolved = catalog_->resolve(id, SkeletonAsset::type);
            if (resolved.state != AssetState::Available || !resolved.record->subasset)
                throw std::runtime_error("Choose a current skeleton from an imported model");
            owner_ = resolved.record->subasset->owner;
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void changed() {
        needs_publish_ = true;
        refresh_owner();
    }
    template <class F> void mutate(std::string label, F edit) {
        try {
            document_->edit(document_->revision(), std::move(label), edit);
            error_.clear();
            changed();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void mutate_entry(std::string label, const char* field, const Json& value) {
        mutate(std::move(label), [&](Json& d) {
            for (auto& row : d["samples"])
                if (row.at("id") == Json(selected_))
                    row[field] = value;
        });
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& c, const auto& p, const auto&) {
                    prepare_animation_blend_space_publication(c, p);
                },
                [](const auto&, const auto&) {});
        } catch (const std::exception& e) {
            needs_publish_ = true;
            close_ = false;
            error_ = e.what();
        }
    }
    void attach(Scene& scene) {
        try {
            if (!ui::editor_context)
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto id = ui::editor_context->selection.entity();
            auto actor = scene.entity(id);
            if (!actor || !actor.has<Animator>())
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto& config = actor.get<Animator>();
            if (config.skeleton.id &&
                Json(config.skeleton.id) != document_->source().document.at("skeleton"))
                throw std::runtime_error("Choose an Animator with this imported skeleton");
            const auto compiled = document_->source().compiled();
            Json values{{"blend_weight", 0},
                        {"blend_root", nullptr},
                        {"blend_mask", nullptr},
                        {"blend_mode", 0},
                        {"blend_clip", nullptr},
                        {"states", nullptr},
                        {"state", 0},
                        {"clip", nullptr},
                        {"skeleton", compiled.skeleton},
                        {"blend_space", compiled.asset},
                        {"blend_position", float(compiled.samples.front().position)}};
            auto commands = Json::array();
            for (const char* field :
                 {"blend_weight", "blend_root", "blend_mask", "blend_mode", "blend_clip", "states",
                  "state", "clip", "skeleton", "blend_space", "blend_position"})
                commands.push_back({{"operation", "property.set"},
                                    {"arguments",
                                     {{"entity", id},
                                      {"component", "forge.animator"},
                                      {"field", field},
                                      {"value", values.at(field)}}}});
            apply_authoring(scene, commands, scene.revision());
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
