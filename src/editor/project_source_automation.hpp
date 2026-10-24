#pragma once
#include "../asset_bytes.hpp"
#include "../asset_storage.hpp"
#include "../bounded_json.hpp"
#include "../ui_asset_catalog.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include <forge/shader_asset.hpp>

namespace forge {
// Raw UI/shader files have no in-editor text document. These operations use the
// same leased, baseline-checked source storage as external editing; registration
// and publication stay explicit in their existing owners. Scene/prefab/typed
// source JSON, metadata, C++ and engine files cannot enter this path.
struct ProjectSourceAutomation {
    static constexpr std::size_t limit = 1024 * 1024;
    static bool allowed_root(const std::filesystem::path& path) {
        const auto first = path.begin()->string();
        return first == "Assets" || first == "Sources" || first == "UI" || first == "Shaders";
    }
    static bool source_extension(const std::filesystem::path& path) {
        const auto extension = path.extension();
        return extension == ".rml" || extension == ".rcss" || extension == ".hlsl" ||
               extension == ".hlsli" || path_utf8(path).ends_with(".shader.json");
    }
    static std::filesystem::path path(EditorFiles& files, const Json& request) {
        files.document.check_ownership();
        auto path = automation_relative_path(request.at("path").get<std::string>());
        if (!allowed_root(path) || !source_extension(path))
            throw AuthoringError(
                "unsupported_source",
                "Choose project UI/shader source under Assets, Sources, UI or Shaders");
        const ProjectPaths project(files.document.project());
        // Check the logical path before canonical resolution can erase an alias
        // into Scene/Native/metadata. Containment alone is not source ownership.
        asset_storage::ordinary(project.root() / path);
        return project.resolve(path);
    }
    static std::string digest(const std::string& text) {
        return asset_detail::content_digest(std::as_bytes(std::span(text.data(), text.size())));
    }
    static std::optional<std::string> read(const std::filesystem::path& path) {
        const auto text = asset_storage::read(path, limit);
        if (text) {
            if (text->find('\0') != std::string::npos)
                throw AuthoringError("invalid_source", "Source must be UTF-8 text without NUL");
            (void)Json(*text).dump();
        }
        return text;
    }
    static void validate(const std::filesystem::path& path, const std::string& text,
                         const std::optional<std::string>& before) {
        if (text.size() > limit || text.find('\0') != std::string::npos)
            throw AuthoringError("invalid_source",
                                 "Source must be UTF-8 text without NUL, at most1MiB");
        (void)Json(text).dump();
        if (path.extension() == ".rml" || path.extension() == ".rcss")
            validate_ui_text(text, path.extension() == ".rml");
        if (path_utf8(path).ends_with(".shader.json")) {
            if (!before)
                throw AuthoringError("create_required",
                                     "Use source.shader.create to mint a shader identity first");
            const auto old = Json::parse(*before), candidate = Json::parse(text);
            if (old.at("format") != "forge.shader" || old.at("version") == 3 ||
                candidate.at("format") != "forge.shader" || candidate.at("version") == 3 ||
                old.at("asset_id") != candidate.at("asset_id"))
                throw AuthoringError("identity_changed",
                                     "Keep the existing shader identity/format; use the graph "
                                     "owner for graph sources");
            (void)shader_program_source(candidate);
        }
    }
    static std::vector<LiveAuthoringMethod> methods(EditorFiles& files,
                                                    const ui::AutomationPermissions& permissions,
                                                    std::function<std::string()> unavailable) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "source.text.list",
            "Page contained UI/shader source files and directories, including "
            "unregistered files. Catalog identities remain in assets.list.",
            "assets.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [&files](const Json& request) {
                files.document.check_ownership();
                const ProjectPaths project(files.document.project());
                std::vector<Json> entries;
                std::size_t visited = 0;
                for (const auto* name : {"Assets", "Sources", "UI", "Shaders"}) {
                    asset_storage::ordinary(project.root() / name);
                    const auto root = project.resolve(name);
                    if (!std::filesystem::is_directory(root))
                        continue;
                    entries.push_back({{"path", name}, {"directory", true}});
                    for (std::filesystem::recursive_directory_iterator it(root), end; it != end;
                         ++it) {
                        if (it->is_symlink()) {
                            it.disable_recursion_pending();
                            continue;
                        }
                        if (++visited > 10000 || it.depth() > 32)
                            throw AuthoringError("source_limit",
                                                 "Source discovery exceeds its bounded traversal");
                        const auto relative = project.relative(it->path());
                        if (it->is_directory() ||
                            (it->is_regular_file() && source_extension(relative)))
                            entries.push_back(
                                {{"path", path_utf8(relative)}, {"directory", it->is_directory()}});
                    }
                }
                std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                    return a.at("path").template get<std::string>() <
                           b.at("path").template get<std::string>();
                });
                const auto offset = request.value("offset", std::size_t{}),
                           count = request.value("limit", std::size_t(128));
                Json page = Json::array();
                for (auto i = offset; i < entries.size() && i - offset < count; ++i)
                    page.push_back(entries[i]);
                return Json{{"entries", std::move(page)}, {"total", entries.size()}};
            }));
        result.push_back(automation_method(
            "source.text.read",
            "Read a contained raw UI/HLSL source or shader-program descriptor "
            "and its exact disk digest. A missing path returns state:'missing'. "
            "Typed source drafts remain in source.read; this is external-style "
            "file editing, with no document Undo history.",
            "assets.read", automation_object({{"path", automation_text(2047, 1)}}, {"path"}), false,
            [&files](const Json& request) {
                const auto file = path(files, request);
                const auto text = read(file);
                return Json{{"path", request.at("path")},
                            {"exists", bool(text)},
                            {"state", text ? digest(*text) : "missing"},
                            {"text", text.value_or("")}};
            }));
        if (!permissions.assets)
            return result;
        const auto ready = [&files, unavailable] {
            files.document.check_ownership();
            const auto reason = unavailable ? unavailable() : "";
            if (!reason.empty())
                throw AuthoringError("source_busy", reason);
        };
        result.push_back(automation_method(
            "source.folder.create",
            "Create one new source directory under Assets, Sources, UI or "
            "Shaders, with an existing parent. No catalog identity is minted.",
            "assets.write", automation_object({{"path", automation_text(2047, 1)}}, {"path"}), true,
            [&files, ready](const Json& request) {
                ready();
                const auto relative =
                    automation_relative_path(request.at("path").get<std::string>());
                if (!allowed_root(relative))
                    throw AuthoringError("invalid_path", "Choose a source directory");
                const ProjectPaths project(files.document.project());
                asset_storage::ordinary(project.root() / relative);
                const auto file = project.resolve(relative);
                if (!std::filesystem::create_directory(file))
                    throw AuthoringError("already_exists", "This source directory already exists");
                asset_storage::sync_directory(file.parent_path());
                return Json{{"path", path_utf8(relative)}, {"created", true}};
            }));
        result.push_back(automation_method(
            "source.text.write",
            "Atomically write raw UI/HLSL source at its observed disk digest, "
            "or create at state:'missing'. Source directories must exist. "
            "Preserve shader-program identity; UI registration and shader "
            "import/publication remain separate. This does not write engine, "
            "scene, prefab, metadata, C++ or typed source documents.",
            "assets.write",
            automation_object({{"path", automation_text(2047, 1)},
                               {"state", automation_text(64, 1)},
                               {"text", automation_text(limit)}},
                              {"path", "state", "text"}),
            true, [&files, ready](const Json& request) {
                ready();
                const auto file = path(files, request);
                const auto before = read(file);
                if (request.at("state") != (before ? digest(*before) : "missing"))
                    throw AuthoringError("source_conflict",
                                         "Source changed; read it before writing");
                const auto text = request.at("text").get<std::string>();
                validate(file, text, before);
                if (!std::filesystem::is_directory(file.parent_path()))
                    throw AuthoringError("invalid_path", "Choose an existing source directory");
                asset_storage::replace(file, text);
                return Json{{"path", request.at("path")}, {"state", digest(text)}};
            }));
        result.push_back(automation_method(
            "source.shader.create",
            "Create a version1 shader-program descriptor with an engine-"
            "minted identity and validated stages, without overwriting a "
            "file or compiling. Edit HLSL with source.text.write; build "
            "through the existing shader importer.",
            "assets.write",
            automation_object({{"path", automation_text(2047, 1)},
                               {"source_root", automation_text(2047, 1)},
                               {"stages",
                                {{"type", "array"},
                                 {"minItems", 1},
                                 {"maxItems", 6},
                                 {"items", automation_object({{"stage", automation_text(32, 1)},
                                                              {"source", automation_text(2047, 1)},
                                                              {"entry", automation_text(256, 1)}},
                                                             {"stage", "source", "entry"})}}}},
                              {"path", "source_root", "stages"}),
            true, [&files, ready](const Json& request) {
                ready();
                const auto file = path(files, request);
                if (!path_utf8(file).ends_with(".shader.json") || std::filesystem::exists(file))
                    throw AuthoringError("invalid_path", "Choose a new .shader.json file");
                const auto root =
                    automation_relative_path(request.at("source_root").get<std::string>());
                if (!std::filesystem::is_directory(
                        ProjectPaths(files.document.project()).resolve(root)))
                    throw AuthoringError("invalid_path",
                                         "Choose an existing contained source_root");
                const auto id = AssetId::generate();
                Json document{{"format", "forge.shader"},
                              {"version", 1},
                              {"asset_id", id},
                              {"source_root", path_utf8(root)},
                              {"stages", request.at("stages")}};
                (void)shader_program_source(document);
                const auto text = document.dump(2) + "\n";
                asset_storage::replace(file, text);
                return Json{{"path", request.at("path")}, {"asset", id}, {"state", digest(text)}};
            }));
        result.push_back(automation_method(
            "source.ui.register",
            "Admit a project RML/style/font/image through the existing UI "
            "catalog operation, preserving registered identities. This "
            "publishes metadata; it does not reload live fonts/UI.",
            "assets.write", automation_object({{"path", automation_text(2047, 1)}}, {"path"}), true,
            [&files, ready](const Json& request) {
                ready();
                const auto relative =
                    automation_relative_path(request.at("path").get<std::string>());
                const auto record = register_ui_source(*files.document.writer_guard(), relative);
                return Json{{"asset", record.id},
                            {"type", record.type},
                            {"path", path_utf8(record.source)}};
            }));
        result.push_back(automation_method(
            "source.ui.example",
            "Create and register the existing HUD example through its native "
            "owner. This is separate from scene Undo; assign its returned "
            "document asset to a UI Document component.",
            "assets.write", automation_object(), true, [&files, ready](const Json&) {
                ready();
                const auto record = create_ui_example(files.document.project());
                return Json{{"asset", record.id},
                            {"type", record.type},
                            {"path", path_utf8(record.source)}};
            }));
        return result;
    }
};
} // namespace forge
