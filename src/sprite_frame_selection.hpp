#pragma once
#include "render_values.hpp"
#include <algorithm>
#include <forge/texture_asset.hpp>

namespace forge {
inline std::uint32_t sprite_frame_key_slot(std::span<const SpriteFrameEntry> frames,
                                           std::uint32_t key) {
    if (frames.empty() || frames.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Sprite Texture frames need a published Sprite atlas");
    const auto found = std::lower_bound(frames.begin(), frames.end(), key,
                                        [](const auto& entry, auto key) { return entry.id < key; });
    if (found == frames.end() || found->id != key)
        throw std::invalid_argument(
            "Sprite frame key is missing from the selected Texture revision");
    return std::uint32_t(found - frames.begin());
}
// Derived slot inside one immutable Texture lease; never a persistent frame ID.
inline std::uint32_t sprite_texture_frame_slot(const SpriteRenderer& sprite,
                                               const TextureData& texture) {
    return sprite.texture_frames ? sprite_frame_key_slot(texture.sprite_frames, sprite.frame)
                                 : sprite.frame;
}
// Bounded, detached adapter for consumers that enumerate a whole frame bank
// (mip preparation and the picker). Authored/requested values remain untouched.
inline SpriteRenderer sprite_resolved_frame_bank(const SpriteRenderer& sprite,
                                                 const TextureData& texture) {
    detail::validate_sprite_renderer(sprite);
    if (!sprite.texture_frames)
        return sprite;
    auto result = sprite;
    result.frame = sprite_texture_frame_slot(sprite, texture);
    result.texture_frames = false;
    result.frame_regions.reserve(texture.sprite_frames.size());
    for (const auto& entry : texture.sprite_frames)
        result.frame_regions.push_back(entry.region);
    detail::validate_sprite_renderer(result);
    return result;
}
} // namespace forge
