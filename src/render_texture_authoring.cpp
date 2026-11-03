#include "render_texture_authoring.hpp"
#include "asset_single_source_importer.hpp"

namespace forge {
namespace {
const SingleSourceImportProfile<RenderTextureSource> profile{
    .importer = "forge.render.texture",
    .revision = "forge.render.texture.v1",
    .label = "Render Texture",
    .description = "Publish a camera output's size and linear HDR image profile.",
    .suffix = ".rendertexture.json",
    .format = "forge.render-texture",
    .asset_type = RenderTextureAsset::type,
    .file = "render-texture.json",
    .evidence_schema = "forge.render-texture.single.v1",
    .metadata_key = "forge.render_texture",
    .source_limit = render_texture_source_byte_limit,
    .cooked_limit = render_texture_source_byte_limit,
    .parse_cooked = RenderTextureSource::parse};
} // namespace
std::shared_ptr<const AssetImporterRegistry> render_texture_import_registry() {
    return single_source_import_registry(profile);
}
void prepare_render_texture_publication(AssetPublicationCandidate& candidate,
                                        const AssetImportPlan& plan) {
    prepare_single_source_publication(candidate, plan, profile);
}
} // namespace forge
