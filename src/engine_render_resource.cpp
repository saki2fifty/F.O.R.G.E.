#include "engine_render_resource.hpp"
#include "primitive_surface.hpp"
#include <forge/asset_build.hpp>
#include <forge/geometry.hpp>
namespace forge::asset_detail {
MeshResourceData engine_mesh_resource(AssetRef<MeshAsset> ref) {
    const auto* asset = engine_asset(ref.id);
    if (!asset || !asset->primitive || std::string_view(asset->type) != MeshAsset::type)
        throw std::runtime_error("Engine asset is not a primitive mesh");
    const auto kind = *asset->primitive;
    const auto& vertices = primitive_meshes().at(kind);
    MeshResourceData result;
    auto& part = result.mesh.lods.emplace_back().parts.emplace_back();
    part.vertices = static_cast<std::uint32_t>(vertices.size());
    std::vector<float> positions, normals, coordinates, tangents;
    positions.reserve(vertices.size() * 3);
    normals.reserve(vertices.size() * 3);
    coordinates.reserve(vertices.size() * 2);
    tangents.reserve(vertices.size() * 4);
    part.indices.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); i += 3) {
        // Same outward winding correction as the admitted blockout renderer.
        auto a = vertices[i], b = vertices[i + 1], c = vertices[i + 2];
        const auto area =
            geom_cross(geom_sub(b.position, a.position), geom_sub(c.position, a.position));
        if (geom_dot(area, a.normal) < 0)
            std::swap(b, c);
        const std::array triangle{a, b, c};
        const auto surface = primitive_surface(kind, triangle);
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto& vertex = triangle[corner];
            positions.insert(positions.end(), vertex.position.begin(), vertex.position.end());
            normals.insert(normals.end(), vertex.normal.begin(), vertex.normal.end());
            coordinates.insert(coordinates.end(), surface[corner].uv.begin(),
                               surface[corner].uv.end());
            tangents.insert(tangents.end(), surface[corner].tangent.begin(),
                            surface[corner].tangent.end());
            part.indices.push_back(static_cast<std::uint32_t>(part.indices.size()));
        }
    }
    part.streams = {{"POSITION", 3, std::move(positions)},
                    {"NORMAL", 3, std::move(normals)},
                    {"TEXCOORD_0", 2, std::move(coordinates)},
                    {"TANGENT", 4, std::move(tangents)}};
    part.bounds = mesh_bounds(part);
    const bool planar = kind == 3 || kind == 8 || kind == 9 || kind == 10;
    result.materials.push_back(
        {0, "surface",
         engine_material(planar ? EngineMaterial::TwoSided : EngineMaterial::Default)});
    validate_mesh(result.mesh);
    validate_mesh_material_bindings(result);
    return result;
}
MaterialResourceData engine_material_resource(AssetRef<MaterialAsset> ref) {
    const auto* asset = engine_asset(ref.id);
    if (!asset || std::string_view(asset->type) != MaterialAsset::type)
        throw std::runtime_error("Engine asset is not a material");
    MaterialResourceData result;
    auto& material = result.values;
    const bool legacy = ref == engine_material(EngineMaterial::LegacyBlockout);
    material.model = legacy ? "forge.gltf.unlit.v1" : "forge.gltf.metallic-roughness.v1";
    material.double_sided = legacy || ref == engine_material(EngineMaterial::TwoSided);
    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    validate_material(material);
    return result;
}
ResourceTicket request_engine_mesh(ResourcePool<MeshAsset>& pool, AssetRef<MeshAsset> ref) {
    // Immutable recipe revision, separate from logical identity and pool generation.
    return pool.request(
        ref, asset_build_digest({{"recipe", "forge-engine-primitive-v2"}, {"asset", ref.id}}), 2,
        [ref](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Engine mesh preparation cancelled");
            auto value = std::make_unique<MeshResourceData>(engine_mesh_resource(ref));
            const auto bytes = value->resident_bytes();
            return ResourceCandidate<MeshAsset>{std::move(value), {bytes}};
        });
}
ResourceTicket request_engine_material(ResourcePool<MaterialAsset>& pool,
                                       AssetRef<MaterialAsset> ref) {
    return pool.request(
        ref, asset_build_digest({{"recipe", "forge-engine-material-v1"}, {"asset", ref.id}}), 1,
        [ref](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Engine material preparation cancelled");
            auto value = std::make_unique<MaterialResourceData>(engine_material_resource(ref));
            const auto bytes = value->resident_bytes();
            return ResourceCandidate<MaterialAsset>{std::move(value), {bytes}};
        });
}
} // namespace forge::asset_detail
