#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "mesh_draw_bundle.hpp"
#include "particle_gpu_order.hpp"
#include <memory>
#include <numeric>
namespace forge::test_particle_palette {
using namespace Diligent;
using test_bloom::check;
using Row = std::array<float, 4>;
using Packet = std::array<Row, 7>;
inline constexpr unsigned extent = 64;
inline RefCntAutoPtr<IBuffer> buffer(IRenderDevice* device, const void* data, Uint64 bytes,
                                     Diligent::BIND_FLAGS bindings, unsigned stride = 0) {
    BufferDesc desc;
    desc.Name = "FORGE Mesh palette fixture";
    desc.Size = bytes;
    desc.Usage = USAGE_IMMUTABLE;
    desc.BindFlags = bindings;
    if (stride) {
        desc.Mode = BUFFER_MODE_STRUCTURED;
        desc.ElementByteStride = stride;
    }
    BufferData initial{data, bytes};
    RefCntAutoPtr<IBuffer> result;
    device->CreateBuffer(desc, &initial, &result);
    check(bool(result), "Mesh palette fixture buffer unavailable");
    return result;
}
inline RefCntAutoPtr<ITexture> source_texture(IRenderDevice* device, unsigned index) {
    const std::vector<Row> pixels = index ? std::vector<Row>{{.1f, .9f, .2f, .8f},
                                                             {.8f, .2f, .7f, .4f},
                                                             {.6f, .7f, .1f, .6f},
                                                             {.1f, .2f, 1.f, .2f}}
                                          : std::vector<Row>{{1.f, .1f, .2f, .9f},
                                                             {.2f, .8f, .7f, .7f},
                                                             {.8f, .3f, .1f, .5f},
                                                             {.4f, .2f, .8f, .3f}};
    return test_bloom::texture(device, 2, 2, pixels);
}
template <class T>
ResourceLease<T> resource(ResourcePool<T>& pool, typename ResourceTraits<T>::Data data) {
    const auto ticket =
        pool.request(AssetRef<T>{AssetId::generate()}, std::string(64, 'c'), 1,
                     [data = std::move(data)](std::stop_token) mutable {
                         auto candidate =
                             std::make_unique<typename ResourceTraits<T>::Data>(std::move(data));
                         const auto bytes = candidate->resident_bytes();
                         return ResourceCandidate<T>{std::move(candidate), {bytes}};
                     });
    check(pool.wait(ticket, std::chrono::seconds(5)), "Palette CPU resource did not load");
    return pool.acquire(ticket);
}
inline void binding_scope(DiligentPresentation& presentation, IDeviceContext* context,
                          const GpuMeshPart& mesh, ITexture* color, ITexture* depth,
                          const CameraView& view) {
    auto* device = presentation.device();
    const auto cube = [&](Row value) {
        TextureDesc desc;
        desc.Name = "FORGE binding scope environment";
        desc.Type = RESOURCE_DIM_TEX_CUBE;
        desc.Width = desc.Height = desc.MipLevels = 1;
        desc.ArraySize = 6;
        desc.Format = TEX_FORMAT_RGBA32_FLOAT;
        desc.Usage = USAGE_IMMUTABLE;
        desc.BindFlags = BIND_SHADER_RESOURCE;
        std::array<TextureSubResData, 6> faces{};
        for (auto& face : faces) {
            face.pData = value.data();
            face.Stride = sizeof(value);
        }
        Diligent::TextureData initial{faces.data(), Uint32(faces.size())};
        RefCntAutoPtr<ITexture> texture;
        device->CreateTexture(desc, &initial, &texture);
        check(bool(texture), "Binding scope environment unavailable");
        return texture;
    };
    const auto first = cube({.7f, .15f, .3f, 1}), second = cube({.1f, .4f, .8f, 1});
    MaterialData material;
    material.model = "forge.gltf.metallic-roughness.v1";
    material.alpha = MaterialAlpha::Blend;
    material.depth_write = false;
    material.double_sided = true;
    material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                              {.8f, .7f, .5f, .35f}};
    material.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.8f}};
    MeshDraw left(presentation, context, mesh, material, {}, TEX_FORMAT_RGBA32_FLOAT,
                  TEX_FORMAT_D32_FLOAT);
    material.parameters["baseColorFactor"].value = {.2f, .8f, .6f, .6f};
    MeshDraw right(presentation, context, mesh, material, {}, TEX_FORMAT_RGBA32_FLOAT,
                   TEX_FORMAT_D32_FLOAT);
    GpuEnvironment maps;
    const EnvironmentLighting lighting{&maps};
    const Row background{.02f, .04f, .08f, 1};
    constexpr unsigned draws = 48;
    for (unsigned mode = 0; mode < 5; ++mode) {
        const auto render = [&](bool reuse) {
            maps.diffuse = maps.specular = maps.sheen = first;
            auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearRenderTarget(rtv, background.data(),
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Viewport viewport{0, 0, float(extent), float(extent), 0, 1};
            context->SetViewports(1, &viewport, extent, extent);
            MeshDraw::BindingScope scope;
            for (unsigned i = 0; i < draws; ++i) {
                if (!reuse)
                    scope.invalidate();
                if (i == draws / 2 && mode == 1)
                    left.bind_environment(&maps);
                if (i == draws / 2 && mode == 2)
                    maps.diffuse = maps.specular = maps.sheen = second;
                AffineTransform world;
                world.m[0] = mode == 3 && i % 2 ? -1 : 1;
                world.m[3] = (double(i % 8) - 3.5) * .18;
                world.m[7] = (double(i / 8) - 2.5) * .18;
                world.m[11] = 3 + .002 * i;
                const std::array tint{.4f + .1f * float(i % 3), .5f + .1f * float(i % 2), .9f};
                auto& selected = mode == 4 && i % 2 ? right : left;
                selected.draw(context, world, view, {}, &lighting, &tint, nullptr, {}, nullptr, {},
                              nullptr, {}, {}, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {},
                              nullptr, {}, nullptr, nullptr, &scope);
            }
            const unsigned expected = !reuse || mode >= 3 ? draws : mode ? 2 : 1;
            check(scope.commits == expected && scope.commits + scope.reuses == draws,
                  "Binding scope lost a required commit or failed to reuse identical resources");
            auto pixels = test_bloom::read(device, context, color);
            context->FinishFrame();
            device->ReleaseStaleResources();
            return pixels;
        };
        const auto baseline = render(false), reused = render(true);
        unsigned covered = 0;
        for (unsigned i = 0; i < baseline.size(); ++i) {
            covered += baseline[i] != background;
            for (unsigned c = 0; c < 4; ++c)
                check(std::isfinite(reused[i][c]) &&
                          std::abs(reused[i][c] - baseline[i][c]) < 3e-5f,
                      "Binding reuse changed an independently committed reference image");
        }
        check(covered > 100, "Binding scope comparison has insufficient visible coverage");
    }
    std::cout << "MESH_BINDING_SCOPE cases=5 frames=10 draws=48; measured full-commit baseline, "
                 "uniform changes, external binding, environment-view swap, parity and owner\n";
}
inline void bundle_preparation(DiligentPresentation& presentation, IDeviceContext* context,
                               const MeshData& source, const GpuMeshPart& reference_mesh,
                               ITexture* color, ITexture* depth) {
    auto* device = presentation.device();
    ResourcePool<MeshAsset> cpu_meshes;
    ResourcePool<MaterialAsset> cpu_materials;
    MaterialResourceData base;
    base.values.model = "forge.gltf.unlit.v1";
    base.values.alpha = MaterialAlpha::Blend;
    base.values.depth_write = false;
    base.values.double_sided = true;
    base.values.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                 {.8f, .2f, .1f, .4f}};
    const auto material = resource(cpu_materials, base);
    MeshResourceData geometry;
    geometry.mesh = source;
    geometry.materials = {{0, "surface", {material.identity().asset}}};
    asset_detail::PreparedModelDraw prepared;
    prepared.mesh = resource(cpu_meshes, geometry);
    prepared.selection = select_mesh_materials(prepared.mesh.get(), {});
    prepared.materials.emplace(material.identity().asset, material);
    GpuResidency<MeshAsset> meshes(device, context, 1024 * 1024);
    GpuResidency<TextureAsset> textures(device, context, 1024 * 1024);
    MeshDrawBundle bundle(presentation, context, prepared, meshes, textures,
                          TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT);
    const std::vector parameters{MaterialParameterOverride{
        "surface", "baseColorFactor", {MaterialParameterType::LinearColor4, {.2f, .8f, .1f, .6f}}}};
    const std::array members{MeshDrawBundle::PaletteMember{&bundle, {}},
                             MeshDrawBundle::PaletteMember{&bundle, parameters}};
    auto palette = bundle.prepare_palette(context, 0, 0, members);
    auto green = base.values;
    green.parameters["baseColorFactor"] = parameters.front().parameter;
    MeshDraw red(presentation, context, reference_mesh, base.values, {}, TEX_FORMAT_RGBA32_FLOAT,
                 TEX_FORMAT_D32_FLOAT);
    MeshDraw alternate(presentation, context, reference_mesh, green, {}, TEX_FORMAT_RGBA32_FLOAT,
                       TEX_FORMAT_D32_FLOAT);
    std::array<Packet, 2> packets{};
    for (unsigned i = 0; i < packets.size(); ++i) {
        packets[i][0] = {1, 0, 0, 0};
        packets[i][1] = {0, 1, 0, 0};
        packets[i][2] = {0, 0, 1, float(3 - i)};
        packets[i][3] = {1, 0, 0, 0};
        packets[i][4] = {0, 1, 0, 0};
        packets[i][5] = {0, 0, 1, 0};
        packets[i][6] = {1, 1, 1, 2};
    }
    auto instances = buffer(device, packets.data(), sizeof(packets), BIND_VERTEX_BUFFER);
    const std::array<std::uint32_t, 2> indices{0, 1};
    auto tags = buffer(device, indices.data(), sizeof(indices), BIND_VERTEX_BUFFER);
    const auto view = camera_view(Camera{}, {}, extent, extent);
    const auto render = [&](bool selected) {
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Row background{.02f, .04f, .08f, 1};
        context->ClearRenderTarget(rtv, background.data(),
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(extent), float(extent), 0, 1};
        context->SetViewports(1, &viewport, extent, extent);
        if (selected) {
            const MeshDraw::InstanceBuffer input{instances, 0, 2, tags, 0};
            palette->draw(context, {}, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                          nullptr, {}, {}, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {},
                          nullptr, {}, nullptr, &input);
        } else {
            AffineTransform world;
            world.m[11] = 3;
            red.draw(context, world, view, {});
            world.m[11] = 2;
            alternate.draw(context, world, view, {});
        }
        auto pixels = test_bloom::read(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    auto invalid = parameters;
    invalid.front().parameter.value[3] = 2;
    const std::array rejected{members.front(), MeshDrawBundle::PaletteMember{&bundle, invalid}};
    bool refused = false;
    try {
        (void)bundle.prepare_palette(context, 0, 0, rejected);
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "Palette bundle accepted an invalid projected Material value");
    const auto expected = render(false), actual = render(true);
    for (unsigned i = 0; i < actual.size(); ++i)
        for (unsigned c = 0; c < 4; ++c)
            check(std::isfinite(actual[i][c]) && std::abs(actual[i][c] - expected[i][c]) < 3e-5f,
                  "Bundle palette lost projected values or the retained draw after refusal");
    check(actual[extent * extent / 2 + extent / 2] != Row{.02f, .04f, .08f, 1},
          "Bundle palette comparison did not render geometry");
    check(material->values == base.values, "Palette projection changed shared Material data");
    context->WaitForIdle();
    std::cout << "MATERIAL_PALETTE_BUNDLE frames=2; physical leases, sparse values and "
                 "refusal/recovery\n";
}
// Compare actual common MeshDraw lighting/geometry with independently ordered
// per-Material CPU instance submissions, including an incorrect-index control.
inline void exercise(DiligentPresentation& presentation, IDeviceContext* context,
                     bool bindings_only = false) {
    auto* device = presentation.device();
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-1, -1, 0, -1, 1, 0, 1, 1, 0, 1, -1, 0}},
        {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
        {"TANGENT", 4, std::vector<float>{1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1}},
        {"TEXCOORD_0", 2, std::vector<float>{0, 0, 0, 1, 1, 1, 1, 0}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    auto gpu = upload_mesh(device, mesh);
    TextureDesc target;
    target.Name = "FORGE Mesh palette regression";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = target.Height = extent;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(target, nullptr, &color);
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(target, nullptr, &depth);
    check(color && depth, "Mesh palette targets unavailable");
    if (bindings_only) {
        binding_scope(presentation, context, gpu.lods[0].parts[0], color, depth,
                      camera_view(Camera{}, {}, extent, extent));
        return;
    }
    unsigned frames = 0, cases = 0, mismatched_cases = 0;
    for (const char* model : {"forge.gltf.unlit.v1", "forge.gltf.metallic-roughness.v1",
                              "forge.gltf.specular-glossiness.v1"})
        for (unsigned member_count : {2u, 4u})
            for (bool different_sampler : {false, true}) {
                std::vector<MaterialData> values;
                std::vector<MeshDraw::Textures> images;
                std::vector<RefCntAutoPtr<ITexture>> assets;
                for (unsigned i = 0; i < member_count; ++i) {
                    assets.push_back(source_texture(device, i % 2));
                    MaterialData value;
                    value.model = model;
                    value.alpha = MaterialAlpha::Blend;
                    value.depth_write = false;
                    value.double_sided = true;
                    value.parameters["baseColorFactor"] = {
                        MaterialParameterType::LinearColor4,
                        Row{.3f + .17f * i, .8f - .12f * i, .5f + .09f * i, .24f + .04f * i}};
                    const bool sg = std::string_view(model) == "forge.gltf.specular-glossiness.v1";
                    const auto role = sg ? "diffuseTexture" : "baseColorTexture";
                    auto& slot = value.textures[role];
                    slot.semantic = TextureSemantic::Color;
                    slot.sampler.min = slot.sampler.mag = slot.sampler.mip = TextureFilter::Nearest;
                    slot.sampler.u = slot.sampler.v = slot.sampler.w = TextureWrap::ClampEdge;
                    slot.offset = i % 2 ? std::array{-.4f, .4f} : std::array{.2f, .15f};
                    slot.scale = i % 2 ? std::array{1.3f, -.8f} : std::array{.75f, 1.1f};
                    slot.rotation = i % 2 ? -.6f : .3f;
                    if (i % 2 && different_sampler) {
                        slot.sampler.min = slot.sampler.mag = TextureFilter::Linear;
                        slot.sampler.u = TextureWrap::Repeat;
                    }
                    value.textures[role].offset[0] += .07f * i;
                    if (std::string_view(model) != "forge.gltf.unlit.v1") {
                        value.parameters[sg ? "glossinessFactor" : "roughnessFactor"] = {
                            MaterialParameterType::Scalar, {.2f + .15f * i}};
                        value.parameters["emissiveFactor"] = {MaterialParameterType::LinearColor3,
                                                              {.04f * i, .03f, .01f}};
                    }
                    values.push_back(std::move(value));
                    auto& textures = images.emplace_back();
                    textures[role] = assets.back()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                }
                std::vector<MeshDraw::PaletteMaterial> members;
                std::vector<std::unique_ptr<MeshDraw>> singles;
                for (unsigned i = 0; i < member_count; ++i) {
                    members.push_back({&values[i], &images[i]});
                    singles.push_back(std::make_unique<MeshDraw>(
                        presentation, context, gpu.lods[0].parts[0], values[i], images[i],
                        TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT));
                }
                MeshDraw palette(presentation, context, gpu.lods[0].parts[0], values[0], images[0],
                                 TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, true,
                                 SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT,
                                 nullptr, MeshDrawProfile::Mesh, false, false, members);
                ParticleGpuOrder order(presentation);
                for (unsigned births : {3u, 65u})
                    for (bool reverse : {false, true}) {
                        Camera camera;
                        camera.projection = 1;
                        camera.orthographic_height = 2;
                        AffineTransform camera_pose;
                        if (reverse) {
                            camera_pose.m[0] = camera_pose.m[10] = -1;
                            camera_pose.m[11] = 10;
                        }
                        auto view = camera_view(camera, camera_pose, extent, extent);
                        Light light;
                        light.intensity = 2;
                        auto lamp = light_view(light, camera_pose);
                        const std::array lights{lamp};
                        std::vector<std::vector<Packet>> packets(member_count);
                        std::vector<RefCntAutoPtr<IBuffer>> input(member_count);
                        std::vector<ParticleGpuOrder::Source> slices(member_count);
                        std::vector<MeshDraw::Instance> originals;
                        std::vector<unsigned> tags;
                        for (unsigned source = 0; source < member_count; ++source) {
                            for (unsigned birth = 0; birth < births; ++birth) {
                                const float z = 2.f + .008f * (birth * member_count + source);
                                Packet packet{};
                                packet[0] = {.7f, 0, 0, .03f * source};
                                packet[1] = {0, .7f, 0, .02f * (birth % 3)};
                                packet[2] = {0, 0, .7f, float(z - view.position[2])};
                                packet[3] = {1, 0, 0, .7f};
                                packet[4] = {0, 1, 0, 0};
                                packet[5] = {0, 0, 1, 0};
                                packet[6] = {1, 1, 1, 3};
                                packets[source].push_back(packet);
                                MeshDraw::Instance instance;
                                for (unsigned r = 0; r < 3; ++r)
                                    for (unsigned c = 0; c < 4; ++c)
                                        instance.world.m[r * 4 + c] = packet[r][c];
                                instance.world.m[11] = z;
                                instance.material_tint_only = true;
                                instance.legacy_tint = std::array{1.f, 1.f, 1.f};
                                instance.opacity = .7f;
                                originals.push_back(instance);
                                tags.push_back(source);
                            }
                            input[source] =
                                buffer(device, packets[source].data(),
                                       packets[source].size() * sizeof(Packet),
                                       BIND_SHADER_RESOURCE | BIND_VERTEX_BUFFER, sizeof(Row));
                            slices[source] = {input[source], births, source};
                        }
                        order.evaluate(context, slices, view, {});
                        std::vector<unsigned> sorted(originals.size());
                        std::iota(sorted.begin(), sorted.end(), 0);
                        std::stable_sort(sorted.begin(), sorted.end(), [&](unsigned a, unsigned b) {
                            return originals[a].world.m[11] * view.forward[2] >
                                   originals[b].world.m[11] * view.forward[2];
                        });
                        const std::vector<std::uint32_t> zeros(sorted.size(), 0);
                        auto wrong =
                            buffer(device, zeros.data(), zeros.size() * sizeof(std::uint32_t),
                                   BIND_VERTEX_BUFFER, sizeof(std::uint32_t));
                        const std::vector<std::uint32_t> invalid_values(sorted.size(), UINT32_MAX);
                        auto invalid_tags = buffer(device, invalid_values.data(),
                                                   invalid_values.size() * sizeof(std::uint32_t),
                                                   BIND_VERTEX_BUFFER, sizeof(std::uint32_t));
                        SceneDistanceFog fog;
                        fog.enabled = different_sampler;
                        fog.color = {.15f, .25f, .35f};
                        fog.start = .5f;
                        fog.end = 8;
                        const auto submit = [&](const MeshDraw::InstanceBuffer& instances) {
                            palette.draw(context, {}, view, lights, nullptr, nullptr, nullptr, {},
                                         nullptr, {}, nullptr, {}, {}, {}, nullptr, &fog, nullptr,
                                         {}, nullptr, {}, {}, nullptr, {}, nullptr, &instances);
                        };
                        if (!cases) {
                            const MeshDraw::InstanceBuffer valid{order.buffer(), 0, 1, order.tags(),
                                                                 0};
                            for (unsigned mode = 0; mode < 6; ++mode) {
                                auto rejected = valid;
                                if (mode == 0)
                                    rejected.material_indices = nullptr;
                                if (mode == 1)
                                    rejected.material_offset = 1;
                                if (mode == 2)
                                    rejected.material_offset = order.tags()->GetDesc().Size;
                                if (mode == 3)
                                    rejected.offset = order.buffer()->GetDesc().Size;
                                if (mode == 4)
                                    rejected.count = 0;
                                if (mode == 5)
                                    rejected.count = MeshDraw::instance_limit + 1;
                                bool refused = false;
                                try {
                                    submit(rejected);
                                } catch (const std::exception&) {
                                    refused = true;
                                }
                                check(refused,
                                      "Mesh palette accepted an invalid device input range");
                            }
                        }
                        const auto draw = [&](unsigned mode) {
                            auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                            auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
                            context->SetRenderTargets(1, &rtv, dsv,
                                                      RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            const Row background{.02f, .04f, .08f, 1};
                            context->ClearRenderTarget(rtv, background.data(),
                                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            Viewport viewport{0, 0, float(extent), float(extent), 0, 1};
                            context->SetViewports(1, &viewport, extent, extent);
                            if (mode == 0) {
                                for (unsigned ordinal : sorted) {
                                    const auto& instance = originals[ordinal];
                                    singles[tags[ordinal]]->draw(
                                        context, instance.world, view, lights, nullptr, nullptr,
                                        nullptr, {}, nullptr, {}, nullptr, std::span(&instance, 1),
                                        {}, {}, nullptr, &fog);
                                }
                            } else {
                                for (unsigned start = 0; start < order.count();
                                     start += MeshDraw::instance_limit) {
                                    const MeshDraw::InstanceBuffer instances{
                                        order.buffer(), start * sizeof(Packet),
                                        std::min(MeshDraw::instance_limit, order.count() - start),
                                        mode == 1   ? order.tags()
                                        : mode == 2 ? wrong.RawPtr()
                                                    : invalid_tags.RawPtr(),
                                        start * sizeof(std::uint32_t)};
                                    submit(instances);
                                }
                            }
                            auto pixels = test_bloom::read(device, context, color);
                            context->FinishFrame();
                            device->ReleaseStaleResources();
                            ++frames;
                            return pixels;
                        };
                        if (!cases) {
                            const auto invalid = draw(3);
                            for (const auto& pixel : invalid)
                                check(pixel == Row{.02f, .04f, .08f, 1},
                                      "Out-of-palette indices sampled a valid Material");
                        }
                        const auto expected = draw(0), actual = draw(1), negative = draw(2);
                        unsigned differences = 0, visible = 0;
                        double absolute_error = 0, scaled_error = 0;
                        unsigned worst_pixel = 0;
                        bool mismatched = false;
                        for (unsigned i = 0; i < actual.size(); ++i)
                            for (unsigned c = 0; c < 4; ++c) {
                                check(std::isfinite(actual[i][c]), "Nonfinite palette pixel");
                                const double error =
                                    std::abs(double(actual[i][c]) - expected[i][c]);
                                if (error > absolute_error) {
                                    absolute_error = error;
                                    worst_pixel = i;
                                }
                                scaled_error = std::max(
                                    scaled_error,
                                    error / std::max(1., std::abs(double(expected[i][c]))));
                                mismatched |= std::abs(actual[i][c] - expected[i][c]) > 3e-5f;
                                differences += std::abs(actual[i][c] - negative[i][c]) > .001f;
                                visible += c < 3 && std::abs(expected[i][c] - .02f) > .01f;
                            }
                        mismatched_cases += mismatched;
                        std::cout << "MATERIAL_CASE_ERROR "
                                  << nlohmann::json{{"model", model},
                                                    {"members", member_count},
                                                    {"births", births},
                                                    {"reverse", reverse},
                                                    {"different_sampler", different_sampler},
                                                    {"max_absolute", absolute_error},
                                                    {"max_scaled", scaled_error},
                                                    {"worst_pixel", worst_pixel},
                                                    {"actual", actual[worst_pixel]},
                                                    {"expected", expected[worst_pixel]},
                                                    {"mismatched", mismatched},
                                                    {"wrong_index_channels", differences},
                                                    {"covered_channels", visible}}
                                         .dump()
                                  << std::endl;
                        check(differences > 100 && visible > 100,
                              "Mesh palette failed visible/incorrect-index negative controls");
                        ++cases;
                    }
                context->WaitForIdle();
            }
    std::cout << "MATERIAL_MESH_DRAW_PROOF cases=" << cases << " frames=" << frames
              << "; real MeshDraw, three builtin models, two/four Materials, lighting, "
                 "UV/textures/samplers, reverse views, 64-instance slices\n";
    check(!mismatched_cases, "Material image matrix contains original-threshold failures");
    bundle_preparation(presentation, context, mesh, gpu.lods[0].parts[0], color, depth);
}
} // namespace forge::test_particle_palette
