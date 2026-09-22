#pragma once
#include "mesh_draw.hpp"
#include "shader_diligent.hpp"
#include "texture_gpu.hpp"
inline void check_surface_draw(forge::DiligentPresentation& presentation,
                               Diligent::IDeviceContext* context,
                               const std::filesystem::path& images) {
    using namespace forge;
    using namespace Diligent;
    ShaderProgramSource source;
    source.stages = {{ShaderStage::Pixel, "surface/main.hlsl", "Shade"}};
    source.surface.emplace();
    source.surface->uv_sets = {17};
    source.surface->parameters["tint"] = {MaterialParameterType::LinearColor4, {.2f, .6f, .1f, 1}};
    source.surface->textures["color"].semantic = TextureSemantic::Color;
    source.surface->textures["color"].uv_set = 17;
    ShaderSources files{{"surface/main.hlsl",
                         "#include \"lib/factor.hlsli\"\n"
                         "float4 Shade(ForgeSurfaceInput input){return "
                         "ForgeParameter_tint()*ForgeSample_color(input)*input.Color*Factor();}"},
                        {"surface/lib/factor.hlsli", "float Factor(){return 1;}"}};
    auto selected = asset_detail::compile_diligent_shader(presentation.device(), source, files, {});
    MaterialShaderSnapshot snapshot{{AssetId::generate()},
                                    selected.data.build_key,
                                    decode_shader(encode_shader(selected.data))};
    auto material = surface_material_defaults(*source.surface);
    forge::TextureData tex;
    tex.width = tex.height = 1;
    tex.format = TextureFormat::RGBA8Srgb;
    tex.semantic = TextureSemantic::Color;
    tex.subresources = {std::vector<std::byte>(4, std::byte{255})};
    auto texture = upload_texture(presentation.device(), tex);
    MeshDraw::Textures textures;
    textures["color"] = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 0, -1, 1, 0, 1, 1, 0, 1, -1, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
                    {"COLOR_0", 4, std::vector<float>(16, 1)},
                    {"TEXCOORD_17", 2, std::vector<float>(8, .5f)}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    auto gpu = upload_mesh(presentation.device(), mesh);
    auto prepare = [&](const GpuMeshPart& geometry, const MaterialData& values,
                       bool depth_only = false, const MaterialShaderSnapshot* program = nullptr) {
        return std::make_unique<MeshDraw>(presentation, context, geometry, values, textures,
                                          depth_only ? TEX_FORMAT_UNKNOWN : TEX_FORMAT_RGBA8_UNORM,
                                          TEX_FORMAT_D32_FLOAT, true, SHADER_COMPILER_DEFAULT,
                                          SHADER_OPTIMIZATION_LEVEL_DEFAULT,
                                          program ? program : &snapshot);
    };
    auto current = prepare(gpu.lods[0].parts[0], material);
    const auto hits = presentation.cache_hits();
    auto repeated = prepare(gpu.lods[0].parts[0], material);
    require(presentation.cache_hits() >= hits + 3,
            "Repeated custom surfaces must reuse the presentation shader cache");
    TextureDesc desc;
    desc.Name = "FORGE custom surface acceptance";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    require(color && depth, "Surface targets unavailable");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    Camera camera;
    camera.vertical_fov = 1.5707963267948966;
    camera.far_plane = 10;
    auto view = camera_view(camera, {}, 32, 32);
    AffineTransform world;
    world.m[11] = 2;
    const auto clear = [&] {
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0, 0, 32, 32, 0, 1};
        context->SetViewports(1, &viewport, 32, 32);
    };
    const auto render = [&](MeshDraw& draw, std::span<const float> weights = {},
                            const SkinPose* pose = nullptr) {
        clear();
        draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {}, nullptr, weights, pose);
        return readback(presentation.device(), context, rtv);
    };
    const auto reference = render(*current);
    const auto center = 16 * 32 + 16;
    require(reference[center][0] == 51 && reference[center][1] == 153 &&
                reference[center][2] >= 25 && reference[center][2] <= 26,
            "Custom reflected parameter/UV/texture binding did not produce expected pixels");
    save(reference, 32, 32, images / "surface-custom-color.ppm");
    world.m[0] = -1;
    require(render(*current) == reference, "Custom surface changed reflected geometry coverage");
    world.m[0] = 1;
    world.m[10] = 0;
    require(render(*current) == reference, "Custom surface lost surviving zero-scale geometry");
    world.m[10] = 1;
    auto masked = material;
    masked.alpha = MaterialAlpha::Mask;
    masked.parameters.at("tint").value[3] = .25f;
    auto mask_draw = prepare(gpu.lods[0].parts[0], masked);
    require(render(*mask_draw)[center] == std::array<unsigned char, 4>{0, 0, 0, 255},
            "Custom alpha mask did not discard below cutoff");
    masked.alpha_cutoff = .1f;
    mask_draw = prepare(gpu.lods[0].parts[0], masked);
    require(render(*mask_draw) == reference, "Custom accepted mask changed opaque color");
    auto blended = material;
    blended.alpha = MaterialAlpha::Blend;
    blended.depth_write = false;
    blended.parameters.at("tint").value[3] = .5f;
    auto blend_draw = prepare(gpu.lods[0].parts[0], blended);
    auto blend_pixels = render(*blend_draw);
    require(blend_pixels[center][0] >= 25 && blend_pixels[center][0] <= 26 &&
                blend_pixels[center][1] >= 76 && blend_pixels[center][1] <= 77,
            "Custom blend ignored surface alpha/pipeline intent");
    save(blend_pixels, 32, 32, images / "surface-custom-blend.ppm");
    // Same custom function supplies shadow mask coverage, not an unrelated
    // built-in base-color alpha interpretation.
    MaterialData behind;
    behind.model = "forge.gltf.unlit.v1";
    behind.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4, {1, 0, 0, 1}};
    MeshDraw behind_draw(presentation, context, gpu.lods[0].parts[0], behind, {},
                         TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    for (const bool discard : {false, true}) {
        masked.alpha_cutoff = discard ? .5f : .1f;
        auto shadow = prepare(gpu.lods[0].parts[0], masked, true);
        clear();
        context->SetRenderTargets(0, nullptr, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        shadow->draw(context, world, view, {});
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        auto farther = world;
        farther.m[11] = 3;
        behind_draw.draw(context, farther, view, {});
        const auto pixels = readback(presentation.device(), context, rtv);
        require(pixels[center][0] == (discard ? 255 : 0),
                "Custom color/shadow mask coverage disagrees");
    }
    // Existing geometry implementation is exercised with a custom pixel program.
    auto morphed = mesh;
    morphed.morph_names = {"move"};
    morphed.morph_defaults = {0};
    morphed.lods[0].parts[0].morph_targets = {
        {{"POSITION", 3, std::vector<float>{.5f, 0, 0, .5f, 0, 0, .5f, 0, 0, .5f, 0, 0}}}};
    auto gpu_morph = upload_mesh(presentation.device(), morphed);
    auto morph_draw = prepare(gpu_morph.lods[0].parts[0], material);
    require(render(*morph_draw) == reference, "Custom surface changed zero-weight morph geometry");
    const std::array<float, 1> weights{1};
    require(render(*morph_draw, weights) != reference, "Custom surface bypassed morph deformation");
    auto skinned = mesh;
    auto& skin_part = skinned.lods[0].parts[0];
    skin_part.joint_palette = {0};
    skin_part.streams.push_back({"JOINTS_0", 4, std::vector<std::uint32_t>(16, 0)});
    skin_part.streams.push_back(
        {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}});
    auto gpu_skin = upload_mesh(presentation.device(), skinned);
    auto skin_draw = prepare(gpu_skin.lods[0].parts[0], material);
    const std::array<AffineTransform, 1> inverse_bind{};
    std::array<AffineTransform, 1> joints{world};
    auto pose = prepare_skin_pose(joints, inverse_bind, skin_part.joint_palette, skin_part.bounds);
    require(render(*skin_draw, {}, &pose) == reference, "Custom surface bypassed skin geometry");
    joints[0].m[0] = -1;
    pose = prepare_skin_pose(joints, inverse_bind, skin_part.joint_palette, skin_part.bounds);
    require(render(*skin_draw, {}, &pose) == reference,
            "Custom surface bypassed signed skin winding");
    auto bad = snapshot;
    bad.program.surface->parameters.at("tint").type = MaterialParameterType::Scalar;
    bool failed = false;
    try {
        (void)prepare(gpu.lods[0].parts[0], material, false, &bad);
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed && render(*current) == reference,
            "Failed custom GPU candidate damaged previous complete draw");
    files.at("surface/lib/factor.hlsli") = "float Factor(){return .5;}";
    auto next = asset_detail::compile_diligent_shader(presentation.device(), source, files, {});
    MaterialShaderSnapshot replacement{snapshot.shader, next.data.build_key, next.data};
    auto next_draw = prepare(gpu.lods[0].parts[0], material, false, &replacement);
    require(render(*next_draw)[center][1] < reference[center][1] && render(*current) == reference,
            "Compatible include rebuild failed to prepare independently of retained draw");
}
