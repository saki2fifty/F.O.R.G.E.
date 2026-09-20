#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
#include <fstream>
#include <iostream>
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
    require(service.wait_idle(65s), "Shader worker did not finish within its limit");
    auto results = service.poll();
    require(results.size() == 1, "Shader completion count mismatch");
    return std::move(results.front());
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Shader worker test needs worker and test directory");
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
        write(root / "surface.shader.json", document.dump());
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
        submit();
        auto outcome = finish(service);
        require(outcome.published && !outcome.cache_hit,
                "Initial shader worker import failed: " + outcome.diagnostic);
        catalog = AssetCatalog::open_project(root);
        active_layout = catalog.records().at(id).metadata.at("forge.shader").at("layout");
        const auto first_key = catalog.records().at(id).metadata.at("forge.import").at("key");
        submit();
        outcome = finish(service);
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
        require(service.wait_idle(65s), "Shader stale test stalled");
        write(root / "Shaders/color.hlsli", good + "// stale during publication\n");
        outcome = finish(service);
        require(!outcome.published, "Shader published after include changed");
        write(root / "Shaders/color.hlsli", good);
        auto job = submit();
        require(service.wait_idle(65s), "Shader cancel test stalled");
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
        require(catalog.records().at(id).metadata.at("forge.import").at("key") != first_key &&
                    catalog.dependency_graph().invalidated_by_source("Shaders/color.hlsli") ==
                        std::vector<AssetId>{id},
                "Shader replacement lost identity or include dependency");
        require(read_bytes(root / "surface.shader.json", 1024 * 1024).size() ==
                    document.dump().size(),
                "Shader import rewrote authored document");
        require(std::filesystem::is_empty(root / ".forge/jobs"),
                "Shader worker leaked per-job staging");
        std::cout << "Shader worker compilation/cache/publication/rejection tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
