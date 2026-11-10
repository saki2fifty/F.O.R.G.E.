#pragma once
#include "../ui_range_numeric.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "ui_source_editor.hpp"

namespace forge {
// Borrows the native widget's document/history; no agent-owned text or DOM state.
struct UiSourceAutomation {
    static Json pixel_properties() {
        auto result = Json::object();
        for (const auto* name : {"left", "top", "width", "height"})
            result[name] = {
                {"type", "number"},
                {"minimum",
                 std::string_view(name) == "left" || std::string_view(name) == "top" ? -65536 : 1},
                {"maximum", 65536}};
        return result;
    }
    static UiPixelLayout pixel_values(const Json& request) {
        return {request.at("left").get<float>(), request.at("top").get<float>(),
                request.at("width").get<float>(), request.at("height").get<float>()};
    }
    static Json style_profile(std::string_view name) {
        auto common = Json::array();
        for (const auto property : ui_common_style_properties)
            common.push_back(property);
        Json result{{"name", name},       {"known", false},
                    {"kind", "text"},     {"keywords", Json::array()},
                    {"numeric", nullptr}, {"common_properties", std::move(common)}};
        if (const auto profile = ui_style_literal_profile(name)) {
            result["known"] = true;
            result["kind"] = profile->color               ? "color"
                             : !profile->units.empty()    ? "number"
                             : !profile->keywords.empty() ? "keyword"
                                                          : "text";
            for (const auto keyword : profile->keywords)
                result["keywords"].push_back(keyword);
            if (!profile->units.empty()) {
                auto units = Json::array();
                for (const auto unit : profile->units)
                    units.push_back(ui_style_unit_suffixes[static_cast<std::size_t>(unit)]);
                result["numeric"] = {
                    {"units", std::move(units)},
                    {"minimum", profile->minimum},
                    {"maximum", profile->maximum},
                    {"integer", profile->integer},
                    {"initial_literal", format_ui_style_literal_number(name, profile->initial)}};
            }
        }
        return result;
    }
    static Json document_state(const ui::UiSourceEditor& editor, const SceneDocument& project) {
        project.check_ownership();
        if (editor.project() != project.project())
            return {{"open", false}};
        return editor.state();
    }
    static Json control_properties(const UiMarkupControlProperties& view) {
        Json rows = Json::array();
        for (const auto& item : view.properties) {
            Json value;
            if (const auto* flag = std::get_if<bool>(&item.value))
                value = *flag;
            else if (const auto* number = std::get_if<double>(&item.value))
                value = *number;
            else if (const auto* text = std::get_if<std::string>(&item.value))
                value = *text;
            const char* type = item.type == UiControlPropertyType::Flag     ? "boolean"
                               : item.type == UiControlPropertyType::Number ? "number"
                               : item.type == UiControlPropertyType::Choice ? "choice"
                                                                            : "text";
            rows.push_back({{"name", item.name},
                            {"label", item.label},
                            {"type", type},
                            {"choices", item.choices},
                            {"value", std::move(value)},
                            {"minimum", item.minimum ? Json(*item.minimum) : Json{}},
                            {"maximum", item.maximum ? Json(*item.maximum) : Json{}},
                            {"minimum_positive",
                             item.minimum_positive ? Json(*item.minimum_positive) : Json{}},
                            {"integer", item.integer},
                            {"authored", item.authored},
                            {"available", item.available},
                            {"editable", item.editable},
                            {"reason", item.reason}});
        }
        return {{"kind", view.kind},
                {"label", view.label},
                {"reason", view.reason},
                {"properties", std::move(rows)}};
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
        auto rules = Json::array();
        const auto& text = value.at("text").get_ref<const std::string&>();
        for (std::size_t i = 0; i < editor.rules().size(); ++i) {
            const auto& rule = editor.rules()[i];
            rules.push_back({{"index", i},
                             {"selector", text.substr(rule.selector.begin,
                                                      rule.selector.end - rule.selector.begin)},
                             {"editable", rule.flat},
                             {"source", range(rule.source)},
                             {"content", range(rule.content)}});
        }
        value["rules"] = std::move(rules);
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
            "Incomplete source reports outline_error. RCSS reports top-level rules, editable "
            "flat blocks and transient rule_selection; at-rules/nested blocks use Source.",
            "assets.read", automation_object(), false,
            [&files, &editor](const Json&) { return state(editor, files.document); }));
        const auto ready = [&files, &editor, unavailable] {
            files.document.check_ownership();
            const auto reason = unavailable ? unavailable() : std::string{};
            if (!reason.empty())
                throw AuthoringError("source_busy", reason);
            if (editor.close_pending() || editor.gesture_active())
                throw AuthoringError("source_busy",
                                     "Finish the native UI source dialog or source gesture first");
        };
        const Json guard{{"path", automation_text(2047, 1)},
                         {"session", automation_uuid()},
                         {"revision", automation_integer()},
                         {"state", automation_digest()}};
        const Json required{"path", "session", "revision", "state"};
        {
            auto properties = guard;
            properties["name"] = automation_text(64, 1);
            auto fields = required;
            fields.push_back("name");
            result.push_back(automation_method(
                "ui.source.style_profile",
                "Read optional literal input controls for a CSS property using current source "
                "guards/assets.read. Returns known/kind, keyword presets, numeric units/bounds/"
                "integer/initial_literal where available, and the shared Common property menu. "
                "These are source control conveniences, not native CSS admission, defaults, "
                "computed styling or loaded font-face guarantees. Unsupported/custom values "
                "remain in CSS value/Source. No source/selection/history/Save/registration or "
                "native initialization. Existing style_read/rule_read results remain unchanged.",
                "assets.read", automation_object(std::move(properties), std::move(fields)), false,
                [&files, &editor](const Json& request) {
                    observed(editor, files.document, request);
                    return style_profile(request.at("name").get<std::string>());
                }));
        }
        {
            auto properties = guard;
            auto fields = required;
            properties["element"] = automation_integer(0, 8191);
            fields.push_back("element");
            result.push_back(automation_method(
                "ui.source.control_properties",
                "Read typed authored/default control properties for one current RML source "
                "element. "
                "Uses native source guards and the same stateless helper as Elements. Reports "
                "kind, "
                "labels/types/choices, authored/available/editable facts and unsupported/binding "
                "reasons. "
                "Values are source literals/defaults, not computed/native Play state. Boolean "
                "flags "
                "use attribute presence; source numbers retain native-float precision. No source, "
                "selection/history, Save, registration, native initialization or game execution.",
                "assets.read", automation_object(std::move(properties), std::move(fields)), false,
                [&files, &editor](const Json& request) {
                    observed(editor, files.document, request);
                    return control_properties(
                        editor.control_properties(request.at("element").get<std::size_t>()));
                }));
        }
        for (const bool sheet : {false, true}) {
            const auto* index = sheet ? "rule" : "element";
            auto properties = guard;
            properties[index] = automation_integer(0, sheet ? 4095 : 8191);
            properties["name"] = automation_text(64, 1);
            auto fields = required;
            fields.push_back(index);
            fields.push_back("name");
            result.push_back(automation_method(
                sheet ? "ui.source.rule_read" : "ui.source.style_read",
                std::string(
                    sheet ? "Read an authored property from a flat RCSS rule. At-rules/"
                            "nested blocks use Source. Needs current source guards and rule/name. "
                          : "Read an authored inline CSS property. Needs current source guards and "
                            "element/name; one plain quoted non-entity style is required. ") +
                    "Requires assets.read; returns name/value, null when absent. Unique literal "
                    "matching properties only, not computed styling/binding evaluation. No "
                    "native initialization, selection/history mutation, Save or registration.",
                "assets.read", automation_object(std::move(properties), std::move(fields)), false,
                [&files, &editor, sheet, index](const Json& request) {
                    observed(editor, files.document, request);
                    const auto i = request.at(index).get<std::size_t>();
                    const auto name = request.at("name").get<std::string>();
                    const auto value =
                        sheet ? editor.read_rule_property(i, name) : editor.read_style(i, name);
                    return Json{{"name", name}, {"value", value ? Json(*value) : Json(nullptr)}};
                }));
        }
        for (const bool classes : {false, true}) {
            auto properties = guard;
            properties["elements"] =
                automation_array(automation_integer(0, 8191), ui_source_selection_limit, 1);
            auto fields = required;
            fields.push_back("elements");
            if (!classes) {
                properties["name"] = automation_text(64, 1);
                fields.push_back("name");
            }
            result.push_back(automation_method(
                classes ? "ui.source.classes_read" : "ui.source.style_group_read",
                "Read authored values for1..32distinct editable RML siblings with current source "
                "guards/assets.read. "
                "Pass elements, plus name for a CSS property. Return ordered element/value rows, "
                "mixed, "
                "and a uniform value (null when absent or mixed; inspect mixed to distinguish). "
                "Class "
                "values are decoded literals; ambiguous/dynamic/NUL-containing classes use Source. "
                "Inline styles share the existing plain quoted/unique-property profile. No native "
                "initialization, computed styling, selection/history mutation, Save or "
                "registration.",
                "assets.read", automation_object(std::move(properties), std::move(fields)), false,
                [&files, &editor, classes](const Json& request) {
                    observed(editor, files.document, request);
                    const auto indices = request.at("elements").get<std::vector<std::size_t>>();
                    const auto values =
                        classes
                            ? editor.read_classes(indices)
                            : editor.read_styles(indices, request.at("name").get<std::string>());
                    const bool mixed = !std::ranges::all_of(
                        values, [&](const auto& v) { return v == values.front(); });
                    auto rows = Json::array();
                    for (std::size_t i = 0; i < indices.size(); ++i)
                        rows.push_back({{"element", indices[i]},
                                        {"value", values[i] ? Json(*values[i]) : Json(nullptr)}});
                    Json value{{"values", std::move(rows)},
                               {"mixed", mixed},
                               {"value",
                                !mixed && values.front() ? Json(*values.front()) : Json(nullptr)}};
                    if (!classes)
                        value["name"] = request.at("name");
                    return value;
                }));
        }
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
        layout_properties.update(pixel_properties());
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
                editor.layout(request.at("element").get<std::size_t>(), pixel_values(request),
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
            "Move, align border edges/centers, distribute equal gaps or match border dimensions "
            "for2..32 distinct "
            "same-parent native-admitted source elements. Distribution needs3elements. Native "
            "bounds choose geometry. match_width/height/size use the last requested member as "
            "reference, preserve positions and validate actual native target dimensions. Other "
            "actions preserve source sizes. Unrelated text remains unchanged. dx/dy "
            "are optional pixel offsets for move only. Rechecks exact source/native preview "
            "guards and the complete candidate before one source Undo record. Unsupported, "
            "duplicate, out-of-range or stale members reject the whole operation. No Save, "
            "resource registration, Scene history, relative/flow layout or general group-scaling "
            "gestures.",
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
             {"replace",       "attribute",     "style",       "text",         "insert",
              "duplicate",     "reparent",      "remove",      "remove_group", "move",
              "save",          "undo",          "redo",        "reload",       "close",
              "rule_property", "rule_selector", "rule_insert", "rule_remove",  "rule_copy",
              "rule_move",     "style_group",   "classes",     "control",      "control_values",
              "place_control"}) {
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
            if (action.starts_with("rule_") && action != "rule_insert")
                add("rule", automation_integer(0, 4095));
            if (action == "rule_selector" || action == "rule_insert")
                add("selector", automation_text(1024, 1));
            if (action == "attribute" || action == "style" || action == "text" ||
                action == "duplicate" || action == "remove" || action == "move" ||
                action == "control_values")
                add("element", automation_integer(0, 8191));
            if (action == "duplicate")
                add("prefix", automation_text(64), false);
            if (action == "remove_group" || action == "reparent" || action == "style_group" ||
                action == "classes")
                add("elements",
                    automation_array(automation_integer(0, 8191), ui_source_selection_limit, 1));
            if (action == "attribute" || action == "style" || action == "rule_property" ||
                action == "style_group") {
                add("name", automation_text(64, 1));
                add("value", {{"oneOf",
                               {automation_text(action == "attribute" ? 256 * 1024 : 8192),
                                Json{{"type", "null"}}}}});
            }
            if (action == "classes")
                add("value", {{"oneOf", {automation_text(1024, 1), Json{{"type", "null"}}}}});
            if (action == "text" || action == "insert")
                add("text", automation_text(256 * 1024), action == "text");
            if (action == "control_values") {
                Json values = Json::object();
                const Json value{{"oneOf",
                                  {Json{{"type", "null"}}, Json{{"type", "boolean"}},
                                   Json{{"type", "number"},
                                        {"minimum", -ui_range_magnitude},
                                        {"maximum", ui_range_magnitude}},
                                   automation_text(8192)}}};
                for (const auto& name : ui_markup_control_property_names())
                    values[name] = value;
                add("values", automation_object(std::move(values)));
            }
            if (action == "control" || action == "place_control") {
                const bool placement = action == "place_control";
                auto kinds = Json::array();
                for (const auto& kind : ui_markup_control_kinds)
                    if (!placement || (kind.tag != "br" && kind.tag != "option"))
                        kinds.push_back(kind.id);
                add("kind", {{"type", "string"}, {"enum", std::move(kinds)}});
                add("text", automation_text(8192), false);
                add("id", automation_text(128, placement ? 1 : 0), placement);
                if (placement) {
                    add("x", {{"type", "number"}, {"minimum", 0}, {"maximum", 2048}});
                    add("y", {{"type", "number"}, {"minimum", 0}, {"maximum", 2048}});
                    const auto geometry = pixel_properties();
                    add("width", geometry.at("width"));
                    add("height", geometry.at("height"));
                    add("preview_instance", automation_uuid());
                    add("preview_revision", automation_integer());
                } else
                    add("pixels",
                        automation_object(pixel_properties(), {"left", "top", "width", "height"}),
                        false);
            }
            if (action == "insert" || action == "reparent" || action == "control" ||
                action == "place_control") {
                add("parent", automation_integer(0, 8191));
                if (action == "insert")
                    add("tag", automation_text(64, 1));
                if (action != "place_control")
                    add("before", automation_integer(0, 8191), false);
            }
            if (action == "move" || action == "rule_move")
                add("direction", {{"type", "integer"}, {"enum", {-1, 1}}});
            if (action == "close" || action == "reload")
                add("discard", {{"type", "boolean"}}, false);
            result.push_back(automation_method(
                "ui.source." + action,
                action == "control_values"
                    ? "Set typed authored properties on one current RML source control. Read "
                      "control_properties first. Null removes an attribute/inline override; "
                      "boolean "
                      "flags use presence/removal. Numbers are finite native floats within "
                      "+/-1000000; "
                      "slider step is at least0.000001. Image rect is empty or four "
                      "space-separated "
                      "finite numbers x y width height with crop dimensions0.000001..1000000. "
                      "Image size "
                      "attributes admit0 or0.000001..1000000; zero omits that size request. Source "
                      "queries "
                      "do not resolve intrinsic dimensions. Read each row's "
                      "minimum/maximum/integer "
                      "constraints for text length/dimensions. Password initial values are "
                      "authored "
                      "source, never a live password observation. Strings fit8192bytes without "
                      "NUL/dynamic "
                      "expressions; choices follow the property profile. Bound/ambiguous "
                      "properties "
                      "reject; explicit valid replacements can repair unsupported plain literals. "
                      "Compose privately, validate and publish once through the native draft "
                      "owner, "
                      "retaining the selected element and one Undo. Preserve unrelated source, IDs "
                      "and bindings; no runtime normalization/state, Save, catalog or Scene "
                      "writes. "
                      "Requires current source guards and assets.write; old raw tools remain."
                : action == "place_control"
                    ? "Append a named control inside the selected editable absolute-pixel source "
                      "container at viewport x/y. Requires current source guards, preview instance/"
                      "revision and assets.write. The point must lie inside its current native "
                      "padding box and viewport. Native padding geometry supplies local pixels; "
                      "the configured candidate uses zero margin and validates actual native "
                      "corner/"
                      "canvas eligibility before one source Undo. Width/height are authored CSS "
                      "pixels; native box sizing still applies. Breaks/options are excluded. "
                      "No source/history publication on invalid/stale/unsupported candidates; "
                      "refresh Preview after a refused native candidate. No Save, catalog, Scene "
                      "or runtime execution. Selects the created root; old source control creation "
                      "remains supported."
                : action == "control"
                    ? "Create one common RML control with parent/kind, optional initial text/value "
                      "and optional before-child index. Compose existing escaped source insert/"
                      "attribute/text candidates, validate before one Undo and select the new "
                      "root. "
                      "Needs current source guards/assets.write. Initial text fits8192bytes; "
                      "inputs "
                      "use it as value, dropdowns create one selected option, ordinary controls "
                      "use "
                      "text. Image/break/progress accept no text; progress starts at0. Dropdowns "
                      "contain only option controls; options require a dropdown. Text "
                      "areas/options/"
                      "progress cannot parent new controls. Optional id assigns a unique literal "
                      "source ID of at most128bytes. Optional pixels supplies finite "
                      "left/top/width/height "
                      "within+/-65536 with size>=1 and requires id; breaks/options keep flow "
                      "layout. "
                      "Pixel setup rejects bound ancestors. Old calls preserve flow/source bytes; "
                      "Preview eligibility still follows native geometry/cascade. No automatic "
                      "IDs/label targets or resource "
                      "registration or runtime execution. Generic insert remains supported; Save "
                      "stays explicit."
                : action == "classes"
                    ? "Set literal class lists for1..32distinct editable RML siblings as one Undo. "
                      "Needs elements/value and current source guards/assets.write; null clears. "
                      "Space-separated ASCII classes,1..32distinct tokens/64bytes each/1024total, "
                      "starting with letter/underscore. Direct/encoded expressions and ambiguous "
                      "attributes use Source. Preserve IDs, other source and data-class bindings. "
                      "Logical equal values preserve original encoding/quotes. Changed source "
                      "retains the selected group; exact no-op retains history. Save stays "
                      "explicit."
                : action == "style_group"
                    ? "Set/remove one authored inline CSS property for1..32distinct editable RML "
                      "siblings in one Undo. Needs elements/name/value and current "
                      "guards/assets.write. "
                      "Same literal inline name/value/quote/duplicate/source budgets as style. "
                      "Validate every member and full candidate before source replacement. "
                      "Preserve "
                      "other source/bindings, retain edited selection, exact no-op history and "
                      "explicit Save."
                : action == "rule_copy" || action == "rule_move"
                    ? "Copy or reorder the observed flat RCSS rule with current "
                      "guards/assets.write. "
                      "Copy inserts the unchanged block/selector after itself with a newline and "
                      "selects it; "
                      "move takes direction -1/up or1/down, swaps adjacent flat blocks and selects "
                      "the moved rule. "
                      "At-rules/nested barriers use Source. Preserve block/interstitial bytes, one "
                      "Undo, "
                      "boundary/identical no-op history. Order affects native cascade; no "
                      "automatic selector "
                      "renaming or computed-style rewrite. Save remains separate.4096block outline "
                      "cap."
                : action.starts_with("rule_")
                    ? action +
                          " in the observed RCSS source with current guards/assets.write. "
                          "Flat ordinary rules only; at-rules/nested blocks use Source. Property "
                          "name/value edits share literal declaration admission, null removes. "
                          "Lowercase names up to64bytes, one value up to8192bytes, no duplicate "
                          "matching property or extra declarations. Selector/insert takes one "
                          "literal selector up to1024bytes, no "
                          "at-rules/comments/escapes/injection. "
                          "Insert appends; remove deletes that block. Preserve unrelated source, "
                          "rule order and comments; one Undo, exact no-op retains history. No "
                          "computed styling, Save or registration. Save then refresh RML Preview/"
                          "Reload UI to test native cascade."
                : action == "style"
                    ? "Set one literal inline CSS property or remove it with value:null. "
                      "Needs element/name/value and current source guards/assets.write. Preserve "
                      "unrelated source, declarations/comments and bindings in one Undo step; "
                      "exact no-ops retain source history. Lowercase name starts with a letter, "
                      "up to64bytes; literal value up to8192bytes. Requires one quoted non-entity "
                      "style, no duplicate matching properties, quote conflicts, extra "
                      "declarations "
                      "or markup injection. Full source admission applies; native CSS behavior is "
                      "checked in Preview/Play. This edits authored values; bindings can override "
                      "them. No Save/registration."
                : action == "reparent"
                    ? "Move1..32 static source siblings into parent, appending or placing before "
                      "its optional direct child. Preserve source order, moved bytes and IDs in "
                      "one Undo step. Cycles, protected/dynamic/ambiguous source and invalid "
                      "destinations reject atomically. Styles/parent-relative coordinates follow "
                      "the new hierarchy; no automatic layout or reference rewrite. Requires "
                      "current source guards/assets.write; no Save/registration/Scene history."
                : action == "duplicate"
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
                    else if (action == "rule_property")
                        editor.rule_property(
                            request.at("rule").get<std::size_t>(),
                            request.at("name").get<std::string>(),
                            request.at("value").is_null()
                                ? std::nullopt
                                : std::optional(request.at("value").get<std::string>()));
                    else if (action == "rule_selector")
                        editor.rule_selector(request.at("rule").get<std::size_t>(),
                                             request.at("selector").get<std::string>());
                    else if (action == "rule_insert")
                        editor.add_rule(request.at("selector").get<std::string>());
                    else if (action == "rule_remove")
                        editor.remove_rule(request.at("rule").get<std::size_t>());
                    else if (action == "rule_copy")
                        editor.copy_rule(request.at("rule").get<std::size_t>());
                    else if (action == "rule_move")
                        editor.move_rule(request.at("rule").get<std::size_t>(),
                                         request.at("direction").get<int>());
                    else if (action == "style_group")
                        editor.styles(request.at("elements").get<std::vector<std::size_t>>(),
                                      request.at("name").get<std::string>(),
                                      request.at("value").is_null()
                                          ? std::nullopt
                                          : std::optional(request.at("value").get<std::string>()));
                    else if (action == "classes")
                        editor.classes(request.at("elements").get<std::vector<std::size_t>>(),
                                       request.at("value").is_null()
                                           ? std::nullopt
                                           : std::optional(request.at("value").get<std::string>()));
                    else if (action == "attribute")
                        editor.attribute(
                            request.at("element").get<std::size_t>(),
                            request.at("name").get<std::string>(),
                            request.at("value").is_null()
                                ? std::nullopt
                                : std::optional(request.at("value").get<std::string>()));
                    else if (action == "style")
                        editor.style(request.at("element").get<std::size_t>(),
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
                    else if (action == "control_values") {
                        std::map<std::string, UiControlPropertyValue> values;
                        for (const auto& [name, value] : request.at("values").items()) {
                            if (value.is_null())
                                values.emplace(name, std::monostate{});
                            else if (value.is_boolean())
                                values.emplace(name, value.get<bool>());
                            else if (value.is_number())
                                values.emplace(name, value.get<double>());
                            else if (value.is_string())
                                values.emplace(name, value.get<std::string>());
                            else
                                throw AuthoringError(
                                    "invalid_arguments",
                                    "Use null, boolean, number or string control values");
                        }
                        editor.control_values(request.at("element").get<std::size_t>(), values);
                    } else if (action == "place_control")
                        editor.place_control(
                            request.at("parent").get<std::size_t>(),
                            request.at("kind").get<std::string>(),
                            request.value("text", std::string{}),
                            request.at("id").get<std::string>(),
                            {request.at("x").get<float>(), request.at("y").get<float>(),
                             request.at("width").get<float>(), request.at("height").get<float>()},
                            request.at("preview_instance").get<std::string>(),
                            request.at("preview_revision").get<std::uint64_t>());
                    else if (action == "control")
                        editor.control(request.at("parent").get<std::size_t>(),
                                       request.at("kind").get<std::string>(),
                                       request.value("text", std::string{}),
                                       request.contains("before")
                                           ? std::optional(request.at("before").get<std::size_t>())
                                           : std::nullopt,
                                       request.value("id", std::string{}),
                                       request.contains("pixels")
                                           ? std::optional(pixel_values(request.at("pixels")))
                                           : std::nullopt);
                    else if (action == "duplicate")
                        editor.duplicate(request.at("element").get<std::size_t>(),
                                         request.value("prefix", std::string{}));
                    else if (action == "reparent")
                        editor.reparent(request.at("elements").get<std::vector<std::size_t>>(),
                                        request.at("parent").get<std::size_t>(),
                                        request.contains("before")
                                            ? std::optional(request.at("before").get<std::size_t>())
                                            : std::nullopt);
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
