#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/render_texture.hpp>

namespace forge {
std::shared_ptr<const AssetImporterRegistry> render_texture_import_registry();
void prepare_render_texture_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct RenderTextureDocumentTraits {
    using Source = RenderTextureSource;
    static constexpr auto byte_limit = render_texture_source_byte_limit;
    static constexpr auto asset_type = RenderTextureAsset::type;
    static constexpr auto suffix = ".rendertexture.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using RenderTextureDocument = AssetSourceDocument<RenderTextureDocumentTraits>;
} // namespace forge
