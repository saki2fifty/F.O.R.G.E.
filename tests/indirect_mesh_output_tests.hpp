#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include <forge/engine_assets.hpp>

namespace forge::test_indirect_mesh {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    constexpr unsigned width = 65, height = 49;
    auto* device = presentation.device();
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-.9f, -.9f, 2, -.9f, .9f, 2, .9f, .9f, 2, .9f, -.9f, 2}},
        {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    auto gpu = upload_mesh(device, mesh);
    MaterialData material;
    material.model = "forge.gltf.metallic-roughness.v1";
    material.double_sided = true;
    material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                              {.7f, .3f, .1f, 1}};
    material.parameters["emissiveFactor"] = {MaterialParameterType::LinearColor3,
                                             {.15f, .1f, .05f, 0}};
    material.parameters["emissiveStrength"] = {MaterialParameterType::Scalar, {2, 0, 0, 0}};
    const auto blank = std::vector<test_bloom::Pixel>(width * height);
    auto color = test_bloom::texture(device, width, height, blank);
    auto indirect = test_bloom::texture(device, width, height, blank);
    TextureDesc desc;
    desc.Name = "FORGE indirect output fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> depth, motion;
    device->CreateTexture(desc, nullptr, &depth);
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &motion);
    check(depth && motion, "Indirect fixture targets unavailable");
    auto& pbr = presentation.pbr(context);
    const float environment_color[]{.25f, .5f, .75f, 1};
    auto cube = pbr.CreateIrradianceCube(context, "FORGE constant indirect fixture",
                                         TEX_FORMAT_RGBA32_FLOAT, 4, environment_color);
    check(bool(cube), "Indirect fixture environment allocation failed");
    GpuEnvironment maps;
    maps.source = maps.diffuse = maps.specular = maps.sheen = cube;
    EnvironmentLighting environment{&maps, 1, 0};
    Light light;
    light.intensity = 3;
    const std::array lights{light_view(light, {})};
    Camera camera;
    camera.near_plane = .1;
    camera.far_plane = 100;
    const auto view = camera_view(camera, {}, width, height);
    SceneDistanceFog fog{true, {.2f, .3f, .5f}, 0, 4};
    unsigned cases = 0;
    for (unsigned layers = 0; layers < 3; ++layers)
        for (bool temporal : {false, true}) {
            auto layered_material = material;
            if (layers >= 1) {
                layered_material.parameters["clearcoatFactor"] = {MaterialParameterType::Scalar,
                                                                  {.8f}};
                layered_material.parameters["clearcoatRoughnessFactor"] = {
                    MaterialParameterType::Scalar, {.3f}};
            }
            if (layers >= 2) {
                layered_material.parameters["sheenColorFactor"] = {
                    MaterialParameterType::LinearColor3, {.5f, .2f, .1f}};
                layered_material.parameters["sheenRoughnessFactor"] = {
                    MaterialParameterType::Scalar, {.4f}};
            }
            MeshDraw ordinary(presentation, context, gpu.lods[0].parts[0], layered_material, {},
                              TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                              SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                              temporal ? MeshDrawProfile::MeshTemporal : MeshDrawProfile::Mesh);
            MeshDraw split(presentation, context, gpu.lods[0].parts[0], layered_material, {},
                           TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                           SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                           temporal ? MeshDrawProfile::MeshTemporalIndirect
                                    : MeshDrawProfile::MeshIndirect);
            auto environment_material = layered_material;
            environment_material.parameters["emissiveFactor"].value = {0, 0, 0, 0};
            MeshDraw environment_only(presentation, context, gpu.lods[0].parts[0],
                                      environment_material, {}, TEX_FORMAT_RGBA16_FLOAT,
                                      TEX_FORMAT_D32_FLOAT);
            const auto render = [&](MeshDraw& draw, bool outputs, bool lit, bool env, bool mist) {
                presentation.begin_frame();
                const float zero[4]{};
                auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
                auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                auto* ambient = indirect->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                auto* velocity = motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                context->ClearRenderTarget(rtv, zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->ClearRenderTarget(ambient, zero,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->ClearRenderTarget(velocity, zero,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                // The ordinary reference has one target even in the surrounding temporal test.
                const bool moving = &draw != &environment_only && temporal;
                std::vector<ITextureView*> targets{rtv};
                if (moving)
                    targets.push_back(velocity);
                if (outputs)
                    targets.push_back(ambient);
                context->SetRenderTargets(Uint32(targets.size()), targets.data(), dsv,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                Diligent::Viewport viewport{0, 0, float(width), float(height), 0, 1};
                context->SetViewports(1, &viewport, width, height);
                draw.bind_environment(&maps);
                auto lighting = environment;
                if (!env)
                    lighting.intensity = 0;
                MeshDraw::Motion completed;
                completed.previous_view = &view;
                AffineTransform world;
                completed.previous_world = &world;
                completed.valid = true;
                draw.draw(context, world, view,
                          lit ? std::span(lights) : std::span<const LightView>{}, &lighting,
                          nullptr, nullptr, {}, nullptr, {}, nullptr, {}, {}, {}, nullptr,
                          mist ? &fog : nullptr, moving ? &completed : nullptr);
                presentation.end_frame(context);
                const auto a = test_bloom::read(device, context, color),
                           b = test_bloom::read(device, context, indirect);
                context->FinishFrame();
                device->ReleaseStaleResources();
                return std::pair{a, b};
            };
            const auto reference = render(environment_only, false, false, true, false).first;
            check(reference[(height / 2) * width + width / 2][0] > .001f,
                  "Indirect fixture has no environment radiance");
            for (bool mist : {false, true})
                for (bool env : {false, true}) {
                    const auto baseline = render(ordinary, false, true, env, mist).first;
                    const auto [actual, ambient] = render(split, true, true, env, mist);
                    for (unsigned y = height / 2 - 4; y < height / 2 + 4; ++y)
                        for (unsigned x = width / 2 - 4; x < width / 2 + 4; ++x) {
                            const auto i = y * width + x;
                            check(actual[i][3] == baseline[i][3],
                                  "Indirect Mesh output changed current alpha");
                            const double px = ((x + .5) * 2 / width - 1) * 2 / view.projection[0];
                            const double py = (1 - (y + .5) * 2 / height) * 2 / view.projection[5];
                            const double transmission =
                                mist ? 1 - std::clamp(std::hypot(px, py, 2.) / 4, 0., 1.) : 1;
                            for (unsigned c = 0; c < 3; ++c) {
                                check(std::abs(actual[i][c] - baseline[i][c]) < .003f,
                                      "Extra Mesh target changed full lighting");
                                const double expected = env ? reference[i][c] * transmission : 0;
                                check(std::abs(ambient[i][c] - expected) < .002,
                                      "Indirect Mesh target contains punctual light, emission or "
                                      "additive fog");
                            }
                        }
                    ++cases;
                }
        }
    context->WaitForIdle();
    std::cout << cases
              << " actual Mesh indirect MRT cases PASS: ordinary/temporal, isolated environment, "
                 "punctual/emission/fog/alpha preservation\n";
}
} // namespace forge::test_indirect_mesh
