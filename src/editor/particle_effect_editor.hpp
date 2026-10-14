#pragma once
#include "../particle_effect_authoring.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>
namespace forge {
class ParticleEffectEditor {
  public:
    std::function<void(const ParticleEffectSource&, double, std::shared_ptr<const AssetCatalog>)>
        draw_preview;
    std::function<void()> release_preview;
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const ParticleEffectDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_; }
    double preview_time() const { return time_; }
    bool preview_playing() const { return playing_; }
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
        } catch (const std::exception& error) {
            error_ = error.what();
            open_error_ = true;
        }
    }
    void catalog_changed(std::shared_ptr<const AssetCatalog> catalog) {
        catalog_ = std::move(catalog);
    }
    void source_published(SceneDocument& project, AssetId id) {
        if (document_ && document_->source().asset() == id && !dirty())
            load(project, document_->locator());
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New particle effect...",
                       "Create a reusable CPU mesh effect in the Particle Effect document."))
            ImGui::OpenPopup("New particle effect");
        FORGE_UI_PROBE("particles:new");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New particle effect", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("particles:new_path");
            ui::help("Choose a new .particles.json file inside the project. Existing files are "
                     "protected.");
            ImGui::BeginDisabled(locked || dirty());
            if (ui::button("Create",
                           "Create original default values using the engine Cube mesh.")) {
                try {
                    auto created = ParticleEffectDocument::create(project.writer_guard(),
                                                                  std::filesystem::u8path(path_));
                    const auto source = created->locator();
                    created.reset();
                    load(project, source);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            FORGE_UI_PROBE("particles:create");
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
                    catalog_ = published_ =
                        std::make_shared<const AssetCatalog>(result.publication->catalog);
                    needs_publish_ = false;
                    error_.clear();
                    message = "Particle Effect saved and published.";
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
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Particle Effect###Particle Effect", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close the unavailable source document."))
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
        const bool expanded = ImGui::Begin("Particle Effect###Particle Effect", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("particle_effect",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::TextWrapped(
                "CPU mesh particles move in emitter-local space. Scene and exported games use the "
                "same cooked effect. Press Enter to commit numeric fields.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button(
                    "Save / Publish",
                    "Save and cook; failed publication retains the selected working revision."))
                request_save();
            FORGE_UI_PROBE("particles:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo the effect source edit."))
                undo();
            FORGE_UI_PROBE("particles:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo the effect source edit."))
                redo();
            FORGE_UI_PROBE("particles:redo");
            ImGui::EndDisabled();
            const auto data = document_->source().compiled();
            Json mesh = data.mesh, material = data.material.id ? Json(data.material) : Json();
            if (asset_ref_picker(*catalog_, mesh, MeshAsset::type, "Mesh", false))
                set("mesh", mesh);
            FORGE_UI_PROBE("particles:mesh");
            if (asset_ref_picker(*catalog_, material, MaterialAsset::type, "Material", true)) {
                mutate("Set particle material", [&](auto& source) {
                    if (material.is_null())
                        source.erase("material");
                    else
                        source["material"] = material;
                });
            }
            FORGE_UI_PROBE("particles:material");
            ui::help("One static triangle part, one LOD, one slot. Built-in opaque/masked "
                     "materials; Blend, transmission, custom Shader surfaces, skins and morphs are "
                     "unsupported. Empty material follows the mesh.");
            ImGui::SeparatorText("Emission");
            int emission = int(data.emission);
            if (ImGui::Combo("Mode", &emission, "Continuous\0Initial burst\0"))
                set("emission", emission ? "burst" : "continuous");
            FORGE_UI_PROBE("particles:emission");
            count("Capacity", "capacity", data.capacity);
            if (data.emission == ParticleEmission::Burst)
                count("Burst count", "burst_count", data.burst_count);
            else
                number("Particles / second", "spawn_rate", data.spawn_rate);
            number("Lifetime (seconds)", "lifetime", data.lifetime);
            ui::help("Capacity: 1–1024 per effect. Continuous rate × lifetime must fit. Scene "
                     "limits: 64 enabled emitters, 8192 reserved instances.");
            ImGui::SeparatorText("Local motion");
            vector("Direction", "direction", data.direction);
            number("Cone half-angle (degrees)", "cone_angle", data.cone_angle);
            number("Minimum speed (m/s)", "speed_min", data.speed_min);
            number("Maximum speed (m/s)", "speed_max", data.speed_max);
            vector("Acceleration (m/s²)", "acceleration", data.acceleration);
            ImGui::SeparatorText("Over lifetime");
            number("Start size", "size_start", data.size_start);
            number("End size", "size_end", data.size_end);
            vector("Start color (linear RGB)", "color_start", data.color_start);
            vector("End color (linear RGB)", "color_end", data.color_end);
            ImGui::BeginDisabled(needs_publish_ || document_->dirty() || !ui::editor_context ||
                                 ui::editor_context->selection.entity().empty());
            if (ui::button("Assign to selected Particle Emitter",
                           "Select an object with Particle Emitter; assign this published effect "
                           "in one scene Undo step.")) {
                try {
                    const auto id = ui::editor_context->selection.entity();
                    const auto actor = scene.entity(id);
                    if (!actor || !actor.has<ParticleEmitter>())
                        throw std::runtime_error("Select a Particle Emitter in Hierarchy.");
                    authoring_command(scene, "property.set",
                                      {{"entity", id},
                                       {"component", "forge.particle_emitter"},
                                       {"field", "effect"},
                                       {"value", document_->source().asset()}});
                    error_.clear();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            FORGE_UI_PROBE("particles:attach");
            ImGui::EndDisabled();
            ImGui::SeparatorText("Isolated preview");
            if (ui::button(playing_ ? "Pause preview" : "Play preview",
                           "Control this effect preview only; gameplay time and scene values are "
                           "unchanged."))
                playing_ = !playing_;
            FORGE_UI_PROBE("particles:preview_play");
            ImGui::SameLine();
            if (ui::button("Reset preview", "Return to birth time zero and pause this preview.")) {
                time_ = 0;
                playing_ = false;
            }
            FORGE_UI_PROBE("particles:preview_reset");
            double seek = time_;
            if (ImGui::InputDouble("Preview seconds", &seek, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (std::isfinite(seek) && seek >= 0 && seek <= 120) {
                    time_ = seek;
                    playing_ = false;
                    error_.clear();
                } else
                    error_ = "Preview time must be between 0 and 120 seconds.";
            }
            FORGE_UI_PROBE("particles:preview_time");
            ImGui::EndDisabled();
            if (locked)
                playing_ = false;
            if (playing_) {
                time_ =
                    std::min(120., time_ + std::clamp(double(ImGui::GetIO().DeltaTime), 0., .25));
                if (time_ == 120)
                    playing_ = false;
            }
            if (draw_preview)
                draw_preview(document_->source(), time_, catalog_);
            if (pending())
                ImGui::TextUnformatted("Publishing Particle Effect...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source needs Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        // Open and consume the modal under the same document ID scope, including
        // a collapsed window whose close button was pressed.
        if (close_ && document_ && !ImGui::IsPopupOpen("Unsaved Particle Effect"))
            ImGui::OpenPopup("Unsaved Particle Effect");
        if (ImGui::BeginPopupModal("Unsaved Particle Effect", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save / Publish changes before closing? Discard closes unsaved "
                               "edits; already saved source files remain on disk.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Publish, then close after success.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("particles:close_save");
            ImGui::SameLine();
            if (ui::button("Discard",
                           "Close this draft and retain the last selected runtime revision.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("particles:close_discard");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Keep the effect document open.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("particles:close_cancel");
            ImGui::EndPopup();
        }
        ImGui::End();
    }

  private:
    std::unique_ptr<ParticleEffectDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_source_;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false,
         playing_ = false;
    double time_ = .5;
    char path_[256] = "Assets/NewEffect.particles.json";
    std::string error_;
    void finish_close() {
        if (release_preview)
            release_preview();
        document_.reset();
        service_.reset();
        catalog_.reset();
        job_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = playing_ = false;
        time_ = .5;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto document = std::make_unique<ParticleEffectDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), particle_effect_import_registry(), desktop_texture_target());
        const auto found = catalog->records().find(document->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    particle_effect_byte_limit);
        const bool needs = found == catalog->records().end() ||
                           !found->second.metadata.contains("forge.import") ||
                           found->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(document);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        error_.clear();
    }
    void changed() {
        // The source document owns draft dirtiness. Undo to the saved, selected
        // source must be clean; needs_publish_ describes saved publication state.
        error_.clear();
    }
    template <class F> void mutate(std::string label, F action) {
        try {
            document_->edit(document_->revision(), std::move(label), action);
            changed();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    void set(const char* name, const Json& value) {
        mutate(std::string("Set particle ") + name, [&](auto& source) { source[name] = value; });
    }
    void number(const char* label, const char* name, double value) {
        if (ImGui::InputDouble(label, &value, 0, 0, "%.5g", ImGuiInputTextFlags_EnterReturnsTrue))
            set(name, value);
        FORGE_UI_PROBE(std::string("particles:") + name);
    }
    void count(const char* label, const char* name, std::uint32_t value) {
        if (ImGui::InputScalar(label, ImGuiDataType_U32, &value, nullptr, nullptr, "%u",
                               ImGuiInputTextFlags_EnterReturnsTrue))
            set(name, value);
        FORGE_UI_PROBE(std::string("particles:") + name);
    }
    template <class T> void vector(const char* label, const char* name, std::array<T, 3> value) {
        if (ImGui::InputScalarN(
                label, std::is_same_v<T, float> ? ImGuiDataType_Float : ImGuiDataType_Double,
                value.data(), 3, nullptr, nullptr, "%.5g", ImGuiInputTextFlags_EnterReturnsTrue))
            set(name, value);
        FORGE_UI_PROBE(std::string("particles:") + name);
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_particle_effect_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
        } catch (const std::exception& error) {
            needs_publish_ = true;
            close_ = false;
            error_ = error.what();
        }
    }
};
} // namespace forge
