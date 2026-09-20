#pragma once
#include "mesh_draw.hpp"
void check_mesh_draw(forge::DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    forge::MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 0, -1, 1, 0, 1, 1, 0, 1, -1, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}}};
    part.bounds = forge::mesh_bounds(part);
    forge::MeshData mesh;
    mesh.lods = {{1, {part}}};
    const auto gpu = forge::upload_mesh(presentation.device(), mesh);
    forge::MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    material.parameters["baseColorFactor"] = {forge::MaterialParameterType::LinearColor4,
                                              {.2f, .6f, .1f, 1}};
    forge::MeshDraw draw(presentation, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_RGBA8_UNORM,
                         TEX_FORMAT_D32_FLOAT);
    TextureDesc desc;
    desc.Name = "FORGE prepared draw acceptance";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    require(color && depth, "Mesh draw targets unavailable");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    forge::Camera camera;
    camera.vertical_fov = 1.5707963267948966;
    camera.near_plane = .1;
    camera.far_plane = 10;
    auto camera_world = forge::AffineTransform{};
    camera_world.m[3] = 1e12;
    camera_world.m[7] = -1e12;
    auto view = forge::camera_view(camera, camera_world, 32, 32);
    forge::AffineTransform world;
    world.m[3] = 1e12;
    world.m[7] = -1e12;
    world.m[11] = 2;
    auto render = [&](forge::MeshDraw& prepared, std::span<const forge::LightView> lights = {}) {
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0.f, 0.f, 32.f, 32.f, 0.f, 1.f};
        context->SetViewports(1, &viewport, 32, 32);
        prepared.draw(context, world, view, lights);
        return readback(presentation.device(), context, rtv);
    };
    const auto reference = render(draw);
    require(reference[16 * 32 + 16] == std::array<unsigned char, 4>{51, 153, 26, 255} ||
                reference[16 * 32 + 16] == std::array<unsigned char, 4>{51, 153, 25, 255},
            "Prepared unlit mesh did not render with camera-relative placement");
    world.m[0] = -1;
    require(render(draw) == reference,
            "Reflected draw changed coverage or disappeared through culling");
    world.m[0] = 1;
    world.m[5] = -1;
    require(render(draw) == reference, "Vertical reflection changed mesh coverage");
    world.m[5] = 1;
    world.m[10] = 0;
    require(render(draw) == reference, "Rank-two surviving plane disappeared");
    world.m[0] = 0;
    const auto collapsed = render(draw);
    require(
        std::all_of(collapsed.begin(), collapsed.end(),
                    [](auto pixel) { return pixel == std::array<unsigned char, 4>{0, 0, 0, 255}; }),
        "Fully collapsed projected primitive covered pixels");
    world.m[0] = world.m[10] = 1;
    camera.flip_y = true;
    view = forge::camera_view(camera, camera_world, 32, 32);
    require(render(draw) == reference, "Camera image flip broke face culling");
    camera.flip_y = false;
    view = forge::camera_view(camera, camera_world, 32, 32);
    material.model = "forge.gltf.metallic-roughness.v1";
    material.parameters["metallicFactor"] = {forge::MaterialParameterType::Scalar, {0}};
    material.parameters["roughnessFactor"] = {forge::MaterialParameterType::Scalar, {.7f}};
    forge::MeshDraw lit(presentation, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_RGBA8_UNORM,
                        TEX_FORMAT_D32_FLOAT);
    forge::Light authored;
    authored.intensity = 1;
    auto light = forge::light_view(authored, forge::AffineTransform{});
    const auto dark = render(lit);
    require(dark[16 * 32 + 16] == std::array<unsigned char, 4>{0, 0, 0, 255},
            "Game draw invented an implicit light");
    const auto illuminated = render(lit, std::span(&light, 1));
    const auto pixel = illuminated[16 * 32 + 16];
    require(pixel[1] > pixel[0] && pixel[0] > pixel[2] && pixel[2] > 0 && pixel[3] == 255,
            "Native PBR material/light binding failed or produced diagnostic color");
    world.m[0] = -1;
    const auto reflected = render(lit, std::span(&light, 1));
    require(reflected == illuminated, "Signed surface normal changed reflected PBR illumination");
    world.m[0] = 1;
    world.m[10] = 0;
    require(render(lit, std::span(&light, 1)) == illuminated,
            "Rank-two cofactor frame changed surviving PBR surface");
}
