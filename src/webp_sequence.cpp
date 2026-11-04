#include "webp_sequence.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
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
    WorkerLimits limits;
    limits.memory_bytes = 1024ull * 1024 * 1024;
    limits.file_bytes = FORGE_VIEW_IMAGE_ENCODED_LIMIT;
    limits.total_bytes = webp_sequence_source_limit + 4ull * 1024 * 1024;
    limits.seconds = 120;
    limits.cpu_seconds = 110;
    limits.files = FORGE_SEQUENCE_FRAME_MAX_COUNT + 2;
    limits.cancellation_grace_ms = 250;
    return limits;
}
WebpSequenceInfo write_webp_sequence(const std::filesystem::path& worker,
                                     const std::filesystem::path& project,
                                     const std::filesystem::path& folder,
                                     std::span<const std::string> frames, unsigned width,
                                     unsigned height, double fps, std::stop_token stop) {
    require(!stop.stop_requested(), "WebP animation cancelled");
    require(!worker.empty(), "WebP animation needs the packaged asset worker");
    require(frames.size() <= FORGE_SEQUENCE_FRAME_MAX_COUNT, "WebP animation has too many frames");
    const auto durations = webp_sequence_durations(fps, unsigned(frames.size()));
    require(width && height && width <= FORGE_VIEW_IMAGE_MAX_EXTENT &&
                height <= FORGE_VIEW_IMAGE_MAX_EXTENT,
            "WebP animation axes must be between 1 and 4096");
    asset_storage::ordinary(folder);
    const auto destination = folder / "animation.webp";
    asset_storage::ordinary(destination);
    require(!std::filesystem::exists(destination), "WebP animation destination already exists");
    ImportProcessRequest request{{{"recipe", webp_sequence_recipe},
                                  {"profile", webp_sequence_profile},
                                  {"width", width},
                                  {"height", height},
                                  {"durations_ms", durations}},
                                 {}};
    std::size_t total = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        require(!stop.stop_requested(), "WebP animation cancelled");
        const auto relative = std::filesystem::u8path(frames[i]);
        require(relative.filename() == relative && !relative.empty() &&
                    relative.extension() == ".png",
                "WebP animation requires flat completed PNG frame names");
        const auto source = folder / relative;
        asset_storage::ordinary(source);
        auto bytes = read_bytes(source, std::min<std::size_t>(FORGE_VIEW_IMAGE_ENCODED_LIMIT,
                                                              webp_sequence_source_limit - total));
        total += bytes.size();
        request.inputs.push_back({"frame-" + std::to_string(i) + ".png", std::move(bytes)});
    }
    std::vector<ArtifactFile> output;
    try {
        output = run_import_process(worker, project, std::move(request),
                                    webp_sequence_worker_limits(), stop);
    } catch (const std::exception&) {
        require(!stop.stop_requested(), "WebP animation cancelled before publication");
        throw;
    }
    require(!stop.stop_requested(), "WebP animation cancelled before publication");
    require(output.size() == 1 && output[0].name == "animation.webp" &&
                output[0].bytes.size() >= 12 &&
                output[0].bytes.size() <= FORGE_VIEW_IMAGE_ENCODED_LIMIT &&
                std::memcmp(output[0].bytes.data(), "RIFF", 4) == 0 &&
                std::memcmp(output[0].bytes.data() + 8, "WEBP", 4) == 0,
            "WebP animation worker returned an unexpected container");
    asset_storage::ordinary(destination);
    require(!std::filesystem::exists(destination), "WebP animation destination already exists");
    asset_storage::replace(destination,
                           std::string_view(reinterpret_cast<const char*>(output[0].bytes.data()),
                                            output[0].bytes.size()));
    return {output[0].bytes.size(), validate_webp_sequence_durations(durations)};
}
} // namespace forge::asset_detail
