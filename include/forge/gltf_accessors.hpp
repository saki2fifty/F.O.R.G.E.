#pragma once
#include <forge/gltf_source.hpp>

namespace forge {
struct GltfAccessorLimits {
    std::size_t elements_per_accessor = 16 * 1024 * 1024;
    std::size_t total_components = 128 * 1024 * 1024;
};
struct GltfAccessorAdmission {
    std::size_t accessors = 0, elements = 0, components = 0, sparse_replacements = 0;
};
// Reject malformed binary ranges/numbers before native accessor conversion.
// Supports core scalar/vector/matrix layouts, normalized integer attributes,
// interleaving, zero-backed and sparse storage. Semantic uses (mesh attributes,
// animation channels, joints, indices) need their additional domain validation.
GltfAccessorAdmission validate_gltf_accessors(const GltfSourceBundle& source,
                                              GltfAccessorLimits limits = {},
                                              std::stop_token cancel = {});
} // namespace forge
