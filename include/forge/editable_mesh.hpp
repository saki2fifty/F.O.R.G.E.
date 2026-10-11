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
struct EditableMeshSource {
    nlohmann::json document;
    AssetId asset() const;
    static EditableMeshSource create_cube(AssetId asset);
    static EditableMeshSource create_sculpt_sphere(AssetId asset);
    static EditableMeshSource parse(std::span<const std::byte> bytes);
    void validate() const;
    MeshData cook() const;
    void translate_vertices(std::span<const std::uint32_t> ids, std::array<double, 3> translation);
    void transform_vertices(std::span<const std::uint32_t> ids, std::array<double, 3> translation,
                            std::array<double, 3> rotation_degrees, std::array<double, 3> scale);
    std::uint32_t extrude_face(std::uint32_t face, double distance);
    // Uniform inward edge offset; preserves the selected inner face identity.
    std::uint32_t inset_face(std::uint32_t face, double distance);
    // Author the existing ordered face fan; first triangle keeps its face ID.
    void triangulate_faces(std::span<const std::uint32_t> faces);
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
    void transform_uv(std::span<const MeshUvCorner> corners, std::array<double, 2> translation,
                      double angle_radians, std::array<double, 2> scale);
    void fit_uv(std::span<const MeshUvCorner> corners, double padding = .02);
    void project_face_uv(std::uint32_t face);
};
} // namespace forge
