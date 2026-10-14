#include "fixtures/texture_jpeg.hpp"
#include <cstring>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
#include <png.h>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid UI image accepted");
}
std::vector<std::byte> png(const void* pixels, unsigned width, unsigned height,
                           png_uint_32 format) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = format;
    png_alloc_size_t size = 0;
    check(png_image_write_to_memory(&image, nullptr, &size, 0, pixels, 0, nullptr) != 0,
          "Owned PNG sizing failed");
    std::vector<std::byte> bytes(size);
    check(png_image_write_to_memory(&image, bytes.data(), &size, 0, pixels, 0, nullptr) != 0,
          "Owned PNG encoding failed");
    bytes.resize(size);
    png_image_free(&image);
    return bytes;
}
unsigned channel(const UiImage& image, unsigned x, unsigned y, unsigned c) {
    return std::to_integer<unsigned>(image.rgba[(std::size_t(y) * image.width + x) * 4 + c]);
}
void write(const std::filesystem::path& path, std::span<const std::byte> data) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    check(bool(file), "Fixture write failed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need image scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const std::array<unsigned char, 16> pixels{255, 128, 64, 128, 255, 255, 255, 0,
                                                   16,  32,  48, 255, 0,   64,  128, 64};
        const auto rgba = png(pixels.data(), 2, 2, PNG_FORMAT_RGBA);
        const auto image = decode_ui_image(rgba);
        check(image.width == 2 && image.height == 2 && image.rgba.size() == 16,
              "PNG extent/layout");
        check(channel(image, 0, 0, 0) == 128 && channel(image, 0, 0, 1) == 64 &&
                  channel(image, 0, 0, 2) == 32 && channel(image, 0, 0, 3) == 128,
              "PNG alpha was omitted or premultiplied twice");
        check(channel(image, 1, 0, 0) == 0 && channel(image, 1, 0, 3) == 0 &&
                  channel(image, 0, 1, 0) == 16 && channel(image, 0, 1, 2) == 48,
              "PNG zero alpha or row order");
        const std::array<unsigned char, 4> gray{128, 128, 64, 255};
        const std::array<unsigned char, 2> opaque_gray{96, 192};
        const auto gray_image = decode_ui_image(png(opaque_gray.data(), 2, 1, PNG_FORMAT_GRAY));
        check(channel(gray_image, 0, 0, 0) == 96 && channel(gray_image, 0, 0, 2) == 96 &&
                  channel(gray_image, 0, 0, 3) == 255,
              "Opaque PNG grayscale normalization");
        const auto ga = decode_ui_image(png(gray.data(), 2, 1, PNG_FORMAT_GA));
        check(channel(ga, 0, 0, 0) == 64 && channel(ga, 0, 0, 1) == 64 &&
                  channel(ga, 0, 0, 2) == 64 && channel(ga, 0, 0, 3) == 128 &&
                  channel(ga, 1, 0, 0) == 64,
              "PNG grayscale/alpha normalization");
        const std::array<std::uint16_t, 3> high{65535, 32896, 16448};
        const auto rgba16 = decode_ui_image(png(high.data(), 1, 1, PNG_FORMAT_LINEAR_RGB));
        check(channel(rgba16, 0, 0, 0) == 255 && channel(rgba16, 0, 0, 1) == 128 &&
                  channel(rgba16, 0, 0, 2) == 64 && channel(rgba16, 0, 0, 3) == 255,
              "PNG16 host-endian display-byte normalization");
        const std::span jpeg(reinterpret_cast<const std::byte*>(texture_jpeg_fixture.data()),
                             texture_jpeg_fixture.size());
        const auto photo = decode_ui_image(jpeg);
        check(photo.width == 2 && photo.height == 2, "JPEG extent");
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned c = 0; c < 3; ++c)
                check(channel(photo, 0, y, c) <= 2 && channel(photo, 1, y, c) >= 253 &&
                          channel(photo, 0, y, 3) == 255 && channel(photo, 1, y, 3) == 255,
                      "JPEG owned black/white columns or opaque alpha");
        for (std::size_t size = 0; size < rgba.size(); ++size) {
            const std::vector<std::byte> truncated(rgba.begin(), rgba.begin() + size);
            reject([&] { decode_ui_image(truncated); });
        }
        for (std::size_t size = 0; size < jpeg.size(); ++size) {
            const std::vector<std::byte> truncated(jpeg.begin(), jpeg.begin() + size);
            reject([&] { decode_ui_image(truncated); });
        }
        auto corrupt = rgba;
        corrupt[corrupt.size() - 5] ^= std::byte{1};
        reject([&] { decode_ui_image(corrupt); });
        auto trailing = rgba;
        trailing.push_back(std::byte{});
        reject([&] { decode_ui_image(trailing); });
        const std::vector<unsigned char> wide(2049 * 4, 255);
        const auto oversized = png(wide.data(), 2049, 1, PNG_FORMAT_RGBA);
        reject([&] { decode_ui_image(oversized); });
        const std::vector<std::uint16_t> large16(1536 * 1536 * 4, 65535);
        const auto native_budget = png(large16.data(), 1536, 1536, PNG_FORMAT_LINEAR_RGB_ALPHA);
        reject([&] { decode_ui_image(native_budget); });
        const std::vector<std::byte> source_budget(16 * 1024 * 1024 + 275);
        reject([&] { decode_ui_image(source_budget); });
        // Original uncompressed TGA output remains exact, including bottom-origin rows.
        std::vector<std::byte> tga(18);
        tga[2] = std::byte{2};
        tga[12] = std::byte{1};
        tga[14] = std::byte{2};
        tga[16] = std::byte{32};
        tga.insert(tga.end(), {std::byte{48}, std::byte{32}, std::byte{16}, std::byte{255},
                               std::byte{64}, std::byte{128}, std::byte{255}, std::byte{128}});
        const auto legacy = decode_ui_image(tga);
        check(channel(legacy, 0, 0, 0) == 128 && channel(legacy, 0, 0, 1) == 64 &&
                  channel(legacy, 0, 1, 0) == 16,
              "Legacy TGA changed");
        for (unsigned id_bytes : {137u, 255u}) {
            auto with_id = tga;
            with_id[0] = std::byte(id_bytes);
            with_id.insert(with_id.begin() + 18, id_bytes, std::byte{});
            check(decode_ui_image(with_id).rgba == legacy.rgba,
                  "Legacy TGA image ID changed pixel decoding");
            const auto name = "image-id-" + std::to_string(id_bytes) + ".tga";
            write(root / name, with_id);
            UiResources identified(root);
            identified.read(name);
            const auto observed = identified.snapshot();
            check(observed.sources[0].details.at("width") == 1 &&
                      observed.sources[0].details.at("height") == 2,
                  "TGA image ID was mistaken for a PNG/JPEG signature");
        }
        write(root / "alpha.png", rgba);
        write(root / "photo.jpg", jpeg);
        write(root / "photo.jpeg", jpeg);
        write(root / "bad.png", tga);
        write(root / "bad.tga", rgba);
        UiResources resources(root);
        resources.read("alpha.png");
        resources.read("photo.jpg");
        resources.read("photo.jpeg");
        const auto snapshot = resources.snapshot();
        check(snapshot.sources.size() == 3, "UI image observation missing");
        for (const auto& source : snapshot.sources)
            check(source.type == "texture" && source.details.at("width") == 2 &&
                      source.details.at("height") == 2,
                  "Image Catalog type/extent mismatch");
        reject([&] { resources.read("bad.png"); });
        reject([&] { resources.read("bad.tga"); });
        check(resources.snapshot().sources == snapshot.sources,
              "Rejected image changed admitted observations");
        std::cout << "UI PNG/JPEG exact pixels/alpha/layout/PNG16, legacy TGA, "
                     "truncated/CRC/budget/type admission passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
