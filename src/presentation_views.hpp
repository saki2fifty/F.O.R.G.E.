#pragma once
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"

namespace forge {
// Complete single-sample 2D surface targets. Depth SRVs use the selected
// backend's typed D32/R32 view; no texture ownership or allocation is introduced.
inline bool complete_surface_view(Diligent::ITextureView* view, Diligent::TEXTURE_FORMAT format,
                                  Diligent::TEXTURE_VIEW_TYPE type) {
    using namespace Diligent;
    if (!view || (type != TEXTURE_VIEW_SHADER_RESOURCE && type != TEXTURE_VIEW_RENDER_TARGET))
        return false;
    const auto& texture = view->GetTexture()->GetDesc();
    const auto& v = view->GetDesc();
    const auto binding =
        type == TEXTURE_VIEW_SHADER_RESOURCE ? BIND_SHADER_RESOURCE : BIND_RENDER_TARGET;
    return texture.Type == RESOURCE_DIM_TEX_2D && texture.ArraySize == 1 &&
           texture.SampleCount == 1 && texture.MipLevels == 1 && texture.Format == format &&
           (texture.BindFlags & binding) != 0 && v.ViewType == type &&
           v.TextureDim == RESOURCE_DIM_TEX_2D && v.MostDetailedMip == 0 &&
           (type != TEXTURE_VIEW_SHADER_RESOURCE || v.NumMipLevels == 1) &&
           v.FirstArraySlice == 0 && v.NumArraySlices == 1 &&
           v.Format == (format == TEX_FORMAT_D32_FLOAT ? TEX_FORMAT_R32_FLOAT : format);
}
} // namespace forge
