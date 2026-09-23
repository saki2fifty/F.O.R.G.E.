#include "runtime_raw_assets.hpp"
#include "asset_bytes.hpp"
#include "legacy_animation_provenance.hpp"
#include <forge/animation_components.hpp>
#include <forge/ui_assets.hpp>
namespace forge::package_detail {
bool legacy_animation(const AssetRecord& r) {
    return !r.subasset && (r.type == SkeletonAsset::type || r.type == AnimationClipAsset::type);
}
bool ui_source(const AssetRecord& r) {
    return !r.subasset && r.metadata.contains("forge.ui_source") &&
           (r.type == UiDocumentAsset::type || r.type == "ui_stylesheet" || r.type == "ui_font" ||
            r.type == TextureAsset::type);
}
bool raw_only(const AssetRecord& r) {
    return legacy_animation(r) || (ui_source(r) && !r.metadata.contains("forge.import"));
}
std::filesystem::path raw_locator(const AssetRecord& r) {
    return legacy_animation(r) ? std::filesystem::path("runtime/animation") / (r.id.str() + ".ozz")
           : r.metadata.contains("forge.runtime_ui")
               ? std::filesystem::u8path(
                     r.metadata.at("forge.runtime_ui").at("source").get<std::string>())
               : r.source;
}
std::vector<std::byte> admit_raw(const std::filesystem::path& root, const AssetRecord& r) {
    if (legacy_animation(r)) {
        animation_detail::validate_legacy_provenance(r.metadata);
        const auto data = asset_detail::read_bytes(ProjectPaths(root).resolve(r.source),
                                                   animation_detail::max_archive_bytes);
        if (asset_detail::content_digest(data) != r.metadata.at("artifact_sha256"))
            throw std::runtime_error("export.animation.corrupt: " + r.id.str());
        (void)animation_detail::validate_archive(
            data, r.type == SkeletonAsset::type ? animation_detail::ArchiveKind::Skeleton
                                                : animation_detail::ArchiveKind::Animation);
        return data;
    }
    if (!ui_source(r))
        throw std::runtime_error("export.ui.unsupported: No admitted UI source for " + r.id.str());
    UiResources resources(root);
    const auto path = raw_locator(r);
    auto data = resources.read(path_utf8(path));
    const auto source = resources.snapshot().sources.front();
    const auto& metadata = r.metadata.at("forge.ui_source");
    if (source.type != r.type || source.digest != metadata.at("digest") ||
        source.bytes != metadata.at("bytes"))
        throw std::runtime_error("export.ui.stale: Re-admit changed UI source " + path_utf8(path));
    return data;
}
void validate_raw_selections(const std::filesystem::path& root, const AssetCatalog& catalog) {
    for (const auto& [id, r] : catalog.records()) {
        if (ui_source(r) || legacy_animation(r))
            (void)admit_raw(root, r);
        if (!legacy_animation(r))
            continue;
        const auto sid = r.metadata.at("skeleton_asset").get<AssetId>();
        if (r.type == SkeletonAsset::type && sid != id)
            throw std::runtime_error(
                "export.animation.identity: Skeleton provenance disagrees with its identity");
        const auto found = catalog.records().find(sid);
        if (found == catalog.records().end() || found->second.type != SkeletonAsset::type)
            throw std::runtime_error("export.animation.missing: Missing skeleton " + sid.str());
        animation_detail::validate_legacy_pair(sid, found->second.metadata, r.metadata);
        if (r.type == AnimationClipAsset::type) {
            auto skeleton =
                std::make_shared<animation_detail::Skeleton>(admit_raw(root, found->second));
            auto clip = std::make_shared<animation_detail::Clip>(admit_raw(root, r));
            animation_detail::Sampler sampler(skeleton, clip);
            for (const auto ratio : {0.f, .5f, 1.f})
                (void)sampler.sample(ratio);
        }
    }
}
} // namespace forge::package_detail
