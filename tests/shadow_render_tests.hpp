#pragma once
#include "mesh_draw.hpp"
#include "shadow_renderer.hpp"
#include <numbers>
void check_shadow_render(forge::DiligentPresentation& presentation,
                         Diligent::IDeviceContext* context, const std::filesystem::path& images) {
    using namespace forge;
    using namespace Diligent;
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 0, -1, 1, 0, 1, 1, 0, 1, -1, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    const auto native = upload_mesh(presentation.device(), mesh);
    MaterialData material;
    material.model = "forge.gltf.metallic-roughness.v1";
    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    MeshDraw receiver(presentation, context, native.lods[0].parts[0], material, {},
                      TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    MeshDraw caster(presentation, context, native.lods[0].parts[0], material, {},
                    TEX_FORMAT_UNKNOWN, TEX_FORMAT_D32_FLOAT);
    auto mask = material;
    mask.alpha = MaterialAlpha::Mask;
    mask.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4, {1, 1, 1, 0}};
    MeshDraw cutout(presentation, context, native.lods[0].parts[0], mask, {}, TEX_FORMAT_UNKNOWN,
                    TEX_FORMAT_D32_FLOAT);
    TextureDesc desc;
    desc.Name = "FORGE shadow acceptance";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    require(color && depth, "Shadow acceptance target allocation failed");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    Camera lens;
    lens.vertical_fov = std::numbers::pi / 2;
    lens.near_plane = .1;
    lens.far_plane = 10;
    AffineTransform origin;
    origin.m[3] = 1e12;
    const auto camera = camera_view(lens, origin, 32, 32);
    AffineTransform receiver_world = origin, caster_world = origin;
    receiver_world.m[0] = receiver_world.m[5] = 2;
    receiver_world.m[11] = 4;
    caster_world.m[0] = caster_world.m[5] = .5;
    caster_world.m[11] = 2;
    RenderScene scene;
    scene.scene = AssetId::generate();
    // Resolve the caster across more than the native 3x3 PCF footprint. At 64
    // texels the second cascade's small caster is mostly a penumbra, so its
    // center is not a valid fully-occluded sample for the strict ratio below.
    scene.settings.shadows.resolution = 256;
    scene.settings.shadows.cascades = 2;
    scene.settings.shadows.distance = 8;
    Light light;
    light.cast_shadows = true;
    light.range = 10;
    light.intensity = 32;
    light.shadow_normal_bias = 0;
    const auto entity = EntityId::generate();
    ShadowRenderer renderer(presentation);
    auto render = [&](MeshDraw& shadow_draw, bool enabled) {
        scene.settings.shadows.enabled = enabled;
        const ShadowCasterBounds bounds{transform_bounds(part.bounds, caster_world), UINT32_MAX};
        renderer.render(context, scene, camera, UINT32_MAX, std::span(&bounds, 1),
                        [&](const CameraView& view, std::uint32_t) {
                            shadow_draw.draw(context, caster_world, view, {});
                        });
        require(renderer.diagnostics().empty(), "Valid shadow view emitted a resource diagnostic");
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0, 0, 32, 32, 0, 1};
        context->SetViewports(1, &viewport, 32, 32);
        const int slot = renderer.selection(entity);
        if (enabled) {
            require(slot >= 0, "Shadow renderer did not select the requested light");
            const auto& rows = renderer.lighting().values;
            std::cout << "Shadow slot " << slot << " slices " << rows[40][0] << std::endl;
            for (unsigned slice = 0; slice < unsigned(rows[40][0]); ++slice) {
                std::array<float, 4> clip{};
                for (unsigned row = 0; row < 4; ++row)
                    clip[row] = rows[slice * 4 + row][2] * 4 + rows[slice * 4 + row][3];
                std::cout << "  slice " << slice << " range " << rows[32 + slice][0] << ','
                          << rows[32 + slice][1] << " receiver clip " << clip[0] << ',' << clip[1]
                          << ',' << clip[2] << ',' << clip[3] << std::endl;
            }
        }
        receiver.draw(context, receiver_world, camera, std::span(&scene.lights[0].light, 1),
                      nullptr, nullptr, &renderer.lighting(), std::span(&slot, 1));
        auto pixels = readback(presentation.device(), context, rtv);
        if (enabled) {
            const auto raw =
                readback(presentation.device(), context, renderer.lighting().maps[slot]);
            float minimum = 1;
            for (auto pixel : raw)
                minimum = std::min(minimum, std::bit_cast<float>(pixel));
            std::cout << "  first shadow slice minimum depth " << minimum << std::endl;
        }
        return pixels;
    };
    for (auto kind : {LightKind::Directional, LightKind::Spot, LightKind::Point}) {
        light.kind = std::uint32_t(kind);
        light.intensity = kind == LightKind::Directional ? 2 : 32;
        scene.lights = {{entity, light_view(light, origin)}};
        const auto lit = render(caster, false);
        const auto shadowed = render(caster, true);
        const auto center = 16 * 32 + 16;
        const auto suffix = std::to_string(unsigned(kind));
        save(lit, 32, 32, images / ("shadow-lit-kind-" + suffix + ".ppm"));
        save(shadowed, 32, 32, images / ("shadow-kind-" + suffix + ".ppm"));
        std::cout << "Shadow kind " << suffix << ": lit=" << unsigned(lit[center][0])
                  << ", shadowed=" << unsigned(shadowed[center][0]) << std::endl;
        require(lit[center][0] > 40 && shadowed[center][0] < lit[center][0] / 4,
                "Shadow did not occlude the lit receiver at a large world origin");
        const auto transparent = render(cutout, true);
        require(transparent[center][0] == lit[center][0],
                "Alpha-cutout caster made an opaque shadow");
        caster_world.m[0] = -.5;
        const auto mirrored = render(caster, true);
        require(mirrored[center][0] == shadowed[center][0], "Mirroring removed a shadow caster");
        caster_world.m[0] = .5;
    }
    scene.settings.shadows.resolution = UINT32_MAX;
    renderer.render(context, scene, camera, UINT32_MAX, {},
                    [](const CameraView&, std::uint32_t) {});
    require(renderer.selection(entity) == -1 && renderer.diagnostics().size() == 1,
            "Oversized shadow allocation was not rejected before native allocation");
}
