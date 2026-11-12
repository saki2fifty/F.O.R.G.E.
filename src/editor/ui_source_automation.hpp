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
            "flat blocks and transient rule_selection. Width-conditioned blocks use media_read; "
            "other at-rules/nested forms remain in Source.",
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
            properties["asset"] = automation_uuid();
            auto fields = required;
            fields.push_back("asset");
            result.push_back(automation_method(
                "ui.source.image_reference",
                "Resolve one registered standalone project image to the current UI source's "
                "document-relative reference. Requires current source guards/assets.read. Uses "
                "the shared UI resource path, identity and image-byte admission owners; PNG, "
                "JPEG and TGA source files only. Returns asset/source/reference/details. Rejects "
                "foreign, missing, ambiguous, embedded or unsupported resources. No source "
                "selection, staging, Undo, Save, registration, native context, GPU or gameplay "
                "execution. Use control_values src to author the returned reference; a nonempty "
                "sprite retains its existing precedence.",
                "assets.read", automation_object(std::move(properties), std::move(fields)), false,
                [&files, &editor](const Json& request) {
                    observed(editor, files.document, request);
                    try {
                        return ui_image_reference(
                            AssetCatalog::open_project(files.document.project()),
                            std::filesystem::u8path(editor.state().at("path").get<std::string>()),
                            request.at("asset").get<AssetId>());
                    } catch (const std::exception& error) {
                        throw AuthoringError("ui_image_reference", error.what());
                    }
                }));
        }
        {
            auto properties = guard;
            properties["asset"] = automation_uuid();
            auto fields = required;
            fields.push_back("asset");
            result.push_back(automation_method(
                "ui.source.material_reference",
                "Resolve a published UI-domain Material into a decorator CSS value using the "
                "shared cooked Material loader and UI-domain admission. Requires current source "
                "guards/assets.read. Returns asset/value/shader/shader_revision; the Shader is "
                "the immutable snapshot embedded in that Material. Missing, unpublished, corrupt "
                "or non-UI Materials fail. No source changes, registration, publication, GPU "
                "readiness or dependency declaration. Use style/style_group/rule_property with "
                "name decorator to author value, then Save source. Runtime Dependencies remain "
                "explicit; assigning replaces the complete decorator list.",
                "assets.read", automation_object(std::move(properties), std::move(fields)), false,
                [&files, &editor](const Json& request) {
                    observed(editor, files.document, request);
                    try {
                        return ui_material_reference(
                            AssetCatalog::open_project(files.document.project()),
                            request.at("asset").get<AssetId>());
                    } catch (const std::exception& error) {
                        throw AuthoringError("ui_material_reference", error.what());
                    }
                }));
        }
        for (const std::string action :
             {"stylesheet_reference", "stylesheet_links", "stylesheet_link",
              "stylesheet_link_remove", "stylesheet_link_move"}) {
            const bool reference = action == "stylesheet_reference";
            const bool read_links = action == "stylesheet_links";
            const bool write = action == "stylesheet_link";
            const bool move = action == "stylesheet_link_move";
            const bool mutating = !reference && !read_links;
            if (mutating && !permissions.assets)
                continue;
            auto properties = guard;
            auto fields = required;
            if (reference || write) {
                properties["asset"] = automation_uuid();
                fields.push_back("asset");
            }
            if (mutating) {
                properties["element"] = automation_integer(0, 8191);
                if (!write)
                    fields.push_back("element");
            }
            if (move) {
                properties["direction"] = {{"type", "integer"}, {"enum", {-1, 1}}};
                fields.push_back("direction");
            }
            std::string description;
            if (reference)
                description =
                    "Read a registered project RCSS resource's current document-relative "
                    "reference/source/details for the open RML draft. Current source guards "
                    "and assets.read required. Uses shared catalog/path/current-byte admission; "
                    "no source/history/selection/registration/native/GPU mutation.";
            else if (read_links)
                description =
                    "Read up to32 literal direct-head RCSS/CSS links in the open RML draft, "
                    "in source order: transient element index, decoded reference and literal type. "
                    "Current guards/assets.read required. No mutation. Duplicate/bound/ambiguous "
                    "header forms require Source. Indices belong to the observed draft revision.";
            else if (write)
                description =
                    "Append or replace a literal head stylesheet link using a registered RCSS "
                    "asset UUID. Optional element selects an existing link; omit to append. "
                    "Re-admits current bytes and a contained document-relative reference. "
                    "Creates a missing head or expands an empty self-closing head explicitly. "
                    "Requires one complete RML root/current guards/assets.write/replay; one Undo, "
                    "retained source selection and explicit Save. Other metadata/comments/body "
                    "remain; unchanged decoded references retain source spelling. Generic "
                    "metadata editing barriers remain; no native load/registration/Scene edit.";
            else if (move)
                description =
                    "Move a literal RML stylesheet link to the adjacent head link: direction "
                    "-1(up) or1(down). Other head nodes, including inline styles, are barriers. "
                    "Current source guards/assets.write/replay; one Undo, retained selection, "
                    "explicit Save. Comments stay between links; native cascade follows order.";
            else
                description =
                    "Remove one literal direct-head RML stylesheet link by observed element "
                    "index. Current source guards/assets.write/replay; one source Undo and "
                    "explicit Save. Other head content/body remain; registration is separate.";
            result.push_back(automation_method(
                "ui.source." + action, description, mutating ? "assets.write" : "assets.read",
                automation_object(std::move(properties), std::move(fields)), mutating,
                [&files, &editor, ready, reference, read_links, write, move,
                 mutating](const Json& request) -> Json {
                    if (mutating)
                        ready();
                    observed(editor, files.document, request);
                    if (reference) {
                        try {
                            return editor.stylesheet_reference(request.at("asset").get<AssetId>());
                        } catch (const std::exception& error) {
                            throw AuthoringError("ui_stylesheet_reference", error.what());
                        }
                    }
                    if (read_links) {
                        auto links = Json::array();
                        for (const auto& link : editor.stylesheet_links())
                            links.push_back({{"element", link.element},
                                             {"reference", link.reference},
                                             {"type", link.type}});
                        return Json{{"links", std::move(links)}};
                    }
                    if (write)
                        editor.stylesheet_link(
                            request.contains("element")
                                ? std::optional(request.at("element").get<std::size_t>())
                                : std::nullopt,
                            request.at("asset").get<AssetId>());
                    else if (move)
                        editor.move_stylesheet_link(request.at("element").get<std::size_t>(),
                                                    request.at("direction").get<int>());
                    else
                        editor.remove_stylesheet_link(request.at("element").get<std::size_t>());
                    return state(editor, files.document);
                }));
        }
        for (const std::string action :
             {"media_read", "media_query", "media_rule", "media_remove"}) {
            const bool read = action == "media_read", query = action == "media_query",
                       rule = action == "media_rule";
            if (!read && !permissions.assets)
                continue;
            auto properties = guard;
            auto fields = required;
            properties["rule"] = automation_integer(0, 4095);
            if (!query)
                fields.push_back("rule");
            if (query) {
                const auto bound = automation_object(
                    {{"value", {{"type", "number"}, {"minimum", 0}, {"maximum", 65536}}},
                     {"unit", {{"type", "string"}, {"enum", {"px", "dp"}}}}},
                    {"value", "unit"});
                for (const auto* name : {"minimum", "maximum"})
                    properties[name] = {{"oneOf", Json::array({bound, Json{{"type", "null"}}})}};
            }
            if (rule || action == "media_remove")
                properties["child"] = automation_integer(0, 4095);
            if (rule) {
                properties["selector"] = automation_text(1024, 1);
                fields.push_back("selector");
                properties["properties"] = {
                    {"type", "object"},
                    {"maxProperties", 32},
                    {"propertyNames", automation_text(64, 1)},
                    {"additionalProperties",
                     {{"oneOf", Json::array({automation_text(8192, 1), Json{{"type", "null"}}})}}}};
            }
            result.push_back(automation_method(
                "ui.source." + action,
                std::string(read    ? "Read a literal viewport-width media block, px/dp bounds and "
                                      "flat child selectors/declarations. "
                            : query ? "Create (omit rule) or replace a viewport-width condition; "
                                      "minimum/maximum are value/unit objects or null. At least "
                                      "one bound required. "
                            : rule  ? "Append (omit child) or edit one flat child selector and "
                                      "optional property map in a viewport-width media block. Null "
                                      "property values remove overrides. "
                                    : "Remove one flat media child, or omit child to remove the "
                                      "admitted whole width block. ") +
                    "Current RCSS source guards required. Static min/max-width conditions joined "
                    "by and only; other queries/nesting use Source. Native CSS evaluates inclusive "
                    "bounds and density. "
                    "Source-only; no native initialization or cascade flattening. Mutations use "
                    "assets.write/replay, one Undo and explicit Save; preserve unrelated source "
                    "and Scene/catalog ownership.",
                read ? "assets.read" : "assets.write",
                automation_object(std::move(properties), std::move(fields)), !read,
                [&files, &editor, ready, read, query, rule](const Json& request) {
                    if (!read)
                        ready();
                    observed(editor, files.document, request);
                    if (read) {
                        const auto block =
                            editor.media_block(request.at("rule").get<std::size_t>());
                        const auto text = editor.state().at("text").get<std::string>();
                        const auto number = [](const std::optional<UiStyleNumber>& value) -> Json {
                            return value ? Json{{"value", value->value},
                                                {"unit",
                                                 ui_style_unit_suffixes[std::size_t(value->unit)]}}
                                         : Json(nullptr);
                        };
                        auto children = Json::array();
                        for (std::size_t i = 0; i < block.rules.size(); ++i) {
                            const auto& child = block.rules[i];
                            const auto body = std::string_view(text).substr(
                                child.content.begin, child.content.end - child.content.begin);
                            auto declarations = Json::array();
                            for (const auto& item : inspect_ui_style(body))
                                declarations.push_back(
                                    {{"name", item.name},
                                     {"value", body.substr(item.value.begin,
                                                           item.value.end - item.value.begin)}});
                            children.push_back({{"index", i},
                                                {"selector", text.substr(child.selector.begin,
                                                                         child.selector.end -
                                                                             child.selector.begin)},
                                                {"properties", std::move(declarations)}});
                        }
                        return Json{{"minimum", number(block.query.minimum)},
                                    {"maximum", number(block.query.maximum)},
                                    {"children", std::move(children)}};
                    }
                    const auto index = request.contains("rule")
                                           ? std::optional(request.at("rule").get<std::size_t>())
                                           : std::nullopt;
                    const auto child = request.contains("child")
                                           ? std::optional(request.at("child").get<std::size_t>())
                                           : std::nullopt;
                    if (query) {
                        const auto bound = [&](const char* name) -> std::optional<UiStyleNumber> {
                            if (!request.contains(name) || request.at(name).is_null())
                                return {};
                            const auto& value = request.at(name);
                            return UiStyleNumber{value.at("value").get<double>(),
                                                 value.at("unit") == "px"
                                                     ? UiStyleUnit::Pixels
                                                     : UiStyleUnit::DensityPixels};
                        };
                        editor.media_query(index, {bound("minimum"), bound("maximum")});
                    } else if (rule) {
                        std::map<std::string, std::optional<std::string>> values;
                        const auto authored = request.value("properties", Json::object());
                        for (const auto& [name, value] : authored.items())
                            values[name] = value.is_null()
                                               ? std::nullopt
                                               : std::optional(value.get<std::string>());
                        editor.media_rule(*index, child, request.at("selector").get<std::string>(),
                                          values);
                    } else
                        editor.remove_media_rule(*index, child);
                    return state(editor, files.document);
                }));
        }
        for (const std::string action :
             {"font_reference", "font_face_read", "font_face", "font_face_remove"}) {
            auto properties = guard;
            auto fields = required;
            const bool reference = action == "font_reference";
            const bool read_face = action == "font_face_read";
            const bool write_face = action == "font_face";
            const bool mutating = write_face || action == "font_face_remove";
            if (mutating && !permissions.assets)
                continue;
            if (reference || write_face) {
                properties["asset"] = automation_uuid();
                fields.push_back("asset");
                properties["face_index"] = automation_integer(0, 31);
            }
            if (!reference) {
                properties["rule"] = automation_integer(0, 4095);
                if (!write_face)
                    fields.push_back("rule");
            }
            if (write_face) {
                properties["family"] = automation_text(64, 1);
                fields.push_back("family");
                properties["weight"] = Json{
                    {"oneOf",
                     Json::array({automation_integer(1, 1000),
                                  Json{{"type", "string"}, {"enum", {"all", "normal", "bold"}}},
                                  Json{{"type", "null"}}})}};
                properties["style"] = Json{
                    {"oneOf", Json::array({Json{{"type", "string"}, {"enum", {"normal", "italic"}}},
                                           Json{{"type", "null"}}})}};
                properties["fallback"] = Json{
                    {"oneOf", Json::array({Json{{"type", "boolean"}}, Json{{"type", "null"}}})}};
            }
            const std::string description =
                reference ? "Read a registered standalone project UI font's current source and "
                            "face metadata. "
                            "Requires current source guards/assets.read. Optional zero-based "
                            "face_index defaults to0. "
                            "Returns asset/source/face_index/selected_face/details through shared "
                            "actual-byte font admission. "
                            "Font sources are project relative. Recorded family names are "
                            "informational, not authored aliases "
                            "or loaded availability. No native/GPU/gameplay initialization or "
                            "source/catalog/history mutation."
                : read_face
                    ? "Read authored literal font-family/src/-rmlui-face-index and optional "
                      "font-weight/font-style/-rmlui-fallback-face values from a "
                      "current top-level literal @font-face "
                      "in an RCSS draft. Current source guards/assets.read required. Uses "
                      "the existing stylesheet "
                      "source ranges/declaration owner; no selection/history/source or "
                      "font-load changes."
                : write_face
                    ? "Add or update one literal RCSS @font-face with an explicit plain family "
                      "alias, registered project "
                      "font UUID and optional face_index(default0). Omit rule to append. "
                      "Optional weight accepts integer1..1000 or all/normal/bold; style accepts "
                      "normal/italic. Null removes that override; omission preserves it. "
                      "Font-face all uses native automatic selection; absent style is normal. "
                      "Optional boolean fallback controls native missing-glyph fallback "
                      "participation (default false); null removes the override. "
                      "Re-admits actual font bytes/face; "
                      "writes a project-relative src, preserving other "
                      "declarations/comments/rules. Plain aliases are "
                      "1..64 ASCII letters/digits/_/-, starting with a letter or _. Uses existing "
                      "source owner: one Undo, "
                      "retained rule selection, explicit Save, current guards/assets.write/replay. "
                      "No registration, "
                      "automatic font-family assignment, native font load or Scene history."
                    : "Remove one literal top-level RCSS @font-face through the existing source "
                      "owner. Requires current "
                      "source guards/assets.write; one Undo and explicit Save. Other source "
                      "remains unchanged; styles "
                      "that use its alias may fall back. Other at-rules retain Source editing.";
            result.push_back(automation_method(
                "ui.source." + action, description, mutating ? "assets.write" : "assets.read",
                automation_object(std::move(properties), std::move(fields)), mutating,
                [&files, &editor, ready, reference, read_face, write_face,
                 mutating](const Json& request) -> Json {
                    if (mutating)
                        ready();
                    observed(editor, files.document, request);
                    if (reference) {
                        try {
                            return ui_font_reference(
                                AssetCatalog::open_project(files.document.project()),
                                request.at("asset").get<AssetId>(),
                                request.value("face_index", 0u));
                        } catch (const std::exception& error) {
                            throw AuthoringError("ui_font_reference", error.what());
                        }
                    }
                    if (read_face)
                        return Json{{"rule", request.at("rule")},
                                    {"values", editor.font_face_properties(
                                                   request.at("rule").get<std::size_t>())}};
                    if (write_face) {
                        std::map<std::string, std::optional<std::string>> appearance;
                        for (const auto* name : {"weight", "style", "fallback"})
                            if (request.contains(name)) {
                                const auto& value = request.at(name);
                                appearance[std::string_view(name) == "fallback"
                                               ? "-rmlui-fallback-face"
                                               : std::string("font-") + name] =
                                    value.is_null()      ? std::nullopt
                                    : value.is_boolean() ? std::optional<std::string>(
                                                               value.get<bool>() ? "true" : "false")
                                    : value.is_number_integer()
                                        ? std::optional(std::to_string(value.get<int>()))
                                        : std::optional(value.get<std::string>());
                            }
                        editor.font_face(request.contains("rule")
                                             ? std::optional(request.at("rule").get<std::size_t>())
                                             : std::nullopt,
                                         request.at("asset").get<AssetId>(),
                                         request.value("face_index", 0u),
                                         request.at("family").get<std::string>(), appearance);
                    } else
                        editor.remove_font_face(request.at("rule").get<std::size_t>());
                    return state(editor, files.document);
                }));
        }
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
            "Returns native border boxes, visibility/ID uniqueness, source indices, editable "
            "flags/reasons and a transient "
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
        for (const bool rectangle : {false, true}) {
            auto properties = guard;
            auto fields = required;
            if (rectangle) {
                for (const auto* name : {"left", "top", "width", "height"}) {
                    properties[name] = {{"type", "number"}, {"minimum", 0}, {"maximum", 2048}};
                    fields.push_back(name);
                }
                properties["add"] = {{"type", "boolean"}, {"default", false}};
                properties["preview_instance"] = automation_uuid();
                properties["preview_revision"] = automation_integer();
                fields.push_back("preview_instance");
                fields.push_back("preview_revision");
            } else {
                properties["elements"] =
                    automation_array(automation_integer(0, 8191), ui_source_selection_limit, 0);
                fields.push_back("elements");
            }
            result.push_back(automation_method(
                rectangle ? "ui.source.select_rectangle" : "ui.source.select",
                std::string(rectangle
                                ? "Select visible uniquely named source elements whose complete "
                                  "native border boxes fit inside the viewport rectangle. Requires "
                                  "current source and native preview guards; metadata-only hosts "
                                  "are supported. Native viewport pixels, nonnegative coordinates/"
                                  "sizes, entirely inside Preview. Zero area matches nothing. "
                                  "Default replaces selection in source order; add=true preserves "
                                  "existing order and appends new members in source order. "
                                : "Replace the ordered source element selection with0..32 distinct "
                                  "current RML element indices. Empty clears; last is active. "
                                  "Requires current source guards; no native preview is needed. ") +
                    "Uses the same selection as Elements and Preview. At most32 total members; "
                    "invalid, stale or oversized results refuse atomically. Requires assets.write "
                    "and the ordinary mutation envelope/replay. Selection does not change source "
                    "revision, dirty state, Undo, Save, registration or Scene history. Selecting "
                    "an element does not make its layout/style editable.",
                "assets.write", automation_object(std::move(properties), std::move(fields)), true,
                [&files, &editor, ready, rectangle](const Json& request) {
                    ready();
                    observed(editor, files.document, request);
                    if (rectangle)
                        editor.select_rectangle(
                            {request.at("left").get<float>(), request.at("top").get<float>(),
                             request.at("width").get<float>(), request.at("height").get<float>()},
                            request.value("add", false),
                            request.at("preview_instance").get<std::string>(),
                            request.at("preview_revision").get<std::uint64_t>());
                    else
                        editor.selection(request.at("elements").get<std::vector<std::size_t>>());
                    return state(editor, files.document);
                }));
        }
        auto preset_properties = guard;
        auto presets = Json::array();
        for (const auto& [key, label] : ui_responsive_layout_presets)
            presets.push_back(key);
        preset_properties["element"] = automation_integer(0, 8191);
        preset_properties["preset"] = {{"type", "string"}, {"enum", std::move(presets)}};
        for (const auto* name : {"width", "height", "horizontal_offset", "vertical_offset"})
            preset_properties[name] = {
                {"type", "number"},
                {"minimum", std::string_view(name).ends_with("offset") ? -65536 : 1},
                {"maximum", 65536}};
        auto preset_required = required;
        for (const auto* name : {"element", "preset", "width", "height"})
            preset_required.push_back(name);
        result.push_back(automation_method(
            "ui.source.layout_preset",
            "Apply one of16 native CSS layout presets as one source Undo record. Requires current "
            "source guards and assets.write/replay. Static RML widget only; bound/event-driven or "
            "inline transformed/animated ancestry refuses. Replaces inline position, edges, size "
            "and margins, preserving other source. Absolute positioning uses the nearest "
            "positioned "
            "ancestor's padding box; fixed axes use width/height pixels, stretched axes use auto "
            "size. Offsets default0: positive edge inset, centered right/down displacement, or "
            "both "
            "stretch insets. Custom styles/constraints still affect native layout; inspect "
            "Preview. "
            "No preview observation required, native drag eligibility extension, Save or Scene "
            "write.",
            "assets.write",
            automation_object(std::move(preset_properties), std::move(preset_required)), true,
            [&files, &editor, ready](const Json& request) {
                ready();
                observed(editor, files.document, request);
                editor.layout_preset(
                    request.at("element").get<std::size_t>(),
                    request.at("preset").get<std::string>(), request.at("width").get<float>(),
                    request.at("height").get<float>(), request.value("horizontal_offset", 0.f),
                    request.value("vertical_offset", 0.f));
                return state(editor, files.document);
            }));
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
            automation_array(automation_integer(0, 8191), ui_source_selection_limit, 1);
        group_properties["action"] = {{"type", "string"}, {"enum", std::move(group_actions)}};
        group_properties["preview_instance"] = automation_uuid();
        group_properties["preview_revision"] = automation_integer();
        group_properties["grid"] = {
            {"type", "number"}, {"minimum", 1}, {"maximum", 1024}, {"default", 8}};
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
            "actions preserve source sizes. snap_grid accepts1..32members and grid spacing1..1024 "
            "pixels (default8), aligning native border corners to the viewport grid; sizes stay "
            "unchanged. Other actions require2members; only snap_grid accepts nondefault grid. "
            "Unrelated text remains unchanged. dx/dy "
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
                               request.at("preview_revision").get<std::uint64_t>(),
                               request.value("grid", 8.f));
                return state(editor, files.document);
            }));
        auto transform_properties = guard;
        auto transform_actions = Json::array();
        for (const auto& [key, label] : ui_layout_transform_actions)
            transform_actions.push_back(key);
        transform_properties["elements"] =
            automation_array(automation_integer(0, 8191), ui_source_selection_limit, 1);
        transform_properties["action"] = {{"type", "string"},
                                          {"enum", std::move(transform_actions)}};
        transform_properties["preview_instance"] = automation_uuid();
        transform_properties["preview_revision"] = automation_integer();
        for (const auto* name : {"dx", "dy"})
            transform_properties[name] = {
                {"type", "number"}, {"minimum", -131072}, {"maximum", 131072}};
        transform_properties["grid"] = {{"type", "number"}, {"minimum", 1}, {"maximum", 1024}};
        auto transform_required = required;
        for (const auto* name :
             {"elements", "action", "dx", "dy", "preview_instance", "preview_revision"})
            transform_required.push_back(name);
        result.push_back(automation_method(
            "ui.source.transform",
            "Apply the same native-pixel projections as canvas dragging to1..32 admitted siblings. "
            "move preserves spacing; move_axis constrains the dominant delta (horizontal on ties). "
            "resize and resize_aspect require exactly one member. Resize aspect uses the native "
            "border ratio, accounting for source box-sizing/padding. Deltas clamp to source bounds "
            "like dragging. Optional grid1..1024 snaps the last member's border anchor when "
            "moving, "
            "or sizes when resizing; aspect takes priority, snapping the dominant size. Zero delta "
            "is a no-op; use arrange snap_grid for one-shot position alignment. Invalid snapped "
            "source bounds reject. Requires exact source/preview guards and assets.write; the "
            "complete native result must match before one Undo/selection. No Save, registration, "
            "Scene history, runtime input, relative/flow layout or group resize.",
            "assets.write",
            automation_object(std::move(transform_properties), std::move(transform_required)), true,
            [&files, &editor, ready](const Json& request) {
                ready();
                observed(editor, files.document, request);
                editor.transform(request.at("elements").get<std::vector<std::size_t>>(),
                                 request.at("action").get<std::string>(),
                                 request.at("dx").get<float>(), request.at("dy").get<float>(),
                                 request.contains("grid")
                                     ? std::optional(request.at("grid").get<float>())
                                     : std::nullopt,
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
