#pragma once
#include "animation_blend_space_authoring.hpp"
#include "animation_mask_authoring.hpp"
#include "animation_states_authoring.hpp"
#include "audio_imports.hpp"
#include "editable_mesh_authoring.hpp"
#include "source_document_automation.hpp"
#include "transform_animation_authoring.hpp"
#include <fstream>

// Runs against the actual editors already bound by mcp_source_host, using a
// disposable copy of FeatureGallery and real import/publication services.
inline void test_source_publication(forge::ui::DocumentWorkspace& documents,
                                    forge::SceneDocument& project) {
    using namespace forge;
    const auto require = [](bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    struct Case {
        const char *editor, *path, *field;
        Json value;
        std::shared_ptr<const AssetImporterRegistry> registry;
        std::function<void(AssetPublicationCandidate&, const AssetImportPlan&)> prepare;
    };
    const std::vector<Case> cases{
        {"animation_states", "Assets/MarkerPulse.states.json", "/states/0/speed", 1.5,
         animation_states_import_registry(), prepare_animation_states_publication},
        {"animation_blend_space", "Assets/Motion.blendspace.json", "/cycle_seconds", 3.0,
         animation_blend_space_import_registry(), prepare_animation_blend_space_publication},
        {"animation_mask", "Assets/Weighted.mask.json", "/default_weight", .25,
         animation_mask_import_registry(), prepare_animation_mask_publication},
        {"animation_timeline", "Assets/Platform.animation.json", "/keys/0/translation/1", 2.0,
         transform_animation_import_registry(), prepare_transform_animation_publication},
        {"editable_mesh", "Assets/Shape.mesh.json", "/faces/0/corners/0/uv/0", .125,
         editable_mesh_import_registry(), prepare_editable_mesh_publication}};
    for (const auto& test : cases) {
        const auto& access = *documents.find(test.editor)->source;
        const auto status = [&] { return access.invoke("status", Json::object()); };
        const auto read = [&] {
            return access.invoke("read", {{"document_session", status().at("session")}})
                .at("value");
        };
        const auto request = [](const Json& state) {
            return Json{{"document_session", state.at("session")},
                        {"document_revision", state.at("revision")}};
        };
        access.invoke("open", {{"path", test.path}});
        const auto original = status();
        const auto source = read();
        const auto id = original.at("asset").get<AssetId>();
        const Json::json_pointer field(test.field);
        require(!original.at("dirty").get<bool>(),
                std::string(test.editor) + " gallery source must initially open cleanly");
        const auto write = [&](const Json& value) {
            std::ofstream file(project.project() / test.path, std::ios::binary | std::ios::trunc);
            file << value.dump();
            file.close();
            require(bool(file), "Cannot write external test source");
        };
        const auto publish = [&](const Json& value) {
            write(value);
            AssetImportService service(project.writer_guard(), test.registry,
                                       desktop_texture_target());
            service.submit(
                service.prepare(test.path),
                [&](auto& candidate, const auto& plan, const auto&) {
                    test.prepare(candidate, plan);
                },
                [](const auto&, const auto&) {});
            require(service.wait_idle(std::chrono::seconds(15)), "Publication timed out");
            const auto result = service.poll();
            require(result.size() == 1 && result.front().published,
                    std::string(test.editor) + " publication failed: " +
                        (result.empty() ? "no result" : result.front().diagnostic));
            documents.source_published(id);
        };
        auto external = source;
        external.at(field) = test.value;
        publish(external);
        const auto refreshed = status();
        require(read() == external && refreshed.at("session") != original.at("session") &&
                    !refreshed.at("dirty").get<bool>() && !refreshed.at("can_undo").get<bool>(),
                std::string(test.editor) + " retained stale source/history after publication");
        try {
            access.invoke("read", {{"document_session", original.at("session")}});
            throw std::runtime_error("Stale MCP session was accepted");
        } catch (const AuthoringError& error) {
            require(error.code == "wrong_document", "Unexpected stale-session error");
        }
        documents.source_published(AssetId::generate());
        require(status() == refreshed, "An unrelated publication reset this document");

        auto patch = request(refreshed);
        patch["patch"] =
            Json::array({{{"op", "replace"}, {"path", test.field}, {"value", source.at(field)}}});
        access.invoke("patch", patch);
        const auto dirty = status();
        require(documents.source_publication_blocked(id), "Dirty source did not block reimport");
        // A notification arriving after the user starts a draft cannot replace it.
        publish(external);
        require(status() == dirty && read().at(field) == source.at(field),
                "Publication replaced a dirty draft or its history");
        auto close = request(dirty);
        close["discard"] = true;
        access.invoke("close", close);
        access.invoke("open", {{"path", test.path}});
        const auto reopened = status();
        require(!documents.source_publication_blocked(id) && read() == external,
                "Reopening did not adopt the published source");

        // The file can change again after publication but before its notification.
        // Failed parsing must keep the accepted editor document available.
        write(Json::object());
        documents.source_published(id);
        require(status().at("session") == reopened.at("session") && read() == external &&
                    !status().at("diagnostic").get<std::string>().empty(),
                "Failed reload lost the last good document or hid its diagnostic");
        write(external);
        documents.source_published(id);
        require(status().at("diagnostic") == "" && !status().at("dirty").get<bool>(),
                "Corrected source could not reload after failure");
        access.invoke("save", request(status()));
        const auto saving = status();
        require(saving.at("save_requested").get<bool>() && documents.source_publication_blocked(id),
                "Pending Save did not block automatic replacement");
        documents.source_published(id);
        require(status() == saving, "Publication cleared a pending Save request");
    }
}

inline void test_import_publication(forge::ui::DocumentWorkspace& documents,
                                    forge::SceneDocument& project,
                                    forge::AudioImportEditor& audio) {
    using namespace forge;
    const auto require = [](bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    const auto source = std::filesystem::path("Assets/ProcessingOriginal.wav");
    audio.open(project, source);
    require(audio.is_open() && !audio.dirty() && audio.diagnostic().empty(),
            "Gallery audio import did not open cleanly");
    const auto generation = audio.selection_generation();
    const auto asset = audio.selected_asset();
    std::filesystem::rename(project.project() / source, project.project() / "held.wav");
    documents.source_published(asset);
    require(audio.selection_generation() == generation && audio.selected_source() == source &&
                audio.selected_asset() == asset && !audio.diagnostic().empty(),
            "Failed import refresh lost its last draft or diagnostic");
    std::filesystem::rename(project.project() / "held.wav", project.project() / source);
    documents.source_published(asset);
    require(audio.selection_generation() > generation && audio.diagnostic().empty() &&
                !audio.dirty(),
            "Corrected import source could not refresh");
    const auto refreshed = audio.selection_generation();
    documents.source_published(asset, "audio_import");
    require(audio.selection_generation() == refreshed,
            "Publication unnecessarily reloaded its originating importer");
    audio.request_save();
    require(documents.source_publication_blocked(asset), "Pending import Save did not block");
    documents.source_published(asset);
    require(audio.selection_generation() == refreshed &&
                documents.source_publication_blocked(asset),
            "Background import refresh cleared a pending Save");
}
