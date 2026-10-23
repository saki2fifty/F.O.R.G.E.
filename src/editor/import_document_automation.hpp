#pragma once
#include "../asset_bytes.hpp"
#include "asset_import_editor.hpp"
#include "source_workflow_automation.hpp"

namespace forge {
struct ImportDocumentAutomation {
    static std::string digest(const AssetImportEditor& editor) {
        Json decisions = Json::array();
        for (const auto& decision : editor.decisions_)
            decisions.push_back(
                {{"address", decision.address},
                 {"previous", decision.previous ? Json(*decision.previous) : Json(nullptr)}});
        const auto text = Json{
            {"generation", editor.selection_generation_},
            {"source", path_utf8(editor.selected_source())},
            {"settings", editor.draft_ ? Json(editor.draft_->request.settings) : Json(nullptr)},
            {"decisions", std::move(decisions)},
            {"review", editor.conflict_key_},
            {"job",
             editor.job_}}.dump();
        return asset_detail::content_digest(std::as_bytes(std::span(text)));
    }
    static Json status(const AssetImportEditor& editor, const SceneDocument& document) {
        if (editor.project_ != document.project() || !editor.draft_)
            return {{"open", false}};
        Json conflicts = Json::array();
        for (const auto& conflict : editor.conflicts_) {
            Json previous = Json::array();
            for (auto id : conflict.previous)
                previous.push_back({{"asset", id}, {"label", editor.identity_option_label(id)}});
            conflicts.push_back({{"code", conflict.code},
                                 {"type", conflict.type},
                                 {"observations", conflict.observations},
                                 {"previous", std::move(previous)},
                                 {"diagnostic", conflict.diagnostic}});
        }
        Json jobs = Json::array();
        if (editor.service_)
            for (const auto& job : editor.service_->jobs())
                if (job.id == editor.job_)
                    jobs.push_back({{"job", job.id},
                                    {"state", asset_job_state_name(job.state)},
                                    {"stage", job.stage},
                                    {"progress", job.progress},
                                    {"diagnostic", job.diagnostic}});
        return {{"open", editor.open_},
                {"session", editor.selection_generation_},
                {"asset", editor.selected_asset()},
                {"source", path_utf8(editor.selected_source())},
                {"state", digest(editor)},
                {"settings", editor.draft_->request.settings},
                {"effective",
                 editor.draft_->importer->settings().effective(editor.draft_->request.settings)},
                {"dirty", editor.dirty()},
                {"job", editor.job_},
                {"jobs", std::move(jobs)},
                {"conflicts", std::move(conflicts)},
                {"review", editor.conflict_key_},
                {"diagnostic", editor.error_}};
    }
    static ui::SourceDocumentAccess bind(AssetImportEditor& editor, SceneDocument& document) {
        return {
            "", [&editor, &document](std::string_view action, const Json& request) -> Json {
                document.check_ownership();
                if (action == "status")
                    return status(editor, document);
                if (action == "open") {
                    if (editor.dirty() || editor.pending() || editor.save_ || editor.close_)
                        throw AuthoringError(
                            "document_busy",
                            "Settle this importer document before opening another source");
                    editor.ensure(document);
                    editor.load(automation_relative_path(request.at("path").get<std::string>()));
                    return status(editor, document);
                }
                if (!editor.draft_ || editor.project_ != document.project())
                    throw AuthoringError("not_open",
                                         "Open the current project's source in this importer");
                if (action == "schema") {
                    Json rules = Json::array();
                    static const char* types[]{"boolean", "integer",     "number",    "text",
                                               "choice",  "string_list", "string_map"};
                    const auto& schema = editor.draft_->importer->settings();
                    for (const auto& rule : schema.rules()) {
                        Json value{{"key", rule.key},
                                   {"label", rule.label},
                                   {"help", rule.help},
                                   {"type", types[static_cast<unsigned>(rule.type)]},
                                   {"default", rule.default_value},
                                   {"choices", rule.choices},
                                   {"max_length", rule.max_length},
                                   {"max_entries", rule.max_entries}};
                        if (rule.minimum)
                            value["minimum"] = *rule.minimum;
                        if (rule.maximum)
                            value["maximum"] = *rule.maximum;
                        rules.push_back(std::move(value));
                    }
                    return {{"importer", schema.importer()},
                            {"version", schema.version()},
                            {"rules", std::move(rules)}};
                }
                if (request.at("session") != editor.selection_generation_ ||
                    request.at("state") != digest(editor))
                    throw AuthoringError(
                        "stale_import",
                        "Importer settings, source selection, job or identity review changed");
                if (action == "cancel") {
                    if (!editor.job_ || request.at("job") != editor.job_)
                        throw AuthoringError("wrong_job", "Cancel the exact active importer job");
                    editor.service_->cancel(editor.job_);
                    return status(editor, document);
                }
                if (editor.pending() || editor.close_ || editor.save_)
                    throw AuthoringError("document_busy",
                                         "Wait for this importer job or close request");
                if (action == "settings") {
                    auto candidate = editor.draft_->request.settings;
                    for (const auto& change : request.at("changes")) {
                        if (change.value("reset", false) && change.contains("value"))
                            throw AuthoringError("invalid_change",
                                                 "Reset and value are mutually exclusive");
                        if (!change.value("reset", false) && !change.contains("value"))
                            throw AuthoringError("invalid_change",
                                                 "Provide a setting value or reset=true");
                        candidate = editor.draft_->importer->settings().edit(
                            candidate, change.at("key").get<std::string>(),
                            change.value("reset", false) ? std::optional<Json>{}
                                                         : std::optional<Json>{change.at("value")});
                    }
                    editor.draft_->request.settings = std::move(candidate);
                } else if (action == "identity") {
                    std::optional<AssetId> previous;
                    if (request.contains("previous"))
                        previous = request.at("previous").get<AssetId>();
                    editor.decide_identity(request.at("address").get<std::string>(), previous);
                } else if (action == "publish")
                    editor.submit_draft();
                else if (action == "close") {
                    if (editor.dirty() && !request.value("discard", false))
                        throw AuthoringError(
                            "unsaved_document",
                            "Publish these settings or explicitly discard this importer draft");
                    editor.finish_close();
                } else
                    throw AuthoringError("unknown_operation", "Unknown importer operation");
                return status(editor, document);
            }};
    }
};

inline std::vector<LiveAuthoringMethod>
import_document_automation_methods(ui::DocumentWorkspace& workspace,
                                   const ui::AutomationPermissions& permissions) {
    Json ids = Json::array();
    for (const auto& entry : workspace.entries())
        if (entry.importer)
            ids.push_back(entry.id);
    const Json editor_schema{{"type", "string"}, {"enum", ids}};
    auto invoke = [&workspace](std::string_view action, const Json& request) {
        const auto* entry = workspace.find(request.at("editor").get<std::string>());
        if (!entry || !entry->importer)
            throw AuthoringError("unknown_editor", "Choose an advertised import settings editor");
        return entry->importer->invoke(action, request);
    };
    std::vector<LiveAuthoringMethod> methods;
    methods.push_back(automation_method(
        "asset.settings.types",
        "List actual import settings editors and their current source/job state.", "asset.read",
        automation_object(), false, [&workspace](const Json&) {
            Json result = Json::array();
            for (const auto& entry : workspace.entries())
                if (entry.importer)
                    result.push_back(
                        {{"editor", entry.id},
                         {"title", entry.title},
                         {"status", entry.importer->invoke("status", Json::object())}});
            return Json{{"editors", std::move(result)}};
        }));
    for (const auto* action : {"status", "schema"})
        methods.push_back(automation_method(
            std::string("asset.settings.") + action,
            "Read the shared importer draft, typed settings rules, reviewed identity conflicts and "
            "job diagnostics.",
            "asset.read", automation_object({{"editor", editor_schema}}, {"editor"}), false,
            [invoke, action](const Json& request) { return invoke(action, request); }));
    if (!permissions.assets)
        return methods;
    methods.push_back(automation_method(
        "asset.settings.open",
        "Open a project-relative raw/authored source in its actual import settings owner, "
        "including first imports.",
        "asset.write",
        automation_object({{"editor", editor_schema}, {"path", automation_text(1023, 1)}},
                          {"editor", "path"}),
        true, [invoke](const Json& request) { return invoke("open", request); }));
    for (const auto* action : {"settings", "identity", "publish", "cancel", "close"}) {
        Json properties{{"editor", editor_schema},
                        {"session", automation_integer(1)},
                        {"state", automation_digest()}};
        Json required{"editor", "session", "state"};
        if (std::string_view(action) == "settings") {
            properties["changes"] =
                automation_array(automation_object({{"key", automation_text(128, 1)},
                                                    {"value", Json::object()},
                                                    {"reset", {{"type", "boolean"}}}},
                                                   {"key"}),
                                 128, 1);
            required.push_back("changes");
        } else if (std::string_view(action) == "identity") {
            properties["address"] = automation_text(1024, 1);
            properties["previous"] = automation_uuid();
            required.push_back("address");
        } else if (std::string_view(action) == "cancel") {
            properties["job"] = automation_integer(1);
            required.push_back("job");
        } else if (std::string_view(action) == "close")
            properties["discard"] = {{"type", "boolean"}};
        methods.push_back(automation_method(
            std::string("asset.settings.") +
                (std::string_view(action) == "settings" ? "edit" : action),
            std::string(action) +
                " through this importer owner at the exact observed session/state. Identity "
                "without previous explicitly selects a new logical asset. Publish revalidates the "
                "reviewed source/build key; poll status for completion.",
            "asset.write", automation_object(std::move(properties), std::move(required)), true,
            [invoke, action](const Json& request) { return invoke(action, request); },
            std::string_view(action) == "cancel"));
    }
    return methods;
}
} // namespace forge
