#pragma once
#include "image_pixels.hpp"
#include "view_image_limits.h"
#include <cstddef>
#include <span>
#include <stop_token>
#include <vector>
namespace forge {
// Independent bounded RGBE writer. Shared-exponent quantization is not lossless.
// No alpha, exposure, tone mapping, source metadata or calibrated photometry.
std::vector<std::byte> encode_hdr_image(const Rgb32Pixels&,
                                        std::size_t byte_limit = FORGE_VIEW_IMAGE_ENCODED_LIMIT,
                                        std::stop_token stop = {});
} // namespace forge
