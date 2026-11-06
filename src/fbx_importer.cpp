#include "fbx_importer.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "model_bundle.hpp"
#include <algorithm>
#include <cstring>
#include <forge/project_paths.hpp>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t file_limit = 512ull * 1024 * 1024;
constexpr std::size_t total_limit = 1024ull * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
ArtifactFile json_file(std::string name, const Json& value) {
    const auto text = value.dump();
    require(text.size() <= 4 * 1024 * 1024, "FBX snapshot description exceeds bounds");
    const auto bytes = std::as_bytes(std::span(text));
    return {std::move(name), {bytes.begin(), bytes.end()}};
}
AssetImporterDescriptor descriptor() {
    AssetImporterDescriptor d;
    d.id = fbx_importer_id;
    d.revision = fbx_recipe_revision();
    d.label = "FBX model";
    d.description = "Prepare a bounded FBX7 model family using a supervised native worker.";
    d.extensions = {".fbx"};
    d.source_kinds = {"fbx"};
    d.output_types = {"model",    "mesh",           "material",  "texture",
                      "skeleton", "animation_clip", "model_node"};
    d.output_format = "forge.model-bundle";
    d.output_version = 5;
    const auto limits = model_worker_limits();
    d.limits = {limits.memory_bytes, 512ull * 1024 * 1024, limits.seconds, 4096};
    d.targets = {{"*", "d3d12", "desktop"}, {"*", "none", "cpu"}};
    return d;
}
struct Captured {
    std::string digest;
    Json description;
    std::vector<AssetSourceDependency> dependencies;
    std::vector<ArtifactFile> files;
};
Captured capture_fbx_source(const std::filesystem::path& worker, const AssetImportRequest& request,
                            std::stop_token stop) {
    require(!stop.stop_requested(), "FBX capture cancelled");
    ProjectPaths paths(request.project);
    const auto source = ProjectPaths::normalize(request.source);
    auto bytes = read_bytes(paths.resolve(source), file_limit);
    Captured result;
    result.digest = content_digest(bytes);
    ImportProcessRequest inspect{{{"recipe", fbx_importer_id},
                                  {"revision", fbx_recipe_revision()},
                                  {"phase", "inspect"},
                                  {"source_digest", result.digest}},
                                 {{"source.fbx", bytes}}};
    auto files = run_import_process(worker, request.project, std::move(inspect),
                                    model_worker_limits(), stop);
    require(files.size() == 1 && files[0].name == "inspection.json",
            "FBX worker omitted source inspection");
    result.description = parse_bounded_json(files[0].bytes, 4 * 1024 * 1024);
    require(result.description.at("version") == 1 &&
                result.description.at("parser_revision") == fbx_parser_revision,
            "FBX inspection profile differs from the selected parser");
    std::size_t total = bytes.size();
    result.files.push_back({"source.fbx", std::move(bytes)});
    Json captured = Json::array();
    std::map<std::filesystem::path, std::pair<std::string, std::string>, ProjectLocatorLess> seen;
    const auto& images = result.description.at("images");
    require(images.is_array() && images.size() <= 4096, "FBX image count exceeds bounds");
    for (const auto& image : images) {
        require(!stop.stop_requested(), "FBX image capture cancelled");
        const auto key = image.at("key").get<std::string>();
        const auto relative = image.at("path").get<std::string>();
        require(key.size() <= 1024 && key.starts_with("fbx:image:") && !relative.empty() &&
                    relative.size() <= 4096 && relative.front() != '/' &&
                    relative.find(':') == std::string::npos &&
                    relative.find('\\') == std::string::npos &&
                    relative.find('\0') == std::string::npos,
                "FBX dependency must be a bounded relative image locator");
        const auto locator =
            paths.relative(paths.resolve(source.parent_path() / std::filesystem::u8path(relative)));
        auto found = seen.find(locator);
        if (found == seen.end()) {
            auto data =
                read_bytes(paths.resolve(locator), std::min(file_limit, total_limit - total));
            total += data.size();
            const auto name = "image-" + std::to_string(seen.size()) + ".bin";
            const auto digest = content_digest(data);
            found = seen.emplace(locator, std::pair{name, digest}).first;
            result.files.push_back({name, std::move(data)});
        }
        captured.push_back(
            {{"key", key}, {"file", found->second.first}, {"sha256", found->second.second}});
        result.dependencies.push_back({locator, "image:" + key, found->second.second});
    }
    result.files.push_back(json_file("images.json", captured));
    return result;
}
class FbxImporter final : public AssetImporter {
    std::filesystem::path worker_, converter_;
    AssetImportPlan plan(const AssetImportRequest& request, const Captured& source) const {
        auto result =
            make_model_import_plan(request, *this, source.digest, source.dependencies,
                                   source.description.at("animation").get<bool>(), converter_);
        result.input.tool_revisions.emplace("ufbx", fbx_parser_tool_revision());
        result.data["inspection"] = source.description;
        return result;
    }

  public:
    FbxImporter(std::filesystem::path worker, std::filesystem::path converter)
        : AssetImporter(asset_detail::descriptor(), model_settings(fbx_importer_id)),
          worker_(std::move(worker)), converter_(std::move(converter)) {
        if (converter_.empty())
            converter_ =
                worker_.parent_path() / "tools" / ("gltf2ozz" + worker_.extension().string());
    }
    ImportProbeResult probe(const ImportProbe& p) const override {
        auto ext = p.source.extension().string();
        for (auto& c : ext)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        if (ext != ".fbx")
            return {};
        const std::string_view prefix(reinterpret_cast<const char*>(p.prefix.data()),
                                      p.prefix.size());
        if (prefix.starts_with(std::string_view("Kaydara FBX Binary  \0\x1a\0", 23)))
            return {ImportProbeMatch::Strong, "fbx",
                    "Binary FBX candidate; supervised admission follows"};
        if (prefix.find("FBXHeaderExtension:") != std::string_view::npos ||
            prefix.find("FBXVersion:") != std::string_view::npos)
            return {ImportProbeMatch::Possible, "fbx",
                    "ASCII FBX candidate; supervised admission follows"};
        return {};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        return plan(request, capture_fbx_source(worker_, request, stop));
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& prepared,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto source = capture_fbx_source(worker_, request, stop);
        const auto current = plan(request, source);
        require(current.input.document() == prepared.input.document() &&
                    current.data == prepared.data,
                "FBX source, dependencies, tools or settings changed after discovery");
        if (progress)
            progress(.2, "Preparing FBX model in isolated worker");
        ImportProcessRequest transport{{{"recipe", fbx_importer_id},
                                        {"revision", fbx_recipe_revision()},
                                        {"phase", "cook"},
                                        {"settings", request.settings},
                                        {"source_digest", source.digest},
                                        {"backend", request.target.backend}},
                                       std::move(source.files)};
        auto files = run_import_process(worker_, request.project, std::move(transport),
                                        model_worker_limits(), stop);
        files = finish_model_recipe(std::move(files), converter_, request.project, source.digest,
                                    prepared.data.at("converter_sha256").get<std::string>(), stop);
        validate({{}, Json::object(), files});
        if (progress)
            progress(1., "FBX model family validated");
        return files;
    }
    void validate(const CachedArtifact& candidate) const override {
        const auto index = validate_model_bundle(candidate.files);
        require(index.evidence_schema == fbx_evidence_schema,
                "FBX candidate has a different source identity profile");
    }
};
} // namespace
std::string fbx_recipe_revision() {
    return asset_build_digest({{"sources_toolchain", FORGE_FBX_RECIPE_FINGERPRINT},
                               {"configuration", FORGE_FBX_RECIPE_CONFIGURATION}});
}
std::shared_ptr<const AssetImporter> fbx_importer(std::filesystem::path worker,
                                                  std::filesystem::path converter) {
    return std::make_shared<FbxImporter>(std::move(worker), std::move(converter));
}
std::vector<AssetSourceDependency> fbx_source_dependencies(const std::filesystem::path& worker,
                                                           const std::filesystem::path& source,
                                                           std::stop_token stop) {
    AssetImportRequest request;
    const auto absolute = std::filesystem::absolute(source).lexically_normal();
    request.project = absolute.parent_path();
    request.source = absolute.filename();
    auto captured = capture_fbx_source(worker, request, stop);
    captured.dependencies.push_back({request.source, "source", captured.digest});
    return std::move(captured.dependencies);
}
} // namespace forge::asset_detail
