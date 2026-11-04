#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace forge {
struct MeshVertexFetch {
    // Native ByteAddressBuffer g_MeshVertices + ForgeLoadMeshVertex(uint vertexId).
    std::string source;
    // Dense transient slots mapped from persistent material UV-set semantics.
    std::vector<unsigned> uv_sets;
    bool normal{}, tangent{}, color{}, skin{}, triangles{};
    unsigned morph_count{};
    // Packed byte offset / scalar width for POSITION, NORMAL, TANGENT, COLOR_0
    // and selected UV channels, four slots per row. UINT32_MAX is absent.
    std::vector<std::array<std::uint32_t, 4>> morph_offsets;
    bool temporal = false;
    bool wind = false;
};
// One semantic layout for generated geometry and its typed pixel inputs.
std::string mesh_shader_varyings(const MeshVertexFetch&, bool sprite_profile,
                                 std::string_view name = "ForgeVarying", bool front_face = false);
} // namespace forge
