#include "shader_pipeline.hpp"
#include <algorithm>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
AssetImporterDescriptor descriptor(const ShaderCompilerProfile& compiler,
                                   const ShaderSources& engine) {
    AssetImporterDescriptor d;
    d.id = "forge.shader.diligent";
    d.revision = shader_import_revision(compiler, engine);
    d.label = "D3D12 shader";
    d.description = "Compile a captured HLSL program in the isolated shader worker.";
    d.extensions = {".json"};
    d.source_kinds = {"forge.shader"};
    d.output_types = {"shader"};
    d.output_format = "forge.shader.dxbc";
    d.targets = {{"windows-x64", "d3d12", "fxc-5.1"}};
    d.limits.memory_bytes = shader_worker_limits().memory_bytes;
    d.limits.output_bytes = shader_worker_limits().total_bytes;
    d.limits.seconds = shader_worker_limits().seconds;
    d.limits.output_files = 2;
    return d;
}
ImportSettingsSchema schema() {
    ImportSettingRule rule{"permutation", "Permutation",
                           "Select one declared value per axis; only this permutation is built.",
                           ImportSettingType::StringMap, Json::object()};
    rule.max_entries = 16;
    rule.max_length = 255;
    return {"forge.shader.diligent", 1, {rule}};
}
class ShaderImporter final : public AssetImporter {
    std::filesystem::path worker_;
    const ShaderCompilerProfile compiler_;
    const ShaderSources engine_;
    auto selection(const AssetImportRequest& request) const {
        return settings()
            .effective(request.settings)
            .at("permutation")
            .get<std::map<std::string, std::string>>();
    }
    ShaderSnapshot snapshot(const AssetImportRequest& request, std::stop_token stop) const {
        const auto source = capture_shader_source(request.project, request.source, engine_, stop);
        require(source.document.at("asset_id").get<AssetId>() == request.asset,
                "Shader document AssetId differs from its catalog identity");
        return source;
    }

  public:
    ShaderImporter(std::filesystem::path worker, ShaderCompilerProfile compiler,
                   ShaderSources engine)
        : AssetImporter(asset_detail::descriptor(compiler, engine), schema()),
          worker_(std::move(worker)), compiler_(std::move(compiler)), engine_(std::move(engine)) {
        require(valid_content_digest(compiler_.digest), "Missing shader compiler identity");
    }
    ImportProbeResult probe(const ImportProbe& probe) const override {
        // An incomplete 64KiB prefix is not a parsed source document. Full bounded
        // validation is discovery work, never run during Content's prefix probe.
        if (path_utf8(probe.source).ends_with(".shader.json"))
            return {ImportProbeMatch::Possible, "forge.shader", "Shader asset document suffix"};
        return {};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(request.target == AssetImporter::descriptor().targets.front(),
                "Shader importer requires Windows x64 / D3D12 / FXC5.1");
        const auto source = snapshot(request, stop);
        const auto selected = selection(request);
        AssetImportPlan result;
        result.input = shader_import_input(source, selected, compiler_);
        for (const auto& [path, digest] : result.input.source_dependencies)
            result.sources.push_back({std::filesystem::u8path(path), "shader-source", digest});
        result.data = {
            {"compiler_input_key", shader_build_input(source.program, source.sources, selected,
                                                      compiler_.digest, compiler_.debug)
                                       .key()},
            {"asset_id", request.asset}};
        return result;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        const auto source = snapshot(request, stop);
        const auto selected = selection(request);
        require(shader_import_input(source, selected, compiler_).document() ==
                    plan.input.document(),
                "Shader source/settings changed after discovery");
        if (progress)
            progress(.15, "Compiling shader in isolated worker");
        auto files = run_import_process(worker_, request.project,
                                        shader_process_request(source, selected, compiler_),
                                        shader_worker_limits(), stop);
        validate({{}, Json::object(), files});
        const auto data = decode_shader(files.front().bytes);
        require(data.build_key == plan.data.at("compiler_input_key").get<std::string>(),
                "Shader worker returned another compiler candidate");
        if (progress)
            progress(1., "Shader candidate validated");
        return files;
    }
    void validate(const CachedArtifact& candidate) const override {
        require(candidate.files.size() == 1 && candidate.files.front().name == "program.shader",
                "Invalid shader artifact file set");
        const auto data = decode_shader(candidate.files.front().bytes);
        require(data.compiler_digest == compiler_.digest && data.compiler_debug == compiler_.debug,
                "Cached shader belongs to another compiler profile");
    }
};
} // namespace
std::shared_ptr<const AssetImporter> shader_importer(std::filesystem::path worker,
                                                     ShaderCompilerProfile compiler,
                                                     ShaderSources engine) {
    return std::make_shared<ShaderImporter>(std::move(worker), std::move(compiler),
                                            std::move(engine));
}
} // namespace forge::asset_detail
