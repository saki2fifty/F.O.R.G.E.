#include "model_importer.hpp"
#include "model_bundle.hpp"
#include <algorithm>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool condition, const char* why) {
    if (!condition)
        throw std::runtime_error(why);
}
std::string extension(const std::filesystem::path& path) {
    auto result = path.extension().string();
    for (auto& c : result)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return result;
}
AssetImporterDescriptor descriptor() {
    AssetImporterDescriptor d;
    d.id = "forge.model.gltf";
    d.revision = model_recipe_revision();
    d.label = "glTF model";
    d.description = "Prepare a complete validated glTF model asset family in an isolated worker.";
    d.extensions = {".gltf", ".glb"};
    d.source_kinds = {"gltf", "glb"};
    d.output_types = {"model", "mesh", "material", "texture"};
    d.output_format = "forge.model-bundle";
    const auto limits = model_worker_limits();
    d.limits.memory_bytes = limits.memory_bytes;
    d.limits.output_bytes = 512ull * 1024 * 1024;
    d.limits.output_files = 4096;
    d.limits.seconds = limits.seconds;
    d.targets = {{"*", "d3d12", "desktop"}, {"*", "none", "cpu"}};
    return d;
}
class ModelImporter final : public AssetImporter {
    std::filesystem::path worker_;
    GltfSourceBundle capture(const AssetImportRequest& request, std::stop_token stop) const {
        const auto ext = extension(request.source);
        require(ext == ".gltf" || ext == ".glb", "Model source requires .gltf or .glb extension");
        require((request.target.backend == "none" && request.target.profile == "cpu") ||
                    (request.target.backend == "d3d12" && request.target.profile == "desktop"),
                "Unsupported model preparation target");
        return capture_gltf_source(request.project, request.source, model_cook_extensions(), {},
                                   stop);
    }
    AssetImportPlan plan(const AssetImportRequest& request, const GltfSourceBundle& source) const {
        AssetImportPlan result;
        auto& input = result.input;
        const auto& d = AssetImporter::descriptor();
        input.source_digest = source.source_digest;
        input.importer = d.id;
        input.importer_revision = d.revision;
        input.settings_version = settings().version();
        input.settings = settings().effective(request.settings);
        input.output_format = d.output_format;
        input.output_version = d.output_version;
        input.platform = request.target.platform;
        input.backend = request.target.backend;
        input.profile = request.target.profile;
        for (const auto& dep : source.dependencies) {
            const auto [at, added] =
                input.source_dependencies.emplace(path_utf8(dep.source), dep.revision);
            require(added || at->second == dep.revision,
                    "Conflicting captured source dependency revisions");
        }
        result.sources = source.dependencies;
        result.data = {{"version", 1}, {"binary", source.binary_container}};
        return result;
    }

  public:
    explicit ModelImporter(std::filesystem::path worker)
        : AssetImporter(asset_detail::descriptor(), model_settings()), worker_(std::move(worker)) {}
    ImportProbeResult probe(const ImportProbe& p) const override {
        const auto ext = extension(p.source);
        if (ext != ".gltf" && ext != ".glb")
            return {};
        if (p.prefix.size() >= 4 && p.prefix[0] == std::byte{'g'} &&
            p.prefix[1] == std::byte{'l'} && p.prefix[2] == std::byte{'T'} &&
            p.prefix[3] == std::byte{'F'})
            return {ImportProbeMatch::Strong, "glb",
                    "glTF binary signature; full admission follows"};
        const auto first = std::find_if(p.prefix.begin(), p.prefix.end(), [](std::byte c) {
            return c != std::byte{' '} && c != std::byte{'\n'} && c != std::byte{'\r'} &&
                   c != std::byte{'\t'};
        });
        if (ext == ".gltf" && first != p.prefix.end() && *first == std::byte{'{'})
            return {ImportProbeMatch::Possible, "gltf",
                    "JSON model candidate; full admission follows"};
        return {};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        return plan(request, capture(request, stop));
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& prepared,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto source = capture(request, stop);
        const auto current = plan(request, source);
        require(current.input.document() == prepared.input.document() &&
                    current.data == prepared.data,
                "Model source, dependencies or settings changed after discovery");
        if (progress)
            progress(.15, "Preparing model in isolated worker");
        ImportProcessRequest transport{{{"recipe", AssetImporter::descriptor().id},
                                        {"revision", model_recipe_revision()},
                                        {"settings", request.settings},
                                        {"source_digest", prepared.input.source_digest},
                                        {"backend", request.target.backend}},
                                       encode_gltf_snapshot(source, {}, stop)};
        // Release the captured shared storage before waiting for the native process.
        source = {};
        auto files = run_import_process(worker_, request.project, std::move(transport),
                                        model_worker_limits(), stop);
        validate({{}, Json::object(), files});
        if (progress)
            progress(1., "Model family validated");
        return files;
    }
    void validate(const CachedArtifact& candidate) const override {
        (void)validate_model_bundle(candidate.files);
    }
};
} // namespace
std::string model_recipe_revision() {
    return asset_build_digest({{"sources_toolchain", FORGE_MODEL_RECIPE_FINGERPRINT},
                               {"configuration", FORGE_MODEL_RECIPE_CONFIGURATION}});
}
WorkerLimits model_worker_limits() {
    WorkerLimits limits;
    limits.memory_bytes = 4ull * 1024 * 1024 * 1024;
    limits.file_bytes = 512ull * 1024 * 1024;
    // Aggregate includes captured inputs, cooked outputs and both manifests.
    limits.total_bytes = 2ull * 1024 * 1024 * 1024;
    limits.files = 16384;
    limits.seconds = 240;
    limits.cpu_seconds = 220;
    limits.cancellation_grace_ms = 250;
    return limits;
}
const std::set<std::string>& model_cook_extensions() {
    // Exact implemented CPU preservation/codec paths, not claims of GPU shading.
    static const std::set<std::string> extensions{
        "EXT_meshopt_compression",   "KHR_draco_mesh_compression",
        "KHR_mesh_quantization",     "KHR_texture_transform",
        "KHR_texture_basisu",        "EXT_texture_webp",
        "KHR_materials_unlit",       "KHR_materials_pbrSpecularGlossiness",
        "KHR_materials_clearcoat",   "KHR_materials_specular",
        "KHR_materials_sheen",       "KHR_materials_anisotropy",
        "KHR_materials_iridescence", "KHR_materials_transmission",
        "KHR_materials_volume",      "KHR_materials_ior",
        "KHR_materials_dispersion",  "KHR_materials_emissive_strength"};
    return extensions;
}
ImportSettingsSchema model_settings() {
    std::vector<ImportSettingRule> rules;
    auto choice = [&](std::string key, std::string label, std::string help, std::string value,
                      std::vector<std::string> choices) {
        ImportSettingRule r{std::move(key), std::move(label), std::move(help),
                            ImportSettingType::Choice, std::move(value)};
        r.choices = std::move(choices);
        rules.push_back(std::move(r));
    };
    choice("normals", "Normals",
           "Preserve, generate missing flat normals, or recalculate all normals.", "missing",
           {"preserve", "missing", "recalculate"});
    choice("tangents", "Tangents",
           "Generate tangent frames using each material's normal-map UV set.", "missing",
           {"preserve", "missing", "recalculate"});
    rules.push_back({"weld_exact", "Merge identical vertices",
                     "Merge only vertices whose complete streams and morph data match.",
                     ImportSettingType::Boolean, true});
    rules.push_back({"vertex_fetch", "Optimize vertex fetch",
                     "Reorder vertex storage without changing triangle order.",
                     ImportSettingType::Boolean, true});
    choice(
        "compression", "Image compression",
        "BC processing for PNG/JPEG/WebP; supplied Basis images use the target transcode profile.",
        "none", {"none", "bc", "bc-high-quality"});
    ImportSettingRule maximum{"max_texture_size", "Maximum texture dimension",
                              "Select or generate a mip that fits the dimension limit.",
                              ImportSettingType::Integer, 16384};
    maximum.minimum = 1;
    maximum.maximum = 16384;
    rules.push_back(maximum);
    return ImportSettingsSchema("forge.model.gltf", 1, std::move(rules));
}
std::shared_ptr<const AssetImporter> model_importer(std::filesystem::path worker) {
    return std::make_shared<ModelImporter>(std::move(worker));
}
} // namespace forge::asset_detail
