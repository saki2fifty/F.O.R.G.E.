#pragma once
#include <forge/gltf_source.hpp>

namespace forge::asset_detail {
// Private worker transport: source provenance remains in the captured bundle.
// Native decoders never receive unbounded output sizes or unspecified filters.
GltfSourceBundle decode_gltf_meshopt(const GltfSourceBundle& captured,
                                     std::size_t decoded_limit = 512 * 1024 * 1024,
                                     std::stop_token cancel = {});
} // namespace forge::asset_detail
