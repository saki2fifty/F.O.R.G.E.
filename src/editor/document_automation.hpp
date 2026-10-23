#pragma once
#include "document.hpp"
#include <forge/live_authoring.hpp>

namespace forge {
inline std::filesystem::path automation_scene_path(const Json& request) {
    const auto text = request.at("path").get<std::string>();
    const auto path = std::filesystem::u8path(text);
    if (text.empty() || text.size() > 1024 || path.is_absolute() || path.has_root_name() ||
        text.find_first_of("\\:") != std::string::npos ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; }) ||
        std::find(path.begin(), path.end(), "..") != path.end())
        throw std::runtime_error("Use a project-relative scene path with forward slashes");
    return path;
}
// Bindings borrow the existing document/file owners. They never retain authored
// intent, disk baselines, leases, history or a second project representation.
inline Json automation_document_status(SceneDocument& document) {
    document.check_ownership();
    const auto relative = document.path().lexically_relative(document.project()).generic_u8string();
    return {{"project_name", document.name()},
            {"scene_path", document.path().empty()
                               ? Json(nullptr)
                               : Json(std::string(relative.begin(), relative.end()))},
            {"generation", document.generation()},
            {"persisted", document.on_disk()},
            {"dirty", document.dirty()},
            {"recovery_available", document.has_recovery()},
            {"project_settings", document.settings().document()}};
}
inline std::vector<LiveAuthoringMethod> document_automation_methods(
    SceneDocument& document, bool writes,
    std::function<void(const std::filesystem::path&)> save = {},
    std::function<void(const std::filesystem::path&, bool)> switch_scene = {}) {
    const auto schema = [](Json properties = Json::object(), Json required = Json::array()) {
        return Json{{"type", "object"},
                    {"properties", std::move(properties)},
                    {"required", std::move(required)},
                    {"additionalProperties", false}};
    };
    std::vector<LiveAuthoringMethod> methods;
    methods.push_back({"project.read", "Read the current project's settings and scene file status.",
                       "document.read", schema(), false,
                       [&document](const Json&) { return automation_document_status(document); }});
    if (!writes)
        return methods;
    if (!save || !switch_scene)
        throw std::logic_error("Document writes require the editor's file-operation owners");
    const auto path = Json{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}};
    const auto discard = Json{{"type", "boolean"}, {"default", false}};
    auto add = [&](const char* name, const char* description, Json input,
                   std::function<void(const Json&)> run) {
        methods.push_back({name, description, "document.write", std::move(input), true,
                           [&document, run = std::move(run)](const Json& request) {
                               const auto generation = document.generation();
                               run(request);
                               auto result = automation_document_status(document);
                               result["rediscover"] = generation != document.generation();
                               return result;
                           }});
    };
    add("document.save", "Save this scene through the editor; reject external disk changes.",
        schema(), [save](const Json&) { save({}); });
    add("document.save_as",
        "Save to a new project-relative JSON filename. Copying a saved scene creates new asset "
        "and entity identities. Rediscover after a filename change.",
        schema({{"path", path}}, {"path"}),
        [save](const Json& request) { save(automation_scene_path(request)); });
    add("document.new",
        "Create an untitled scene in this project. Unsaved changes require explicit discard. "
        "Rediscover its new target before further edits.",
        schema({{"discard", discard}}),
        [switch_scene](const Json& request) { switch_scene({}, request.value("discard", false)); });
    add("document.open",
        "Open a project-relative JSON scene through the editor. Unsaved changes require "
        "explicit discard. Rediscover its new target before further edits.",
        schema({{"path", path}, {"discard", discard}}, {"path"}),
        [switch_scene](const Json& request) {
            switch_scene(automation_scene_path(request), request.value("discard", false));
        });
    return methods;
}
} // namespace forge
