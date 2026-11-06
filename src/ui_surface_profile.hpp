#pragma once
#include "material_shader.hpp"
#include "mesh_uv_surface.hpp"
#include "model_draw_candidate.hpp"
#include "model_instance_pose.hpp"
#include "ui_surface_limits.hpp"
#include <forge/engine_assets.hpp>
#include <stdexcept>

namespace forge {
// Derived from the exact retained Material selection and the shader's UV rows.
// Axis flips keep a complete one-to-one image mapping for pointer and IME routing.
struct UiSurfaceImageMapping {
    std::array<std::array<float, 4>, 2> rows;
    Double2 image_uv(Double2 uv) const {
        return {rows[0][0] * uv[0] + rows[0][1] * uv[1] + rows[0][2],
                rows[1][0] * uv[0] + rows[1][1] * uv[1] + rows[1][2]};
    }
    Double2 mesh_uv(Double2 image) const {
        return {(image[0] - rows[0][2]) / rows[0][0], (image[1] - rows[1][2]) / rows[1][1]};
    }
    Double2 image(Double2 local) const { return image_uv({local[0] + .5, local[1] + .5}); }
    Double2 local(Double2 image) const {
        const auto uv = mesh_uv(image);
        return {uv[0] - .5, uv[1] - .5};
    }
};
inline UiSurfaceImageMapping ui_surface_material_mapping(const MaterialResourceData& data,
                                                         AssetRef<RenderTextureAsset> output) {
    const auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    require(bool(output.id), "UI Surface Input needs a Render Texture assignment");
    const auto& values = data.values;
    require(!data.surface && values.model == "forge.gltf.unlit.v1" &&
                values.alpha == MaterialAlpha::Opaque && values.double_sided && values.depth_test &&
                values.depth_write,
            "UI Surface Input requires an opaque, two-sided, depth-tested unlit Material");
    require(values.textures.size() == 1 && values.textures.contains("baseColorTexture") &&
                data.textures.empty() && data.render_textures.size() == 1 &&
                data.render_textures.contains("baseColorTexture") &&
                data.render_textures.at("baseColorTexture") == output,
            "UI Surface Input Render Texture must match the Material's base color image");
    const auto& slot = values.textures.at("baseColorTexture");
    require(slot.uv_set == 0 && slot.dimension == TextureDimension::D2 &&
                slot.semantic == TextureSemantic::Color && slot.rotation == 0 &&
                slot.sampler.compare == TextureCompare::None,
            "UI Surface Input requires Color 2D, UV0, no UV rotation or comparison sampling");
    for (unsigned axis = 0; axis < 2; ++axis)
        require((slot.scale[axis] == 1 && slot.offset[axis] == 0) ||
                    (slot.scale[axis] == -1 && slot.offset[axis] == 1),
                "UI Surface Input UVs support identity and complete image-axis flips");
    return {material_texture_transform_rows(slot)};
}
inline UiSurfaceImageMapping ui_surface_image_mapping(const asset_detail::PreparedModelDraw& draw,
                                                      const MeshInstancePose& pose,
                                                      AssetRef<RenderTextureAsset> output) {
    if (draw.mesh.identity().asset != engine_primitive(8).id)
        throw std::runtime_error("UI Surface Input requires the Engine / Quad mesh");
    if (pose.skinned || !pose.morph_weights.empty() || pose.wind.enabled)
        throw std::runtime_error("UI Surface Input requires an undeformed Quad");
    if (draw.has_fallbacks() || draw.selection.parts.size() != 1 ||
        draw.selection.parts.front().size() != 1)
        throw std::runtime_error(
            "UI Surface Input requires a complete single-part Material selection");
    const auto material = draw.materials.find(draw.selection.parts.front().front().id);
    if (material == draw.materials.end())
        throw std::runtime_error("UI Surface Input Material is unavailable");
    return ui_surface_material_mapping(material->second.get(), output);
}
inline UiSurfaceImageMapping ui_surface_mesh_material(const asset_detail::PreparedModelDraw& draw,
                                                      unsigned lod, unsigned part,
                                                      AssetRef<RenderTextureAsset> output) {
    const auto& selected = draw.selection.parts.at(lod).at(part);
    const auto material = draw.materials.find(selected.id);
    if (material == draw.materials.end())
        throw std::runtime_error("UI Surface Input mesh Material is unavailable");
    return ui_surface_material_mapping(material->second.get(), output);
}
inline void validate_ui_surface_mesh(const asset_detail::PreparedModelDraw& draw,
                                     AssetRef<RenderTextureAsset> output, MeshPickBudget& budget) {
    const auto& mesh = draw.mesh->mesh;
    if (draw.has_fallbacks() || mesh.lods.empty() ||
        draw.selection.parts.size() != mesh.lods.size())
        throw std::runtime_error("UI Surface Input requires a complete mesh Material selection");
    for (unsigned lod = 0; lod < mesh.lods.size(); ++lod) {
        const auto& parts = mesh.lods[lod].parts;
        if (parts.empty() || draw.selection.parts[lod].size() != parts.size())
            throw std::runtime_error("UI Surface Input mesh has an incomplete LOD selection");
        for (unsigned p = 0; p < parts.size(); ++p) {
            const auto& part = parts[p];
            const auto* uv = part.find("TEXCOORD_0");
            if (part.topology != MeshTopology::Triangles || !uv || uv->components != 2 ||
                !std::holds_alternative<std::vector<float>>(uv->values) ||
                uv->scalar_count() != std::uint64_t(part.vertices) * 2)
                throw std::runtime_error(
                    "UI Surface Input mesh requires triangles with float2 UV0");
            (void)ui_surface_mesh_material(draw, lod, p, output);
            const auto corners = part.indices.empty() ? part.vertices : part.indices.size();
            if (corners % 3 != 0)
                throw std::runtime_error("UI Surface Input mesh has incomplete triangles");
            for (std::size_t primitive = 0; primitive < corners / 3; ++primitive) {
                const MeshUvTriangle triangle(part, primitive, budget);
                if (!triangle.coordinates(triangle.uv[0]))
                    throw std::runtime_error("UI Surface Input mesh has a degenerate UV0 triangle");
            }
        }
    }
}

} // namespace forge
