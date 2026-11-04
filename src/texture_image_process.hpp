#pragma once
#include "import_process.hpp"
#include "texture_image.hpp"

namespace forge::asset_detail {
inline constexpr std::string_view texture_image_decode_recipe = "forge.texture.image-decode";
inline constexpr std::string_view texture_image_decode_profile = "bc-unorm-rgba8-v1";
inline constexpr std::string_view texture_signed_image_decode_profile = "bc-snorm-rgba16-v1";
WorkerLimits texture_image_worker_limits(bool signed_samples = false);
// Transient decode of one checked immutable plane. No import, catalog write,
// source-image access, GPU readback or publication selection occurs here.
ImagePixels texture_compressed_pixels(const std::filesystem::path& worker,
                                      const std::filesystem::path& project, const TextureData&,
                                      TextureImageSlice = {}, std::stop_token = {});
// Worker-only implementation; the parent uses only the process transport above.
std::vector<ArtifactFile> execute_texture_image_recipe(const ImportProcessRequest&,
                                                       std::stop_token = {});
} // namespace forge::asset_detail
