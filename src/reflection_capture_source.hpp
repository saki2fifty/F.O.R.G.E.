#pragma once
#include <forge/texture_asset.hpp>
namespace forge {
// One complete finite, nonnegative, opaque RGBA16F cube, encoded as a DDS source
// for the existing texture importer. No IO, asset identity or publication here.
std::vector<std::byte> encode_reflection_capture_source(const TextureData&,
                                                        TextureLimits limits = {});
} // namespace forge
