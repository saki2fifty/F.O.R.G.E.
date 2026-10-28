#include "Common/interface/Float16.hpp"
#include "sprite_frame_mips.hpp"
#include <chrono>
#include <cmath>
#include <forge/texture_resource.hpp>
#include <future>
#include <iostream>
#include <memory>
#include <string>

namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
float scalar(const std::vector<std::byte>& bytes, unsigned pixel, unsigned lane) {
    const auto at = std::size_t(pixel * 4 + lane) * 2;
    const auto bits = std::uint16_t(std::to_integer<unsigned>(bytes.at(at)) |
                                    (std::to_integer<unsigned>(bytes.at(at + 1)) << 8));
    return float(Diligent::Float16{bits});
}
template <class F> void rejects(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid native Sprite mip input was admitted");
}
} // namespace
int main() {
    using namespace forge;
    try {
        check(Diligent::Float16{0.f}.Raw() == 0 && Diligent::Float16{.5f}.Raw() == 0x3800 &&
                  Diligent::Float16{1.f}.Raw() == 0x3c00 &&
                  float(Diligent::Float16{std::uint16_t{0x3400}}) == .25f,
              "Pinned half API differs from known IEEE binary16 encodings");
        for (const bool srgb : {false, true}) {
            TextureData source;
            source.width = source.height = 2;
            source.format = srgb ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8;
            source.alpha = TextureAlpha::Straight;
            const unsigned red = srgb ? 128 : 255, blue = srgb ? 64 : 255;
            // Invisible green/white must contribute no color to filtered alpha.
            source.subresources = {{std::byte(red), std::byte{0}, std::byte{0}, std::byte{255},
                                    std::byte{0}, std::byte{255}, std::byte{0}, std::byte{0},
                                    std::byte{0}, std::byte{0}, std::byte(blue), std::byte{255},
                                    std::byte{255}, std::byte{255}, std::byte{255}, std::byte{0}}};
            const auto before = source.subresources;
            const auto plan = sprite_frame_mip_plan({}, source, 40);
            const auto result = prepare_sprite_frame_mips(source, plan.frames[0], 40);
            check(result.format == TextureFormat::RGBA16Float &&
                      result.alpha == TextureAlpha::Premultiplied && result.mips == 2 &&
                      result.byte_size() == plan.bytes && source.subresources == before,
                  "Native frame mips changed storage, payload or immutable source");
            const auto linear = [&](unsigned value) {
                const double color = value / 255.;
                return !srgb             ? color
                       : color <= .04045 ? color / 12.92
                                         : std::pow((color + .055) / 1.055, 2.4);
            };
            check(std::abs(scalar(result.subresources[1], 0, 0) - linear(red) / 4) < .0001 &&
                      scalar(result.subresources[1], 0, 1) == 0 &&
                      std::abs(scalar(result.subresources[1], 0, 2) - linear(blue) / 4) < .0001 &&
                      scalar(result.subresources[1], 0, 3) == .5f,
                  "Native premultiplied mip mixed transparent RGB or filtered encoded sRGB");
            for (const auto pixel : {1u, 3u})
                for (unsigned lane = 0; lane < 4; ++lane)
                    check(scalar(result.subresources[0], pixel, lane) == 0,
                          "Invisible base RGB survived premultiplication");
            ResourcePool<TextureAsset> pool;
            const auto ticket = pool.request(
                {AssetId::generate()}, std::string(64, 'c'), 1, [source](std::stop_token) {
                    return ResourceCandidate<TextureAsset>{std::make_unique<TextureData>(source),
                                                           ResourceMemory{source.resident_bytes()}};
                });
            check(pool.wait(ticket, std::chrono::seconds(2)), "Source mip snapshot load stalled");
            const auto lease = pool.acquire(ticket);
            auto input = copy_sprite_frame_mip_input(lease.get(), plan.frames[0], 40);
            pool.close();
            rejects([&] { (void)lease.get(); });
            auto worker = std::async(std::launch::async, [input = std::move(input)] {
                return prepare_sprite_frame_mips(input.pixels, input.frame, 40);
            });
            check(worker.get().subresources == result.subresources,
                  "Worker frame preparation depended on a revoked owner lease");
            rejects([&] { prepare_sprite_frame_mips(source, plan.frames[0], 39); });
            std::stop_source stop;
            stop.request_stop();
            rejects(
                [&] { prepare_sprite_frame_mips(source, plan.frames[0], 40, stop.get_token()); });
        }
        std::cout
            << "Sprite private half mip payload, pinned float API, linear/alpha filtering PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
