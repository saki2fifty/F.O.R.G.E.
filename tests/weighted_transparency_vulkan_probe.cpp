#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "engine_render_resource.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw_shader.hpp"
#include "weighted_transparency_shader.hpp"
#include "weighted_transparency_target_tests.hpp"
#include <chrono>
#include <forge/material_graph.hpp>
#include <iostream>
#include <thread>
using namespace Diligent;
using namespace forge;
namespace {
using test_bloom::check;
using Pixels = std::vector<test_bloom::Pixel>;
void equal_pixels(const Pixels& a, const Pixels& b, double tolerance, const char* why) {
    check(a.size() == b.size(), "Transparency readback sizes differ");
    for (std::size_t i = 0; i < a.size(); ++i)
        for (unsigned c = 0; c < 4; ++c)
            if (!std::isfinite(a[i][c]) || std::abs(a[i][c] - b[i][c]) > tolerance)
                throw std::runtime_error(nlohmann::json{
                    {"reason", why},
                    {"pixel", i},
                    {"channel", c},
                    {"actual", a[i]},
                    {"expected", b[i]}}.dump());
}
void execute(const std::filesystem::path& root, bool scene_only) {
    std::filesystem::create_directories(root);
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
    create.Features.IndependentBlend = DEVICE_FEATURE_STATE_DISABLED;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    check(device && context, "Transparency native device unavailable");
    DiligentPresentation presentation(device);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialData values;
    values.model = "forge.gltf.unlit.v1";
    values.alpha = MaterialAlpha::Blend;
    values.depth_write = false;
    values.double_sided = true;
    host->preview_material(material, {values, {}, {}});
    const auto quad = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto slot = quad.materials.front().key;
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 2;
    camera.background_r = .02f;
    camera.background_g = .04f;
    camera.background_b = .08f;
    camera.background_a = .25f;
    constexpr unsigned width = 64, height = 32;
    auto view = camera_view(camera, {}, width, height);
    TextureDesc target;
    target.Name = "FORGE transparency test HDR";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = width;
    target.Height = height;
    target.MipLevels = 1;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(target, nullptr, &color);
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(target, nullptr, &depth);
    check(color && depth, "Transparency test attachments unavailable");
    MeshSceneRenderer renderer(host, TEX_FORMAT_RGBA32_FLOAT);
    const std::array<float, 4> background{.02f, .04f, .08f, .25f}, zero{};
    unsigned frames = 0;
    const auto render = [&](bool weighted, const std::array<float, 4>& clear,
                            bool warnings = false) {
        scene.settings.weighted_transparency = weighted;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline, "Transparency resources stalled");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, clear.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto area = view.viewport;
        Viewport viewport{
            float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        if (!warnings && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        check(!renderer.omitted_diagnostics(), "Transparency diagnostics were truncated");
        auto result = test_bloom::read(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        ++frames;
        return result;
    };
    std::array<EntityId, 3> ids{EntityId::generate(), EntityId::generate(), EntityId::generate()};
    std::sort(ids.begin(), ids.end());
    const std::array<std::array<float, 4>, 3> paint{
        {{.9f, .1f, .2f, .25f}, {.2f, .9f, .1f, .5f}, {.1f, .2f, .9f, .75f}}};
    std::vector<RenderMesh> meshes;
    for (unsigned i = 0; i < 3; ++i) {
        RenderMesh mesh;
        mesh.entity = ids[i];
        mesh.renderer.mesh = engine_primitive(8);
        mesh.renderer.cast_shadows = false;
        mesh.renderer.materials = {{slot, material}};
        mesh.renderer.material_parameters = {
            {slot, "baseColorFactor", {MaterialParameterType::LinearColor4, paint[i]}}};
        mesh.world.m[11] = 3;
        meshes.push_back(std::move(mesh));
    }
    const auto oracle = [&](const std::vector<Pixels>& fragments) {
        Pixels result(width * height);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto pixel = y * width + x;
                const double wx = (double(x) + .5 - double(width) / 2) * 4 / width;
                const double wy = (double(y) + .5 - double(height) / 2) * 2 / height;
                std::array<double, 3> sum{};
                double denominator = 0, log_transmittance = 0;
                bool opaque = false;
                for (const auto& source : fragments) {
                    const auto& value = source[pixel];
                    const double alpha = value[3], w = 1 / (1 + (wx * wx + wy * wy + 9) * .01);
                    for (unsigned c = 0; c < 3; ++c)
                        sum[c] += value[c] * w;
                    denominator += alpha * w;
                    if (alpha == 1)
                        opaque = true;
                    else
                        log_transmittance += std::log1p(-alpha);
                }
                const double coverage = opaque ? 1 : -std::expm1(log_transmittance);
                for (unsigned c = 0; c < 3; ++c)
                    result[pixel][c] = float(background[c] * (1 - coverage) +
                                             (denominator ? sum[c] / denominator * coverage : 0));
                result[pixel][3] = float(coverage + background[3] * (1 - coverage));
            }
        return result;
    };
    Pixels first_sorted;
    unsigned changed_sorted = 0;
    // Unlit, punctual PBR and area-lit PBR all use the same material/draw owners.
    for (unsigned lighting = 0; lighting < 3; ++lighting) {
        std::cout << "Lighting case " << lighting << ", preceding frames " << frames << std::endl;
        values.model = lighting ? "forge.gltf.metallic-roughness.v1" : "forge.gltf.unlit.v1";
        if (lighting) {
            values.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
            values.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.7f}};
        }
        host->preview_material(material, {values, {}, {}});
        Light light;
        light.cast_shadows = false;
        light.intensity = 3;
        if (lighting == 2) {
            light.kind = unsigned(LightKind::Rectangle);
            light.emitter_width = light.emitter_height = 4;
        }
        scene.lights = {{EntityId::generate(), light_view(light, {})}};
        std::vector<Pixels> fragments;
        for (const auto& mesh : meshes) {
            scene.meshes = {mesh};
            fragments.push_back(render(false, zero));
        }
        check(std::any_of(fragments[0].begin(), fragments[0].end(),
                          [](const auto& p) { return p[3] > .1f && p[0] > 0; }),
              "Transparency fragment oracle has no visible geometry");
        const auto wanted = oracle(fragments);
        std::array<unsigned, 3> order{0, 1, 2};
        do {
            scene.meshes = meshes;
            for (unsigned i = 0; i < 3; ++i)
                scene.meshes[i].entity = ids[order[i]];
            equal_pixels(
                render(true, background), wanted, 5e-6,
                "Weighted Mesh pixels differ from independent captured-fragment composition");
            if (!lighting) {
                const auto sorted = render(false, background);
                if (first_sorted.empty())
                    first_sorted = sorted;
                else if (sorted != first_sorted)
                    ++changed_sorted;
            }
        } while (std::next_permutation(order.begin(), order.end()));
        scene.meshes = meshes;
        const auto ordinary = render(false, background);
        (void)render(true, background);
        equal_pixels(render(false, background), ordinary, 0,
                     "Disabling weighted transparency retained attachment state");
    }
    check(changed_sorted > 0, "Permutation fixture failed to change actual sorted alpha order");
    values.model = "forge.gltf.unlit.v1";
    scene.lights.clear();
    host->preview_material(material, {values, {}, {}});
    const auto compare_fragments = [&](const std::vector<RenderMesh>& selected,
                                       double tolerance = 5e-6) {
        std::vector<Pixels> fragments;
        for (const auto& mesh : selected) {
            scene.meshes = {mesh};
            fragments.push_back(render(false, zero));
        }
        scene.meshes = selected;
        const auto actual = render(true, background);
        equal_pixels(actual, oracle(fragments), tolerance,
                     "Weighted transparency boundary case differs from fragment composition");
        return actual;
    };
    for (const auto alpha : {0.f, 1.f, 0x1p-24f, .124f}) {
        auto selected = meshes;
        for (auto& mesh : selected)
            mesh.renderer.material_parameters.front().parameter.value[3] = alpha;
        compare_fragments(selected);
    }
    view.viewport = {7, 4, 32, 16};
    compare_fragments(meshes);
    view = camera_view(camera, {}, width, height);
    // Exercise actual material HDR, beyond the standalone additive prerequisite.
    values.model = "forge.gltf.metallic-roughness.v1";
    values.parameters["emissiveFactor"] = {MaterialParameterType::LinearColor3, {1, .5f, .25f}};
    values.parameters["emissiveStrength"] = {MaterialParameterType::Scalar, {64}};
    host->preview_material(material, {values, {}, {}});
    // At radiance64, binary32 arithmetic needs an absolute bound above one ULP.
    const auto hdr = compare_fragments(meshes, 8e-5);
    check(std::any_of(hdr.begin(), hdr.end(), [](const auto& p) { return p[0] > 2; }),
          "HDR transparency fixture did not produce actual emissive radiance");
    values.model = "forge.gltf.unlit.v1";
    values.parameters.erase("emissiveFactor");
    values.parameters.erase("emissiveStrength");
    host->preview_material(material, {values, {}, {}});
    auto occluder = meshes.front();
    occluder.entity = EntityId::generate();
    occluder.renderer.materials = {{slot, engine_material(EngineMaterial::TwoSided)}};
    occluder.world.m[11] = 2;
    scene.meshes = {occluder};
    const auto opaque_only = render(false, background);
    check(std::any_of(opaque_only.begin(), opaque_only.end(),
                      [](const auto& p) { return p[3] == 1; }),
          "Occlusion fixture has no visible depth-writing geometry");
    equal_pixels(render(true, background), opaque_only, 0,
                 "Opting in without Blend geometry changed ordinary rendering");
    scene.meshes = meshes;
    scene.meshes.push_back(occluder);
    equal_pixels(render(true, background), opaque_only, 0,
                 "Weighted transparency ignored opaque depth occlusion");
    auto sprite_values = values;
    sprite_values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    host->preview_material(
        material,
        {sprite_values, {{"baseColorTexture", engine_texture(EngineTexture::White)}}, {}});
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = material;
    sprite.renderer.tint_a = .4f;
    sprite.world.m[11] = 1;
    scene.sprites = {sprite};
    scene.meshes = meshes;
    const auto mixed_sorted = render(false, background);
    equal_pixels(render(true, background, true), mixed_sorted, 0,
                 "Visible Sprite did not retain whole-view sorted alpha");
    scene.sprites.clear();
    host->preview_material(material, {values, {}, {}});
    compare_fragments(meshes);
    scene.meshes = meshes;
    values.depth_write = true;
    host->preview_material(material, {values, {}, {}});
    const auto depth_sorted = render(false, background);
    equal_pixels(render(true, background, true), depth_sorted, 0,
                 "Incompatible transparent depth writes did not retain sorted geometry");
    check(std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                      [](const auto& d) {
                          return d.category == "render.transparency.unavailable" &&
                                 d.context.property == "rendering.weighted_transparency";
                      }),
          "Weighted transparency fallback lacks a contextual warning");
    values.depth_write = false;
    host->preview_material(material, {values, {}, {}});
    const auto recovered = render(true, background);
    AffineTransform shifted;
    shifted.m[3] = shifted.m[7] = shifted.m[11] = double(1ull << 40);
    view = camera_view(camera, shifted, width, height);
    for (auto& mesh : scene.meshes)
        for (unsigned axis = 0; axis < 3; ++axis)
            mesh.world.m[axis * 4 + 3] += shifted.m[axis * 4 + 3];
    for (auto& light : scene.lights)
        for (unsigned axis = 0; axis < 3; ++axis)
            light.light.position[axis] += shifted.m[axis * 4 + 3];
    equal_pixels(render(true, background), recovered, 1e-5,
                 "Weighted transparency depends on the absolute world origin");
    test_weighted::targets(presentation, context, color,
                           depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL));
    if (!scene_only) {
        const auto graph = compile_material_graph(create_material_graph());
        for (bool area : {false, true}) {
            auto source = material_graph_shader_wrapper(graph.surface, "graph.hlsli", false, false,
                                                        false, false, area, true);
            source = weighted_color_wrapper(std::move(source), true);
            const auto replace = [&](const std::string& old, const std::string& value) {
                const auto at = source.find(old);
                check(at != std::string::npos, "Weighted graph lost an engine source");
                source.replace(at, old.size(), value);
            };
            replace("#include \"engine/forge.surface.hlsli\"",
                    surface_shader_header(graph.surface));
            replace("#include \"graph.hlsli\"", graph.source);
            if (area)
                replace("ForgeSurfaceWeightedColor(", "ForgeSurfaceAreaWeightedColor(");
            ShaderCreateInfo info;
            info.Desc.Name = "FORGE weighted physical graph source";
            info.Desc.ShaderType = SHADER_TYPE_PIXEL;
            info.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
            info.Source = source.c_str();
            info.EntryPoint =
                surface_shader_entry(surface_color_role(false, false, false, false, area, true));
            info.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
            RefCntAutoPtr<IShader> shader;
            presentation.shader(info, &shader);
            check(bool(shader), "Weighted physical graph stage did not compile");
        }
    }
    context->WaitForIdle();
    std::cout
        << "Weighted transparency: " << frames
        << " actual shared renderer frames, captured-fragment oracles, "
           "six draw orders, unlit/punctual/area PBR, fallback, recovery and large origin passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--scene-only"),
              "Need transparency scratch directory");
        execute(argv[1], argc == 3);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
