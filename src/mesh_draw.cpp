#include "mesh_draw.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "decal_clusters.hpp"
#include "decal_shader.hpp"
#include "material_parameter_values.hpp"
#include "mesh_draw_shader.hpp"
#include "render_backend.hpp"
#include "shader_runtime_backend.hpp"
#include "sprite_material_profile.hpp"
#include "temporal_motion_shader.hpp"
#include "texture_gpu.hpp"
#include "weighted_transparency_shader.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace forge {
namespace {
using namespace Diligent;
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Mesh draw: " + message);
}
float gpu(double x) {
    require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(),
            "value is outside GPU finite representation");
    return float(x);
}
RefCntAutoPtr<IBuffer> buffer(IRenderDevice* device, const char* name, Uint64 bytes,
                              const void* initial = nullptr, unsigned stride = 0) {
    BufferDesc desc;
    desc.Name = name;
    desc.Size = bytes;
    desc.BindFlags = stride ? BIND_SHADER_RESOURCE : BIND_UNIFORM_BUFFER;
    desc.Mode = stride ? BUFFER_MODE_STRUCTURED : BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = stride;
    desc.Usage = initial ? USAGE_IMMUTABLE : USAGE_DYNAMIC;
    desc.CPUAccessFlags = initial ? CPU_ACCESS_NONE : CPU_ACCESS_WRITE;
    BufferData data{initial, bytes};
    RefCntAutoPtr<IBuffer> result;
    device->CreateBuffer(desc, initial ? &data : nullptr, &result);
    require(bool(result), "draw buffer allocation failed");
    return result;
}
// Float4 rows match HLSL cbuffer packing; native PBR's integer lanes use bit_cast.
using Row = std::array<float, 4>;
constexpr std::array decal_storage_names{"g_ForgeDecalRows", "g_ForgeDecalRanges",
                                         "g_ForgeDecalIndices"};
constexpr unsigned mesh_instance_rows = 7, sprite_instance_rows = 10;
constexpr unsigned wind_instance_rows = 8, wind_temporal_instance_rows = temporal_instance_rows + 2;

using IrradianceValues = std::array<Row, environment_probe_rows>;
IrradianceValues prepare_irradiance(std::span<const EnvironmentProbe> probes, Double3 origin) {
    require(probes.size() <= environment_probe_limit, "local irradiance exceeds the probe profile");
    IrradianceValues values{};
    values[0][0] = float(probes.size());
    for (unsigned i = 0; i < probes.size(); ++i) {
        const auto& probe = probes[i];
        require(complete_environment_cube(probe.irradiance) &&
                    probe.irradiance->GetTexture()->GetDesc().MipLevels == 1,
                "local irradiance requires a complete single-mip HDR cube");
        require(std::isfinite(probe.diffuse_intensity) && probe.diffuse_intensity > 0 &&
                    probe.diffuse_intensity <= 1000000 && std::isfinite(probe.blend_distance) &&
                    probe.blend_distance >= 0 && probe.blend_distance <= 1000000,
                "local irradiance intensity/fade is outside its supported range");
        const auto center = reflection_probe_view_center(probe.volume, origin);
        const unsigned row = 1 + 5 * i;
        values[row] = {center[0], center[1], center[2], gpu(probe.diffuse_intensity)};
        for (unsigned axis = 0; axis < 3; ++axis) {
            require(probe.volume.planes[axis][3] >= std::numeric_limits<float>::min(),
                    "local irradiance volume has an invalid half distance");
            for (unsigned c = 0; c < 4; ++c)
                values[row + 1 + axis][c] = gpu(probe.volume.planes[axis][c]);
        }
        values[row + 4] = {gpu(probe.blend_distance), gpu(probe.volume.map_rotation[0]),
                           gpu(probe.volume.map_rotation[1]), 0};
    }
    return values;
}

std::array<Row, 769> prepare_skin_values(const SkinPose* skin, std::size_t joint_count,
                                         const CameraView& view, float thickness) {
    require(skin && skin->palette.size() == joint_count && joint_count <= 256,
            "skin draw requires the complete admitted palette");
    double largest = 0;
    for (const auto& matrix : skin->palette)
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                require(std::isfinite(matrix.m[4 * r + c]), "nonfinite skin matrix");
                if (c < 3)
                    largest = std::max(largest, std::abs(matrix.m[4 * r + c]));
            }
    std::array<Row, 769> values{};
    values[0] = {gpu(largest), view.orientation_reversed ? 1.f : 0.f, 0, 0};
    require(largest == 0 || values[0][0] != 0, "skin linear scale underflows the GPU profile");
    std::array<double, 3> extents{};
    for (unsigned c = 0; c < 3; ++c) {
        require(std::isfinite(skin->source_bounds.minimum[c]) &&
                    std::isfinite(skin->source_bounds.maximum[c]) &&
                    skin->source_bounds.minimum[c] <= skin->source_bounds.maximum[c],
                "skin draw has invalid morphed source bounds");
        extents[c] = std::max(std::abs(double(skin->source_bounds.minimum[c])),
                              std::abs(double(skin->source_bounds.maximum[c])));
    }
    for (std::size_t i = 0; i < skin->palette.size(); ++i) {
        const auto& matrix = skin->palette[i];
        double norm = 0;
        for (unsigned r = 0; r < 3; ++r) {
            double products = 0;
            auto& row = values[1 + 3 * i + r];
            for (unsigned c = 0; c < 3; ++c) {
                const double scaled = largest > 0 ? matrix.m[4 * r + c] / largest : 0;
                row[c] = gpu(scaled);
                products += std::abs(scaled) * extents[c];
                norm = std::hypot(norm, matrix.m[4 * r + c]);
            }
            row[3] = gpu(matrix.m[4 * r + 3] - view.position[r]);
            // Bound intermediate dot products as well as the final position,
            // including float accumulation slack. Reject derived GPU overflow
            // without changing authored transforms or the previously bound pose.
            const double limit = std::numeric_limits<float>::max() /
                                 (1 + 64 * double(std::numeric_limits<float>::epsilon()));
            require(products <= limit && products * largest + std::abs(double(row[3])) <= limit,
                    "skinned positions exceed the finite GPU profile");
        }
        require(double(thickness) * norm <= std::numeric_limits<float>::max(),
                "skinned volume thickness exceeds the finite GPU profile");
    }
    return values;
}

void upload_morph_weights(IDeviceContext* context, IBuffer* buffer,
                          std::span<const float> morph_weights, std::size_t target_count) {
    require(morph_weights.size() == target_count &&
                std::all_of(morph_weights.begin(), morph_weights.end(),
                            [](float value) { return std::isfinite(value); }),
            "morph weights must be finite and match every target");
    MapHelper<Row> data(context, buffer, MAP_WRITE, MAP_FLAG_DISCARD);
    require(static_cast<Row*>(data) != nullptr, "morph upload allocation failed");
    std::fill_n(static_cast<Row*>(data), (morph_weights.size() + 3) / 4, Row{});
    for (std::size_t i = 0; i < morph_weights.size(); ++i)
        data[i / 4][i % 4] = morph_weights[i];
}

} // namespace
unsigned MeshDraw::instance_rows() const {
    if (wind_)
        return temporal_ ? wind_temporal_instance_rows : wind_instance_rows;
    return temporal_ ? temporal_instance_rows : sprite_ ? sprite_instance_rows : mesh_instance_rows;
}
MeshDraw::MeshDraw(DiligentPresentation& presentation, IDeviceContext* context,
                   const GpuMeshPart& mesh, const MaterialData& source, const Textures& textures,
                   TEXTURE_FORMAT color_format, TEXTURE_FORMAT depth_format, bool enable_skin,
                   SHADER_COMPILER compiler, SHADER_OPTIMIZATION_LEVEL optimization,
                   const MaterialShaderSnapshot* surface, MeshDrawProfile draw_profile,
                   bool area_lights, bool wind)
    : mesh_(mesh), shadow_pass_(color_format == TEX_FORMAT_UNKNOWN),
      temporal_(temporal_mesh_profile(draw_profile) && !shadow_pass_),
      indirect_(indirect_mesh_profile(draw_profile) && !shadow_pass_),
      reflection_(reflection_mesh_profile(draw_profile) && !shadow_pass_),
      normal_(normal_mesh_profile(draw_profile) && !shadow_pass_),
      weighted_(draw_profile == MeshDrawProfile::MeshWeighted && !shadow_pass_),
      area_lights_(area_lights && !shadow_pass_), sprite_(sprite_draw_profile(draw_profile)),
      sprite_mips_(sprite_draw_mip_filter(draw_profile).has_value()), wind_(wind) {
    require(draw_profile == MeshDrawProfile::Mesh || temporal_mesh_profile(draw_profile) ||
                indirect_mesh_profile(draw_profile) ||
                draw_profile == MeshDrawProfile::MeshWeighted || sprite_,
            "unknown draw profile");
    require(!weighted_ ||
                (source.alpha == MaterialAlpha::Blend && source.depth_test && !source.depth_write &&
                 color_format == TEX_FORMAT_RGBA32_FLOAT && depth_format == TEX_FORMAT_D32_FLOAT),
            "Weighted transparency requires a depth-tested Blend Mesh without depth writes");
    require(!weighted_ || !surface || surface->program.surface_wrapper_version >= 9,
            "Rebuild this Shader and dependent Materials to use weighted transparency");
    require(!(temporal_ || indirect_) ||
                (source.depth_test && source.depth_write && source.alpha != MaterialAlpha::Blend),
            "Surface effects require depth-writing opaque or masked Mesh");
    require(!temporal_ || !surface || surface->program.surface_wrapper_version >= 3,
            "Rebuild this Shader to use temporal surface effects");
    require(!indirect_ || (color_format == TEX_FORMAT_RGBA16_FLOAT &&
                           (!surface || surface->program.surface_wrapper_version >= 4)),
            "Indirect Mesh output requires HDR and a current surface Shader cook");
    require(!reflection_ || !surface || surface->program.surface_wrapper_version >= 5,
            "Rebuild this Shader and dependent Materials to use screen-space reflections");
    require(!normal_ || !surface || surface->program.surface_wrapper_version >= 6,
            "Rebuild this Shader and dependent Materials to use material-normal occlusion");
    require(!area_lights_ || !sprite_, "Area lighting requires a Mesh draw");
    sprite_trim_allowed_ = sprite_trim_material_compatible(source);
    device_ = presentation.device();
    material_surface_ = surface != nullptr;
    std::optional<PbrMaterialProfile> profile;
    if (surface) {
        validate_shader(surface->program);
        require(surface->program.surface.has_value(), "Shader has no surface interface");
        validate_surface_material(source, *surface->program.surface);
        require(sprite_ == surface->program.surface->sprite_image.has_value(),
                "Surface Shader domain does not match this draw profile");
        if (surface->program.surface->physically_based) {
            MaterialData defaults;
            defaults.model = "forge.gltf.metallic-roughness.v1";
            profile = prepare_pbr_material(defaults);
        }
    } else
        profile = prepare_pbr_material(source);
    area_lights_ = area_lights_ && (surface ? surface->program.surface->physically_based
                                            : profile->workflow != PbrWorkflow::Unlit);
    require(!area_lights_ || !surface || surface->program.surface_wrapper_version >= 7,
            "Rebuild this Shader and dependent Materials to use area lighting");
    auto fetch =
        surface ? mesh_vertex_fetch(mesh, surface->program.surface->uv_sets, enable_skin, temporal_)
                : mesh_vertex_fetch(mesh, *profile, enable_skin, temporal_);
    fetch.wind = wind_;

    const auto sampler_binding = emulated_resource_arrays(presentation.device()->GetDeviceInfo())
                                     ? MaterialSamplerBinding::NamedElements
                                     : MaterialSamplerBinding::Array;
    MeshDrawShader program;
    if (surface) {
        const auto generated = mesh_geometry_shader(fetch, true, sprite_);
        program.vertex = generated.vertex;
        program.geometry = generated.geometry;
        program.instanced = generated.instanced;
        if (surface->program.surface->physically_based)
            program.material = material_shader(*profile, fetch.uv_sets, sampler_binding);
        if (!sprite_ && !shadow_pass_ && surface->program.surface_wrapper_version >= 8)
            program.decal_capacity =
                decal_shader_capacity(surface->program.surface->textures.size(),
                                      surface->program.surface->physically_based);
    } else
        program = mesh_draw_shader(fetch, *profile, shadow_pass_, sampler_binding, nullptr, {},
                                   sprite_, sprite_draw_mip_filter(draw_profile), indirect_,
                                   reflection_, normal_, area_lights_, true);
    decal_capacity_ = program.decal_capacity;
    decal_surface_channels_ = !surface || surface->program.surface_wrapper_version >= 10;
    decal_clustered_ = !surface || surface->program.surface_wrapper_version >= 11;
    require(!weighted_ || !program.transmission,
            "Weighted transparency cannot replace sorted transmission transport");
    if (weighted_ && !surface)
        program.pixel = weighted_color_wrapper(std::move(program.pixel), false);
    const auto& material = program.material;
    require(!(temporal_ || indirect_) || !program.transmission,
            "Transmission draws follow temporal opaque accumulation");
    auto compile = [&](SHADER_TYPE stage, const std::string& code) {
        ShaderCreateInfo ci;
        ci.Desc.Name = "FORGE prepared mesh draw";
        ci.Desc.ShaderType = stage;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        // Normal passes use the presentation owner's backend policy. An explicit
        // compiler remains available for identical-source native diagnostics.
        ci.ShaderCompiler = compiler;
        ci.ShaderOptimizationLevel = optimization;
        ci.EntryPoint = "main";
        ci.Source = code.c_str();
        ci.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> result;
        presentation.shader(ci, &result);
        return result;
    };
    RefCntAutoPtr<IShader> geometry;
    if (!program.geometry.empty())
        geometry = compile(SHADER_TYPE_GEOMETRY, program.geometry);
    auto vertex = compile(SHADER_TYPE_VERTEX, program.vertex);
    auto* device = presentation.device();
    RefCntAutoPtr<IShader> pixel;
    SurfaceBindingLayout surface_layout;
    if (surface) {
        const auto role = sprite_        ? (sprite_mips_ ? ShaderEntryRole::SurfaceSpriteMipColor
                                                         : ShaderEntryRole::SurfaceSpriteColor)
                          : shadow_pass_ ? ShaderEntryRole::SurfaceDepth
                                         : surface_color_role(temporal_, indirect_, reflection_,
                                                              normal_, area_lights_, weighted_);
        auto native = realize_renderer_shader(presentation, surface->program);
        pixel = native.stages.at(ShaderStageKey{ShaderStage::Pixel, role});
        const auto stage = std::find_if(native.data.stages.begin(), native.data.stages.end(),
                                        [role](const auto& entry) { return entry.role == role; });
        require(stage != native.data.stages.end(), "custom surface entry role is missing");
        surface_layout = surface_binding_layout(*surface->program.surface, stage->reflection, role,
                                                surface->program.surface_wrapper_version);
        fog_supported_ = surface->program.surface_wrapper_version >= 2;
        fog_volumes_supported_ = surface->program.surface_wrapper_version >= 16;
    } else
        pixel = compile(SHADER_TYPE_PIXEL, program.pixel);
    const bool lit = !shadow_pass_ && (surface ? surface->program.surface->physically_based
                                               : profile->workflow != PbrWorkflow::Unlit);
    local_irradiance_ = lit && (!surface || surface->program.surface_wrapper_version >= 13);
    if (local_irradiance_) {
        const IrradianceValues empty{};
        irradiance_buffer_ = buffer(device, "FORGE local irradiance", sizeof(empty));
        empty_irradiance_ =
            buffer(device, "FORGE empty local irradiance", sizeof(empty), empty.data());
    }
    light_images_ =
        lit && decal_capacity_ && (!surface || surface->program.surface_wrapper_version >= 12);
    if (light_images_) {
        const std::array<Row, mesh_draw_light_limit * 4> empty{};
        light_images_buffer_ = buffer(device, "FORGE light images", sizeof(empty));
        empty_light_images_ =
            buffer(device, "FORGE empty light images", sizeof(empty), empty.data());
    }
    if (decal_capacity_) {
        decals_ =
            buffer(device, "FORGE projected surface",
                   decal_shader_rows(decal_capacity_, decal_surface_channels_, decal_clustered_) *
                       sizeof(Row));
        white_decal_ = presentation.pbr(context).GetWhiteTexSRV();
        default_decal_sampler_ = upload_sampler(device, SamplerState{});
        require(white_decal_ && default_decal_sampler_, "Decal fallback resources unavailable");
        // Empty receiver state survives frames without a dynamic upload. DISCARD
        // allocations are frame-local and must never serve as retained defaults.
        const std::array<Row, decal_shader_rows(decal_draw_limit, true, true)> empty{};
        empty_decals_ =
            buffer(device, "FORGE empty projected surface",
                   decal_shader_rows(decal_capacity_, decal_surface_channels_, decal_clustered_) *
                       sizeof(Row),
                   empty.data());
        if (decal_clustered_)
            empty_decal_storage_ = presentation.empty_projected_buffers();
    }
    RefCntAutoPtr<ITextureView> ggx;
    if (lit) {
        ggx = presentation.pbr(context).GetPreintegratedGGX_SRV();
        black_environment_ = presentation.black_environment(context);
        environment_ = buffer(device, "FORGE environment lighting", sizeof(Row));
        shadows_ = buffer(device, "FORGE shadow receiver", sizeof(ShadowLighting::values));
        empty_shadow_ = presentation.empty_shadow(context);
    }
    if (program.transmission) {
        volume_thickness_ = profile->values.parameters.at("thicknessFactor").value[0];
        transmission_ = buffer(device, "FORGE transmission background", 2 * sizeof(Row));
        black_background_ = presentation.pbr(context).GetBlackTexSRV();
        require(bool(black_background_), "native black transmission fallback unavailable");
    }
    RefCntAutoPtr<ITextureView> sheen;
    if (program.sheen) {
        sheen = presentation.pbr(context).GetPreintegratedSheen_SRV();
        require(bool(sheen), "native sheen lookup resource unavailable");
    }
    if (fetch.skin)
        skin_ = buffer(device, "FORGE copied camera-relative skin", 769 * sizeof(Row));
    if (temporal_) {
        temporal_object_ =
            buffer(device, "FORGE previous object and view", temporal_object_rows * sizeof(Row));
        if (fetch.skin)
            previous_skin_ =
                buffer(device, "FORGE previous camera-relative skin", 769 * sizeof(Row));
    }
    RefCntAutoPtr<IBuffer> morph_offsets;
    if (fetch.morph_count) {
        require(mesh.morph_defaults.size() == fetch.morph_count,
                "morph defaults differ from the target count");
        morph_offsets =
            buffer(device, "FORGE immutable morph channel offsets",
                   fetch.morph_offsets.size() * sizeof(Row), fetch.morph_offsets.data());
        morph_weights_ = buffer(device, "FORGE copied morph weights",
                                ((fetch.morph_count + 3) / 4) * sizeof(Row));
        if (temporal_)
            previous_morph_weights_ = buffer(device, "FORGE previous morph weights",
                                             ((fetch.morph_count + 3) / 4) * sizeof(Row));
    }
    if (program.instanced) {
        BufferDesc desc;
        desc.Name = "FORGE static instance transforms";
        desc.Size = instance_limit * instance_rows() * sizeof(Row);
        desc.BindFlags = BIND_VERTEX_BUFFER;
        // A full Tile Map uses many Sprite batches in one frame. Streaming
        // those bytes through DISCARD would retain one allocation per batch
        // in the backend's fixed dynamic heap until submission. Ordered buffer
        // updates use its paged upload owner instead, without enlarging that
        // heap or changing the admitted scene profile.
        desc.Usage = sprite_ ? USAGE_DEFAULT : USAGE_DYNAMIC;
        desc.CPUAccessFlags = sprite_ ? CPU_ACCESS_NONE : CPU_ACCESS_WRITE;
        device->CreateBuffer(desc, nullptr, &instances_);
        require(bool(instances_), "instance buffer allocation failed");
    }
    object_ = buffer(device, "FORGE camera-relative object", 15 * sizeof(Row));
    lights_ = buffer(device, "FORGE light list",
                     mesh_draw_light_limit * (area_lights_ ? 7 : 4) * sizeof(Row));
    RefCntAutoPtr<IBuffer> values, surface_state, surface_uv, graph_pbr_values;
    if (surface) {
        const auto bytes = surface_parameter_bytes(source, surface_layout);
        material_defaults_ = bytes;
        for (const auto& binding : surface_layout.parameters)
            material_parameters_.push_back({binding.name, source.parameters.at(binding.name).type,
                                            binding.offset, binding.width});
        if (!bytes.empty())
            values =
                buffer(device, "FORGE reflected surface parameters", bytes.size(), bytes.data());
        if (surface_layout.settings) {
            const Row state{float(source.alpha), source.alpha_cutoff, 0, 0};
            surface_state = buffer(device, "FORGE surface state", sizeof(state), state.data());
        }
        if (surface_layout.uv_bytes) {
            const auto rows = surface_uv_rows(source, *surface->program.surface);
            require(rows.size() * sizeof(Row) == surface_layout.uv_bytes,
                    "surface UV buffer size mismatch");
            surface_uv = buffer(device, "FORGE surface texture coordinates",
                                surface_layout.uv_bytes, rows.data());
        }
    } else {
        values = buffer(device, "FORGE material values", material.uniforms.size() * sizeof(Row),
                        material.uniforms.data());
        if (!shadow_pass_ || source.alpha == MaterialAlpha::Mask) {
            const auto bytes = std::as_bytes(std::span(material.uniforms));
            material_defaults_.assign(bytes.begin(), bytes.end());
            material_parameters_ = material_parameter_uniforms(profile->values);
        }
    }
    if (!material_defaults_.empty())
        material_immutable_ = values;
    std::sort(material_parameters_.begin(), material_parameters_.end(),
              [](const auto& a, const auto& b) { return a.key < b.key; });
    if (surface && surface->program.surface->physically_based)
        graph_pbr_values = buffer(device, "FORGE graph PBR defaults",
                                  material.uniforms.size() * sizeof(Row), material.uniforms.data());
    auto sampler_states = material.samplers;
    if (surface)
        for (const auto& [role, declaration] : surface->program.surface->textures) {
            (void)declaration;
            auto state = source.textures.at(role).sampler;
            if (sprite_mips_ && role == *surface->program.surface->sprite_image)
                state = sprite_frame_sampler(state, *sprite_draw_mip_filter(draw_profile));
            sampler_states.push_back(state);
        }
    if (sprite_)
        sprite_image_variable_ = surface
                                     ? "g_SurfaceTexture_" + *surface->program.surface->sprite_image
                                     : "g_MaterialTexture0";
    std::vector<RefCntAutoPtr<ISampler>> samplers;
    std::vector<IDeviceObject*> sampler_objects;
    for (const auto& state : sampler_states) {
        samplers.push_back(upload_sampler(device, state));
        sampler_objects.push_back(samplers.back());
    }
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE prepared mesh draw";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    // Environment revisions change on a live SRB. Mutable means set-once in
    // Diligent; dynamic bindings are copied safely for each committed draw.
    std::vector<ShaderResourceVariableDesc> resource_variables{
        {SHADER_TYPE_VERTEX, "g_MeshVertices", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
         SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS}};
    for (Uint32 i = 0; i < pixel->GetResourceCount(); ++i) {
        ShaderResourceDesc resource;
        pixel->GetResourceDesc(i, resource);
        if (resource.Name && std::strcmp(resource.Name, "ForgeFogVolumes") == 0)
            resource_variables.push_back(
                {SHADER_TYPE_PIXEL, "ForgeFogVolumes", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }
    if (sprite_mips_ &&
        (!surface ||
         std::find(surface_layout.textures.begin(), surface_layout.textures.end(),
                   *surface->program.surface->sprite_image) != surface_layout.textures.end()))
        resource_variables.push_back({SHADER_TYPE_PIXEL, sprite_image_variable_.c_str(),
                                      SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    if (material_immutable_)
        resource_variables.push_back(
            {SHADER_TYPE_PIXEL, material_surface_ ? "ForgeSurfaceMaterial" : "ForgeMaterialValues",
             SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    if (fetch.morph_count)
        resource_variables.push_back({SHADER_TYPE_VERTEX, "g_ForgeMorphDeltas",
                                      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
                                      SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS});
    // Immutable raw buffers use bounded descriptor-table views on D3D12.
    // Root SRVs omit the view size and cannot check out-of-range shader loads.
    if (lit) {
        const ShaderResourceVariableDesc environment_variables[]{
            {SHADER_TYPE_PIXEL, "g_ForgeDiffuse", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_ForgeSpecular", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_ForgeShadows", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_ForgeCharlie", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
        resource_variables.insert(resource_variables.end(), std::begin(environment_variables),
                                  std::end(environment_variables) - (program.sheen ? 0 : 1));
        if (program.transmission)
            resource_variables.push_back(
                {SHADER_TYPE_PIXEL, "g_ForgeTransmission", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }
    std::array<std::string, environment_probe_limit> irradiance_names;
    if (local_irradiance_) {
        resource_variables.push_back(
            {SHADER_TYPE_PIXEL, "ForgeLocalIrradiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        for (unsigned i = 0; i < environment_probe_limit; ++i) {
            irradiance_names[i] = "g_ForgeIrradiance" + std::to_string(i);
            resource_variables.push_back({SHADER_TYPE_PIXEL, irradiance_names[i].c_str(),
                                          SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        }
    }
    std::array<std::string, decal_draw_limit> decal_names;
    std::array<std::string, decal_draw_limit> cube_names;
    if (decal_capacity_) {
        resource_variables.push_back(
            {SHADER_TYPE_PIXEL, "ForgeDecals", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        resource_variables.push_back(
            {SHADER_TYPE_PIXEL, "g_ForgeDecalSamplers", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        if (decal_clustered_)
            for (auto* name : decal_storage_names)
                resource_variables.push_back(
                    {SHADER_TYPE_PIXEL, name, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        for (unsigned i = 0; i < decal_capacity_; ++i) {
            decal_names[i] = decal_shader_map_name(i, decal_surface_channels_);
            resource_variables.push_back(
                {SHADER_TYPE_PIXEL, decal_names[i].c_str(), SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
            if (light_images_) {
                cube_names[i] = "g_ForgeLightCube" + std::to_string(i);
                resource_variables.push_back({SHADER_TYPE_PIXEL, cube_names[i].c_str(),
                                              SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
            }
        }
        if (light_images_)
            resource_variables.push_back(
                {SHADER_TYPE_PIXEL, "ForgeLightImages", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
    }
    ci.PSODesc.ResourceLayout.Variables = resource_variables.data();
    ci.PSODesc.ResourceLayout.NumVariables = Uint32(resource_variables.size());

    ci.pGS = geometry;
    ci.pVS = vertex;
    ci.pPS = pixel;
    auto& g = ci.GraphicsPipeline;
    g.NumRenderTargets = Uint8(shadow_pass_ ? 0
                               : weighted_  ? 2
                                            : 1 + unsigned(temporal_) + unsigned(indirect_) +
                                                  (reflection_ ? 3u
                                                   : normal_   ? 1u
                                                               : 0u));
    g.RTVFormats[0] = color_format;
    if (weighted_)
        g.RTVFormats[1] = TEX_FORMAT_RG32_FLOAT;
    if (temporal_)
        g.RTVFormats[1] = TEX_FORMAT_RGBA32_FLOAT;
    if (indirect_)
        g.RTVFormats[temporal_ ? 2 : 1] = TEX_FORMAT_RGBA16_FLOAT;
    if (reflection_ || normal_)
        for (unsigned slot = temporal_ ? 3u : 2u; slot < g.NumRenderTargets; ++slot)
            g.RTVFormats[slot] = TEX_FORMAT_RGBA16_FLOAT;
    g.DSVFormat = depth_format;
    g.PrimitiveTopology = mesh.topology;
    std::array<LayoutElement, wind_temporal_instance_rows> instance_layout;
    if (program.instanced) {
        const unsigned rows = instance_rows();
        for (unsigned i = 0; i < rows; ++i)
            instance_layout[i] =
                LayoutElement{i, 0, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE};
        g.InputLayout.LayoutElements = instance_layout.data();
        g.InputLayout.NumElements = rows;
    }
    g.DepthStencilDesc.DepthEnable = shadow_pass_ || source.depth_test;
    g.DepthStencilDesc.DepthWriteEnable = shadow_pass_ || source.depth_write;
    if (!shadow_pass_ && source.alpha == MaterialAlpha::Blend) {
        auto& b = g.BlendDesc.RenderTargets[0];
        b.BlendEnable = true;
        b.SrcBlend = weighted_ ? BLEND_FACTOR_ONE : BLEND_FACTOR_SRC_ALPHA;
        b.DestBlend = weighted_ ? BLEND_FACTOR_ONE : BLEND_FACTOR_INV_SRC_ALPHA;
        b.SrcBlendAlpha = BLEND_FACTOR_ONE;
        b.DestBlendAlpha = weighted_ ? BLEND_FACTOR_ONE : BLEND_FACTOR_INV_SRC_ALPHA;
    }
    for (unsigned parity = 0; parity < 3; ++parity) {
        g.RasterizerDesc.CullMode = source.double_sided || parity == 2 || volume_thickness_ > 0
                                        ? CULL_MODE_NONE
                                        : CULL_MODE_BACK;
        g.RasterizerDesc.FrontCounterClockwise = parity == 1;
        presentation.graphics(ci, &pipelines_[parity]);
        pipelines_[parity]->CreateShaderResourceBinding(&bindings_[parity], true);
        auto bind = [&](SHADER_TYPE stage, const char* name, IDeviceObject* value,
                        bool required = true) {
            auto* var = bindings_[parity]->GetVariableByName(stage, name);
            require(var || !required, std::string("shader binding missing: ") + name);
            if (var)
                var->Set(value);
        };
        bind(SHADER_TYPE_VERTEX, "g_MeshVertices",
             mesh.vertices->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        bind(SHADER_TYPE_VERTEX, "ForgeObject", object_);
        if (skin_) {
            bind(SHADER_TYPE_VERTEX, "ForgeSkin", skin_);
            bind(SHADER_TYPE_GEOMETRY, "ForgeSkin", skin_, false);
            bind(SHADER_TYPE_PIXEL, "ForgeSkin", skin_, false);
        }
        if (fetch.morph_count) {
            bind(SHADER_TYPE_VERTEX, "g_ForgeMorphDeltas",
                 mesh.morphs->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
            bind(SHADER_TYPE_VERTEX, "ForgeMorphOffsets", morph_offsets);
            bind(SHADER_TYPE_VERTEX, "ForgeMorphWeights", morph_weights_);
            if (temporal_)
                bind(SHADER_TYPE_VERTEX, "ForgePreviousMorphWeights", previous_morph_weights_);
        }
        if (temporal_) {
            bind(SHADER_TYPE_VERTEX, "ForgeTemporalObject", temporal_object_);
            bind(SHADER_TYPE_PIXEL, "ForgeTemporalObject", temporal_object_);
            if (previous_skin_)
                bind(SHADER_TYPE_VERTEX, "ForgePreviousSkin", previous_skin_);
        }
        bind(SHADER_TYPE_PIXEL, "ForgeObject", object_, false);
        if (local_irradiance_) {
            bind(SHADER_TYPE_PIXEL, "ForgeLocalIrradiance", empty_irradiance_);
            for (unsigned i = 0; i < environment_probe_limit; ++i)
                bind(SHADER_TYPE_PIXEL, irradiance_names[i].c_str(), black_environment_);
        }

        if (decal_capacity_) {
            bind(SHADER_TYPE_PIXEL, "ForgeDecals", empty_decals_);
            std::array<IDeviceObject*, decal_draw_limit> defaults;
            defaults.fill(default_decal_sampler_);
            auto* variable =
                bindings_[parity]->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeDecalSamplers");
            require(variable, "Decal sampler declaration is missing");
            ShaderResourceDesc reflected;
            variable->GetResourceDesc(reflected);
            require(reflected.ArraySize == decal_capacity_, "Decal sampler profile mismatch");
            variable->SetArray(defaults.data(), 0, decal_capacity_);
            for (unsigned i = 0; i < decal_capacity_; ++i)
                bind(SHADER_TYPE_PIXEL, decal_names[i].c_str(), white_decal_);
            if (light_images_) {
                bind(SHADER_TYPE_PIXEL, "ForgeLightImages", empty_light_images_);
                for (unsigned i = 0; i < decal_capacity_; ++i)
                    bind(SHADER_TYPE_PIXEL, cube_names[i].c_str(), black_environment_);
            }
        }
        if (!shadow_pass_ &&
            bindings_[parity]->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDistanceFog")) {
            if (!fog_)
                fog_ = buffer(device, "FORGE distance fog", 2 * sizeof(Row));
            bind(SHADER_TYPE_PIXEL, "ForgeDistanceFog", fog_);
        }
        if (!shadow_pass_ &&
            bindings_[parity]->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeFogVolumes")) {
            if (!empty_fog_volumes_) {
                const FogVolumeUniform empty{};
                empty_fog_volumes_ =
                    buffer(device, "FORGE empty fog volumes", sizeof(empty), empty.data());
            }
            bind(SHADER_TYPE_PIXEL, "ForgeFogVolumes", empty_fog_volumes_);
        }
        if (auto* variable =
                bindings_[parity]->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeLights")) {
            variable->Set(lights_);
            lights_active_ = true;
        }
        if (surface) {
            if (values)
                bind(SHADER_TYPE_PIXEL, "ForgeSurfaceMaterial", values);
            if (surface_state)
                bind(SHADER_TYPE_PIXEL, "ForgeSurfaceState", surface_state);
            if (surface_uv)
                bind(SHADER_TYPE_PIXEL, "ForgeSurfaceUV", surface_uv);
            if (graph_pbr_values)
                bind(SHADER_TYPE_PIXEL, "ForgeMaterialValues", graph_pbr_values, false);
        } else
            bind(SHADER_TYPE_PIXEL, "ForgeMaterialValues", values, !shadow_pass_);
        if (lit) {
            bind(SHADER_TYPE_PIXEL, "ForgeEnvironment", environment_);
            bind(SHADER_TYPE_PIXEL, "ForgeShadows", shadows_);
            SamplerDesc comparison;
            comparison.MinFilter = comparison.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
            comparison.MipFilter = FILTER_TYPE_COMPARISON_POINT;
            comparison.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
            comparison.AddressU = comparison.AddressV = comparison.AddressW =
                TEXTURE_ADDRESS_BORDER;
            std::fill(std::begin(comparison.BorderColor), std::end(comparison.BorderColor), 1.f);
            RefCntAutoPtr<ISampler> shadow_sampler;
            device->CreateSampler(comparison, &shadow_sampler);
            require(bool(shadow_sampler), "shadow comparison sampler allocation failed");
            bind(SHADER_TYPE_PIXEL, "g_ForgeShadowSampler", shadow_sampler);
            SamplerDesc sampler;
            sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
            RefCntAutoPtr<ISampler> native;
            device->CreateSampler(sampler, &native);
            require(bool(native), "environment sampler allocation failed");
            bind(SHADER_TYPE_PIXEL, "g_ForgeGGX", ggx);
            bind(SHADER_TYPE_PIXEL, "g_ForgeLightSampler", native);
            for (const auto* name : {"g_ForgeDiffuse", "g_ForgeSpecular", "g_ForgeCharlie"}) {
                bind(SHADER_TYPE_PIXEL, name, black_environment_, false);
            }
        }
        if (program.transmission) {
            bind(SHADER_TYPE_PIXEL, "ForgeTransmission", transmission_);
            bind(SHADER_TYPE_PIXEL, "g_ForgeTransmission", black_background_);
        }
        if (program.sheen) {
            bind(SHADER_TYPE_PIXEL, "g_ForgeSheen", sheen);
        }
        if (auto* variable = bindings_[parity]->GetVariableByName(
                SHADER_TYPE_PIXEL, surface ? "g_SurfaceSamplers" : material_sampler_variable)) {
            ShaderResourceDesc desc;
            variable->GetResourceDesc(desc);
            require(desc.ArraySize > 0 && desc.ArraySize <= sampler_objects.size(),
                    "material sampler array differs from the admitted binding table");
            variable->SetArray(sampler_objects.data(), 0, desc.ArraySize);
        }
        if (surface) {
            const auto dimension = [](TextureDimension value) {
                switch (value) {
                case TextureDimension::D2:
                    return RESOURCE_DIM_TEX_2D;
                case TextureDimension::D2Array:
                    return RESOURCE_DIM_TEX_2D_ARRAY;
                case TextureDimension::Cube:
                    return RESOURCE_DIM_TEX_CUBE;
                case TextureDimension::CubeArray:
                    return RESOURCE_DIM_TEX_CUBE_ARRAY;
                case TextureDimension::D3:
                    return RESOURCE_DIM_TEX_3D;
                }
                throw std::runtime_error("Unknown surface texture dimension");
            };
            for (const auto& role : surface_layout.textures) {
                if (sprite_mips_ && role == *surface->program.surface->sprite_image)
                    continue; // The complete derived frame is bound before drawing.
                const auto found = textures.find(role);
                require(found != textures.end() && found->second,
                        "surface texture unavailable: " + role);
                require(found->second->GetTexture()->GetDesc().Type ==
                            dimension(source.textures.at(role).dimension),
                        "surface texture dimension mismatch: " + role);
                bind(SHADER_TYPE_PIXEL, ("g_SurfaceTexture_" + role).c_str(), found->second);
            }
        }
        for (const auto& slot : material.textures) {
            if (sprite_mips_)
                continue; // The complete derived frame is bound immediately before drawing.
            const auto found = textures.find(slot.role);
            require(found != textures.end() && found->second,
                    "texture resource unavailable: " + slot.role);
            const auto& desc = found->second->GetTexture()->GetDesc();
            require(desc.Type == RESOURCE_DIM_TEX_2D, "material texture requires a 2D resource");
            bind(SHADER_TYPE_PIXEL, slot.texture_variable.c_str(), found->second, false);
        }
    }
    if (decal_capacity_ && decal_clustered_)
        bind_decal_storage(empty_decal_storage_);
}
void MeshDraw::bind_sprite_mip(ITextureView* view) {
    require(sprite_mips_ && view, "Sprite mip draw requires a complete filtered frame");
    const auto& desc = view->GetTexture()->GetDesc();
    require(desc.Type == RESOURCE_DIM_TEX_2D && desc.Format == TEX_FORMAT_RGBA16_FLOAT,
            "Sprite mip draw requires its linear premultiplied frame texture");
    for (auto& binding : bindings_) {
        auto* variable =
            binding->GetVariableByName(SHADER_TYPE_PIXEL, sprite_image_variable_.c_str());
        // A custom final-color function may leave its declared image unused;
        // reflection legitimately omits that optimized-out binding.
        require(variable || material_surface_, "Sprite mip color binding is unavailable");
        if (variable)
            variable->Set(view);
    }
}
void MeshDraw::bind_environment(const GpuEnvironment* maps) {
    if (!environment_)
        return;
    if (maps)
        require(maps->diffuse && maps->specular && maps->sheen, "incomplete environment candidate");
    auto view = [&](Diligent::ITexture* texture) {
        return texture ? texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)
                       : black_environment_.RawPtr();
    };
    for (auto& binding : bindings_)
        for (const auto& [name, value] :
             {std::pair{"g_ForgeDiffuse", view(maps ? maps->diffuse.RawPtr() : nullptr)},
              std::pair{"g_ForgeSpecular", view(maps ? maps->specular.RawPtr() : nullptr)},
              std::pair{"g_ForgeCharlie", view(maps ? maps->sheen.RawPtr() : nullptr)}})
            if (auto* variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, name))
                variable->Set(value);
}
void MeshDraw::bind_shadows(const ShadowLighting* shadows) {
    if (!shadows_)
        return;
    std::array<IDeviceObject*, shadow_light_limit> maps;
    for (unsigned i = 0; i < shadow_light_limit; ++i)
        maps[i] = shadows && shadows->maps[i] ? shadows->maps[i].RawPtr() : empty_shadow_.RawPtr();
    for (auto& binding : bindings_) {
        auto* variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadows");
        require(variable, "shadow resource array unavailable");
        variable->SetArray(maps.data(), 0, shadow_light_limit);
    }
}
void MeshDraw::bind_transmission(const TransmissionLighting* lighting) {
    if (!transmission_)
        return;
    auto* view = lighting ? lighting->background : black_background_.RawPtr();
    require(view, "transmission background unavailable");
    for (auto& binding : bindings_)
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeTransmission")->Set(view);
}
const MaterialParameterUniform* MeshDraw::material_parameter_binding(std::string_view key) const {
    const auto found = std::lower_bound(
        material_parameters_.begin(), material_parameters_.end(), key,
        [](const auto& item, std::string_view selected) { return item.key < selected; });
    return found != material_parameters_.end() && found->key == key ? &*found : nullptr;
}
bool MeshDraw::prepare_object_material_values(std::span<const MaterialParameterOverride> parameters,
                                              std::string_view slot) {
    if (!material_immutable_)
        return false;
    bool active = false;
    for (const auto& parameter : parameters)
        if (parameter.slot == slot)
            if (const auto* field = material_parameter_binding(parameter.key)) {
                detail::validate_material_parameter(parameter.parameter);
                require(field->type == parameter.parameter.type &&
                            field->offset <= material_defaults_.size() &&
                            field->width * sizeof(float) <=
                                material_defaults_.size() - field->offset,
                        "object material parameter differs from the admitted native layout");
                active = true;
            }
    if (active && !material_dynamic_)
        material_dynamic_ =
            buffer(device_, "FORGE object material values", material_defaults_.size());
    return active;
}
void MeshDraw::object_material_values(IDeviceContext* context,
                                      std::span<const MaterialParameterOverride> parameters,
                                      std::string_view slot) {
    const bool active = prepare_object_material_values(parameters, slot);
    if (active) {
        MapHelper<std::byte> memory(context, material_dynamic_, MAP_WRITE, MAP_FLAG_DISCARD);
        auto* bytes = static_cast<std::byte*>(memory);
        require(bytes != nullptr, "object material upload allocation failed");
        std::memcpy(bytes, material_defaults_.data(), material_defaults_.size());
        for (const auto& parameter : parameters)
            if (parameter.slot == slot)
                if (const auto* field = material_parameter_binding(parameter.key))
                    std::memcpy(bytes + field->offset, parameter.parameter.value.data(),
                                field->width * sizeof(float));
    }
    // A parameter optimized out of this pass needs no patch. The Entry already
    // checked the selected material declaration/domain before draw adoption.
    if (active || material_dynamic_bound_)
        for (auto& state : bindings_)
            if (state)
                if (auto* variable = state->GetVariableByName(
                        SHADER_TYPE_PIXEL,
                        material_surface_ ? "ForgeSurfaceMaterial" : "ForgeMaterialValues"))
                    variable->Set(active ? material_dynamic_ : material_immutable_);
    material_dynamic_bound_ = active;
}
void MeshDraw::clear_local_irradiance() {
    if (!local_irradiance_active_)
        return;
    for (const auto& binding : bindings_) {
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeLocalIrradiance")
            ->Set(empty_irradiance_);
        for (unsigned i = 0; i < environment_probe_limit; ++i)
            binding
                ->GetVariableByName(SHADER_TYPE_PIXEL,
                                    ("g_ForgeIrradiance" + std::to_string(i)).c_str())
                ->Set(black_environment_);
    }
    local_irradiance_active_ = false;
}
void MeshDraw::clear_decals(IDeviceContext*) {
    if (!decals_active_)
        return;
    std::array<IDeviceObject*, decal_draw_limit> samplers;
    samplers.fill(default_decal_sampler_);
    for (const auto& binding : bindings_) {
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDecals")->Set(empty_decals_);
        for (unsigned i = 0; i < decal_capacity_; ++i)
            binding
                ->GetVariableByName(SHADER_TYPE_PIXEL,
                                    (decal_shader_map_name(i, decal_surface_channels_)).c_str())
                ->Set(white_decal_);
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeDecalSamplers")
            ->SetArray(samplers.data(), 0, decal_capacity_);
        if (light_images_) {
            binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeLightImages")
                ->Set(empty_light_images_);
            for (unsigned i = 0; i < decal_capacity_; ++i)
                binding
                    ->GetVariableByName(SHADER_TYPE_PIXEL,
                                        ("g_ForgeLightCube" + std::to_string(i)).c_str())
                    ->Set(black_environment_);
        }
    }
    if (decal_clustered_)
        bind_decal_storage(empty_decal_storage_);
    decals_active_ = false;
}
void MeshDraw::bind_decal_storage(const DiligentPresentation::ProjectedBuffers& buffers) {
    const std::array<IBuffer*, 3> selected{buffers.rows, buffers.ranges, buffers.indices};
    for (const auto& binding : bindings_)
        for (unsigned i = 0; i < selected.size(); ++i) {
            auto* variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, decal_storage_names[i]);
            require(variable && selected[i], "Projected storage binding is unavailable");
            variable->Set(selected[i]->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        }
}
std::optional<MeshDraw::DecalBindings>
MeshDraw::decal_bindings(std::span<const Decal> decals, std::span<const LightImage> images) const {
    if (decals.size() > (decal_clustered_ && decal_capacity_ ? decal_scene_limit : decal_capacity_))
        return {};
    if ((!images.empty() && !light_images_) || images.size() > mesh_draw_light_limit)
        return {};
    DecalBindings result;
    result.textures.fill(white_decal_);
    result.cubes.fill(black_environment_);
    result.samplers.fill(default_decal_sampler_);
    std::array<const SamplerState*, decal_draw_limit> intents{};
    std::array<IDeviceObject*, decal_draw_limit> views{};
    unsigned used = 0;
    const auto select = [&](const ProjectedTexture& texture, const SamplerState& intent,
                            bool cube) -> float {
        if (!texture.view || !texture.sampler)
            return 0;
        if (texture.view->GetDesc().TextureDim !=
                (cube ? RESOURCE_DIM_TEX_CUBE : RESOURCE_DIM_TEX_2D) ||
            intent.compare != TextureCompare::None)
            return 0;
        unsigned slot = 0;
        while (slot < used && (views[slot] != texture.view || *intents[slot] != intent))
            ++slot;
        if (slot == used) {
            if (used == decal_capacity_)
                return 0;
            views[slot] = texture.view;
            (cube ? result.cubes : result.textures)[slot] = texture.view;
            result.samplers[slot] = texture.sampler;
            intents[used++] = &intent;
        }
        return float(slot + 1);
    };
    for (unsigned i = 0; i < decals.size(); ++i) {
        const auto& selected = decals[i];
        if (!selected.projection || !selected.material)
            return {};
        const auto& value = selected.projection->values;
        const bool normal =
            value.normal_influence > 0 && (selected.channels & (1u << decal_normal_channel));
        const bool rough =
            value.roughness_influence > 0 && (selected.channels & (1u << decal_roughness_channel));
        if (!decal_surface_channels_ && (normal || rough))
            return {};
        for (unsigned c = 0; c < decal_channel_count; ++c) {
            if ((c == decal_normal_channel && !normal) ||
                (c == decal_roughness_channel && !rough) ||
                !selected.material->textures[c].asset.id)
                continue;
            const auto& texture = selected.textures[c];
            if (!texture.view || !texture.sampler)
                return {};
            if (!decal_surface_channels_) {
                result.textures[i] = texture.view;
                result.samplers[i] = texture.sampler;
                result.slots[i][c] = 1;
                continue;
            }
            const auto& intent = selected.material->textures[c].slot.sampler;
            result.slots[i][c] = select(texture, intent, false);
            if (!result.slots[i][c])
                return {};
        }
    }
    for (unsigned i = 0; i < images.size(); ++i) {
        const auto& image = images[i];
        if (!image.frame)
            continue;
        if (!image.intent || image.frame->kind > unsigned(LightKind::Spot))
            return {};
        result.image_slots[i] =
            select(image.texture, *image.intent, image.frame->kind == unsigned(LightKind::Point));
        if (!result.image_slots[i])
            return {};
    }
    return result;
}
bool MeshDraw::accepts_decals(std::span<const Decal> decals) const {
    return decal_bindings(decals).has_value();
}
bool MeshDraw::accepts_projected(std::span<const Decal> decals,
                                 std::span<const LightImage> images) const {
    return decal_bindings(decals, images).has_value();
}
void MeshDraw::draw(IDeviceContext* context, const AffineTransform& object_world,
                    const CameraView& view, std::span<const LightView> lights,
                    const EnvironmentLighting* environment, const std::array<float, 3>* legacy_tint,
                    const ShadowLighting* shadows, std::span<const int> shadow_slots,
                    const TransmissionLighting* transmission, std::span<const float> morph_weights,
                    const SkinPose* skin, std::span<const Instance> instances,
                    std::span<const MaterialParameterOverride> object_parameters,
                    std::string_view material_slot, const SpriteDrawValues* sprite,
                    const SceneDistanceFog* fog, const Motion* motion,
                    std::span<const Decal> decals, const RenderBounds* decal_bounds,
                    std::span<const LightImage> images,
                    std::span<const EnvironmentProbe> local_irradiance, const MeshWindSample* wind,
                    std::span<const FogVolumeSample> local_fog) {
    require(shadow_pass_ || local_fog.empty() || fog_volumes_supported_,
            "Local fog volumes require a current surface wrapper; reimport its Shader and rebuild "
            "the Material");
    const auto volume_values =
        local_fog.empty()
            ? std::optional<FogVolumeUniform>{}
            : fog_volume_uniform(local_fog, view.projection[15] == 1 ? &view.forward : nullptr);
    require(local_irradiance.empty() || local_irradiance_,
            "receiver requires a current local irradiance Shader cook");
    std::optional<IrradianceValues> irradiance;
    if (!local_irradiance.empty())
        irradiance = prepare_irradiance(local_irradiance, view.position);
    require(images.empty() || images.size() == lights.size(),
            "Light image selection differs from the ordinary light list");
    const auto bindings = decal_bindings(decals, images);
    require(bindings.has_value(), "Projected channels exceed this receiver's map/profile capacity; "
                                  "ordinary rendering remains available");
    std::optional<std::array<Row, mesh_draw_light_limit * 4>> image_values;
    for (unsigned i = 0; i < images.size(); ++i)
        if (images[i].frame) {
            require(images[i].frame->kind == lights[i].kind,
                    "Light image projection differs from its light kind");
            const auto frame = light_image_gpu_frame(*images[i].frame, view.position);
            if (!image_values)
                image_values.emplace();
            std::copy(frame.begin(), frame.end(), image_values->begin() + i * 4);
            (*image_values)[i * 4][3] = bindings->image_slots[i];
            (*image_values)[i * 4 + 1][3] = float(images[i].frame->kind);
            (*image_values)[i * 4 + 2][3] = float(images[i].frame->clipped);
        }
    const bool has_images = image_values.has_value();
    std::vector<Row> decal_values;
    std::vector<DecalTransformRows> decal_transforms;
    const bool storage = decal_clustered_ && decals.size() > decal_capacity_;
    std::optional<DecalClusters> clusters;
    if (!decals.empty()) {
        decal_values.resize(decal_shader_rows(std::max<unsigned>(decal_capacity_, decals.size()),
                                              decal_surface_channels_, decal_clustered_));
        decal_values[0][0] = float(decals.size());
        if (storage)
            decal_transforms.reserve(decals.size());
        for (unsigned i = 0; i < decals.size(); ++i) {
            const auto& selected = decals[i];
            const auto& projection = *selected.projection;
            const auto& material = *selected.material;
            const auto transform = decal_gpu_transform(projection, view.position);
            const unsigned at = (decal_clustered_ ? 4 : 1) + (decal_surface_channels_ ? 13 : 8) * i;
            if (storage)
                decal_transforms.push_back(transform);
            for (unsigned r = 0; r < 3; ++r) {
                decal_values[at + r] = transform[r];
                decal_values[at + 4][r] = gpu(projection.forward[r]);
            }
            const auto& value = projection.values;
            decal_values[at + 3] = {gpu(double(material.color[0]) * value.tint_r),
                                    gpu(double(material.color[1]) * value.tint_g),
                                    gpu(double(material.color[2]) * value.tint_b),
                                    value.opacity *
                                        (decal_surface_channels_ ? 1 : value.color_influence)};
            decal_values[at + 4][3] = projection.cosine_fade_end;
            decal_values[at + 5] = {projection.cosine_fade_start, material.color[3],
                                    float(material.alpha), material.cutoff};
            decal_values[at + 6] = material.textures[decal_color_channel].uv[0];
            decal_values[at + 7] = material.textures[decal_color_channel].uv[1];
            decal_values[at + 6][3] = bindings->slots[i][decal_color_channel];
            if (decal_surface_channels_) {
                decal_values[at + 7][3] = bindings->slots[i][decal_normal_channel];
                decal_values[at + 8] = {
                    value.color_influence,
                    selected.channels & (1u << decal_normal_channel) ? value.normal_influence : 0.f,
                    selected.channels & (1u << decal_roughness_channel) ? value.roughness_influence
                                                                        : 0.f,
                    material.normal_scale};
                decal_values[at + 9] = material.textures[decal_normal_channel].uv[0];
                decal_values[at + 9][3] = bindings->slots[i][decal_roughness_channel];
                decal_values[at + 10] = material.textures[decal_normal_channel].uv[1];
                decal_values[at + 10][3] = float(material.roughness_component);
                decal_values[at + 11] = material.textures[decal_roughness_channel].uv[0];
                decal_values[at + 11][3] = material.roughness_scale;
                decal_values[at + 12] = material.textures[decal_roughness_channel].uv[1];
                decal_values[at + 12][3] = material.roughness_bias;
            }
        }
    }
    if (storage) {
        decal_values[0][1] = 1;
        if (decal_bounds)
            clusters = prepare_decal_clusters(decal_transforms, *decal_bounds, view.position);
        if (clusters) {
            decal_values[0][2] = 1;
            decal_values[0][3] = float(clusters->indices.size());
            std::copy(clusters->controls.begin(), clusters->controls.end(),
                      decal_values.begin() + 1);
        }
    }
    if (fog)
        validate_scene_distance_fog(*fog);
    require(shadow_pass_ || !fog || !fog->enabled || fog_supported_,
            "Distance fog needs a current surface wrapper; reimport its Shader and rebuild the "
            "Material");
    const auto world = skin_ ? AffineTransform{} : object_world;
    require(instances.empty() || (instances_ && instances.size() <= instance_limit),
            "instance batch exceeds its supported static draw profile");
    if (!instances.empty())
        require(instances.front().world == world,
                "instance batch leader differs from draw transform");
    require(context && lights.size() <= mesh_draw_light_limit,
            "invalid context or light list exceeds draw profile");
    require(temporal_ == bool(motion), "Mesh motion input differs from its prepared draw profile");
    require(!motion || !motion->valid || motion->previous_view,
            "Valid Mesh motion requires its completed-frame camera");
    const auto& previous_view = motion && motion->valid ? *motion->previous_view : view;
    const bool previous_valid = motion && motion->valid && motion->previous_world;
    if (previous_valid) {
        require(bool(skin_) == bool(motion->previous_skin),
                "Previous Mesh motion has a different skin profile");
        require(bool(morph_weights_) || motion->previous_morph_weights.empty(),
                "Previous morph values were supplied without targets");
    }
    std::array<Row, temporal_object_rows> temporal{};
    if (temporal_) {
        require(view.viewport.width && view.viewport.height,
                "Mesh motion requires a nonempty current viewport");
        const auto previous_world = skin_            ? AffineTransform{}
                                    : previous_valid ? *motion->previous_world
                                                     : world;
        const auto& origin = previous_valid ? previous_view.position : view.position;
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 3; ++c)
                temporal[r][c] = gpu(previous_world.m[r * 4 + c]);
            temporal[r][3] = gpu(previous_world.m[r * 4 + 3] - origin[r]);
            temporal[3][r] = gpu(previous_view.right[r]);
            temporal[4][r] = gpu(previous_view.up[r]);
            temporal[5][r] = gpu(previous_view.forward[r]);
        }
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                const float value = previous_view.projection[r * 4 + c];
                require(std::isfinite(value), "Nonfinite previous Mesh projection");
                temporal[6 + r][c] = value;
            }
        for (unsigned axis = 0; axis < 2; ++axis) {
            require(std::isfinite(motion->jitter[axis]) &&
                        std::isfinite(motion->previous_jitter[axis]),
                    "Nonfinite Mesh projection jitter");
            temporal[10][axis] = motion->jitter[axis];
            temporal[10][axis + 2] = motion->previous_jitter[axis];
        }
        temporal[11][0] = motion->valid && (instances_ || previous_valid) ? 1.f : 0.f;
        temporal[12] = {float(view.viewport.x), float(view.viewport.y), float(view.viewport.width),
                        float(view.viewport.height)};
    }
    require(shadow_slots.empty() || shadow_slots.size() == lights.size(),
            "shadow selections differ from the selected light list");
    require(sprite_ || !sprite, "Sprite values require the Sprite draw profile");
    if (sprite_) {
        require(!legacy_tint, "Sprite color must not use legacy blockout lighting");
        if (instances.empty()) {
            require(sprite, "Sprite draw requires its detached numeric values");
            validate_sprite_draw_values(*sprite);
            require(!sprite->trimmed() || sprite_trim_allowed_,
                    "Trimmed Sprite draws require blending without depth writes");
        }
        for (const auto& item : instances) {
            require(item.sprite, "Sprite instance requires its detached numeric values");
            require(!item.legacy_tint, "Sprite color must not use legacy blockout lighting");
            validate_sprite_draw_values(*item.sprite);
            require(!item.sprite->trimmed() || sprite_trim_allowed_,
                    "Trimmed Sprite instances require blending without depth writes");
        }
    } else
        for (const auto& item : instances)
            require(!item.sprite, "Sprite instance values require the Sprite draw profile");
    object_material_values(context, object_parameters, material_slot);
    if (morph_weights_) {
        if (morph_weights.empty())
            morph_weights = mesh_.morph_defaults;
        upload_morph_weights(context, morph_weights_, morph_weights, mesh_.morph_targets.size());
        if (previous_morph_weights_)
            upload_morph_weights(context, previous_morph_weights_,
                                 previous_valid ? motion->previous_morph_weights : morph_weights,
                                 mesh_.morph_targets.size());
    } else {
        require(morph_weights.empty(), "morph weights supplied to a mesh without targets");
    }
    if (skin_) {
        const auto values =
            prepare_skin_values(skin, mesh_.joint_palette.size(), view, volume_thickness_);
        MapHelper<Row> mapped(context, skin_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(mapped) != nullptr, "skin upload allocation failed");
        std::copy(values.begin(), values.end(), static_cast<Row*>(mapped));
        if (previous_skin_) {
            const auto previous =
                previous_valid ? prepare_skin_values(motion->previous_skin,
                                                     mesh_.joint_palette.size(), previous_view, 0)
                               : values;
            MapHelper<Row> old(context, previous_skin_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(old) != nullptr, "previous skin upload allocation failed");
            std::copy(previous.begin(), previous.end(), static_cast<Row*>(old));
        }
    } else
        require(!skin, "skin pose supplied to an unskinned mesh");
    if (fog_) {
        const auto value = fog ? *fog : SceneDistanceFog{};
        MapHelper<Row> mapped(context, fog_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(mapped) != nullptr, "distance fog upload allocation failed");
        mapped[0] = {value.color[0], value.color[1], value.color[2], value.enabled ? 1.f : 0.f};
        mapped[1] = {value.start, value.end, 0, 0};
    }
    if (empty_fog_volumes_) {
        if (volume_values) {
            if (!fog_volumes_)
                fog_volumes_ = buffer(device_, "FORGE fog volumes", sizeof(*volume_values));
            MapHelper<Row> mapped(context, fog_volumes_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(mapped), "fog volume upload allocation failed");
            std::copy(volume_values->begin(), volume_values->end(), static_cast<Row*>(mapped));
        }
        if (fog_volumes_active_ != bool(volume_values)) {
            for (const auto& binding : bindings_)
                if (auto* variable =
                        binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeFogVolumes"))
                    variable->Set(volume_values ? fog_volumes_ : empty_fog_volumes_);
            fog_volumes_active_ = bool(volume_values);
        }
    }
    std::array<Row, 15> object{};
    if (legacy_tint) {
        for (unsigned i = 0; i < 3; ++i) {
            require(std::isfinite((*legacy_tint)[i]) && (*legacy_tint)[i] >= 0 &&
                        (*legacy_tint)[i] <= 1,
                    "invalid legacy blockout color");
            object[14][i] = (*legacy_tint)[i];
        }
        object[14][3] = 1;
    }
    double largest = 0;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            largest = std::max(largest, std::abs(world.m[r * 4 + c]));
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c) {
            object[r][c] = gpu(world.m[r * 4 + c]);
            object[3 + r][c] = largest > 0 ? gpu(world.m[r * 4 + c] / largest) : 0;
        }
        object[r][3] = gpu(world.m[r * 4 + 3] - view.position[r]);
        object[6][r] = gpu(view.right[r]);
        object[7][r] = gpu(view.up[r]);
        object[8][r] = gpu(view.forward[r]);
    }
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            object[9 + r][c] = view.projection[r * 4 + c];
    if (transmission_ && volume_thickness_ > 0 &&
        transform_parity(world) != TransformParity::Singular) {
        // Frobenius norm bounds ||A^T N|| for every unit surface normal. This
        // is GPU admission of derived optical distance, never a LocalScale edit.
        double norm = 0;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                norm = std::hypot(norm, world.m[r * 4 + c]);
        require(double(volume_thickness_) * norm <= std::numeric_limits<float>::max(),
                "world-space volume thickness exceeds the finite GPU optical profile");
    }
    object[13][0] = float(lights.size());
    object[13][1] = gpu(largest);
    // Projection W is constant for orthographic views. Surface view direction
    // must then be parallel for every pixel, not aimed at the camera position.
    object[13][2] = view.projection[14] == 0 ? 1.f : 0.f;
    object[13][3] = transform_parity(world) == TransformParity::Singular ? 1.f : 0.f;
    std::array<Row, mesh_draw_light_limit * 4> packed{};
    std::optional<std::array<Row, mesh_draw_light_limit * 3>> area_values;
    if (area_lights_)
        area_values.emplace();
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& light = lights[i];
        auto* rows = packed.data() + i * 4;
        require(light.kind <= unsigned(LightKind::Sphere), "unknown light kind");
        const bool emitter = light.kind >= unsigned(LightKind::Rectangle);
        require(!emitter || area_lights_ || !lights_active_,
                "Area lights require an area-specialized Mesh draw");
        rows[0][0] = std::bit_cast<float>(light.kind + 1);
        for (unsigned c = 0; c < 3; ++c) {
            rows[0][c + 1] = gpu(light.position[c] - view.position[c]);
            rows[1][c] = gpu(light.direction[c]);
            rows[2][c] = gpu(double(light.color[c]) * light.intensity);
        }
        const int slot = shadows && !shadow_slots.empty() ? shadow_slots[i] : -1;
        require(slot >= -1 && slot < int(shadow_light_limit), "shadow light index out of range");
        require(!emitter || slot == -1, "Area lights do not support shadow maps");
        rows[1][3] = std::bit_cast<float>(std::int32_t(slot));
        const double range = light.range;
        rows[2][3] = gpu(range * range * range * range);
        require(range == 0 || rows[2][3] >= std::numeric_limits<float>::min(),
                "light range underflows native attenuation");
        if (light.kind == std::uint32_t(LightKind::Spot)) {
            rows[3][0] = gpu(1. / (double(light.cosine_inner) - light.cosine_outer));
            rows[3][1] = gpu(-double(light.cosine_outer) * rows[3][0]);
        }
        if (emitter && area_values) {
            const auto size = [](float value) {
                require(std::isfinite(value) && value >= .001f && value <= 10000,
                        "Area emitter dimensions must be 0.001–10000 metres");
                return value;
            };
            auto* shape = area_values->data() + i * 3;
            if (light.kind == unsigned(LightKind::Sphere))
                shape[0][0] = size(light.emitter_radius);
            else {
                shape[0][1] = size(light.emitter_width);
                shape[0][2] = size(light.emitter_height);
                for (unsigned c = 0; c < 3; ++c) {
                    shape[1][c] = gpu(light.right[c]);
                    shape[2][c] = gpu(light.up[c]);
                }
            }
        }
    }
    {
        MapHelper<Row> values(context, object_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(values) != nullptr, "object upload allocation failed");
        std::copy(object.begin(), object.end(), static_cast<Row*>(values));
    }
    if (temporal_) {
        MapHelper<Row> values(context, temporal_object_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(values) != nullptr, "Mesh motion upload allocation failed");
        std::copy(temporal.begin(), temporal.end(), static_cast<Row*>(values));
    }
    if (lights_active_) {
        MapHelper<Row> values(context, lights_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(values) != nullptr, "light upload allocation failed");
        std::copy(packed.begin(), packed.end(), static_cast<Row*>(values));
        if (area_values)
            std::copy(area_values->begin(), area_values->end(),
                      static_cast<Row*>(values) + mesh_draw_light_limit * 4);
    }
    const auto parity = skin_ ? TransformParity::Positive : transform_parity(world);
    const unsigned index =
        parity == TransformParity::Singular
            ? 2
            : unsigned((parity == TransformParity::Negative) != view.orientation_reversed);
    if (decals.empty())
        clear_decals(context);
    else {
        if (storage) {
            if (!decal_storage_.rows) {
                DiligentPresentation::ProjectedBuffers candidate;
                candidate.rows = buffer(device_, "FORGE projected rows",
                                        decal_scene_limit * 13 * sizeof(Row), nullptr, sizeof(Row));
                candidate.ranges = buffer(device_, "FORGE projected cell ranges",
                                          decal_cluster_cells * 2 * sizeof(std::uint32_t), nullptr,
                                          2 * sizeof(std::uint32_t));
                candidate.indices =
                    buffer(device_, "FORGE projected cell indices",
                           decal_cluster_cells * decal_scene_limit * sizeof(std::uint32_t), nullptr,
                           sizeof(std::uint32_t));
                decal_storage_ = std::move(candidate);
            }
            {
                MapHelper<Row> rows(context, decal_storage_.rows, MAP_WRITE, MAP_FLAG_DISCARD);
                require(static_cast<Row*>(rows), "Projected row upload allocation failed");
                std::copy_n(decal_values.data() + 4, decals.size() * 13, static_cast<Row*>(rows));
            }
            {
                using Range = std::array<std::uint32_t, 2>;
                MapHelper<Range> ranges(context, decal_storage_.ranges, MAP_WRITE,
                                        MAP_FLAG_DISCARD);
                require(static_cast<Range*>(ranges), "Projected range upload allocation failed");
                if (clusters)
                    std::copy(clusters->ranges.begin(), clusters->ranges.end(),
                              static_cast<Range*>(ranges));
                else
                    ranges[0] = {};
            }
            {
                MapHelper<std::uint32_t> indices(context, decal_storage_.indices, MAP_WRITE,
                                                 MAP_FLAG_DISCARD);
                require(static_cast<std::uint32_t*>(indices),
                        "Projected index upload allocation failed");
                indices[0] = 0;
                if (clusters)
                    std::copy(clusters->indices.begin(), clusters->indices.end(),
                              static_cast<std::uint32_t*>(indices));
            }
        }
        if (decal_clustered_)
            bind_decal_storage(storage ? decal_storage_ : empty_decal_storage_);
        MapHelper<Row> values(context, decals_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(values) != nullptr, "Decal upload allocation failed");
        std::copy_n(decal_values.data(),
                    decal_shader_rows(decal_capacity_, decal_surface_channels_, decal_clustered_),
                    static_cast<Row*>(values));
        // Each winding SRB retains native references. Update every SRB before
        // dropping an old selection, including winding modes absent this frame.
        for (const auto& binding : bindings_) {
            binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDecals")->Set(decals_);
        }
        decals_active_ = true;
    }
    if (!decals.empty() || has_images) {
        if (light_images_ && has_images) {
            MapHelper<Row> rows(context, light_images_buffer_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(rows), "Light image upload allocation failed");
            std::copy(image_values->begin(), image_values->end(), static_cast<Row*>(rows));
        }
        // Every winding retains references, even when absent from this frame.
        for (const auto& binding : bindings_) {
            for (unsigned i = 0; i < decal_capacity_; ++i)
                binding
                    ->GetVariableByName(SHADER_TYPE_PIXEL,
                                        (decal_shader_map_name(i, decal_surface_channels_)).c_str())
                    ->Set(bindings->textures[i]);
            binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeDecalSamplers")
                ->SetArray(bindings->samplers.data(), 0, decal_capacity_);
            if (light_images_) {
                binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeLightImages")
                    ->Set(has_images ? light_images_buffer_ : empty_light_images_);
                for (unsigned i = 0; i < decal_capacity_; ++i)
                    binding
                        ->GetVariableByName(SHADER_TYPE_PIXEL,
                                            ("g_ForgeLightCube" + std::to_string(i)).c_str())
                        ->Set(bindings->cubes[i]);
            }
        }
        decals_active_ = true;
    }
    if (irradiance) {
        {
            MapHelper<Row> values(context, irradiance_buffer_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(values), "local irradiance upload allocation failed");
            std::copy(irradiance->begin(), irradiance->end(), static_cast<Row*>(values));
        }
        for (const auto& binding : bindings_) {
            binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeLocalIrradiance")
                ->Set(irradiance_buffer_);
            for (unsigned i = 0; i < environment_probe_limit; ++i)
                binding
                    ->GetVariableByName(SHADER_TYPE_PIXEL,
                                        ("g_ForgeIrradiance" + std::to_string(i)).c_str())
                    ->Set(i < local_irradiance.size() ? local_irradiance[i].irradiance
                                                      : black_environment_.RawPtr());
        }
        local_irradiance_active_ = true;
    } else
        clear_local_irradiance();
    if (environment_) {
        Row params{0, 0, 1, 0};
        ITextureView *diffuse = black_environment_, *specular = black_environment_,
                     *charlie = black_environment_;
        if (environment) {
            require(environment->maps && std::isfinite(environment->intensity) &&
                        environment->intensity >= 0 && std::isfinite(environment->rotation),
                    "invalid environment lighting parameters");
            const auto& maps = *environment->maps;
            require(maps.diffuse && maps.specular && maps.sheen,
                    "incomplete environment candidate");
            diffuse = maps.diffuse->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            specular = maps.specular->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            charlie = maps.sheen->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            params = {environment->intensity, float(maps.specular->GetDesc().MipLevels - 1),
                      float(std::cos(environment->rotation)),
                      float(std::sin(environment->rotation))};
        }
        MapHelper<Row> values(context, environment_, MAP_WRITE, MAP_FLAG_DISCARD);
        require(static_cast<Row*>(values) != nullptr, "environment upload allocation failed");
        values[0] = params;
        for (const auto& [name, value] :
             {std::pair{"g_ForgeDiffuse", diffuse}, std::pair{"g_ForgeSpecular", specular},
              std::pair{"g_ForgeCharlie", charlie}})
            if (auto* variable = bindings_[index]->GetVariableByName(SHADER_TYPE_PIXEL, name))
                variable->Set(value);
    }
    if (shadows_) {
        {
            MapHelper<Row> data(context, shadows_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(data) != nullptr, "shadow upload allocation failed");
            if (shadows)
                std::copy(shadows->values.begin(), shadows->values.end(), static_cast<Row*>(data));
            else
                std::fill_n(static_cast<Row*>(data),
                            shadow_light_limit * ShadowLighting::rows_per_light, Row{});
        }
        bind_shadows(shadows);
    }
    if (transmission_) {
        require(transmission && transmission->background && transmission->viewport == view.viewport,
                "transmissive draw requires its camera's opaque background snapshot");
        const auto& v = transmission->viewport;
        const auto& desc = transmission->background->GetTexture()->GetDesc();
        require(v.width && v.height && desc.Width == v.width && desc.Height == v.height &&
                    desc.Format == TEX_FORMAT_RGBA16_FLOAT,
                "transmission background dimensions/format differ from its camera");
        {
            MapHelper<Row> data(context, transmission_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(data) != nullptr, "transmission upload allocation failed");
            data[0] = {float(v.x), float(v.y), 1.f / v.width, 1.f / v.height};
            data[1] = {float(desc.MipLevels - 1), 0, 0, 0};
        }
        bind_transmission(transmission);
    }
    if (instances_) {
        // Prepare the complete bounded batch before publishing any mapped bytes.
        std::array<Row, wind_temporal_instance_rows * instance_limit> copied{};
        const unsigned rows = instance_rows();
        const Instance single{world,  legacy_tint ? std::optional(*legacy_tint) : std::nullopt,
                              sprite, motion ? motion->previous_world : nullptr,
                              wind,   motion ? motion->previous_wind : nullptr};
        const auto selected = instances.empty() ? std::span(&single, 1) : instances;
        for (std::size_t i = 0; i < selected.size(); ++i) {
            const auto& item = selected[i];
            auto* output = copied.data() + i * rows;
            require(transform_parity(item.world) == transform_parity(world),
                    "instance batch mixes raster winding policies");
            double maximum = 0;
            if (!sprite_)
                for (unsigned r = 0; r < 3; ++r)
                    for (unsigned c = 0; c < 3; ++c)
                        maximum = std::max(maximum, std::abs(item.world.m[4 * r + c]));
            for (unsigned r = 0; r < 3; ++r) {
                for (unsigned c = 0; c < 3; ++c) {
                    output[r][c] = gpu(item.world.m[4 * r + c]);
                    if (!sprite_)
                        output[3 + r][c] = maximum > 0 ? gpu(item.world.m[4 * r + c] / maximum) : 0;
                }
                output[r][3] = gpu(item.world.m[4 * r + 3] - view.position[r]);
            }
            if (sprite_) {
                const auto& values = *item.sprite;
                // The unlit Sprite path does not consume a lighting basis. Reuse
                // those three rows for numeric fill mapping. The separate crop
                // row retains margins for all image modes.
                for (unsigned axis = 0; axis < 2; ++axis) {
                    const auto& mapping = values.image.axes[axis];
                    output[3 + axis] = {gpu(mapping.source_low), gpu(mapping.source_high),
                                        gpu(mapping.target_low), gpu(mapping.target_high)};
                }
                output[5] = {float(values.image.mode), gpu(values.image.axes[0].repeat),
                             gpu(values.image.axes[1].repeat), values.transpose_uv ? 1.f : 0.f};
                if (values.image.mode == 0)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        output[3][lane] = gpu(values.quad_crop[lane]);
                output[6] = values.color;
                for (unsigned axis = 0; axis < 2; ++axis) {
                    output[7][axis] = gpu(values.region.origin[axis]);
                    output[7][axis + 2] = gpu(values.region.scale[axis]);
                    output[8][axis] = gpu(values.region.sample_minimum[axis]);
                    output[8][axis + 2] = gpu(values.region.sample_maximum[axis]);
                }
                for (unsigned lane = 0; lane < 4; ++lane)
                    output[9][lane] = gpu(values.quad_crop[lane]);
            } else if (item.legacy_tint) {
                for (unsigned c = 0; c < 3; ++c) {
                    const auto value = (*item.legacy_tint)[c];
                    require(std::isfinite(value) && value >= 0 && value <= 1,
                            "invalid instance blockout color");
                    output[6][c] = value;
                }
                output[6][3] = 1;
            }
            if (wind_) {
                require(item.wind && item.wind->enabled,
                        "Wind draw requires its admitted geometry sample");
                const auto slot = temporal_ ? temporal_instance_rows : mesh_instance_rows;
                output[slot] = {gpu(item.wind->displacement[0]), gpu(item.wind->displacement[1]),
                                gpu(item.wind->inverse_height), 0};
                if (temporal_ && motion->valid && item.previous_world) {
                    require(item.previous_wind && item.previous_wind->enabled,
                            "Wind motion requires its retained previous geometry sample");
                    output[slot + 1] = {gpu(item.previous_wind->displacement[0]),
                                        gpu(item.previous_wind->displacement[1]),
                                        gpu(item.previous_wind->inverse_height), 0};
                }
            }
            if (temporal_ && motion->valid && item.previous_world) {
                for (unsigned r = 0; r < 3; ++r) {
                    for (unsigned c = 0; c < 3; ++c)
                        output[7 + r][c] = gpu(item.previous_world->m[4 * r + c]);
                    output[7 + r][3] =
                        gpu(item.previous_world->m[4 * r + 3] - previous_view.position[r]);
                }
                output[10][0] = 1;
            }
        }
        if (sprite_) {
            context->UpdateBuffer(instances_, 0, selected.size() * rows * sizeof(Row),
                                  copied.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        } else {
            MapHelper<Row> mapped(context, instances_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<Row*>(mapped) != nullptr, "instance upload allocation failed");
            for (std::size_t i = 0; i < selected.size(); ++i)
                std::copy_n(copied.data() + i * rows, rows, static_cast<Row*>(mapped) + i * rows);
        }
        IBuffer* vertices[]{instances_};
        const Uint64 offset = 0;
        context->SetVertexBuffers(0, 1, vertices, &offset,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                  SET_VERTEX_BUFFERS_FLAG_RESET);
    }
    context->SetPipelineState(pipelines_[index]);
    context->CommitShaderResources(bindings_[index], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (mesh_.indices) {
        context->SetIndexBuffer(mesh_.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawIndexedAttribs draw;
        draw.NumIndices = mesh_.index_count;
        draw.NumInstances = instances.empty() ? 1 : Uint32(instances.size());
        draw.IndexType = mesh_.index_type;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->DrawIndexed(draw);
    } else {
        DrawAttribs draw;
        draw.NumVertices = mesh_.vertex_count;
        draw.NumInstances = instances.empty() ? 1 : Uint32(instances.size());
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
    }
}
} // namespace forge
