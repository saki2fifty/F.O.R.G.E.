#pragma once
#include "gltf_native.hpp"
namespace forge::asset_detail {
// Import-only expansion into ordinary source-node records. No ECS allocation,
// source write, persistent identity allocation or GPU objects.
std::unique_ptr<GltfSourceBundle> expand_gltf_instances(const NativeGltfDocument&);
} // namespace forge::asset_detail
