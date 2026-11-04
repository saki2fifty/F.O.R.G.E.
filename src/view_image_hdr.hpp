#pragma once
#include "image_pixels.hpp"
#include "view_image_limits.h"
#include <cstddef>
#include <span>
#include <stop_token>
#include <vector>
namespace forge {
struct ViewImagePreview;
// Independent bounded RGBE writer. Shared-exponent quantization is not lossless.
// No alpha, exposure, tone mapping, source metadata or calibrated photometry.
std::vector<std::byte> encode_hdr_image(const Rgb32Pixels&,
                                        std::size_t byte_limit = FORGE_VIEW_IMAGE_ENCODED_LIMIT,
                                        std::stop_token stop = {});
// Only the existing FORGE writer's fixed-primary RGBE output profile. Produces
// unit-exposure per-channel c/(1+c), then sRGB and nearest8-bit opaque PNG.
// No generic source-file HDR decoder or match to the scene display transform.
ViewImagePreview preview_hdr_image(std::span<const std::byte>, std::stop_token = {});
} // namespace forge
