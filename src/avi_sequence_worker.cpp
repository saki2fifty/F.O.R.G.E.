#include "sequence_frame_pixels.hpp"
#include "texture_encode.h"
#include <limits>
#include <stdexcept>

namespace forge::asset_detail {
std::vector<ArtifactFile> execute_avi_sequence_recipe(ImportProcessRequest request,
                                                      std::stop_token stop) {
    auto require = [](bool value, const char* reason) {
        if (!value)
            throw std::runtime_error(reason);
    };
    auto cancelled = [&] { require(!stop.stop_requested(), "AVI movie cancelled"); };
    cancelled();
    const auto& payload = request.payload;
    const bool sound = payload.is_object() && payload.value("recipe", "") == avi_game_recipe;
    const auto& selected =
        sequence_encoding_profile(sound ? SequenceEncoding::AviGame : SequenceEncoding::AviMjpeg);
    require(payload.is_object() && payload.size() == 8 && payload.at("recipe") == selected.recipe &&
                payload.at("profile") == selected.profile,
            "Invalid AVI movie worker profile");
    auto integer = [&](const char* field, std::uint64_t maximum) {
        const auto& value = payload.at(field);
        require(value.is_number_unsigned() && value.get<std::uint64_t>() > 0 &&
                    value.get<std::uint64_t>() <= maximum,
                "Invalid AVI movie numeric field");
        return value.get<unsigned>();
    };
    const auto width = integer("width", FORGE_VIEW_IMAGE_MAX_EXTENT);
    const auto height = integer("height", FORGE_VIEW_IMAGE_MAX_EXTENT);
    const auto frames = integer("frames", FORGE_SEQUENCE_FRAME_MAX_COUNT);
    const MovieFrameRate rate{integer("rate", std::numeric_limits<std::int32_t>::max()),
                              integer("scale", std::numeric_limits<std::int32_t>::max())};
    const auto& encoded_quality = payload.at("jpeg_quality");
    require(encoded_quality.is_number_integer() && encoded_quality >= FORGE_JPEG_QUALITY_MIN &&
                encoded_quality <= FORGE_JPEG_QUALITY_MAX,
            "Movie JPEG quality must be an integer from 1 to 100");
    const auto quality = encoded_quality.get<int>();
    require(request.inputs.size() == frames + unsigned(sound),
            "Movie frame count does not match its snapshots");
    ArtifactFile audio;
    if (sound) {
        validate_avi_game_timing(rate, frames);
        const auto samples = avi_movie_audio_frames(rate, frames);
        audio = std::move(request.inputs.back());
        request.inputs.pop_back();
        require(audio.name == "audio.s16le" && audio.bytes.size() == samples * 4,
                "Game movie PCM does not match its exact video duration");
    }
    validate_sequence_snapshots(request.inputs, "AVI movie", audio.bytes.size());
    AviMovie movie(width, height, rate, frames,
                   sound ? MovieAudio::PcmStereo48k : MovieAudio::None);
    for (unsigned frame_index = 0; auto& input : request.inputs) {
        cancelled();
        auto png = std::move(input.bytes);
        auto pixels = decode_sequence_frame(png, width, height, "AVI movie");
        struct Encoded : ForgeImageBytes {
            Encoded() : ForgeImageBytes{} {}
            ~Encoded() { forge_image_bytes_free(this); }
        } frame;
        require(forge_jpeg_encode_rgba8(pixels.pixels, pixels.size, width, height, pixels.stride,
                                        quality, FORGE_VIEW_IMAGE_ENCODED_LIMIT, &frame),
                frame.error);
        cancelled();
        const std::span<const std::byte> jpeg{reinterpret_cast<const std::byte*>(frame.bytes),
                                              frame.size};
        if (sound) {
            const auto begin = avi_movie_audio_frames(rate, frame_index);
            const auto end = avi_movie_audio_frames(rate, frame_index + 1);
            std::vector<std::int16_t> stereo;
            stereo.reserve(std::size_t(end - begin) * 2);
            for (auto at = begin * 4; at < end * 4; at += 2)
                stereo.push_back(
                    std::int16_t(std::to_integer<unsigned>(audio.bytes[at]) |
                                 (std::to_integer<unsigned>(audio.bytes[at + 1]) << 8)));
            movie.append(jpeg, stereo);
        } else
            movie.append(jpeg);
        ++frame_index;
    }
    cancelled();
    auto output = movie.finish();
    cancelled();
    std::vector<ArtifactFile> result;
    result.push_back({std::string(selected.filename), std::move(output)});
    return result;
}
} // namespace forge::asset_detail
