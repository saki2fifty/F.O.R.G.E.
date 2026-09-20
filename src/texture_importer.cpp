#include "texture_importer.hpp"
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/texture_asset.hpp>
#include <forge/texture_bundle.hpp>

namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t source_limit = 256 * 1024 * 1024;
std::string importer_id(bool container) {
    return container ? "forge.texture.container" : "forge.texture.image";
}
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string extension(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return ext;
}
ImportSettingRule choice(std::string key, std::string label, std::string help, std::string value,
                         std::vector<std::string> choices) {
    ImportSettingRule rule{std::move(key), std::move(label), std::move(help),
                           ImportSettingType::Choice, std::move(value)};
    rule.choices = std::move(choices);
    return rule;
}
AssetImporterDescriptor descriptor(bool container) {
    AssetImporterDescriptor d;
    d.id = importer_id(container);
    d.revision = texture_recipe_revision();
    d.label = container ? "Texture container" : "Image texture";
    d.description =
        container ? "Preserve admitted DDS/KTX mip chains and dimensions."
                  : "Prepare bounded image pixels, semantic mips and optional BC compression.";
    d.extensions = container ? std::vector<std::string>{".dds", ".ktx", ".ktx2"}
                             : std::vector<std::string>{".png", ".jpg", ".jpeg", ".tga",
                                                        ".bmp", ".hdr", ".rgbe", ".webp"};
    d.source_kinds = container ? std::vector<std::string>{"dds", "ktx", "ktx2"}
                               : std::vector<std::string>{"image", "hdr"};
    d.output_types = {"texture"};
    d.output_format = "forge.texture-bundle";
    d.limits.output_files = 16;
    d.targets = {{"*", "d3d12", "desktop"}, {"*", "none", "cpu"}};
    return d;
}
class TextureImporter final : public AssetImporter {
    std::filesystem::path worker_;
    bool container_;

  public:
    TextureImporter(std::filesystem::path worker, bool container)
        : AssetImporter(asset_detail::descriptor(container), texture_settings(container)),
          worker_(std::move(worker)), container_(container) {}
    ImportProbeResult probe(const ImportProbe& p) const override {
        const auto ext = extension(p.source);
        auto magic = [&](std::string_view value) {
            return p.prefix.size() >= value.size() &&
                   std::equal(value.begin(), value.end(), p.prefix.begin(),
                              [](char a, std::byte b) {
                                  return static_cast<unsigned char>(a) ==
                                         std::to_integer<unsigned char>(b);
                              });
        };
        if (container_) {
            if (magic("DDS "))
                return {ImportProbeMatch::Strong, "dds", "DDS container signature"};
            if (magic(std::string_view("\xABKTX 11\xBB\r\n\x1A\n", 12)))
                return {ImportProbeMatch::Strong, "ktx", "KTX1 container signature"};
            if (magic(std::string_view("\xABKTX 20\xBB\r\n\x1A\n", 12)))
                return {ImportProbeMatch::Strong, "ktx2", "KTX2 container signature"};
            return {};
        }
        if (magic("#?RADIANCE\n") || magic("#?RGBE\n"))
            return {ImportProbeMatch::Strong, "hdr", "Radiance RGBE signature"};
        if (magic(std::string_view("\x89PNG\r\n\x1A\n", 8)) ||
            magic(std::string_view("\xff\xd8", 2)) || magic("BM"))
            return {ImportProbeMatch::Strong, "image",
                    "Recognized image signature; decoding still requires admission"};
        if (magic("RIFF") && p.prefix.size() >= 12 &&
            std::string_view(reinterpret_cast<const char*>(p.prefix.data() + 8), 4) == "WEBP")
            return {ImportProbeMatch::Strong, "image", "WebP RIFF signature"};
        if (ext == ".tga")
            return {ImportProbeMatch::Possible, "image",
                    "TGA requires full pixel/header admission"};
        return {};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(!stop.stop_requested(), "Texture discovery cancelled");
        const auto locator = ProjectPaths::normalize(request.source);
        const ProjectPaths paths(request.project);
        auto bytes = read_bytes(paths.resolve(locator), source_limit);
        const auto kind =
            probe({locator, std::span(bytes).first(std::min<std::size_t>(65536, bytes.size()))});
        require(kind.match != ImportProbeMatch::No, "Texture source does not match importer");
        const auto& d = AssetImporter::descriptor();
        require(std::find(d.extensions.begin(), d.extensions.end(), extension(locator)) !=
                    d.extensions.end(),
                "Unsupported texture source extension");
        require(std::any_of(d.targets.begin(), d.targets.end(),
                            [&](const auto& t) {
                                return t.backend == request.target.backend &&
                                       t.profile == request.target.profile;
                            }),
                "Unsupported texture target");
        AssetImportPlan plan;
        auto& input = plan.input;
        input.source_digest = content_digest(bytes);
        input.importer = d.id;
        input.importer_revision = d.revision;
        input.settings_version = settings().version();
        input.settings = settings().effective(request.settings);
        input.output_format = d.output_format;
        input.output_version = d.output_version;
        input.platform = request.target.platform;
        input.backend = request.target.backend;
        input.profile = request.target.profile;
        plan.data = {{"source_kind", kind.source_kind}, {"extension", extension(locator)}};
        require(!stop.stop_requested(), "Texture discovery cancelled");
        return plan;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        const auto current = discover(request, stop);
        require(current.input.document() == plan.input.document() && current.data == plan.data,
                "Texture source/settings changed after discovery");
        auto bytes =
            read_bytes(ProjectPaths(request.project).resolve(request.source), source_limit);
        require(content_digest(bytes) == plan.input.source_digest,
                "Texture source changed while capturing snapshot");
        if (progress)
            progress(.15, "Preparing texture in isolated worker");
        const Json payload{{"recipe", AssetImporter::descriptor().id},
                           {"revision", texture_recipe_revision()},
                           {"settings", request.settings},
                           {"kind", plan.data.at("source_kind")},
                           {"extension", plan.data.at("extension")},
                           {"source_digest", plan.input.source_digest},
                           {"backend", request.target.backend}};
        auto files = run_import_process(worker_, request.project,
                                        {payload, {{"source.bin", std::move(bytes)}}},
                                        texture_worker_limits(), stop);
        CachedArtifact candidate{{}, Json::object(), std::move(files)};
        validate(candidate);
        if (progress)
            progress(1., "Texture candidate validated");
        return std::move(candidate.files);
    }
    void validate(const CachedArtifact& candidate) const override {
        const auto found = std::find_if(candidate.files.begin(), candidate.files.end(),
                                        [](const auto& f) { return f.name == "texture.json"; });
        require(found != candidate.files.end(), "Missing texture bundle index");
        const auto index = decode_texture_bundle_index(found->bytes);
        require(candidate.files.size() == index.variants.size() + 1,
                "Texture bundle has unexpected file count");
        std::set<std::string> names;
        for (const auto& file : candidate.files)
            require(names.insert(file.name).second, "Duplicate texture bundle file");
        for (const auto& entry : index.variants) {
            const auto file = std::find_if(candidate.files.begin(), candidate.files.end(),
                                           [&](const auto& f) { return f.name == entry.file; });
            require(file != candidate.files.end() && file->bytes.size() == entry.bytes &&
                        content_digest(file->bytes) == entry.digest,
                    "Texture variant file/digest disagrees with bundle");
            require(decode_texture(file->bytes).semantic == entry.semantic,
                    "Texture variant semantic disagrees with bundle");
        }
    }
};
} // namespace
std::string texture_recipe_revision() {
    return asset_build_digest({{"sources_toolchain", FORGE_TEXTURE_RECIPE_FINGERPRINT},
                               {"configuration", FORGE_TEXTURE_RECIPE_CONFIGURATION}});
}
WorkerLimits texture_worker_limits() {
    WorkerLimits limits;
    limits.memory_bytes = 1024ull * 1024 * 1024;
    limits.file_bytes = 256ull * 1024 * 1024;
    limits.total_bytes = 512ull * 1024 * 1024;
    limits.seconds = 120;
    limits.cpu_seconds = 110;
    limits.files = 16;
    limits.cancellation_grace_ms = 250;
    return limits;
}
ImportSettingsSchema texture_settings(bool container) {
    std::vector<ImportSettingRule> rules;
    rules.push_back(choice("semantic", "Usage",
                           "Controls color-space and normal-map interpretation.", "auto",
                           {"auto", "color", "data", "normal", "hdr"}));
    ImportSettingRule additional{
        "additional_usages", "Additional usages",
        "Cook additional color/data/normal/HDR variants under the same Texture AssetId.",
        ImportSettingType::StringList, Json::array()};
    additional.choices = {"color", "data", "normal", "hdr"};
    additional.max_entries = 3;
    additional.max_length = 16;
    rules.push_back(additional);
    rules.push_back(choice("wrap", "Wrap", "Sampling beyond the texture edges.", "repeat",
                           {"repeat", "mirror", "clamp"}));
    rules.push_back(choice("filter", "Filtering", "Nearest or linear texture sampling.", "linear",
                           {"nearest", "linear"}));
    ImportSettingRule aniso{"anisotropy", "Anisotropy",
                            "Maximum anisotropic samples; requires linear filtering.",
                            ImportSettingType::Integer, 1};
    aniso.minimum = 1;
    aniso.maximum = 16;
    rules.push_back(aniso);
    if (!container) {
        rules.push_back(choice("transfer", "Color space",
                               "Auto uses sRGB for color and linear for data/normal/HDR.", "auto",
                               {"auto", "srgb", "linear"}));
        rules.push_back(choice("compression", "Compression",
                               "Native BC encoding is for 8-bit normalized images.", "none",
                               {"none", "bc", "bc-high-quality"}));
        rules.push_back({"mips", "Generate mips", "Prepare filtered smaller levels.",
                         ImportSettingType::Boolean, true});
        rules.push_back({"flip_vertical", "Flip vertically",
                         "Flip pixel rows before mip preparation.", ImportSettingType::Boolean,
                         false});
        rules.push_back({"flip_green", "Flip normal green",
                         "Convert the opposite tangent-space Y convention.",
                         ImportSettingType::Boolean, false});
        rules.push_back({"premultiply", "Premultiply alpha",
                         "Prepare premultiplied color before filtering.",
                         ImportSettingType::Boolean, false});
        ImportSettingRule maximum{"max_size", "Maximum dimension",
                                  "Select the first standard mip that fits.",
                                  ImportSettingType::Integer, 16384};
        maximum.minimum = 1;
        maximum.maximum = 16384;
        rules.push_back(maximum);
    }
    return ImportSettingsSchema(
        importer_id(container), 1, std::move(rules), [container](const Json& j) {
            std::set<std::string> usages;
            for (const auto& value : j.at("additional_usages")) {
                const auto usage = value.get<std::string>();
                require(
                    (usage == "color" || usage == "data" || usage == "normal" || usage == "hdr") &&
                        usages.insert(usage).second,
                    "Additional texture usages must be unique supported semantics");
            }
            require(j.at("filter") == "linear" || j.at("anisotropy") == 1,
                    "Anisotropy requires linear filtering");
            if (!container) {
                require(j.at("semantic") == "normal" || usages.contains("normal") ||
                            j.at("flip_green") == false,
                        "Green flip requires normal-map usage");
                require(j.at("transfer") != "srgb" ||
                            (j.at("semantic") != "normal" && j.at("semantic") != "data" &&
                             j.at("semantic") != "hdr"),
                        "Non-color usage requires linear transfer");
            }
        });
}
std::shared_ptr<const AssetImporter> texture_importer(std::filesystem::path worker,
                                                      bool container) {
    return std::make_shared<TextureImporter>(std::move(worker), container);
}
} // namespace forge::asset_detail
