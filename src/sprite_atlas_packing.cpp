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
std::optional<SpriteAtlasLayout> arrange(std::span<const SpriteAtlasSize> sizes,
                                         std::span<const std::size_t> order, unsigned width,
                                         const SpriteAtlasPacking& settings, std::stop_token stop) {
    SpriteAtlasLayout result;
    result.frames.resize(sizes.size());
    std::vector<Shelf> shelves;
    unsigned height = 0;
    for (const auto index : order) {
        cancelled(stop);
        const auto padded_width = sizes[index].width + 2 * settings.padding;
        const auto padded_height = sizes[index].height + 2 * settings.padding;
        auto best = shelves.end();
        for (auto shelf = shelves.begin(); shelf != shelves.end(); ++shelf)
            if (shelf->height >= padded_height && padded_width <= width - shelf->used &&
                (best == shelves.end() || shelf->used > best->used))
                best = shelf;
        if (best == shelves.end()) {
            if (padded_width > width || padded_height > settings.max_size - height)
                return {};
            shelves.push_back({height, padded_height, 0});
            height += padded_height;
            best = std::prev(shelves.end());
        }
        result.frames[index] = {best->used + settings.padding, best->y + settings.padding,
                                sizes[index].width, sizes[index].height};
        best->used += padded_width;
        result.width = std::max(result.width, best->used);
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
    unsigned minimum_width = 0;
    std::uint64_t area = 0;
    for (const auto& size : sizes) {
        cancelled(stop);
        if (!size.width || !size.height || size.width > settings.max_size ||
            size.height > settings.max_size)
            throw std::invalid_argument("Sprite image dimensions exceed the atlas profile");
        const auto width = size.width + 2 * settings.padding;
        const auto height = size.height + 2 * settings.padding;
        if (width > settings.max_size || height > settings.max_size)
            throw std::invalid_argument("Sprite image and padding do not fit the atlas maximum");
        minimum_width = std::max(minimum_width, width);
        area += std::uint64_t(width) * height;
        if (area > settings.byte_limit / 4)
            throw std::invalid_argument("Sprite images and padding exceed the atlas byte budget");
    }
    std::vector<std::size_t> order(sizes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return std::tie(sizes[a].height, sizes[a].width) >
               std::tie(sizes[b].height, sizes[b].width);
    });
    std::optional<SpriteAtlasLayout> best;
    const auto consider = [&](unsigned width) {
        auto candidate = arrange(sizes, order, width, settings, stop);
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
SpriteAtlasImage build_sprite_atlas(std::span<const TextureData* const> images,
                                    SpriteAtlasPacking settings, std::stop_token stop) {
    settings.validate();
    cancelled(stop);
    if (images.empty() || images.size() > sprite_frame_region_limit)
        throw std::invalid_argument("Pack between 1 and 512 Sprite images");
    std::vector<SpriteAtlasSize> sizes;
    TextureLimits limits;
    limits.bytes = settings.byte_limit;
    limits.dimension = settings.max_size;
    std::uint64_t source_bytes = 0;
    for (const auto* image : images) {
        cancelled(stop);
        if (!image)
            throw std::invalid_argument("Atlas input image is unavailable");
        validate_texture(*image, limits);
        validate_sprite_texture_profile(*image);
        if (!sizes.empty() && image->format != images.front()->format)
            throw std::invalid_argument("Atlas images must use the same color transfer format");
        source_bytes += image->subresources.front().size();
        if (source_bytes > settings.byte_limit)
            throw std::invalid_argument("Atlas base-level input pixels exceed the byte budget");
        sizes.push_back({image->width, image->height});
    }
    SpriteAtlasImage result;
    result.layout = pack_sprite_atlas(sizes, settings, stop);
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
        const auto padding = settings.extrude ? settings.padding : 0;
        for (unsigned y = 0; y < frame.height + 2 * padding; ++y) {
            cancelled(stop);
            const auto source_y =
                unsigned(std::clamp<std::int64_t>(std::int64_t(y) - padding, 0, source.height - 1));
            const auto* row =
                source.subresources.front().data() + std::size_t(source_y) * source.width * 4;
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
