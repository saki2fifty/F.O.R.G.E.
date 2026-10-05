#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <forge/mesh_asset.hpp>
#include <nlohmann/json.hpp>
#include <span>

namespace forge {
// Small first-class authored mesh. Polygon/corner identities belong to this
// source; cooked MeshData remains an immutable runtime projection.
inline constexpr std::size_t editable_mesh_source_byte_limit = 2 * 1024 * 1024;
struct MeshBrushSample {
    std::array<double, 3> point{};
    double pressure = 1;
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
    void sculpt(std::span<const MeshBrushSample> samples, double radius, double strength);
    void paint_color(std::span<const MeshBrushSample> samples, double radius,
                     std::array<double, 4> color, double opacity);
    void transform_uv(std::uint32_t face, std::span<const std::size_t> corners,
                      std::array<double, 2> translation, double angle_radians, double scale);
    void project_face_uv(std::uint32_t face);
};
} // namespace forge
