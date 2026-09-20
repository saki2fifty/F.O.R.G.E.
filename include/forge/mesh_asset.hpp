#pragma once
#include <array>
#include <forge/asset_ref.hpp>
#include <span>
#include <variant>
#include <vector>

namespace forge {

enum class MeshTopology { Points, Lines, Triangles };
struct MeshStream {
    std::string semantic;
    unsigned components = 0;
    // Admitted cooked scalars, never a source accessor or a native GPU pointer.
    std::variant<std::vector<float>, std::vector<std::uint32_t>> values;
    std::size_t scalar_count() const;
};
struct MeshBounds {
    std::array<float, 3> minimum{}, maximum{};
    auto operator<=>(const MeshBounds&) const = default;
};
struct MeshPart {
    MeshTopology topology = MeshTopology::Triangles;
    std::uint32_t vertices = 0, material_slot = 0;
    std::vector<MeshStream> streams;
    std::vector<std::uint32_t> indices;
    std::vector<std::vector<MeshStream>> morph_targets;
    MeshBounds bounds;
    // Prepared skin draw palette -> ordered skin binding joint index. Empty
    // means unprepared/no skin. A GPU skin consumer requires a nonempty palette.
    // Actual Skeleton identity and inverse binds belong to the skin binding.
    std::vector<std::uint32_t> joint_palette;
    const MeshStream* find(std::string_view semantic) const;
};
struct MeshLod {
    // Decreasing projected diameter / viewport height; first LOD starts at 1.
    float screen_coverage = 1;
    std::vector<MeshPart> parts;
};
struct MeshData {
    std::uint32_t material_slots = 1;
    std::vector<MeshLod> lods;
    std::vector<std::string> morph_names;
    std::vector<float> morph_defaults;
    // Scalar storage, independent of allocator/container bookkeeping.
    std::size_t byte_size() const;
    // Conservative retained allocation estimate, including container capacity.
    // Allocator headers are unknown; short-string capacity may be counted twice.
    std::size_t resident_bytes() const;
};
struct MeshLimits {
    std::size_t bytes = 512 * 1024 * 1024, vertices = 16 * 1024 * 1024;
    std::size_t indices = 64 * 1024 * 1024, parts = 65536;
    unsigned streams = 64, morph_targets = 256, lods = 16;
};
MeshBounds mesh_bounds(const MeshPart& part);
void validate_mesh(const MeshData& mesh, MeshLimits limits = {});
// Versioned little-endian cooked artifact. No host ABI, asset UUID allocation,
// source parser, editor state or graphics device is involved in runtime decoding.
std::vector<std::byte> encode_mesh(const MeshData& mesh, MeshLimits limits = {});
MeshData decode_mesh(std::span<const std::byte> bytes, MeshLimits limits = {});
} // namespace forge
