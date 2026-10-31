#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "mesh_draw_shader.hpp"
#include "mesh_vertex_fetch.hpp"
#include "render_backend.hpp"
#include "texture_gpu.hpp"
#include "weighted_transparency_shader.hpp"
#include <algorithm>
#include <chrono>
#include <forge/engine_assets.hpp>
#include <forge/material_graph.hpp>
#include <iostream>
#include <thread>
using namespace Diligent;
using namespace forge;
namespace {
using test_bloom::check;
using Pixels = std::vector<test_bloom::Pixel>;
void equal_pixels(const Pixels& actual, const Pixels& expected, const char* why) {
    check(actual.size() == expected.size(), "Decal frame extents differ");
    for (std::size_t i = 0; i < actual.size(); ++i)
        for (unsigned c = 0; c < 4; ++c)
            check(std::isfinite(actual[i][c]) && std::abs(actual[i][c] - expected[i][c]) <= 2e-5f,
                  why);
}
void execute(const std::filesystem::path& root, bool scene_only, bool channels_only,
             bool clusters_only) {
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    check(device && context, "Decal native device unavailable");
    DiligentPresentation presentation(device);
    const auto compile_pixel = [&](const std::string& source, const char* entry = "main") {
        ShaderCreateInfo shader;
        shader.Desc.Name = "FORGE combined projected material admission";
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.Source = source.c_str();
        shader.EntryPoint = entry;
        shader.LoadConstantBufferReflection = true;
        shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> pixel;
        presentation.shader(shader, &pixel);
        return pixel;
    };
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 0, -1, 1, 0, 1, 1, 0, 1, -1, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
                    {"TEXCOORD_0", 2, std::vector<float>{0, 1, 0, 0, 1, 0, 1, 1}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    const auto gpu_mesh = upload_mesh(device, mesh);
    MaterialData receiver;
    receiver.model = "forge.gltf.metallic-roughness.v1";
    receiver.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                              {.2f, .4f, .6f, 1}};
    receiver.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    receiver.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.7f}};
    auto prepare = [&](const MaterialData& material,
                       const MeshDraw::Textures& textures = MeshDraw::Textures{}) {
        return std::make_unique<MeshDraw>(presentation, context, gpu_mesh.lods[0].parts[0],
                                          material, textures, TEX_FORMAT_RGBA32_FLOAT,
                                          TEX_FORMAT_D32_FLOAT);
    };
    auto draw = prepare(receiver);
    check(draw->decal_capacity() == 8, "Simple PBR receiver lost the eight-projector profile");
    TextureDesc target;
    target.Name = "FORGE decal PBR acceptance";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = target.Height = 32;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(target, nullptr, &color);
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(target, nullptr, &depth);
    check(color && depth, "Decal receiver targets unavailable");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    RefCntAutoPtr<ITexture> weights;
    target.Format = TEX_FORMAT_RG32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET;
    device->CreateTexture(target, nullptr, &weights);
    check(bool(weights), "Projected weighted target unavailable");
    auto* weights_rtv = weights->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    AffineTransform camera_world, world;
    camera_world.m[3] = world.m[3] = std::ldexp(1., 40);
    camera_world.m[7] = world.m[7] = -std::ldexp(1., 40);
    world.m[11] = 3;
    Camera camera;
    camera.near_plane = .1;
    camera.far_plane = 10;
    const auto view = camera_view(camera, camera_world, 32, 32);
    Light light;
    light.intensity = 1;
    auto illumination = light_view(light, {});
    auto render = [&](MeshDraw& prepared, std::span<const MeshDraw::Decal> decals = {},
                      const TransmissionLighting* background = nullptr, bool weighted = false,
                      const RenderBounds* region = nullptr) {
        ITextureView* targets[]{rtv, weights_rtv};
        context->SetRenderTargets(weighted ? 2 : 1, targets, dsv,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{0, 0, 0, 0};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (weighted)
            context->ClearRenderTarget(weights_rtv, clear,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 32, 32, 0, 1};
        context->SetViewports(1, &viewport, 32, 32);
        prepared.draw(context, world, view, std::span(&illumination, 1), nullptr, nullptr, nullptr,
                      {}, background, {}, nullptr, {}, {}, {}, nullptr, nullptr, nullptr, decals,
                      region);
        return test_bloom::read(device, context, color);
    };
    DecalProjector projector;
    projector.width = projector.height = 8;
    projector.depth = 2;
    if (!scene_only && !channels_only) {
        const auto unpainted = render(*draw);
        std::array<DecalView, 128> projections;
        std::array<DecalMaterial, 128> materials;
        std::array<MeshDraw::Decal, 128> selection;
        MaterialData source;
        source.model = "forge.gltf.unlit.v1";
        source.alpha = MaterialAlpha::Blend;
        source.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                {1, 1, 1, .7f}};
        const auto source_material = prepare_decal_material(source, {});
        for (unsigned case_id = 0; case_id < 6; ++case_id) {
            auto expected_values = receiver;
            for (unsigned i = 0; i < selection.size(); ++i) {
                auto value = projector;
                value.width = value.height = case_id == 2 ? 8.f : .6f;
                value.opacity = .35f;
                auto placement = world;
                if (case_id != 2) {
                    placement.m[3] += (double(i % 8) - 3.5) * .25;
                    placement.m[7] += (double((i / 8) % 8) - 3.5) * .25;
                }
                if (case_id == 1) {
                    placement.m[0] = i % 2 ? -1 : 1;
                    placement.m[1] = .35;
                    placement.m[4] = -.15;
                }
                projections[i] = decal_view(value, placement);
                materials[i] = source_material;
                materials[i].color[0] = float(i % 7) / 7;
                materials[i].color[1] = float(i % 11) / 11;
                materials[i].color[2] = float(i % 13) / 13;
                selection[i] = {&projections[i], &materials[i]};
                if (case_id == 2)
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        auto& color = expected_values.parameters["baseColorFactor"].value[channel];
                        color = std::lerp(color, materials[i].color[channel], .35f * .7f);
                    }
            }
            auto region = transform_bounds(part.bounds, world);
            if (case_id == 3)
                region.minimum = region.maximum = {world.m[3], world.m[7], world.m[11]};
            if (case_id == 4)
                region.maximum[0] = region.minimum[0] + .25; // Other fragments use the full list.
            if (case_id == 5) {
                region.minimum[0] -= 1e25;
                region.maximum[0] += 1e25; // This poorly resolved axis must collapse.
            }
            const auto exhaustive = render(*draw, selection);
            for (unsigned frame = 0; frame < 3; ++frame) {
                equal_pixels(render(*draw, selection, nullptr, false, &region), exhaustive,
                             "Spatial projector selection differs from exhaustive PBR output");
                context->FinishFrame();
            }
            if (case_id == 2) {
                auto expected = prepare(expected_values);
                equal_pixels(
                    exhaustive, render(*expected),
                    "Dense storage list lost ordered projectors or changed pre-BRDF blending");
            }
            check(exhaustive != unpainted, "Large projector list did not paint the receiver");
            const auto small = std::span(selection).first(8);
            const auto small_frame = render(*draw, small);
            context->FinishFrame();
            equal_pixels(render(*draw, small, nullptr, false, &region), small_frame,
                         "Storage-to-small-packet transition changed receiver output");
            equal_pixels(render(*draw), unpainted,
                         "Clearing storage projection retained prior rows or maps");
            context->FinishFrame();
            equal_pixels(render(*draw, selection, nullptr, false, &region), exhaustive,
                         "Storage projection did not reactivate after an empty frame");
        }
        std::cout << "Clustered PBR 128-projector spatial/exhaustive/order/large-origin/affine/"
                     "collapsed/outside/clear/reactivate frames passed\n";
        if (clusters_only) {
            context->WaitForIdle();
            return;
        }
    }
    if (!scene_only) {
        const auto baseline = render(*draw);
        check(baseline[16 * 32 + 16][3] == 1 && baseline[16 * 32 + 16][1] > .01f,
              "PBR receiver is absent or unlit");
        auto projection = decal_view(projector, world);
        MaterialData source;
        source.model = "forge.gltf.unlit.v1";
        source.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                {.8f, .1f, .2f, .25f}};
        auto material = prepare_decal_material(source, {});
        MeshDraw::Decal decal{&projection, &material};
        const auto selected = std::span(&decal, 1);
        auto manual = receiver;
        manual.parameters["baseColorFactor"].value = {.8f, .1f, .2f, 1};
        auto reference = prepare(manual);
        equal_pixels(render(*draw, selected), render(*reference),
                     "Projected opaque color differs from the same pre-lighting material color");
        equal_pixels(render(*draw), baseline,
                     "Removing the projector failed to restore ordinary PBR");
        projection.values.opacity = .25f;
        for (unsigned c = 0; c < 3; ++c)
            manual.parameters["baseColorFactor"].value[c] = std::lerp(
                receiver.parameters.at("baseColorFactor").value[c], material.color[c], .25f);
        reference = prepare(manual);
        equal_pixels(render(*draw, selected), render(*reference),
                     "Projected opacity blends after lighting instead of before it");
        material.alpha = MaterialAlpha::Blend;
        for (unsigned c = 0; c < 3; ++c)
            manual.parameters["baseColorFactor"].value[c] = std::lerp(
                receiver.parameters.at("baseColorFactor").value[c], material.color[c], .0625f);
        reference = prepare(manual);
        equal_pixels(render(*draw, selected), render(*reference),
                     "Projected source alpha differs from color composition");
        material.alpha = MaterialAlpha::Mask;
        material.cutoff = .5f;
        equal_pixels(render(*draw, selected), baseline,
                     "Masked decal discards receiving geometry instead of skipping its paint");
        material.alpha = MaterialAlpha::Opaque;
        projection.forward = {0, 0, -1};
        equal_pixels(render(*draw, selected), baseline,
                     "Reverse projection paints a rejected surface");
        projection = decal_view(projector, world);
        projection.projector_from_world.m[11] += 3;
        equal_pixels(render(*draw, selected), baseline,
                     "Out-of-volume projection changes the receiver");
        projection = decal_view(projector, world);
        const auto lit = render(*draw, selected);
        illumination.intensity = .25f;
        const auto dim = render(*draw, selected);
        check(dim[16 * 32 + 16][0] < lit[16 * 32 + 16][0] * .3f,
              "Projected color behaves as an unlit overlay");
        illumination.intensity = 1;
        std::array<MeshDraw::Decal, 129> overflow;
        overflow.fill(decal);
        bool rejected = false;
        try {
            (void)render(*draw, overflow);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Renderer silently truncated the projector capacity");
        equal_pixels(render(*draw), baseline,
                     "Rejected projection damaged ordinary receiver state");
        for (const auto alpha :
             {MaterialAlpha::Opaque, MaterialAlpha::Mask, MaterialAlpha::Blend}) {
            auto variant = receiver;
            variant.alpha = alpha;
            variant.parameters["baseColorFactor"].value[3] =
                alpha == MaterialAlpha::Opaque ? 1 : .25f;
            auto reference_values = variant;
            for (unsigned c = 0; c < 3; ++c)
                reference_values.parameters["baseColorFactor"].value[c] = material.color[c];
            auto projected = prepare(variant), expected = prepare(reference_values);
            equal_pixels(render(*projected, selected), render(*expected),
                         "Decal color changed receiver transparency or mask coverage");
        }
        {
            auto variant = receiver;
            variant.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.5f}};
            auto reference_values = variant;
            for (unsigned c = 0; c < 3; ++c)
                reference_values.parameters["baseColorFactor"].value[c] = material.color[c];
            auto projected = prepare(variant), expected = prepare(reference_values);
            const auto opaque = test_bloom::texture(
                device, 32, 32, std::vector<test_bloom::Pixel>(32 * 32, {.05f, .1f, .2f, 1}));
            TransmissionBackground snapshot(presentation);
            const auto background = snapshot.capture(context, opaque, view.viewport);
            equal_pixels(render(*projected, selected, &background),
                         render(*expected, {}, &background),
                         "Projected color differs from the same transmissive material albedo");
        }
        {
            // Compare projected channels to the existing ordinary material owner,
            // not to a second rendering implementation of the projection equation.
            auto projection = decal_view(projector, world);
            projection.values.color_influence = 0;
            projection.values.normal_influence = 1;
            projection.values.roughness_influence = 1;
            auto source = receiver;
            source.parameters["roughnessFactor"].value[0] = .5f;
            source.parameters["normalScale"] = {MaterialParameterType::Scalar, {.75f}};
            MaterialTextureSlot normal_slot, rough_slot;
            normal_slot.semantic = TextureSemantic::Normal;
            rough_slot.semantic = TextureSemantic::Data;
            source.textures = {{"normalTexture", normal_slot},
                               {"metallicRoughnessTexture", rough_slot}};
            MaterialTextureBindings bindings{{"normalTexture", {AssetId::generate()}},
                                             {"metallicRoughnessTexture", {AssetId::generate()}}};
            auto channels = prepare_decal_material(source, bindings);
            const auto normal_map = test_bloom::texture(device, 1, 1, {{.75f, .625f, .875f, 1}});
            const auto flat_map = test_bloom::texture(device, 1, 1, {{.5f, .5f, 1, 1}});
            const auto rough_map = test_bloom::texture(device, 1, 1, {{.1f, .25f, .8f, .5f}});
            auto sampler = upload_sampler(device, SamplerState{});
            MeshDraw::Decal selected{&projection, &channels};
            selected.textures[decal_normal_channel] = {
                normal_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), sampler};
            selected.textures[decal_roughness_channel] = {
                rough_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), sampler};
            const auto selected_span = std::span(&selected, 1);
            auto expected_values = receiver;
            expected_values.parameters["roughnessFactor"].value[0] = .125f;
            expected_values.parameters["normalScale"] = source.parameters.at("normalScale");
            expected_values.textures["normalTexture"] = normal_slot;
            MeshDraw::Textures normal_views;
            normal_views["normalTexture"] = selected.textures[decal_normal_channel].view;
            auto expected = prepare(expected_values, normal_views);
            const auto full = render(*draw, selected_span);
            std::array<MeshDraw::Decal, 128> large_channels;
            large_channels.fill(selected);
            check(draw->accepts_decals(large_channels),
                  "Shared channel maps were counted once per projector instead of per binding");
            const auto channel_region = transform_bounds(part.bounds, world);
            equal_pixels(render(*draw, large_channels, nullptr, false, &channel_region),
                         render(*draw, large_channels),
                         "Storage normal/roughness maps differ between grid and full selection");
            equal_pixels(full, render(*expected),
                         "Projected normal/roughness differs from ordinary MR material shading");
            for (bool area : {false, true}) {
                auto blend = receiver, expected_blend = expected_values;
                for (auto* values : {&blend, &expected_blend}) {
                    values->alpha = MaterialAlpha::Blend;
                    values->depth_write = false;
                    values->parameters["baseColorFactor"].value[3] = .4f;
                }
                MeshDraw actual(presentation, context, gpu_mesh.lods[0].parts[0], blend, {},
                                TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                                SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                                MeshDrawProfile::MeshWeighted, area);
                MeshDraw wanted(presentation, context, gpu_mesh.lods[0].parts[0], expected_blend,
                                normal_views, TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                                SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                                MeshDrawProfile::MeshWeighted, area);
                equal_pixels(render(actual, selected_span, nullptr, true),
                             render(wanted, {}, nullptr, true),
                             "Projected normal/roughness bypassed the weighted lighting helper");
            }
            check(full[16 * 32 + 16] != baseline[16 * 32 + 16],
                  "Surface channel oracle is insensitive to normal/roughness");
            projection.values.roughness_influence = 0;
            for (unsigned axis = 0; axis < 2; ++axis) {
                auto& uv = channels.textures[decal_normal_channel].uv;
                uv[axis][axis] = -1;
                const auto mirrored_map = test_bloom::texture(
                    device, 1, 1, {{axis == 0 ? .25f : .75f, axis == 1 ? .375f : .625f, .875f, 1}});
                auto mirrored_values = expected_values;
                mirrored_values.parameters["roughnessFactor"] =
                    receiver.parameters.at("roughnessFactor");
                MeshDraw::Textures mirrored_views;
                mirrored_views["normalTexture"] =
                    mirrored_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                auto mirrored = prepare(mirrored_values, mirrored_views);
                equal_pixels(render(*draw, selected_span), render(*mirrored),
                             "Projected normal lost signed UV orientation");
                uv[axis][axis] = 1;
            }
            auto sloped_values = expected_values;
            sloped_values.parameters["roughnessFactor"] = receiver.parameters.at("roughnessFactor");
            auto sloped = prepare(sloped_values, normal_views);
            selected.textures[decal_normal_channel].view =
                flat_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            equal_pixels(render(*sloped, selected_span), render(*sloped),
                         "Flat projection flattened the receiver's existing mapped normal");
            selected.textures[decal_normal_channel].view = normal_views.at("normalTexture");
            const auto normal_uv = channels.textures[decal_normal_channel].uv;
            channels.textures[decal_normal_channel].uv = {};
            equal_pixels(render(*draw, selected_span), baseline,
                         "Degenerate projected UV gradients changed the receiver normal");
            channels.textures[decal_normal_channel].uv = normal_uv;
            projection.values.normal_influence = projection.values.roughness_influence = 0;
            equal_pixels(render(*draw, selected_span), baseline,
                         "Zero channel influence changed the ordinary receiver");
            projection.values.normal_influence = 1;
            selected.textures[decal_normal_channel].view =
                flat_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            equal_pixels(render(*draw, selected_span), baseline,
                         "Flat projected normal changed the receiver normal");
            selected.textures[decal_normal_channel].view = normal_views.at("normalTexture");
            channels.normal_scale = 0;
            equal_pixels(render(*draw, selected_span), baseline,
                         "Zero projected normal scale changed the receiver normal");
            channels.normal_scale = .75f;
            selected.channels = 1u << decal_color_channel;
            equal_pixels(render(*draw, selected_span), baseline,
                         "Unadmitted channel leaked into the last-good selection");
            selected.channels = decal_all_channels;
            projection.values.opacity = 0;
            equal_pixels(render(*draw, selected_span), baseline,
                         "Zero coverage changed projected surface channels");
            projection.values.opacity = 1;
            projection.values.normal_influence = 0;
            projection.values.roughness_influence = .5f;
            expected_values = receiver;
            expected_values.parameters["roughnessFactor"].value[0] = (.7f + .125f) / 2;
            expected = prepare(expected_values);
            equal_pixels(render(*draw, selected_span), render(*expected),
                         "Partial projected roughness bypassed the native perceptual input");
            // SG source alpha stores glossiness; its inverse becomes roughness.
            channels.roughness_component = 3;
            channels.roughness_scale = -.5f;
            channels.roughness_bias = 1;
            projection.values.roughness_influence = 1;
            expected_values.parameters["roughnessFactor"].value[0] = .75f;
            expected = prepare(expected_values);
            equal_pixels(render(*draw, selected_span), render(*expected),
                         "Projected SG glossiness was not converted to roughness");
            auto sg = receiver;
            sg.model = "forge.gltf.specular-glossiness.v1";
            sg.parameters.clear();
            sg.parameters["baseColorFactor"] = receiver.parameters.at("baseColorFactor");
            sg.parameters["glossinessFactor"] = {MaterialParameterType::Scalar, {.3f}};
            auto sg_draw = prepare(sg);
            sg.parameters["glossinessFactor"].value[0] = .25f;
            auto sg_expected = prepare(sg);
            equal_pixels(render(*sg_draw, selected_span), render(*sg_expected),
                         "Projected roughness did not reach the SG receiver BRDF");
            projection.values.normal_influence = projection.values.roughness_influence = 0;
            sg.parameters["glossinessFactor"].value[0] = .1f;
            auto sg_unchanged = prepare(sg);
            equal_pixels(render(*sg_unchanged, selected_span), render(*sg_unchanged),
                         "Zero channel influence changed the SG receiver");
            projection.values.roughness_influence = 1;
            std::array<MeshDraw::Decal, 8> repeated;
            repeated.fill(selected);
            check(draw->accepts_decals(repeated),
                  "Shared channel maps reduced the eight-projector capacity");
            std::array<DecalMaterial, 8> distinct;
            for (unsigned i = 0; i < repeated.size(); ++i) {
                distinct[i] = channels;
                distinct[i].textures[decal_roughness_channel].slot.sampler.lod_bias = float(i);
                repeated[i].material = &distinct[i];
            }
            check(draw->accepts_decals(repeated), "Eight distinct map/sampler slots were rejected");
            projection.values.normal_influence = 1;
            check(!draw->accepts_decals(repeated),
                  "Nine unique channel slots silently truncated their complete set");
            equal_pixels(render(*draw), baseline,
                         "Channel admission checks changed retained ordinary bindings");
            std::cout << "Projected normal/roughness native MR/SG/defaults/coverage/last-good "
                         "mask/shared slots passed\n";
        }
        if (channels_only) {
            context->WaitForIdle();
            return;
        }
        for (const auto kind : {LightKind::Rectangle, LightKind::Sphere}) {
            Light emitter;
            emitter.kind = unsigned(kind);
            emitter.intensity = 10;
            emitter.emitter_width = 2;
            emitter.emitter_height = 1;
            auto emitter_world = camera_world;
            emitter_world.m[11] = 1;
            illumination = light_view(emitter, emitter_world);
            auto reference_values = receiver;
            for (unsigned c = 0; c < 3; ++c)
                reference_values.parameters["baseColorFactor"].value[c] = material.color[c];
            MeshDraw projected(presentation, context, gpu_mesh.lods[0].parts[0], receiver, {},
                               TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                               SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                               MeshDrawProfile::Mesh, true);
            MeshDraw expected(presentation, context, gpu_mesh.lods[0].parts[0], reference_values,
                              {}, TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                              SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                              MeshDrawProfile::Mesh, true);
            const auto painted = render(projected, selected);
            check(painted[16 * 32 + 16][0] > .01f, "Decal area-light receiver is unlit");
            equal_pixels(painted, render(expected),
                         "Area lighting changed projected albedo intent");
        }
        illumination = light_view(light, {});
        const auto colored = [&] {
            auto value = receiver;
            for (unsigned c = 0; c < 3; ++c)
                value.parameters["baseColorFactor"].value[c] = material.color[c];
            return value;
        }();
        for (bool area : {false, true})
            for (const auto profile :
                 {MeshDrawProfile::Mesh, MeshDrawProfile::MeshTemporal,
                  MeshDrawProfile::MeshIndirect, MeshDrawProfile::MeshTemporalIndirect,
                  MeshDrawProfile::MeshReflection, MeshDrawProfile::MeshTemporalReflection,
                  MeshDrawProfile::MeshNormal, MeshDrawProfile::MeshTemporalNormal}) {
                const bool temporal = temporal_mesh_profile(profile),
                           indirect = indirect_mesh_profile(profile),
                           reflection = reflection_mesh_profile(profile),
                           normal = normal_mesh_profile(profile);
                const unsigned targets = 1 + unsigned(temporal) + unsigned(indirect) +
                                         (reflection ? 3u
                                          : normal   ? 1u
                                                     : 0u);
                std::vector<RefCntAutoPtr<ITexture>> outputs(targets);
                std::vector<ITextureView*> views;
                for (unsigned i = 0; i < targets; ++i) {
                    TextureDesc desc;
                    desc.Name = "FORGE projected material surface output";
                    desc.Type = RESOURCE_DIM_TEX_2D;
                    desc.Width = desc.Height = 32;
                    desc.Format =
                        temporal && i == 1 ? TEX_FORMAT_RGBA32_FLOAT : TEX_FORMAT_RGBA16_FLOAT;
                    desc.BindFlags = BIND_RENDER_TARGET;
                    device->CreateTexture(desc, nullptr, &outputs[i]);
                    check(bool(outputs[i]), "Projected material surface target unavailable");
                    views.push_back(outputs[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                }
                MeshDraw projected(presentation, context, gpu_mesh.lods[0].parts[0], receiver, {},
                                   TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                                   SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT,
                                   nullptr, profile, area);
                MeshDraw expected(presentation, context, gpu_mesh.lods[0].parts[0], colored, {},
                                  TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                                  SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT,
                                  nullptr, profile, area);
                const auto surface = [&](MeshDraw& prepared,
                                         std::span<const MeshDraw::Decal> selected_decals) {
                    context->SetRenderTargets(targets, views.data(), dsv,
                                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    const float clear[]{0, 0, 0, 0};
                    for (auto* target : views)
                        context->ClearRenderTarget(target, clear,
                                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    const Viewport viewport{0, 0, 32, 32, 0, 1};
                    context->SetViewports(1, &viewport, 32, 32);
                    MeshDraw::Motion motion;
                    prepared.draw(context, world, view, std::span(&illumination, 1), nullptr,
                                  nullptr, nullptr, {}, nullptr, {}, nullptr, {}, {}, {}, nullptr,
                                  nullptr, temporal ? &motion : nullptr, selected_decals);
                    std::vector<Pixels> pixels;
                    for (const auto& output : outputs)
                        pixels.push_back(test_bloom::read(device, context, output));
                    return pixels;
                };
                const auto actual = surface(projected, selected), wanted = surface(expected, {});
                for (unsigned i = 0; i < targets; ++i)
                    equal_pixels(
                        actual[i], wanted[i],
                        "Projected PBR output differs from the equivalent material surface");
            }
        const auto graph = compile_material_graph(create_material_graph());
        for (unsigned value = unsigned(ShaderEntryRole::SurfaceColor);
             value <= unsigned(ShaderEntryRole::SurfaceAreaWeightedColor); ++value) {
            const auto role = ShaderEntryRole(value);
            const bool temporal = temporal_surface_role(role),
                       indirect = indirect_surface_role(role),
                       reflection = reflection_surface_role(role),
                       normal = normal_surface_role(role),
                       depth_only = role == ShaderEntryRole::SurfaceDepth;
            auto source =
                material_graph_shader_wrapper(graph.surface, "graph.hlsli", depth_only, indirect,
                                              reflection, normal, area_surface_role(role), true);
            if (weighted_surface_role(role))
                source = weighted_color_wrapper(std::move(source), true);
            else if (indirect)
                source = indirect_surface_wrapper(std::move(source), temporal, reflection, normal);
            else if (temporal)
                source = temporal_surface_wrapper(std::move(source));
            const auto insert = [&](const std::string& old, const std::string& replacement) {
                const auto at = source.find(old);
                check(at != std::string::npos, "Projected graph lost its declared source/header");
                source.replace(at, old.size(), replacement);
            };
            if (temporal)
                insert("#include \"engine/forge.surface.temporal.hlsli\"",
                       surface_shader_header(graph.surface, false, true));
            insert("#include \"engine/forge.surface.hlsli\"", surface_shader_header(graph.surface));
            insert("#include \"graph.hlsli\"", graph.source);
            if (area_surface_role(role))
                insert(std::string(surface_shader_entry(ordinary_surface_role(role))) + "(",
                       std::string(surface_shader_entry(role)) + "(");
            const auto pixel = compile_pixel(source, surface_shader_entry(role));
            bool declares_projection = false;
            for (unsigned i = 0; i < pixel->GetResourceCount(); ++i) {
                ShaderResourceDesc resource;
                pixel->GetResourceDesc(i, resource);
                declares_projection =
                    declares_projection || std::string_view(resource.Name) == "ForgeDecals";
            }
            check(declares_projection != depth_only,
                  "Graph color/depth projection declarations differ from their role");
        }
        std::cout
            << "Projected albedo matches all eight Mesh outputs with/without area layout; all "
               "nineteen physical graph sources compile\n";
        const std::pair<const char*, TextureSemantic> roles[]{
            {"baseColorTexture", TextureSemantic::Color},
            {"metallicRoughnessTexture", TextureSemantic::Data},
            {"normalTexture", TextureSemantic::Normal},
            {"occlusionTexture", TextureSemantic::Data},
            {"emissiveTexture", TextureSemantic::Color},
            {"clearcoatTexture", TextureSemantic::Data},
            {"clearcoatRoughnessTexture", TextureSemantic::Data},
            {"clearcoatNormalTexture", TextureSemantic::Normal},
            {"specularTexture", TextureSemantic::Data},
            {"specularColorTexture", TextureSemantic::Color},
            {"sheenColorTexture", TextureSemantic::Color},
            {"sheenRoughnessTexture", TextureSemantic::Data},
            {"anisotropyTexture", TextureSemantic::Data},
            {"iridescenceTexture", TextureSemantic::Data},
            {"iridescenceThicknessTexture", TextureSemantic::Data},
            {"transmissionTexture", TextureSemantic::Data},
            {"thicknessTexture", TextureSemantic::Data}};
        const auto white = test_bloom::texture(device, 1, 1, {{1, 1, 1, 1}});
        const auto sampling_policy = emulated_resource_arrays(device->GetDeviceInfo())
                                         ? MaterialSamplerBinding::NamedElements
                                         : MaterialSamplerBinding::Array;
        for (const auto& [count, expected_capacity] :
             {std::pair{6u, 8u}, {7u, 7u}, {13u, 1u}, {14u, 0u}, {17u, 0u}}) {
            auto many = receiver;
            MeshDraw::Textures views;
            for (unsigned i = 0; i < count; ++i) {
                MaterialTextureSlot slot;
                slot.semantic = roles[i].second;
                slot.sampler.lod_bias = float(i) / 16;
                many.textures.emplace(roles[i].first, slot);
                views.emplace(roles[i].first, white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
            }
            const auto profile = prepare_pbr_material(many);
            const auto fetch = mesh_vertex_fetch(gpu_mesh.lods[0].parts[0], profile);
            const auto generated =
                mesh_draw_shader(fetch, profile, false, sampling_policy, nullptr, {}, false, {},
                                 false, false, false, false, true);
            check(generated.material.samplers.size() == count &&
                      generated.decal_capacity == expected_capacity,
                  "Distinct material sampler intent changed the receiver profile");
            const auto pixel = compile_pixel(generated.pixel);
            unsigned reflected_samplers = 0, reflected_decal_samplers = 0;
            bool projected_buffer = false;
            for (unsigned i = 0; i < pixel->GetResourceCount(); ++i) {
                ShaderResourceDesc resource;
                pixel->GetResourceDesc(i, resource);
                if (resource.Type == SHADER_RESOURCE_TYPE_SAMPLER) {
                    reflected_samplers += resource.ArraySize;
                    if (std::string_view(resource.Name).starts_with("g_ForgeDecalSamplers"))
                        reflected_decal_samplers += resource.ArraySize;
                }
                if (std::string_view(resource.Name) == "ForgeDecals") {
                    const auto* buffer = pixel->GetConstantBufferDesc(i);
                    check(buffer && buffer->Size == (4 + expected_capacity * 13) * 16,
                          "Actual combined shader changed the projector packet layout");
                    projected_buffer = true;
                }
            }
            check(reflected_decal_samplers == expected_capacity &&
                      projected_buffer == (expected_capacity != 0) &&
                      (!expected_capacity || reflected_samplers <= 16),
                  "Actual combined shader exceeds its bounded sampler/resource profile");
            MeshDraw prepared(presentation, context, gpu_mesh.lods[0].parts[0], many, views,
                              TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT);
            check(prepared.decal_capacity() == expected_capacity,
                  "Native draw selected a different receiver binding profile");
            auto same = many;
            for (auto& [name, slot] : same.textures) {
                (void)name;
                slot.sampler.lod_bias = 0;
            }
            MeshDraw deduplicated(presentation, context, gpu_mesh.lods[0].parts[0], same, views,
                                  TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT);
            equal_pixels(render(prepared), render(deduplicated),
                         "Zero projection changes an otherwise equivalent complex material");
            if (expected_capacity) {
                (void)render(prepared, selected);
                equal_pixels(render(prepared), render(deduplicated),
                             "Complex material failed to restore after color projection");
            }
            std::cout << "Material samplers=" << count << ", decal slots=" << expected_capacity
                      << ", actual pixel samplers=" << reflected_samplers << '\n';
        }
    }
    std::filesystem::create_directories(root);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> receiver_id{AssetId::generate()};
    host->preview_material(receiver_id, {receiver, {}, {}});
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    Camera scene_camera;
    scene_camera.background_r = scene_camera.background_g = scene_camera.background_b = 0;
    scene.cameras.push_back({EntityId::generate(), scene_camera, {}});
    scene.lights.push_back({EntityId::generate(), illumination});
    for (unsigned i = 0; i < 2; ++i) {
        RenderMesh cube;
        cube.entity = EntityId::generate();
        cube.renderer.mesh = engine_primitive(0);
        cube.renderer.materials = {{"surface", receiver_id}};
        cube.renderer.cast_shadows = false;
        cube.renderer.layers = i + 1;
        cube.world.m[3] = i ? .7 : -.7;
        cube.world.m[11] = 3;
        scene.meshes.push_back(cube);
    }
    const auto cameras = prepare_game_cameras(scene, 64, 32);
    check(cameras.diagnostics.empty() && cameras.cameras.size() == 1,
          "Decal scene camera was rejected");
    FrameRenderer frame(presentation);
    frame.resources(host);
    const auto scene_render = [&](bool warnings = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        ITextureView* output = nullptr;
        do {
            output = frame.render(context, scene, cameras.cameras, 64, 32, 0, nullptr, 0, nullptr,
                                  FrameRenderer::Output::LinearHdr);
            check(std::chrono::steady_clock::now() < deadline, "Decal scene admission timed out");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!warnings && !frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(output && !frame.omitted_diagnostics(),
              "Decal scene output or diagnostics unavailable");
        return test_bloom::read(device, context, output->GetTexture());
    };
    const auto ordinary_scene = scene_render();
    projector.material = engine_material();
    projector.tint_r = .8f;
    projector.tint_g = .1f;
    projector.tint_b = .2f;
    projector.layers = 1;
    AffineTransform scene_projection;
    scene_projection.m[11] = 3;
    const auto decal_id = EntityId::generate();
    scene.decals = {{decal_id, decal_view(projector, scene_projection)}};
    const auto painted_scene = scene_render();
    scene.decals.clear();
    scene.meshes[0].renderer.material_parameters = {
        {"surface", "baseColorFactor", {MaterialParameterType::LinearColor4, {.8f, .1f, .2f, 1}}}};
    const auto manual_scene = scene_render();
    equal_pixels(painted_scene, manual_scene,
                 "Scene decal resources, receiver layers or instance batching changed PBR intent");
    scene.meshes[0].renderer.material_parameters.clear();
    equal_pixels(scene_render(), ordinary_scene, "Removing a scene decal retained native bindings");
    scene.decals = {{decal_id, decal_view(projector, scene_projection)}};
    equal_pixels(scene_render(), painted_scene,
                 "Re-adding a scene decal failed resource admission");
    {
        // An isolated preview host intentionally admits one unsaved Material.
        // Keep receivers on built-in assets while previewing the projector source.
        const std::vector<MaterialParameterOverride> receiver_parameters{
            {"surface", "baseColorFactor", receiver.parameters.at("baseColorFactor")},
            {"surface", "metallicFactor", receiver.parameters.at("metallicFactor")},
            {"surface", "roughnessFactor", receiver.parameters.at("roughnessFactor")}};
        for (auto& mesh : scene.meshes) {
            mesh.renderer.materials = {{"surface", engine_material()}};
            mesh.renderer.material_parameters = receiver_parameters;
        }
        MaterialResourceData source;
        source.values.model = receiver.model;
        source.values.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.25f}};
        source.values.parameters["normalScale"] = {MaterialParameterType::Scalar, {0}};
        MaterialTextureSlot normal, rough;
        normal.semantic = TextureSemantic::Normal;
        rough.semantic = TextureSemantic::Data;
        source.values.textures = {{"normalTexture", normal}, {"metallicRoughnessTexture", rough}};
        source.textures = {
            {"normalTexture", engine_texture(EngineTexture::FlatNormal, TextureDimension::D2)},
            {"metallicRoughnessTexture",
             engine_texture(EngineTexture::White, TextureDimension::D2)}};
        const AssetRef<MaterialAsset> source_id{AssetId::generate()};
        host->preview_material(source_id, source);
        auto projected = projector;
        projected.material = source_id;
        projected.normal_influence = projected.roughness_influence = 1;
        scene.decals = {{decal_id, decal_view(projected, scene_projection)}};
        const auto channel_frame = scene_render();
        scene.decals.clear();
        scene.meshes[0].renderer.material_parameters = {
            {"surface",
             "baseColorFactor",
             {MaterialParameterType::LinearColor4, {.8f, .1f, .2f, 1}}},
            {"surface", "roughnessFactor", {MaterialParameterType::Scalar, {.25f}}},
            {"surface", "metallicFactor", {MaterialParameterType::Scalar, {0}}}};
        equal_pixels(channel_frame, scene_render(),
                     "Scene normal/data channel resources differ from ordinary material intent");
        scene.meshes[0].renderer.material_parameters = receiver_parameters;
        projected.color_influence = projected.normal_influence = projected.roughness_influence = 0;
        scene.decals = {{decal_id, decal_view(projected, scene_projection)}};
        equal_pixels(scene_render(), ordinary_scene,
                     "Scene zero-influence channels retained a previous GPU selection");
        const AssetRef<MaterialAsset> broken_id{AssetId::generate()};
        source.textures["normalTexture"] = {AssetId::generate()};
        host->preview_material(broken_id, source);
        projected.material = broken_id;
        projected.color_influence = projected.roughness_influence = 1;
        scene.decals = {{decal_id, decal_view(projected, scene_projection)}};
        equal_pixels(scene_render(), channel_frame,
                     "Disabled normal channel requested its unavailable texture");
        scene.decals[0].projector.values.normal_influence = 1;
        equal_pixels(scene_render(true), channel_frame,
                     "Failed channel addition replaced the complete last-good set");
        check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                          [](const auto& d) { return d.category == "render.decal.resource"; }),
              "Failed normal channel has no contextual resource diagnostic");
        source.textures["normalTexture"] =
            engine_texture(EngineTexture::FlatNormal, TextureDimension::D2);
        host->preview_material(broken_id, source);
        equal_pixels(scene_render(), channel_frame,
                     "Repaired channel resource failed complete candidate recovery");
        scene.decals[0].projector.values.roughness_influence = 0;
        equal_pixels(scene_render(), painted_scene,
                     "Disabling a projected channel retained its prior effect");
        scene.decals.clear();
        equal_pixels(scene_render(), ordinary_scene,
                     "Removing multichannel projection retained old bindings");
        for (auto& mesh : scene.meshes) {
            mesh.renderer.materials = {{"surface", receiver_id}};
            mesh.renderer.material_parameters.clear();
        }
        host->preview_material(receiver_id, {receiver, {}, {}});
        std::cout << "Scene projected normal/data semantics, disabled dependencies, complete "
                     "last-good mask and repair passed\n";
    }
    auto green = projector;
    green.tint_r = .1f;
    green.tint_g = .8f;
    green.priority = 2;
    const auto green_id = EntityId::generate();
    scene.decals = {{green_id, decal_view(green, scene_projection)}};
    const auto green_scene = scene_render();
    scene.decals.push_back({decal_id, decal_view(projector, scene_projection)});
    equal_pixels(scene_render(), green_scene, "Decal priority follows source enumeration");
    scene.decals[1].projector.values.priority = 3;
    equal_pixels(scene_render(), painted_scene, "Higher decal priority did not paint last");
    scene.decals[0].projector.values.priority = 0;
    scene.decals[1].projector.values.priority = 0;
    const auto tied = scene_render();
    std::reverse(scene.decals.begin(), scene.decals.end());
    equal_pixels(scene_render(), tied, "Equal decal priority changed with enumeration order");
    equal_pixels(tied, green_id < decal_id ? painted_scene : green_scene,
                 "Equal decal priority does not use stable EntityId order");
    scene.decals = {{decal_id, decal_view(projector, scene_projection)}};
    scene.decals[0].projector.values.material = {AssetId::generate()};
    equal_pixels(scene_render(true), painted_scene,
                 "Failed decal material replaced the last admitted projection");
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) { return d.category == "render.decal.resource"; }),
          "Failed decal source lacks its resource diagnostic");
    scene.decals.clear();
    for (unsigned i = 0; i < 128; ++i)
        scene.decals.push_back({EntityId::generate(), decal_view(projector, scene_projection)});
    equal_pixels(scene_render(), painted_scene,
                 "Scene storage projector list or existing instance bounds changed PBR paint");
    {
        scene.meshes[1].renderer.layers = 1;
        MeshSceneRenderer batched(host, TEX_FORMAT_RGBA32_FLOAT);
        const auto render_batch = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            do {
                host->pump();
                batched.update(scene);
                check(batched.diagnostics().empty(),
                      "Projector instance admission reported an error");
                check(std::chrono::steady_clock::now() < deadline,
                      "Projector instance admission timed out");
                if (batched.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (batched.pending());
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[]{0, 0, 0, 0};
            context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const Viewport viewport{0, 0, 32, 32, 0, 1};
            context->SetViewports(1, &viewport, 32, 32);
            batched.draw(scene, camera_view(scene_camera, {}, 32, 32), UINT32_MAX, color, dsv);
            check(batched.diagnostics().empty(), "Projector instance draw reported an error");
            const auto stats = batched.draw_stats();
            check(stats.calls == 1 && stats.instances == 2 && stats.batched_calls == 1,
                  "Equal projector selections did not preserve existing instance batching");
            return test_bloom::read(device, context, color);
        };
        const auto large_batch = render_batch();
        scene.decals.resize(1);
        equal_pixels(
            large_batch, render_batch(),
            "Storage projection across unioned instance bounds differs from small projection");
        scene.meshes[1].renderer.layers = 2;
    }
    scene.decals.clear();
    for (unsigned i = 0; i < 129; ++i)
        scene.decals.push_back({EntityId::generate(), decal_view(projector, scene_projection)});
    equal_pixels(scene_render(true), ordinary_scene,
                 "Over-capacity decal scene silently truncated its projector selection");
    scene.decals.clear();
    equal_pixels(scene_render(), ordinary_scene,
                 "Scene projection rejection damaged ordinary rendering");
    auto tiny_projector = projector;
    tiny_projector.width = tiny_projector.height = tiny_projector.depth = 1;
    AffineTransform tiny_world;
    tiny_world.m[0] = tiny_world.m[5] = tiny_world.m[10] = 5e-39;
    tiny_world.m[3] = -.7;
    tiny_world.m[11] = 3;
    scene.decals = {{decal_id, decal_view(tiny_projector, tiny_world)}};
    equal_pixels(scene_render(true), ordinary_scene,
                 "Unrepresentable projector suppressed ordinary geometry");
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) { return d.category == "render.decal.view"; }),
          "Unrepresentable projector lacks a view admission warning");
    // The particle owner supplies instances to the same receiver selection path.
    // Two stationary one-particle emitters must match the ordinary cube oracles.
    ParticleEffectData effect;
    effect.mesh = engine_primitive(0);
    effect.material = receiver_id;
    effect.emission = ParticleEmission::Burst;
    effect.capacity = effect.burst_count = 1;
    effect.acceleration = {0, 0, 0};
    effect.speed_min = effect.speed_max = 0;
    effect.size_start = effect.size_end = 1;
    effect.color_start = effect.color_end = {1, 1, 1};
    const AssetRef<ParticleEffectAsset> effect_id{AssetId::generate()};
    host->preview_particle_effect(effect_id, effect);
    for (const auto& mesh : scene.meshes) {
        RenderParticleEmitter emitter;
        emitter.entity = mesh.entity;
        emitter.world = mesh.world;
        emitter.emitter.effect = effect_id;
        emitter.emitter.layers = mesh.renderer.layers;
        scene.particles.push_back(emitter);
    }
    scene.meshes.clear();
    scene.decals = {{decal_id, decal_view(projector, scene_projection)}};
    equal_pixels(scene_render(), painted_scene,
                 "Mesh particle projector selection differs from ordinary Mesh receivers");
    scene.decals.clear();
    equal_pixels(scene_render(), ordinary_scene,
                 "Removing particle projection failed to restore ordinary materials");
    context->WaitForIdle();
    if (scene_only) {
        std::cout << "Decal scene selection, particles, resources, layers, priority, recovery and "
                     "precision "
                     "passed\n";
        return;
    }
    std::cout << "Decal PBR receiver, large origin, source alpha, opacity, clipping, angular "
                 "rejection, lighting and recovery passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || (argc == 3 && (std::string_view(argv[2]) == "--scene-only" ||
                                          std::string_view(argv[2]) == "--channels-only" ||
                                          std::string_view(argv[2]) == "--clusters-only")),
              "Need decal scratch directory and optional --scene-only / --channels-only / "
              "--clusters-only");
        execute(argv[1], argc == 3 && std::string_view(argv[2]) == "--scene-only",
                argc == 3 && std::string_view(argv[2]) == "--channels-only",
                argc == 3 && std::string_view(argv[2]) == "--clusters-only");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
