#include "editor/material_graph_editor.hpp"
#include "editor/source_document_automation.hpp"
#include "surface_material_fixture.hpp"
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
        check(argc == 2 || argc == 3,
              "Need graph lifecycle scratch root and optional fault worker");
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
        unsigned preview_requests = 0;
        MaterialGraphEditor preview(std::filesystem::absolute(argc == 3 ? argv[2] : argv[0]), [&] {
            ++preview_requests;
            return asset_detail::ShaderCompilerProfile{std::string(64, 'a'), false};
        });
        unsigned preview_adoptions = 0;
        std::optional<MaterialResourceData> adopted;
        preview.update_preview = [&](auto data, auto) {
            ++preview_adoptions;
            adopted = std::move(data);
        };
        SurfaceShaderDefinition definition;
        definition.uv_sets = {0};
        definition.parameters["gain"] = {MaterialParameterType::Scalar, {1, 0, 0, 0}};
        definition.textures["image"].semantic = TextureSemantic::Color;
        auto values = surface_material_defaults(definition);
        values.parameters.at("gain").value[0] = 3;
        const AssetRef<TextureAsset> texture{AssetId::generate()};
        preview.preview_material(
            {values, {{"image", texture}}, AssetRef<ShaderAsset>{first_id}, {}});
        preview.open(project, source);
        std::this_thread::sleep_for(std::chrono::milliseconds(310));
        preview.poll(project, message);
        await(
            [&] {
                preview.poll(project, message);
                if (!preview.pending() && !preview.diagnostic().empty())
                    throw std::runtime_error("Preview worker stopped before startup: " +
                                             preview.diagnostic());
                return std::filesystem::exists(first / "preview-worker-started");
            },
            "Actual preview worker did not start");
        check(preview.pending(), "Preview future was not retained");
        unsigned preview_publications = 0;
        preview.publish = [&](const auto&) {
            ++preview_publications;
            return true;
        };
        preview.request_save();
        preview.poll(project, message);
        check(preview_publications == 0,
              "Save started publication before the active preview worker retired");
        await(
            [&] {
                preview.poll(project, message);
                return preview_publications == 1;
            },
            "Save did not publish after cancelling the active preview");
        std::this_thread::sleep_for(std::chrono::milliseconds(310));
        preview.poll(project, message);
        check(preview_requests == 1 && preview_adoptions == 0,
              "Preview compilation restarted during publication");
        const auto saved_session = preview.document()->session();
        const auto saved_revision = preview.document()->revision();
        auto selected = AssetCatalog::open_project(first);
        const auto cooked = test::publish_surface_fixture(first, selected, first_id, definition);
        auto published_record = selected.records().at(first_id);
        published_record.source = source;
        selected.replace(std::move(published_record));
        selected.save(AssetCatalog::project_index(first));
        preview.source_published(first_id);
        preview.asset_catalog_changed(std::make_shared<const AssetCatalog>(selected));
        await(
            [&] {
                preview.poll(project, message);
                return !preview.pending();
            },
            "Published Shader preview did not settle");
        check(preview.preview_current() && preview.diagnostic().empty() && preview_requests == 1 &&
                  preview_adoptions == 1 && adopted &&
                  encode_shader(adopted->surface->program) == encode_shader(cooked) &&
                  adopted->surface->revision ==
                      selected.records().at(first_id).metadata.at("forge.import").at("key") &&
                  adopted->values.parameters.at("gain").value[0] == 3 &&
                  adopted->textures.at("image") == texture &&
                  preview.document()->session() == saved_session &&
                  preview.document()->revision() == saved_revision,
              "Publication reuse changed its program, preview values, selection or history");

        preview.request_save();
        preview.poll(project, message);
        auto broken = selected;
        auto record = broken.records().at(first_id);
        record.metadata["forge.shader"]["layout"] = std::string(64, 'f');
        broken.replace(std::move(record));
        preview.source_published(first_id);
        preview.asset_catalog_changed(std::make_shared<const AssetCatalog>(broken));
        await(
            [&] {
                preview.poll(project, message);
                return !preview.pending();
            },
            "Rejected published Shader preview did not settle");
        check(preview.preview_ready() && !preview.diagnostic().empty() && preview_adoptions == 1 &&
                  preview_requests == 1,
              "Invalid published provenance replaced the last-good preview or recompiled it");

        preview.request_close();
        preview.poll(project, message);
        check(!preview.is_open(), "Saved graph failed to close after preview rejection");
        std::filesystem::remove(first / "preview-worker-started");
        preview.open(project, source);
        check(preview.is_open(), preview.diagnostic().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(310));
        preview.poll(project, message);
        await(
            [&] {
                preview.poll(project, message);
                if (!preview.pending() && !preview.diagnostic().empty())
                    throw std::runtime_error(preview.diagnostic());
                return std::filesystem::exists(first / "preview-worker-started");
            },
            "Unsaved preview path did not restart after reopening");
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
        check(preview_adoptions == 1 && preview.diagnostic().empty(),
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
