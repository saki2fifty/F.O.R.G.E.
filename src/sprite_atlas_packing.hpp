#pragma once
#include <forge/sprite_frames.hpp>
#include <forge/texture_asset.hpp>
#include <span>
#include <stop_token>

namespace forge {
// Private authoring transform. No files, identities, catalog or GPU ownership.
struct SpriteAtlasSize {
    unsigned width = 0, height = 0;
};
struct SpriteAtlasPacking {
    unsigned max_size = 4096, padding = 1;
    std::size_t byte_limit = 64 * 1024 * 1024;
    bool extrude = true;
    bool trim_transparent = false, allow_rotation = false;
    void validate() const;
};
struct SpriteAtlasLayout {
    unsigned width = 0, height = 0;
    // Input order defines frame numbers; packing order is independent.
    std::vector<SpriteFrameRegion> frames;
    bool operator==(const SpriteAtlasLayout&) const = default;
};
struct SpriteAtlasImage {
    SpriteAtlasLayout layout;
    TextureData texture;
};
// Bounded independent shelf search. Deterministic, without an optimality claim.
SpriteAtlasLayout pack_sprite_atlas(std::span<const SpriteAtlasSize>, SpriteAtlasPacking = {},
                                    std::stop_token = {});
// Shared original-image scan and region layout. Alpha-zero edges may be removed;
// one available transparent ring stays for linear filtering. Original canvas and
// retained offset survive packing; all-transparent images retain one real texel.
SpriteFrameRegion sprite_atlas_image_region(const TextureData&, SpriteAtlasPacking = {},
                                            std::stop_token = {});
SpriteAtlasLayout pack_sprite_atlas_regions(std::span<const SpriteFrameRegion>,
                                            SpriteAtlasPacking = {}, std::stop_token = {});
// Copies admitted base-level RGBA8 color pixels unchanged; all images must have
// the same transfer format. Padding can extrude the nearest edge; gaps stay clear.
// Borrowed immutable images must outlive this call. No native decoder is invoked.
SpriteAtlasImage build_sprite_atlas(std::span<const TextureData* const>, SpriteAtlasPacking = {},
                                    std::stop_token = {});
} // namespace forge
