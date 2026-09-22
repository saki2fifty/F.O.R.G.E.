#include "mesh_draw_bundle.hpp"
#include <forge/engine_assets.hpp>
namespace forge {
MeshDrawBundle::MeshDrawBundle(DiligentPresentation& presentation,
                               Diligent::IDeviceContext* context,
                               const asset_detail::PreparedModelDraw& prepared,
                               GpuResidency<MeshAsset>& meshes,
                               GpuResidency<TextureAsset>& textures, Diligent::TEXTURE_FORMAT color,
                               Diligent::TEXTURE_FORMAT depth, bool skinned,
                               std::shared_ptr<const MeshPoseGeometry> geometry)
    : prepared_(prepared),
      geometry_(geometry ? std::move(geometry)
                         : std::make_shared<const MeshPoseGeometry>(
                               prepare_mesh_pose_geometry(prepared.mesh->mesh))),
      skinned_(skinned) {
    const auto& source = prepared.mesh.get();
    validate_mesh_material_bindings(source);
    if (prepared.selection.parts.size() != source.mesh.lods.size())
        throw std::runtime_error("Material selection LOD count differs from mesh");
    for (const auto& [requested, fallback] : prepared.material_fallbacks) {
        (void)requested;
        if (fallback != engine_material(EngineMaterial::Error).id)
            throw std::runtime_error("Invalid material fallback identity");
    }
    for (const auto& [requested, fallback] : prepared.texture_fallbacks)
        if (!engine_texture_asset(fallback.first) || requested.second != fallback.second)
            throw std::runtime_error("Invalid texture fallback identity or semantic");
    // CPU preparation already validated immutable values. Recheck lease scopes and
    // matching binding identities before allocating physical resources.
    for (const auto& [id, material] : prepared.materials) {
        if (material.identity().asset != id)
            throw std::runtime_error("Draw material lease identity mismatch");
        validate_render_material(material.get());
        for (const auto& [role, ref] : material->textures) {
            const auto semantic = material->values.textures.at(role).semantic;
            const auto resolved = prepared.texture_asset({ref.id, semantic});
            const auto& texture = prepared.textures.at(resolved);
            if (texture.identity().asset != resolved.first || texture->semantic != semantic ||
                texture->dimension != material->values.textures.at(role).dimension)
                throw std::runtime_error("Draw texture lease identity/semantic mismatch");
        }
    }
    mesh_ = meshes.acquire(prepared.mesh);
    for (const auto& [key, texture] : prepared.textures)
        textures_.emplace(key, textures.acquire(texture));
    unresolved_ = prepared.selection.unresolved;
    MaterialData default_material;
    default_material.model = "forge.gltf.metallic-roughness.v1";
    for (std::size_t l = 0; l < mesh_.get().lods.size(); ++l) {
        const auto& lod = mesh_.get().lods[l];
        const auto& selected = prepared.selection.parts[l];
        if (selected.size() != lod.parts.size())
            throw std::runtime_error("Material selection part count differs from mesh");
        auto& output = lods_.emplace_back();
        auto& shadows = shadow_lods_.emplace_back();
        auto& info = info_.emplace_back();
        for (std::size_t p = 0; p < lod.parts.size(); ++p) {
            const auto& part = lod.parts[p];
            const AssetRef<MaterialAsset> material_ref{prepared.material_asset(selected[p].id)};
            const MaterialData* values = &default_material;
            const MaterialShaderSnapshot* surface = nullptr;
            MeshDraw::Textures native_textures;
            if (material_ref.id) {
                const auto& material = prepared.materials.at(material_ref.id).get();
                values = &material.values;
                surface = material.surface ? &*material.surface : nullptr;
                for (const auto& [role, ref] : material.textures) {
                    const auto semantic = values->textures.at(role).semantic;
                    native_textures[role] =
                        textures_.at(prepared.texture_asset({ref.id, semantic}))
                            .get()
                            ->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                }
            }
            const auto prepare = [&](Diligent::TEXTURE_FORMAT target) {
                return std::make_unique<MeshDraw>(
                    presentation, context, part, *values, native_textures, target, depth, skinned,
                    Diligent::SHADER_COMPILER_DEFAULT, Diligent::SHADER_OPTIMIZATION_LEVEL_DEFAULT,
                    surface);
            };
            output.push_back(prepare(color));
            // Blended surfaces do not have a single opaque shadow depth. Masked
            // surfaces evaluate the same base alpha and cutoff as their color pass.
            const bool transmits = !surface && material_transmits(prepare_pbr_material(*values));
            shadows.push_back(values->alpha == MaterialAlpha::Blend || transmits
                                  ? nullptr
                                  : prepare(Diligent::TEX_FORMAT_UNKNOWN));
            info.push_back({values->alpha, material_ref.id, part.bounds, transmits});
        }
    }
}
void MeshDrawBundle::draw_shadow(Diligent::IDeviceContext* context, const AffineTransform& world,
                                 const CameraView& camera, unsigned lod,
                                 const MeshInstancePose* pose) {
    (void)mesh_.get();
    for (const auto& [key, texture] : textures_) {
        (void)key;
        (void)texture.get();
    }
    const auto& parts = shadow_lods_.at(lod);
    for (unsigned p = 0; p < parts.size(); ++p)
        if (parts[p]) {
            const auto* skin =
                pose && pose->lods.at(lod).at(p).skin ? &*pose->lods.at(lod).at(p).skin : nullptr;
            parts[p]->draw(context, pose ? pose->world : world, camera, {}, nullptr, nullptr,
                           nullptr, {}, nullptr,
                           pose ? std::span<const float>(pose->morph_weights)
                                : std::span<const float>{},
                           skin);
        }
}
void MeshDrawBundle::environment(const EnvironmentLease& lease) {
    const auto* maps = lease ? &lease.get() : nullptr;
    // Replace every parity/LOD binding, including currently invisible parts,
    // before releasing its old lease. Budget accounting must cover native SRBs.
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->bind_environment(maps);
    environment_ = lease;
}
void MeshDrawBundle::shadows(const ShadowLighting* lighting) {
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->bind_shadows(lighting);
}
void MeshDrawBundle::transmission(const TransmissionLighting* lighting) {
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->bind_transmission(lighting);
}
void MeshDrawBundle::draw(Diligent::IDeviceContext* context, const AffineTransform& world,
                          const CameraView& camera, std::span<const LightView> lights,
                          const EnvironmentLighting* environment, unsigned lod) {
    if (lod >= lods_.size())
        throw std::runtime_error("Mesh draw LOD is outside the prepared candidate");
    for (unsigned part = 0; part < lods_[lod].size(); ++part)
        draw_part(context, world, camera, lights, lod, part, environment);
}
void MeshDrawBundle::draw_part(Diligent::IDeviceContext* context, const AffineTransform& world,
                               const CameraView& camera, std::span<const LightView> lights,
                               unsigned lod, unsigned part, const EnvironmentLighting* environment,
                               const std::array<float, 3>* legacy_tint,
                               const ShadowLighting* shadows, std::span<const int> shadow_slots,
                               const TransmissionLighting* transmission,
                               const MeshInstancePose* pose,
                               std::span<const MeshDraw::Instance> instances) {
    auto& selected = lods_.at(lod).at(part);
    // Validate owner lifetime and mark this submission before any native draw.
    (void)mesh_.get();
    for (const auto& [key, texture] : textures_) {
        (void)key;
        (void)texture.get();
    }
    const auto* skin =
        pose && pose->lods.at(lod).at(part).skin ? &*pose->lods.at(lod).at(part).skin : nullptr;
    selected->draw(context, pose ? pose->world : world, camera, lights, environment, legacy_tint,
                   shadows, shadow_slots, transmission,
                   pose ? std::span<const float>(pose->morph_weights) : std::span<const float>{},
                   skin, instances);
}
} // namespace forge
