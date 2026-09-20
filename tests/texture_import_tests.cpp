#include "fixtures/texture_jpeg.hpp"
#include "texture_diligent.hpp"
#include "texture_import.hpp"
#include <DataBlobImpl.hpp>
#include <Image.h>
#include <PNGCodec.h>
#include <RefCntAutoPtr.hpp>
#include <cstdlib>
#include <iostream>
#include <png.h>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F fn, std::source_location location = std::source_location::current()) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid texture import accepted at line " +
                             std::to_string(location.line()));
}
std::vector<std::byte> encode(std::span<const unsigned char> pixels, unsigned w, unsigned h,
                              bool alpha = true,
                              Diligent::IMAGE_FILE_FORMAT f = Diligent::IMAGE_FILE_FORMAT_PNG) {
    Diligent::Image::EncodeInfo info;
    info.Width = w;
    info.Height = h;
    info.TexFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
    info.KeepAlpha = alpha;
    info.pData = pixels.data();
    info.Stride = w * 4;
    info.FileFormat = f;
    Diligent::RefCntAutoPtr<Diligent::IDataBlob> blob;
    Diligent::Image::Encode(info, &blob);
    require(bool(blob) && blob->GetSize(), "Fixture image encode failed");
    const auto* data = static_cast<const std::byte*>(blob->GetConstDataPtr());
    return {data, data + blob->GetSize()};
}
int channel(const TextureData& t, unsigned mip, unsigned c) {
    return std::to_integer<int>(t.subresources[mip][c]);
}
} // namespace
int main() {
    try {
        const std::array<unsigned char, 16> pixels{0, 0, 0, 255, 255, 255, 255, 255,
                                                   0, 0, 0, 255, 255, 255, 255, 255};
        const auto png = encode(pixels, 2, 2);
        TextureImportSettings settings;
        auto color = import_texture_image(png, "fixture.png", settings);
        require(color.format == TextureFormat::RGBA8Srgb && color.mips == 2 &&
                    color.alpha == TextureAlpha::Opaque && channel(color, 1, 0) >= 185 &&
                    channel(color, 1, 0) <= 190,
                "sRGB mip filtering is not linear-light");
        settings.srgb = false;
        auto linear = import_texture_image(png, "fixture.png", settings);
        require(channel(linear, 1, 0) >= 127 && channel(linear, 1, 0) <= 128,
                "Linear mip average incorrect");
        settings.generate_mips = false;
        auto base = import_texture_image(png, "fixture.png", settings);
        require(base.mips == 1 && base.subresources.size() == 1,
                "Disabled mip generation exposed uninitialized levels");
        settings.max_size = 1;
        auto small = import_texture_image(png, "fixture.png", settings);
        require(small.width == 1 && small.height == 1 && small.mips == 1,
                "Maximum dimension resize failed");
        settings = {};
        settings.srgb = false;
        settings.compression = TextureCompression::NativeBcHighQuality;
        auto compressed = import_texture_image(png, "fixture.png", settings);
        require(compressed.format == TextureFormat::BC3 && compressed.mips == 2 &&
                    compressed.subresources[1].size() == 16,
                "Native BC compression/layout failed");
        const std::array<unsigned char, 16> normals{128, 220, 220, 255, 128, 220, 220, 255,
                                                    128, 220, 220, 255, 128, 220, 220, 255};
        const auto normal_png = encode(normals, 2, 2);
        settings = {};
        settings.srgb = false;
        settings.semantic = TextureSemantic::Normal;
        auto normal = import_texture_image(normal_png, "normal.png", settings);
        settings.flip_normal_green = true;
        auto flipped = import_texture_image(normal_png, "normal.png", settings);
        require(channel(normal, 0, 1) > 200 && channel(flipped, 0, 1) < 55 &&
                    channel(normal, 1, 1) > 200 && channel(flipped, 1, 1) < 55,
                "Normal orientation/mip normalization failed");
        settings.srgb = true;
        rejects([&] { import_texture_image(normal_png, "normal.png", settings); });
        const std::array<unsigned char, 4> transparent{200, 100, 50, 128};
        const auto transparent_png = encode(transparent, 1, 1);
        settings = {};
        settings.srgb = false;
        settings.premultiply_alpha = true;
        auto premult = import_texture_image(transparent_png, "alpha.png", settings);
        require(premult.alpha == TextureAlpha::Premultiplied && channel(premult, 0, 0) >= 99 &&
                    channel(premult, 0, 0) <= 101,
                "Premultiplied metadata/pixels disagree");
        // Gray+alpha is color, not an RG color-channel pair; data retains RG.
        const std::array<Diligent::Uint8, 2> ga{200, 128};
        auto ga_blob = Diligent::DataBlobImpl::Create();
        require(Diligent::EncodePng(ga.data(), 1, 1, 2, PNG_COLOR_TYPE_GRAY_ALPHA, ga_blob) ==
                    Diligent::ENCODE_PNG_RESULT_OK,
                "Gray-alpha fixture failed");
        const std::span ga_bytes(static_cast<const std::byte*>(ga_blob->GetConstDataPtr()),
                                 ga_blob->GetSize());
        settings = {};
        settings.srgb = false;
        settings.premultiply_alpha = true;
        auto ga_color = import_texture_image(ga_bytes, "gray.png", settings);
        require(channel(ga_color, 0, 0) >= 99 && channel(ga_color, 0, 0) <= 101 &&
                    channel(ga_color, 0, 1) == channel(ga_color, 0, 0) &&
                    channel(ga_color, 0, 2) == channel(ga_color, 0, 0) &&
                    channel(ga_color, 0, 3) == 128,
                "Gray-alpha color expansion/premultiplication failed");
        settings.premultiply_alpha = false;
        settings.semantic = TextureSemantic::Data;
        settings.compression = TextureCompression::NativeBc;
        require(import_texture_image(ga_bytes, "rg.png", settings).format == TextureFormat::BC5,
                "RG data compression lost channels");
        const std::vector<std::byte> jpeg(
            reinterpret_cast<const std::byte*>(texture_jpeg_fixture.data()),
            reinterpret_cast<const std::byte*>(texture_jpeg_fixture.data() +
                                               texture_jpeg_fixture.size()));
        settings = {};
        auto decoded_jpeg = import_texture_image(jpeg, "fixture.jpg", settings);
        require(decoded_jpeg.width == 2 && decoded_jpeg.alpha == TextureAlpha::Opaque,
                "JPEG decoder failed");
        std::vector<std::byte> tga(18);
        tga[2] = std::byte{2};
        tga[12] = std::byte{1};
        tga[14] = std::byte{1};
        tga[16] = std::byte{24};
        tga[17] = std::byte{32};
        tga.insert(tga.end(), {std::byte{10}, std::byte{20}, std::byte{30}});
        auto decoded_tga = import_texture_image(tga, "fixture.tga", settings);
        require(channel(decoded_tga, 0, 0) == 30 && channel(decoded_tga, 0, 2) == 10,
                "TGA BGR/orientation decode failed");
        for (std::size_t n = 0; n < tga.size(); ++n) {
            const std::vector<std::byte> truncated(tga.begin(), tga.begin() + n);
            rejects([&] { import_texture_image(truncated, "bad.tga", settings); });
        }
        auto rle_tga = tga;
        rle_tga[2] = std::byte{10};
        rle_tga.insert(rle_tga.begin() + 18, std::byte{128});
        require(import_texture_image(rle_tga, "rle.tga", settings).width == 1, "TGA RLE failed");
        rle_tga[18] = std::byte{129};
        rejects([&] { import_texture_image(rle_tga, "bad-rle.tga", settings); });
        const std::string hdr_header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
        std::vector<std::byte> hdr(
            reinterpret_cast<const std::byte*>(hdr_header.data()),
            reinterpret_cast<const std::byte*>(hdr_header.data() + hdr_header.size()));
        hdr.insert(hdr.end(), {std::byte{128}, std::byte{64}, std::byte{32}, std::byte{130}});
        settings = {};
        settings.srgb = false;
        settings.semantic = TextureSemantic::HdrColor;
        auto decoded_hdr = import_texture_image(hdr, "fixture.hdr", settings);
        require(decoded_hdr.format == TextureFormat::RGBA32Float &&
                    decoded_hdr.alpha == TextureAlpha::Opaque,
                "HDR range was quantized to LDR");
        settings.compression = TextureCompression::NativeBc;
        rejects([&] { import_texture_image(hdr, "fixture.hdr", settings); });
        settings = {};
        for (std::size_t n = 0; n < png.size(); ++n) {
            const std::vector<std::byte> truncated(png.begin(), png.begin() + n);
            rejects([&] { import_texture_image(truncated, "bad.png", settings); });
        }
        for (std::size_t n = 0; n < jpeg.size(); ++n) {
            const std::vector<std::byte> truncated(jpeg.begin(), jpeg.begin() + n);
            rejects([&] { import_texture_image(truncated, "bad.jpg", settings); });
        }
        TextureLimits budget;
        budget.bytes = 12;
        rejects([&] { import_texture_image(png, "fixture.png", settings, budget); });
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { import_texture_image(png, "fixture.png", settings, {}, stop.get_token()); });
        for (unsigned i = 0; i <= unsigned(TextureFormat::BC7Srgb); ++i)
            require(forge_texture_format(diligent_texture_format(TextureFormat(i))) ==
                        TextureFormat(i),
                    "Native format mapping disagrees");
        std::cout << "Native texture image/color/mip/compression/normal/alpha/HDR tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
