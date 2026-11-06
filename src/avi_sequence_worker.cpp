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
    require(payload.is_object() && payload.size() == 8 &&
                payload.at("recipe") == avi_sequence_recipe &&
                payload.at("profile") == avi_sequence_profile,
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
    require(request.inputs.size() == frames, "Movie frame count does not match its snapshots");
    validate_sequence_snapshots(request.inputs, "AVI movie");
    AviMovie movie(width, height, rate, frames);
    for (auto& input : request.inputs) {
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
        movie.append({reinterpret_cast<const std::byte*>(frame.bytes), frame.size});
    }
    cancelled();
    auto output = movie.finish();
    cancelled();
    std::vector<ArtifactFile> result;
    result.push_back({"animation.avi", std::move(output)});
    return result;
}
} // namespace forge::asset_detail
