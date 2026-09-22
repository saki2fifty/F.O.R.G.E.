#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
namespace forge::asset_detail {
// List topologies have no restart sentinel. Pick by referenced index, not by
// total vertex count; preserve exact uint32 working values in CPU geometry.
inline unsigned mesh_index_width(std::span<const std::uint32_t> indices) {
    if (indices.empty())
        return 0;
    return *std::max_element(indices.begin(), indices.end()) <= UINT16_MAX ? 2u : 4u;
}
} // namespace forge::asset_detail
