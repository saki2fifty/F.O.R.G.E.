#include "texture_image_process.hpp"
#include "texture_block_image.hpp"
#include "texture_image_slice.hpp"
#include "view_image_limits.h"
#include <cstring>
#include <stdexcept>

namespace forge::asset_detail {
WorkerLimits texture_image_worker_limits(bool wide_samples) {
    WorkerLimits limits;
    limits.file_bytes =
        wide_samples ? FORGE_UINT16_IMAGE_PIXEL_LIMIT : FORGE_VIEW_IMAGE_PIXEL_LIMIT;
    limits.total_bytes = limits.file_bytes + 8ull * 1024 * 1024;
    limits.files = 4;
    limits.cancellation_grace_ms = 250;
    return limits;
}
ImagePixels texture_compressed_pixels(const std::filesystem::path& worker,
                                      const std::filesystem::path& project,
                                      const TextureData& texture, TextureImageSlice slice,
                                      std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Compressed Texture image decode cancelled");
    if (!texture_block_image_supported(texture.format))
        throw std::runtime_error(
            "Compressed image export supports UNORM BC1/2/3/4/5/7, SNORM BC4/5 and HDR BC6");
    const bool signed_samples = texture_snorm_block_storage(texture.format);
    const bool hdr = texture_hdr_block_storage(texture.format);
    if (hdr && texture.semantic != TextureSemantic::HdrColor)
        throw std::runtime_error("BC6 HDR output requires an HdrColor Texture variant");
    const bool wide_samples = signed_samples || hdr;
    const auto plane = texture_image_plane(
        texture, slice, hdr ? texture_hdr_source_limit : texture_image_source_limit);
    if (worker.empty())
        throw std::runtime_error("Compressed Texture image export needs the packaged asset worker");
    ImportProcessRequest request{{{"recipe", texture_image_decode_recipe},
                                  {"profile", hdr              ? texture_hdr_image_decode_profile
                                              : signed_samples ? texture_signed_image_decode_profile
                                                               : texture_image_decode_profile},
                                  {"format", unsigned(texture.format)},
                                  {"width", plane.layout.width},
                                  {"height", plane.layout.height}},
                                 {{"blocks.bin", {plane.bytes.begin(), plane.bytes.end()}}}};
    auto files = run_import_process(worker, project, std::move(request),
                                    texture_image_worker_limits(wide_samples), stop);
    const auto samples = std::size_t(plane.layout.width) * plane.layout.height * 4;
    const auto count = samples * (wide_samples ? 2 : 1);
    const char* name = hdr ? "rgba16f-le.bin" : signed_samples ? "rgba16le.bin" : "rgba8.bin";
    if (files.size() != 1 || files[0].name != name || files[0].bytes.size() != count)
        throw std::runtime_error("Texture image worker returned an unexpected typed RGBA plane");
    if (hdr) {
        TextureData decoded;
        decoded.format = TextureFormat::RGBA16Float;
        decoded.semantic = TextureSemantic::HdrColor;
        decoded.alpha = TextureAlpha::Opaque;
        decoded.width = plane.layout.width;
        decoded.height = plane.layout.height;
        decoded.subresources.push_back(std::move(files[0].bytes));
        auto pixels = texture_hdr_pixels(decoded, {}, stop);
        pixels.bc6_block_decoded = true;
        return pixels;
    }
    if (signed_samples) {
        Rgba16Pixels result{plane.layout.width, plane.layout.height,
                            std::vector<std::uint16_t>(samples), true};
        for (std::size_t i = 0; i < samples; ++i)
            result.rgba[i] =
                std::uint16_t(std::to_integer<unsigned>(files[0].bytes[i * 2]) |
                              (std::to_integer<unsigned>(files[0].bytes[i * 2 + 1]) << 8));
        return result;
    }
    Rgba8Pixels result{plane.layout.width, plane.layout.height, std::vector<std::uint8_t>(count),
                       texture_format_info(texture.format).srgb, true};
    std::memcpy(result.rgba.data(), files[0].bytes.data(), count);
    return result;
}
} // namespace forge::asset_detail
