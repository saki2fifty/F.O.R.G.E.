#include "texture_image_process.hpp"
#include "texture_block_image.hpp"
#include "texture_image_slice.hpp"
#include "view_image_limits.h"
#include <cstring>
#include <stdexcept>

namespace forge::asset_detail {
WorkerLimits texture_image_worker_limits(bool signed_samples) {
    WorkerLimits limits;
    limits.file_bytes =
        signed_samples ? FORGE_UINT16_IMAGE_PIXEL_LIMIT : FORGE_VIEW_IMAGE_PIXEL_LIMIT;
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
            "Compressed image export supports UNORM BC1/2/3/4/5/7 and SNORM BC4/5 only");
    const bool signed_samples = texture_snorm_block_storage(texture.format);
    const auto plane = texture_image_plane(texture, slice, texture_image_source_limit);
    if (worker.empty())
        throw std::runtime_error("Compressed Texture image export needs the packaged asset worker");
    ImportProcessRequest request{{{"recipe", texture_image_decode_recipe},
                                  {"profile", signed_samples ? texture_signed_image_decode_profile
                                                             : texture_image_decode_profile},
                                  {"format", unsigned(texture.format)},
                                  {"width", plane.layout.width},
                                  {"height", plane.layout.height}},
                                 {{"blocks.bin", {plane.bytes.begin(), plane.bytes.end()}}}};
    auto files = run_import_process(worker, project, std::move(request),
                                    texture_image_worker_limits(signed_samples), stop);
    const auto samples = std::size_t(plane.layout.width) * plane.layout.height * 4;
    const auto count = samples * (signed_samples ? 2 : 1);
    if (files.size() != 1 || files[0].name != (signed_samples ? "rgba16le.bin" : "rgba8.bin") ||
        files[0].bytes.size() != count)
        throw std::runtime_error("Texture image worker returned an unexpected typed RGBA plane");
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
