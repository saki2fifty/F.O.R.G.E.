#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
namespace forge::test_reflection_material {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    constexpr unsigned width = 33, height = 25;
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-.9f, -.9f, 2, -.9f, .9f, 2, .9f, .9f, 2, .9f, -.9f, 2}},
        {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
        {"TANGENT", 4, std::vector<float>{1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1}},
        {"TEXCOORD_0", 2, std::vector<float>{0, 1, 0, 0, 1, 0, 1, 1}}};
    part.bounds = mesh_bounds(part);
    MeshData source;
    source.lods = {{1, {part}}};
    const auto mesh = upload_mesh(device, source);
    const std::vector<test_bloom::Pixel> blank(width * height);
    std::array<RefCntAutoPtr<ITexture>, 5> targets;
    for (auto& target : targets)
        target = test_bloom::texture(device, width, height, blank);
    auto normal =
        test_bloom::texture(device, 2, 2, std::vector<test_bloom::Pixel>(4, {.8f, .5f, .9f, 1}));
    TextureDesc d;
    d.Name = "FORGE actual reflection material targets";
    d.Type = RESOURCE_DIM_TEX_2D;
    d.Width = width;
    d.Height = height;
    d.Format = TEX_FORMAT_D32_FLOAT;
    d.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> depth, motion;
    device->CreateTexture(d, nullptr, &depth);
    d.Format = TEX_FORMAT_RGBA32_FLOAT;
    d.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(d, nullptr, &motion);
    check(depth && motion, "Reflection material target allocation failed");
    const float radiance[]{.25f, .5f, .75f, 1};
    auto cube = presentation.pbr(context).CreateIrradianceCube(
        context, "FORGE reflection material reference", TEX_FORMAT_RGBA32_FLOAT, 4, radiance);
    check(bool(cube), "Reflection material environment allocation failed");
    GpuEnvironment maps;
    maps.source = maps.diffuse = maps.specular = maps.sheen = cube;
    Camera settings;
    settings.near_plane = .1;
    settings.far_plane = 100;
    const auto view = camera_view(settings, {}, width, height);
    Light light;
    light.intensity = 3;
    const std::array lights{light_view(light, {})};
    SceneDistanceFog fog{true, {.2f, .3f, .5f}, 0, 4};
    unsigned cases = 0;
    for (bool mapped : {false, true})
        for (unsigned layers = 0; layers < 4; ++layers)
            for (float metallic : {0.f, 1.f})
                for (float roughness : {.05f, .7f}) {
                    MaterialData material;
                    material.model = "forge.gltf.metallic-roughness.v1";
                    material.double_sided = true;
                    material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                              {.7f, .3f, .1f, 1}};
                    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar,
                                                             {metallic}};
                    material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar,
                                                              {roughness}};
                    material.parameters["emissiveFactor"] = {MaterialParameterType::LinearColor3,
                                                             {.15f, .1f, .05f}};
                    material.parameters["emissiveStrength"] = {MaterialParameterType::Scalar, {2}};
                    if (layers >= 1) {
                        material.parameters["clearcoatFactor"] = {MaterialParameterType::Scalar,
                                                                  {.8f}};
                        material.parameters["clearcoatRoughnessFactor"] = {
                            MaterialParameterType::Scalar, {.3f}};
                    }
                    if (layers >= 2) {
                        material.parameters["sheenColorFactor"] = {
                            MaterialParameterType::LinearColor3, {.5f, .2f, .1f}};
                        material.parameters["sheenRoughnessFactor"] = {
                            MaterialParameterType::Scalar, {.4f}};
                    }
                    if (layers == 3) {
                        material.parameters["anisotropyStrength"] = {MaterialParameterType::Scalar,
                                                                     {.8f}};
                        material.parameters["anisotropyRotation"] = {MaterialParameterType::Scalar,
                                                                     {.2f}};
                    }
                    MeshDraw::Textures textures;
                    if (mapped) {
                        material.textures["normalTexture"].semantic = TextureSemantic::Normal;
                        material.textures["normalTexture"].uv_set = 0;
                        material.parameters["normalScale"] = {MaterialParameterType::Scalar,
                                                              {.75f}};
                        textures["normalTexture"] =
                            normal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    }
                    std::array<std::unique_ptr<MeshDraw>, 3> draws;
                    for (unsigned mode = 0; mode < 3; ++mode) {
                        const auto profile = mode == 0   ? MeshDrawProfile::MeshIndirect
                                             : mode == 1 ? MeshDrawProfile::MeshReflection
                                                         : MeshDrawProfile::MeshTemporalReflection;
                        draws[mode] = std::make_unique<MeshDraw>(
                            presentation, context, mesh.lods[0].parts[0], material, textures,
                            TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                            SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                            profile);
                        draws[mode]->bind_environment(&maps);
                    }
                    for (bool mist : {false, true})
                        for (float intensity : {0.f, 1.f}) {
                            auto current_view = view;
                            if (intensity == 0) {
                                current_view.right = {0, 1, 0};
                                current_view.up = {-1, 0, 0};
                            }
                            std::array<std::vector<test_bloom::Pixel>, 5> baseline;
                            for (unsigned mode = 0; mode < 3; ++mode) {
                                presentation.begin_frame();
                                std::vector<ITextureView*> views{
                                    targets[0]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
                                if (mode == 2)
                                    views.push_back(
                                        motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                                views.push_back(
                                    targets[1]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                                if (mode)
                                    for (unsigned i = 2; i < 5; ++i)
                                        views.push_back(
                                            targets[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                                const float zero[4]{};
                                for (auto* target : views)
                                    context->ClearRenderTarget(
                                        target, zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
                                context->ClearDepthStencil(
                                    dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                context->SetRenderTargets(
                                    Uint32(views.size()), views.data(), dsv,
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                Diligent::Viewport viewport{0, 0, float(width), float(height),
                                                            0, 1};
                                context->SetViewports(1, &viewport, width, height);
                                EnvironmentLighting environment{&maps, intensity, 0};
                                MeshDraw::Motion completed;
                                AffineTransform world;
                                completed.previous_view = &current_view;
                                completed.previous_world = &world;
                                completed.valid = true;
                                draws[mode]->draw(
                                    context, world, current_view, lights, &environment, nullptr,
                                    nullptr, {}, nullptr, {}, nullptr, {}, {}, {}, nullptr,
                                    mist ? &fog : nullptr, mode == 2 ? &completed : nullptr);
                                presentation.end_frame(context);
                                std::array<std::vector<test_bloom::Pixel>, 5> data;
                                for (unsigned i = 0; i < (mode ? 5u : 2u); ++i)
                                    data[i] = test_bloom::read(device, context, targets[i]);
                                if (mode == 0)
                                    baseline = data;
                                else
                                    for (unsigned y = height / 2 - 2; y < height / 2 + 2; ++y)
                                        for (unsigned x = width / 2 - 2; x < width / 2 + 2; ++x) {
                                            const auto at = y * width + x;
                                            for (unsigned c = 0; c < 4; ++c) {
                                                check(std::abs(data[0][at][c] -
                                                               baseline[0][at][c]) < .003f &&
                                                          std::abs(data[1][at][c] -
                                                                   baseline[1][at][c]) < .003f,
                                                      "Reflection MRTs changed full color or total "
                                                      "indirect light");
                                                if (c < 3) {
                                                    const double expected =
                                                        data[3][at][c] * radiance[c] * intensity;
                                                    check(std::abs(data[4][at][c] - expected) <
                                                              .003 * std::max(1., expected),
                                                          "Reflection MRT unit response disagrees "
                                                          "with actual base specular");
                                                    check(data[1][at][c] + .003f >= data[4][at][c],
                                                          "Reflection base specular exceeds total "
                                                          "indirect");
                                                }
                                            }
                                            if (layers == 3) {
                                                for (unsigned target = 2; target < 5; ++target)
                                                    check(data[target][at] == test_bloom::Pixel{},
                                                          "Anisotropic reflection receiver failed "
                                                          "environment fallback");
                                                continue;
                                            }
                                            const auto& n = data[2][at];
                                            check(std::abs(std::hypot(n[0], n[1], n[2]) - 1) <
                                                          .001 &&
                                                      std::abs(n[3] - roughness) < .001,
                                                  "Reflection MRT lost real shaded normal or "
                                                  "roughness");
                                            const double nx =
                                                mapped ? .45 / std::hypot(.45, .8) : 0;
                                            const double nz =
                                                mapped ? -.8 / std::hypot(.45, .8) : -1;
                                            const std::array<double, 3> expected_normal =
                                                intensity == 0 ? std::array<double, 3>{0, -nx, nz}
                                                               : std::array<double, 3>{nx, 0, nz};
                                            for (unsigned c = 0; c < 3; ++c)
                                                check(std::abs(n[c] - expected_normal[c]) < .003,
                                                      "Reflection normal lost tangent mapping or "
                                                      "world-to-view axes");
                                            check(data[3][at][0] + data[3][at][1] + data[3][at][2] >
                                                      .00001,
                                                  "Reflection response depends on nonzero "
                                                  "environment intensity");
                                        }
                                context->FinishFrame();
                                device->ReleaseStaleResources();
                            }
                            ++cases;
                        }
                }
    context->WaitForIdle();
    std::cout << cases
              << " actual reflection Mesh material cases PASS: MRT5/6, mapped normals, roughness, "
                 "layers, anisotropic fallback, response, environment0/1, fog and ordinary "
                 "color/indirect preservation\n";
}
} // namespace forge::test_reflection_material
