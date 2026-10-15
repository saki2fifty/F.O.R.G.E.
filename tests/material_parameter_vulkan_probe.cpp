#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "engine_render_resource.hpp"
#include "mesh_render_host.hpp"
#include <bit>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
using Pixels = std::vector<std::array<std::uint8_t, 4>>;
Pixels read(IRenderDevice* device, IDeviceContext* context, ITexture* texture) {
    auto desc = texture->GetDesc();
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    check(bool(staging), "Material readback allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({texture, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    check(mapped.pData != nullptr, "Material readback mapping failed");
    Pixels pixels(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    desc.Width * sizeof(pixels.front()));
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels;
}
std::size_t dominant(const Pixels& pixels, unsigned channel) {
    return std::count_if(pixels.begin(), pixels.end(), [channel](const auto& value) {
        return value[channel] > 30 && value[channel] > value[(channel + 1) % 3] + 12 &&
               value[channel] > value[(channel + 2) % 3] + 12;
    });
}
void image(const std::filesystem::path& path, const Pixels& pixels) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n384 192\n255\n";
    for (const auto& value : pixels)
        file.write(reinterpret_cast<const char*>(value.data()), 3);
    check(bool(file.flush()), "Material image write failed");
}
void execute(const std::filesystem::path& root, const std::filesystem::path& gallery = {}) {
    std::filesystem::create_directories(root);
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    check(device && context, "Material Vulkan device unavailable");
    std::cout << "Object material native adapter: " << device->GetAdapterInfo().Description << '\n';
    DiligentPresentation presentation(device);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    MeshSceneRenderer renderer(host);
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE object material proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 384;
    desc.Height = 192;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Material targets unavailable");
    RenderScene scene;
    scene.scene = AssetId::generate();
    for (unsigned i = 0; i < 4; ++i) {
        RenderMesh mesh;
        mesh.entity = EntityId::generate();
        mesh.renderer.mesh = engine_primitive(0);
        mesh.renderer.materials = {{"surface", engine_material(EngineMaterial::LegacyBlockout)}};
        mesh.renderer.cast_shadows = mesh.renderer.receive_shadows = false;
        mesh.world.m[3] = double(i) * 2 - 3;
        scene.meshes.push_back(std::move(mesh));
    }
    // Fix the object submission order so the no-override neighbor follows an
    // overridden object on the very same shared physical draw.
    std::sort(scene.meshes.begin(), scene.meshes.end(),
              [](const auto& a, const auto& b) { return a.entity < b.entity; });
    for (unsigned i = 0; i < 4; ++i)
        scene.meshes[i].world.m[3] = double(i) * 2 - 3;
    const auto red = MaterialParameterOverride{
        "surface", "baseColorFactor", {MaterialParameterType::LinearColor4, {1, .01f, .01f, 1}}};
    auto green = red;
    green.parameter.value = {.01f, 1, .01f, 1};
    scene.meshes[0].renderer.material_parameters = {red};
    scene.meshes[1].renderer.material_parameters = {green};
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 5;
    AffineTransform camera_world;
    camera_world.m[11] = -10;
    const auto view = camera_view(camera, camera_world, 384, 192);
    const auto admit = [&](bool errors = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline, "Material admission timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        check(errors || renderer.diagnostics().empty(),
              renderer.diagnostics().empty() ? "" : renderer.diagnostics().front().text);
    };
    const auto draw = [&](bool errors = false) {
        admit(errors);
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        host->submit();
        context->Flush();
        return read(device, context, color);
    };
    const auto distinct = draw();
    check(dominant(distinct, 0) > 100 && dominant(distinct, 1) > 100 &&
              renderer.bundle_count() == 1 && renderer.draw_stats().calls == 3 &&
              renderer.draw_stats().batched_calls == 1 && renderer.draw_stats().instances == 4,
          "Object values did not isolate native pixels or preserve equal/default batches");
    image(root / "distinct.ppm", distinct);
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters = {red};
    const auto equal = draw();
    check(dominant(equal, 0) > 600 && dominant(equal, 1) == 0 && renderer.bundle_count() == 1 &&
              renderer.draw_stats().calls == 1 && renderer.draw_stats().batched_calls == 1,
          "Equal object values disabled shared native instancing");
    image(root / "equal.ppm", equal);
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters.clear();
    const auto defaults = draw();
    check(dominant(defaults, 0) == 0 && dominant(defaults, 1) == 0 && defaults != equal &&
              renderer.draw_stats().calls == 1,
          "Object values leaked into the following immutable/default draw");
    image(root / "defaults.ppm", defaults);
    scene.meshes[0].renderer.material_parameters = {red};
    const auto before_invalid = draw();
    scene.meshes[0].renderer.material_parameters[0].parameter.type = MaterialParameterType::Vector4;
    const auto retained = draw(true);
    check(retained == before_invalid && !renderer.diagnostics().empty() && !renderer.pending(),
          "Invalid semantic edit lost the complete last-good draw or retried indefinitely");
    scene.meshes[0].renderer.material_parameters = {green};
    const auto recovered = draw();
    check(dominant(recovered, 1) > 100 && dominant(recovered, 0) == 0,
          "Valid values did not recover after semantic rejection");
    // Values are admitted with a replacement resource, not applied prematurely
    // to its last-good predecessor. Failed replacements retain both together.
    for (auto& mesh : scene.meshes)
        mesh.renderer.materials = {{"surface", engine_material()}};
    Light sun;
    sun.cast_shadows = false;
    sun.intensity = 1;
    AffineTransform sun_world;
    sun_world.m[0] = sun_world.m[10] = std::cos(.4);
    sun_world.m[2] = std::sin(.4);
    sun_world.m[8] = -std::sin(.4);
    scene.lights = {{EntityId::generate(), light_view(sun, sun_world)}};
    auto rough = MaterialParameterOverride{
        "surface", "roughnessFactor", {MaterialParameterType::Scalar, {.15f}}};
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters = {red, rough};
    const auto smooth = draw();
    image(root / "pbr-smooth.ppm", smooth);
    std::cout << "PBR smooth red=" << dominant(smooth, 0)
              << " calls=" << renderer.draw_stats().calls
              << " instances=" << renderer.draw_stats().instances
              << " bundles=" << renderer.bundle_count() << '\n';
    check(dominant(smooth, 0) > 100 && renderer.draw_stats().calls == 1,
          "Lit PBR object values were not consumed by the shared shader");
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters[1].parameter.value[0] = .9f;
    const auto matte = draw();
    check(matte != smooth && renderer.bundle_count() == 1,
          "PBR roughness changed no pixels or created per-object resource bundles");
    image(root / "pbr-roughness.ppm", matte);
    const auto before_reload = matte;
    auto incompatible = asset_detail::engine_material_resource(engine_material());
    incompatible.values.model = "forge.gltf.specular-glossiness.v1";
    incompatible.values.parameters.erase("metallicFactor");
    host->preview_material(engine_material(), incompatible);
    const auto failed_reload = draw(true);
    check(failed_reload == before_reload && !renderer.diagnostics().empty() &&
              renderer.bundle_count() == 1,
          "Incompatible resource reload mixed new resources with prior object values");
    host->preview_material(engine_material(),
                           asset_detail::engine_material_resource(engine_material()));
    check(draw() == before_reload, "Compatible resource recovery changed retained object values");
    const auto bytes = renderer.material_payload_bytes();
    check(bytes > 0 && bytes < 16 * 1024 * 1024, "Object material payload is not accounted");
    MeshSceneRenderer limited(host, TEX_FORMAT_RGBA8_UNORM, mesh_pose_payload_limit, 4096, 1);
    for (unsigned retry = 0; retry < 2; ++retry)
        limited.update(scene);
    check(!limited.diagnostics().empty() && limited.material_payload_bytes() == 0,
          "Scene material budget admitted unaccounted object allocations");
    // Exercise the shared MeshDraw path with simultaneous skin + morph data.
    // Skinned draws have no instance SRB; numeric updates must tolerate that
    // absent parity slot and preserve all existing vertex bindings.
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.joint_palette = {0};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-.5f, -.5f, 0, -.5f, .5f, 0, .5f, .5f, 0, .5f, -.5f, 0}},
        {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
        {"JOINTS_0", 4, std::vector<std::uint32_t>(16, 0)},
        {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}}};
    part.morph_targets = {
        {{"POSITION", 3, std::vector<float>{.5f, 0, 0, .5f, 0, 0, .5f, 0, 0, .5f, 0, 0}}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    mesh.morph_names = {"Shift"};
    mesh.morph_defaults = {0};
    const auto gpu = upload_mesh(device, mesh);
    auto material =
        asset_detail::engine_material_resource(engine_material(EngineMaterial::LegacyBlockout))
            .values;
    material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4, {1, 1, 1, 1}};
    MeshDraw skin_draw(presentation, context, gpu.lods[0].parts[0], material, {},
                       TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    check(!skin_draw.supports_instances(), "Skin fixture unexpectedly used static instancing");
    const std::array<AffineTransform, 1> joints{}, inverse_bind{};
    auto morph_bounds = part.bounds;
    morph_bounds.minimum[0] += .5f;
    morph_bounds.maximum[0] += .5f;
    const auto pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, morph_bounds);
    const std::array<float, 1> weights{1};
    const auto direct = [&](MeshDraw& selected, std::span<const MaterialParameterOverride> values,
                            bool depth_only) {
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(depth_only ? 0 : 1, depth_only ? nullptr : &rtv, dsv,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{0, 0, 0, 1};
        if (!depth_only)
            context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        selected.draw(context, {}, view, {}, nullptr, nullptr, nullptr, {}, nullptr, weights, &pose,
                      {}, values, "surface");
        context->Flush();
        return read(device, context, depth_only ? depth : color);
    };
    const auto skin_red = direct(skin_draw, std::span(&red, 1), false);
    const auto skin_green = direct(skin_draw, std::span(&green, 1), false);
    const auto skin_default = direct(skin_draw, {}, false);
    check(dominant(skin_red, 0) > 100 && dominant(skin_green, 1) > 100 &&
              dominant(skin_default, 0) == 0 && dominant(skin_default, 1) == 0,
          "Numeric material updates lost skinned/morphed geometry or leaked values");
    image(root / "skinned-morph-values.ppm", skin_green);
    material.alpha = MaterialAlpha::Mask;
    MeshDraw masked_shadow(presentation, context, gpu.lods[0].parts[0], material, {},
                           TEX_FORMAT_UNKNOWN, TEX_FORMAT_D32_FLOAT);
    auto transparent = red;
    transparent.parameter.value[3] = 0;
    const auto depth_opaque = direct(masked_shadow, std::span(&red, 1), true);
    const auto depth_cutout = direct(masked_shadow, std::span(&transparent, 1), true);
    const auto depth_restored = direct(masked_shadow, {}, true);
    const auto written = [](const Pixels& pixels) {
        return std::count_if(pixels.begin(), pixels.end(), [](const auto& value) {
            const auto depth = std::bit_cast<float>(value);
            return std::isfinite(depth) && depth < 1;
        });
    };
    check(written(depth_opaque) > 100 && written(depth_cutout) == 0 &&
              depth_restored == depth_opaque,
          "Masked shadow pass ignored object alpha or failed to restore material defaults");
    if (!gallery.empty()) {
        WorldContext gallery_world;
        Scene gallery_scene(gallery_world);
        gallery_scene.restore_snapshot(
            load_game_scene(gallery, {AssetId::parse("c94f7ad7-c2cf-4e7e-89f7-d32e20294036")}));
        const auto frame = extract_render_scene(gallery_scene.effective_document());
        const auto cameras = prepare_game_cameras(frame, 384, 192);
        check(frame.meshes.size() == 7 && frame.diagnostics.empty() && cameras.cameras.size() == 1,
              "Gallery66 native scene/camera extraction differs from its CPU proof");
        auto gallery_host = std::make_shared<MeshResourceHost>(presentation, context, gallery);
        gallery_host->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(gallery)));
        MeshSceneRenderer gallery_renderer(gallery_host);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            gallery_renderer.update(frame);
            check(std::chrono::steady_clock::now() < deadline,
                  "Gallery66 native admission timed out");
            if (gallery_renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (gallery_renderer.pending());
        check(gallery_renderer.diagnostics().empty(),
              gallery_renderer.diagnostics().empty() ? ""
                                                     : gallery_renderer.diagnostics().front().text);
        const auto& gallery_view = cameras.cameras[0].view;
        gallery_renderer.shadows(frame, gallery_view, UINT32_MAX);
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float background[4]{.02f, .025f, .03f, 1};
        context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        gallery_renderer.draw(frame, gallery_view, UINT32_MAX, color, dsv);
        check(gallery_renderer.diagnostics().empty(), "Gallery66 native draw/shadow failed");
        gallery_host->submit();
        context->Flush();
        const auto gallery_pixels = read(device, context, color);
        image(root / "gallery66-shared-values.ppm", gallery_pixels);
        std::cout << "Gallery66 native RGB=" << dominant(gallery_pixels, 0) << ','
                  << dominant(gallery_pixels, 1) << ',' << dominant(gallery_pixels, 2)
                  << " bundles=" << gallery_renderer.bundle_count()
                  << " instances=" << gallery_renderer.draw_stats().instances << '\n';
        check(dominant(gallery_pixels, 0) > 30 && dominant(gallery_pixels, 1) > 30 &&
                  dominant(gallery_pixels, 2) > 30 && gallery_renderer.bundle_count() == 2 &&
                  gallery_renderer.draw_stats().instances == 7,
              "Gallery66 lost native object colors or split the shared material resources");
    }
    std::cout << "Native object color/roughness, default restoration, equal-value instancing, "
                 "last-good edits/reloads/recovery, bounded sparse payload, skin/morph values "
                 "and masked shadow alpha passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3,
              "Need material native scratch root and optional Gallery/package");
        execute(std::filesystem::path(argv[1]) / AssetId::generate().str(),
                argc == 3 ? argv[2] : "");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
