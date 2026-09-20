#include "animation_asset.hpp"
#include "animation_worker.hpp"
#include "asset_bytes.hpp"
#include "gltf_transform.hpp"
#include <forge/animation_conversion.hpp>
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <set>
namespace forge {
namespace {
using namespace animation_detail;
using namespace asset_detail;
std::string index_digest(const std::filesystem::path& index) {
    return std::filesystem::exists(index) ? content_digest(read_bytes(index, max_asset_index_bytes))
                                          : "absent";
}
std::span<const std::byte> bytes(const std::string& text) { return std::as_bytes(std::span(text)); }
void write(const std::filesystem::path& path, std::span<const std::byte> value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out ||
        !out.write(reinterpret_cast<const char*>(value.data()),
                   static_cast<std::streamsize>(value.size())) ||
        !out.flush())
        throw std::runtime_error("Cannot stage animation candidate");
}
struct Input {
    std::filesystem::path path;
    std::string digest;
};
} // namespace
struct AnimationCandidate::Impl {
    std::filesystem::path root, staging;
    AssetCatalog catalog;
    std::string baseline;
    std::vector<Input> inputs;
    std::vector<AssetRecord> records;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> artifacts;
    bool published = false;
    explicit Impl(const std::filesystem::path& project)
        : root(ProjectPaths(project).root()), catalog(root) {
        baseline = index_digest(AssetCatalog::project_index(root));
        catalog = AssetCatalog::open_project(root);
        if (index_digest(AssetCatalog::project_index(root)) != baseline)
            throw std::runtime_error("Asset catalog changed while preparing conversion");
        staging =
            ProjectPaths(root).resolve(".forge/animation-staging/" + AssetId::generate().str());
        std::filesystem::create_directories(staging);
    }
    ~Impl() {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
    }
};
AnimationCandidate::AnimationCandidate(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
AnimationCandidate::AnimationCandidate(AnimationCandidate&&) noexcept = default;
AnimationCandidate& AnimationCandidate::operator=(AnimationCandidate&&) noexcept = default;
AnimationCandidate::~AnimationCandidate() = default;
AnimationCandidate prepare_animation_conversion(const std::filesystem::path& root,
                                                const std::filesystem::path& source,
                                                const std::filesystem::path& converter,
                                                std::stop_token cancel) {
    using namespace animation_detail;
    using namespace asset_detail;
    ProjectPaths paths(root);
    auto locator = ProjectPaths::normalize(source);
    if (locator.extension() != ".gltf")
        throw std::runtime_error("Select a project-contained .gltf animation source");
    auto candidate = std::make_unique<AnimationCandidate::Impl>(root);
    auto data = read_bytes(paths.resolve(locator), 4 * 1024 * 1024);
    candidate->inputs.push_back({locator, content_digest(data)});
    auto doc = Json::parse(data.begin(), data.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 64)
            throw std::runtime_error("Animation source nesting exceeds 64 levels");
        return true;
    });
    if (!doc.is_object() || doc.at("asset").at("version") != "2.0" ||
        doc.contains("extensionsRequired") || doc.contains("extensionsUsed"))
        throw std::runtime_error("Animation conversion supports core glTF 2.0 without extensions");
    if (!doc.contains("nodes") || !doc["nodes"].is_array() || doc["nodes"].empty() ||
        doc["nodes"].size() > max_joints || !doc.contains("animations") ||
        !doc["animations"].is_array() || doc["animations"].empty() || doc["animations"].size() > 8)
        throw std::runtime_error("Animation source requires 1-1024 nodes and 1-8 named clips");
    if (!doc.contains("buffers") || !doc["buffers"].is_array() || doc["buffers"].size() > 16)
        throw std::runtime_error("Unsupported animation source buffers");
    // Material/image import is outside this bridge. Do not allow image URI access.
    if (doc.contains("images") && !doc["images"].empty())
        throw std::runtime_error("Use an animation-only glTF source without image assets");
    std::size_t total = data.size();
    unsigned buffer_index = 0;
    Json dependencies = Json::array();
    for (auto& buffer : doc["buffers"]) {
        const auto uri = buffer.at("uri").get<std::string>();
        if (uri.starts_with("data:application/octet-stream;base64,") ||
            uri.starts_with("data:application/gltf-buffer;base64,")) {
            if (uri.size() > 4 * 1024 * 1024)
                throw std::runtime_error("Embedded animation buffer exceeds limit");
        } else {
            if (uri.empty() || uri.find_first_of(":%?#\\") != std::string::npos)
                throw std::runtime_error("Unsupported animation source dependency URI");
            auto dependency =
                ProjectPaths::normalize(locator.parent_path() / std::filesystem::u8path(uri));
            auto buffer_data = read_bytes(paths.resolve(dependency), 16 * 1024 * 1024);
            if (buffer_data.size() > 16 * 1024 * 1024 - total)
                throw std::runtime_error("Animation source dependencies exceed 16 MiB");
            total += buffer_data.size();
            const auto digest = content_digest(buffer_data);
            candidate->inputs.push_back({dependency, digest});
            dependencies.push_back({{"source", path_utf8(dependency)}, {"sha256", digest}});
            auto filename = "buffer-" + std::to_string(buffer_index) + ".bin";
            write(candidate->staging / filename, buffer_data);
            buffer["uri"] = filename;
        }
        ++buffer_index;
    }
    std::set<std::string> names;
    Json settings = {{"skeleton", {{"filename", "skeleton.ozz"}}}, {"animations", Json::array()}};
    for (const auto& animation : doc["animations"]) {
        if (!animation.at("channels").is_array() || animation.at("channels").empty() ||
            animation.at("channels").size() > 3072)
            throw std::runtime_error("Animation requires bounded skeletal transform channels");
        for (const auto& channel : animation.at("channels")) {
            const auto property = channel.at("target").at("path").get<std::string>();
            if (property != "translation" && property != "rotation" && property != "scale")
                throw std::runtime_error(
                    "Only skeletal translation, rotation and scale channels are supported");
        }
        auto name = animation.at("name").get<std::string>();
        if (name.empty() || name.size() > 128 ||
            name.find_first_of("*?\0", 0, 3) != std::string::npos || !names.insert(name).second)
            throw std::runtime_error("Animation clips require unique names without wildcards");
        settings["animations"].push_back(
            {{"clip", name},
             {"filename", "clip-" + std::to_string(settings["animations"].size()) + ".ozz"},
             {"iframe_interval", 0},
             {"raw", false},
             {"additive", false},
             {"optimize", true},
             {"sampling_rate", 30}});
    }
    // Exact Ozz0.17 fallback channels read TRS even when the skeleton reads matrix.
    // Convert only the private input; original source/provenance remains unchanged.
    for (auto& node : doc["nodes"]) {
        const auto rest = canonical_ozz_rest(node);
        node.erase("matrix");
        node.update(rest);
    }
    write(candidate->staging / "source.gltf", bytes(doc.dump()));
    write(candidate->staging / "config.json", bytes(settings.dump()));
    run_converter(converter, candidate->staging, cancel);
    auto skeleton_bytes = read_bytes(candidate->staging / "skeleton.ozz", max_archive_bytes);
    auto skeleton = std::make_shared<Skeleton>(skeleton_bytes);
    auto skeleton_digest = content_digest(skeleton_bytes);
    AssetRecord source_record{AssetId::generate(), "animation_source", locator, 1, {}};
    for (const auto& [id, record] : candidate->catalog.records())
        if (paths.same_locator(record.source, locator)) {
            if (record.type != "animation_source")
                throw std::runtime_error("Source already has another asset type");
            source_record = record;
        }
    AssetId skeleton_id = AssetId::generate();
    std::map<std::string, AssetId> existing_clips;
    for (const auto& [id, record] : candidate->catalog.records()) {
        const auto& metadata = record.metadata;
        if (metadata.value("source_asset", std::string{}) != source_record.id.str())
            continue;
        if (record.type == SkeletonAsset::type)
            skeleton_id = id;
        if (record.type == AnimationClipAsset::type)
            existing_clips.emplace(metadata.at("clip_name").get<std::string>(), id);
    }
    if (!existing_clips.empty()) {
        std::set<std::string> old_names;
        for (const auto& [name, id] : existing_clips) {
            (void)id;
            old_names.insert(name);
        }
        if (old_names != names)
            throw std::runtime_error(
                "Clip names changed; previous animation set retained. Renaming/removing clips "
                "requires an explicit future remapping workflow.");
    }
    const auto revision = AssetId::generate().str();
    const auto destination =
        std::filesystem::path("Assets/Animation/Generated") / source_record.id.str() / revision;
    Json provenance = {
        {"version", 1},
        {"ozz_version", "0.17.0"},
        {"ozz_revision", ozz_revision},
        {"converter", "gltf2ozz"},
        {"converter_revision", ozz_revision},
        {"converter_sha256", content_digest(read_bytes(converter, 64 * 1024 * 1024))},
        {"settings", settings},
        {"source_asset", source_record.id},
        {"source_sha256", candidate->inputs.front().digest},
        {"source_dependencies", dependencies},
        {"skeleton_asset", skeleton_id},
        {"skeleton_sha256", skeleton_digest}};
    auto skeleton_metadata = provenance;
    skeleton_metadata["artifact_sha256"] = skeleton_digest;
    candidate->records.push_back({skeleton_id,
                                  SkeletonAsset::type,
                                  destination / "skeleton.ozz",
                                  1,
                                  {source_record.id},
                                  skeleton_metadata});
    candidate->artifacts.push_back(
        {candidate->staging / "skeleton.ozz", paths.resolve(destination / "skeleton.ozz")});
    for (const auto& entry : settings["animations"]) {
        const auto name = entry.at("clip").get<std::string>(),
                   filename = entry.at("filename").get<std::string>();
        auto clip_bytes = read_bytes(candidate->staging / filename, max_archive_bytes);
        auto clip = std::make_shared<Clip>(clip_bytes);
        Sampler sampler(skeleton, clip);
        for (float ratio : {0.f, .25f, .5f, .75f, 1.f})
            sampler.sample(ratio);
        const auto id =
            existing_clips.contains(name) ? existing_clips.at(name) : AssetId::generate();
        auto metadata = provenance;
        metadata["artifact_sha256"] = content_digest(clip_bytes);
        metadata["clip_name"] = name;
        candidate->records.push_back({id,
                                      AnimationClipAsset::type,
                                      destination / filename,
                                      1,
                                      {skeleton_id, source_record.id},
                                      metadata});
        candidate->artifacts.push_back(
            {candidate->staging / filename, paths.resolve(destination / filename)});
    }
    source_record.metadata = {{"version", 1}, {"source_sha256", candidate->inputs.front().digest}};
    candidate->records.push_back(source_record);
    for (const auto& record : candidate->records)
        if (candidate->catalog.records().contains(record.id))
            candidate->catalog.replace(record);
        else
            candidate->catalog.add(record);
    if (cancel.stop_requested())
        throw std::runtime_error("Animation conversion cancelled");
    return AnimationCandidate(std::move(candidate));
}
std::vector<AssetRecord> AnimationCandidate::publish() {
    using namespace animation_detail;
    using namespace asset_detail;
    if (!impl_ || impl_->published)
        throw std::runtime_error("Animation candidate is no longer publishable");
    ProjectPaths paths(impl_->root);
    auto index = AssetCatalog::project_index(impl_->root);
    if (index_digest(index) != impl_->baseline)
        throw std::runtime_error("Asset catalog changed during animation conversion; retry");
    for (const auto& input : impl_->inputs)
        if (content_digest(read_bytes(paths.resolve(input.path), 16 * 1024 * 1024)) != input.digest)
            throw std::runtime_error(
                "Animation source changed during conversion; previous assets retained");
    // Unique immutable candidate directory. Catalog is the only active selector.
    // Failed/interrupted publication can leave unselected files, never a partial active set.
    for (const auto& [staged, target] : impl_->artifacts) {
        const auto expected =
            std::find_if(impl_->records.begin(), impl_->records.end(),
                         [&](const auto& r) { return paths.resolve(r.source) == target; });
        auto data = read_bytes(staged, max_archive_bytes);
        if (expected == impl_->records.end() ||
            content_digest(data) != expected->metadata.at("artifact_sha256").get<std::string>())
            throw std::runtime_error(
                "Staged animation candidate changed; previous assets retained");
        std::filesystem::create_directories(target.parent_path());
        if (std::filesystem::exists(target))
            throw std::runtime_error("Animation candidate destination already exists");
        write(target, data);
    }
    if (index_digest(index) != impl_->baseline)
        throw std::runtime_error("Asset catalog changed before animation publication");
    impl_->catalog.save(index);
    impl_->published = true;
    return impl_->records;
}
} // namespace forge
