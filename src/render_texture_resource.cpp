#include "render_texture_resource.hpp"

namespace forge {
RenderTextureData admit_render_texture_artifact(const AssetRecord& record,
                                                const CachedArtifact& artifact) {
    const auto& file = asset_detail::admit_single_source_artifact(
        record, artifact,
        {RenderTextureAsset::type, "forge.render.texture", "forge.render.texture.v1",
         "forge.render-texture", "render-texture.json", "forge.render_texture"});
    const auto source = RenderTextureSource::parse(file.bytes);
    if (source.asset() != record.id)
        throw std::runtime_error("Cooked render texture identity mismatch");
    return source.compiled();
}
ResourceTicket request_render_texture(ResourcePool<RenderTextureAsset>& pool,
                                      const std::filesystem::path& project,
                                      const AssetRecord& record) {
    if (record.type != RenderTextureAsset::type || record.schema_version != 1 || record.subasset)
        throw std::runtime_error("Render texture asset has the wrong type/version");
    const auto& selection = record.metadata.at("forge.import");
    const auto revision = selection.at("key").get<std::string>();
    if (!selection.at("generation").is_number_unsigned() ||
        !selection.at("generation").get<std::uint64_t>())
        throw std::runtime_error("Render texture selection has an invalid generation");
    return pool.request(
        AssetRef<RenderTextureAsset>{record.id}, revision,
        selection.at("generation").get<std::uint64_t>(),
        [project, record, revision](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Render texture load cancelled");
            DerivedDataCache cache(project, {render_texture_source_byte_limit,
                                             render_texture_source_byte_limit + 65536, 1});
            const auto artifact = cache.load_selected(revision, [](const auto&) {});
            auto result = std::make_unique<RenderTextureData>(
                admit_render_texture_artifact(record, artifact));
            if (stop.stop_requested())
                throw std::runtime_error("Render texture load cancelled");
            const auto bytes = result->resident_bytes();
            return ResourceCandidate<RenderTextureAsset>{std::move(result), {bytes}};
        });
}
} // namespace forge
