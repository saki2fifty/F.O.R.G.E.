#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include "gallery_asset_publication.hpp"
#include "material_lightmap_fixture.hpp"
#include "runtime_package.hpp"
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
#include "ui_effect_fixture.hpp"
#include <forge/material_graph.hpp>
#include <forge/shader_resource.hpp>
#include <forge/surface_shader.hpp>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
using Json = nlohmann::json;
namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(bool(out.write(text.data(), std::streamsize(text.size()))) && bool(out.flush()),
            "Shader worker fixture write failed");
}
AssetImportOutcome finish(AssetImportService& service) {
    require(service.wait_idle(
                std::chrono::seconds(forge::asset_detail::shader_worker_limits().seconds + 5)),
            "Shader worker did not finish within its limit");
    auto results = service.poll();
    require(results.size() == 1, "Shader completion count mismatch");
    return std::move(results.front());
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool mesh_only = argc == 4 && std::string_view(argv[3]) == "mesh-graph";
        require(argc == 3 || mesh_only,
                "Shader worker test needs worker, directory and optional mesh-graph");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Shaders");
        const auto id = AssetId::generate();
        Json document{
            {"format", "forge.shader"},
            {"version", 1},
            {"asset_id", id},
            {"source_root", "Shaders"},
            {"stages",
             Json::array({{{"stage", "vertex"}, {"source", "main.hlsl"}, {"entry", "vs"}},
                          {{"stage", "pixel"}, {"source", "main.hlsl"}, {"entry", "ps"}}})}};
        if (mesh_only)
            document = MaterialGraphSource::create(id).document;
        const auto authored_source = document.dump();
        write(root / "surface.shader.json", authored_source);
        write(root / "Shaders/main.hlsl", R"(
#include "color.hlsli"
float4 vs(uint id:SV_VertexID):SV_POSITION {return float4(id,0,0,1);}
float4 ps():SV_TARGET {return color();}
)");
        const std::string good = "float4 color(){return float4(1,.25,0,1);}\n";
        write(root / "Shaders/color.hlsli", good);
        auto lease = std::make_shared<ProjectLease>(root);
        // Authored Shader documents already own an AssetId before compilation.
        // Importing/copying a document never silently invents a replacement ID.
        AssetCatalog catalog(root);
        catalog.add({id, "shader", "surface.shader.json", 1, {}});
        catalog.save(AssetCatalog::project_index(root));
        const ShaderCompilerProfile compiler{diligent_shader_compiler_digest(),
                                             diligent_shader_compiler_debug()};
        std::cout << "Shader compiler digest: " << compiler.digest << "; debug=" << compiler.debug
                  << std::endl;
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(shader_importer(std::filesystem::absolute(argv[1]), compiler));
        registry->seal();
        AssetImportService service(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"}, 1);
        const auto prepare = [](AssetPublicationCandidate& c, const AssetImportPlan& p,
                                const AssetCatalog&) { prepare_shader_publication(c, p); };
        std::string active_layout;
        const auto compatible = [&](const AssetCatalog&, const CachedArtifact& artifact) {
            const auto shader = decode_shader(artifact.files.front().bytes);
            if (!active_layout.empty())
                require(shader.layout_digest() == active_layout,
                        "Fixture material binding layout changed");
        };
        auto submit = [&] {
            return service.submit(service.prepare("surface.shader.json"), prepare, compatible);
        };
        const auto initial_started = std::chrono::steady_clock::now();
        submit();
        auto outcome = finish(service);
        const auto initial_finished = std::chrono::steady_clock::now();
        if (mesh_only)
            std::cout << "Default Mesh graph worker wall seconds: "
                      << std::chrono::duration<double>(initial_finished - initial_started).count()
                      << "; published=" << outcome.published
                      << "; diagnostic=" << outcome.diagnostic << std::endl;
        require(outcome.published && !outcome.cache_hit,
                "Initial shader worker import failed: " + outcome.diagnostic);
        catalog = AssetCatalog::open_project(root);
        active_layout = catalog.records().at(id).metadata.at("forge.shader").at("layout");
        ResourcePool<ShaderAsset> resources;
        const AssetRef<ShaderAsset> reference{id};
        const auto first = request_shader(resources, root, catalog, reference);
        require(resources.wait(first, 5s), "Compiled shader resource could not load");
        const auto old = resources.acquire(first);
        if (mesh_only) {
            const auto authored_bytes = std::as_bytes(std::span(authored_source));
            require(
                old && old->surface && old->surface->physically_based &&
                    old->stages.size() ==
                        surface_shader_roles(*old->surface, current_surface_wrapper_version).size(),
                "Default Mesh graph worker lost its complete native output family");
            require(read_bytes(root / "surface.shader.json", 1024 * 1024) ==
                        std::vector<std::byte>(authored_bytes.begin(), authored_bytes.end()),
                    "Default Mesh graph worker changed its authored source");
            require(std::filesystem::is_empty(root / ".forge/jobs"),
                    "Default Mesh graph worker leaked per-job staging");
            return 0;
        }
        require(old && old->stages.size() == 2, "Compiled shader resource lost stages");
        const auto content_path =
            root.parent_path() / ("shader-content-" + AssetId::generate().str());
        const std::array content_roots{id};
        (void)package_runtime_content(root, content_path, content_roots, {"windows", "d3d12"});
        const auto relocated_content = content_path.string() + "-relocated";
        std::filesystem::rename(content_path, relocated_content);
        const auto content_catalog = open_runtime_content(relocated_content, {"windows", "d3d12"});
        ResourcePool<ShaderAsset> packaged_resources;
        const auto packaged_ticket =
            request_shader(packaged_resources, relocated_content, content_catalog, reference);
        require(packaged_resources.wait(packaged_ticket, 5s) &&
                    packaged_resources.acquire(packaged_ticket)->layout_digest() ==
                        old->layout_digest() &&
                    !std::filesystem::exists(std::filesystem::path(relocated_content) / "Shaders"),
                "Relocated compiled shader package required HLSL sources or lost reflection");
        std::filesystem::remove_all(relocated_content);
        const auto first_key = catalog.records().at(id).metadata.at("forge.import").at("key");
        const auto cached_started = std::chrono::steady_clock::now();
        submit();
        outcome = finish(service);
        const auto cached_finished = std::chrono::steady_clock::now();
        require(outcome.published && outcome.cache_hit,
                "Shader DDC reuse failed: " + outcome.diagnostic);
        const auto baseline = read_bytes(AssetCatalog::project_index(root), max_asset_index_bytes);
        write(root / "Shaders/color.hlsli", "not valid HLSL @");
        submit();
        outcome = finish(service);
        require(!outcome.published &&
                    outcome.diagnostic.find("shader.compile.failed") != std::string::npos,
                "Invalid shader source was not rejected with worker diagnostics");
        require(read_bytes(AssetCatalog::project_index(root), max_asset_index_bytes) == baseline,
                "Failed shader compilation replaced last-good catalog");
        write(root / "Shaders/color.hlsli", "#include \"color.hlsli\"\n");
        submit();
        outcome = finish(service);
        require(!outcome.published, "Recursive shader include was admitted");
        write(root / "Shaders/color.hlsli",
              "cbuffer Changed {float4 Value;};float4 color(){return Value;}\n");
        submit();
        outcome = finish(service);
        require(!outcome.published &&
                    outcome.diagnostic.find("layout changed") != std::string::npos,
                "Incompatible shader layout was published");
        require(read_bytes(AssetCatalog::project_index(root), max_asset_index_bytes) == baseline,
                "Layout rejection replaced previous shader");
        write(root / "Shaders/color.hlsli", good);
        submit();
        require(service.wait_idle(
                    std::chrono::seconds(forge::asset_detail::shader_worker_limits().seconds + 5)),
                "Shader stale test stalled");
        write(root / "Shaders/color.hlsli", good + "// stale during publication\n");
        outcome = finish(service);
        require(!outcome.published, "Shader published after include changed");
        write(root / "Shaders/color.hlsli", good);
        auto job = submit();
        require(service.wait_idle(
                    std::chrono::seconds(forge::asset_detail::shader_worker_limits().seconds + 5)),
                "Shader cancel test stalled");
        service.cancel(job);
        outcome = finish(service);
        require(!outcome.published && outcome.job.state == AssetJobState::Cancelled,
                "Cancelled shader candidate was published");
        write(root / "Shaders/color.hlsli", "float4 color(){return float4(0,.25,1,1);}\n");
        submit();
        outcome = finish(service);
        require(outcome.published && !outcome.cache_hit,
                "Valid shader replacement failed: " + outcome.diagnostic);
        catalog = AssetCatalog::open_project(root);
        const auto replacement = request_shader(resources, root, catalog, reference);
        require(resources.wait(replacement, 5s) &&
                    resources.acquire(replacement)->build_key != old->build_key,
                "Cooked shader replacement lost old lease or new code");
        require(catalog.records().at(id).metadata.at("forge.import").at("key") != first_key &&
                    catalog.dependency_graph().invalidated_by_source("Shaders/color.hlsli") ==
                        std::vector<AssetId>{id},
                "Shader replacement lost identity or include dependency");
        require(read_bytes(root / "surface.shader.json", 1024 * 1024).size() ==
                    document.dump().size(),
                "Shader import rewrote authored document");
        require(std::filesystem::is_empty(root / ".forge/jobs"),
                "Shader worker leaked per-job staging");
        // Actual Sprite graph worker cook, publication, cached reuse and a
        // source-free relocated Shader package. This executes only on Windows.
        const auto sprite = MaterialGraphSource::create_sprite(AssetId::generate());
        write(root / "sprite.shader.json", sprite.document.dump(2));
        catalog.add({sprite.asset(), "shader", "sprite.shader.json", 1, {}});
        catalog.save(AssetCatalog::project_index(root));
        const auto no_live_consumers = [](const auto&, const auto&) {};
        const auto submit_sprite = [&] {
            service.submit(service.prepare("sprite.shader.json"), prepare, no_live_consumers);
        };
        submit_sprite();
        const auto sprite_outcome = finish(service);
        require(sprite_outcome.published && !sprite_outcome.cache_hit,
                "Sprite graph worker compilation failed: " + sprite_outcome.diagnostic);
        catalog = AssetCatalog::open_project(root);
        const auto sprite_ticket = request_shader(resources, root, catalog, {sprite.asset()});
        require(resources.wait(sprite_ticket, 5s), "Sprite graph Shader resource did not load");
        const auto sprite_lease = resources.acquire(sprite_ticket);
        require(sprite_lease && sprite_lease->surface && sprite_lease->surface->sprite_image &&
                    sprite_lease->stages.size() == 2 &&
                    sprite_lease->surface_wrapper_version == current_surface_wrapper_version,
                "Sprite worker publication lost its typed domain or mip entry");
        submit_sprite();
        const auto sprite_cached = finish(service);
        require(sprite_cached.published && sprite_cached.cache_hit,
                "Sprite worker did not reuse its exact immutable source recipe");
        const auto sprite_package =
            root.parent_path() / ("sprite-content-" + AssetId::generate().str());
        const std::array sprite_roots{sprite.asset()};
        (void)package_runtime_content(root, sprite_package, sprite_roots, {"windows", "d3d12"});
        const auto relocated_sprite = sprite_package.string() + "-relocated";
        std::filesystem::rename(sprite_package, relocated_sprite);
        const auto sprite_catalog = open_runtime_content(relocated_sprite, {"windows", "d3d12"});
        ResourcePool<ShaderAsset> sprite_resources;
        const auto packaged_sprite =
            request_shader(sprite_resources, relocated_sprite, sprite_catalog, {sprite.asset()});
        require(sprite_resources.wait(packaged_sprite, 5s) &&
                    encode_shader(sprite_resources.acquire(packaged_sprite).get()) ==
                        encode_shader(sprite_lease.get()) &&
                    !std::filesystem::exists(std::filesystem::path(relocated_sprite) /
                                             "sprite.shader.json"),
                "Relocated Sprite Shader lost its exact cooked program or retained source");
        std::filesystem::remove_all(relocated_sprite);
        // Real FXC worker output and source-free package admission for the
        // imported-irradiance graph. CPU marker fixtures do not enter this path.
        auto lightmap = MaterialGraphSource::create(AssetId::generate());
        lightmap.document["graph"] = lightmap_test::graph();
        write(root / "lightmap.shader.json", lightmap.document.dump(2));
        catalog.add({lightmap.asset(), "shader", "lightmap.shader.json", 1, {}});
        catalog.save(AssetCatalog::project_index(root));
        test::publish_gallery_asset(service, "lightmap.shader.json", prepare_shader_publication,
                                    std::chrono::seconds(shader_worker_limits().seconds + 15));
        catalog = AssetCatalog::open_project(root);
        require(catalog.records()
                        .at(lightmap.asset())
                        .metadata.at("forge.shader")
                        .at("surface_wrapper_version") == current_surface_wrapper_version,
                "Lightmap publication omitted its runtime surface capability");
        const AssetRef<ShaderAsset> lightmap_ref{lightmap.asset()};
        const auto lightmap_ticket = request_shader(resources, root, catalog, lightmap_ref);
        require(resources.wait(lightmap_ticket, 5s), "Compiled lightmap graph did not load");
        const auto lightmap_program = resources.acquire(lightmap_ticket);
        require(lightmap_program && lightmap_program->surface &&
                    lightmap_program->surface->textures.at("lightmap").semantic ==
                        TextureSemantic::HdrColor &&
                    lightmap_program->surface->uv_sets == std::vector<unsigned>{3} &&
                    lightmap_program->surface_wrapper_version == current_surface_wrapper_version,
                "Compiled lightmap lost its HDR, sparse UV or surface profile");
        unsigned lightmap_generation = 1;
        for (const auto& value : {Json(), Json(14), Json("15")}) {
            auto changed = catalog;
            auto record = changed.records().at(lightmap.asset());
            record.metadata["forge.import"]["generation"] = ++lightmap_generation;
            if (value.is_null())
                record.metadata["forge.shader"].erase("surface_wrapper_version");
            else
                record.metadata["forge.shader"]["surface_wrapper_version"] = value;
            changed.replace(record);
            const auto failure = request_shader(resources, root, changed, lightmap_ref);
            require(!resources.wait(failure, 5s) &&
                        resources.current(lightmap_ref).identity() == lightmap_program.identity(),
                    "Altered lightmap surface capability replaced the last-good native program");
        }
        const auto lightmap_package =
            root.parent_path() / ("lightmap-content-" + AssetId::generate().str());
        const std::array lightmap_roots{lightmap.asset()};
        (void)package_runtime_content(root, lightmap_package, lightmap_roots, {"windows", "d3d12"});
        const auto relocated_lightmap = lightmap_package.string() + "-relocated";
        std::filesystem::rename(lightmap_package, relocated_lightmap);
        const auto lightmap_catalog =
            open_runtime_content(relocated_lightmap, {"windows", "d3d12"});
        ResourcePool<ShaderAsset> lightmap_resources;
        const auto packaged_lightmap =
            request_shader(lightmap_resources, relocated_lightmap, lightmap_catalog, lightmap_ref);
        require(lightmap_resources.wait(packaged_lightmap, 5s) &&
                    encode_shader(lightmap_resources.acquire(packaged_lightmap).get()) ==
                        encode_shader(lightmap_program.get()) &&
                    !std::filesystem::exists(std::filesystem::path(relocated_lightmap) /
                                             "lightmap.shader.json"),
                "Relocated lightmap Shader lost its cooked program or retained graph source");
        std::filesystem::remove_all(relocated_lightmap);
        // One project-authored UI Pixel entry, compiled by the same supervised
        // worker and loaded again without its descriptor or HLSL source.
        const auto ui = AssetId::generate();
        const auto ui_definition = test::ui_effect_definition();
        write(root / "ui.shader.json", test::ui_effect_document(ui).dump());
        write(root / "Shaders/ui.hlsl", test::ui_effect_source);
        catalog.add({ui, "shader", "ui.shader.json", 1, {}});
        catalog.save(AssetCatalog::project_index(root));
        service.submit(service.prepare("ui.shader.json"), prepare, no_live_consumers);
        const auto ui_outcome = finish(service);
        require(ui_outcome.published && !ui_outcome.cache_hit,
                "UI effect worker compilation failed: " + ui_outcome.diagnostic);
        catalog = AssetCatalog::open_project(root);
        const auto ui_ticket = request_shader(resources, root, catalog, {ui});
        require(resources.wait(ui_ticket, 5s), "Compiled UI effect did not load");
        const auto ui_program = resources.acquire(ui_ticket);
        require(ui_program && ui_program->surface == ui_definition &&
                    ui_program->surface_wrapper_version == current_surface_wrapper_version &&
                    ui_program->stages.size() == 1 &&
                    ui_program->stages.front().stage == ShaderStage::Pixel &&
                    ui_program->stages.front().role == ShaderEntryRole::SurfaceUiColor,
                "UI worker publication lost its domain, capability or single Pixel entry");
        const auto ui_layout = surface_binding_layout(
            ui_definition, ui_program->stages.front().reflection, ShaderEntryRole::SurfaceUiColor,
            current_surface_wrapper_version);
        require(ui_layout.settings && ui_layout.parameter_bytes &&
                    ui_layout.parameters.size() == ui_definition.parameters.size(),
                "Real UI reflection lost element state or Material parameters");
        service.submit(service.prepare("ui.shader.json"), prepare, no_live_consumers);
        const auto ui_cached = finish(service);
        require(ui_cached.published && ui_cached.cache_hit,
                "UI effect did not reuse its immutable compilation recipe");
        const auto ui_package = root.parent_path() / ("ui-content-" + AssetId::generate().str());
        const std::array ui_roots{ui};
        (void)package_runtime_content(root, ui_package, ui_roots, {"windows", "d3d12"});
        const auto relocated_ui = ui_package.string() + "-relocated";
        std::filesystem::rename(ui_package, relocated_ui);
        const auto ui_catalog = open_runtime_content(relocated_ui, {"windows", "d3d12"});
        ResourcePool<ShaderAsset> ui_resources;
        const auto packaged_ui = request_shader(ui_resources, relocated_ui, ui_catalog, {ui});
        require(
            ui_resources.wait(packaged_ui, 5s) &&
                encode_shader(ui_resources.acquire(packaged_ui).get()) ==
                    encode_shader(ui_program.get()) &&
                !std::filesystem::exists(std::filesystem::path(relocated_ui) / "Shaders") &&
                !std::filesystem::exists(std::filesystem::path(relocated_ui) / "ui.shader.json"),
            "Relocated UI effect lost cooked bytes or retained project shader sources");
        std::filesystem::remove_all(relocated_ui);
        const auto milliseconds = [](auto duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        const Json timings{
            {"scope",
             "End-to-end prepare, worker/cache validation and publication; not compiler-only"},
            {"profile", "windows-x64/d3d12/fxc-5.1"},
            {"stages", 2},
            {"initial_ms", milliseconds(initial_finished - initial_started)},
            {"cached_ms", milliseconds(cached_finished - cached_started)}};
        std::cout << timings.dump() << '\n';
        std::cout << "Shader worker compilation/cache/publication/rejection tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
