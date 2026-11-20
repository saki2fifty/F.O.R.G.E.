#pragma once
#include "engine_render_resource.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "mesh_render_host.hpp"
#include "particle_ribbon.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
#include <numeric>
#include <thread>
namespace forge::test_particle_ribbon {
using namespace Diligent;
using test_bloom::check;
constexpr unsigned width = 96, height = 80;
using Pixels = std::vector<test_bloom::Pixel>;
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed ribbon submission accepted");
}
struct Targets {
    RefCntAutoPtr<ITexture> color, depth;
    Targets(IRenderDevice* device) {
        TextureDesc desc;
        desc.Name = "FORGE ribbon numeric target";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA32_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET;
        device->CreateTexture(desc, nullptr, &color);
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(desc, nullptr, &depth);
        check(color && depth, "Ribbon numeric targets unavailable");
    }
    void clear(IDeviceContext* context) const {
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float background[]{.03125f, .0625f, .125f, 1};
        context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
    }
};
inline ParticlePathSamples path() {
    ParticlePathSamples result;
    result.paths.push_back({73, 0, 4});
    for (unsigned i = 0; i < 4; ++i) {
        ParticleSample p;
        p.birth = 73;
        p.age = .25 * (i + 1);
        p.position = std::array<Double3, 4>{
            {{-.7, -.8, 3}, {-.3, -.25, 3.15}, {.15, .25, 2.9}, {.7, .7, 3.1}}}[i];
        p.size = std::array{.35, .7, .45, .2}[i];
        p.color = std::array<std::array<float, 3>, 4>{
            {{1, .1f, .2f}, {.2f, 1, .3f}, {.1f, .3f, 1}, {1, .8f, .2f}}}[i];
        p.opacity = std::array{.1, .9, .6, .2}[i];
        result.points.push_back(p);
    }
    return result;
}
// A detached ordinary mesh oracle. Read the canonical triangle ordering, then
// bake independently computed corner positions, colors and UVs into its streams.
// It never reads the instance packet or invokes the ribbon shader branch.
inline MeshData reference_mesh(const MeshData& quad,
                               std::span<const ParticleRibbonSegment> segments,
                               const CameraView& view) {
    MeshPart part;
    std::vector<float> positions, colors, uv, normals, tangents;
    const auto& local =
        std::get<std::vector<float>>(quad.lods[0].parts[0].find("POSITION")->values);
    const auto& coordinates =
        std::get<std::vector<float>>(quad.lods[0].parts[0].find("TEXCOORD_0")->values);
    for (const auto& s : segments)
        for (std::size_t i = 0; i < local.size() / 3; ++i) {
            const unsigned end = local[3 * i + 1] > 0 ? 1 : 0;
            const auto& center = end ? s.vertices.last : s.vertices.first;
            const auto& side = end ? s.vertices.last_side : s.vertices.first_side;
            Double3 across{}, along{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float point = float(center[axis] - view.position[axis]);
                positions.push_back(point + float(side[axis]) * local[3 * i]);
                colors.push_back(s.vertices.color[end][axis]);
                const float a = float(s.vertices.first[axis] - view.position[axis]) +
                                float(s.vertices.first_side[axis]) * local[3 * i];
                const float b = float(s.vertices.last[axis] - view.position[axis]) +
                                float(s.vertices.last_side[axis]) * local[3 * i];
                along[axis] = double(b) - a;
                across[axis] = float(side[axis]);
            }
            Double3 normal{across[1] * along[2] - across[2] * along[1],
                           across[2] * along[0] - across[0] * along[2],
                           across[0] * along[1] - across[1] * along[0]};
            const auto normal_length = std::hypot(normal[0], normal[1], normal[2]);
            const auto side_length = std::hypot(across[0], across[1], across[2]);
            check(normal_length > 0 && side_length > 0, "Reference ribbon face collapsed");
            for (unsigned axis = 0; axis < 3; ++axis) {
                normals.push_back(float(normal[axis] / normal_length));
                tangents.push_back(float(across[axis] / side_length));
            }
            tangents.push_back(1);
            colors.push_back(s.vertices.opacity[end]);
            uv.push_back(coordinates[2 * i]);
            uv.push_back(float(s.vertices.texture_v[end]));
        }
    part.vertices = unsigned(positions.size() / 3);
    part.streams = {{"POSITION", 3, std::move(positions)},
                    {"COLOR_0", 4, std::move(colors)},
                    {"TEXCOORD_0", 2, std::move(uv)},
                    {"NORMAL", 3, std::move(normals)},
                    {"TANGENT", 4, std::move(tangents)}};
    part.bounds = mesh_bounds(part);
    MeshData result;
    result.lods = {{1, {std::move(part)}}};
    validate_mesh(result);
    return result;
}
inline void same_pixels(const Pixels& actual, const Pixels& expected) {
    check(actual.size() == expected.size(), "Ribbon target extent changed");
    unsigned visible = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (expected[i][0] != .03125f || expected[i][1] != .0625f || expected[i][2] != .125f)
            ++visible;
        for (unsigned c = 0; c < 4; ++c)
            check(std::isfinite(actual[i][c]) && std::abs(actual[i][c] - expected[i][c]) < 1e-4f,
                  "Ribbon corners, endpoint appearance or alpha differ from triangle oracle");
    }
    check(visible > 80, "Ribbon comparison admitted an empty or tiny image");
}
inline void execute(DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    Targets target(device);
    const auto quad = asset_detail::engine_mesh_resource(engine_primitive(8)).mesh;
    const auto gpu_quad = upload_mesh(device, quad);
    std::vector<test_bloom::Pixel> texels;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            texels.push_back({float(x + 1) / 4, float(y + 1) / 4, .375f, .75f + float(x + y) / 24});
    const auto texture = test_bloom::texture(device, 4, 4, texels);
    const auto normal_texture = test_bloom::texture(device, 1, 1, {{.75f, .65f, .9f, 1}});
    Light light;
    light.intensity = 2;
    const std::array lights{light_view(light, {})};
    unsigned cases = 0;
    for (unsigned shading = 0; shading < 3; ++shading)
        for (auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Mask, MaterialAlpha::Blend}) {
            std::cout << "Ribbon shading " << shading << " alpha " << unsigned(alpha) << std::endl;
            MaterialData material;
            material.model =
                shading == 2 ? "forge.gltf.metallic-roughness.v1" : "forge.gltf.unlit.v1";
            material.double_sided = true;
            material.alpha = alpha;
            material.depth_write = alpha != MaterialAlpha::Blend;
            MeshDraw::Textures textures;
            if (shading) {
                material.textures["baseColorTexture"].semantic = TextureSemantic::Color;
                textures["baseColorTexture"] =
                    texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            }
            if (shading == 2) {
                material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
                material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.8f}};
                material.textures["normalTexture"].semantic = TextureSemantic::Normal;
                textures["normalTexture"] =
                    normal_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            }
            MeshDraw draw(presentation, context, gpu_quad.lods[0].parts[0], material, textures,
                          TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT);
            for (double translation : {0., 1e12})
                for (unsigned basis = 0; basis < 2; ++basis)
                    for (unsigned flips = 0; flips < 4; ++flips) {
                        Camera camera;
                        camera.basis = basis;
                        camera.projection = unsigned(CameraProjection::Orthographic);
                        camera.orthographic_height = 2.4;
                        camera.flip_x = bool(flips & 1);
                        camera.flip_y = bool(flips & 2);
                        AffineTransform camera_world;
                        camera_world.m[3] = translation;
                        camera_world.m[7] = -translation;
                        // Both basis adapters observe the same world-space path.
                        if (basis) {
                            camera_world.m[0] = -1;
                            camera_world.m[10] = -1;
                        }
                        const auto view = camera_view(camera, camera_world, width, height);
                        AffineTransform emitter;
                        emitter.m[3] = translation;
                        emitter.m[7] = -translation;
                        auto segments = particle_ribbon_segments(path(), emitter, view);
                        std::vector<MeshDraw::Instance> instances;
                        for (const auto& segment : segments) {
                            MeshDraw::Instance instance;
                            instance.world = segment.world;
                            instance.material_tint_only = true;
                            instance.ribbon = &segment.vertices;
                            instances.push_back(instance);
                        }
                        const auto render = [&] {
                            target.clear(context);
                            draw.draw(context, instances.front().world, view, lights, nullptr,
                                      nullptr, nullptr, {}, nullptr, {}, nullptr, instances);
                            return test_bloom::read(device, context, target.color);
                        };
                        const auto actual = render();
                        const auto reference = reference_mesh(quad, segments, view);
                        const auto gpu_reference = upload_mesh(device, reference);
                        MeshDraw oracle(presentation, context, gpu_reference.lods[0].parts[0],
                                        material, textures, TEX_FORMAT_RGBA32_FLOAT,
                                        TEX_FORMAT_D32_FLOAT);
                        auto relative_view = view;
                        relative_view.position = {};
                        target.clear(context);
                        oracle.draw(context, {}, relative_view, lights);
                        same_pixels(actual, test_bloom::read(device, context, target.color));
                        if (!cases) {
                            const auto retained = segments.back().vertices;
                            segments.back().vertices.opacity[1] =
                                std::numeric_limits<float>::quiet_NaN();
                            rejects([&] { (void)render(); });
                            segments.back().vertices = retained;
                            same_pixels(render(), actual);
                            instances.back().legacy_tint = std::array{1.f, 1.f, 1.f};
                            rejects([&] { (void)render(); });
                            instances.back().legacy_tint.reset();
                            same_pixels(render(), actual);
                        }
                        ++cases;
                    }
        }
    context->WaitForIdle();
    std::cout << cases << " ribbon device/triangle comparisons passed\n";
}
inline void scene_test(DiligentPresentation& presentation, IDeviceContext* context,
                       const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto* device = presentation.device();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    material.alpha = MaterialAlpha::Blend;
    material.depth_write = false;
    material.double_sided = true;
    const AssetRef<MaterialAsset> surface{AssetId::generate()};
    host->preview_material(surface, {material, {}, {}, {}});
    auto middle_material = material;
    middle_material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                     {.2f, .8f, .2f, .5f}};
    ParticleEffectData data;
    data.version = 10;
    data.mesh = engine_primitive(8);
    data.material = surface;
    data.trail = ParticleTrailSettings{1, 4};
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 1;
    data.direction = {1, 1, 0};
    data.cone_angle = 0;
    data.speed_min = data.speed_max = 1;
    data.acceleration = {0, -.4, .25};
    data.size_start = .4;
    data.size_end = .2;
    data.color_start = data.color_end = {1, 1, 1};
    data.opacity_start = data.opacity_end = .5;
    const AssetRef<ParticleEffectAsset> effect{AssetId::generate()};
    host->preview_particle_effect(effect, data);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    for (unsigned i = 0; i < 2; ++i) {
        RenderParticleEmitter emitter;
        emitter.entity = EntityId::generate();
        emitter.emitter.effect = effect;
        emitter.time = 1.5;
        emitter.world.m[3] = i ? -.2 : -.4;
        emitter.world.m[7] = i ? -.7 : -.6;
        emitter.world.m[11] = i ? 3.15 : 3;
        emitter.emitter.tint_r = i ? .1f : .9f;
        emitter.emitter.tint_g = .1f;
        emitter.emitter.tint_b = i ? .9f : .1f;
        scene.particles.push_back(emitter);
    }
    RenderMesh middle;
    middle.entity = EntityId::generate();
    middle.renderer.mesh = data.mesh;
    middle.renderer.cast_shadows = false;
    middle.renderer.materials = {{"surface", surface}};
    middle.renderer.material_parameters = {
        {"surface",
         "baseColorFactor",
         {MaterialParameterType::LinearColor4, {.2f, .8f, .2f, .5f}}}};
    middle.world.m[11] = 3.25;
    scene.meshes.push_back(middle);
    MeshSceneRenderer renderer(host);
    const auto prepare = [&](bool invalid = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline, "Trail Scene preparation timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        if (!invalid && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        check(!invalid || !renderer.diagnostics().empty(), "Invalid trail Scene lost diagnostic");
    };
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 2.4;
    const auto view = camera_view(camera, {}, width, height);
    Targets targets(device);
    const auto draw = [&](const CameraView& selected) {
        targets.clear(context);
        renderer.draw(scene, selected, UINT32_MAX, targets.color,
                      targets.depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL));
        if (!renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        auto pixels = test_bloom::read(device, context, targets.color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    prepare();
    const auto actual = draw(view);
    check(renderer.draw_stats().instances == 9 && renderer.draw_stats().calls == 9,
          "Trail segments or ordinary Mesh disappeared from sorted composition");
    // Independently draw ordinary baked triangles in descending camera depth.
    struct Reference {
        double depth;
        MeshData mesh;
        MaterialData material;
    };
    std::vector<Reference> reference;
    const auto quad = asset_detail::engine_mesh_resource(data.mesh).mesh;
    for (const auto& emitter : scene.particles) {
        const auto paths = sample_particle_paths(data, emitter.time, emitter.emitter.seed, 1, 4);
        const auto segments = particle_ribbon_segments(
            paths, emitter.world, view, 1,
            {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b});
        for (const auto& segment : segments)
            reference.push_back({std::midpoint(segment.vertices.first[2], segment.vertices.last[2]),
                                 reference_mesh(quad, std::span(&segment, 1), view), material});
    }
    auto flat = quad;
    auto& flat_part = flat.lods.front().parts.front();
    for (auto& stream : flat_part.streams)
        if (stream.semantic == "POSITION") {
            auto& positions = std::get<std::vector<float>>(stream.values);
            for (std::size_t i = 2; i < positions.size(); i += 3)
                positions[i] += 3.25f;
        }
    flat_part.bounds = mesh_bounds(flat_part);
    reference.push_back({3.25, std::move(flat), middle_material});
    std::sort(reference.begin(), reference.end(),
              [](const auto& a, const auto& b) { return a.depth > b.depth; });
    targets.clear(context);
    for (const auto& item : reference) {
        const auto gpu = upload_mesh(device, item.mesh);
        MeshDraw oracle(presentation, context, gpu.lods[0].parts[0], item.material, {},
                        TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT);
        oracle.draw(context, {}, view, {});
    }
    same_pixels(actual, test_bloom::read(device, context, targets.color));
    std::reverse(scene.particles.begin(), scene.particles.end());
    prepare();
    check(draw(view) == actual, "Emitter insertion order changed segment sorting");
    AffineTransform reverse;
    reverse.m = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 6};
    (void)draw(camera_view(camera, reverse, width, height));
    check(draw(view) == actual, "Second camera changed retained source path or first-view edges");
    material.double_sided = false;
    host->preview_material(surface, {material, {}, {}, {}});
    prepare(true);
    material.double_sided = true;
    host->preview_material(surface, {material, {}, {}, {}});
    prepare();
    check(draw(view) == actual, "Rejected trail Material prevented valid recovery");
    auto excessive = data;
    excessive.capacity = 1024;
    excessive.trail->segments = 8;
    host->preview_particle_effect(effect, excessive);
    prepare(true);
    check(std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                      [](const auto& value) {
                          return value.text.find("8192 reserved") != std::string::npos;
                      }),
          "Scene reservation counted heads instead of declared trail segments");
    host->preview_particle_effect(effect, data);
    prepare();
    check(draw(view) == actual, "Trail reservation rejection prevented valid frame recovery");
    context->WaitForIdle();
    std::cout
        << "Trail Scene composition, per-segment sorting, view retention and recovery passed\n";
}
inline void gpu_scene_test(DiligentPresentation& presentation, IDeviceContext* context,
                           const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto* device = presentation.device();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    MaterialData material;
    material.model = "forge.gltf.metallic-roughness.v1";
    material.double_sided = true;
    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.8f}};
    const AssetRef<MaterialAsset> surface{AssetId::generate()};
    host->preview_material(surface, {material, {}, {}, {}});
    ParticleEffectData data;
    data.version = 23;
    data.mesh = engine_primitive(8);
    data.material = surface;
    data.trail = ParticleTrailSettings{1, 4};
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 1;
    data.direction = {1, 1, 0};
    data.cone_angle = 0;
    data.speed_min = data.speed_max = 1;
    data.acceleration = {0, -.4, .25};
    data.size_start = data.size_end = .6;
    data.color_start = data.color_end = {1, 1, 1};
    const AssetRef<ParticleEffectAsset> effect{AssetId::generate()};
    host->preview_particle_effect(effect, data);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    Light light;
    light.intensity = 2;
    const std::array lights{light_view(light, {})};
    scene.lights.push_back({EntityId::generate(), lights.front(), {}, {}});
    RenderParticleEmitter emitter;
    emitter.entity = EntityId::generate();
    emitter.emitter.effect = effect;
    emitter.emitter.seed = 41;
    emitter.time = 1.5;
    scene.particles.push_back(emitter);
    MeshSceneRenderer renderer(host);
    Targets targets(device);
    const auto quad = asset_detail::engine_mesh_resource(data.mesh).mesh;
    auto cpu_reference = data;
    cpu_reference.version = 10;
    unsigned frames = 0;
    for (unsigned transform = 0; transform < 3; ++transform)
        for (bool flip : {false, true}) {
            auto& world = scene.particles.front().world;
            world = {};
            world.m[0] = transform == 1 ? -1 : 1;
            world.m[1] = .2;
            world.m[10] = transform == 2 ? 0 : 1;
            world.m[3] = transform == 1 ? .7 : -.7;
            world.m[7] = -.6;
            world.m[11] = 3;
            Camera settings;
            settings.projection = unsigned(CameraProjection::Orthographic);
            settings.orthographic_height = 2.4;
            settings.flip_x = flip;
            const auto camera = camera_view(settings, {}, width, height);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            do {
                host->pump();
                renderer.update(scene);
                check(std::chrono::steady_clock::now() < deadline,
                      "GPU ribbon Scene resources timed out");
                if (renderer.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (renderer.pending());
            targets.clear(context);
            renderer.draw(scene, camera, UINT32_MAX, targets.color,
                          targets.depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL));
            if (!renderer.diagnostics().empty())
                throw std::runtime_error(renderer.diagnostics().front().text);
            check(renderer.draw_stats().instances == 4, "GPU ribbon Scene lost reserved segments");
            const auto actual = test_bloom::read(device, context, targets.color);
            // Independent CPU trajectory and ordinary triangles establish both
            // geometry and lit face orientation; no device output is read back.
            const auto paths =
                sample_particle_paths(cpu_reference, emitter.time, emitter.emitter.seed, 1, 4);
            const auto segments = particle_ribbon_segments(paths, world, camera);
            const auto triangles = upload_mesh(device, reference_mesh(quad, segments, camera));
            MeshDraw oracle(presentation, context, triangles.lods[0].parts[0], material, {},
                            TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT);
            targets.clear(context);
            oracle.draw(context, {}, camera, lights);
            same_pixels(actual, test_bloom::read(device, context, targets.color));
            context->FinishFrame();
            device->ReleaseStaleResources();
            ++frames;
        }
    context->WaitForIdle();
    std::cout << "GPU ribbon lit Scene matches independent triangles in " << frames
              << " reflected, sheared, singular and flipped-view frames\n";
}
} // namespace forge::test_particle_ribbon
