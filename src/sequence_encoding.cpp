#include "sequence_encoding.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "webp_sequence.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace forge::asset_detail {
namespace {
void require(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
} // namespace
const SequenceEncodingProfile& sequence_encoding_profile(SequenceEncoding format) {
    static const SequenceEncodingProfile webp{"WebP animation",
                                              webp_sequence_recipe,
                                              webp_sequence_profile,
                                              "animation.webp",
                                              "image/webp",
                                              "WEBP",
                                              FORGE_VIEW_IMAGE_ENCODED_LIMIT};
    static const SequenceEncodingProfile movie{
        "AVI movie", avi_sequence_recipe, avi_sequence_profile, "animation.avi", "video/vnd.avi",
        "AVI ",      avi_movie_byte_limit};
    static const SequenceEncodingProfile game{
        "Game movie",    avi_game_recipe, avi_game_profile,    "game.avi",
        "video/vnd.avi", "AVI ",          avi_movie_byte_limit};
    switch (format) {
    case SequenceEncoding::Webp:
        return webp;
    case SequenceEncoding::AviMjpeg:
        return movie;
    case SequenceEncoding::AviGame:
        return game;
    }
    throw std::runtime_error("Unsupported sequence encoding format");
}
WorkerLimits sequence_encoding_worker_limits(SequenceEncoding format) {
    WorkerLimits limits;
    limits.memory_bytes = 1024ull * 1024 * 1024;
    limits.file_bytes = sequence_encoding_profile(format).output_byte_limit;
    limits.total_bytes = sequence_snapshot_byte_limit + 4ull * 1024 * 1024;
    limits.seconds = 120;
    limits.cpu_seconds = 110;
    limits.files = FORGE_SEQUENCE_FRAME_MAX_COUNT + 2;
    limits.cancellation_grace_ms = 250;
    return limits;
}
std::size_t write_encoded_sequence(const std::filesystem::path& worker,
                                   const std::filesystem::path& project,
                                   const std::filesystem::path& folder,
                                   std::span<const std::string> frames, unsigned width,
                                   unsigned height, double fps, SequenceEncoding format,
                                   int quality, std::stop_token stop) {
    const auto& selected = sequence_encoding_profile(format);
    const auto name = std::string(selected.name);
    require(!stop.stop_requested(), name + " cancelled");
    require(!worker.empty(), name + " needs the packaged asset worker");
    require(frames.size() <= FORGE_SEQUENCE_FRAME_MAX_COUNT, name + " has too many frames");
    ImportProcessRequest request{{{"recipe", selected.recipe},
                                  {"profile", selected.profile},
                                  {"width", width},
                                  {"height", height}},
                                 {}};
    if (format == SequenceEncoding::Webp)
        request.payload["durations_ms"] = webp_sequence_durations(fps, unsigned(frames.size()));
    else {
        const auto rate = avi_movie_frame_rate(fps);
        require(!frames.empty(), "Movie requires 1 to 4096 frames");
        require(quality >= FORGE_JPEG_QUALITY_MIN && quality <= FORGE_JPEG_QUALITY_MAX,
                "Movie JPEG quality must be from 1 to 100");
        request.payload["rate"] = rate.rate;
        request.payload["scale"] = rate.scale;
        request.payload["frames"] = frames.size();
        request.payload["jpeg_quality"] = quality;
    }
    require(width && height && width <= FORGE_VIEW_IMAGE_MAX_EXTENT &&
                height <= FORGE_VIEW_IMAGE_MAX_EXTENT,
            name + " axes must be between 1 and 4096");
    std::uint64_t audio_frames = 0;
    if (format == SequenceEncoding::AviGame) {
        const auto rate = avi_movie_frame_rate(fps);
        validate_avi_game_timing(rate, unsigned(frames.size()));
        audio_frames = avi_movie_audio_frames(rate, unsigned(frames.size()));
    }
    asset_storage::ordinary(folder);
    const auto destination = folder / selected.filename;
    asset_storage::ordinary(destination);
    require(!std::filesystem::exists(destination), name + " destination already exists");
    std::size_t total = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        require(!stop.stop_requested(), name + " cancelled");
        const auto relative = std::filesystem::u8path(frames[i]);
        require(relative.filename() == relative && !relative.empty() &&
                    relative.extension() == ".png",
                name + " requires flat completed PNG frame names");
        const auto source = folder / relative;
        asset_storage::ordinary(source);
        auto bytes =
            read_bytes(source, std::min<std::size_t>(FORGE_VIEW_IMAGE_ENCODED_LIMIT,
                                                     sequence_snapshot_byte_limit - total));
        total += bytes.size();
        request.inputs.push_back({"frame-" + std::to_string(i) + ".png", std::move(bytes)});
    }
    if (format == SequenceEncoding::AviGame) {
        const auto source = folder / "audio.s16le";
        asset_storage::ordinary(source);
        auto pcm = read_bytes(
            source, std::min(avi_game_audio_byte_limit, sequence_snapshot_byte_limit - total));
        require(pcm.size() == audio_frames * 4,
                "Game movie PCM does not match its exact video duration");
        request.inputs.push_back({"audio.s16le", std::move(pcm)});
    }
    std::vector<ArtifactFile> output;
    try {
        output = run_import_process(worker, project, std::move(request),
                                    sequence_encoding_worker_limits(format), stop);
    } catch (const std::exception&) {
        require(!stop.stop_requested(), name + " cancelled before publication");
        throw;
    }
    require(!stop.stop_requested(), name + " cancelled before publication");
    require(output.size() == 1 && output[0].name == selected.filename &&
                output[0].bytes.size() >= 12 &&
                output[0].bytes.size() <= selected.output_byte_limit &&
                std::memcmp(output[0].bytes.data(), "RIFF", 4) == 0 &&
                std::memcmp(output[0].bytes.data() + 8, selected.riff_type.data(), 4) == 0,
            name + " worker returned an unexpected container");
    asset_storage::ordinary(destination);
    require(!std::filesystem::exists(destination), name + " destination already exists");
    asset_storage::replace(destination,
                           std::string_view(reinterpret_cast<const char*>(output[0].bytes.data()),
                                            output[0].bytes.size()));
    return output[0].bytes.size();
}
AviSequenceInfo write_avi_sequence(const std::filesystem::path& worker,
                                   const std::filesystem::path& project,
                                   const std::filesystem::path& folder,
                                   std::span<const std::string> frames, unsigned width,
                                   unsigned height, double fps, int quality, std::stop_token stop) {
    const auto rate = avi_movie_frame_rate(fps);
    return {write_encoded_sequence(worker, project, folder, frames, width, height, fps,
                                   SequenceEncoding::AviMjpeg, quality, stop),
            rate};
}
AviSequenceInfo write_avi_game(const std::filesystem::path& worker,
                               const std::filesystem::path& project,
                               const std::filesystem::path& folder,
                               std::span<const std::string> frames, unsigned width, unsigned height,
                               double fps, int quality, std::stop_token stop) {
    const auto rate = avi_movie_frame_rate(fps);
    const auto samples = avi_movie_audio_frames(rate, unsigned(frames.size()));
    return {write_encoded_sequence(worker, project, folder, frames, width, height, fps,
                                   SequenceEncoding::AviGame, quality, stop),
            rate, samples};
}
} // namespace forge::asset_detail
