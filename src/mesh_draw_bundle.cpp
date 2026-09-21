#include "mesh_draw_bundle.hpp"
namespace forge {
MeshDrawBundle::MeshDrawBundle(DiligentPresentation& presentation,
                               Diligent::IDeviceContext* context,
                               const asset_detail::PreparedModelDraw& prepared,
                               GpuResidency<MeshAsset>& meshes,
                               GpuResidency<TextureAsset>& textures, Diligent::TEXTURE_FORMAT color,
                               Diligent::TEXTURE_FORMAT depth) {
    const auto& source = prepared.mesh.get();
    validate_mesh_material_bindings(source);
    std::map<std::uint32_t, const MeshMaterialBinding*> bindings;
    for (const auto& binding : prepared.selection.bindings)
        if (!bindings.emplace(binding.physical_slot, &binding).second)
            throw std::runtime_error("Duplicate physical material selection in draw candidate");
    // CPU preparation already validated immutable values. Recheck lease scopes and
    // matching binding identities before allocating physical resources.
    for (const auto& [id, material] : prepared.materials) {
        if (material.identity().asset != id)
            throw std::runtime_error("Draw material lease identity mismatch");
        validate_material_bindings(material->values, material->textures);
        for (const auto& [role, ref] : material->textures) {
            const auto semantic = material->values.textures.at(role).semantic;
            const auto& texture = prepared.textures.at({ref.id, semantic});
            if (texture.identity().asset != ref.id || texture->semantic != semantic)
                throw std::runtime_error("Draw texture lease identity/semantic mismatch");
        }
    }
    mesh_ = meshes.acquire(prepared.mesh);
    for (const auto& [key, texture] : prepared.textures)
        textures_.emplace(key, textures.acquire(texture));
    unresolved_ = prepared.selection.unresolved;
    MaterialData default_material;
    default_material.model = "forge.gltf.metallic-roughness.v1";
    for (const auto& lod : mesh_.get().lods) {
        auto& output = lods_.emplace_back();
        for (const auto& part : lod.parts) {
            const auto& binding = *bindings.at(part.material_slot);
            const MaterialData* values = &default_material;
            MeshDraw::Textures native_textures;
            if (binding.material.id) {
                const auto& material = prepared.materials.at(binding.material.id).get();
                values = &material.values;
                for (const auto& [role, ref] : material.textures) {
                    const auto semantic = values->textures.at(role).semantic;
                    native_textures[role] =
                        textures_.at({ref.id, semantic})
                            .get()
                            ->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                }
            }
            output.push_back(std::make_unique<MeshDraw>(presentation, context, part, *values,
                                                        native_textures, color, depth));
        }
    }
}
void MeshDrawBundle::draw(Diligent::IDeviceContext* context, const AffineTransform& world,
                          const CameraView& camera, std::span<const LightView> lights,
                          unsigned lod) {
    if (lod >= lods_.size())
        throw std::runtime_error("Mesh draw LOD is outside the prepared candidate");
    // Validate owner lifetime and mark this submission before any native draw.
    (void)mesh_.get();
    for (const auto& [key, texture] : textures_) {
        (void)key;
        (void)texture.get();
    }
    for (const auto& part : lods_[lod])
        part->draw(context, world, camera, lights);
}
} // namespace forge
