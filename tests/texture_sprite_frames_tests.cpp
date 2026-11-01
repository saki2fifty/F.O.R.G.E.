#include "cooked_envelope.hpp"
#include <forge/texture_asset.hpp>
#include <iostream>
using namespace forge;
namespace {
using Json = nlohmann::json;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid texture frame mapping was admitted");
}
constexpr std::array<std::byte, 8> magic{std::byte{'F'}, std::byte{'R'}, std::byte{'G'},
                                         std::byte{'T'}, std::byte{'E'}, std::byte{'X'},
                                         std::byte{0},   std::byte{0}};
TextureData image() {
    TextureData result;
    result.width = result.height = 4;
    result.format = TextureFormat::RGBA8Srgb;
    result.alpha = TextureAlpha::Opaque;
    result.subresources.emplace_back(64, std::byte{255});
    return result;
}
Json legacy_metadata() {
    return {{"dimension", 0}, {"format", 3},
            {"semantic", 0},  {"alpha", 0},
            {"width", 4},     {"height", 4},
            {"depth", 1},     {"layers", 1},
            {"mips", 1},      {"sampler", SamplerState{}}};
}
void legacy_and_roundtrip() {
    const auto plain = image();
    const auto legacy = asset_detail::encode_envelope(legacy_metadata(), plain.subresources.front(),
                                                      magic, 4 * 1024 * 1024);
    check(encode_texture(plain) == legacy && decode_texture(legacy).sprite_frames.empty(),
          "Ordinary texture encoding or legacy frame absence changed");
    auto mapped = plain;
    mapped.sprite_frames.reserve(8);
    mapped.sprite_frames.push_back({7, {0, 0, 2, 4, false, 6, 4, 1, 0}});
    mapped.sprite_frames.push_back({UINT32_MAX, {2, 0, 2, 4, true, 4, 6, 0, 2}});
    const auto bytes = encode_texture(mapped);
    const auto restored = decode_texture(bytes);
    check(restored.sprite_frames == mapped.sprite_frames &&
              restored.subresources == plain.subresources && restored.sampler == plain.sampler,
          "Frame keys, trimmed/turned canvas or exact pixels changed in cooked roundtrip");
    check(mapped.byte_size() == plain.byte_size() &&
              mapped.resident_bytes() >= plain.resident_bytes() + 8 * sizeof(SpriteFrameEntry),
          "Frame metadata was omitted from retained accounting or charged as pixels");
    check(plain.sprite_frames.empty(), "Adding mapping changed the original value");
    rejects([&] {
        auto invalid = mapped;
        invalid.sprite_frames[1].id = 7;
        encode_texture(invalid);
    });
    rejects([&] {
        auto invalid = mapped;
        std::swap(invalid.sprite_frames[0], invalid.sprite_frames[1]);
        encode_texture(invalid);
    });
    rejects([&] {
        auto invalid = mapped;
        invalid.sprite_frames[0].region.x = 3;
        encode_texture(invalid);
    });
    rejects([&] {
        auto invalid = mapped;
        invalid.sprite_frames[0].region.source_width = 1;
        encode_texture(invalid);
    });
    rejects([&] {
        auto invalid = mapped;
        invalid.alpha = TextureAlpha::Premultiplied;
        encode_texture(invalid);
    });
    rejects([&] {
        auto invalid = mapped;
        invalid.format = TextureFormat::RGBA8;
        encode_texture(invalid);
    });
    rejects([&] { decode_texture(bytes, TextureLimits{63}); });
}
void untrusted_metadata() {
    const auto plain = image();
    auto metadata = legacy_metadata();
    const Json entry{{"id", 0}, {"region", {{"x", 0}, {"y", 0}, {"width", 4}, {"height", 4}}}};
    metadata["sprite_frames"] = {{"version", 1}, {"entries", Json::array({entry})}};
    const auto admit = [&](const Json& value) {
        return decode_texture(asset_detail::encode_envelope(value, plain.subresources.front(),
                                                            magic, 4 * 1024 * 1024));
    };
    check(admit(metadata).sprite_frames.front().id == 0, "Zero stable frame key was rejected");
    for (const Json& key :
         {Json(-1), Json(std::uint64_t(UINT32_MAX) + 1), Json(1.5), Json("0"), Json(true)}) {
        auto invalid = metadata;
        invalid["sprite_frames"]["entries"][0]["id"] = key;
        rejects([&] { admit(invalid); });
    }
    rejects([&] {
        auto invalid = metadata;
        invalid["sprite_frames"]["version"] = 2;
        admit(invalid);
    });
    rejects([&] {
        auto invalid = metadata;
        invalid["sprite_frames"]["entries"] = Json::array();
        admit(invalid);
    });
    rejects([&] {
        auto invalid = metadata;
        for (unsigned i = 0; i < 512; ++i)
            invalid["sprite_frames"]["entries"].push_back(entry);
        admit(invalid);
    });
    rejects([&] {
        auto invalid = metadata;
        invalid["sprite_frames"]["entries"][0]["region"]["width"] = 5;
        admit(invalid);
    });
    rejects([&] {
        auto invalid = metadata;
        invalid["sprite_frames"]["entries"][0]["region"]["rotated_clockwise"] = 1;
        admit(invalid);
    });
}
} // namespace
int main() {
    try {
        legacy_and_roundtrip();
        untrusted_metadata();
        std::cout << "Texture-owned stable Sprite frames: legacy bytes, exact metadata/pixels and "
                     "invalid admission PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
