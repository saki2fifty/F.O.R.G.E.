#pragma once
#include "../view_image_limits.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace forge::ui {
struct SequenceFrameExtent {
    unsigned width = 0, height = 0;
    bool operator==(const SequenceFrameExtent&) const = default;
};
struct SequenceFrameOutput {
    bool fixed = false;
    int width = 1280, height = 720;
    bool valid() const {
        return !fixed ||
               (width > 0 && height > 0 && width <= int(FORGE_VIEW_IMAGE_MAX_EXTENT) &&
                height <= int(FORGE_VIEW_IMAGE_MAX_EXTENT) &&
                std::uint64_t(width) * std::uint64_t(height) * 4 <= FORGE_VIEW_IMAGE_PIXEL_LIMIT);
    }
    void validate() const {
        if (!valid())
            throw std::runtime_error("Choose width and height from 1 to 4096 pixels");
    }
    std::optional<SequenceFrameExtent> extent() const {
        if (!fixed || !valid())
            return {};
        return SequenceFrameExtent{unsigned(width), unsigned(height)};
    }
};
struct SequenceFrameFit {
    float x = 0, y = 0, width = 0, height = 0;
};
// Display geometry only. The renderer supplies real output pixels at the chosen
// extent; fitting the editor preview never resamples the exported PNG.
inline SequenceFrameFit fit_sequence_frame(SequenceFrameExtent output, float width, float height) {
    if (!output.width || !output.height || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0 || height <= 0)
        return {};
    const auto scale = std::min(double(width) / output.width, double(height) / output.height);
    const auto fitted_width = std::min(width, float(output.width * scale));
    const auto fitted_height = std::min(height, float(output.height * scale));
    return {(width - fitted_width) / 2, (height - fitted_height) / 2, fitted_width, fitted_height};
}
} // namespace forge::ui
