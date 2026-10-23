#pragma once
#include "source_document_automation.hpp"
#include <forge/material_graph.hpp>

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
    std::vector<LiveAuthoringMethod> methods;
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
            return Json{{"nodes", std::move(nodes)}};
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
        "allocates its own identity. Tile Maps require tile_set.",
        "asset.read",
        automation_object({{"editor", editor_schema}, {"tile_set", automation_uuid()}}, {"editor"}),
        false, [invoke](const Json& request) { return invoke("template", request); }));
    methods.push_back(automation_method(
        "asset.document.status",
        "Read this editor's open source session, revision, publication status and diagnostics.",
        "asset.read", automation_object({{"editor", editor_schema}}, {"editor"}), false,
        [invoke](const Json& request) { return invoke("status", request); }));
    methods.push_back(automation_method(
        "asset.document.read",
        "Read the shared source draft at a JSON Pointer. Arrays are paged. Tile Maps return "
        "metadata; query cells separately.",
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
    if (!permissions.assets)
        return methods;
    for (const auto* action : {"open", "create"})
        methods.push_back(automation_method(
            std::string("asset.document.") + action,
            std::string(action) + " a project source in its actual editor. Settle the prior draft "
                                  "first. Create supports a validated initial patch; Tile Maps "
                                  "require tile_set. It does not publish until Save.",
            "asset.write",
            automation_object({{"editor", editor_schema},
                               {"path", automation_text(2047, 1)},
                               {"tile_set", automation_uuid()},
                               {"patch", patch}},
                              {"editor", "path"}),
            true, [invoke, action](const Json& request) { return invoke(action, request); }));
    auto fields = Json{{"editor", editor_schema},
                       {"document_session", automation_integer(1)},
                       {"document_revision", automation_integer(1)}};
    const Json required = {"editor", "document_session", "document_revision"};
    for (const auto* action : {"patch", "paint", "undo", "redo", "save", "close"}) {
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
        } else if (std::string_view(action) == "close")
            properties["discard"] = {{"type", "boolean"}};
        methods.push_back(automation_method(
            std::string("asset.document.") + action,
            std::string(action) +
                " the shared asset source at its exact session/revision. Patch uses validated "
                "RFC6902 add/remove/replace/test in one Undo step; Tile Map patch edits metadata "
                "and paint edits cells. Save checks the disk baseline then admits ordinary "
                "publication; poll status for completion. Close requires discard for "
                "unsaved/unpublished drafts.",
            "asset.write", automation_object(std::move(properties), std::move(needed)), true,
            [invoke, action](const Json& request) { return invoke(action, request); }));
    }
    return methods;
}
} // namespace forge
