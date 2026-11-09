#pragma once
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "ui_source_editor.hpp"

namespace forge {
// Borrows the native widget's document/history; no agent-owned text or DOM state.
struct UiSourceAutomation {
    static Json document_state(const ui::UiSourceEditor& editor, const SceneDocument& project) {
        project.check_ownership();
        if (editor.project() != project.project())
            return {{"open", false}};
        return editor.state();
    }
    static Json state(const ui::UiSourceEditor& editor, const SceneDocument& project) {
        auto value = document_state(editor, project);
        if (!value.at("open").get<bool>())
            return value;
        auto nodes = Json::array();
        const auto range = [](UiSourceRange span) {
            return Json{{"begin", span.begin}, {"end", span.end}};
        };
        const auto& outline = editor.outline();
        for (std::size_t i = 0; i < outline.size(); ++i) {
            const auto& node = outline[i];
            auto attributes = Json::array();
            for (const auto& attr : node.attributes)
                attributes.push_back({{"name", attr.name},
                                      {"source", range(attr.source)},
                                      {"value", range(attr.value)},
                                      {"has_value", attr.has_value}});
            nodes.push_back(
                {{"index", i},
                 {"tag", node.tag},
                 {"parent",
                  node.parent == UiMarkupElement::no_parent ? Json(nullptr) : Json(node.parent)},
                 {"source", range(node.source)},
                 {"content", range(node.content)},
                 {"self_closing", node.self_closing},
                 {"widget_editable", ui_markup_widget_editable(node)},
                 {"attributes", std::move(attributes)}});
        }
        value["elements"] = std::move(nodes);
        return value;
    }
    static void observed(const ui::UiSourceEditor& editor, const SceneDocument& project,
                         const Json& request) {
        const auto current = document_state(editor, project);
        if (!current.at("open").get<bool>())
            throw AuthoringError("source_closed", "Open a UI source in this project first");
        for (const char* key : {"path", "session", "revision", "state"})
            if (request.at(key) != current.at(key))
                throw AuthoringError("stale_ui_source",
                                     "UI source changed; read its current state");
    }
    static Json image_source(ui::UiSourceEditor& editor, SceneDocument& project,
                             const Json& request) {
        observed(editor, project, request);
        auto source = editor.image_source(project);
        for (const auto* key : {"preview_instance", "preview_revision"})
            if (request.at(key) != source.at(key))
                throw AuthoringError("stale_ui_preview",
                                     "Preview the current UI source before image capture");
        return source;
    }
    static std::vector<LiveAuthoringMethod> methods(EditorFiles& files, ui::UiSourceEditor& editor,
                                                    const ui::AutomationPermissions& permissions,
                                                    std::function<std::string()> unavailable) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "ui.source.read",
            "Read the native UI source draft/history and its source-derived element byte ranges. "
            "Indices are transient within this revision; no persisted DOM identity is created. "
            "Incomplete RML retains its source and reports outline_error. RCSS has no element "
            "outline.",
            "assets.read", automation_object(), false,
            [&files, &editor](const Json&) { return state(editor, files.document); }));
        const auto ready = [&files, &editor, unavailable] {
            files.document.check_ownership();
            const auto reason = unavailable ? unavailable() : std::string{};
            if (!reason.empty())
                throw AuthoringError("source_busy", reason);
            if (editor.close_pending() || editor.gesture_active())
                throw AuthoringError("source_busy",
                                     "Finish the native UI source dialog or canvas gesture first");
        };
        const Json guard{{"path", automation_text(2047, 1)},
                         {"session", automation_uuid()},
                         {"revision", automation_integer()},
                         {"state", automation_digest()}};
        const Json required{"path", "session", "revision", "state"};
        auto preview_properties = guard;
        preview_properties["width"] = automation_integer(1, 2048);
        preview_properties["height"] = automation_integer(1, 2048);
        preview_properties["density"] = {{"type", "number"}, {"minimum", .5}, {"maximum", 4}};
        auto preview_required = required;
        preview_required.push_back("width");
        preview_required.push_back("height");
        result.push_back(automation_method(
            "ui.source.preview",
            "Preview the observed registered RML draft while Play is stopped, without saving, "
            "publishing metadata or executing gameplay bindings. Reloads saved linked "
            "dependencies. "
            "Returns native border boxes, source indices, editable flags/reasons and a transient "
            "preview instance/revision for atomic layout edits. At most512 unique-ID observations, "
            "128-byte IDs and2048x2048 physical pixels; density0.5..4. A failed candidate retains "
            "last-good pixels and marks geometry stale. Metadata-only hosts need no GPU image.",
            "assets.read",
            automation_object(std::move(preview_properties), std::move(preview_required)), false,
            [&files, &editor, ready](const Json& request) {
                ready();
                observed(editor, files.document, request);
                return editor.preview_state(files.document, request.at("width").get<int>(),
                                            request.at("height").get<int>(),
                                            request.value("density", 1.f));
            }));
        if (!permissions.assets)
            return result;
        auto layout_properties = guard;
        layout_properties["element"] = automation_integer(0, 8191);
        layout_properties["preview_instance"] = automation_uuid();
        layout_properties["preview_revision"] = automation_integer();
        for (const auto* name : {"left", "top", "width", "height"})
            layout_properties[name] = {
                {"type", "number"},
                {"minimum",
                 std::string_view(name) == "left" || std::string_view(name) == "top" ? -65536 : 1},
                {"maximum", 65536}};
        auto layout_required = required;
        for (const auto* name :
             {"element", "preview_instance", "preview_revision", "left", "top", "width", "height"})
            layout_required.push_back(name);
        result.push_back(automation_method(
            "ui.source.layout",
            "Atomically set the observed source element's existing inline left/top/width/height "
            "pixel values as one native Undo record. Requires the current source guards and "
            "preview instance/revision from ui.source.preview. Only uniquely identified, visible "
            "absolute layouts with plain quoted inline pixel values are editable. Transforms, "
            "animation, scrolling, constraints, data-bound ancestors and ambiguous cascade reject "
            "with reasons. Preserves unrelated source; no save, registration or Scene history "
            "write.",
            "assets.write",
            automation_object(std::move(layout_properties), std::move(layout_required)), true,
            [&files, &editor, ready](const Json& request) {
                ready();
                observed(editor, files.document, request);
                editor.layout(request.at("element").get<std::size_t>(),
                              {request.at("left").get<float>(), request.at("top").get<float>(),
                               request.at("width").get<float>(), request.at("height").get<float>()},
                              request.at("preview_instance").get<std::string>(),
                              request.at("preview_revision").get<std::uint64_t>());
                return state(editor, files.document);
            }));
        auto group_properties = guard;
        auto group_actions = Json::array();
        for (const auto& [key, label] : ui_layout_group_actions)
            group_actions.push_back(key);
        group_properties["elements"] =
            automation_array(automation_integer(0, 8191), ui_source_selection_limit, 2);
        group_properties["action"] = {{"type", "string"}, {"enum", std::move(group_actions)}};
        group_properties["preview_instance"] = automation_uuid();
        group_properties["preview_revision"] = automation_integer();
        for (const auto* name : {"dx", "dy"})
            group_properties[name] = {
                {"type", "number"}, {"minimum", -131072}, {"maximum", 131072}};
        auto group_required = required;
        for (const auto* name : {"elements", "action", "preview_instance", "preview_revision"})
            group_required.push_back(name);
        result.push_back(automation_method(
            "ui.source.arrange",
            "Move, align border edges/centers or distribute equal gaps for2..32 distinct "
            "same-parent native-admitted source elements. Distribution needs3elements. Native "
            "bounds choose geometry; source sizes and unrelated text remain unchanged. dx/dy "
            "are optional pixel offsets for move only. Rechecks exact source/native preview "
            "guards and the complete candidate before one source Undo record. Unsupported, "
            "duplicate, out-of-range or stale members reject the whole operation. No Save, "
            "resource registration, Scene history or relative/flow/group-resize support.",
            "assets.write",
            automation_object(std::move(group_properties), std::move(group_required)), true,
            [&files, &editor, ready](const Json& request) {
                ready();
                observed(editor, files.document, request);
                editor.arrange(request.at("elements").get<std::vector<std::size_t>>(),
                               request.at("action").get<std::string>(), request.value("dx", 0.f),
                               request.value("dy", 0.f),
                               request.at("preview_instance").get<std::string>(),
                               request.at("preview_revision").get<std::uint64_t>());
                return state(editor, files.document);
            }));
        result.push_back(automation_method(
            "ui.source.open",
            "Open a contained RML/RCSS source in the actual UI Source document. Reuses its draft. "
            "A different unsaved document rejects; Save or explicitly close/discard it first. "
            "No resource registration, publication or game execution is implicit.",
            "assets.write", automation_object({{"path", automation_text(2047, 1)}}, {"path"}), true,
            [&files, &editor, ready](const Json& request) {
                ready();
                const auto path = automation_relative_path(request.at("path").get<std::string>());
                if (editor.dirty() && !editor.owns(files.document.project(), path))
                    throw AuthoringError("unsaved_source", "Save or close the open UI draft first");
                editor.open(files.document, path_utf8(path));
                return state(editor, files.document);
            }));
        for (const auto* operation :
             {"replace", "attribute", "text", "insert", "duplicate", "remove", "remove_group",
              "move", "save", "undo", "redo", "reload", "close"}) {
            const std::string action(operation);
            auto properties = guard;
            auto fields = required;
            const auto add = [&](const char* name, Json schema, bool needed = true) {
                properties[name] = std::move(schema);
                if (needed)
                    fields.push_back(name);
            };
            if (action == "replace")
                add("text", automation_text(256 * 1024 - 1));
            if (action == "attribute" || action == "text" || action == "duplicate" ||
                action == "remove" || action == "move")
                add("element", automation_integer(0, 8191));
            if (action == "duplicate")
                add("prefix", automation_text(64), false);
            if (action == "remove_group")
                add("elements",
                    automation_array(automation_integer(0, 8191), ui_source_selection_limit, 1));
            if (action == "attribute") {
                add("name", automation_text(64, 1));
                add("value", {{"oneOf", {automation_text(256 * 1024), Json{{"type", "null"}}}}});
            }
            if (action == "text" || action == "insert")
                add("text", automation_text(256 * 1024), action == "text");
            if (action == "insert") {
                add("parent", automation_integer(0, 8191));
                add("tag", automation_text(64, 1));
                add("before", automation_integer(0, 8191), false);
            }
            if (action == "move")
                add("direction", {{"type", "integer"}, {"enum", {-1, 1}}});
            if (action == "close" || action == "reload")
                add("discard", {{"type", "boolean"}}, false);
            result.push_back(automation_method(
                "ui.source." + action,
                action == "duplicate"
                    ? "Copy one static source subtree as its next sibling in one Undo step. "
                      "Existing bytes/IDs stay unchanged; copied IDs use prefix-oldID, and an "
                      "anonymous root gains prefix. Empty/omitted prefix tries copy,copy2..copy64. "
                      "Explicit prefixes are lowercase UI names up to64bytes; new IDs fit128bytes. "
                      "Internal literal label targets remap; external references/CSS selectors "
                      "stay unchanged. Bound/event-driven/expression/protected/ambiguous source "
                      "rejects. Requires current source guards/assets.write; no Save/registration."
                : action == "remove_group"
                    ? "Remove1..32 distinct same-parent source subtrees as one Undo step. "
                      "Protected, nested, duplicate, invalid or stale selections reject "
                      "without partial changes. Requires current source guards/assets.write; "
                      "no Save, registration or Scene history."
                    : action +
                          " the observed native UI source at its path/session/revision/content "
                          "digest, sharing one widget Undo history. Text and attribute values "
                          "escape "
                          "markup characters; RML bindings retain their source meaning. A null "
                          "attribute value removes it. Structured edits preserve "
                          "unrelated source and validate the full candidate. Raw replace may "
                          "retain "
                          "incomplete markup; Save validates and checks the disk baseline. Reload/"
                          "Close require explicit discard for an unsaved draft. Resource "
                          "registration "
                          "and Reload UI remain separate; no Scene history or OS clipboard write.",
                "assets.write", automation_object(std::move(properties), std::move(fields)), true,
                [&files, &editor, ready, action](const Json& request) {
                    ready();
                    observed(editor, files.document, request);
                    if (action == "replace")
                        editor.replace_text(request.at("text").get<std::string>());
                    else if (action == "attribute")
                        editor.attribute(
                            request.at("element").get<std::size_t>(),
                            request.at("name").get<std::string>(),
                            request.at("value").is_null()
                                ? std::nullopt
                                : std::optional(request.at("value").get<std::string>()));
                    else if (action == "text")
                        editor.text(request.at("element").get<std::size_t>(),
                                    request.at("text").get<std::string>());
                    else if (action == "insert")
                        editor.insert(request.at("parent").get<std::size_t>(),
                                      request.at("tag").get<std::string>(),
                                      request.value("text", std::string{}),
                                      request.contains("before")
                                          ? std::optional(request.at("before").get<std::size_t>())
                                          : std::nullopt);
                    else if (action == "duplicate")
                        editor.duplicate(request.at("element").get<std::size_t>(),
                                         request.value("prefix", std::string{}));
                    else if (action == "remove")
                        editor.remove(request.at("element").get<std::size_t>());
                    else if (action == "remove_group")
                        editor.removes(request.at("elements").get<std::vector<std::size_t>>());
                    else if (action == "move")
                        editor.move(request.at("element").get<std::size_t>(),
                                    request.at("direction").get<int>());
                    else if (action == "save")
                        editor.save(files.document);
                    else if (action == "undo" || action == "redo") {
                        if (action == "undo" ? !editor.can_undo() : !editor.can_redo())
                            throw AuthoringError("history_empty",
                                                 "No UI source history in this direction");
                        editor.undo(action == "redo");
                    } else if (action == "reload")
                        editor.reload(files.document, request.value("discard", false));
                    else {
                        if (editor.dirty() && !request.value("discard", false))
                            throw AuthoringError("unsaved_source",
                                                 "Save or explicitly discard this UI draft");
                        editor.discard();
                    }
                    return state(editor, files.document);
                }));
        }
        return result;
    }
};
} // namespace forge
