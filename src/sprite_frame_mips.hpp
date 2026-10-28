#pragma once
#include "sprite_material_profile.hpp"
#include <algorithm>
#include <compare>
#include <forge/render_components.hpp>
#include <set>
#include <stop_token>
#include <vector>

namespace forge {
inline constexpr std::uint64_t sprite_frame_mip_payload_limit = 256ull * 1024 * 1024;
inline constexpr unsigned sprite_frame_mip_pixel_bytes = 8; // Linear premultiplied RGBA16F.
// Normalized physical pixels and original logical canvas. These are private
// derived-resource inputs, not a new asset, authored frame ID or source table.
struct SpriteFrameMipKey {
    std::uint32_t x{}, y{}, width{}, height{}, canvas_width{}, canvas_height{}, trim_x{}, trim_y{};
    bool rotated_clockwise{};
    auto operator<=>(const SpriteFrameMipKey&) const = default;
};
struct SpriteFrameMipPlan {
    std::vector<SpriteFrameMipKey> frames; // Authored order, including duplicates.
    std::vector<SpriteFrameMipKey> unique; // Canonical physical resource order.
    std::uint64_t bytes = 0;               // Complete unique mip-chain payload, not peak RAM/VRAM.
};
inline SpriteFrameCanvas sprite_mip_canvas(const SpriteFrameMipKey& key) {
    return sprite_frame_canvas({key.x, key.y, key.width, key.height, key.rotated_clockwise,
                                key.canvas_width, key.canvas_height, key.trim_x, key.trim_y});
}
inline std::uint64_t sprite_frame_mip_bytes(const SpriteFrameMipKey& key, std::uint64_t limit) {
    limit = std::min(limit, sprite_frame_mip_payload_limit);
    const auto canvas = sprite_mip_canvas(key);
    std::uint64_t bytes = 0;
    auto width = canvas.width, height = canvas.height;
    for (;;) {
        const auto level = std::uint64_t(width) * height * sprite_frame_mip_pixel_bytes;
        if (level > limit - bytes)
            throw std::invalid_argument("Sprite frame mip chain exceeds its pixel payload budget");
        bytes += level;
        if (width == 1 && height == 1)
            return bytes;
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
}
inline SpriteFrameMipPlan sprite_frame_mip_plan(const SpriteRenderer& sprite,
                                                const TextureData& source,
                                                std::uint64_t byte_limit) {
    byte_limit = std::min(byte_limit, sprite_frame_mip_payload_limit);
    validate_sprite_texture_profile(source);
    validate_texture(source);
    const SpriteGridLayout grid{sprite.columns,  sprite.rows,      sprite.margin_x,
                                sprite.margin_y, sprite.spacing_x, sprite.spacing_y};
    const auto count = sprite_frame_count(grid, sprite.frame_regions);
    if (!count || count > sprite_frame_region_limit || sprite.frame >= count)
        throw std::invalid_argument(
            "Frame mip filtering needs a valid bank or grid of at most512 frames");
    const auto crop =
        sprite.region_enabled
            ? std::optional<SpritePixelRegion>{{sprite.region_x, sprite.region_y,
                                                sprite.region_width, sprite.region_height}}
            : std::nullopt;
    SpriteFrameMipPlan result;
    result.frames.reserve(std::size_t(count));
    const auto area =
        sprite.frame_regions.empty()
            ? SpritePixelRegion{}
            : sprite_frame_bank_area(source.width, source.height, crop, sprite.frame_regions);
    std::set<SpriteFrameMipKey> unique;
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto* frame =
            sprite.frame_regions.empty() ? nullptr : &sprite.frame_regions[std::size_t(index)];
        const auto region =
            frame ? SpritePixelRegion{area.x + frame->x, area.y + frame->y, frame->width,
                                      frame->height}
                  : sprite_grid_region(source.width, source.height, grid, index, crop);
        const auto canvas =
            frame
                ? sprite_frame_canvas(*frame)
                : SpriteFrameCanvas{region.width, region.height, region.width, region.height, 0, 0};
        const SpriteFrameMipKey key{region.x,      region.y,     region.width,
                                    region.height, canvas.width, canvas.height,
                                    canvas.x,      canvas.y,     frame && frame->rotated_clockwise};
        if (unique.insert(key).second)
            result.bytes += sprite_frame_mip_bytes(key, byte_limit - result.bytes);
        result.frames.push_back(key);
    }
    result.unique.assign(unique.begin(), unique.end());
    return result;
}
inline SpriteFrameCanvas validate_sprite_frame_mip_input(const TextureData& source,
                                                         const SpriteFrameMipKey& key,
                                                         std::uint64_t byte_limit) {
    validate_sprite_texture_profile(source);
    validate_texture(source);
    const auto canvas = sprite_mip_canvas(key);
    (void)sprite_frame_mip_bytes(key, byte_limit);
    if (key.x >= source.width || key.y >= source.height || key.width > source.width - key.x ||
        key.height > source.height - key.y)
        throw std::invalid_argument("Sprite mip frame exceeds its selected source pixels");
    return canvas;
}
struct SpriteFrameMipInput {
    TextureData pixels;
    SpriteFrameMipKey frame;
};
// Copy only retained physical pixels while the caller can access its source
// lease on the owner thread. Workers receive this owned value, never a lease or
// a pointer into a scope that close/unload can revoke.
inline SpriteFrameMipInput copy_sprite_frame_mip_input(const TextureData& source,
                                                       const SpriteFrameMipKey& key,
                                                       std::uint64_t byte_limit,
                                                       std::stop_token stop = {}) {
    (void)validate_sprite_frame_mip_input(source, key, byte_limit);
    if (stop.stop_requested())
        throw std::runtime_error("Sprite frame mip preparation cancelled");
    SpriteFrameMipInput result;
    result.frame = key;
    result.frame.x = result.frame.y = 0;
    auto& image = result.pixels;
    image.width = key.width;
    image.height = key.height;
    image.format = source.format;
    image.alpha = source.alpha;
    image.sampler = source.sampler;
    auto& pixels = image.subresources.emplace_back(std::size_t(key.width) * key.height * 4);
    for (unsigned y = 0; y < key.height; ++y) {
        if (stop.stop_requested())
            throw std::runtime_error("Sprite frame mip preparation cancelled");
        const auto* from = source.subresources.front().data() +
                           (std::size_t(key.y + y) * source.width + key.x) * 4;
        std::copy_n(from, std::size_t(key.width) * 4,
                    pixels.data() + std::size_t(y) * key.width * 4);
    }
    return result;
}
// Restore each logical canvas before filtering: transparent margins and stored
// quarter turns must not change alpha coverage or mix a neighboring atlas cell.
inline TextureData sprite_frame_mip_base(const TextureData& source, const SpriteFrameMipKey& key,
                                         std::uint64_t byte_limit, std::stop_token stop = {}) {
    const auto canvas = validate_sprite_frame_mip_input(source, key, byte_limit);
    if (stop.stop_requested())
        throw std::runtime_error("Sprite frame mip preparation cancelled");
    TextureData result;
    result.width = canvas.width;
    result.height = canvas.height;
    result.format = source.format;
    result.alpha = canvas.trimmed() ? TextureAlpha::Straight : source.alpha;
    result.sampler = source.sampler;
    result.subresources.emplace_back(std::size_t(canvas.width) * canvas.height * 4, std::byte{});
    for (unsigned y = 0; y < canvas.retained_height; ++y) {
        if (stop.stop_requested())
            throw std::runtime_error("Sprite frame mip preparation cancelled");
        for (unsigned x = 0; x < canvas.retained_width; ++x) {
            const auto sx = key.x + (key.rotated_clockwise ? canvas.retained_height - 1 - y : x);
            const auto sy = key.y + (key.rotated_clockwise ? x : y);
            const auto* from =
                source.subresources.front().data() + (std::size_t(sy) * source.width + sx) * 4;
            auto* to = result.subresources.front().data() +
                       (std::size_t(canvas.y + y) * canvas.width + canvas.x + x) * 4;
            std::copy_n(from, 4, to);
        }
    }
    return result;
}
// Native filtering uses the pinned 32-bit float API. Immutable output uses
// linear premultiplied half floats; callers retain the original source lease.
TextureData prepare_sprite_frame_mips(const TextureData&, const SpriteFrameMipKey&,
                                      std::uint64_t byte_limit, std::stop_token = {});
} // namespace forge
