#include "texture_image_process.hpp"
#include "texture_block_image.hpp"
#include "texture_image_slice.hpp"
#include <cstring>
#include <stdexcept>

namespace forge::asset_detail {
WorkerLimits texture_image_worker_limits() {
    WorkerLimits limits;
    limits.file_bytes = 64ull * 1024 * 1024;
    limits.total_bytes = 72ull * 1024 * 1024;
    limits.files = 4;
    limits.cancellation_grace_ms = 250;
    return limits;
}
Rgba8Pixels texture_compressed_pixels(const std::filesystem::path& worker,
                                      const std::filesystem::path& project,
                                      const TextureData& texture, TextureImageSlice slice,
                                      std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Compressed Texture image decode cancelled");
    if (!texture_block_image_supported(texture.format))
        throw std::runtime_error(
            "Compressed image export supports UNORM BC1, BC2, BC3, BC4, BC5 and BC7 only");
    const auto plane = texture_image_plane(texture, slice, texture_image_source_limit);
    if (worker.empty())
        throw std::runtime_error("Compressed Texture image export needs the packaged asset worker");
    ImportProcessRequest request{{{"recipe", texture_image_decode_recipe},
                                  {"profile", texture_image_decode_profile},
                                  {"format", unsigned(texture.format)},
                                  {"width", plane.layout.width},
                                  {"height", plane.layout.height}},
                                 {{"blocks.bin", {plane.bytes.begin(), plane.bytes.end()}}}};
    auto files = run_import_process(worker, project, std::move(request),
                                    texture_image_worker_limits(), stop);
    const auto count = std::size_t(plane.layout.width) * plane.layout.height * 4;
    if (files.size() != 1 || files[0].name != "rgba8.bin" || files[0].bytes.size() != count)
        throw std::runtime_error("Texture image worker returned an unexpected RGBA8 plane");
    Rgba8Pixels result{plane.layout.width, plane.layout.height, std::vector<std::uint8_t>(count),
                       texture_format_info(texture.format).srgb, true};
    std::memcpy(result.rgba.data(), files[0].bytes.data(), count);
    return result;
}
} // namespace forge::asset_detail
