#include "sprite_atlas_packing.hpp"
#include "sprite_material_profile.hpp"
#include <algorithm>
#include <bit>
#include <iterator>
#include <numeric>
#include <optional>
#include <tuple>

namespace forge {
namespace {
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Sprite atlas packing cancelled");
}
struct Shelf {
    unsigned y, height, used;
};
std::optional<SpriteAtlasLayout> arrange(std::span<const SpriteFrameRegion> regions,
                                         std::span<const std::size_t> order, unsigned width,
                                         const SpriteAtlasPacking& settings, std::stop_token stop) {
    SpriteAtlasLayout result;
    result.frames.resize(regions.size());
    std::vector<Shelf> shelves;
    unsigned height = 0;
    for (const auto index : order) {
        cancelled(stop);
        const auto& region = regions[index];
        struct Placement {
            std::size_t shelf;
            unsigned width, height;
            bool rotated;
        };
        std::optional<Placement> best;
        for (unsigned orientation = 0; orientation < (settings.allow_rotation ? 2u : 1u);
             ++orientation) {
            if (orientation && region.width == region.height)
                continue;
            const auto pw = (orientation ? region.height : region.width) + 2 * settings.padding;
            const auto ph = (orientation ? region.width : region.height) + 2 * settings.padding;
            for (std::size_t shelf = 0; shelf < shelves.size(); ++shelf) {
                const auto& row = shelves[shelf];
                if (ph > row.height || pw > width - row.used)
                    continue;
                const auto score = [&](const Placement& p) {
                    const auto& candidate = shelves[p.shelf];
                    return std::tuple{width - candidate.used - p.width,
                                      settings.allow_rotation ? candidate.height - p.height : 0u,
                                      p.rotated, p.shelf};
                };
                const Placement candidate{shelf, pw, ph, bool(orientation)};
                if (!best || score(candidate) < score(*best))
                    best = candidate;
            }
        }
        if (!best) {
            for (unsigned orientation = 0; orientation < (settings.allow_rotation ? 2u : 1u);
                 ++orientation) {
                if (orientation && region.width == region.height)
                    continue;
                const auto pw = (orientation ? region.height : region.width) + 2 * settings.padding;
                const auto ph = (orientation ? region.width : region.height) + 2 * settings.padding;
                if (pw > width || ph > settings.max_size - height)
                    continue;
                const Placement candidate{shelves.size(), pw, ph, bool(orientation)};
                if (!best || std::tie(ph, pw, candidate.rotated) <
                                 std::tie(best->height, best->width, best->rotated))
                    best = candidate;
            }
            if (!best)
                return {};
            shelves.push_back({height, best->height, 0});
            height += best->height;
        }
        auto& shelf = shelves[best->shelf];
        auto frame = region;
        frame.x = shelf.used + settings.padding;
        frame.y = shelf.y + settings.padding;
        frame.width = best->width - 2 * settings.padding;
        frame.height = best->height - 2 * settings.padding;
        frame.rotated_clockwise = best->rotated;
        result.frames[index] = frame;
        shelf.used += best->width;
        result.width = std::max(result.width, shelf.used);
    }
    result.height = height;
    if (std::uint64_t(result.width) * height > settings.byte_limit / 4)
        return {};
    return result;
}
} // namespace
void SpriteAtlasPacking::validate() const {
    if (!max_size || max_size > 16384 || padding > 64 || byte_limit < 4 ||
        byte_limit > 512ull * 1024 * 1024)
        throw std::invalid_argument(
            "Atlas needs max_size 1..16384, padding 0..64 and a bounded RGBA8 byte budget");
}
SpriteAtlasLayout pack_sprite_atlas(std::span<const SpriteAtlasSize> sizes,
                                    SpriteAtlasPacking settings, std::stop_token stop) {
    settings.validate();
    cancelled(stop);
    if (sizes.empty() || sizes.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Pack between 1 and 512 Sprite images");
    std::vector<SpriteFrameRegion> regions;
    regions.reserve(sizes.size());
    for (const auto& size : sizes)
        regions.push_back({0, 0, size.width, size.height});
    return pack_sprite_atlas_regions(regions, settings, stop);
}
SpriteAtlasLayout pack_sprite_atlas_regions(std::span<const SpriteFrameRegion> regions,
                                            SpriteAtlasPacking settings, std::stop_token stop) {
    settings.validate();
    cancelled(stop);
    if (regions.empty() || regions.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Pack between 1 and 512 Sprite images");
    unsigned minimum_width = 0;
    std::uint64_t area = 0;
    for (const auto& size : regions) {
        cancelled(stop);
        (void)sprite_frame_canvas(size);
        if (size.x || size.y || size.rotated_clockwise)
            throw std::invalid_argument("Atlas inputs must be unplaced, unrotated image regions");
        if (!size.width || !size.height || size.width > settings.max_size ||
            size.height > settings.max_size)
            throw std::invalid_argument("Sprite image dimensions exceed the atlas profile");
        const auto width = size.width + 2 * settings.padding;
        const auto height = size.height + 2 * settings.padding;
        if (width > settings.max_size || height > settings.max_size)
            throw std::invalid_argument("Sprite image and padding do not fit the atlas maximum");
        minimum_width =
            std::max(minimum_width, settings.allow_rotation ? std::min(width, height) : width);
        area += std::uint64_t(width) * height;
        if (area > settings.byte_limit / 4)
            throw std::invalid_argument("Sprite images and padding exceed the atlas byte budget");
    }
    std::vector<std::size_t> order(regions.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        if (settings.allow_rotation)
            return std::pair{std::max(regions[a].width, regions[a].height),
                             std::min(regions[a].width, regions[a].height)} >
                   std::pair{std::max(regions[b].width, regions[b].height),
                             std::min(regions[b].width, regions[b].height)};
        return std::tie(regions[a].height, regions[a].width) >
               std::tie(regions[b].height, regions[b].width);
    });
    std::optional<SpriteAtlasLayout> best;
    const auto consider = [&](unsigned width) {
        auto candidate = arrange(regions, order, width, settings, stop);
        if (!candidate)
            return;
        const auto score = [](const SpriteAtlasLayout& layout) {
            return std::tuple{std::uint64_t(layout.width) * layout.height,
                              std::max(layout.width, layout.height), layout.width};
        };
        if (!best || score(*candidate) < score(*best))
            best = std::move(candidate);
    };
    for (unsigned width = std::bit_ceil(minimum_width); width < settings.max_size; width *= 2)
        consider(width);
    consider(settings.max_size);
    if (!best)
        throw std::runtime_error(
            "The bounded atlas packing could not fit; increase its maximum or use fewer images");
    return std::move(*best);
}
SpriteFrameRegion sprite_atlas_image_region(const TextureData& image, SpriteAtlasPacking settings,
                                            std::stop_token stop) {
    settings.validate();
    cancelled(stop);
    TextureLimits limits;
    limits.bytes = settings.byte_limit;
    limits.dimension = settings.trim_transparent ? 16384 : settings.max_size;
    validate_texture(image, limits);
    validate_sprite_texture_profile(image);
    SpriteFrameRegion result{0, 0, image.width, image.height};
    if (!settings.trim_transparent)
        return result;
    unsigned left = image.width, top = image.height, right = 0, bottom = 0;
    const auto& pixels = image.subresources.front();
    for (unsigned y = 0; y < image.height; ++y) {
        cancelled(stop);
        for (unsigned x = 0; x < image.width; ++x)
            if (pixels[(std::size_t(y) * image.width + x) * 4 + 3] != std::byte{}) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
    }
    result.source_width = image.width;
    result.source_height = image.height;
    if (!right) {
        result.width = result.height = 1;
        return result;
    }
    left -= left != 0;
    top -= top != 0;
    right += right != image.width;
    bottom += bottom != image.height;
    result.width = right - left;
    result.height = bottom - top;
    result.trim_x = left;
    result.trim_y = top;
    return result;
}
SpriteAtlasImage build_sprite_atlas(std::span<const TextureData* const> images,
                                    SpriteAtlasPacking settings, std::stop_token stop) {
    settings.validate();
    cancelled(stop);
    if (images.empty() || images.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Pack between 1 and 512 Sprite images");
    std::vector<SpriteFrameRegion> regions;
    TextureLimits limits;
    limits.bytes = settings.byte_limit;
    limits.dimension = settings.max_size;
    std::uint64_t source_bytes = 0;
    for (const auto* image : images) {
        cancelled(stop);
        if (!image)
            throw std::invalid_argument("Atlas input image is unavailable");
        const auto region = sprite_atlas_image_region(*image, settings, stop);
        if (!regions.empty() && image->format != images.front()->format)
            throw std::invalid_argument("Atlas images must use the same color transfer format");
        source_bytes += image->subresources.front().size();
        if (source_bytes > settings.byte_limit)
            throw std::invalid_argument("Atlas base-level input pixels exceed the byte budget");
        regions.push_back(region);
    }
    SpriteAtlasImage result;
    result.layout = pack_sprite_atlas_regions(regions, settings, stop);
    auto& texture = result.texture;
    texture.width = result.layout.width;
    texture.height = result.layout.height;
    texture.format = images.front()->format;
    texture.alpha = TextureAlpha::Straight;
    texture.sampler.u = texture.sampler.v = TextureWrap::ClampEdge;
    texture.sampler.max_lod = 0;
    texture.subresources.emplace_back(std::size_t(texture.width) * texture.height * 4, std::byte{});
    auto& pixels = texture.subresources.front();
    for (std::size_t index = 0; index < images.size(); ++index) {
        const auto& source = *images[index];
        const auto& frame = result.layout.frames[index];
        const auto& region = regions[index];
        const auto padding = settings.extrude ? settings.padding : 0;
        if (frame.rotated_clockwise) {
            for (unsigned y = 0; y < frame.height + 2 * padding; ++y) {
                cancelled(stop);
                const auto physical_y = unsigned(
                    std::clamp<std::int64_t>(std::int64_t(y) - padding, 0, frame.height - 1));
                for (unsigned x = 0; x < frame.width + 2 * padding; ++x) {
                    const auto physical_x = unsigned(
                        std::clamp<std::int64_t>(std::int64_t(x) - padding, 0, frame.width - 1));
                    const auto source_x = region.trim_x + physical_y;
                    const auto source_y = region.trim_y + region.height - 1 - physical_x;
                    const auto* pixel = source.subresources.front().data() +
                                        (std::size_t(source_y) * source.width + source_x) * 4;
                    auto* output =
                        pixels.data() + ((std::size_t(frame.y - padding) + y) * texture.width +
                                         frame.x - padding + x) *
                                            4;
                    std::copy_n(pixel, 4, output);
                }
            }
            continue;
        }
        for (unsigned y = 0; y < frame.height + 2 * padding; ++y) {
            cancelled(stop);
            const auto source_y =
                region.trim_y +
                unsigned(std::clamp<std::int64_t>(std::int64_t(y) - padding, 0, region.height - 1));
            const auto* row = source.subresources.front().data() +
                              (std::size_t(source_y) * source.width + region.trim_x) * 4;
            auto* output = pixels.data() +
                           ((std::size_t(frame.y - padding) + y) * texture.width + frame.x) * 4;
            std::copy_n(row, std::size_t(frame.width) * 4, output);
            for (unsigned x = 0; x < padding; ++x) {
                std::copy_n(row, 4, output - std::size_t(x + 1) * 4);
                std::copy_n(row + std::size_t(frame.width - 1) * 4, 4,
                            output + std::size_t(frame.width + x) * 4);
            }
        }
    }
    validate_texture(texture, limits);
    return result;
}
} // namespace forge
