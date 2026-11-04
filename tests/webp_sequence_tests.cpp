#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "texture_encode.h"
#include "view_image.hpp"
#include "webp_sequence.hpp"
#include "webp_sequence_check.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/mux.h>

using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void reject(F action, std::string_view reason = {}) {
    try {
        action();
    } catch (const std::exception& error) {
        if (!reason.empty() &&
            std::string_view(error.what()).find(reason) == std::string_view::npos)
            throw std::runtime_error("WebP animation rejection expected '" + std::string(reason) +
                                     "', received: " + error.what());
        return;
    }
    throw std::runtime_error("Invalid WebP animation admitted");
}
Rgba8Pixels pixels(unsigned frame) {
    Rgba8Pixels image{8, 4, std::vector<std::uint8_t>(128), true};
    for (unsigned i = 0; i < 32; ++i) {
        image.rgba[i * 4] = std::uint8_t((i * 13 + frame * 73) & 255);
        image.rgba[i * 4 + 1] = std::uint8_t((i * 7 + frame * 31) & 255);
        image.rgba[i * 4 + 2] = std::uint8_t((i * 3 + frame * 53) & 255);
        image.rgba[i * 4 + 3] = std::uint8_t(i % 7 == frame ? 0 : i % 5 == frame ? 99 : 255);
    }
    return image;
}
std::vector<std::byte> png(const Rgba8Pixels& image) {
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    check(forge_png_encode_rgba8_profile(image.rgba.data(), image.rgba.size(), image.width,
                                         image.height, image.width * 4, image.srgb,
                                         FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded),
          encoded.error);
    const auto* first = reinterpret_cast<const std::byte*>(encoded.bytes);
    return {first, first + encoded.size};
}
ImportProcessRequest request() {
    ImportProcessRequest result{{{"recipe", webp_sequence_recipe},
                                 {"profile", webp_sequence_profile},
                                 {"width", 8u},
                                 {"height", 4u},
                                 {"durations_ms", webp_sequence_durations(30, 3)}},
                                {}};
    for (unsigned i = 0; i < 3; ++i)
        result.inputs.push_back({"frame-" + std::to_string(i) + ".png", png(pixels(i))});
    return result;
}
void inspect(std::span<const std::byte> file) {
    const std::array<Rgba8Pixels, 3> frames{pixels(0), pixels(1), pixels(2)};
    const std::array<unsigned, 3> durations{33, 34, 33};
    test_webp_sequence::inspect(file, frames, durations);
}

void timing() {
    check(webp_sequence_durations(30, 3) == std::vector<unsigned>{33, 34, 33},
          "Fractional millisecond frame schedule changed");
    check(validate_webp_sequence_durations(webp_sequence_durations(29.97, 180)) == 6006,
          "Fractional FPS accumulated duration drift");
    check(validate_webp_sequence_durations(webp_sequence_durations(240, 4096)) == 17067,
          "Maximum frame/FPS schedule changed");
    for (double fps :
         {0., -1., 241., std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::denorm_min()})
        reject([&] { webp_sequence_durations(fps, 3); });
    for (unsigned count : {0u, 1u, 4097u})
        reject([&] { webp_sequence_durations(30, count); });
    reject([&] { webp_sequence_durations(.00005, 2); }, "frame duration");
    reject([&] { webp_sequence_durations(.001, 4096); }, "duration exceeds");
    const std::array<unsigned, 2> zero{0, 1}, too_long{0x1000000, 1};
    reject([&] { validate_webp_sequence_durations(zero); }, "frame duration");
    reject([&] { validate_webp_sequence_durations(too_long); }, "frame duration");
}
void direct() {
    const auto result = execute_webp_sequence_recipe(request());
    check(result.size() == 1 && result[0].name == "animation.webp", "Unexpected sequence result");
    inspect(result[0].bytes);
    auto invalid = request();
    invalid.payload["profile"] = "other";
    reject([&] { execute_webp_sequence_recipe(invalid); }, "profile");
    invalid = request();
    invalid.payload["width"] = 9u;
    reject([&] { execute_webp_sequence_recipe(invalid); }, "consistent");
    invalid = request();
    invalid.payload["width"] = -1;
    reject([&] { execute_webp_sequence_recipe(invalid); }, "axes");
    invalid = request();
    invalid.payload["durations_ms"][1] = 0u;
    reject([&] { execute_webp_sequence_recipe(invalid); }, "frame duration");
    invalid = request();
    invalid.inputs[1].name = "frame-0.png";
    reject([&] { execute_webp_sequence_recipe(invalid); }, "source snapshot");
    invalid = request();
    auto unlabelled = pixels(1);
    unlabelled.srgb = false;
    invalid.inputs[1].bytes = png(unlabelled);
    reject([&] { execute_webp_sequence_recipe(invalid); }, "sRGB RGBA8");
    invalid = request();
    invalid.inputs[2].bytes.pop_back();
    reject([&] { execute_webp_sequence_recipe(invalid); });
    invalid = request();
    const std::array<std::uint16_t, 128> rgba16{};
    ForgeImageBytes encoded16{};
    check(forge_png_encode_rgba16(rgba16.data(), sizeof(rgba16), 8, 4, 8 * 8, 1024 * 1024,
                                  &encoded16),
          encoded16.error);
    const auto* first16 = reinterpret_cast<const std::byte*>(encoded16.bytes);
    invalid.inputs[1].bytes.assign(first16, first16 + encoded16.size);
    forge_image_bytes_free(&encoded16);
    reject([&] { execute_webp_sequence_recipe(invalid); }, "sRGB RGBA8");
    invalid = request();
    invalid.inputs[0].bytes.resize(FORGE_VIEW_IMAGE_ENCODED_LIMIT + 1);
    reject([&] { execute_webp_sequence_recipe(std::move(invalid)); }, "byte budget");
    invalid = request();
    invalid.payload["durations_ms"] = std::vector<unsigned>(5, 1);
    invalid.inputs.clear();
    for (unsigned i = 0; i < 5; ++i)
        invalid.inputs.push_back({"frame-" + std::to_string(i) + ".png",
                                  std::vector<std::byte>(i == 4 ? 1 : 64 * 1024 * 1024)});
    reject([&] { execute_webp_sequence_recipe(std::move(invalid)); }, "byte budget");
    std::stop_source stopped;
    stopped.request_stop();
    reject([&] { execute_webp_sequence_recipe(request(), stopped.get_token()); }, "cancelled");
}
void process(const std::filesystem::path& worker, const std::filesystem::path& base) {
    const auto root = base / AssetId::generate().str();
    const auto folder = root / "Frames";
    std::filesystem::create_directories(folder);
    std::vector<std::string> names;
    for (unsigned i = 0; i < 3; ++i) {
        names.push_back("frame-00000" + std::to_string(i) + ".png");
        save_image_pixels(folder / names.back(), pixels(i));
    }
    const auto info = write_webp_sequence(worker, root, folder, names, 8, 4, 30);
    const auto output = read_bytes(folder / "animation.webp", FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    check(info.bytes == output.size() && info.duration_ms == 100,
          "Process lost animation metadata");
    inspect(output);
    reject([&] { write_webp_sequence(worker, root, folder, names, 8, 4, 30); }, "already exists");
    check(read_bytes(folder / "animation.webp", FORGE_VIEW_IMAGE_ENCODED_LIMIT) == output,
          "Rejected sequence replacement changed a completed file");
    const auto bad = root / "Bad";
    std::filesystem::create_directory(bad);
    for (unsigned i = 0; i < 2; ++i)
        save_image_pixels(bad / names[i], pixels(i));
    asset_storage::replace(bad / names[2], "invalid PNG");
    reject([&] { write_webp_sequence(worker, root, bad, names, 8, 4, 30); });
    check(!std::filesystem::exists(bad / "animation.webp"), "Failed worker published an animation");
    std::stop_source stopped;
    stopped.request_stop();
    reject([&] { write_webp_sequence(worker, root, bad, names, 8, 4, 30, stopped.get_token()); },
           "cancelled");
    const auto escaping = std::vector<std::string>{"../one.png", "two.png"};
    reject([&] { write_webp_sequence(worker, root, bad, escaping, 8, 4, 30); }, "flat");
}
void cancel_process(const std::filesystem::path& worker, const std::filesystem::path& base) {
    const auto root = base / AssetId::generate().str();
    const auto folder = root / "Cancel";
    std::filesystem::create_directories(folder);
    Rgba8Pixels image{512, 512, std::vector<std::uint8_t>(512 * 512 * 4), true};
    std::uint32_t noise = 123456789;
    for (auto& byte : image.rgba) {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        byte = std::uint8_t(noise);
    }
    std::vector<std::string> names;
    for (unsigned i = 0; i < 16; ++i) {
        names.push_back("frame-" + std::to_string(i) + ".png");
        save_image_pixels(folder / names.back(), image);
    }
    const auto original = read_bytes(folder / names.front(), FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    std::stop_source stop;
    auto job = std::async(std::launch::async, [&] {
        reject(
            [&] {
                write_webp_sequence(worker, root, folder, names, 512, 512, 30, stop.get_token());
            },
            "cancel");
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool staged = false;
    while (!staged) {
        check(std::chrono::steady_clock::now() < deadline, "Cancellation snapshot stalled");
        const auto jobs = root / ".forge/jobs";
        if (std::filesystem::is_directory(jobs))
            for (const auto& entry : std::filesystem::directory_iterator(jobs))
                staged |= std::filesystem::exists(entry.path() / "input/frame-15.png");
        check(job.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
              "Animation completed before cancellation checkpoint");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Allow the real supervisor to enter encoding after the immutable snapshot.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.request_stop();
    check(job.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
          "Supervised animation cancellation did not retire promptly");
    job.get();
    check(!std::filesystem::exists(folder / "animation.webp") &&
              read_bytes(folder / names.front(), FORGE_VIEW_IMAGE_ENCODED_LIMIT) == original,
          "Cancellation changed completed PNGs or published an animation");
}
} // namespace
int main(int argc, char** argv) {
    try {
        timing();
        direct();
        if (argc == 3) {
            process(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
            cancel_process(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
        }
        std::cout
            << "Lossless WebP sequence pixels/timing/admission and supervised output passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
