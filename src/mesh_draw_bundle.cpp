#include "mesh_draw_bundle.hpp"
#include "sprite_draw_selection.hpp"
#include <algorithm>
#include <forge/engine_assets.hpp>
namespace forge {
MeshDrawBundle::MeshDrawBundle(
    DiligentPresentation& presentation, Diligent::IDeviceContext* context,
    const asset_detail::PreparedModelDraw& prepared, GpuResidency<MeshAsset>& meshes,
    GpuResidency<TextureAsset>& textures, Diligent::TEXTURE_FORMAT color,
    Diligent::TEXTURE_FORMAT depth, bool skinned, std::shared_ptr<const MeshPoseGeometry> geometry,
    MeshDrawProfile profile, const RenderTextureImages& render_textures, bool wind)
    : presentation_(presentation), color_(color), depth_(depth), prepared_(prepared),
      geometry_(geometry ? std::move(geometry)
                         : std::make_shared<const MeshPoseGeometry>(
                               prepare_mesh_pose_geometry(prepared.mesh->mesh))),
      skinned_(skinned), wind_(wind), profile_(profile) {
    const auto& source = prepared.mesh.get();
    if (profile != MeshDrawProfile::Mesh && !sprite_draw_profile(profile))
        throw std::invalid_argument("Unknown mesh draw bundle profile");
    if (sprite_draw_profile(profile))
        (void)asset_detail::sprite_source_texture(prepared, skinned);
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
        for (const auto& [role, ref] : material->render_textures) {
            (void)role;
            const auto image = render_textures.find(ref.id);
            if (image == render_textures.end() || !image->second)
                throw RenderTexturePending("Camera output image is not admitted yet");
            if (image->second.source().asset != ref.id)
                throw std::runtime_error("Render texture lease identity mismatch");
            render_textures_.try_emplace(ref.id, image->second);
        }
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
    for (const auto& [id, material] : prepared.materials) {
        (void)id;
        const auto image = sprite_draw_mip_filter(profile) ? sprite_material_image(material.get())
                                                           : std::string_view{};
        for (const auto& [role, ref] : material->textures) {
            if (role == image)
                continue;
            const auto semantic = material->values.textures.at(role).semantic;
            const auto key = prepared.texture_asset({ref.id, semantic});
            if (!textures_.contains(key))
                textures_.emplace(key, textures.acquire(prepared.textures.at(key)));
        }
    }
    unresolved_ = prepared.selection.unresolved;
    std::map<std::uint32_t, std::size_t> bindings;
    for (std::size_t i = 0; i < prepared.selection.bindings.size(); ++i)
        bindings.emplace(prepared.selection.bindings[i].physical_slot, i);
    for (std::size_t l = 0; l < mesh_.get().lods.size(); ++l) {
        const auto& lod = mesh_.get().lods[l];
        const auto& selected = prepared.selection.parts[l];
        if (selected.size() != lod.parts.size())
            throw std::runtime_error("Material selection part count differs from mesh");
        auto& output = lods_.emplace_back();
        auto& shadows = shadow_lods_.emplace_back();
        for (unsigned variant = 0; variant < 7; ++variant)
            surface_lods_[variant].emplace_back(lod.parts.size());
        auto& info = info_.emplace_back();
        for (std::size_t p = 0; p < lod.parts.size(); ++p) {
            const auto& part = lod.parts[p];
            const AssetRef<MaterialAsset> material_ref{prepared.material_asset(selected[p].id)};
            const auto* values =
                material_ref.id ? &prepared.materials.at(material_ref.id)->values : nullptr;
            const auto* surface = material_ref.id && prepared.materials.at(material_ref.id)->surface
                                      ? &*prepared.materials.at(material_ref.id)->surface
                                      : nullptr;
            MaterialData default_material;
            default_material.model = "forge.gltf.metallic-roughness.v1";
            if (!values)
                values = &default_material;
            output.push_back(prepare_draw(context, unsigned(l), unsigned(p), color, profile));
            // Blended surfaces do not have a single opaque shadow depth. Masked
            // surfaces evaluate the same base alpha and cutoff as their color pass.
            const bool transmits = !surface && material_transmits(prepare_pbr_material(*values));
            shadows.push_back(sprite_draw_profile(profile) ||
                                      values->alpha == MaterialAlpha::Blend || transmits
                                  ? nullptr
                                  : prepare_draw(context, unsigned(l), unsigned(p),
                                                 Diligent::TEX_FORMAT_UNKNOWN, profile));
            info.push_back({values->alpha, material_ref.id, part.bounds, transmits,
                            bindings.at(part.material_slot),
                            values->depth_test && values->depth_write});
        }
    }
}
std::unique_ptr<MeshDraw> MeshDrawBundle::prepare_draw(Diligent::IDeviceContext* context,
                                                       unsigned lod, unsigned part,
                                                       Diligent::TEXTURE_FORMAT target,
                                                       MeshDrawProfile profile, bool area_lights) {
    MaterialData defaults;
    defaults.model = "forge.gltf.metallic-roughness.v1";
    const auto& selected = prepared_.selection.parts.at(lod).at(part);
    const auto id = prepared_.material_asset(selected.id);
    const auto* values = &defaults;
    const MaterialShaderSnapshot* surface = nullptr;
    MeshDraw::Textures native_textures;
    if (id) {
        const auto& material = prepared_.materials.at(id).get();
        values = &material.values;
        surface = material.surface ? &*material.surface : nullptr;
        const auto image =
            sprite_draw_mip_filter(profile) ? sprite_material_image(material) : std::string_view{};
        for (const auto& [role, ref] : material.textures) {
            if (role == image)
                continue;
            const auto semantic = values->textures.at(role).semantic;
            native_textures[role] = textures_.at(prepared_.texture_asset({ref.id, semantic}))
                                        .get()
                                        ->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        }
        for (const auto& [role, ref] : material.render_textures)
            native_textures[role] = render_textures_.at(ref.id).get()->GetDefaultView(
                Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
    return std::make_unique<MeshDraw>(
        presentation_, context, mesh_.get().lods.at(lod).parts.at(part), *values, native_textures,
        target, depth_, skinned_, Diligent::SHADER_COMPILER_DEFAULT,
        Diligent::SHADER_OPTIMIZATION_LEVEL_DEFAULT, surface, profile, area_lights, wind_);
}
bool MeshDrawBundle::render_textures_ready(const RenderTextureInputs& inputs) const {
    for (const auto& [asset, image] : render_textures_) {
        const auto current = inputs.images.find(asset);
        if (!inputs.produced.contains(asset) || current == inputs.images.end() ||
            !current->second || current->second.get() != image.get())
            return false;
    }
    return true;
}
std::size_t MeshDrawBundle::draw_part_count() const {
    std::size_t count = 0;
    for (const auto& lod : lods_)
        count += lod.size();
    for (const auto& variants : surface_lods_)
        for (const auto& lod : variants)
            count +=
                std::count_if(lod.begin(), lod.end(), [](const auto& part) { return bool(part); });
    return count;
}
void MeshDrawBundle::prepare_temporal(Diligent::IDeviceContext* context, unsigned lod,
                                      unsigned part,
                                      std::span<const MaterialParameterOverride> parameters) {
    prepare_surface(context, lod, part, true, false, parameters);
}
void MeshDrawBundle::prepare_surface(Diligent::IDeviceContext* context, unsigned lod, unsigned part,
                                     bool motion, bool indirect,
                                     std::span<const MaterialParameterOverride> parameters,
                                     bool reflection, bool normal, bool area_lights,
                                     bool weighted) {
    const auto& info = info_.at(lod).at(part);
    if (profile_ != MeshDrawProfile::Mesh ||
        ((motion || indirect) &&
         (info.transmission || info.alpha == MaterialAlpha::Blend || !info.depth_write)))
        throw std::invalid_argument("Surface effects require a depth-writing Mesh part");
    auto& variants = surface_lods_.at(
        surface_index(motion, indirect, reflection, normal, area_lights, weighted));
    if (variants.empty()) {
        std::vector<std::vector<std::unique_ptr<MeshDraw>>> layout;
        for (const auto& parts : info_)
            layout.emplace_back(parts.size());
        variants = std::move(layout);
    }
    auto& selected = variants.at(lod).at(part);
    if (!selected) {
        const auto profile =
            weighted     ? MeshDrawProfile::MeshWeighted
            : reflection ? (motion ? MeshDrawProfile::MeshTemporalReflection
                                   : MeshDrawProfile::MeshReflection)
            : normal ? (motion ? MeshDrawProfile::MeshTemporalNormal : MeshDrawProfile::MeshNormal)
            : indirect
                ? (motion ? MeshDrawProfile::MeshTemporalIndirect : MeshDrawProfile::MeshIndirect)
            : motion ? MeshDrawProfile::MeshTemporal
                     : MeshDrawProfile::Mesh;
        auto candidate =
            prepare_draw(context, lod, part, weighted ? Diligent::TEX_FORMAT_RGBA32_FLOAT : color_,
                         profile, area_lights);
        candidate->bind_environment(environment_ ? &environment_.get() : nullptr);
        candidate->prepare_object_material_values(parameters, slot(lod, part));
        selected = std::move(candidate);
    } else
        selected->prepare_object_material_values(parameters, slot(lod, part));
}
void MeshDrawBundle::prepare_material_parameters(
    std::span<const MaterialParameterOverride> parameters) {
    if (parameters.empty())
        return;
    for (unsigned lod = 0; lod < lods_.size(); ++lod)
        for (unsigned part = 0; part < lods_[lod].size(); ++part) {
            lods_[lod][part]->prepare_object_material_values(parameters, slot(lod, part));
            if (auto& shadow = shadow_lods_[lod][part])
                shadow->prepare_object_material_values(parameters, slot(lod, part));
            for (auto& variants : surface_lods_)
                if (!variants.empty())
                    if (auto& surface = variants[lod][part])
                        surface->prepare_object_material_values(parameters, slot(lod, part));
        }
}
void MeshDrawBundle::draw_shadow(Diligent::IDeviceContext* context, const AffineTransform& world,
                                 const CameraView& camera, unsigned lod,
                                 const MeshInstancePose* pose,
                                 std::span<const MaterialParameterOverride> parameters) {
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
                           skin, {}, parameters, slot(lod, p), nullptr, nullptr, nullptr, {},
                           nullptr, {}, {}, pose ? &pose->wind : nullptr);
        }
}
void MeshDrawBundle::environment(const EnvironmentLease& lease) {
    const auto* maps = lease ? &lease.get() : nullptr;
    // Replace every parity/LOD binding, including currently invisible parts,
    // before releasing its old lease. Budget accounting must cover native SRBs.
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->bind_environment(maps);
    for (auto& variants : surface_lods_)
        for (auto& lod : variants)
            for (auto& part : lod)
                if (part)
                    part->bind_environment(maps);
    environment_ = lease;
}
void MeshDrawBundle::shadows(const ShadowLighting* lighting) {
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->bind_shadows(lighting);
    for (auto& variants : surface_lods_)
        for (auto& lod : variants)
            for (auto& part : lod)
                if (part)
                    part->bind_shadows(lighting);
}
void MeshDrawBundle::transmission(const TransmissionLighting* lighting) {
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->bind_transmission(lighting);
    for (auto& variants : surface_lods_)
        for (auto& lod : variants)
            for (auto& part : lod)
                if (part)
                    part->bind_transmission(lighting);
}
void MeshDrawBundle::clear_decals(Diligent::IDeviceContext* context) {
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->clear_decals(context);
    for (auto& variants : surface_lods_)
        for (auto& lod : variants)
            for (auto& part : lod)
                if (part)
                    part->clear_decals(context);
}
void MeshDrawBundle::clear_local_irradiance() {
    for (auto& lod : lods_)
        for (auto& part : lod)
            part->clear_local_irradiance();
    for (auto& variants : surface_lods_)
        for (auto& lod : variants)
            for (auto& part : lod)
                if (part)
                    part->clear_local_irradiance();
}
void MeshDrawBundle::draw(Diligent::IDeviceContext* context, const AffineTransform& world,
                          const CameraView& camera, std::span<const LightView> lights,
                          const EnvironmentLighting* environment, unsigned lod) {
    if (lod >= lods_.size())
        throw std::runtime_error("Mesh draw LOD is outside the prepared candidate");
    for (unsigned part = 0; part < lods_[lod].size(); ++part)
        draw_part(context, world, camera, lights, lod, part, environment);
}
void MeshDrawBundle::draw_part(
    Diligent::IDeviceContext* context, const AffineTransform& world, const CameraView& camera,
    std::span<const LightView> lights, unsigned lod, unsigned part,
    const EnvironmentLighting* environment, const std::array<float, 3>* legacy_tint,
    const ShadowLighting* shadows, std::span<const int> shadow_slots,
    const TransmissionLighting* transmission, const MeshInstancePose* pose,
    std::span<const MeshDraw::Instance> instances,
    std::span<const MaterialParameterOverride> parameters, const SpriteDrawValues* sprite,
    const SceneDistanceFog* fog, const GpuLease<TextureAsset>* sprite_mip,
    const MeshDraw::Motion* motion, bool indirect, bool reflection, bool normal,
    std::span<const MeshDraw::Decal> decals, bool weighted, const RenderBounds* decal_bounds,
    std::span<const MeshDraw::LightImage> images,
    std::span<const EnvironmentProbe> local_irradiance) {
    const bool area_lights = std::any_of(lights.begin(), lights.end(), [](const auto& light) {
        return light.kind >= unsigned(LightKind::Rectangle);
    });
    if ((motion || indirect || reflection || normal || area_lights || weighted) &&
        !surface_prepared(lod, part, bool(motion), indirect, reflection, normal, area_lights,
                          weighted))
        throw std::runtime_error("Surface Mesh part was not prepared before drawing");
    auto& selected = motion || indirect || reflection || normal || area_lights || weighted
                         ? surface_lods_
                               .at(surface_index(bool(motion), indirect, reflection, normal,
                                                 area_lights, weighted))
                               .at(lod)
                               .at(part)
                         : lods_.at(lod).at(part);
    if (!selected)
        throw std::runtime_error("Surface Mesh part was not prepared before drawing");
    if (sprite_draw_mip_filter(profile_)) {
        if (!sprite_mip || !*sprite_mip)
            throw std::runtime_error("Sprite mip draw has no admitted filtered frame");
        selected->bind_sprite_mip(
            sprite_mip->get()->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        sprite_mip_ = *sprite_mip;
    } else if (sprite_mip)
        throw std::invalid_argument("Base-level draw cannot consume a filtered Sprite frame");
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
                   skin, instances, parameters, slot(lod, part), sprite, fog, motion, decals,
                   decal_bounds, images, local_irradiance, pose ? &pose->wind : nullptr);
}
} // namespace forge
