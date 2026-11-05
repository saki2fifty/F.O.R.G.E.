#include "texture_importer.hpp"
#include "asset_bytes.hpp"
#include "paint_texture_source.hpp"
#include "texture_bundle_validation.hpp"
#include <algorithm>
#include <forge/texture_asset.hpp>
#include <forge/texture_bundle.hpp>
#include <limits>

namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t source_limit = 256 * 1024 * 1024;
enum class TextureKind { Image, Container, Paint };
std::string importer_id(TextureKind kind) {
    return kind == TextureKind::Paint       ? paint_texture_importer_id
           : kind == TextureKind::Container ? "forge.texture.container"
                                            : "forge.texture.image";
}
ImportSettingsSchema settings_schema(TextureKind kind);
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
    ImportSettingRule rule{std::move(key),
                           std::move(label),
                           std::move(help),
                           ImportSettingType::Choice,
                           std::move(value),
                           {},
                           {},
                           {}};
    rule.choices = std::move(choices);
    return rule;
}
AssetImporterDescriptor descriptor(TextureKind kind) {
    const bool container = kind == TextureKind::Container, paint = kind == TextureKind::Paint;
    AssetImporterDescriptor d;
    d.id = importer_id(kind);
    d.revision = texture_recipe_revision();
    d.label = paint ? "Paint texture" : container ? "Texture container" : "Image texture";
    d.description =
        paint       ? "Prepare editable RGBA8 paint pixels, mips and optional BC compression."
        : container ? "Preserve admitted DDS/KTX mip chains and dimensions."
                    : "Prepare bounded image pixels, semantic mips and optional BC compression.";
    d.extensions = paint       ? std::vector<std::string>{".json"}
                   : container ? std::vector<std::string>{".dds", ".ktx", ".ktx2"}
                               : std::vector<std::string>{".png", ".jpg", ".jpeg", ".tga",
                                                          ".bmp", ".hdr", ".rgbe", ".webp"};
    d.source_kinds = paint       ? std::vector<std::string>{"forge.paint-texture"}
                     : container ? std::vector<std::string>{"dds", "ktx", "ktx2"}
                                 : std::vector<std::string>{"image", "hdr"};
    d.output_types = {"texture"};
    d.output_format = "forge.texture-bundle";
    d.limits.output_files = 16;
    d.targets = {{"*", "d3d12", "desktop"}, {"*", "none", "cpu"}};
    return d;
}
class TextureImporter final : public AssetImporter {
    std::filesystem::path worker_;
    TextureKind kind_;

  public:
    TextureImporter(std::filesystem::path worker, TextureKind kind)
        : AssetImporter(asset_detail::descriptor(kind), settings_schema(kind)),
          worker_(std::move(worker)), kind_(kind) {}
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
        if (kind_ == TextureKind::Paint) {
            auto name = p.source.filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : char(c);
            });
            return name.ends_with(".paint-texture.json")
                       ? ImportProbeResult{ImportProbeMatch::Possible, "forge.paint-texture",
                                           "Editable paint source requires full admission"}
                       : ImportProbeResult{};
        }
        if (kind_ == TextureKind::Container) {
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
        auto bytes = read_bytes(paths.resolve(locator), kind_ == TextureKind::Paint
                                                            ? paint_texture_source_byte_limit
                                                            : source_limit);
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
        if (kind_ == TextureKind::Paint) {
            const auto source = PaintTextureSource::parse(bytes);
            require(source.asset() == request.asset, "Paint texture source identity mismatch");
            plan.data["asset_id"] = source.asset().str();
        }
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
        auto bytes = read_bytes(ProjectPaths(request.project).resolve(request.source),
                                kind_ == TextureKind::Paint ? paint_texture_source_byte_limit
                                                            : source_limit);
        require(content_digest(bytes) == plan.input.source_digest,
                "Texture source changed while capturing snapshot");
        if (progress)
            progress(.15, "Preparing texture in isolated worker");
        Json payload{{"recipe", AssetImporter::descriptor().id},
                     {"revision", texture_recipe_revision()},
                     {"settings", request.settings},
                     {"kind", plan.data.at("source_kind")},
                     {"extension", plan.data.at("extension")},
                     {"source_digest", plan.input.source_digest},
                     {"backend", request.target.backend}};
        if (kind_ == TextureKind::Paint)
            payload["asset_id"] = plan.data.at("asset_id");
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
        (void)validate_texture_bundle(candidate.files);
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
namespace {
ImportSettingsSchema settings_schema(TextureKind kind) {
    const bool container = kind == TextureKind::Container, paint = kind == TextureKind::Paint;
    std::vector<ImportSettingRule> rules;
    rules.push_back(
        choice("semantic", "Usage", "Controls color-space and normal-map interpretation.", "auto",
               paint ? std::vector<std::string>{"auto", "color", "data"}
                     : std::vector<std::string>{"auto", "color", "data", "normal", "hdr"}));
    ImportSettingRule additional{
        "additional_usages",
        "Additional usages",
        paint ? "Cook additional color/data variants under the same Texture AssetId."
              : "Cook additional color/data/normal/HDR variants under the same Texture AssetId.",
        ImportSettingType::StringList,
        Json::array(),
        {},
        {},
        {}};
    additional.choices = paint ? std::vector<std::string>{"color", "data"}
                               : std::vector<std::string>{"color", "data", "normal", "hdr"};
    additional.max_entries = paint ? 2 : 3;
    additional.max_length = 16;
    rules.push_back(additional);
    rules.push_back(choice("wrap", "Wrap", "Sampling beyond the texture edges.", "repeat",
                           {"repeat", "mirror", "clamp"}));
    rules.push_back(choice("filter", "Filtering", "Nearest or linear texture sampling.", "linear",
                           {"nearest", "linear"}));
    ImportSettingRule aniso{"anisotropy",
                            "Anisotropy",
                            "Requested anisotropy; requires linear filtering and device support.",
                            ImportSettingType::Integer,
                            1,
                            {},
                            {},
                            {}};
    aniso.minimum = 1;
    aniso.maximum = std::numeric_limits<unsigned>::max();
    rules.push_back(aniso);
    if (!container) {
        rules.push_back(choice("transfer", "Color space",
                               "Auto uses sRGB for color and linear for data/normal/HDR.", "auto",
                               {"auto", "srgb", "linear"}));
        rules.push_back(choice("compression", "Compression",
                               "Native BC encoding is for 8-bit normalized images.", "none",
                               {"none", "bc", "bc-high-quality"}));
        rules.push_back({"mips",
                         "Generate mips",
                         "Prepare filtered smaller levels.",
                         ImportSettingType::Boolean,
                         true,
                         {},
                         {},
                         {}});
        rules.push_back({"flip_vertical",
                         "Flip vertically",
                         "Flip pixel rows before mip preparation.",
                         ImportSettingType::Boolean,
                         false,
                         {},
                         {},
                         {}});
        rules.push_back({"flip_green",
                         "Flip normal green",
                         "Convert the opposite tangent-space Y convention.",
                         ImportSettingType::Boolean,
                         false,
                         {},
                         {},
                         {}});
        rules.push_back({"premultiply",
                         "Premultiply alpha",
                         "Prepare premultiplied color before filtering.",
                         ImportSettingType::Boolean,
                         false,
                         {},
                         {},
                         {}});
        ImportSettingRule maximum{"max_size",
                                  "Maximum dimension",
                                  "Select the first standard mip that fits.",
                                  ImportSettingType::Integer,
                                  16384,
                                  {},
                                  {},
                                  {}};
        maximum.minimum = 1;
        maximum.maximum = 16384;
        rules.push_back(maximum);
    }
    return ImportSettingsSchema(
        importer_id(kind), 1, std::move(rules), [container, paint](const Json& j) {
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
            if (paint) {
                require(j.at("semantic") != "normal" && j.at("semantic") != "hdr" &&
                            !usages.contains("normal") && !usages.contains("hdr") &&
                            j.at("flip_green") == false,
                        "Paint textures support color/data interpretation");
            }
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
} // namespace
ImportSettingsSchema texture_settings(bool container) {
    return settings_schema(container ? TextureKind::Container : TextureKind::Image);
}
ImportSettingsSchema paint_texture_settings() { return settings_schema(TextureKind::Paint); }
std::shared_ptr<const AssetImporter> paint_texture_importer(std::filesystem::path worker) {
    return std::make_shared<TextureImporter>(std::move(worker), TextureKind::Paint);
}
std::shared_ptr<const AssetImporter> texture_importer(std::filesystem::path worker,
                                                      bool container) {
    return std::make_shared<TextureImporter>(std::move(worker), container ? TextureKind::Container
                                                                          : TextureKind::Image);
}
} // namespace forge::asset_detail
