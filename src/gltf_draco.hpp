#pragma once
#include <forge/gltf_source.hpp>
namespace forge::asset_detail {
// Worker-only native decode. The caller supplies process memory/time limits;
// output limits below do not bound the decoder's intermediate allocations.
GltfSourceBundle decode_gltf_draco(const GltfSourceBundle& captured,
                                   std::size_t decoded_limit = 512 * 1024 * 1024,
                                   std::stop_token cancel = {});
} // namespace forge::asset_detail
