#pragma once
#include "mesh_draw.hpp"
#include "mesh_draw_bundle.hpp"
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
    forge::MeshDraw draw(presentation, context, gpu.lods[0].parts[0], material, {},
                         TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
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
    auto render = [&](auto& prepared, std::span<const forge::LightView> lights = {}) {
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
    {
        using namespace std::chrono_literals;
        forge::ResourcePool<forge::MeshAsset> cpu_mesh;
        forge::ResourcePool<forge::MaterialAsset> cpu_material;
        const forge::AssetRef<forge::MeshAsset> mesh_id{forge::AssetId::generate()};
        const forge::AssetRef<forge::MaterialAsset> material_id{forge::AssetId::generate()};
        auto mesh_ticket = cpu_mesh.request(
            mesh_id, std::string(64, 'a'), 1, [mesh, material_id](std::stop_token) {
                auto value = std::make_unique<forge::MeshResourceData>();
                value->mesh = mesh;
                value->materials = {{0, "default", material_id}};
                const auto bytes = value->resident_bytes();
                return forge::ResourceCandidate<forge::MeshAsset>{std::move(value), {bytes}};
            });
        auto material_ticket =
            cpu_material.request(material_id, std::string(64, 'b'), 1, [material](std::stop_token) {
                auto value = std::make_unique<forge::MaterialResourceData>();
                value->values = material;
                const auto bytes = value->resident_bytes();
                return forge::ResourceCandidate<forge::MaterialAsset>{std::move(value), {bytes}};
            });
        require(cpu_mesh.wait(mesh_ticket, 5s) && cpu_material.wait(material_ticket, 5s),
                "Bundle CPU fixtures failed");
        forge::asset_detail::PreparedModelDraw prepared;
        prepared.mesh = cpu_mesh.acquire(mesh_ticket);
        prepared.selection = forge::select_mesh_materials(prepared.mesh.get(), {});
        prepared.materials.emplace(material_id.id, cpu_material.acquire(material_ticket));
        forge::GpuResidency<forge::MeshAsset> meshes(presentation.device(), context, 4096);
        forge::GpuResidency<forge::TextureAsset> textures(presentation.device(), context, 4096);
        auto bundle = std::make_unique<forge::MeshDrawBundle>(
            presentation, context, prepared, meshes, textures, TEX_FORMAT_RGBA8_UNORM,
            TEX_FORMAT_D32_FLOAT);
        require(render(*bundle) == reference && bundle->mesh_identity() == prepared.mesh.identity(),
                "Complete bundle changed the selected mesh draw");
        auto invalid = prepared;
        invalid.materials.clear();
        bool rejected = false;
        try {
            auto replacement = std::make_unique<forge::MeshDrawBundle>(
                presentation, context, invalid, meshes, textures, TEX_FORMAT_RGBA8_UNORM,
                TEX_FORMAT_D32_FLOAT);
            bundle = std::move(replacement);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && render(*bundle) == reference,
                "Failed whole GPU candidate replaced the previous complete draw");
        cpu_mesh.close();
        cpu_material.close();
        require(render(*bundle) == reference, "GPU bundle retained invalid CPU-owner dependence");
        meshes.submit();
        textures.submit();
        bundle.reset(); // SRBs before leases, bundles before residency owners.
    }
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
    forge::MeshDraw lit(presentation, context, gpu.lods[0].parts[0], material, {},
                        TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
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
    world.m[10] = 1;
    auto coated_material = material;
    coated_material.parameters["clearcoatFactor"] = {forge::MaterialParameterType::Scalar, {1}};
    coated_material.parameters["clearcoatRoughnessFactor"] = {forge::MaterialParameterType::Scalar,
                                                              {.35f}};
    forge::MeshDraw coated(presentation, context, gpu.lods[0].parts[0], coated_material, {},
                           TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    const auto coating = render(coated, std::span(&light, 1));
    const auto coated_pixel = coating[16 * 32 + 16];
    require(coated_pixel != illuminated[16 * 32 + 16] &&
                coated_pixel != std::array<unsigned char, 4>{255, 0, 255, 255},
            "Clearcoat factor/roughness did not reach native layer shading");
    world.m[0] = -1;
    require(render(coated, std::span(&light, 1)) == coating,
            "Clearcoat response changed under reflection");
    world.m[0] = 1;
    // A valid authored frame on constant UVs: screen-gradient reconstruction
    // cannot substitute for the supplied tangent or silently lose tangent.w.
    world.m[10] = 1;
    auto film_material = material;
    film_material.parameters["iridescenceFactor"] = {forge::MaterialParameterType::Scalar, {1}};
    film_material.parameters["iridescenceThicknessMaximum"] = {forge::MaterialParameterType::Scalar,
                                                               {350}};
    forge::MeshDraw film(presentation, context, gpu.lods[0].parts[0], film_material, {},
                         TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    const auto film_image = render(film, std::span(&light, 1));
    require(film_image[16 * 32 + 16] != pixel &&
                film_image[16 * 32 + 16] != std::array<unsigned char, 4>{255, 0, 255, 255},
            "Iridescence did not change the native reflection response");
    auto zero_film_material = film_material;
    zero_film_material.parameters["iridescenceThicknessMaximum"].value[0] = 0;
    forge::MeshDraw zero_film(presentation, context, gpu.lods[0].parts[0], zero_film_material, {},
                              TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    require(render(zero_film, std::span(&light, 1)) == illuminated,
            "Zero film thickness changed the base material");
    auto sheen_material = material;
    sheen_material.parameters["sheenColorFactor"] = {forge::MaterialParameterType::LinearColor3,
                                                     {.8f, .3f, .1f}};
    sheen_material.parameters["sheenRoughnessFactor"] = {forge::MaterialParameterType::Scalar,
                                                         {.6f}};
    forge::MeshDraw sheen(presentation, context, gpu.lods[0].parts[0], sheen_material, {},
                          TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    const auto sheen_image = render(sheen, std::span(&light, 1));
    require(sheen_image[16 * 32 + 16] != pixel &&
                sheen_image[16 * 32 + 16] != std::array<unsigned char, 4>{255, 0, 255, 255},
            "Sheen factor/roughness/native lookup did not affect the base layer");
    sheen_material.parameters["sheenRoughnessFactor"].value[0] = 0;
    forge::MeshDraw zero_sheen(presentation, context, gpu.lods[0].parts[0], sheen_material, {},
                               TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    require(render(zero_sheen, std::span(&light, 1)) == illuminated,
            "Zero sheen roughness did not follow the native limiting behavior");
    material.textures["normalTexture"].semantic = forge::TextureSemantic::Normal;
    forge::TextureData normal;
    normal.width = normal.height = 1;
    normal.semantic = forge::TextureSemantic::Normal;
    normal.subresources = {{std::byte{128}, std::byte{230}, std::byte{230}, std::byte{255}}};
    auto normal_gpu = forge::upload_texture(presentation.device(), normal);
    forge::MeshDraw::Textures bindings;
    bindings["normalTexture"] = normal_gpu->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    part.streams.push_back({"TEXCOORD_0", 2, std::vector<float>(8, 0)});
    part.streams.push_back(
        {"TANGENT", 4, std::vector<float>{0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1}});
    mesh.lods[0].parts[0] = part;
    auto tangent_gpu = forge::upload_mesh(presentation.device(), mesh);
    forge::MeshDraw tangent_draw(presentation, context, tangent_gpu.lods[0].parts[0], material,
                                 bindings, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    light.direction = {.8, 0, .6};
    const auto handed = render(tangent_draw, std::span(&light, 1));
    auto& tangents = std::get<std::vector<float>>(mesh.lods[0].parts[0].streams.back().values);
    for (unsigned i = 3; i < tangents.size(); i += 4)
        tangents[i] = 1;
    auto opposite_gpu = forge::upload_mesh(presentation.device(), mesh);
    forge::MeshDraw opposite_draw(presentation, context, opposite_gpu.lods[0].parts[0], material,
                                  bindings, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    const auto opposite = render(opposite_draw, std::span(&light, 1));
    require(handed[16 * 32 + 16][1] > opposite[16 * 32 + 16][1] + 15,
            "Normal-map draw ignored the authored tangent frame or tangent.w");
    auto anisotropic_material = material;
    anisotropic_material.textures.clear();
    anisotropic_material.parameters["roughnessFactor"] = {forge::MaterialParameterType::Scalar,
                                                          {.35f}};
    anisotropic_material.parameters["metallicFactor"] = {forge::MaterialParameterType::Scalar, {1}};
    anisotropic_material.parameters["anisotropyStrength"] = {forge::MaterialParameterType::Scalar,
                                                             {.9f}};
    forge::MeshDraw aniso(presentation, context, opposite_gpu.lods[0].parts[0],
                          anisotropic_material, {}, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    const auto directional = render(aniso, std::span(&light, 1));
    anisotropic_material.parameters["anisotropyRotation"] = {forge::MaterialParameterType::Scalar,
                                                             {1.57079632679f}};
    forge::MeshDraw rotated_aniso(presentation, context, opposite_gpu.lods[0].parts[0],
                                  anisotropic_material, {}, TEX_FORMAT_RGBA8_UNORM,
                                  TEX_FORMAT_D32_FLOAT);
    const auto rotated = render(rotated_aniso, std::span(&light, 1));
    require(directional[16 * 32 + 16] != rotated[16 * 32 + 16] &&
                rotated[16 * 32 + 16] != std::array<unsigned char, 4>{255, 0, 255, 255},
            "Anisotropy direction/rotation did not reach native shading");
    bool missing_frame_rejected = false;
    try {
        forge::MeshDraw invalid(presentation, context, gpu.lods[0].parts[0], anisotropic_material,
                                {}, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    } catch (const std::exception&) {
        missing_frame_rejected = true;
    }
    require(missing_frame_rejected, "Anisotropic material accepted a mesh with no tangent space");
}
