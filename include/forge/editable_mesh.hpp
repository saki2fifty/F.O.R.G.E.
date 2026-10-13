#pragma once
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <forge/mesh_asset.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>

namespace forge {
// Small first-class authored mesh. Polygon/corner identities belong to this
// source; cooked MeshData remains an immutable runtime projection.
inline constexpr std::size_t editable_mesh_source_byte_limit = 2 * 1024 * 1024;
enum class MeshBrushSymmetry : std::uint8_t { None = 0, X = 1, Y = 2, Z = 4 };
constexpr MeshBrushSymmetry operator|(MeshBrushSymmetry a, MeshBrushSymmetry b) {
    return static_cast<MeshBrushSymmetry>(static_cast<std::uint8_t>(a) |
                                          static_cast<std::uint8_t>(b));
}
struct MeshBrushSample {
    std::array<double, 3> point{};
    double pressure = 1;
};
// Corner index is scoped to its authored face, and invalidated by topology edits.
struct MeshUvCorner {
    std::uint32_t face = 0;
    std::size_t corner = 0;
    auto operator<=>(const MeshUvCorner&) const = default;
};
enum class MeshUvAlignment : std::uint8_t { Minimum, Center, Maximum };
struct EditableMeshSource {
    nlohmann::json document;
    AssetId asset() const;
    static EditableMeshSource create_cube(AssetId asset);
    static EditableMeshSource create_sculpt_sphere(AssetId asset);
    static EditableMeshSource parse(std::span<const std::byte> bytes);
    void validate() const;
    MeshData cook() const;
    // Preserve geometry; reject undefined averaged normals before replacing source.
    void set_smooth_shading(bool smooth);
    void translate_vertices(std::span<const std::uint32_t> ids, std::array<double, 3> translation);
    void transform_vertices(std::span<const std::uint32_t> ids, std::array<double, 3> translation,
                            std::array<double, 3> rotation_degrees, std::array<double, 3> scale);
    std::uint32_t extrude_face(std::uint32_t face, double distance);
    // Uniform inward edge offset; preserves the selected inner face identity.
    std::uint32_t inset_face(std::uint32_t face, double distance);
    // Author the existing ordered face fan; first triangle keeps its face ID.
    void triangulate_faces(std::span<const std::uint32_t> faces);
    // Keep unused vertices until explicit cleanup; at least one face must remain.
    void delete_faces(std::span<const std::uint32_t> faces);
    std::size_t remove_unused_vertices();
    // Join exactly two triangles at this edge. Reject UV/opaque data conflicts.
    // Lower face ID survives; the shared edge remains the cooked fan diagonal.
    std::uint32_t join_triangles(std::array<std::uint32_t, 2> edge);
    // Detached copies within this Mesh; shared selected vertices are copied once.
    std::vector<std::uint32_t> duplicate_faces(std::span<const std::uint32_t> faces,
                                               std::array<double, 3> translation);
    // Mesh-local origin grid; nearest rounding with half ties away from zero.
    void snap_vertices(std::span<const std::uint32_t> vertices, double spacing,
                       std::array<bool, 3> axes = {true, true, true});
    // Geometric edge connectivity; touching at a vertex does not join surfaces.
    std::vector<std::uint32_t> connected_faces(std::uint32_t seed) const;
    // Reverse complete corner records around their existing first fan anchor.
    // A partial selection that breaks shared-edge winding is rejected atomically.
    void reverse_faces(std::span<const std::uint32_t> faces);
    void sculpt(std::span<const MeshBrushSample> samples, double radius, double strength);
    void sculpt(std::span<const MeshBrushSample> samples, double radius, double strength,
                MeshBrushSymmetry symmetry);
    void paint_color(std::span<const MeshBrushSample> samples, double radius,
                     std::array<double, 4> color, double opacity);
    void paint_color(std::span<const MeshBrushSample> samples, double radius,
                     std::array<double, 4> color, double opacity, MeshBrushSymmetry symmetry);
    void transform_uv(std::uint32_t face, std::span<const std::size_t> corners,
                      std::array<double, 2> translation, double angle_radians, double scale);
    // Shared geometric edges connect only when both endpoint UVs match exactly.
    std::vector<MeshUvCorner> uv_island(std::uint32_t face) const;
    std::vector<std::vector<MeshUvCorner>> uv_islands() const;
    void transform_uv(std::span<const MeshUvCorner> corners, std::array<double, 2> translation,
                      double angle_radians, std::array<double, 2> scale);
    void fit_uv(std::span<const MeshUvCorner> corners, double padding = .02);
    // Edit selected corners only; axis 0 is U, 1 is V. Center uses the bounds.
    void align_uv(std::span<const MeshUvCorner> corners, unsigned axis, MeshUvAlignment alignment);
    // Space distinct coordinate levels evenly; coincident levels stay coincident.
    void distribute_uv(std::span<const MeshUvCorner> corners, unsigned axis);
    // Expand touched islands; pack their rectangles at one common scale into [0,1].
    // Unselected islands retain their UVs and are not packing obstacles.
    void pack_uv_islands(std::span<const std::uint32_t> seed_faces, double padding = .01,
                         bool rotate_tall_islands = false);
    void project_face_uv(std::uint32_t face);
};
} // namespace forge
