#pragma once
#include <cstdint>
#include <forge/texture_asset.hpp>
#include <optional>
#include <stdexcept>
namespace forge {
// Transient pipeline specialization, never a durable asset/component identity.
enum class MeshDrawProfile {
    Mesh,
    Sprite,
    SpriteMipNearest,
    SpriteMipLinear,
    MeshTemporal,
    MeshIndirect,
    MeshTemporalIndirect,
    MeshReflection,
    MeshTemporalReflection,
    MeshNormal,
    MeshTemporalNormal
};
inline bool temporal_mesh_profile(MeshDrawProfile profile) {
    return profile == MeshDrawProfile::MeshTemporal ||
           profile == MeshDrawProfile::MeshTemporalIndirect ||
           profile == MeshDrawProfile::MeshTemporalReflection ||
           profile == MeshDrawProfile::MeshTemporalNormal;
}
inline bool indirect_mesh_profile(MeshDrawProfile profile) {
    return profile == MeshDrawProfile::MeshIndirect ||
           profile == MeshDrawProfile::MeshTemporalIndirect ||
           profile == MeshDrawProfile::MeshReflection ||
           profile == MeshDrawProfile::MeshTemporalReflection ||
           profile == MeshDrawProfile::MeshNormal || profile == MeshDrawProfile::MeshTemporalNormal;
}
inline bool normal_mesh_profile(MeshDrawProfile profile) {
    return profile == MeshDrawProfile::MeshNormal || profile == MeshDrawProfile::MeshTemporalNormal;
}
inline bool reflection_mesh_profile(MeshDrawProfile profile) {
    return profile == MeshDrawProfile::MeshReflection ||
           profile == MeshDrawProfile::MeshTemporalReflection;
}
inline bool sprite_draw_profile(MeshDrawProfile profile) {
    return profile == MeshDrawProfile::Sprite || profile == MeshDrawProfile::SpriteMipNearest ||
           profile == MeshDrawProfile::SpriteMipLinear;
}
inline std::optional<TextureFilter> sprite_draw_mip_filter(MeshDrawProfile profile) {
    if (profile == MeshDrawProfile::SpriteMipNearest)
        return TextureFilter::Nearest;
    if (profile == MeshDrawProfile::SpriteMipLinear)
        return TextureFilter::Linear;
    return {};
}
inline MeshDrawProfile sprite_draw_profile(std::uint32_t mode) {
    switch (mode) {
    case 0:
        return MeshDrawProfile::Sprite;
    case 1:
        return MeshDrawProfile::SpriteMipNearest;
    case 2:
        return MeshDrawProfile::SpriteMipLinear;
    default:
        throw std::invalid_argument("Unsupported Sprite mip filtering mode");
    }
}
} // namespace forge
