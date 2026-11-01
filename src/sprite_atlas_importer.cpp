#include "sprite_atlas_importer.hpp"
#include "asset_bytes.hpp"
#include "sprite_atlas_recipe.hpp"
#include "sprite_frame_json.hpp"
#include "texture_bundle_validation.hpp"
#include <algorithm>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
using namespace asset_detail;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json frame_evidence(std::span<const SpriteFrameEntry> frames) {
    Json result = Json::array();
    for (const auto& entry : frames)
        result.push_back(
            {{"id", entry.id}, {"region", detail::sprite_frame_document(entry.region)}});
    return result;
}
void validate_selection(std::span<const ArtifactFile> files, const Json& expected) {
    const auto index = validate_texture_bundle(files, {}, 2);
    const auto& name = index.find(TextureSemantic::Color).file;
    const auto image =
        std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.name == name; });
    const auto data = decode_texture(image->bytes);
    require(data.width == expected.at("width") && data.height == expected.at("height") &&
                frame_evidence(data.sprite_frames) == expected.at("frames"),
            "Atlas candidate frames differ from the captured recipe/layout");
}
struct Snapshot {
    std::vector<std::byte> source;
    SpriteAtlasCapture images;
    AssetImportPlan plan;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Atlas discovery cancelled");
    Snapshot result;
    result.source = read_bytes(ProjectPaths(request.project).resolve(request.source),
                               sprite_atlas_recipe_byte_limit);
    const auto recipe = SpriteAtlasRecipe::parse(result.source);
    require(recipe.asset() == request.asset, "Atlas source/catalog identity mismatch");
    const auto catalog = AssetCatalog::open_project(request.project);
    std::vector<AssetId> textures;
    for (const auto input : recipe.inputs())
        textures.push_back(input.texture);
    result.images =
        capture_sprite_atlas_images(request.project, catalog, textures, recipe.settings(), stop);
    auto& input = result.plan.input;
    input.source_digest = content_digest(result.source);
    input.importer = sprite_atlas_importer_id;
    input.importer_revision = sprite_atlas_source_revision();
    input.output_format = "forge.texture-bundle";
    input.output_version = 2;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    input.dependencies = result.images.dependencies;
    result.plan.data = {
        {"asset", recipe.asset()},
        {"inputs", recipe.document.at("frames")},
        {"width", result.images.layout.width},
        {"height", result.images.layout.height},
        {"frames", frame_evidence(sprite_atlas_frame_entries(recipe, result.images.layout))}};
    return result;
}
class SpriteAtlasImporter final : public AssetImporter {
    std::filesystem::path worker_;

  public:
    explicit SpriteAtlasImporter(std::filesystem::path worker)
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = sprite_atlas_importer_id;
                  d.revision = sprite_atlas_source_revision();
                  d.label = "Sprite atlas";
                  d.description =
                      "Rebuild a packed Texture with stable frame keys from published images.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.sprite-atlas"};
                  d.output_types = {TextureAsset::type};
                  d.output_format = "forge.texture-bundle";
                  d.output_version = 2;
                  d.targets = {{"*", "d3d12", "desktop"}, {"*", "none", "cpu"}};
                  const auto limits = sprite_atlas_source_limits();
                  d.limits = {limits.memory_bytes, 96ull * 1024 * 1024, limits.seconds, 2};
                  return d;
              }(),
              {std::string(sprite_atlas_importer_id), 1, {}}),
          worker_(std::move(worker)) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        auto name = path_utf8(source.source);
        for (auto& c : name)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return name.ends_with(".atlas.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.sprite-atlas",
                                       "FORGE authored Sprite atlas source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::any_of(descriptor().targets.begin(), descriptor().targets.end(),
                            [&](const auto& t) {
                                return t.backend == request.target.backend &&
                                       t.profile == request.target.profile;
                            }),
                "Unsupported Sprite atlas target");
        (void)settings().effective(request.settings);
        return capture(request, stop).plan;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto current = capture(request, stop);
        require(current.plan.input.document() == plan.input.document() &&
                    current.plan.data == plan.data,
                "Atlas source or selected image dependencies changed after discovery");
        ImportProcessRequest process{{{"recipe", descriptor().id},
                                      {"revision", descriptor().revision},
                                      {"asset_id", request.asset},
                                      {"source_digest", plan.input.source_digest}},
                                     {}};
        process.inputs.reserve(current.images.images.size() + 1);
        process.inputs.push_back({"source.bin", std::move(current.source)});
        for (auto& image : current.images.images)
            process.inputs.push_back(std::move(image));
        if (progress)
            progress(.25, "Packing Sprite atlas in isolated worker");
        auto files = run_import_process(worker_, request.project, std::move(process),
                                        sprite_atlas_source_limits(), stop);
        validate_selection(files, plan.data);
        if (progress)
            progress(1., "Atlas pixels and frame keys validated");
        return files;
    }
    void validate(const CachedArtifact& candidate) const override {
        (void)validate_texture_bundle(candidate.files, {}, 2);
    }
};
} // namespace
namespace asset_detail {
std::shared_ptr<const AssetImporter> sprite_atlas_importer(std::filesystem::path worker) {
    return std::make_shared<SpriteAtlasImporter>(std::move(worker));
}
} // namespace asset_detail
void prepare_sprite_atlas_publication(AssetPublicationCandidate& c, const AssetImportPlan& plan) {
    require(c.input.document() == plan.input.document() &&
                plan.input.importer == asset_detail::sprite_atlas_importer_id &&
                plan.input.importer_revision == asset_detail::sprite_atlas_source_revision() &&
                plan.input.output_format == "forge.texture-bundle" &&
                plan.input.output_version == 2 &&
                plan.data.at("asset").get<AssetId>() == c.ticket.owner &&
                c.sidecar.identity.entries.empty(),
            "Atlas publication identity/recipe mismatch");
    validate_selection(c.files, plan.data);
    const auto& frames = plan.data.at("frames");
    const auto& inputs = plan.data.at("inputs");
    require(inputs.is_array() && !inputs.empty() && inputs.size() == frames.size() &&
                plan.input.dependencies.size() == inputs.size(),
            "Atlas dependency closure differs from inputs");
    std::set<AssetId> expected_images;
    std::set<std::uint32_t> expected_keys;
    for (const auto& row : inputs) {
        const auto texture = row.at("texture").get<AssetId>();
        const auto& key = row.at("id");
        require(texture && key.is_number_integer() && key >= 0 && key <= UINT32_MAX &&
                    expected_images.insert(texture).second &&
                    expected_keys.insert(key.get<std::uint32_t>()).second,
                "Atlas captured inputs contain invalid or duplicate references/keys");
    }
    for (const auto& row : frames)
        require(expected_keys.erase(row.at("id").get<std::uint32_t>()) == 1,
                "Atlas cooked keys differ from captured image keys");
    std::set<AssetId> images;
    for (const auto& edge : plan.input.dependencies)
        require(expected_images.contains(edge.target) && edge.target != c.ticket.owner &&
                    images.insert(edge.target).second && edge.expected_type == TextureAsset::type &&
                    edge.kind == AssetDependencyKind::Build &&
                    edge.role == "atlas.image." + edge.target.str() &&
                    valid_content_digest(edge.revision),
                "Atlas publication image dependency mismatch");
    auto& identity = c.sidecar.identity;
    identity.owner = c.ticket.owner;
    identity.source = c.ticket.source;
    identity.source_digest = c.input.source_digest;
    identity.evidence_schema = "forge.sprite-atlas.single.v1";
    AssetRecord record;
    record.id = c.ticket.owner;
    record.type = TextureAsset::type;
    record.source = c.ticket.source;
    c.records = {std::move(record)};
}
} // namespace forge
