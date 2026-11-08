#pragma once
#include "../asset_bytes.hpp"
#include "../prefab_member_choices.hpp"
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
        Json bases = Json::array();
        for (const auto& [asset, source] : editor.baseline_bases_)
            bases.push_back(
                {{"asset", asset}, {"revision", source.at("revision")}, {"sha256", hash(source)}});
        return {{"open", true},
                {"session", editor.source_session_},
                {"dirty", editor.dirty()},
                {"state", hash(Json{{"session", editor.source_session_},
                                    {"draft", editor.draft_},
                                    {"bases", bases}})},
                {"source", editor.draft_},
                {"reviewed_bases", std::move(bases)},
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
        result.push_back(automation_method(
            "prefab.source.read",
            "Read the shared prefab draft/session/digest and reviewed base summaries. "
            "Select authored or effective source, an optional JSON pointer and array page. "
            "Narrow requests to stay within 8 MiB; publication is separate from Scene Undo.",
            "asset.read",
            automation_object({{"view", {{"type", "string"}, {"enum", {"authored", "effective"}}}},
                               {"pointer", automation_text(2048)},
                               {"offset", automation_integer()},
                               {"limit", automation_integer(1, 128)}}),
            false, [&editor, &scene, &project](const Json& request) {
                auto result = state(editor, project);
                if (!result.at("open").get<bool>())
                    return result;
                const auto view = request.value("view", std::string("authored"));
                auto source = view == "effective" ? editor.source_view(scene.schema())
                                                  : std::move(result.at("source"));
                const auto pointer = request.value("pointer", std::string{});
                if (!pointer.empty())
                    source = source.at(Json::json_pointer(pointer));
                if (request.contains("offset") || request.contains("limit")) {
                    if (!source.is_array())
                        throw AuthoringError("invalid_argument",
                                             "Array paging requires an array JSON pointer");
                    const auto total = source.size();
                    const auto offset = request.value("offset", std::size_t{});
                    const auto count = request.value("limit", std::size_t{128});
                    auto rows = Json::array();
                    for (auto i = std::min(offset, total); i < total && rows.size() < count; ++i)
                        rows.push_back(std::move(source[i]));
                    source = std::move(rows);
                    result["total"] = total;
                    result["offset"] = offset;
                }
                if (source.dump().size() > 8 * 1024 * 1024)
                    throw AuthoringError(
                        "response_too_large",
                        "Read a narrower JSON pointer or array page (8 MiB maximum)");
                result["source"] = std::move(source);
                result["view"] = view;
                result["pointer"] = pointer;
                return result;
            }));
        result.push_back(automation_method(
            "prefab.source.targets",
            "Page available source member reference choices, including scoped nested occurrences, "
            "from the same admitted definitions as the native picker. References are portable "
            "source addresses; native values remain EntityRef. No draft or Scene edits.",
            "asset.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [&editor, &scene, &project](const Json& request) {
                const auto current = state(editor, project);
                if (!current.at("open").get<bool>())
                    return Json{{"open", false}};
                auto definitions = editor.baseline_bases_;
                const auto asset = editor.draft_.at("asset_id").get<AssetId>();
                definitions[asset] = editor.draft_;
                definitions = resolve_prefab_sources(definitions, scene.schema());
                const auto choices =
                    detail::prefab_member_choices(definitions.at(asset), definitions);
                const auto offset = request.value("offset", std::size_t{});
                const auto limit = request.value("limit", std::size_t(128));
                auto rows = Json::array();
                for (auto i = std::min(offset, choices.size());
                     i < choices.size() && rows.size() < limit; ++i)
                    rows.push_back(
                        {{"reference", choices[i].reference}, {"label", choices[i].label}});
                return Json{{"open", true},
                            {"state", current.at("state")},
                            {"total", choices.size()},
                            {"offset", offset},
                            {"targets", std::move(rows)}};
            }));
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
            "prefab.variant.create",
            "Create a single-base Prefab variant at a new project path. "
            "Inherits current base values and retains explicit value/structure intent.",
            "asset.write",
            automation_object({{"base", automation_uuid()}, {"path", automation_text(1024, 1)}},
                              {"base", "path"}),
            true, [&scene, &project](const Json& request) {
                project.check_ownership();
                return Json{{"asset",
                             project.prefabs().create_variant(
                                 scene, request.at("base").get<AssetId>(),
                                 automation_relative_path(request.at("path").get<std::string>()))}};
            }));
        result.push_back(automation_method(
            "prefab.duplicate",
            "Duplicate a Prefab at a new project path. Variant copies retain "
            "their base and inherited member IDs while receiving a fresh AssetId.",
            "asset.write",
            automation_object({{"asset", automation_uuid()}, {"path", automation_text(1024, 1)}},
                              {"asset", "path"}),
            true, [&scene, &project](const Json& request) {
                project.check_ownership();
                return Json{{"asset",
                             project.prefabs().duplicate(
                                 scene, request.at("asset").get<AssetId>(),
                                 automation_relative_path(request.at("path").get<std::string>()))}};
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
            [&editor, &scene, &project](const Json& request) {
                project.check_ownership();
                if (editor.dirty() || editor.close_requested_ || editor.save_requested_)
                    throw AuthoringError("document_busy", "Settle the current prefab draft");
                editor.edit_source(scene, project, request.at("asset").get<AssetId>());
                return state(editor, project);
            }));
        result.push_back(automation_method(
            "prefab.source.revert",
            "Remove explicit variant name, component or property intent. "
            "Inherited values remain visible; publish saves the draft.",
            "asset.write",
            automation_object({{"state", automation_digest()},
                               {"member", automation_uuid()},
                               {"component", automation_text(256, 1)},
                               {"field", automation_text(256, 1)}},
                              {"state", "member"}),
            true, [&editor, &scene, &project](const Json& request) {
                check(editor, project, request);
                if (!editor.variant())
                    throw AuthoringError("not_variant",
                                         "Only variants have inherited source intent");
                const auto view = editor.source_view(scene.schema());
                const auto member = request.at("member").get<std::string>();
                if (std::none_of(view.at("members").begin(), view.at("members").end(),
                                 [&](const auto& row) { return row.at("id") == member; }))
                    throw AuthoringError("missing_member", "Select a current variant member");
                if (!editor.inherited_member(member))
                    throw AuthoringError("not_inherited", "Owned additions do not inherit values");
                if (request.contains("field") && !request.contains("component"))
                    throw AuthoringError("invalid_argument", "A field requires its component");
                editor.member_ = member;
                if (!request.contains("component"))
                    editor.revert_name();
                else if (request.contains("field"))
                    editor.revert_property(request.at("component"), request.at("field"));
                else
                    editor.revert_component(request.at("component"));
                return state(editor, project);
            }));
        result.push_back(automation_method(
            "prefab.source.suppress",
            "Suppress an admitted optional component on a current variant member. Replaces its "
            "whole/property overrides and explicitly enables source5, rejecting opaque-field "
            "collisions. The draft retains absence across base changes. source.revert with a "
            "component restores inheritance; source.publish saves the draft.",
            "asset.write",
            automation_object({{"state", automation_digest()},
                               {"member", automation_uuid()},
                               {"component", automation_text(256, 1)}},
                              {"state", "member", "component"}),
            true, [&editor, &scene, &project](const Json& request) {
                check(editor, project, request);
                if (!editor.variant())
                    throw AuthoringError("not_variant", "Only variants support suppression");
                const auto selected = editor.member_;
                editor.member_ = request.at("member").get<std::string>();
                try {
                    editor.suppress_component(request.at("component"), scene.schema());
                } catch (...) {
                    editor.member_ = selected;
                    throw;
                }
                return state(editor, project);
            }));
        for (const auto* action :
             {"add", "remove", "restore", "parent", "spatial", "order", "revert_structure"}) {
            const std::string_view operation = action;
            Json properties{{"state", automation_digest()}};
            Json required{"state"};
            if (operation == "add" || operation == "parent" || operation == "order") {
                properties["parent"] = automation_uuid();
                required.push_back("parent");
            }
            if (operation != "add" && operation != "order") {
                properties["member"] = automation_uuid();
                required.push_back("member");
            }
            if (operation == "add")
                properties["name"] = automation_text(1024, 1);
            if (operation == "spatial") {
                properties["binding"] = automation_object(
                    {{"mode",
                      {{"type", "string"}, {"enum", {"follow_structure", "world", "explicit"}}}},
                     {"member", automation_uuid()}},
                    {"mode"});
                required.push_back("binding");
            }
            if (operation == "order") {
                properties["members"] = automation_array(automation_uuid(), 4096);
                required.push_back("members");
            }
            if (operation == "revert_structure") {
                properties["field"] = {{"type", "string"}, {"enum", {"parent", "spatial"}}};
                required.push_back("field");
            }
            const std::string description =
                operation == "add"
                    ? "Add an owned child with a fresh identity and registered defaults. "
                : operation == "remove" ? "Remove a source branch; variants retain removal intent "
                                          "and owned definitions. "
                : operation == "restore"
                    ? "Clear this variant's member removal; upstream absence still applies. "
                : operation == "parent"  ? "Set a source parent, retaining local channels; missing "
                                           "parents retain dormant intent. "
                : operation == "spatial" ? "Set a source spatial binding, retaining local "
                                           "channels; explicit needs a member. "
                : operation == "order" ? "Set a parent's ordered member-ID prefix; an empty list "
                                         "resets variant order intent. "
                                       : "Revert an inherited member's parent or spatial override "
                                         "to the current base. ";
            result.push_back(automation_method(
                std::string("prefab.source.member.") + action,
                description +
                    "Uses the shared source owner at the exact draft digest. Structural "
                    "variant edits explicitly enable source6 and reject opaque collisions "
                    "and cycles. Unavailable intent remains repairable. Publish saves "
                    "separately; Scene Undo does not edit this draft.",
                "asset.write", automation_object(std::move(properties), std::move(required)), true,
                [&editor, &scene, &project, action](const Json& request) {
                    check(editor, project, request);
                    const std::string_view operation = action;
                    const auto schema = scene.schema();
                    PrefabMemberId added;
                    if (operation == "add")
                        added = editor.add_child(request.at("parent").get<PrefabMemberId>(), schema,
                                                 request.value("name", std::string("Child")));
                    else if (operation == "order")
                        editor.set_member_order(
                            request.at("parent").get<PrefabMemberId>(),
                            request.at("members").get<std::vector<PrefabMemberId>>(), schema);
                    else {
                        const auto member = request.at("member").get<PrefabMemberId>();
                        if (operation == "remove" || operation == "restore")
                            editor.set_member_removed(member, operation == "remove", schema);
                        else if (operation == "parent")
                            editor.set_member_parent(
                                member, request.at("parent").get<PrefabMemberId>(), schema);
                        else if (operation == "spatial")
                            editor.set_member_spatial(member, request.at("binding"), schema);
                        else
                            editor.revert_member_structure(member, request.at("field"), schema);
                    }
                    auto result = state(editor, project);
                    if (added)
                        result["member"] = added;
                    return result;
                }));
        }
        for (const auto* action : {"set", "clear", "revert"}) {
            Json properties{{"state", automation_digest()}, {"member", automation_uuid()}};
            Json required{"state", "member"};
            if (std::string_view(action) == "set") {
                properties["asset"] = automation_uuid();
                required.push_back("asset");
            }
            result.push_back(automation_method(
                std::string("prefab.source.nested.") + action,
                "Edit a member's nested Prefab link through the shared source owner at its exact "
                "draft digest. Set selects an available Prefab asset; clear saves an explicit "
                "empty link; revert restores an inherited link. Set/clear explicitly enable "
                "source7. Checks reviewed supporting sources, cycles and expanded bounds. "
                "Publish saves separately; Scene Undo does not edit this draft.",
                "asset.write", automation_object(std::move(properties), std::move(required)), true,
                [&editor, &scene, &project, action](const Json& request) {
                    check(editor, project, request);
                    const auto member = request.at("member").get<PrefabMemberId>();
                    if (std::string_view(action) == "revert")
                        editor.revert_nested_prefab(scene, project, member);
                    else
                        editor.set_nested_prefab(scene, project, member,
                                                 std::string_view(action) == "set"
                                                     ? request.at("asset").get<AssetId>()
                                                     : AssetId{});
                    return state(editor, project);
                }));
        }
        for (const auto* action : {"patch", "publish", "rebase", "close"}) {
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
                            AssetDocumentAutomation::patch(editor.draft_, request.at("patch")),
                            scene, project);
                    else if (std::string_view(action) == "publish") {
                        if (!editor.publish(scene, project))
                            throw AuthoringError("publication_failed", editor.error_);
                    } else if (std::string_view(action) == "rebase")
                        editor.rebase_draft(scene, project);
                    else {
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
