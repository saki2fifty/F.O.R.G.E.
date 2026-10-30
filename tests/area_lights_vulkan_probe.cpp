#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "area_lights_numeric_vulkan_tests.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include <algorithm>
#include <chrono>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <thread>
constexpr auto require = forge::test_bloom::check;
#include "punctual_light_tests.hpp"
using namespace Diligent;
using namespace forge;
namespace {
using Pixels = std::vector<test_bloom::Pixel>;
using test_bloom::check;
double energy(const Pixels& pixels) {
    double total = 0;
    for (const auto& p : pixels)
        for (unsigned c = 0; c < 3; ++c) {
            check(std::isfinite(p[c]) && p[c] >= 0, "Area frame has invalid HDR color");
            total += p[c];
        }
    return total;
}
double difference(const Pixels& a, const Pixels& b) {
    check(a.size() == b.size(), "Area frames have different extents");
    double total = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            total += std::abs(a[i][c] - b[i][c]);
    return total;
}
void execute(const std::filesystem::path& root, bool numeric_only) {
    std::filesystem::create_directories(root);
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
    create.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    check(device && context, "Area-light native device unavailable");
    DiligentPresentation presentation(device);
    if (numeric_only) {
        check_punctual_lights(presentation, context, SHADER_COMPILER_DEFAULT);
        test_area_lights::radiometry(presentation, context);
        context->WaitForIdle();
        return;
    }
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialResourceData source;
    source.values.model = "forge.gltf.metallic-roughness.v1";
    source.values.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                   {.9f, .6f, .2f, 1}};
    source.values.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.12f}};
    source.values.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {.9f}};
    host->preview_material(material, source);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    Camera camera;
    camera.background_r = camera.background_g = camera.background_b = 0;
    scene.cameras.push_back({EntityId::generate(), camera, {}});
    for (double x : {-.55, .55}) {
        RenderMesh mesh;
        mesh.entity = EntityId::generate();
        mesh.renderer.mesh = engine_primitive(0);
        mesh.renderer.materials = {{"surface", material}};
        mesh.renderer.layers = 1;
        mesh.renderer.cast_shadows = false;
        mesh.world.m[0] = mesh.world.m[5] = mesh.world.m[10] = .7;
        mesh.world.m[3] = x;
        mesh.world.m[11] = 3;
        scene.meshes.push_back(mesh);
    }
    constexpr unsigned width = 96, height = 64;
    const auto cameras = prepare_game_cameras(scene, width, height);
    check(cameras.diagnostics.empty() && cameras.cameras.size() == 1, "Area camera rejected");
    FrameRenderer frame(presentation);
    frame.resources(host);
    unsigned frames = 0;
    const auto render = [&](bool allow_errors = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        ITextureView* target = nullptr;
        do {
            target = frame.render(context, scene, cameras.cameras, width, height, 0, nullptr, 0,
                                  nullptr, FrameRenderer::Output::LinearHdr);
            check(std::chrono::steady_clock::now() < deadline, "Area frame admission timed out");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!allow_errors && !frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(target && frame.omitted_diagnostics() == 0,
              "Area output or diagnostic capacity missing");
        auto pixels = test_bloom::read(device, context, target->GetTexture());
        (void)energy(pixels);
        for (const auto& p : pixels)
            check(!(p[0] == 1 && p[1] == 0 && p[2] == 1),
                  "Area material reached numerical rejection color");
        ++frames;
        return pixels;
    };
    Light light;
    light.kind = unsigned(LightKind::Point);
    light.intensity = 4;
    LocalTransform pose;
    pose.translation = {0, 0, 1};
    const auto id = EntityId::generate();
    const auto select = [&] { scene.lights = {{id, light_view(light, affine_transform(pose))}}; };
    select();
    const auto point = render();
    check(energy(point) > .01, "Point control did not illuminate geometry");
    light.kind = unsigned(LightKind::Sphere);
    light.emitter_radius = .001f;
    select();
    const auto tiny = render();
    check(difference(point, tiny) < .02 * energy(point),
          "Small spherical light lost point brightness");
    light.emitter_radius = .8f;
    select();
    const auto sphere = render();
    check(difference(sphere, point) > .001, "Sphere size did not affect the material highlight");
    light.kind = unsigned(LightKind::Rectangle);
    light.emitter_width = 1.5f;
    light.emitter_height = .25f;
    select();
    const auto rectangle = render();
    check(energy(rectangle) > .01 && difference(rectangle, sphere) > .001,
          "Rectangular emission did not reach distinct geometry pixels");
    pose.rotation = rotation_from_euler({0, 0, 70});
    select();
    const auto rolled = render();
    check(difference(rolled, rectangle) > .001, "Rectangle roll did not reach its emitter plane");
    pose.rotation = rotation_from_euler({0, 180, 0});
    select();
    check(energy(render()) < 1e-4, "Rectangle emitted from its back side");
    pose.rotation = {};
    light.layers = 2;
    select();
    check(energy(render()) < 1e-4, "Area light bypassed render layers");
    light.layers = UINT32_MAX;
    light.intensity = 0;
    select();
    check(energy(render()) < 1e-4, "Zero-intensity area light contributed radiance");
    light.intensity = 4;
    light.kind = unsigned(LightKind::Point);
    select();
    check(render() == point, "Returning to punctual lighting changed its existing draw");
    auto point_source = scene.lights.front();
    point_source.entity = EntityId::generate();
    light.kind = unsigned(LightKind::Rectangle);
    select();
    scene.lights.push_back(point_source);
    const auto mixed = render();
    std::reverse(scene.lights.begin(), scene.lights.end());
    check(difference(mixed, render()) < .002 * energy(mixed),
          "Mixed light packets depend on list order");
    select();
    for (bool temporal : {false, true})
        for (unsigned output = 0; output < 4; ++output) {
            scene.settings.temporal_anti_aliasing.enabled = temporal;
            scene.settings.ambient_occlusion.enabled = output == 1 || output == 2;
            scene.settings.ambient_occlusion.normal_influence = output == 2 ? 1.f : 0.f;
            scene.settings.screen_space_reflections.enabled = output == 3;
            check(energy(render()) > .01, "Area color was lost from a surface-output profile");
        }
    scene.settings.temporal_anti_aliasing.enabled = false;
    scene.settings.ambient_occlusion.enabled = false;
    scene.settings.screen_space_reflections.enabled = false;
    source.values.parameters["clearcoatFactor"] = {MaterialParameterType::Scalar, {.7f}};
    source.values.parameters["clearcoatRoughnessFactor"] = {MaterialParameterType::Scalar, {.25f}};
    source.values.parameters["sheenColorFactor"] = {MaterialParameterType::LinearColor3,
                                                    {.2f, .3f, .5f}};
    source.values.parameters["sheenRoughnessFactor"] = {MaterialParameterType::Scalar, {.4f}};
    source.values.parameters["anisotropyStrength"] = {MaterialParameterType::Scalar, {.7f}};
    source.values.parameters["anisotropyRotation"] = {MaterialParameterType::Scalar, {.3f}};
    source.values.parameters["iridescenceFactor"] = {MaterialParameterType::Scalar, {.4f}};
    host->preview_material(material, source);
    const auto layered = render();
    check(energy(layered) > .01 && difference(layered, rectangle) > .001,
          "Area native material layers did not reach distinct geometry pixels");
    source.values.alpha = MaterialAlpha::Blend;
    source.values.parameters["baseColorFactor"].value[3] = .6f;
    host->preview_material(material, source);
    check(energy(render()) > .01, "Blended area material failed ordinary color admission");
    source.values.alpha = MaterialAlpha::Opaque;
    source.values.parameters["baseColorFactor"].value[3] = 1;
    source.values.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.5f}};
    host->preview_material(material, source);
    check(energy(render()) > .01, "Transmissive area material failed ordinary color admission");
    // A Mesh particle uses the instanced queue, distinct from ordinary receivers.
    // Remove static meshes so the pixel signal can only come from that consumer.
    auto receivers = std::move(scene.meshes);
    ParticleEffectData effect;
    effect.mesh = engine_primitive(0);
    effect.material = engine_material();
    effect.emission = ParticleEmission::Burst;
    effect.capacity = effect.burst_count = 1;
    effect.speed_min = effect.speed_max = 0;
    effect.acceleration = {0, 0, 0};
    effect.size_start = effect.size_end = .7;
    effect.color_start = effect.color_end = {1, 1, 1};
    RenderParticleEmitter emitter;
    emitter.entity = EntityId::generate();
    emitter.emitter.effect = {AssetId::generate()};
    emitter.world.m[11] = 3;
    host->preview_particle_effect(emitter.emitter.effect, effect);
    scene.particles = {emitter};
    for (const auto kind : {LightKind::Rectangle, LightKind::Sphere}) {
        light.kind = unsigned(kind);
        select();
        check(energy(render()) > .01, "Area source did not illuminate the instanced Mesh particle");
        light.intensity = 0;
        select();
        check(energy(render()) < 1e-4,
              "Mesh particle retained illumination from a zero area source");
        light.intensity = 4;
    }
    scene.particles.clear();
    scene.meshes = std::move(receivers);
    light.kind = unsigned(LightKind::Rectangle);
    light.cast_shadows = true;
    scene.settings.shadows.enabled = true;
    select();
    (void)render(true);
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) {
                          return d.text.find("Area lights do not support shadow visibility") !=
                                 std::string::npos;
                      }),
          "Unsupported area shadow request was not diagnosed");
    context->WaitForIdle();
    std::cout << frames
              << " actual area-light frames: native packet, size/roll/front/layers/zero, "
                 "punctual recovery, mixed lists, eight surface profiles, native material layers, "
                 "Blend/transmission, instanced Mesh particles "
                 "and explicit shadow limit PASS. Software native acceptance only.\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--numeric-only"),
              "Expected area-light fixture output folder and optional --numeric-only");
        execute(argv[1], argc == 3);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
