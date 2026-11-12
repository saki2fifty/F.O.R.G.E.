#pragma once
#include "content_files.hpp"
#include "document_workspace.hpp"
#include <thread>

inline void test_source_file_lifecycle(forge::ui::DocumentWorkspace& documents,
                                       forge::SceneDocument& project, forge::Scene& scene) {
    using namespace forge;
    const auto require = [](bool ok, const std::string& why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto original_scene = scene.document();
    ContentImports imports;
    ContentFiles files;
    files.unavailable = [&](const auto& record, auto) -> std::string {
        return documents.source_drafts_dirty() || documents.source_publication_blocked(record.id)
                   ? "Settle source drafts and pending actions"
                   : "";
    };
    files.adopted = [&](const auto& review, const auto&, auto) {
        const auto& request = review.plan.request;
        if (request.action != AssetFileAction::Duplicate)
            documents.source_file_changed(request.asset, request.action == AssetFileAction::Move
                                                             ? std::optional(request.destination)
                                                             : std::nullopt);
    };
    const auto finish = [&](std::string_view expected) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::string message;
        for (;;) {
            files.poll(project, scene, imports, message);
            const auto state = files.automation_status();
            if (state.at("state").get<std::string>() == expected)
                return state;
            require(state.at("diagnostic") == "", "File operation: " + state.dump());
            require(std::chrono::steady_clock::now() < end, "File operation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    const auto perform = [&](AssetId id, AssetFileAction action,
                             const std::filesystem::path& path) {
        const auto catalog = AssetCatalog::open_project(project.project());
        files.automation_prepare(project, catalog.records().at(id), action, path);
        const auto review = finish("review");
        const auto job = review.at("job").get<std::uint64_t>();
        const auto revision = review.at("review").get<std::string>();
        try {
            files.automation_commit(job, revision + "-stale", true);
            throw std::runtime_error("Stale file review was accepted");
        } catch (const AuthoringError& error) {
            require(error.code == "stale_review", "Unexpected file review rejection");
        }
        if (action == AssetFileAction::Delete) {
            try {
                files.automation_commit(job, revision, false);
                throw std::runtime_error("Delete was accepted without impact acknowledgement");
            } catch (const AuthoringError& error) {
                require(error.code == "confirmation_required", "Unexpected Delete rejection");
            }
        }
        files.automation_commit(job, revision, true);
        const auto result = finish("succeeded");
        require(result.at("diagnostic") == "", "Committed file operation: " + result.dump());
        files.automation_close(job, imports);
        return result;
    };
    for (const auto& [owner, path] : {std::pair{"editable_mesh", "Assets/Shape.mesh.json"},
                                      std::pair{"animation_mask", "Assets/Weighted.mask.json"},
                                      std::pair{"material", "Assets/Checker.material.json"},
                                      std::pair{"audio_import", "Assets/ProcessingOriginal.wav"}}) {
        const auto* entry = documents.find(owner);
        const bool source = bool(entry->source);
        const auto& access = source ? *entry->source : *entry->importer;
        const auto status = [&] { return access.invoke("status", Json::object()); };
        access.invoke("open", {{"path", path}});
        auto original = status();
        require(original.at("open").get<bool>() && !original.at("dirty").get<bool>(),
                std::string(owner) + " must start with a clean source");
        const auto id = original.at("asset").get<AssetId>();
        if (std::string_view(owner) == "editable_mesh") {
            auto edit = Json{
                {"document_session", original.at("session")},
                {"document_revision", original.at("revision")},
                {"patch",
                 Json::array({{{"op", "add"}, {"path", "/file_test_note"}, {"value", "draft"}}})}};
            access.invoke("patch", edit);
            const auto catalog = AssetCatalog::open_project(project.project());
            try {
                files.automation_prepare(project, catalog.records().at(id), AssetFileAction::Move,
                                         "Assets/Blocked.mesh.json");
                throw std::runtime_error("File review accepted a dirty mesh editor");
            } catch (const AuthoringError& error) {
                require(error.code == "busy" && !files.busy(), "Dirty file review did not stop");
            }
            const auto dirty = status();
            access.invoke("close", {{"document_session", dirty.at("session")},
                                    {"document_revision", dirty.at("revision")},
                                    {"discard", true}});
            access.invoke("open", {{"path", path}});
            original = status();
        }
        const auto filename = std::filesystem::path(path).filename().string();
        const auto copy = std::filesystem::path("Assets") / ("Copy-" + filename);
        if (std::string_view(owner) == "editable_mesh") {
            // This source format deliberately has no Move/Duplicate adapter.
            // Preserve that admission boundary while fixing its Delete notification.
            for (auto action : {AssetFileAction::Duplicate, AssetFileAction::Move}) {
                const auto catalog = AssetCatalog::open_project(project.project());
                files.automation_prepare(project, catalog.records().at(id), action, copy);
                const auto rejected = finish("failed");
                require(!rejected.at("diagnostic").get<std::string>().empty() &&
                            status() == original &&
                            !std::filesystem::exists(project.project() / copy) &&
                            std::filesystem::exists(project.project() / path),
                        "Unsupported mesh file operation changed its editor/source");
                files.automation_close(rejected.at("job").get<std::uint64_t>(), imports);
            }
            const auto deleted = perform(id, AssetFileAction::Delete, {});
            require(!status().at("open").get<bool>() &&
                        !std::filesystem::exists(project.project() / path) &&
                        !deleted.at("receipt").at("retained_files").get<std::string>().empty(),
                    "Committed mesh Delete left its source editor open");
            continue;
        }
        const auto duplicated = perform(id, AssetFileAction::Duplicate, copy);
        require(duplicated.at("result") != id.str() && status() == original &&
                    std::filesystem::exists(project.project() / copy),
                "Duplicate changed the original editor or reused its asset identity");
        const auto destination = std::filesystem::path("Assets") / ("Moved-" + filename);
        const auto moved = perform(id, AssetFileAction::Move, destination);
        const auto current = status();
        require(moved.at("result") == id.str() && current.at("asset").get<AssetId>() == id &&
                    current.at(source ? "path" : "source") == path_utf8(destination) &&
                    current.at("session") != original.at("session") &&
                    !current.at("dirty").get<bool>() &&
                    !std::filesystem::exists(project.project() / path),
                std::string(owner) + " did not adopt its moved source with the same asset ID");
        if (source) {
            try {
                access.invoke("read", {{"document_session", original.at("session")}});
                throw std::runtime_error("Source Move retained an obsolete MCP session");
            } catch (const AuthoringError& error) {
                require(error.code == "wrong_document", "Wrong stale-session rejection");
            }
        }
        const auto deleted = perform(id, AssetFileAction::Delete, {});
        require(!status().at("open").get<bool>() &&
                    !std::filesystem::exists(project.project() / destination) &&
                    std::filesystem::is_directory(
                        project.project() /
                        std::filesystem::u8path(
                            deleted.at("receipt").at("retained_files").get<std::string>())),
                "Delete left its source editor open or lost the retained backup");
    }
    require(scene.document() == original_scene,
            "Source file operations changed the authored scene");
}
