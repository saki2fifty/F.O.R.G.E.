#pragma once
#include <array>
#include <nlohmann/json.hpp>
namespace forge::asset_detail {
// Exact glTF affine/TRS admission shared by native and animation-only imports.
std::array<double, 16> gltf_node_matrix(const nlohmann::json& node);
// Ozz private input only: preserve explicit signed/zero TRS; convert admitted
// matrix rest into explicit TRS so native fallback channels retain the rest pose.
nlohmann::json canonical_ozz_rest(const nlohmann::json& node);
} // namespace forge::asset_detail
