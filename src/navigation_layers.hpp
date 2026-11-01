#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <forge/navigation_build.hpp>
#include <span>
#include <vector>

namespace forge::navigation_detail {
inline constexpr unsigned layer_width = 32, max_layers = 64, max_obstacles = 8;
struct HeightfieldLayer {
    unsigned x = 0, z = 0, index = 0, hmin = 0, hmax = 0;
    unsigned minx = 0, maxx = 0, minz = 0, maxz = 0;
    std::array<float, 3> minimum{}, maximum{};
    std::array<unsigned char, layer_width * layer_width * 3> cells{};
};
struct HeightfieldLayers {
    NavigationSettings settings;
    std::array<float, 3> minimum{}, maximum{};
    std::vector<HeightfieldLayer> layers;
};
// This is a FORGE raw-cell format. Native headers and mutable cache state are
// constructed only after structural admission; they are never deserialized.
void validate_layers(const HeightfieldLayers&);
std::vector<std::byte> encode_layers(const HeightfieldLayers&);
HeightfieldLayers decode_layers(std::span<const std::byte>);
nlohmann::json layer_properties(const HeightfieldLayers&);
} // namespace forge::navigation_detail
