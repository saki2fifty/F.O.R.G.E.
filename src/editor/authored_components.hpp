#pragma once
#include "../authored_history.hpp"
#include "../authored_inspection.hpp"
#include "../authored_migration_document.hpp"
#include "../bounded_json.hpp"
#include "prefabs.hpp"
#include <future>
#include <misc/cpp/imgui_stdlib.h>
namespace forge {
// Editor-only task/review state. Native project code runs in the supervised SDK
// process; completed metadata and document candidates publish on the owning thread.
class AuthoredComponents {
  public:
#ifdef FORGE_UI_FIXTURE
    bool fixture_open_migration = false;
#endif
    ~AuthoredComponents() { cancel_.request_stop(); }
    bool busy() const { return job_.valid(); }
    void project_changed(Scene& scene, SceneDocument& project) {
        if (root_ == project.project())
            return;
        cancel_.request_stop();
        root_ = project.project();
        active_ = Json();
        history_.reset();
        migration_.reset();
        returned_ = Json();
        migration_open_ = false;
        status_ = "Inspect the project's opted-in components before authoring them.";
        error_.clear();
        // Copied types from another project must never remain in Add Component.
        scene.publish_component_schemas(Json::array());
    }
    void inspect(SceneDocument& project, const std::filesystem::path& runtime) {
        if (busy())
            throw std::runtime_error("A component task is already running");
        project.check_ownership();
        auto history = std::make_unique<detail::AuthoredHistory>(project.project());
        const auto settings = project.settings().document();
        const auto expected = fingerprint(settings);
        if (!std::filesystem::is_regular_file(runtime))
            throw std::runtime_error(
                "The selected Native SDK runtime is missing. Set Native SDK folder first.");
        history_ = std::move(history);
        settings_ = settings;
        job_root_ = root_ = project.project();
        runtime_ = std::filesystem::absolute(runtime);
        cancel_ = std::stop_source{};
        const auto stop = cancel_.get_token();
        job_ = std::async(std::launch::async, [runtime = runtime_, root = root_, expected, stop] {
            return detail::inspect_project_authoring(runtime, root, expected, stop);
        });
        inspecting_ = true;
        error_.clear();
        status_ = "Inspecting native metadata in a separate process...";
    }
    void poll(Scene& scene, SceneDocument& project) {
        if (!busy() || job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto result = job_.get();
            if (cancel_.stop_requested() || project.project() != job_root_)
                throw std::runtime_error("Component task cancelled or its project changed");
            project.check_ownership();
            if (!detail::json_value_equal(project.settings().document(), settings_))
                throw std::runtime_error("Project module settings changed. Inspect again.");
            if (inspecting_) {
                const auto prepared = history_->prepare(result);
                scene.publish_component_schemas(result.at("components"), [&] {
                    project.check_ownership();
                    history_->publish(prepared);
                });
                active_ = std::move(result);
                status_ = std::to_string(active_.at("components").size()) +
                          " opted-in component types are available in Add Component.";
            } else {
                // Review first; later Apply rechecks the scene revision or the
                // exact prefab draft. Worker completion never edits either.
                (void)migration_->candidate(result);
                returned_ = std::move(result);
                status_ = "Migration prepared. Review the target and rules, then apply.";
            }
            error_.clear();
        } catch (const std::exception& error) {
            report(error.what());
        }
    }
    void draw(Scene& scene, SceneDocument& project, PrefabEditor& prefab,
              const std::filesystem::path& runtime, bool locked) {
#ifdef FORGE_UI_FIXTURE
        if (fixture_open_migration) {
            open_migration(scene, project, scene.document(), false);
            fixture_open_migration = false;
        }
#endif
        ui::heading("Authored components",
                    "Project types explicitly admitted from native Flecs metadata. "
                    "Gameplay libraries never load in the editor.");
        ImGui::BeginDisabled(locked || busy() || migration_open_);
        if (ui::button("Inspect components",
                       "Inspect matching SDK modules in a disposable worker, validate "
                       "copied metadata and activate it. Existing values are not migrated."))
            attempt([&] { inspect(project, runtime); });
        ImGui::EndDisabled();
        if (busy()) {
            ImGui::SameLine();
            if (ui::button("Cancel task",
                           "Stop the worker. Existing schemas and values remain usable."))
                cancel_.request_stop();
        }
        ImGui::TextWrapped("%s", status_.c_str());
        ui::help("Unadmitted or incompatible values remain stored and read only. A changed schema "
                 "needs an explicit migration.");
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
            ui::help(
                "The failed candidate was not published. Correct the reported problem and retry.");
        }
        ImGui::BeginDisabled(locked || busy() || active_.is_null() || migration_open_);
        if (ui::button("Migrate scene values...",
                       "Review an explicit migration of the current scene's owned values "
                       "and property overrides. One scene Undo step; no prefab source write."))
            attempt([&] { open_migration(scene, project, scene.document(), false); });
        ImGui::BeginDisabled(!prefab.is_open());
        if (ui::button("Migrate open prefab draft...",
                       "Prepare changes to the open prefab source draft. Publish it "
                       "separately through its normal Save command."))
            attempt([&] { open_migration(scene, project, prefab.migration_document(), true); });
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!active_.is_null() && ImGui::TreeNode("Component details")) {
            ui::help("Admitted type identities, authored versions and owning modules from the last "
                     "successful inspection.");
            for (const auto& type : active_.at("components"))
                ImGui::TextWrapped("%s | %s | version %u | %s",
                                   type.at("display_name").get_ref<const std::string&>().c_str(),
                                   type.at("id").get_ref<const std::string&>().c_str(),
                                   type.at("schema_version").get<unsigned>(),
                                   type.at("module").get_ref<const std::string&>().c_str());
            ImGui::TreePop();
        }
        draw_migration(scene, project, prefab, locked);
    }

  private:
    static std::string fingerprint(const Json& settings) {
        std::string result;
        for (const auto& module : settings.at("modules")) {
            const auto current = module.at("fingerprint").get<std::string>();
            if (!result.empty() && current != result)
                throw std::runtime_error("Project modules declare different SDK fingerprints");
            result = current;
        }
        if (result.empty())
            throw std::runtime_error("This project declares no exact-SDK modules to inspect");
        return result;
    }
    template <class Work> void attempt(Work&& work) {
        try {
            work();
        } catch (const std::exception& error) {
            report(error.what());
        }
    }
    void report(const std::string& message) {
        error_ = message;
        ui::report_error("authored-components", message);
    }
    void open_migration(Scene& scene, SceneDocument& project, Json document, bool prefab) {
        if (!history_ || active_.is_null())
            throw std::runtime_error("Inspect the current component schemas first");
        prior_ = history_->declarations();
        choices_.clear();
        for (std::size_t i = 0; i < prior_.size(); ++i)
            for (std::size_t j = 0; j < active_.at("components").size(); ++j) {
                const auto& from = prior_[i];
                const auto& to = active_.at("components")[j];
                if (from.at("id") == to.at("id") && from.at("module") == to.at("module") &&
                    from.at("schema_version").get<unsigned>() <
                        to.at("schema_version").get<unsigned>())
                    choices_.push_back({i, j});
            }
        if (choices_.empty())
            throw std::runtime_error(
                "No earlier schema version is recorded for an admitted component. "
                "Keep forge.components.json with the project when updating gameplay code.");
        original_ = std::move(document);
        scene_revision_ = scene.revision();
        document_generation_ = project.generation();
        prefab_target_ = prefab;
        choice_ = 0;
        rules_ = "{\n  \"aliases\": [],\n  \"defaults\": []\n}";
        returned_ = Json();
        migration_.reset();
        migration_open_ = true;
        open_popup_ = true;
        error_.clear();
    }
    void prepare(SceneDocument& project) {
        if (project.generation() != document_generation_)
            throw std::runtime_error("The active document changed; reopen migration review");
        if (rules_.size() > 65536)
            throw std::runtime_error("Migration rules exceed64KiB");
        const auto rules =
            asset_detail::parse_bounded_json(std::as_bytes(std::span(rules_)), 65536, 8192, 32);
        const auto [old_index, new_index] = choices_.at(choice_);
        const auto from = prior_.at(old_index), to = active_.at("components").at(new_index);
        auto migration = std::make_unique<detail::AuthoredMigrationDocument>(original_, from, to);
        settings_ = project.settings().document();
        const auto expected = fingerprint(settings_);
        const auto values = migration->values();
        cancel_ = std::stop_source{};
        const auto stop = cancel_.get_token();
        job_root_ = project.project();
        job_ = std::async(std::launch::async, [runtime = runtime_, root = job_root_, expected, from,
                                               to, rules, values, stop] {
            return detail::migrate_project_authoring(runtime, root, expected, from, to, rules,
                                                     values, stop);
        });
        migration_ = std::move(migration);
        returned_ = Json();
        inspecting_ = false;
        status_ = "Validating migration in a separate process...";
        error_.clear();
    }
    void draw_migration(Scene& scene, SceneDocument& project, PrefabEditor& prefab, bool locked) {
        if (!migration_open_) {
            if (ImGui::BeginPopupModal("Migrate component values", nullptr,
                                       ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            return;
        }
        if (open_popup_) {
            ImGui::OpenPopup("Migrate component values");
            open_popup_ = false;
        }
        const auto available = ImGui::GetMainViewport()->WorkSize;
        const ImVec2 maximum{std::max(1.f, available.x - 30), std::max(1.f, available.y - 30)};
        ImGui::SetNextWindowSize({std::min(650 * ui::interface_scale, maximum.x),
                                  std::min(540 * ui::interface_scale, maximum.y)},
                                 ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints({1, 1}, maximum);
        if (!ImGui::BeginPopupModal("Migrate component values", nullptr,
                                    ImGuiWindowFlags_NoSavedSettings))
            return;
        ImGui::TextWrapped("Target: %s",
                           prefab_target_ ? "open prefab source draft" : "current authored scene");
        ui::help("Scene migration is one scene Undo entry. Prefab migration changes only its "
                 "draft; use Save/Publish afterward. "
                 "These are separate operations, not Apply to Prefab.");
        ImGui::BeginDisabled(busy() || locked);
        auto label = [&](std::size_t index) {
            const auto [old_index, new_index] = choices_.at(index);
            const auto& from = prior_.at(old_index);
            const auto& to = active_.at("components").at(new_index);
            return from.at("id").get<std::string>() + " : " +
                   std::to_string(from.at("schema_version").get<unsigned>()) + " -> " +
                   std::to_string(to.at("schema_version").get<unsigned>());
        };
        if (ImGui::BeginCombo("Component version", label(choice_).c_str())) {
            for (std::size_t i = 0; i < choices_.size(); ++i)
                if (ImGui::Selectable(label(i).c_str(), i == choice_)) {
                    choice_ = i;
                    returned_ = Json();
                }
            ImGui::EndCombo();
        }
        ui::help("Only explicit forward migrations to the currently admitted version are "
                 "available. Other stored versions stay untouched.");
        ImGui::TextWrapped("Unchanged fields keep their values. Removed fields remain stored. New "
                           "fields receive declared defaults. "
                           "Add an explicit alias when a field was renamed.");
        ImGui::TextUnformatted("Rules (JSON)");
        if (ImGui::InputTextMultiline("##migration_rules", &rules_,
                                      {-1, 180 * ui::interface_scale}))
            returned_ = Json();
        ui::help("Example: "
                 "{\"aliases\":[{\"path\":[\"hitpoints\"],\"name\":\"health\"}],\"defaults\":[]}. "
                 "Nested collection path: [\"items\",\"*\",\"old_name\"]. New per-entry default: "
                 "{\"path\":[\"items\",\"*\",\"added\"],\"value\":7}. Paths identify native "
                 "reflected fields; "
                 "unknown payload is never searched or overwritten.");
        if (ui::button("Prepare migration", "Validate source/target identities, aliases, bounds "
                                            "and every candidate value in a worker."))
            attempt([&] { prepare(project); });
        if (!returned_.is_null()) {
            ImGui::TextWrapped("Prepared %zu component value(s). Original documents are unchanged.",
                               returned_.size());
            if (ui::button(prefab_target_ ? "Use migrated prefab draft" : "Apply scene migration",
                           "Recheck the target and publish this reviewed candidate. Stale or "
                           "invalid results are rejected."))
                attempt([&] {
                    project.check_ownership();
                    if (project.generation() != document_generation_)
                        throw std::runtime_error("Active document changed during migration review");
                    if (!detail::json_value_equal(project.settings().document(), settings_))
                        throw std::runtime_error(
                            "Project module settings changed; inspect again before migration");
                    if (prefab_target_)
                        prefab.accept_migration(migration_->before(),
                                                migration_->candidate(returned_));
                    else
                        migration_->apply(scene, returned_, scene_revision_);
                    status_ = prefab_target_
                                  ? "Prefab draft migrated. Review it and Save/Publish separately."
                                  : "Scene values migrated. Undo restores the previous values and "
                                    "intent.";
                    migration_open_ = false;
                    ImGui::CloseCurrentPopup();
                });
        }
        ImGui::EndDisabled();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        if (ui::button(busy() ? "Cancel worker" : "Close",
                       "Cancel pending preparation or close review. No unaccepted candidate is "
                       "applied.")) {
            if (busy())
                cancel_.request_stop();
            else {
                migration_open_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    std::filesystem::path root_, job_root_, runtime_;
    std::unique_ptr<detail::AuthoredHistory> history_;
    Json active_, settings_, prior_, original_, returned_;
    std::vector<std::pair<std::size_t, std::size_t>> choices_;
    std::unique_ptr<detail::AuthoredMigrationDocument> migration_;
    std::stop_source cancel_;
    std::future<Json> job_;
    std::size_t choice_ = 0;
    std::uint64_t scene_revision_ = 0, document_generation_ = 0;
    bool inspecting_ = false, migration_open_ = false, open_popup_ = false, prefab_target_ = false;
    std::string rules_, status_ = "Inspect opted-in project components to author them.", error_;
};
} // namespace forge
