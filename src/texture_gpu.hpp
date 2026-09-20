#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <forge/texture_asset.hpp>
namespace forge {
// Device-thread upload of fully admitted immutable cooked bytes. No importer or
// source parser. Returned native resource owns its data beyond the CPU lease.
Diligent::RefCntAutoPtr<Diligent::ITexture> upload_texture(Diligent::IRenderDevice*,
                                                           const TextureData&);
// Sampler ownership is per material binding, not baked into a shared asset view.
Diligent::RefCntAutoPtr<Diligent::ISampler> upload_sampler(Diligent::IRenderDevice*,
                                                           const SamplerState&);
} // namespace forge
