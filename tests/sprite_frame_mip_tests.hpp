#pragma once
#include "sprite_frame_mips.hpp"
#include <array>

inline void test_sprite_frame_mip_pixels() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto rejects = [&](auto run) {
        bool rejected = false;
        try {
            run();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid frame mip pixels or budget admitted");
    };
    TextureData source;
    source.width = 8;
    source.height = 4;
    source.format = TextureFormat::RGBA8Srgb;
    source.alpha = TextureAlpha::Straight;
    auto& pixels = source.subresources.emplace_back(8 * 4 * 4);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            const auto at = (y * 8 + x) * 4;
            pixels[at] = std::byte(x * 29);
            pixels[at + 1] = std::byte(y * 57);
            pixels[at + 2] = std::byte((x + y) * 17);
            pixels[at + 3] = std::byte{128};
        }
    SpriteRenderer sprite;
    sprite.columns = sprite.rows = 2;
    sprite.frame = 3;
    const auto plan = sprite_frame_mip_plan(sprite, source, 352);
    check(plan.bytes == 352 && plan.frames.size() == 4 && plan.unique.size() == 4 &&
              plan.frames[3] == SpriteFrameMipKey{4, 2, 4, 2, 4, 2, 0, 0, false},
          "Grid frame normalization or complete mip budget differs");
    rejects([&] { sprite_frame_mip_plan(sprite, source, 351); });
    const auto selected = sprite_frame_mip_base(source, plan.frames[3], 88);
    check(selected.width == 4 && selected.height == 2 && selected.format == source.format &&
              selected.alpha == source.alpha,
          "Frame base changed dimensions, transfer or alpha declaration");
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            const auto at = (y * 4 + x) * 4;
            check(selected.subresources[0][at] == std::byte((x + 4) * 29) &&
                      selected.subresources[0][at + 1] == std::byte((y + 2) * 57) &&
                      selected.subresources[0][at + 2] == std::byte((x + y + 6) * 17) &&
                      selected.subresources[0][at + 3] == std::byte{128},
                  "Selected frame copied neighboring source pixels");
        }
    sprite.region_enabled = true;
    sprite.region_x = 2;
    sprite.region_y = 1;
    sprite.region_width = 4;
    sprite.region_height = 2;
    sprite.frame = 1;
    sprite.frame_regions = {{0, 0, 2, 2}, {0, 0, 2, 2}};
    const auto duplicate = sprite_frame_mip_plan(sprite, source, 40);
    check(duplicate.frames.size() == 2 && duplicate.unique.size() == 1 && duplicate.bytes == 40 &&
              duplicate.frames[0] == SpriteFrameMipKey{2, 1, 2, 2, 2, 2, 0, 0, false} &&
              duplicate.frames[0] == duplicate.frames[1],
          "Duplicate/crop frames lost order or charged duplicate native pixels");
    rejects([&] { sprite_frame_mip_plan(sprite, source, 39); });
    auto bad = sprite;
    bad.frame_regions[0].x = 100;
    rejects([&] { sprite_frame_mip_plan(bad, source, 1000); });
    bad = sprite;
    bad.frame = 2;
    rejects([&] { sprite_frame_mip_plan(bad, source, 1000); });
    bad = {};
    bad.columns = bad.rows = 32;
    rejects([&] { sprite_frame_mip_plan(bad, source, 1000000); });
    auto incompatible = source;
    incompatible.alpha = TextureAlpha::Premultiplied;
    rejects([&] { sprite_frame_mip_plan(sprite, incompatible, 1000); });
    incompatible = source;
    incompatible.subresources.front().pop_back();
    rejects([&] { sprite_frame_mip_plan(sprite, incompatible, 1000); });

    TextureData atlas;
    atlas.width = atlas.height = 8;
    atlas.format = TextureFormat::RGBA8Srgb;
    atlas.alpha = TextureAlpha::Straight;
    auto& data = atlas.subresources.emplace_back(8 * 8 * 4, std::byte{255});
    // A clockwise-stored 3x2 image: original rows [1,2,3] / [4,5,6].
    // Its physical 2x3 rows are [4,1] / [5,2] / [6,3].
    constexpr std::array physical{4u, 1u, 5u, 2u, 6u, 3u};
    for (unsigned i = 0; i < physical.size(); ++i) {
        const auto at = ((3 + i / 2) * 8 + 2 + i % 2) * 4;
        data[at] = std::byte(physical[i] * 31);
        data[at + 1] = std::byte{13};
        data[at + 2] = std::byte{37};
        data[at + 3] = std::byte{160};
    }
    sprite = {};
    sprite.frame_regions = {{2, 3, 2, 3, true, 7, 6, 1, 2}};
    const auto turned = sprite_frame_mip_plan(sprite, atlas, 416);
    const auto original = atlas.subresources;
    const auto canvas = sprite_frame_mip_base(atlas, turned.frames[0], 416);
    const auto input = copy_sprite_frame_mip_input(atlas, turned.frames[0], 416);
    check(input.pixels.width == 2 && input.pixels.height == 3 && input.pixels.byte_size() == 24 &&
              input.frame.x == 0 && input.frame.y == 0 &&
              sprite_frame_mip_base(input.pixels, input.frame, 416).subresources ==
                  canvas.subresources,
          "Owned mip input copied the whole atlas or changed its rebased logical canvas");
    check(canvas.width == 7 && canvas.height == 6 && turned.bytes == 416 &&
              atlas.subresources == original,
          "Frame canvas budget or immutable source changed");
    for (unsigned y = 0; y < 6; ++y)
        for (unsigned x = 0; x < 7; ++x) {
            const auto at = (y * 7 + x) * 4;
            if (x >= 1 && x < 4 && y >= 2 && y < 4) {
                const auto value = (y - 2) * 3 + x;
                check(canvas.subresources[0][at] == std::byte(value * 31) &&
                          canvas.subresources[0][at + 1] == std::byte{13} &&
                          canvas.subresources[0][at + 2] == std::byte{37} &&
                          canvas.subresources[0][at + 3] == std::byte{160},
                      "Stored quarter turn or trim placement reconstructed incorrectly");
            } else
                for (unsigned lane = 0; lane < 4; ++lane)
                    check(canvas.subresources[0][at + lane] == std::byte{},
                          "Transparent canvas contains an unrelated atlas texel");
        }
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { sprite_frame_mip_base(atlas, turned.frames[0], 416, stop.get_token()); });
    auto outside = turned.frames[0];
    outside.x = 8;
    rejects([&] { sprite_frame_mip_base(atlas, outside, 416); });
}
