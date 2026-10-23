#pragma once
#include "automation_schema.hpp"
#include "content_files.hpp"
namespace forge {
inline std::vector<LiveAuthoringMethod> asset_file_automation_methods(SceneDocument& document,
                                                                      ContentFiles& files,
                                                                      ContentImports& imports,
                                                                      bool writes) {
    std::vector<LiveAuthoringMethod> methods;
    methods.push_back(automation_method(
        "asset.files.review",
        "Read a page of the current source-file operation and reference-impact review.",
        "asset.read",
        automation_object(
            {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
        false, [&document, &files](const Json& request) {
            if (files.project() != document.project())
                return Json{{"state", "idle"}};
            return files.automation_status(request.value("offset", std::size_t{}),
                                           request.value("limit", std::size_t(128)));
        }));
    if (!writes)
        return methods;
    methods.push_back(automation_method(
        "asset.files.prepare",
        "Prepare Move, Duplicate or Delete through the existing source-file owner, including "
        "reference impact. Discovered scene identity may be registered; source changes await "
        "confirmation.",
        "asset.write",
        automation_object(
            {{"asset", automation_uuid()},
             {"action", {{"type", "string"}, {"enum", {"move", "duplicate", "delete"}}}},
             {"source", automation_text(2047, 1)},
             {"destination", automation_text(2047, 1)}},
            {"asset", "action"}),
        true, [&document, &files, &imports](const Json& request) {
            document.check_ownership();
            const auto current = imports.catalog();
            if (!current || current->project_root() != document.project())
                throw AuthoringError("not_ready", "Wait for the current asset catalog");
            const auto id = AssetId::parse(request.at("asset").get<std::string>());
            AssetRecord asset;
            const auto found = current->records().find(id);
            if (found != current->records().end())
                asset = found->second;
            else if (request.contains("source")) {
                AssetCatalog discovered(document.project());
                asset = discovered.add_scene(
                    automation_relative_path(request.at("source").get<std::string>()));
                if (asset.id != id)
                    throw AuthoringError("wrong_target",
                                         "Scene source does not have the requested identity");
            } else
                throw AuthoringError(
                    "not_found",
                    "Asset is not registered; a discovered scene requires its source path");
            if (request.contains("source") &&
                !ProjectPaths(document.project())
                     .same_locator(asset.source, automation_relative_path(
                                                     request.at("source").get<std::string>())))
                throw AuthoringError("wrong_target", "Source does not match the registered asset");
            const auto action = request.at("action").get<std::string>();
            const auto kind = action == "move"        ? AssetFileAction::Move
                              : action == "duplicate" ? AssetFileAction::Duplicate
                                                      : AssetFileAction::Delete;
            if (kind != AssetFileAction::Delete && !request.contains("destination"))
                throw AuthoringError("invalid_arguments",
                                     "Move and Duplicate require a new destination");
            if (kind == AssetFileAction::Delete && request.contains("destination"))
                throw AuthoringError("invalid_arguments", "Delete does not take a destination");
            const auto destination =
                kind == AssetFileAction::Delete
                    ? std::filesystem::path{}
                    : automation_relative_path(request.at("destination").get<std::string>());
            files.automation_prepare(document, asset, kind, destination);
            return files.automation_status();
        }));
    methods.push_back(automation_method(
        "asset.files.commit",
        "Commit this exact reviewed file operation. Delete requires explicit acknowledgment; "
        "completed disk commits are separate from Scene Undo.",
        "asset.write",
        automation_object({{"job", automation_integer(1)},
                           {"review", automation_digest()},
                           {"acknowledge", {{"type", "boolean"}}}},
                          {"job", "review"}),
        true,
        [&document, &files](const Json& request) {
            if (files.project() != document.project())
                throw AuthoringError("wrong_job", "File operation belongs to another project");
            files.automation_commit(request.at("job").get<std::uint64_t>(),
                                    request.at("review").get<std::string>(),
                                    request.value("acknowledge", false));
            return files.automation_status();
        },
        true));
    for (bool close : {false, true})
        methods.push_back(automation_method(
            close ? "asset.files.close" : "asset.files.cancel",
            close ? "Close a settled file operation and resume automatic source updates."
                  : "Cancel this exact pending file operation; completed commits remain.",
            "asset.write", automation_object({{"job", automation_integer(1)}}, {"job"}), true,
            [&document, &files, &imports, close](const Json& request) {
                if (files.project() != document.project())
                    throw AuthoringError("wrong_job", "File operation belongs to another project");
                if (close)
                    files.automation_close(request.at("job").get<std::uint64_t>(), imports);
                else
                    files.automation_cancel(request.at("job").get<std::uint64_t>());
                return files.automation_status();
            },
            true));
    return methods;
}
} // namespace forge
