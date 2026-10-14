#pragma once
#include <array>
#include <cstdint>
namespace forge {
// Shared recorded sample values; domains retain their own source and history.
enum class MeshBrushSymmetry : std::uint8_t { None = 0, X = 1, Y = 2, Z = 4 };
constexpr MeshBrushSymmetry operator|(MeshBrushSymmetry a, MeshBrushSymmetry b) {
    return static_cast<MeshBrushSymmetry>(static_cast<std::uint8_t>(a) |
                                          static_cast<std::uint8_t>(b));
}
struct MeshBrushSample {
    std::array<double, 3> point{};
    double pressure = 1;
};
} // namespace forge
