#pragma once
#include <array>
#include <cstddef>
namespace forge {
using MeshInstanceRow = std::array<float, 4>;
inline constexpr std::size_t mesh_static_instance_rows = 7;
// Explicit quad edges share the static packet size. This is transient draw data,
// not a mesh asset, pose history or installed gameplay structure.
struct MeshRibbonValues {
    std::array<double, 3> first{}, last{}, first_side{}, last_side{}, normal{};
    std::array<std::array<float, 3>, 2> color{};
    std::array<float, 2> opacity{1, 1};
    std::array<double, 2> texture_v{};
};
} // namespace forge
