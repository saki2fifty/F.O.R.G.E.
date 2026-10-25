#include "editor/material_graph_editor.hpp"
#include "editor/source_document_automation.hpp"
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class Predicate> void await(Predicate ready, const char* why) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready()) {
        check(std::chrono::steady_clock::now() < until, why);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
template <class Operation> void rejects(Operation operation, std::string_view code) {
    try {
        operation();
    } catch (const AuthoringError& error) {
        check(error.code == code, "Unexpected source adapter rejection");
        return;
    }
    throw std::runtime_error("Source adapter accepted a stale document session");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--build-asset") {
            // A deliberately blocked worker tests real supervisor cancellation.
            // It never pretends to produce a compiled shader or GPU result.
            const auto project =
                std::filesystem::current_path().parent_path().parent_path().parent_path();
            std::ofstream(project / "preview-worker-started") << "started\n";
            std::this_thread::sleep_for(std::chrono::seconds(10));
            return 1;
        }
        check(argc == 2, "Need graph lifecycle scratch root");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto first = root / "First", second = root / "Second";
        std::filesystem::create_directories(first / "Assets");
        std::filesystem::create_directories(second / "Assets");
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        const std::filesystem::path source = "Assets/Shared.shader.json";
        project.open_project(second, true);
        const auto second_id =
            MaterialGraphDocument::create(project.writer_guard(), source)->source().asset();
        project.open_project(first, true);
        const auto first_id =
            MaterialGraphDocument::create(project.writer_guard(), source)->source().asset();
        check(first_id != second_id, "Projects need distinct graph identities");
        const auto first_bytes = asset_detail::read_bytes(first / source, 1024 * 1024);
        const auto second_bytes = asset_detail::read_bytes(second / source, 1024 * 1024);

        // Exercise the actual document and queue owner. Shader publication/rendering
        // are separate boundaries: this fixture admits and acknowledges publication
        // without claiming shader compilation or GPU acceptance.
        MaterialGraphEditor graph({}, []() -> asset_detail::ShaderCompilerProfile {
            throw std::runtime_error("Headless lifecycle fixture has no shader compiler");
        });
        unsigned publications = 0, releases = 0;
        graph.publish = [&](const auto& locator) {
            check(locator == source, "Unexpected graph publication locator");
            ++publications;
            return true;
        };
        graph.release_preview = [&] { ++releases; };
        std::string message;
        graph.open(project, source);
        check(graph.document() && graph.document()->source().asset() == first_id,
              "Actual first-project graph did not open");
        const auto adapter = AssetDocumentAutomation::bind(graph, project);
        const Json first_request{{"document_session", graph.document()->session()},
                                 {"document_revision", graph.document()->revision()}};
        graph.request_save();
        graph.poll(project, message);
        check(publications == 1, "Fixture publication was not requested");
        graph.source_published(first_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(310));
        graph.poll(project, message);
        check(!graph.dirty() && !graph.pending(), "Fixture graph must settle before switching");

        graph.request_save();
        check(!graph.dirty(), "A queued Save on a clean graph should not create source changes");
        const auto released = releases;
        project.open_project(second, true);
        graph.poll(project, message);
        check(publications == 1, "Old graph queued Save reached the new project's publisher");
        check(!adapter.invoke("status", {}).at("open").get<bool>(),
              "MCP source adapter still exposes the old graph");
        rejects([&] { adapter.invoke("read", first_request); }, "not_open");
        check(!graph.is_open() && !graph.dirty() && !graph.pending(),
              "Old graph remained active after project switch");
        check(releases == released + 1, "Project switch did not release the graph preview");
        check(asset_detail::read_bytes(first / source, 1024 * 1024) == first_bytes &&
                  asset_detail::read_bytes(second / source, 1024 * 1024) == second_bytes,
              "Project switch changed authored graph files");
        graph.open(project, source);
        check(graph.document() && graph.document()->source().asset() == second_id &&
                  graph.document()->project() == project.project(),
              "Same locator opened the previous project's graph");
        rejects([&] { adapter.invoke("read", first_request); }, "wrong_document");
        const Json second_request{{"document_session", graph.document()->session()},
                                  {"document_revision", graph.document()->revision()}};
        check(adapter.invoke("read", second_request).at("value") ==
                  graph.document()->source().document,
              "MCP source adapter reads a different graph than the editor");

        // A request made in the old project must not be reinterpreted as a new
        // project's locator, even when both projects have that same source path.
        const std::filesystem::path other = "Assets/Other.shader.json";
        const auto second_other =
            MaterialGraphDocument::create(project.writer_guard(), other)->source().asset();
        graph.request_save();
        graph.poll(project, message);
        graph.source_published(second_id);
        graph.open(project, other);
        project.open_project(first, true);
        MaterialGraphDocument::create(project.writer_guard(), other);
        graph.poll(project, message);
        check(!graph.is_open(), "Old queued open was resolved inside the new project");
        graph.open(project, source);
        const auto session = graph.document()->session();
        graph.request_save();
        graph.poll(project, message);
        graph.source_published(first_id);
        graph.open(project, other);
        graph.poll(project, message);
        check(graph.document() && graph.document()->locator() == other &&
                  graph.document()->session() != session,
              "Normal same-project queued open no longer works");
        project.open_project(second, true);
        graph.open(project, other); // New-project request arrives before the next poll.
        graph.poll(project, message);
        check(graph.document() && graph.document()->source().asset() == second_other,
              "Explicit new-project open was discarded with the old document");

        project.open_project(first, true);
        graph.poll(project, message);
        MaterialGraphEditor preview(std::filesystem::absolute(argv[0]), [] {
            return asset_detail::ShaderCompilerProfile{std::string(64, 'a'), false};
        });
        unsigned preview_adoptions = 0;
        preview.update_preview = [&](auto, auto) { ++preview_adoptions; };
        preview.open(project, source);
        std::this_thread::sleep_for(std::chrono::milliseconds(310));
        preview.poll(project, message);
        await([&] { return std::filesystem::exists(first / "preview-worker-started"); },
              "Actual preview worker did not start");
        check(preview.pending(), "Preview future was not retained");
        project.open_project(second, true);
        preview.poll(project, message); // Cancel and close without waiting for the child.
        check(!preview.is_open(), "Old project remained open while its preview was cancelling");
        preview.open(project, source); // May queue while the cancelled future drains.
        await(
            [&] {
                preview.poll(project, message);
                return preview.document() && preview.document()->source().asset() == second_id;
            },
            "New-project graph did not open after the cancelled preview drained");
        check(preview_adoptions == 0 && preview.diagnostic().empty(),
              "Old preview result or diagnostic crossed the project boundary");
        check(asset_detail::read_bytes(first / source, 1024 * 1024) == first_bytes &&
                  asset_detail::read_bytes(second / source, 1024 * 1024) == second_bytes,
              "Graph lifecycle changed authored source files");
        std::cout << "Actual graph project boundaries, queued Save/open, worker cancellation and "
                     "reopen passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
