#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include <array>
#include <forge/scene_render_settings.hpp>
namespace forge {
// One camera's transient receiver packet. Matrices consume coordinates relative
// to that camera, not absolute float world positions. No authored state lives here.
struct ShadowLighting {
    using Row = std::array<float, 4>;
    static constexpr unsigned rows_per_light = shadow_cascade_limit * 5 + 2;
    std::array<Row, shadow_light_limit * rows_per_light> values{};
    std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>, shadow_light_limit> maps;
};
} // namespace forge
