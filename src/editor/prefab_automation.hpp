#pragma once
#include "../asset_bytes.hpp"
#include "prefabs.hpp"
#include "source_document_automation.hpp"

namespace forge {
struct PrefabAutomation {
    static std::string hash(const Json& value) {
        const auto text = value.dump();
        return asset_detail::content_digest(std::as_bytes(std::span(text)));
    }
    static Json state(PrefabEditor& editor, SceneDocument& project) {
        project.check_ownership();
        if (!editor.open_ || editor.project_ != project.project())
            return {{"open", false}};
        return {
            {"open", true},
            {"session", editor.source_session_},
            {"dirty", editor.dirty()},
            {"state", hash(Json{{"session", editor.source_session_}, {"draft", editor.draft_}})},
            {"source", editor.draft_},
            {"diagnostic", editor.error_}};
    }
    static void check(PrefabEditor& editor, SceneDocument& project, const Json& request) {
        const auto current = state(editor, project);
        if (!current.at("open").get<bool>())
            throw AuthoringError("not_open", "Open this project's prefab source");
        if (request.at("state") != current.at("state"))
            throw AuthoringError("stale_document", "Prefab source draft/session changed");
        if (editor.close_requested_ || editor.save_requested_)
            throw AuthoringError("document_busy", "Settle the prefab save/close request");
    }
    static std::vector<LiveAuthoringMethod> methods(PrefabEditor& editor, Scene& scene,
                                                    SceneDocument& project,
                                                    const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "prefab.list", "Page the owning project's published prefab library.", "asset.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [&project](const Json& request) {
                project.check_ownership();
                Json records = Json::array();
                std::size_t i = 0;
                const auto offset = request.value("offset", std::size_t{});
                const auto limit = request.value("limit", std::size_t(128));
                for (const auto& [id, record] : project.prefabs().records()) {
                    if (i++ < offset)
                        continue;
                    if (records.size() == limit)
                        break;
                    records.push_back({{"asset", id}, {"source", path_utf8(record.source)}});
                }
                return Json{{"prefabs", std::move(records)},
                            {"total", project.prefabs().records().size()}};
            }));
        result.push_back(
            automation_method("prefab.source.read",
                              "Read the actual prefab editor draft, session and content digest. "
                              "Prefab source publication is separate from Scene Undo.",
                              "asset.read", automation_object(), false,
                              [&editor, &project](const Json&) { return state(editor, project); }));
        if (!permissions.assets)
            return result;
        result.push_back(automation_method(
            "prefab.create",
            "Capture a scene subtree as a new prefab source through the existing library. Original "
            "scene objects remain; instantiate explicitly.",
            "asset.write",
            automation_object({{"entity", automation_uuid()}, {"path", automation_text(1024, 1)}},
                              {"entity", "path"}),
            true, [&scene, &project](const Json& request) {
                project.check_ownership();
                auto id = project.prefabs().create(
                    scene, create_prefab_source(scene, request.at("entity").get<std::string>()),
                    automation_relative_path(request.at("path").get<std::string>()));
                return Json{{"asset", id}};
            }));
        result.push_back(automation_method(
            "prefab.refresh",
            "Validate external prefab changes and reconcile instances through the existing "
            "library. Successful source changes may clear Scene history.",
            "asset.write", automation_object(), true, [&scene, &project, &editor](const Json&) {
                project.check_ownership();
                if (editor.dirty())
                    throw AuthoringError("unsaved_document",
                                         "Settle the open prefab draft before refreshing sources");
                project.prefabs().refresh(scene);
                return Json{{"refreshed", true}};
            }));
        result.push_back(automation_method(
            "prefab.source.open",
            "Open a published prefab in the actual prefab editor; settle its prior draft first.",
            "asset.write", automation_object({{"asset", automation_uuid()}}, {"asset"}), true,
            [&editor, &project](const Json& request) {
                project.check_ownership();
                if (editor.dirty() || editor.close_requested_ || editor.save_requested_)
                    throw AuthoringError("document_busy", "Settle the current prefab draft");
                editor.edit_source(project, request.at("asset").get<AssetId>());
                return state(editor, project);
            }));
        for (const auto* action : {"patch", "publish", "close"}) {
            Json properties{{"state", automation_digest()}};
            Json required{"state"};
            if (std::string_view(action) == "patch") {
                properties["patch"] = automation_array(
                    automation_object(
                        {{"op",
                          {{"type", "string"}, {"enum", {"add", "remove", "replace", "test"}}}},
                         {"path", automation_text(2048)},
                         {"value", Json::object()}},
                        {"op", "path"}),
                    128, 1);
                required.push_back("patch");
            } else if (std::string_view(action) == "close")
                properties["discard"] = {{"type", "boolean"}};
            result.push_back(automation_method(
                std::string("prefab.source.") + action,
                std::string(action) + " the shared prefab draft at its exact content digest. "
                                      "Identity/publication revision stay with the owner; publish "
                                      "reconciles instances and checks the saved source baseline.",
                "asset.write", automation_object(std::move(properties), std::move(required)), true,
                [&editor, &scene, &project, action](const Json& request) {
                    check(editor, project, request);
                    if (std::string_view(action) == "patch")
                        editor.edit_draft(
                            AssetDocumentAutomation::patch(editor.draft_, request.at("patch")));
                    else if (std::string_view(action) == "publish") {
                        if (!editor.publish(scene, project))
                            throw AuthoringError("publication_failed", editor.error_);
                    } else {
                        if (editor.dirty() && !request.value("discard", false))
                            throw AuthoringError("unsaved_document",
                                                 "Publish or explicitly discard the prefab draft");
                        editor.resolve_close(ui::DraftResolution::Discard, scene, project);
                    }
                    return state(editor, project);
                }));
        }
        result.push_back(automation_method(
            "prefab.apply.review",
            "Review an instance's admitted overrides for the existing recoverable prefab+scene "
            "save transaction. Requires a saved scene and no prefab draft.",
            "asset.write", automation_object({{"entity", automation_uuid()}}, {"entity"}), true,
            [&editor, &scene, &project](const Json& request) {
                project.check_ownership();
                if (editor.dirty() || !project.on_disk())
                    throw AuthoringError("document_busy",
                                         "Save the scene and settle prefab drafts first");
                editor.apply_candidate_ =
                    project.prefabs().prepare_apply(scene, request.at("entity").get<std::string>());
                editor.apply_revision_ = scene.revision();
                editor.apply_generation_ = project.generation();
                const auto& candidate = editor.apply_candidate_;
                return Json{{"review", hash(Json{{"expected", candidate.expected},
                                                 {"source", candidate.source},
                                                 {"scene", candidate.scene}})},
                            {"asset", candidate.expected.at("asset_id")},
                            {"overrides", candidate.overrides},
                            {"saves_scene", true},
                            {"saves_prefab", true}};
            }));
        result.push_back(automation_method(
            "prefab.apply.commit",
            "Commit the exact reviewed prefab+scene pair through SceneDocument. Saves all current "
            "scene changes; the existing Scene Undo/Redo owns replay of both files.",
            "asset.write",
            automation_object({{"review", automation_digest()},
                               {"acknowledge_scene_save", {{"type", "boolean"}, {"const", true}}}},
                              {"review", "acknowledge_scene_save"}),
            true, [&editor, &scene, &project](const Json& request) {
                project.check_ownership();
                const auto& candidate = editor.apply_candidate_;
                if (editor.dirty() || editor.apply_revision_ != scene.revision() ||
                    editor.apply_generation_ != project.generation() ||
                    candidate.expected.is_null() ||
                    request.at("review") != hash(Json{{"expected", candidate.expected},
                                                      {"source", candidate.source},
                                                      {"scene", candidate.scene}}))
                    throw AuthoringError("stale_review",
                                         "Review the current instance and source again");
                project.apply_prefab(candidate, editor.apply_revision_);
                editor.apply_candidate_ = {};
                return Json{{"committed", true}};
            }));
        return result;
    }
};
} // namespace forge
