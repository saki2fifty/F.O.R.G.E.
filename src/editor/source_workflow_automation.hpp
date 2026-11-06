#pragma once
#include "../ui_surface_limits.hpp"
#include "source_document_automation.hpp"
#include <forge/application_state_source.hpp>
#include <forge/material_graph.hpp>
#include <forge/navigation_behavior.hpp>
#include <forge/render_texture.hpp>
#include <forge/ui_localization.hpp>

namespace forge {
inline std::vector<LiveAuthoringMethod>
source_document_automation_methods(ui::DocumentWorkspace& workspace,
                                   const ui::AutomationPermissions& permissions) {
    Json editors = Json::array();
    for (const auto& entry : workspace.entries())
        if (entry.source)
            editors.push_back(entry.id);
    const Json editor_schema{{"type", "string"}, {"enum", editors}};
    auto invoke = [&workspace](std::string_view action, const Json& request) {
        const auto* entry = workspace.find(request.at("editor").get<std::string>());
        if (!entry || !entry->source)
            throw AuthoringError("unknown_editor",
                                 "Choose an editor advertised by asset.document.types");
        return entry->source->invoke(action, request);
    };
    const auto patch = automation_array(
        automation_object(
            {{"op", {{"type", "string"}, {"enum", {"add", "remove", "replace", "test"}}}},
             {"path", automation_text(2048)},
             {"value", Json::object()}},
            {"op", "path"}),
        128, 1);
    const auto cell = automation_object(
        {{"layer", automation_integer(1)},
         {"x", {{"type", "integer"}, {"minimum", INT32_MIN}, {"maximum", INT32_MAX}}},
         {"y", {{"type", "integer"}, {"minimum", INT32_MIN}, {"maximum", INT32_MAX}}},
         {"tile", automation_integer()},
         {"flags", automation_integer(0, UINT32_MAX)}},
        {"layer", "x", "y"});
    const Json channel{{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
    const auto rgba = automation_array(automation_integer(0, 255), 4, 4);
    const auto canvas =
        automation_object({{"width", automation_integer(1, paint_texture_dimension_limit)},
                           {"height", automation_integer(1, paint_texture_dimension_limit)},
                           {"semantic", {{"type", "string"}, {"enum", {"color", "data"}}}},
                           {"background", rgba}});
    const Json coordinate{{"type", "number"}, {"minimum", -1000000}, {"maximum", 1000000}};
    const auto paint_point = automation_array(coordinate, 2, 2);
    std::vector<LiveAuthoringMethod> methods;
    methods.push_back(automation_method(
        "asset.paint_texture.catalog",
        "Describe editable pixel textures, brush math, source and publication limits.",
        "asset.read", automation_object(), false, [](const Json&) {
            return Json{
                {"editor", "paint_texture"},
                {"format", "forge.paint-texture"},
                {"version", 1},
                {"suffix", ".paint-texture.json"},
                {"asset_type", "texture"},
                {"limits",
                 {{"dimension", paint_texture_dimension_limit},
                  {"stroke_points", paint_texture_stroke_point_limit},
                  {"source_bytes", paint_texture_source_byte_limit},
                  {"history_entries", 64},
                  {"history_bytes", 8388608}}},
                {"coordinates", "Pixel XY from the top left; centers at x+0.5,y+0.5. Continuous "
                                "capsules are clipped to the canvas. One stroke uses maximum path "
                                "coverage; separate strokes layer. Optional increasing breaks "
                                "are zero-based point indices that start disconnected paths."},
                {"mesh_stroke",
                 "asset.document.mesh_stroke maps explicit published Mesh triangle "
                 "barycentrics through a chosen Material image slot to this canvas. "
                 "Pass exact mesh_revision/material_revision from asset.read "
                 "(engine revision or forge.import key), optional lod/slot and "
                 "use asset.mesh.triangles to page published indices, positions "
                 "and UV0 at an exact Mesh revision. Supply "
                 "1..256 samples with part, primitive, barycentric[3] summing to one. "
                 "Native Scene input uses current visible geometry; explicit "
                 "samples do not test Scene occlusion. Both share UV0/Material "
                 "mapping, disconnected paths, asynchronous brush, history and "
                 "publication. Requires opaque two-sided depth-tested built-in "
                 "Material, matching open Texture and Color/Data 2D usage, unit "
                 "UV0 and identity/full-axis flips. Other profiles reject."},
                {"color", "Brush RGB is linear, alpha is straight opacity. Color pixels store sRGB "
                          "bytes with straight alpha and use linear source-over. Erase reduces "
                          "alpha and preserves hidden RGB."},
                {"data", "Brush channels are independent raw normalized values. Paint blends "
                         "toward them; erase blends toward the canvas background."},
                {"workflow",
                 "Create with optional canvas dimensions/semantic/background; size and "
                 "interpretation stay fixed. Read returns metadata; pixels queries return byte "
                 "RGBA. Patch preserves pixel storage and edits admitted metadata. Fill replaces "
                 "pixels with exact RGBA bytes. Stroke prepares asynchronously; poll status until "
                 "transient_draft is false, then refresh revision and inspect diagnostics. Save "
                 "publishes through the normal texture worker. One admitted stroke/fill is one "
                 "Undo step; oversized history rejects without losing the draft. Save/Publish and "
                 "save the consuming Scene before export."}};
        }));
    methods.push_back(automation_method(
        "asset.render_texture.catalog",
        "Describe saved camera output images, typed material bindings, pass ordering and limits. "
        "Create/read/patch/publish through the render_texture source editor; assign the published "
        "output using the reflected Camera property and a Material version3 image binding. "
        "The surface_input profile describes optional interactive scene mesh surfaces.",
        "asset.read", automation_object(), false, [](const Json&) {
            MaterialData shape;
            shape.model = "forge.gltf.unlit.v1";
            shape.textures["baseColorTexture"].semantic = TextureSemantic::Color;
            const auto slot = material_values_document(shape).at("textures").at("baseColorTexture");
            return Json{
                {"format", "forge.render-texture"},
                {"version", 1},
                {"editor", "render_texture"},
                {"suffix", ".rendertexture.json"},
                {"asset_type", RenderTextureAsset::type},
                {"component", "forge.camera"},
                {"property", "render_texture"},
                {"profile", "linear-hdr-opaque"},
                {"surface_input",
                 {{"profile", ui_surface_detail::profile_version},
                  {"component", "forge.ui_surface_input"},
                  {"property", "render_texture"},
                  {"mesh",
                   "Geometry 0: Engine / Quad. Geometry 1: retained triangle mesh, "
                   "current LOD/pose, float2 UV0 inside [0,1]; complete matching Materials"},
                  {"geometry_property", "geometry"},
                  {"geometry_profiles", {{"quad", 0}, {"mesh", 1}}},
                  {"projection", "Unique UV-to-world point; overlapping islands at different "
                                 "world points and UV holes reject input. Capture cancels on mesh "
                                 "revision or LOD changes."},
                  {"maximum_surfaces", ui_surface_detail::max_surfaces},
                  {"material", "Ordinary opaque, two-sided, depth-tested/depth-writing "
                               "unlit Material; one matching Color2D UV0 baseColorTexture"},
                  {"uvs", "Identity or complete per-axis flips (scale -1, offset 1)"},
                  {"input", "Native presenter focus/capture/text; main UI precedence. "
                            "Semantic commands use existing Play tools; no raw ray tool."}}},
                {"limits",
                 {{"outputs", 4},
                  {"cameras", 16},
                  {"pixels", 4194304},
                  {"minimum_dimension", 1},
                  {"maximum_dimension", render_texture_max_dimension},
                  {"source_bytes", render_texture_source_byte_limit}}},
                {"material",
                 {{"source_version", 3},
                  {"binding_field", "render_texture"},
                  {"slot_dimension", "2D"},
                  {"slot_semantic", "Color"},
                  {"binding_template", {{"render_texture", nullptr}, {"slot", slot}}},
                  {"binding_template_note", "Replace null with the published RenderTexture "
                                            "UUID before patching a Material source."},
                  {"consumer", "MeshRenderer"}}},
                {"composition",
                 "Cameras sharing an output compose by order and EntityId using "
                 "their viewport, clear and layer controls. An empty Camera output "
                 "selects the main Game view. Camera Shot directs main cameras only."},
                {"ordering",
                 "Outputs render before their material consumers. Reads follow selected "
                 "materials on visible objects in the camera layer mask; use separate "
                 "producer and screen layers. Missing writers and feedback cycles reject "
                 "the affected outputs. Previous pixels are never treated as current."},
                {"color", "Linear RGBA16 float with alpha fixed to one. Display exposure, tone "
                          "mapping and grading belong to the consuming view. No mip chain, cube, "
                          "array, transparent-output or CPU image readback asset is authored."},
                {"persistence",
                 "Saved UUID and configuration participate in normal dependency "
                 "closure. Pixel images are scoped to the presentation view/device; "
                 "they are recreated in Play, preview and exported runtime. Save / "
                 "Publish the output and Material before saving/exporting the Scene. "
                 "After resizing an output, republish dependent Materials before export; "
                 "stale recorded dependency revisions are rejected."}};
        }));
    methods.push_back(automation_method(
        "asset.application_states.catalog",
        "Describe logical game-state domains, hierarchy, computed rules and subordinate "
        "conditions. "
        "Use the application_states source editor to create, read, patch and publish; assign its "
        "published definition through the reflected Application States component.",
        "asset.read", automation_object(), false, [](const Json&) {
            return Json{
                {"format", "forge.application-states"},
                {"version", 1},
                {"editor", "application_states"},
                {"suffix", ".appstates.json"},
                {"component", "forge.application_states"},
                {"property", "definition"},
                {"limits",
                 {{"domains", 16},
                  {"states_per_domain", 64},
                  {"states", 512},
                  {"rules_per_domain", 64},
                  {"rules", 256},
                  {"conditions_per_rule", 8},
                  {"hierarchy_depth", 16},
                  {"key_bytes", 48},
                  {"label_bytes", 96},
                  {"source_bytes", application_states_byte_limit}}},
                {"keys",
                 "Lowercase ASCII letter followed by lowercase letters, digits or underscores. "
                 "Unique semantic keys per scope; preserve the asset UUID. Rename keys and all "
                 "recognized references atomically; labels are literal UTF-8."},
                {"domain",
                 {{"key", "app"},
                  {"label", "Game mode"},
                  {"initial", "menu"},
                  {"states", Json::array({{{"key", "menu"}}, {{"key", "playing"}}})}}},
                {"hierarchy",
                 "Optional state parent key in the same domain. Conditions include descendants. "
                 "Any named node can be requested; there is no automatic child selection."},
                {"computed",
                 "Optional computed=true with ordered rules [{state,all:[{domain,state}]}]. "
                 "First match wins; otherwise initial. Computed domains reject direct requests."},
                {"subordinate",
                 "Optional enabled_when={domain,state}. Inactive while false; reactivation "
                 "starts at initial. Conditions form an acyclic dependency graph."},
                {"runtime",
                 "One configured non-Prefab controller per runtime World. State requests apply "
                 "at owner boundaries; last merged request wins. State-conditioned C++ systems "
                 "use native query terms; renderers, physics bodies and the engine clock are not "
                 "automatically disabled by a logical state."},
                {"persistence",
                 "Active state is transient. Stop starts fresh; Keep values does not change "
                 "authored initial states. Ordinary Play private recovery retains admitted "
                 "semantic states and pending requests without replaying entry actions. "
                 "This is not an automatic durable save-game format."}};
        }));
    methods.push_back(automation_method(
        "asset.localization.catalog",
        "Describe game UI language/message catalogs, fallback, bounds and literal RML bindings. "
        "Create/read/patch/publish with the ui_localization source editor. Assign its catalog and "
        "locale through the reflected UI Localization Scene component.",
        "asset.read", automation_object(), false, [](const Json&) {
            return Json{
                {"format", "forge.ui-localization"},
                {"version", 1},
                {"editor", "ui_localization"},
                {"suffix", ".localization.json"},
                {"component", "forge.ui_localization"},
                {"limits",
                 {{"locales", 8},
                  {"keys", 128},
                  {"message_bytes", 1024},
                  {"label_bytes", 64},
                  {"key_bytes", 48},
                  {"source_bytes", ui_localization_byte_limit}}},
                {"locale_tags", "ASCII hyphenated tags, 2..35 bytes; primary 2..8 letters, "
                                "subtags 1..8 letters or digits. Compared without case."},
                {"keys", "ASCII letter or underscore followed by letters, digits or underscores; "
                         "the default locale owns all keys. Other locales may omit translations. "
                         "Rename keys in every language and update calling RML expressions."},
                {"fallback", "Exact locale, primary language, then default locale. Missing "
                             "translations use the default message; unknown keys show [[key]]."},
                {"binding", "{{ 'menu_play' | tr(forge_locale) }}"},
                {"command", {{"name", "forge.locale"}, {"value", "fr"}}},
                {"runtime", "One configured non-Prefab controller per Scene UI context. Its "
                            "locale is the sole authority, adjustable in Play or Pause. Stop "
                            "restores authored values. No automatic player-preference store."},
                {"text", "Literal UTF-8 only; messages cannot inject markup or expressions. "
                         "No plural rules, parameters, full shaping or editor translation. "
                         "Glyph coverage depends on the loaded fonts."}};
        }));
    methods.push_back(automation_method(
        "asset.behavior.catalog",
        "Describe the bounded Navigation Behavior graph schema, actions, conditions, identity and "
        "transition policy. Create/read/patch/publish with the navigation_behavior source editor; "
        "configure its graph/target/anchor/enabled through the reflected Scene component.",
        "asset.read", automation_object(), false, [](const Json&) {
            Json actions = Json::array(), conditions = Json::array();
            Json tree_nodes = Json::array();
            for (auto kind :
                 {NavigationBehaviorTreeKind::Sequence,
                  NavigationBehaviorTreeKind::ReactiveSequence,
                  NavigationBehaviorTreeKind::Selector, NavigationBehaviorTreeKind::Wait,
                  NavigationBehaviorTreeKind::MoveToPoint, NavigationBehaviorTreeKind::FollowTarget,
                  NavigationBehaviorTreeKind::TargetNear, NavigationBehaviorTreeKind::TargetFar,
                  NavigationBehaviorTreeKind::TargetMissing}) {
                Json draft{{"version", 2}, {"entry", 0}, {"next_id", 1}, {"nodes", Json::array()}};
                (void)add_navigation_behavior_node(draft, kind);
                auto example = draft.at("nodes").back();
                example.erase("id");
                example.erase("name");
                tree_nodes.push_back({{"kind", navigation_behavior_tree_kind_name(kind)},
                                      {"example", std::move(example)}});
            }
            for (auto kind : {NavigationBehaviorAction::Wait, NavigationBehaviorAction::MoveToPoint,
                              NavigationBehaviorAction::FollowTarget}) {
                const auto name = navigation_behavior_action_name(kind);
                Json row{{"kind", name}, {"example", {{"kind", name}}}};
                if (kind == NavigationBehaviorAction::MoveToPoint) {
                    row["example"]["point"] = Json::array({0, 0, 0});
                    row["coordinates"] = "Three numbers in [-4096,4096]; world or Anchor-local. XY "
                                         "navigation ignores point Z.";
                }
                actions.push_back(std::move(row));
            }
            for (auto kind :
                 {NavigationBehaviorCondition::Elapsed, NavigationBehaviorCondition::Arrived,
                  NavigationBehaviorCondition::TargetNear, NavigationBehaviorCondition::TargetFar,
                  NavigationBehaviorCondition::TargetMissing}) {
                const auto name = navigation_behavior_condition_name(kind);
                Json row{{"kind", name}, {"example", {{"kind", name}}}};
                if (kind == NavigationBehaviorCondition::Elapsed) {
                    row["example"]["seconds"] = 1;
                    row["maximum"] = 3600;
                }
                if (kind == NavigationBehaviorCondition::TargetNear ||
                    kind == NavigationBehaviorCondition::TargetFar) {
                    row["example"]["distance"] = 2;
                    row["maximum"] = 8192;
                }
                conditions.push_back(std::move(row));
            }
            return Json{
                {"format", "forge.navigation-behavior"},
                {"version", 1},
                {"editor", "navigation_behavior"},
                {"component", "forge.navigation_behavior"},
                {"source_versions", {1, 2}},
                {"profiles", {"state_graph", "tree"}},
                {"tree",
                 {{"source_version", 2},
                  {"nodes", std::move(tree_nodes)},
                  {"limits", {{"nodes", 32}, {"children", 8}, {"depth", 8}}},
                  {"identity", "Nodes allocate next_id monotonically. A connection is "
                               "identified by its child, which has exactly one parent."},
                  {"drafts", "Disconnected nodes and empty composites are editable; "
                             "publish one connected tree with nonempty composites."},
                  {"evaluation", "Sequences remember their running child. Reactive "
                                 "sequences and Selectors reconsider earlier children. "
                                 "One selected action consumes time/current arrival or "
                                 "failure per positive fixed tick. Root completion "
                                 "restarts on the next tick; selection never advances time."}}},
                {"actions", std::move(actions)},
                {"conditions", std::move(conditions)},
                {"transition_example",
                 {{"id", 3}, {"to", 2}, {"condition", {{"kind", "elapsed"}, {"seconds", 1}}}}},
                {"limits",
                 {{"states", 32},
                  {"transitions", 128},
                  {"outgoing", 8},
                  {"source_bytes", navigation_behavior_byte_limit}}},
                {"identity", "Positive uint32 state and transition IDs share next_id; allocate "
                             "then advance next_id. Undo never recycles IDs."},
                {"evaluation",
                 "Outgoing array order is priority; first match, at most one transition per "
                 "positive fixed tick. Elapsed resets on entry. Missing target makes near/far "
                 "false; test target_missing explicitly."},
                {"prerequisites", "Navigation Agent and a published navigation asset. Target and "
                                  "Anchor are ordinary typed EntityRefs. Cursor is runtime-only; "
                                  "observe it through Play presentation."}};
        }));
    methods.push_back(automation_method(
        "asset.graph.catalog",
        "List the existing material graph node schemas and help from the editor's native catalog.",
        "asset.read", automation_object(), false, [](const Json&) {
            Json nodes = Json::array();
            for (const auto& schema : material_graph_node_schemas())
                nodes.push_back({{"schema", schema.key},
                                 {"label", schema.label},
                                 {"category", schema.category},
                                 {"help", schema.help},
                                 {"version", schema.version}});
            return Json{{"nodes", std::move(nodes)},
                        {"domains",
                         {{"mesh", "Surface Output; lit Mesh material"},
                          {"sprite", "Sprite Output; final color, logical UV0, primary image "
                                     "stable key, optional Inspector Tint"}}}};
        }));
    methods.push_back(automation_method(
        "asset.graph.node",
        "Create a node value with the native factory or inspect an existing node's typed ports. "
        "This does not edit a document; insert the returned value with asset.document.patch.",
        "asset.read",
        automation_object({{"schema", automation_text(128, 1)}, {"node", {{"type", "object"}}}}),
        false, [](const Json& request) {
            if (request.contains("schema") == request.contains("node"))
                throw AuthoringError("invalid_argument", "Provide exactly one node or schema");
            const auto node =
                request.contains("node")
                    ? request.at("node")
                    : create_material_graph_node(request.at("schema").get<std::string>(), {0, 0});
            static const char* types[]{"scalar",  "vector2", "vector3",
                                       "vector4", "color3",  "color4"};
            const auto ports = [&](const auto& values) {
                Json result = Json::array();
                for (const auto& port : values)
                    result.push_back({{"key", port.key},
                                      {"label", port.label},
                                      {"type", types[static_cast<unsigned>(port.type)]}});
                return result;
            };
            return Json{{"node", node},
                        {"inputs", ports(material_graph_inputs(node))},
                        {"outputs", ports(material_graph_outputs(node))}};
        }));
    methods.push_back(automation_method(
        "asset.document.types",
        "List the actual source editors, their file suffixes and current open draft status. "
        "Sources remain owned by these editors.",
        "asset.read", automation_object(), false, [&workspace](const Json&) {
            Json result = Json::array();
            for (const auto& entry : workspace.entries())
                if (entry.source)
                    result.push_back({{"editor", entry.id},
                                      {"title", entry.title},
                                      {"suffix", entry.source->suffix},
                                      {"status", entry.source->invoke("status", Json::object())}});
            return Json{{"editors", std::move(result)}};
        }));
    methods.push_back(automation_method(
        "asset.document.template",
        "Inspect a validated initial source shape. Its preview UUID is not reserved; creation "
        "allocates its own identity. Tile Maps require tile_set; Sprite atlases require textures. "
        "Material graphs accept domain mesh (default) or sprite. Navigation behaviors accept "
        "behavior_profile state_graph (default) or tree.",
        "asset.read",
        automation_object(
            {{"editor", editor_schema},
             {"tile_set", automation_uuid()},
             {"domain", {{"type", "string"}, {"enum", {"mesh", "sprite"}}}},
             {"behavior_profile", {{"type", "string"}, {"enum", {"state_graph", "tree"}}}},
             {"textures", automation_array(automation_uuid(), 512, 1)},
             {"canvas", canvas}},
            {"editor"}),
        false, [invoke](const Json& request) { return invoke("template", request); }));
    methods.push_back(automation_method(
        "asset.document.status",
        "Read this editor's open source session, revision, publication status and diagnostics.",
        "asset.read", automation_object({{"editor", editor_schema}}, {"editor"}), false,
        [invoke](const Json& request) { return invoke("status", request); }));
    methods.push_back(automation_method(
        "asset.document.read",
        "Read the shared source draft at a JSON Pointer. Arrays are paged. Tile Maps return "
        "metadata; query cells separately. Paint textures return metadata; query pixels "
        "separately.",
        "asset.read",
        automation_object({{"editor", editor_schema},
                           {"document_session", automation_integer(1)},
                           {"pointer", automation_text(2048)},
                           {"offset", automation_integer()},
                           {"limit", automation_integer(1, 128)}},
                          {"editor", "document_session"}),
        false, [invoke](const Json& request) { return invoke("read", request); }));
    methods.push_back(automation_method(
        "asset.document.cells",
        "Query up to 4096 exact Tile Map cells without copying its source or changing history.",
        "asset.read",
        automation_object({{"editor", editor_schema},
                           {"document_session", automation_integer(1)},
                           {"cells", automation_array(cell, 4096, 1)}},
                          {"editor", "document_session", "cells"}),
        false, [invoke](const Json& request) { return invoke("cells", request); }));
    methods.push_back(automation_method(
        "asset.document.pixels",
        "Query exact byte RGBA pixels of the current paint source without changing history.",
        "asset.read",
        automation_object(
            {{"editor", {{"type", "string"}, {"const", "paint_texture"}}},
             {"document_session", automation_integer(1)},
             {"points",
              automation_array(
                  automation_array(automation_integer(0, paint_texture_dimension_limit - 1), 2, 2),
                  4096, 1)}},
            {"editor", "document_session", "points"}),
        false, [invoke](const Json& request) { return invoke("pixels", request); }));
    if (!permissions.assets)
        return methods;
    for (const auto* action : {"open", "create"}) {
        Json properties{{"editor", editor_schema}, {"path", automation_text(2047, 1)}};
        const bool create = std::string_view(action) == "create";
        if (create) {
            properties["tile_set"] = automation_uuid();
            properties["textures"] = automation_array(automation_uuid(), 512, 1);
            properties["domain"] = {{"type", "string"}, {"enum", {"mesh", "sprite"}}};
            properties["behavior_profile"] = {{"type", "string"},
                                              {"enum", {"state_graph", "tree"}}};
            properties["patch"] = patch;
            properties["canvas"] = canvas;
        }
        methods.push_back(automation_method(
            std::string("asset.document.") + action,
            std::string(action) +
                " a project source in its actual editor. Settle the prior draft "
                "first. Create supports a validated initial patch; Tile Maps "
                "require tile_set; Sprite atlases require textures. It does not "
                "publish until Save. Material graph creation accepts domain mesh "
                "(default) or sprite. Navigation behaviors accept behavior_profile "
                "state_graph (default) or tree.",
            "asset.write", automation_object(std::move(properties), {"editor", "path"}), true,
            [invoke, action](const Json& request) { return invoke(action, request); }));
    }
    auto fields = Json{{"editor", editor_schema},
                       {"document_session", automation_integer(1)},
                       {"document_revision", automation_integer(1)}};
    const Json required = {"editor", "document_session", "document_revision"};
    for (const auto* action :
         {"patch", "paint", "tile_collision", "tile_animation", "tile_autotile", "stroke",
          "mesh_stroke", "fill", "copy_pixels", "cancel_pixels", "undo", "redo", "save", "close"}) {
        auto properties = fields;
        auto needed = required;
        if (std::string_view(action) == "patch") {
            properties["patch"] = patch;
            properties["label"] = automation_text(256, 1);
            needed.push_back("patch");
        } else if (std::string_view(action) == "paint") {
            auto write_cell = cell;
            write_cell["required"].push_back("tile");
            properties["cells"] = automation_array(write_cell, 4096, 1);
            properties["label"] = automation_text(256, 1);
            needed.push_back("cells");
        } else if (std::string_view(action) == "stroke" ||
                   std::string_view(action) == "mesh_stroke") {
            properties["editor"] = {{"type", "string"}, {"const", "paint_texture"}};
            if (std::string_view(action) == "stroke") {
                properties["points"] =
                    automation_array(paint_point, paint_texture_stroke_point_limit, 1);
                properties["breaks"] =
                    automation_array(automation_integer(1, paint_texture_stroke_point_limit - 1),
                                     paint_texture_stroke_point_limit - 1, 0);
                needed.push_back("points");
            } else {
                properties["mesh"] = automation_uuid();
                properties["material"] = automation_uuid();
                properties["mesh_revision"] = automation_text(64, 64);
                properties["material_revision"] = automation_text(64, 64);
                properties["slot"] = automation_text(63, 1);
                properties["lod"] = automation_integer(0, UINT32_MAX);
                properties["samples"] = automation_array(
                    automation_object({{"part", automation_integer(0, UINT32_MAX)},
                                       {"primitive", automation_integer(0, UINT32_MAX)},
                                       {"barycentric", automation_array(channel, 3, 3)},
                                       {"break_before", {{"type", "boolean"}}}},
                                      {"primitive", "barycentric"}),
                    paint_texture_stroke_point_limit, 1);
                for (const auto* field :
                     {"mesh", "material", "mesh_revision", "material_revision", "samples"})
                    needed.push_back(field);
            }
            properties["brush"] = automation_object(
                {{"color", automation_array(channel, 4, 4)},
                 {"radius", {{"type", "number"}, {"minimum", .5}, {"maximum", 256}}},
                 {"hardness", channel},
                 {"opacity", channel},
                 {"erase", {{"type", "boolean"}}}},
                {"color"});
            needed.push_back("brush");
        } else if (std::string_view(action) == "copy_pixels") {
            properties["editor"] = {{"type", "string"}, {"const", "paint_texture"}};
            properties["texture"] = automation_uuid();
            needed.push_back("texture");
        } else if (std::string_view(action) == "cancel_pixels") {
            properties["editor"] = {{"type", "string"}, {"const", "paint_texture"}};
        } else if (std::string_view(action) == "fill") {
            properties["editor"] = {{"type", "string"}, {"const", "paint_texture"}};
            properties["rgba"] = rgba;
            needed.push_back("rgba");
        } else if (std::string_view(action) == "tile_collision") {
            const Json coordinate{{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
            const auto pair = automation_array(coordinate, 2, 2);
            const auto simple = automation_object(
                {{"kind", {{"type", "string"}, {"enum", {"full_cell", "none"}}}}}, {"kind"});
            const auto polygon = automation_object(
                {{"kind", {{"type", "string"}, {"enum", {"convex", "polygon"}}}},
                 {"points", automation_array(pair, tile_collision_vertex_limit, 3)}},
                {"kind", "points"});
            const auto loop = automation_array(pair, tile_collision_vertex_limit, 3);
            const auto contour = automation_object(
                {{"points", loop}, {"holes", automation_array(loop, tile_collision_hole_limit, 0)}},
                {"points"});
            const auto contours = automation_object(
                {{"kind", {{"const", "contours"}}},
                 {"contours", automation_array(contour, tile_collision_contour_limit, 1)}},
                {"kind", "contours"});
            properties["tile"] = automation_integer(1, UINT64_MAX);
            properties["editor"] = {{"type", "string"}, {"const", "tile_set"}};
            properties["collision"] = {{"oneOf", {simple, polygon, contours}}};
            needed.push_back("tile");
            needed.push_back("collision");
        } else if (std::string_view(action) == "tile_animation") {
            const auto frame = automation_object(
                {{"frame", automation_integer(0, UINT32_MAX)},
                 {"seconds", {{"type", "number"}, {"minimum", .001}, {"maximum", 600}}}},
                {"frame", "seconds"});
            const auto enabled = automation_object(
                {{"enabled", {{"const", true}}},
                 {"frames", automation_array(frame, tile_animation_frame_limit, 1)}},
                {"enabled", "frames"});
            const auto disabled = automation_object(
                {{"enabled", {{"const", false}}},
                 {"frames", automation_array(frame, tile_animation_frame_limit, 0)}},
                {"enabled", "frames"});
            properties["tile"] = automation_integer(1, UINT64_MAX);
            properties["editor"] = {{"type", "string"}, {"const", "tile_set"}};
            properties["animation"] = {{"oneOf", {enabled, disabled}}};
            needed.push_back("tile");
            needed.push_back("animation");
        } else if (std::string_view(action) == "tile_autotile") {
            const auto rule = automation_object({{"mask", automation_integer(0, 255)},
                                                 {"same", automation_integer(0, 255)},
                                                 {"tile", automation_integer(1, UINT64_MAX)}},
                                                {"mask", "same", "tile"});
            const auto enabled =
                automation_object({{"enabled", {{"const", true}}},
                                   {"rules", automation_array(rule, tile_neighbor_rule_limit, 1)}},
                                  {"enabled", "rules"});
            const auto disabled =
                automation_object({{"enabled", {{"const", false}}},
                                   {"rules", automation_array(rule, tile_neighbor_rule_limit, 0)}},
                                  {"enabled", "rules"});
            properties["tile"] = automation_integer(1, UINT64_MAX);
            properties["editor"] = {{"type", "string"}, {"const", "tile_set"}};
            properties["autotile"] = {{"oneOf", {enabled, disabled}}};
            needed.push_back("tile");
            needed.push_back("autotile");
        } else if (std::string_view(action) == "close")
            properties["discard"] = {{"type", "boolean"}};
        methods.push_back(automation_method(
            std::string("asset.document.") + action,
            std::string(action) +
                " the shared asset source at its exact session/revision. Patch uses validated "
                "RFC6902 add/remove/replace/test in one Undo step; Tile Map patch edits metadata "
                "and paint edits cells. Save checks the disk baseline then admits ordinary "
                "publication; poll status for completion. Close requires discard for "
                "unsaved/unpublished drafts. Paint textures accept optional canvas on "
                "create/template; "
                "stroke prepares asynchronously and returns transient_draft=true. cancel_pixels "
                "discards pending work without changing history. Poll status until settled, "
                "refresh document_revision and inspect diagnostics. Fill uses exact byte RGBA; "
                "pixels reads exact stored bytes." +
                (std::string_view(action) == "mesh_stroke"
                     ? " Map explicit published Mesh triangle samples through the supplied "
                       "Material's slot (default baseColorTexture), selected lod (default0) and "
                       "sample part (default0). Exact mesh_revision/material_revision required; "
                       "the slot must use this paint canvas. Native Scene visibility is not "
                       "implied by explicit samples. Barycentrics sum to one. Nonadjacent/UV-seam "
                       "samples and break_before start separate paths in one Undo step. Poll "
                       "status for preparation errors without changing source on failure."
                     : "") +
                (std::string_view(action) == "tile_collision"
                     ? " Tile Sets only: apply full_cell, none, convex, polygon or contours. Loops "
                       "need "
                       "3..8 counterclockwise unit-cell XY/up points; convex requires strict "
                       "convexity, polygon admits a simple concave contour decomposed into convex "
                       "pieces. The contours kind supports1..4 filled contours with0..4 strictly "
                       "interior holes per contour; all loops use3..8 counterclockwise points. "
                       "Separate islands inside holes are allowed. Touches, crossings, overlapping "
                       "filled areas, nested holes and repeated/adjacent-collinear vertices "
                       "reject. "
                       "Explicit edits promote to Source2, Source3 for polygon, or Source6 for "
                       "contours; conflicting legacy extensions block promotion. Ordinary edits "
                       "cannot downgrade source versions. Custom polygons follow cell flips and "
                       "support Static, Kinematic and Dynamic bodies/sensors, without one-way "
                       "collision. "
                       "Dynamic Map bodies require World spatial binding."
                     : "") +
                (std::string_view(action) == "tile_animation"
                     ? " Tile Sets only: apply an enabled boolean and ordered frames with uint32 "
                       "frame indices and durations in seconds (0.001..600). Enabled sequences "
                       "need1..64frames; disabled sequences allow0..64. Frames must fit the tile's "
                       "image grid. Explicit safe promotion to Source4 preserves identities and "
                       "history; legacy collision/animation extensions can block promotion. "
                       "Sequences loop on completed simulation ticks, sharing a world timeline. "
                       "Pause/Step/speed apply; collision stays fixed. Publish the Set and its "
                       "dependent Maps before Play/export."
                     : "") +
                (std::string_view(action) == "tile_autotile"
                     ? " Tile Sets only: apply ordered eight-neighbor rules. Enabled profiles "
                       "need1..64rules; "
                       "disabled profiles allow0..64. Mask and same are byte bitsets; same must be "
                       "a subset "
                       "of mask. Bits clockwise in XY/up: N,NE,E,SE,S,SW,W,NW. Neighbors match the "
                       "same logical "
                       "tile ID in the same layer, transformed by cell orientation. First match "
                       "selects an "
                       "existing local tile ID; fallback uses the root. Targets can be self or "
                       "tiles without "
                       "enabled neighbor rules. Safe Source5 promotion preserves IDs/history. "
                       "Rendering and "
                       "collision use selected definitions; raw cells/SDK/checkpoints keep logical "
                       "IDs. "
                       "Publish the Set and dependent Maps before Play/export."
                     : "") +
                (std::string_view(action) == "patch"
                     ? " Timeline source6 supports editable scalar component fields with "
                       "component/field keys and matching inspected admission stamps; each key "
                       "provides component_values. Source7 adds explicit value_type scalar/bool/"
                       "int64/uint64: Boolean and exact integer values hold until the next key, "
                       "without curves. Use the current admitted component schema for native "
                       "widths, ranges and enum choices. Source8 adds optional member_path arrays "
                       "of "
                       "struct member IDs below field on both descriptors and key values; "
                       "read-only ancestors cannot be animated. Source9 adds element_path suffixes "
                       "starting with {index:N} for positional unkeyed collections or "
                       "{key:string-or-string-tuple} for declared collection keys, followed by "
                       "member IDs/selectors. Repeat the complete identity on each key value; "
                       "missing elements reject without resizing or adding rows. UI Values opts "
                       "into "
                       "the same keyed field tracks with forge.builtin-component admission; user "
                       "C++ components retain forge.authored-component admission. Copy the exact "
                       "current schema stamp. UI expressions consume the sampled numbers in Game; "
                       "native CSS animation keeps presentation time."
                     : ""),
            "asset.write", automation_object(std::move(properties), std::move(needed)), true,
            [invoke, action](const Json& request) { return invoke(action, request); }));
    }
    return methods;
}
} // namespace forge
