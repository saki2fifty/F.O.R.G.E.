#pragma once
#include "../avi_movie.hpp"
#include "sequence_frame_output.hpp"
#include "sequence_frame_plan.hpp"
#include <filesystem>

namespace forge::ui {
struct GameRecordingRequest {
    std::filesystem::path destination;
    double fps = 30;
    int frames = 180;
    SequenceFrameOutput output{true, 1280, 720, false, true, FORGE_JPEG_DEFAULT_QUALITY};
    asset_detail::MovieFrameRate rate() const {
        output.validate();
        if (!output.fixed || !output.avi || output.webp || frames < 1)
            throw std::runtime_error(
                "Game recording requires fixed AVI output and a positive frame count");
        const auto value = asset_detail::avi_movie_frame_rate(fps);
        asset_detail::validate_avi_game_timing(value, unsigned(frames));
        return value;
    }
    SequenceFramePlan plan() const { return {0, fps, frames}; }
};
} // namespace forge::ui
