#pragma once
#include "avi_movie.hpp"
#include "base64.hpp"
#include <algorithm>
#include <forge/input.hpp>
#include <nlohmann/json.hpp>
#include <utility>

namespace forge {
struct RecordedAudioFrame {
    unsigned frame = 0;
    std::uint64_t start_audio_frame = 0;
    std::vector<std::byte> pcm;
};
struct LiveAudioChunk {
    std::uint64_t start_audio_frame = 0;
    std::vector<std::byte> pcm;
};
inline LiveAudioChunk read_live_audio(const nlohmann::json& value, std::uint64_t cursor,
                                      unsigned maximum_frames) {
    if (!maximum_frames || maximum_frames > 4096 || !value.is_object() || value.size() != 4 ||
        value.value("format", "") != "s16le-stereo-48000-v1" ||
        !value.at("start_audio_frame").is_number_unsigned() ||
        value.at("start_audio_frame") != cursor || !value.at("audio_frames").is_number_unsigned() ||
        value.at("audio_frames").get<std::uint64_t>() > maximum_frames ||
        !value.at("data").is_string())
        throw std::runtime_error("Live recording returned an invalid PCM interval");
    const auto bytes = std::size_t(value.at("audio_frames").get<unsigned>()) * 4;
    auto pcm = decode_base64(value.at("data").get_ref<const std::string&>(), bytes);
    if (pcm.size() != bytes)
        throw std::runtime_error("Live recording PCM length does not match its interval");
    return {cursor, std::move(pcm)};
}
inline void validate_recording_visits(const nlohmann::json&, const nlohmann::json&, std::uint64_t,
                                      AssetId, bool live = false);
inline void validate_live_audio_status(const nlohmann::json& value, std::uint64_t world,
                                       AssetId scene, std::uint64_t capture = 0,
                                       std::string_view scope = "scene",
                                       const nlohmann::json& previous = nlohmann::json()) {
    if (!world || !scene || !value.is_object() || !value.at("generation").is_number_unsigned() ||
        value.at("generation") == 0 || (capture && value.at("generation") != capture) ||
        !value.at("world_generation").is_number_unsigned() ||
        value.at("world_generation") != world || value.at("scene").get<AssetId>() != scene ||
        !value.at("active_generation").is_number_unsigned() ||
        !value.at("captured_frames").is_number_unsigned() ||
        !value.at("read_frames").is_number_unsigned() ||
        !value.at("queued_frames").is_number_unsigned() || value.at("queued_frames") > 96000u ||
        !value.at("sample_rate").is_number_unsigned() || value.at("sample_rate") != 48000u ||
        !value.at("channels").is_number_unsigned() || value.at("channels") != 2u ||
        !value.at("maximum_queued_frames").is_number_unsigned() ||
        value.at("maximum_queued_frames") != 96000u || !value.at("error").is_string() ||
        value.at("error").get_ref<const std::string&>().size() > 1024)
        throw std::runtime_error("Live recording returned an invalid capture identity or progress");
    const auto observed = value.at("captured_frames").get<std::uint64_t>();
    const auto read = value.at("read_frames").get<std::uint64_t>();
    const auto queued = value.at("queued_frames").get<std::uint64_t>();
    const auto state = value.value("state", "");
    if (read > observed || observed - read != queued ||
        (state != "recording" && state != "stopped" && state != "failed") ||
        ((state == "failed") != !value.at("error").get_ref<const std::string&>().empty()))
        throw std::runtime_error("Live recording progress does not match its committed PCM");
    if ((scope != "scene" && scope != "session") || value.value("capture_scope", "scene") != scope)
        throw std::runtime_error("Live recording returned an incompatible capture scope");
    if (scope == "session") {
        const auto& visits = value.at("visits");
        if (!visits.is_array() || visits.empty())
            throw std::runtime_error("Live session recording has no Scene visits");
        const auto last = visits.back().at("generation").get<std::uint64_t>();
        validate_recording_visits(value, previous, last, visits.back().at("scene").get<AssetId>(),
                                  true);
        if (visits.front().at("generation") != world ||
            visits.front().at("scene").get<AssetId>() != scene ||
            last > value.at("active_generation").get<std::uint64_t>() ||
            (state == "recording" && last != value.at("active_generation")))
            throw std::runtime_error(
                "Live session visits do not match its capture and active World");
    }
}
inline void validate_recording_visits(const nlohmann::json& value, const nlohmann::json& previous,
                                      std::uint64_t generation, AssetId scene, bool live) {
    const auto* counter = live ? "captured_frames" : "audio_frames";
    if (value.value("capture_scope", "") != "session" ||
        value.value("profile", "") != (live ? "live-session-game-v1" : "offline-session-game-v2"))
        throw std::runtime_error("Runtime returned an incompatible session movie profile");
    const auto& visits = value.at("visits");
    if (!visits.is_array() || visits.empty() || visits.size() > input_recording_scene_limit ||
        !value.at(counter).is_number_unsigned())
        throw std::runtime_error("Recording has invalid Scene visit progress");
    std::uint64_t last_generation = 0, last_audio = 0;
    for (const auto& visit : visits) {
        if (!visit.is_object() || visit.size() != 4 ||
            !visit.at("generation").is_number_unsigned() ||
            !visit.at("audio_frame").is_number_unsigned() || !visit.at("source").is_string())
            throw std::runtime_error("Recording has an invalid Scene visit receipt");
        const auto next = visit.at("generation").get<std::uint64_t>();
        const auto audio = visit.at("audio_frame").get<std::uint64_t>();
        const auto& source = visit.at("source").get_ref<const std::string&>();
        if (next <= last_generation || audio < last_audio ||
            audio > value.at(counter).get<std::uint64_t>() || (!last_generation && audio != 0) ||
            !visit.at("scene").get<AssetId>() || source.size() != 64 ||
            source.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error(
                "Recording Scene visit identity or sample boundary is invalid");
        last_generation = next;
        last_audio = audio;
    }
    if (last_generation != generation || visits.back().at("scene").get<AssetId>() != scene)
        throw std::runtime_error("Recording Scene visits do not match the admitted world");
    if (previous.is_object() && previous.value("capture_scope", "") == "session") {
        const auto& prior = previous.at("visits");
        if (prior.size() > visits.size() || !std::equal(prior.begin(), prior.end(), visits.begin()))
            throw std::runtime_error("Recording rewrote an already observed Scene visit");
    }
}
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
inline std::size_t recorded_audio_response_budget(unsigned frame,
                                                  asset_detail::MovieFrameRate rate) {
    const auto [start, end] = recorded_audio_bounds(frame, rate);
    return std::size_t(((end - start) * 4 + 2) / 3 * 4) + 1024;
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
