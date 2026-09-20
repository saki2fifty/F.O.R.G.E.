#include "asset_bytes.hpp"
#include "shader_authoring.hpp"
#include "shader_pipeline.hpp"
#include <forge/project_lease.hpp>
#include <forge/shader_resource.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
using Json = nlohmann::json;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(bool(out.write(text.data(), std::streamsize(text.size()))) && bool(out.flush()),
            "Shader fixture write failed");
}
template <class F>
void rejects(F&& fn, std::source_location loc = std::source_location::current()) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Shader rejection missing at " + std::to_string(loc.line()));
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Shader fixture requires root directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto id = AssetId::generate();
        Json doc{{"format", "forge.shader"},
                 {"version", 1},
                 {"asset_id", id},
                 {"source_root", "Shaders"},
                 {"stages", Json::array({{{"stage", "vertex"}, {"source", "main.hlsl"}}})},
                 {"extension", {{"keep", 42}}}};
        write(root / "surface.shader.json", doc.dump());
        write(root / "Shaders/main.hlsl", "#include \"lib/color.hlsli\"\n");
        write(root / "Shaders/lib/color.hlsli", "#include <engine/math.hlsli>\n");
        write(root / "Shaders/ignore.txt", "Non-shader documentation");
        const ShaderSources engine{{"engine/math.hlsli", "// Engine-owned include\n"}};
        const ShaderCompilerProfile compiler{std::string(64, 'a'), false};
        const auto snapshot = capture_shader_source(root, "surface.shader.json", engine);
        require(snapshot.sources.size() == 3 && snapshot.project_sources.size() == 2 &&
                    snapshot.document == doc,
                "Shader capture changed source or namespace");
        const auto input = shader_import_input(snapshot, {}, compiler);
        require(input.source_dependencies.contains("Shaders/lib/color.hlsli") &&
                    input.source_dependencies.size() == 2,
                "Shader include has no real project dependency locator");
        AssetDependencyGraph graph;
        std::vector<AssetSourceDependency> deps;
        for (const auto& [path, digest] : input.source_dependencies)
            deps.push_back({std::filesystem::u8path(path), "shader-source", digest});
        graph.replace_sources(id, deps);
        require(graph.invalidated_by_source("Shaders/lib/color.hlsli") == std::vector<AssetId>{id},
                "Shared dependency graph omitted shader include invalidation");
        auto request = shader_process_request(snapshot, {}, compiler);
        auto decoded = decode_shader_process_request(request, compiler);
        require(decoded.sources == snapshot.sources && decoded.program == snapshot.program,
                "Shader process snapshot changed virtual paths/program");
        auto malformed = request;
        malformed.inputs.front().bytes.push_back(std::byte{' '});
        rejects([&] { decode_shader_process_request(malformed, compiler); });
        malformed = request;
        malformed.payload["sources"]["lib/color.hlsli"] = malformed.payload["sources"]["main.hlsl"];
        rejects([&] { decode_shader_process_request(malformed, compiler); });
        rejects([&] { decode_shader_process_request(request, {std::string(64, 'b'), false}); });
        rejects([&] { decode_shader_process_request(request, {compiler.digest, true}); });
        std::stop_source stopped;
        stopped.request_stop();
        rejects([&] {
            capture_shader_source(root, "surface.shader.json", engine, stopped.get_token());
        });
        auto changed_engine = engine;
        changed_engine.at("engine/math.hlsli") += "// changed\n";
        require(input.key() != shader_import_input(capture_shader_source(
                                                       root, "surface.shader.json", changed_engine),
                                                   {}, compiler)
                                   .key(),
                "Engine include omitted from shader import identity");
        write(root / "Shaders/lib/color.hlsli", "// Project include changed\n");
        require(input.key() !=
                    shader_import_input(capture_shader_source(root, "surface.shader.json", engine),
                                        {}, compiler)
                        .key(),
                "Project include omitted from shader import identity");
        write(root / "Shaders/lib/color.hlsli", "#include <engine/math.hlsli>\n");
        const auto importer = shader_importer(root / "unused-worker", compiler, engine);
        AssetImportRequest import{id,
                                  root,
                                  "surface.shader.json",
                                  {"windows-x64", "d3d12", "fxc-5.1"},
                                  {"forge.shader.diligent", 1}};
        const auto plan = importer->discover(import, {});
        require(plan.input.document() == input.document() &&
                    input.settings == importer->settings().effective(import.settings) &&
                    input.importer_revision == importer->descriptor().revision,
                "Shader importer disagrees with publication settings/revision contract");
        auto bad_import = import;
        bad_import.asset = AssetId::generate();
        rejects([&] { importer->discover(bad_import, {}); });
        // CPU transport fixture only. These bytes never reach a native graphics API.
        ShaderData shader{
            decoded.build_key,
            compiler.digest,
            true,
            false,
            {{ShaderStage::Vertex,
              "main",
              {std::byte{1}},
              {{"stage", "vertex"}, {"threads", {0, 0, 0}}, {"resources", Json::array()}}}}};
        std::vector<ArtifactFile> files{{"program.shader", encode_shader(shader)}};
        DerivedDataCache cache(root);
        const auto validator = [&](const CachedArtifact& a) { importer->validate(a); };
        cache.publish(input, files, validator);
        const auto hit = cache.find(input, validator);
        require(bool(hit), "Shader compiler profile did not reuse validated DDC entry");
        AssetPublicationCandidate candidate;
        candidate.ticket.owner = id;
        candidate.ticket.source = import.source;
        candidate.input = input;
        candidate.files = hit->files;
        prepare_shader_publication(candidate, plan);
        require(candidate.records.size() == 1 && candidate.records.front().id == id &&
                    candidate.records.front().type == "shader",
                "Shader publication lost logical identity");
        auto wrong = plan;
        wrong.data["compiler_input_key"] = std::string(64, 'b');
        rejects([&] { prepare_shader_publication(candidate, wrong); });
        // Exercise the real shared publisher and source-free runtime CPU loader.
        // Fixture bytecode remains opaque CPU test data, never supplied to a device.
        {
            AssetCatalog catalog(root);
            catalog.add({id, "shader", import.source, 1, {}});
            catalog.save(AssetCatalog::project_index(root));
            ProjectLease lease(root);
            AssetPublisher publisher(lease);
            candidate.ticket = publisher.capture(id, import.source);
            candidate.sidecar.settings = import.settings;
            candidate.sidecar.build_inputs = input.document();
            publisher.publish(candidate, *importer, [](const auto&, const auto&) {});
            catalog = AssetCatalog::open_project(root);
            std::filesystem::rename(root / "Shaders", root / "HiddenSources");
            std::filesystem::rename(root / "surface.shader.json", root / "source.hidden");
            ResourcePool<ShaderAsset> pool;
            const AssetRef<ShaderAsset> reference{id};
            const auto ticket = request_shader(pool, root, catalog, reference);
            require(pool.wait(ticket, std::chrono::seconds(5)),
                    "Cooked shader required source files");
            const auto old = pool.acquire(ticket);
            require(old && old->build_key == decoded.build_key,
                    "Shader resource lost compiler identity");
            auto broken_catalog = catalog;
            auto broken_record = broken_catalog.records().at(id);
            broken_record.metadata["forge.import"]["generation"] = 2;
            broken_record.metadata["forge.shader"]["layout"] = std::string(64, 'f');
            broken_catalog.replace(broken_record);
            const auto failure = request_shader(pool, root, broken_catalog, reference);
            require(!pool.wait(failure, std::chrono::seconds(5)) &&
                        pool.current(reference).identity() == old.identity(),
                    "Mismatched shader layout replaced last-good resource");
            rejects([&] { request_shader(pool, root, catalog, {AssetId::generate()}); });
            std::filesystem::rename(root / "HiddenSources", root / "Shaders");
            std::filesystem::rename(root / "source.hidden", root / "surface.shader.json");
        }
        auto bad = doc;
        bad["source_root"] = "../Shaders";
        write(root / "surface.shader.json", bad.dump());
        rejects([&] { capture_shader_source(root, "surface.shader.json", engine); });
        write(root / "surface.shader.json", doc.dump());
        write(root / "Shaders/engine/math.hlsli", "// shadow attempt\n");
        rejects([&] { capture_shader_source(root, "surface.shader.json", engine); });
        std::filesystem::remove(root / "Shaders/engine/math.hlsli");
#ifndef _WIN32
        std::filesystem::create_symlink(root / "Shaders/main.hlsl", root / "Shaders/alias.hlsl");
        rejects([&] { capture_shader_source(root, "surface.shader.json", engine); });
        std::filesystem::remove(root / "Shaders/alias.hlsl");
#endif
        const auto original_text = doc.dump();
        const auto original_bytes = std::as_bytes(std::span(original_text));
        require(read_bytes(root / "surface.shader.json", 1024 * 1024) ==
                    std::vector<std::byte>(original_bytes.begin(), original_bytes.end()),
                "Shader admission modified authored source");
        std::filesystem::remove_all(root);
        std::cout << "Shader snapshot, worker transport, cache and publication tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
