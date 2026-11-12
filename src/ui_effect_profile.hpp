#pragma once
#include <array>
#include <forge/asset_ref.hpp>

namespace forge::ui_effect_detail {
inline constexpr unsigned profile_version = 2;
struct MaterialEffect {
    AssetRef<MaterialAsset> material;
    std::array<float, 2> dimensions{};
};
} // namespace forge::ui_effect_detail
