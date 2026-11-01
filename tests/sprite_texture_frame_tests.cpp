#include "animation_property_state.hpp"
#include "sprite_frame_mips.hpp"
#include "sprite_render_values.hpp"
#include <iostream>
#include <source_location>

using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Sprite texture frames accepted at " +
                             std::to_string(where.line()));
}
} // namespace
int main() {
    try {
        TextureData source;
        source.width = 16;
        source.height = 8;
        source.format = TextureFormat::RGBA8Srgb;
        source.alpha = TextureAlpha::Straight;
        source.subresources.emplace_back(16 * 8 * 4, std::byte{127});
        source.sprite_frames = {{7, {0, 0, 3, 2, false, 7, 5, 1, 2}},
                                {UINT32_MAX, {8, 2, 2, 3, true, 7, 5, 2, 1}}};
        validate_texture(source);
        SpriteRenderer sprite;
        sprite.texture_frames = true;
        sprite.frame = UINT32_MAX;
        detail::validate_sprite_renderer(sprite);
        const auto document = detail::render_value(sprite);
        require(detail::sprite_renderer_value(document) == sprite,
                "Sprite texture frame intent/key did not round trip");
        auto legacy = detail::render_value(SpriteRenderer{});
        legacy.erase("texture_frames");
        require(!detail::sprite_renderer_value(legacy).texture_frames,
                "Legacy Sprite silently opted into Texture frame keys");
        require(sprite_texture_frame_slot(sprite, source) == 1,
                "Sparse maximum key became an enumeration size");
        const auto resolved = sprite_resolved_frame_bank(sprite, source);
        require(!resolved.texture_frames && resolved.frame == 1 &&
                    resolved.frame_regions.size() == 2 && sprite.frame == UINT32_MAX &&
                    sprite.frame_regions.empty(),
                "Derived frame bank mutated authored values or expanded sparse key space");
        for (auto key : {7u, UINT32_MAX})
            for (unsigned mode : {0u, 1u, 2u})
                for (bool flip_x : {false, true})
                    for (bool flip_y : {false, true}) {
                        sprite.frame = key;
                        sprite.image_mode = mode;
                        sprite.flip_x = flip_x;
                        sprite.flip_y = flip_y;
                        sprite.border_left = sprite.border_right = 1;
                        sprite.border_top = sprite.border_bottom = 1;
                        const auto draw = sprite_draw_values(sprite, source);
                        const auto bank = sprite_resolved_frame_bank(sprite, source);
                        const auto expected = sprite_draw_values(bank, source.width, source.height);
                        require(draw.region.origin == expected.region.origin &&
                                    draw.region.scale == expected.region.scale &&
                                    draw.quad_crop == expected.quad_crop &&
                                    draw.color == expected.color &&
                                    draw.image.mode == expected.image.mode &&
                                    draw.transpose_uv == expected.transpose_uv,
                                "Stable key lost trim, rotation, flips or image fill mapping");
                    }
        sprite.image_mode = 0;
        const auto plan = sprite_frame_mip_plan(sprite, source, 4096);
        require(plan.frames.size() == 2 && plan.unique.size() == 2 &&
                    plan.frames[1].rotated_clockwise && plan.frames[1].canvas_width == 7,
                "Frame-local mip enumeration ignored Texture mapping or expanded sparse keys");
        const auto base = sprite_frame_mip_base(source, plan.frames[1], 4096);
        require(base.width == 7 && base.height == 5 && base.sprite_frames.empty(),
                "Derived mip frame did not restore its logical canvas or leaked atlas identities");
        rejects([&] { sprite_draw_values(sprite, source.width, source.height); });
        auto wrong = sprite;
        wrong.frame = 8;
        rejects([&] { sprite_draw_values(wrong, source); });
        rejects([&] { sprite_frame_mip_plan(wrong, source, 4096); });
        for (unsigned conflict = 0; conflict < 5; ++conflict) {
            wrong = sprite;
            if (conflict == 0)
                wrong.columns = 2;
            if (conflict == 1)
                wrong.spacing_y = 1;
            if (conflict == 2)
                wrong.region_enabled = true;
            if (conflict == 3)
                wrong.region_x = 1;
            if (conflict == 4)
                wrong.frame_regions.push_back({0, 0, 1, 1});
            rejects([&] { detail::validate_sprite_renderer(wrong); });
        }
        wrong = sprite;
        wrong.texture_frames = false;
        rejects([&] { detail::validate_sprite_renderer(wrong); });
        auto missing = source;
        missing.sprite_frames.clear();
        rejects([&] { sprite_draw_values(sprite, missing); });
        SpriteRenderer ordinary;
        const auto whole = sprite_draw_values(ordinary, source);
        require(whole.region.origin == std::array<double, 2>{0, 0} &&
                    whole.region.scale == std::array<double, 2>{1, 1},
                "Ordinary Sprite began interpreting Texture metadata without opting in");
        // Animated frame values remain held and target-dependent. Sparse keys
        // are numeric IDs, never an interpolated sweep through missing frames.
        TransformAnimationClip clip;
        clip.duration = 1000;
        clip.properties = AnimationPropertyState::sprite_mask;
        clip.keys.resize(2);
        clip.keys[0].time = 0;
        clip.keys[1].time = 1000;
        clip.keys[0].properties[unsigned(AnimationProperty::SpriteFrame)] = 7;
        clip.keys[1].properties[unsigned(AnimationProperty::SpriteFrame)] = double(UINT32_MAX);
        AnimationPropertyState state;
        state.sprite = sprite;
        state.validate_clip(clip);
        require(clip.sample_properties(500)[unsigned(AnimationProperty::SpriteFrame)] == 7 &&
                    clip.sample_properties(1000)[unsigned(AnimationProperty::SpriteFrame)] ==
                        double(UINT32_MAX),
                "Sparse Sprite animation interpolated missing keys");
        state.sprite = ordinary;
        rejects([&] { state.validate_clip(clip); });
        validate_animation_property(AnimationProperty::SpriteFrame, double(UINT32_MAX));
        rejects([&] {
            validate_animation_property(AnimationProperty::SpriteFrame, double(UINT32_MAX) + 1);
        });
        std::cout << "Sprite Texture keys, legacy admission, trim/turn/fill, mips and held "
                     "animation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
