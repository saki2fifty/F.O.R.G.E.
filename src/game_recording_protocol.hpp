#pragma once
#include "avi_movie.hpp"
#include "base64.hpp"
#include <nlohmann/json.hpp>
#include <utility>

namespace forge {
struct RecordedAudioFrame {
    unsigned frame = 0;
    std::uint64_t start_audio_frame = 0;
    std::vector<std::byte> pcm;
};
inline asset_detail::MovieFrameRate recording_rate(const nlohmann::json& value) {
    if (!value.is_object() || !value.at("rate").is_number_unsigned() ||
        !value.at("scale").is_number_unsigned() ||
        value.at("rate").get<std::uint64_t>() > asset_detail::movie_rate_integer_limit ||
        value.at("scale").get<std::uint64_t>() > asset_detail::movie_rate_scale_limit)
        throw std::runtime_error("Recording receipt has an invalid frame rate");
    const asset_detail::MovieFrameRate rate{value.at("rate").get<std::uint32_t>(),
                                            value.at("scale").get<std::uint32_t>()};
    asset_detail::validate_avi_game_timing(rate, 1);
    return rate;
}
inline std::pair<std::uint64_t, std::uint64_t>
recorded_audio_bounds(unsigned frame, asset_detail::MovieFrameRate rate) {
    if (frame >= FORGE_SEQUENCE_FRAME_MAX_COUNT)
        throw std::runtime_error("Recording frame exceeds its profile");
    asset_detail::validate_avi_game_timing(rate, frame + 1);
    return {asset_detail::avi_movie_audio_frames(rate, frame),
            asset_detail::avi_movie_audio_frames(rate, frame + 1)};
}
inline nlohmann::json recorded_audio_document(unsigned frame, asset_detail::MovieFrameRate rate,
                                              std::span<const std::byte> pcm) {
    const auto [start, end] = recorded_audio_bounds(frame, rate);
    if (pcm.size() != (end - start) * 4)
        throw std::runtime_error("Recording PCM does not match its exact frame interval");
    return {{"frame", frame},
            {"format", "s16le-stereo-48000-v1"},
            {"start_audio_frame", start},
            {"audio_frames", end - start},
            {"data", encode_base64(pcm)}};
}
inline RecordedAudioFrame read_recorded_audio(const nlohmann::json& value, unsigned frame,
                                              asset_detail::MovieFrameRate rate) {
    const auto [start, end] = recorded_audio_bounds(frame, rate);
    if (!value.is_object() || value.size() != 5 ||
        value.value("format", "") != "s16le-stereo-48000-v1" ||
        !value.at("frame").is_number_unsigned() || value.at("frame") != frame ||
        !value.at("start_audio_frame").is_number_unsigned() ||
        value.at("start_audio_frame") != start || !value.at("audio_frames").is_number_unsigned() ||
        value.at("audio_frames") != end - start || !value.at("data").is_string())
        throw std::runtime_error("Recording PCM receipt has a stale or incompatible interval");
    auto pcm =
        decode_base64(value.at("data").get_ref<const std::string&>(), std::size_t(end - start) * 4);
    if (pcm.size() != (end - start) * 4)
        throw std::runtime_error("Recording PCM receipt has an incomplete sample interval");
    return {frame, start, std::move(pcm)};
}
} // namespace forge
