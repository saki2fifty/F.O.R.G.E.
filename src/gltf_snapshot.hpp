#pragma once
#include <forge/derived_cache.hpp>
#include <forge/gltf_source.hpp>
namespace forge::asset_detail {
// Private parent/worker transport. Retains shared GLB/image backing storage,
// offsets and original source provenance. No filesystem access during decoding.
std::vector<ArtifactFile> encode_gltf_snapshot(const GltfSourceBundle& source,
                                               GltfSourceLimits limits = {},
                                               std::stop_token stop = {});
GltfSourceBundle decode_gltf_snapshot(std::vector<ArtifactFile> files, GltfSourceLimits limits = {},
                                      std::stop_token stop = {});
} // namespace forge::asset_detail
