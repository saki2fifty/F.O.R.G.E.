#pragma once
#include "single_source_admission.hpp"
#include <forge/render_texture.hpp>
#include <forge/resource.hpp>

namespace forge {
RenderTextureData admit_render_texture_artifact(const AssetRecord&, const CachedArtifact&);
template <> struct ResourceTraits<RenderTextureAsset> {
    using Data = RenderTextureData;
};
ResourceTicket request_render_texture(ResourcePool<RenderTextureAsset>&,
                                      const std::filesystem::path&, const AssetRecord&);
} // namespace forge
