#include "webp_sequence.hpp"
#include <cmath>
#include <stdexcept>

namespace forge::asset_detail {
namespace {
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
} // namespace
unsigned validate_webp_sequence_durations(std::span<const unsigned> durations) {
    require(durations.size() >= 2 && durations.size() <= FORGE_SEQUENCE_FRAME_MAX_COUNT,
            "WebP animation requires 2 to 4096 frames; use a still image for one frame");
    std::uint64_t total = 0;
    for (auto duration : durations) {
        require(duration && duration <= 0xffffffu,
                "WebP animation frame duration must be 1 to 16777215 milliseconds");
        total += duration;
        require(total <= 0x7fffffffu, "WebP animation duration exceeds 2147483647 milliseconds");
    }
    return unsigned(total);
}
std::vector<unsigned> webp_sequence_durations(double fps, unsigned count) {
    require(std::isfinite(fps) && fps > 0 && fps <= 240,
            "WebP animation FPS must be above zero and at most 240");
    require(count >= 2 && count <= FORGE_SEQUENCE_FRAME_MAX_COUNT,
            "WebP animation requires 2 to 4096 frames; use a still image for one frame");
    const double total = double(count) * 1000 / fps;
    require(std::isfinite(total) && total <= 0x7fffffffu,
            "WebP animation duration exceeds 2147483647 milliseconds");
    std::vector<unsigned> result;
    result.reserve(count);
    unsigned previous = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto end = unsigned(std::llround(double(i + 1) * 1000 / fps));
        result.push_back(end - previous);
        previous = end;
    }
    (void)validate_webp_sequence_durations(result);
    return result;
}
WorkerLimits webp_sequence_worker_limits() {
    return sequence_encoding_worker_limits(SequenceEncoding::Webp);
}
WebpSequenceInfo write_webp_sequence(const std::filesystem::path& worker,
                                     const std::filesystem::path& project,
                                     const std::filesystem::path& folder,
                                     std::span<const std::string> frames, unsigned width,
                                     unsigned height, double fps, std::stop_token stop) {
    const auto duration =
        validate_webp_sequence_durations(webp_sequence_durations(fps, unsigned(frames.size())));
    return {write_encoded_sequence(worker, project, folder, frames, width, height, fps,
                                   SequenceEncoding::Webp, FORGE_JPEG_DEFAULT_QUALITY, stop),
            duration};
}
} // namespace forge::asset_detail
