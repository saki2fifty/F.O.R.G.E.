#pragma once
#include "engine_render_resource.hpp"
#include "environment_sky.hpp"
#include "mesh_draw.hpp"
#include "mesh_draw_bundle.hpp"
#include "render_bounds.hpp"
#include "render_sort.hpp"
#include <type_traits>
void check_mesh_draw(forge::DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& images) {
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
    auto render = [&](auto& prepared, std::span<const forge::LightView> lights = {},
                      const forge::EnvironmentLighting* environment = nullptr, unsigned lod = 0) {
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0.f, 0.f, 32.f, 32.f, 0.f, 1.f};
        context->SetViewports(1, &viewport, 32, 32);
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(prepared)>,
                                     forge::MeshDrawBundle>)
            prepared.draw(context, world, view, lights, environment, lod);
        else
            prepared.draw(context, world, view, lights, environment);
        return readback(presentation.device(), context, rtv);
    };
    const auto reference = render(draw);
    save(reference, 32, 32, images / "mesh-prepared-positive.ppm");
    require(reference[16 * 32 + 16] == std::array<unsigned char, 4>{51, 153, 26, 255} ||
                reference[16 * 32 + 16] == std::array<unsigned char, 4>{51, 153, 25, 255},
            "Prepared unlit mesh did not render with camera-relative placement");
    for (const bool indexed : {false, true}) {
        auto alternate = mesh;
        auto& converted = alternate.lods[0].parts[0];
        converted.vertices = indexed ? 65537u : unsigned(part.indices.size());
        for (auto& stream : converted.streams) {
            const auto source = std::get<std::vector<float>>(stream.values);
            auto& values = std::get<std::vector<float>>(stream.values);
            values.clear();
            for (unsigned i = 0; i < converted.vertices; ++i) {
                const auto vertex = indexed ? (i == 65536 ? 2u : std::min(i, 3u)) : part.indices[i];
                values.insert(values.end(), source.begin() + vertex * stream.components,
                              source.begin() + (vertex + 1) * stream.components);
            }
        }
        if (indexed)
            converted.indices = {0, 1, 65536, 0, 65536, 3};
        else
            converted.indices.clear();
        converted.bounds = forge::mesh_bounds(converted);
        auto uploaded = forge::upload_mesh(presentation.device(), alternate);
        require(uploaded.lods[0].parts[0].index_type == (indexed ? VT_UINT32 : VT_UNDEFINED),
                "Alternate draw did not exercise the intended index representation");
        forge::MeshDraw alternate_draw(presentation, context, uploaded.lods[0].parts[0], material,
                                       {}, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        const auto pixels = render(alternate_draw);
        require(pixels == reference,
                "Nonindexed or uint32 draw differs from equivalent compact indexed geometry");
        save(pixels, 32, 32, images / (indexed ? "mesh-index32.ppm" : "mesh-nonindexed.ppm"));
    }
    {
        using namespace std::chrono_literals;
        forge::ResourcePool<forge::MeshAsset> cpu_mesh;
        forge::ResourcePool<forge::MaterialAsset> cpu_material;
        const forge::AssetRef<forge::MeshAsset> mesh_id{forge::AssetId::generate()};
        const forge::AssetRef<forge::MaterialAsset> material_id{forge::AssetId::generate()};
        const forge::AssetRef<forge::MaterialAsset> alternate_id{forge::AssetId::generate()};
        const forge::AssetRef<forge::MaterialVariantAsset> variant_id{forge::AssetId::generate()};
        auto lod_mesh = mesh;
        lod_mesh.lods.push_back({.25f, {part}});
        lod_mesh.lods[1].parts[0].indices = {0, 1, 2};
        auto mesh_ticket = cpu_mesh.request(
            mesh_id, std::string(64, 'a'), 1,
            [lod_mesh, material_id, alternate_id, variant_id](std::stop_token) {
                auto value = std::make_unique<forge::MeshResourceData>();
                value->mesh = lod_mesh;
                value->materials = {{0, "default", material_id}};
                value->variants = {
                    {variant_id, "Red variant", {{{0, 0}, alternate_id}, {{1, 0}, alternate_id}}}};
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
        const auto selected_lod = forge::select_mesh_lod(prepared.mesh->mesh, .25f);
        const auto distant = render(*bundle, {}, nullptr, unsigned(selected_lod));
        const auto coverage = [](const auto& pixels) {
            return std::count_if(pixels.begin(), pixels.end(),
                                 [](const auto& p) { return p[0] || p[1] || p[2]; });
        };
        require(selected_lod == 1 && coverage(distant) > 0 &&
                    coverage(distant) < coverage(reference),
                "Native LOD selection did not draw its authored lower-detail geometry");
        save(distant, 32, 32, images / "mesh-lod-far.ppm");
        save(reference, 32, 32, images / "mesh-lod-close.ppm");
        auto alternate = material;
        alternate.parameters["baseColorFactor"] = {forge::MaterialParameterType::LinearColor4,
                                                   {1, 0, 0, 1}};
        const auto alternate_ticket = cpu_material.request(
            alternate_id, std::string(64, 'c'), 1, [alternate](std::stop_token) {
                auto value = std::make_unique<forge::MaterialResourceData>();
                value->values = alternate;
                const auto bytes = value->resident_bytes();
                return forge::ResourceCandidate<forge::MaterialAsset>{std::move(value), {bytes}};
            });
        require(cpu_material.wait(alternate_ticket, 5s), "Variant material fixture did not load");
        auto variant_prepared = prepared;
        variant_prepared.selection =
            forge::select_mesh_materials(prepared.mesh.get(), {}, variant_id);
        variant_prepared.materials.emplace(alternate_id.id, cpu_material.acquire(alternate_ticket));
        {
            forge::MeshDrawBundle variant_draw(presentation, context, variant_prepared, meshes,
                                               textures, TEX_FORMAT_RGBA8_UNORM,
                                               TEX_FORMAT_D32_FLOAT);
            const auto pixels = render(variant_draw);
            require(pixels[16 * 32 + 16] == std::array<unsigned char, 4>{255, 0, 0, 255} &&
                        variant_draw.mesh_identity() == bundle->mesh_identity(),
                    "Native material variant failed to switch surface while reusing geometry");
            const auto low_pixels = render(variant_draw, {}, nullptr, 1);
            require(std::any_of(low_pixels.begin(), low_pixels.end(),
                                [](const auto& p) { return p[0] == 255 && p[1] == 0; }),
                    "Native lower LOD ignored material variant");
            save(pixels, 32, 32, images / "mesh-material-variant.ppm");
        }
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
    {
        using namespace forge;
        using namespace forge::asset_detail;
        ResourcePool<MeshAsset> cpu_mesh;
        ResourcePool<MaterialAsset> cpu_material;
        ResourcePool<TextureAsset> cpu_texture;
        auto catalog = std::make_shared<AssetCatalog>(images);
        auto preview = std::make_shared<MaterialPreviewSelection>();
        preview->asset = {AssetId::generate()};
        preview->revision = std::string(64, 'f');
        preview->generation = 1;
        preview->data = engine_material_resource(engine_material(EngineMaterial::LegacyBlockout));
        preview->data.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        preview->data.textures["baseColorTexture"] = {AssetId::generate()};
        ModelDrawCandidate candidate(images, catalog, 1, engine_primitive(0),
                                     {{"surface", preview->asset}}, cpu_mesh, preview, {}, true);
        auto finish = [&] {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do {
                cpu_mesh.pump();
                cpu_material.pump();
                cpu_texture.pump();
                candidate.advance(1, cpu_mesh, cpu_material, cpu_texture);
                if (candidate.state() != ResourceState::Loading)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (std::chrono::steady_clock::now() < until);
            require(candidate.ready() && candidate.ready()->has_fallbacks(),
                    "Native missing texture candidate was not prepared");
        };
        finish();
        GpuResidency<MeshAsset> fallback_meshes(presentation.device(), context, 8 * 1024 * 1024);
        GpuResidency<TextureAsset> fallback_textures(presentation.device(), context, 1024 * 1024);
        MeshDrawBundle checker(presentation, context, *candidate.ready(), fallback_meshes,
                               fallback_textures, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        const auto checker_pixels = render(checker);
        require(candidate.error_surface("bounded native fallback fixture", cpu_material),
                "Initial surface could not select error material");
        finish();
        MeshDrawBundle error(presentation, context, *candidate.ready(), fallback_meshes,
                             fallback_textures, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        const auto error_pixels = render(error);
        require(checker_pixels != error_pixels &&
                    std::any_of(checker_pixels.begin(), checker_pixels.end(),
                                [](const auto& p) { return p[0] > 20 && p[1] == 0 && p[2] > 20; }),
                "Missing texture checker did not produce a visible distinct native surface");
        save(checker_pixels, 32, 32, images / "mesh-missing-texture.ppm");
        save(error_pixels, 32, 32, images / "mesh-error-surface.ppm");
        fallback_meshes.submit();
        fallback_textures.submit();
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
    {
        require(draw.supports_instances(), "Static mesh did not prepare native instancing");
        std::array<forge::MeshDraw::Instance, 2> instances;
        for (unsigned i = 0; i < 2; ++i) {
            instances[i].world = world;
            instances[i].world.m[0] = instances[i].world.m[5] = .4;
            instances[i].world.m[3] += i ? .7 : -.7;
            instances[i].legacy_tint =
                i ? std::array<float, 3>{1, .2f, .3f} : std::array<float, 3>{.2f, 1, .3f};
        }
        auto render_instances = [&](bool batch) {
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float black[]{0, 0, 0, 1};
            context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            if (batch)
                draw.draw(context, instances[0].world, view, {}, nullptr, nullptr, nullptr, {},
                          nullptr, {}, nullptr, instances);
            else
                for (const auto& instance : instances)
                    draw.draw(context, instance.world, view, {}, nullptr, &*instance.legacy_tint);
            return readback(presentation.device(), context, rtv);
        };
        for (unsigned parity = 0; parity < 3; ++parity) {
            for (auto& instance : instances) {
                instance.world.m[0] = parity == 1 ? -.4 : .4;
                instance.world.m[10] = parity == 2 ? 0 : 1;
            }
            const auto expected = render_instances(false);
            const auto batched = render_instances(true);
            require(
                batched == expected && batched[16 * 32 + 10][1] > 20 &&
                    batched[16 * 32 + 21][0] > batched[16 * 32 + 10][0],
                "Native instances changed placement, per-instance color or reflection/collapse");
            save(batched, 32, 32, images / ("mesh-instances-" + std::to_string(parity) + ".ppm"));
        }
        instances[0].world.m[10] = instances[1].world.m[10] = 1;
        instances[1].world.m[0] = -.4;
        bool rejected = false;
        try {
            render_instances(true);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Mixed instance winding silently chose one raster policy");
    }
    {
        auto blended = material;
        blended.alpha = forge::MaterialAlpha::Blend;
        blended.depth_write = false;
        blended.parameters["baseColorFactor"].value = {1, 0, 0, .5f};
        forge::MeshDraw red(presentation, context, gpu.lods[0].parts[0], blended, {},
                            TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        blended.parameters["baseColorFactor"].value = {0, 0, 1, .5f};
        forge::MeshDraw blue(presentation, context, gpu.lods[0].parts[0], blended, {},
                             TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        forge::RenderSortKey near_item, far_item;
        near_item.alpha = far_item.alpha = forge::MaterialAlpha::Blend;
        near_item.depth = 1.5;
        far_item.depth = 2;
        std::vector<forge::RenderSortKey> queue{near_item, far_item};
        std::sort(queue.begin(), queue.end(), forge::render_key_less);
        world.m[11] = 3;
        render(draw);
        // Readback changes resource states; bind the render targets again.
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        for (const auto& item : queue) {
            world.m[11] = item.depth;
            (item.depth == 2 ? red : blue).draw(context, world, view, {});
        }
        auto image = readback(presentation.device(), context, rtv);
        auto value = image[16 * 32 + 16];
        require(std::abs(int(value[0]) - 77) <= 2 && std::abs(int(value[1]) - 38) <= 2 &&
                    std::abs(int(value[2]) - 134) <= 2 && value[3] == 255,
                "Sorted straight-alpha layers did not compose over opaque depth");
        auto masked = material;
        masked.alpha = forge::MaterialAlpha::Mask;
        masked.parameters["baseColorFactor"].value = {1, 1, 1, .4f};
        forge::MeshDraw mask(presentation, context, gpu.lods[0].parts[0], masked, {},
                             TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        world.m[11] = 1;
        mask.draw(context, world, view, {});
        require(readback(presentation.device(), context, rtv) == image,
                "Masked alpha below cutoff wrote color/depth");
        world.m[11] = 2;
    }
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
    {
        using namespace std::chrono_literals;
        forge::TextureData source;
        source.width = 4;
        source.height = 2;
        source.format = forge::TextureFormat::RGBA32Float;
        source.semantic = forge::TextureSemantic::HdrColor;
        source.subresources = {std::vector<std::byte>(4 * 2 * 16)};
        const float radiance[4]{.25f, .5f, .75f, 1};
        for (unsigned i = 0; i < 8; ++i)
            std::memcpy(source.subresources[0].data() + i * 16, radiance, 16);
        forge::ResourcePool<forge::TextureAsset> pool;
        auto ticket = pool.request(forge::AssetRef<forge::TextureAsset>{forge::AssetId::generate()},
                                   std::string(64, 'c'), 1, [source](std::stop_token) {
                                       auto value = std::make_unique<forge::TextureData>(source);
                                       return forge::ResourceCandidate<forge::TextureAsset>{
                                           std::move(value), {source.resident_bytes()}};
                                   });
        require(pool.wait(ticket, 5s), "Environment CPU fixture failed");
        auto cpu = pool.acquire(ticket);
        forge::EnvironmentRealization policy{&presentation, 4, 8, 32, 32};
        forge::EnvironmentResidency environments(presentation.device(), context, 1024 * 1024, 8,
                                                 policy);
        auto maps = environments.acquire(cpu);
        auto same = environments.acquire(cpu);
        require(&maps.get() == &same.get() && environments.statistics().resident == 1 &&
                    environments.statistics().payload_bytes == 9056 && policy.bytes(source) == 9056,
                "Environment revision was reconvolved or escaped the GPU budget");
        forge::EnvironmentLighting environment{&maps.get(), 1, 0};
        const auto indirect = render(lit, {}, &environment);
        save(indirect, 32, 32, images / "mesh-native-ibl.ppm");
        require(indirect[16 * 32 + 16] != dark[16 * 32 + 16] &&
                    indirect[16 * 32 + 16][1] > indirect[16 * 32 + 16][0] &&
                    indirect[16 * 32 + 16] != std::array<unsigned char, 4>{255, 0, 255, 255},
                "Native IBL did not illuminate a mesh without punctual lights");
        environment.intensity = 0;
        require(render(lit, {}, &environment) == dark, "Disabled IBL still contributed light");
        environment.intensity = 1;
        environment.rotation = 1.3;
        const auto rotated = render(lit, {}, &environment);
        for (unsigned channel = 0; channel < 3; ++channel)
            require(std::abs(int(rotated[16 * 32 + 16][channel]) -
                             int(indirect[16 * 32 + 16][channel])) <= 1,
                    "Constant environment changed radiance with rotation");
        // Reset SRBs to default maps before closing this derived-resource owner.
        render(lit);
        forge::EnvironmentSky sky(presentation, TEX_FORMAT_RGBA8_UNORM);
        sky.select(maps);
        forge::SceneEnvironment sky_settings;
        auto sky_image = [&](const forge::CameraView& sky_view, bool geometry) {
            if (geometry)
                (void)render(draw);
            else {
                context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float clear[4]{0, 0, 0, 1};
                context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            Diligent::Viewport area{0, 0, 32, 32, 0, 1};
            context->SetViewports(1, &area, 32, 32);
            sky.draw(context, sky_view, sky_settings);
            return readback(presentation.device(), context, rtv);
        };
        const auto sky_pixels = sky_image(view, false);
        save(sky_pixels, 32, 32, images / "native-environment-sky.ppm");
        for (const auto index : {0, 16 * 32 + 16, 1023}) {
            const auto pixel = sky_pixels[index];
            require(pixel[0] >= 63 && pixel[0] <= 65 && pixel[1] >= 127 && pixel[1] <= 129 &&
                        pixel[2] >= 190 && pixel[2] <= 192,
                    "Native sky radiance changed color space or failed far-depth coverage");
        }
        auto infinite = camera;
        infinite.infinite_far = true;
        require(sky_image(forge::camera_view(infinite, camera_world, 32, 32), false) == sky_pixels,
                "Infinite-far sky reconstruction changed radiance");
        require(sky_image(view, true)[16 * 32 + 16] == reference[16 * 32 + 16],
                "Sky overwrote foreground geometry");
        sky_settings.sky = false;
        require(sky_image(view, false)[16 * 32 + 16] == std::array<unsigned char, 4>{0, 0, 0, 255},
                "Hidden sky still drew the environment background");
        sky.select({});
        environments.submit();
        maps = {};
        same = {};
        environments.unload(cpu.identity());
        context->WaitForIdle();
        environments.collect();
        require(environments.statistics().payload_bytes == 0,
                "Environment resources failed to retire");
    }
    const auto illuminated = render(lit, std::span(&light, 1));
    const auto pixel = illuminated[16 * 32 + 16];
    require(pixel[1] > pixel[0] && pixel[0] > pixel[2] && pixel[2] > 0 && pixel[3] == 255,
            "Native PBR material/light binding failed or produced diagnostic color");
    world.m[0] = -1;
    const auto reflected = render(lit, std::span(&light, 1));
    save(reflected, 32, 32, images / "mesh-pbr-reflected.ppm");
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
    save(coating, 32, 32, images / "mesh-clearcoat.ppm");
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
    save(film_image, 32, 32, images / "mesh-iridescence.ppm");
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
    save(sheen_image, 32, 32, images / "mesh-sheen.ppm");
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
    save(directional, 32, 32, images / "mesh-anisotropy.ppm");
    save(rotated, 32, 32, images / "mesh-anisotropy-rotated.ppm");
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
    {
        forge::MaterialData layered;
        layered.model = "forge.gltf.metallic-roughness.v1";
        const auto declaration = forge::prepare_pbr_material(layered);
        forge::MeshDraw::Textures textures;
        unsigned count = 0;
        for (const auto& [role, layout] : declaration.layout.textures) {
            if (role == "transmissionTexture" || role == "thicknessTexture")
                continue;
            auto& slot = layered.textures[role];
            slot.semantic = layout.semantic;
            slot.sampler.lod_bias = .01f * count++;
            forge::TextureData tex;
            tex.width = tex.height = 1;
            tex.semantic = layout.semantic;
            tex.format = layout.semantic == forge::TextureSemantic::Color
                             ? forge::TextureFormat::RGBA8Srgb
                             : forge::TextureFormat::RGBA8;
            tex.subresources = {std::vector<std::byte>(4, std::byte{255})};
            if (layout.semantic == forge::TextureSemantic::Normal)
                tex.subresources[0][0] = tex.subresources[0][1] = std::byte{128};
            auto native = forge::upload_texture(presentation.device(), tex);
            textures[role] = native->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        layered.parameters["clearcoatFactor"] = {forge::MaterialParameterType::Scalar, {.3f}};
        layered.parameters["clearcoatRoughnessFactor"] = {forge::MaterialParameterType::Scalar,
                                                          {.4f}};
        layered.parameters["iridescenceFactor"] = {forge::MaterialParameterType::Scalar, {.5f}};
        layered.parameters["sheenColorFactor"] = {forge::MaterialParameterType::LinearColor3,
                                                  {.3f, .2f, .1f}};
        layered.parameters["sheenRoughnessFactor"] = {forge::MaterialParameterType::Scalar, {.6f}};
        layered.parameters["anisotropyStrength"] = {forge::MaterialParameterType::Scalar, {.5f}};
        forge::MeshDraw all(presentation, context, opposite_gpu.lods[0].parts[0], layered, textures,
                            TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        const auto image = render(all, std::span(&light, 1));
        save(image, 32, 32, images / "mesh-all-reflection-textures.ppm");
        require(count == 15 &&
                    image[16 * 32 + 16] != std::array<unsigned char, 4>{255, 0, 255, 255} &&
                    image[16 * 32 + 16] != std::array<unsigned char, 4>{0, 0, 0, 255},
                "Combined material textures/samplers/layers failed native shading");
    }
}
