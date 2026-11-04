#pragma once
#include "import_process.hpp"
#include "view_image_limits.h"
#include <span>

namespace forge::asset_detail {
inline constexpr std::string_view webp_sequence_recipe = "forge.image.webp-sequence";
inline constexpr std::string_view webp_sequence_profile = "lossless-rgba8-full-frames-v1";
inline constexpr std::size_t webp_sequence_source_limit = 256ull * 1024 * 1024;
inline constexpr std::size_t webp_sequence_frame_bytes_limit = 64ull * 1024 * 1024;
struct WebpSequenceInfo {
    std::size_t bytes = 0;
    unsigned duration_ms = 0;
};
// Container timing only. Scene sampling remains the Timeline/AnimationRuntime's
// responsibility. Nearest cumulative milliseconds avoid fractional-FPS drift.
std::vector<unsigned> webp_sequence_durations(double fps, unsigned count);
unsigned validate_webp_sequence_durations(std::span<const unsigned>);
WorkerLimits webp_sequence_worker_limits();
// The caller owns the output folder and supplies only its completed PNG frames.
// This writes animation.webp atomically after the supervised worker succeeds.
WebpSequenceInfo write_webp_sequence(const std::filesystem::path& worker,
                                     const std::filesystem::path& project,
                                     const std::filesystem::path& folder,
                                     std::span<const std::string> frames, unsigned width,
                                     unsigned height, double fps, std::stop_token = {});
// Worker only; consumes immutable snapshots, never project/catalog paths.
std::vector<ArtifactFile> execute_webp_sequence_recipe(ImportProcessRequest, std::stop_token = {});
} // namespace forge::asset_detail
