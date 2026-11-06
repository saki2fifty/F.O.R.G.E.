#pragma once
#include "avi_movie.hpp"
#include "import_process.hpp"
#include <span>

namespace forge::asset_detail {
enum class SequenceEncoding { Webp, AviMjpeg };
inline constexpr std::size_t sequence_snapshot_byte_limit = 256ull * 1024 * 1024;
inline constexpr std::string_view avi_sequence_recipe = "forge.image.avi-sequence";
inline constexpr std::string_view avi_sequence_profile = "mjpeg-opaque-srgb8-v1";
struct SequenceEncodingProfile {
    std::string_view name, recipe, profile, filename, mime, riff_type;
    std::size_t output_byte_limit;
};
const SequenceEncodingProfile& sequence_encoding_profile(SequenceEncoding);
WorkerLimits sequence_encoding_worker_limits(SequenceEncoding);
// Existing frame capture owns these complete flat PNGs. Snapshot validation,
// supervision, cancellation and atomic final publication are shared by encoders.
std::size_t
write_encoded_sequence(const std::filesystem::path& worker, const std::filesystem::path& project,
                       const std::filesystem::path& folder, std::span<const std::string> frames,
                       unsigned width, unsigned height, double fps, SequenceEncoding,
                       int jpeg_quality = FORGE_JPEG_DEFAULT_QUALITY, std::stop_token = {});
struct AviSequenceInfo {
    std::size_t bytes = 0;
    MovieFrameRate frame_rate;
};
AviSequenceInfo
write_avi_sequence(const std::filesystem::path& worker, const std::filesystem::path& project,
                   const std::filesystem::path& folder, std::span<const std::string> frames,
                   unsigned width, unsigned height, double fps,
                   int jpeg_quality = FORGE_JPEG_DEFAULT_QUALITY, std::stop_token = {});
std::vector<ArtifactFile> execute_avi_sequence_recipe(ImportProcessRequest, std::stop_token = {});
} // namespace forge::asset_detail
