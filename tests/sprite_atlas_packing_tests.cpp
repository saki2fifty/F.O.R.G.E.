#include "sprite_atlas_packing.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected invalid atlas input to reject");
}
void geometry() {
    const std::array<SpriteAtlasSize, 5> sizes{{{3, 2}, {1, 4}, {2, 1}, {3, 2}, {7, 1}}};
    const SpriteAtlasPacking settings{16, 1, 1024, true};
    const auto layout = pack_sprite_atlas(sizes, settings);
    check(layout == pack_sprite_atlas(sizes, settings), "Atlas layout is not deterministic");
    check(layout.frames.size() == sizes.size() && layout.width <= 16 && layout.height <= 16 &&
              std::uint64_t(layout.width) * layout.height * 4 <= settings.byte_limit,
          "Atlas dimensions or byte budget escaped the profile");
    std::vector<unsigned> covered(std::size_t(layout.width) * layout.height);
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const auto& frame = layout.frames[index];
        check(frame.width == sizes[index].width && frame.height == sizes[index].height,
              "Packing changed frame size/order instead of mapping back to input order");
        check(frame.x >= 1 && frame.y >= 1 && frame.x + frame.width + 1 <= layout.width &&
                  frame.y + frame.height + 1 <= layout.height,
              "Packed frame/padding is outside the atlas");
        for (unsigned y = frame.y - 1; y < frame.y + frame.height + 1; ++y)
            for (unsigned x = frame.x - 1; x < frame.x + frame.width + 1; ++x)
                check(++covered[std::size_t(y) * layout.width + x] == 1,
                      "Packed image padding overlaps another image");
    }
    const std::array<SpriteAtlasSize, 1> exact{{{11, 7}}};
    const auto odd = pack_sprite_atlas(exact, {13, 1, 13 * 9 * 4, true});
    check(odd.width == 13 && odd.height == 9,
          "Non-power-of-two maximum or exact byte budget was ignored");
    std::vector<SpriteAtlasSize> many(sprite_frame_region_limit, {1, 1});
    check(pack_sprite_atlas(many, {32, 0, 32 * 32 * 4, false}).frames.size() == many.size(),
          "The supported512-frame bound cannot be packed");
    many.push_back({1, 1});
    rejects([&] { pack_sprite_atlas(many); });
    rejects([&] { pack_sprite_atlas({}); });
    for (const auto size :
         {SpriteAtlasSize{0, 1}, SpriteAtlasSize{1, 0},
          SpriteAtlasSize{std::numeric_limits<unsigned>::max(), 1}, SpriteAtlasSize{16, 1}})
        rejects([&] { pack_sprite_atlas(std::span(&size, 1), settings); });
    rejects([&] { pack_sprite_atlas(sizes, {16, 1, 16, true}); });
    rejects([&] { pack_sprite_atlas(sizes, {0, 1, 1024, true}); });
    rejects([&] { pack_sprite_atlas(sizes, {16, 65, 1024, true}); });
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { pack_sprite_atlas(sizes, settings, stop.get_token()); });
}
TextureData image(unsigned width, unsigned height, unsigned seed, TextureFormat format) {
    TextureData result;
    result.width = width;
    result.height = height;
    result.format = format;
    result.alpha = TextureAlpha::Straight;
    result.subresources.emplace_back(std::size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const auto at = (std::size_t(y) * width + x) * 4;
            for (unsigned channel = 0; channel < 4; ++channel)
                result.subresources[0][at + channel] =
                    std::byte((seed + x * 31 + y * 17 + channel * 53) % 256);
        }
    return result;
}
void pixels() {
    for (auto format : {TextureFormat::RGBA8, TextureFormat::RGBA8Srgb}) {
        const auto a = image(3, 2, 0, format), b = image(1, 4, 40, format),
                   c = image(2, 1, 80, format);
        const std::array<const TextureData*, 3> inputs{&a, &b, &c};
        for (bool extrude : {false, true}) {
            const auto result = build_sprite_atlas(inputs, {16, 2, 16 * 16 * 4, extrude});
            const auto& texture = result.texture;
            check(texture.mips == 1 && texture.format == format &&
                      texture.alpha == TextureAlpha::Straight,
                  "Packing changed transfer/alpha semantics or fabricated atlas mips");
            check(decode_texture(encode_texture(texture)).subresources == texture.subresources,
                  "Packed image failed existing cooked texture round trip");
            std::vector<bool> written(std::size_t(texture.width) * texture.height);
            for (std::size_t index = 0; index < inputs.size(); ++index) {
                const auto& source = *inputs[index];
                const auto& frame = result.layout.frames[index];
                for (int y = -2; y < int(source.height) + 2; ++y)
                    for (int x = -2; x < int(source.width) + 2; ++x) {
                        const bool inside =
                            x >= 0 && y >= 0 && x < int(source.width) && y < int(source.height);
                        const auto destination = std::size_t(int(frame.y) + y) * texture.width +
                                                 unsigned(int(frame.x) + x);
                        written[destination] = true;
                        const auto sx = unsigned(std::clamp(x, 0, int(source.width) - 1));
                        const auto sy = unsigned(std::clamp(y, 0, int(source.height) - 1));
                        for (unsigned channel = 0; channel < 4; ++channel) {
                            const auto expected =
                                inside || extrude
                                    ? source.subresources[0][(std::size_t(sy) * source.width + sx) *
                                                                 4 +
                                                             channel]
                                    : std::byte{};
                            check(texture.subresources[0][destination * 4 + channel] == expected,
                                  "Atlas changed a source texel, alpha or padding edge/corner");
                        }
                    }
            }
            for (std::size_t pixel = 0; pixel < written.size(); ++pixel)
                if (!written[pixel])
                    for (unsigned channel = 0; channel < 4; ++channel)
                        check(texture.subresources[0][pixel * 4 + channel] == std::byte{},
                              "Unoccupied atlas pixels were not transparent zero");
        }
    }
    auto valid = image(2, 2, 0, TextureFormat::RGBA8Srgb);
    valid.mips = 2;
    valid.subresources.emplace_back(4, std::byte{255});
    const std::array<const TextureData*, 1> one{&valid};
    check(build_sprite_atlas(one).texture.mips == 1,
          "Source mips were misrepresented as atlas mips");
    auto linear = valid;
    linear.format = TextureFormat::RGBA8;
    const std::array<const TextureData*, 2> mixed{&valid, &linear};
    rejects([&] { build_sprite_atlas(mixed); });
    const std::array<const TextureData*, 1> absent{nullptr};
    rejects([&] { build_sprite_atlas(absent); });
    auto invalid = valid;
    invalid.alpha = TextureAlpha::Premultiplied;
    const std::array<const TextureData*, 1> bad{&invalid};
    rejects([&] { build_sprite_atlas(bad); });
    invalid = valid;
    invalid.subresources[0].pop_back();
    rejects([&] { build_sprite_atlas(bad); });
    invalid = valid;
    invalid.semantic = TextureSemantic::Data;
    rejects([&] { build_sprite_atlas(bad); });
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { build_sprite_atlas(one, {}, stop.get_token()); });
}
} // namespace
int main() {
    try {
        geometry();
        pixels();
        std::cout << "Independent bounded atlas geometry, ordering, budgets and exact "
                     "RGBA8/alpha/padding pixels passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
