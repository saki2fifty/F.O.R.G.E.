#pragma once
#include "mesh_draw.hpp"
#include "render_bounds.hpp"
void check_skin_draw(forge::DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& images) {
    using namespace Diligent;
    using namespace forge;
    MeshPart part;
    part.vertices = 3;
    part.indices = {0, 1, 2};
    part.joint_palette = {0, 1, 2};
    part.streams = {{"POSITION", 3, std::vector<float>{-.8f, -.7f, 0, -.8f, .9f, 0, .6f, -.7f, 0}},
                    {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1}},
                    {"JOINTS_0", 4, std::vector<std::uint32_t>{0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0}},
                    {"WEIGHTS_0", 4,
                     std::vector<float>{1.f / 3, 1.f / 3, 1.f / 3, 0, 1.f / 3, 1.f / 3, 1.f / 3, 0,
                                        1.f / 3, 1.f / 3, 1.f / 3, 0}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    const auto gpu = upload_mesh(presentation.device(), mesh);
    MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                              {.2f, .6f, .1f, 1}};
    require(!material.double_sided, "Skin winding fixture must use back-face culling");
    MeshDraw draw(presentation, context, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_RGBA8_UNORM,
                  TEX_FORMAT_D32_FLOAT);
    MeshDraw shadow(presentation, context, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_UNKNOWN,
                    TEX_FORMAT_D32_FLOAT);
    TextureDesc desc;
    desc.Name = "FORGE signed skin acceptance";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    require(color && depth, "Skin draw targets unavailable");
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    Camera camera;
    camera.projection = std::uint32_t(CameraProjection::Orthographic);
    camera.orthographic_height = 2;
    camera.near_plane = .1;
    camera.far_plane = 10;
    AffineTransform eye;
    eye.m[3] = 1e12;
    auto view = camera_view(camera, eye, 64, 64);
    std::array<AffineTransform, 3> joints{}, inverse_bind{};
    auto reset = [&] {
        for (auto& joint : joints) {
            joint = AffineTransform{};
            joint.m[3] = 1e12;
            joint.m[11] = 2;
        }
    };
    reset();
    // This transform belongs to the skinned mesh node, which glTF ignores.
    AffineTransform ignored_node;
    ignored_node.m.fill(0);
    auto render = [&](const SkinPose& pose, bool depth_only = false) {
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0.f, 0.f, 64.f, 64.f, 0.f, 1.f};
        context->SetViewports(1, &viewport, 64, 64);
        if (depth_only) {
            context->SetRenderTargets(0, nullptr, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            shadow.draw(context, ignored_node, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                        &pose);
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        draw.draw(context, ignored_node, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                  &pose);
        return readback(presentation.device(), context, rtv);
    };
    auto count = [](const auto& pixels) {
        return std::count_if(pixels.begin(), pixels.end(),
                             [](const auto& p) { return p[1] > 100; });
    };
    auto pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, part.bounds);
    const auto positive = render(pose);
    require(count(positive) > 800, "Positive skin failed or used its singular mesh-node matrix");
    save(positive, 64, 64, images / "skin-positive.ppm");
    for (auto& joint : joints)
        joint.m[0] = -1;
    pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, part.bounds);
    const auto mirrored = render(pose);
    require(std::abs(count(mirrored) - count(positive)) < 8, "Reflected skin was back-face culled");
    std::size_t differences = 0;
    for (unsigned y = 0; y < 64; y++)
        for (unsigned x = 0; x < 64; x++)
            differences += (positive[y * 64 + x][1] > 100) != (mirrored[y * 64 + 63 - x][1] > 100);
    require(differences < 10, "Mirrored skin did not preserve asymmetric surface coverage");
    save(mirrored, 64, 64, images / "skin-mirrored.ppm");
    const auto shadowed = render(pose, true);
    require(count(shadowed) < 8, "Skinned shadow depth and color deformation diverged");
    reset();
    for (auto& joint : joints)
        joint.m[10] = 0;
    pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, part.bounds);
    const auto flat = render(pose);
    save(flat, 64, 64, images / "skin-rank-two.ppm");
    require(std::abs(count(flat) - count(positive)) < 8, "Rank-two skin lost a surviving surface");
    // A reflected camera projection changes the PSO front side too. Surviving
    // singular surfaces remain visible from both sides, with culling enabled.
    auto reversed_lens = camera;
    reversed_lens.flip_y = true;
    view = camera_view(reversed_lens, eye, 64, 64);
    const auto reversed_flat = render(pose);
    save(reversed_flat, 64, 64, images / "skin-rank-two-reversed-camera.ppm");
    require(std::abs(count(reversed_flat) - count(positive)) < 8,
            "Rank-two skin disagreed with reflected-camera raster winding");
    view = camera_view(camera, eye, 64, 64);
    reset();
    for (auto& joint : joints)
        joint.m[0] = 0;
    pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, part.bounds);
    require(count(render(pose)) == 0, "Collapsed skin emitted spurious triangles");
    for (const double scale : {-1., -.5, 0., .5, 1.}) {
        reset();
        for (auto& joint : joints)
            joint.m[0] = scale;
        const auto crossing =
            prepare_skin_pose(joints, inverse_bind, part.joint_palette, part.bounds);
        const auto pixels = render(crossing);
        require(std::abs(double(count(pixels)) - std::abs(scale) * double(count(positive))) < 35,
                "Sequential skin scale crossing lost or invented visible area");
    }
    reset();
    // All three palette matrices have positive determinant, but their equal
    // blend is -I/3. The far side is now front-facing; do not inspect joint signs.
    joints[0].m[5] = joints[0].m[10] = -1;
    joints[1].m[0] = joints[1].m[10] = -1;
    joints[2].m[0] = joints[2].m[5] = -1;
    pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, part.bounds);
    require(count(render(pose)) == 0, "Negative blend exposed its back side to the near camera");
    eye.m[0] = -1;
    eye.m[10] = -1;
    eye.m[11] = 4;
    view = camera_view(camera, eye, 64, 64);
    require(count(render(pose)) > 70, "Positive-joint negative blend hid its actual front side");
    save(render(pose), 64, 64, images / "skin-negative-blend.ppm");
    auto invalid = pose;
    invalid.palette.pop_back();
    bool rejected = false;
    try {
        render(invalid);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && count(render(pose)) > 70, "Invalid palette destroyed the usable skin draw");
}
