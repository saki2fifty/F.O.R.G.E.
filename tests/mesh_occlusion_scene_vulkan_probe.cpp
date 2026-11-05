#include "Graphics/GraphicsEngine/interface/Query.h"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "engine_render_resource.hpp"
#include "mesh_render_host.hpp"
#include "sprite_draw_render_tests.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
MeshData dense_mesh() {
    MeshPart part;
    constexpr unsigned side = 96;
    std::vector<float> positions;
    for (unsigned y = 0; y <= side; ++y)
        for (unsigned x = 0; x <= side; ++x) {
            positions.push_back(float(x) / side * 2 - 1);
            positions.push_back(float(y) / side * 2 - 1);
            positions.push_back(0);
        }
    for (unsigned y = 0; y < side; ++y)
        for (unsigned x = 0; x < side; ++x) {
            const unsigned p = y * (side + 1) + x;
            for (const auto i : {p, p + 1, p + side + 1, p + 1, p + side + 2, p + side + 1})
                part.indices.push_back(i);
        }
    part.vertices = (side + 1) * (side + 1);
    part.streams.push_back({"POSITION", 3, std::move(positions)});
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods.push_back({1, {std::move(part)}});
    validate_mesh(mesh);
    return mesh;
}
void exercise(DiligentPresentation& presentation, IDeviceContext* context,
              const std::filesystem::path& root) {
    auto* device = presentation.device();
    std::filesystem::create_directories(root);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MeshAsset> dense{AssetId::generate()};
    host->preview_mesh(dense, dense_mesh());
    const auto wall_mesh = engine_primitive(8);
    const auto wall_slot = asset_detail::engine_mesh_resource(wall_mesh).materials.front().key;
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialData values;
    values.model = "forge.gltf.unlit.v1";
    values.double_sided = true;
    host->preview_material(material, {values, {}, {}, {}});
    for (unsigned group = 0; group < 9; ++group) {
        for (unsigned i = 0; i < (group ? 4u : 1u); ++i) {
            RenderMesh mesh;
            char entity_id[37];
            std::snprintf(entity_id, sizeof(entity_id), "ffff0000-0000-4000-8000-%012x",
                          group * 4 + i);
            mesh.entity = EntityId::parse(entity_id);
            mesh.renderer.mesh = group ? dense : wall_mesh;
            mesh.renderer.cast_shadows = false;
            const auto slot = group ? "default" : wall_slot;
            mesh.renderer.materials = {{slot, material}};
            mesh.renderer.material_parameters = {
                {slot,
                 "baseColorFactor",
                 {MaterialParameterType::LinearColor4, {.2f + group * .02f, .5f, .8f, 1}}}};
            mesh.world.m[0] = mesh.world.m[5] = group ? .8 : 10;
            mesh.world.m[3] = group ? double(i % 2) * 1.8 - .9 : 0;
            mesh.world.m[7] = group ? double(i / 2) * 1.8 - .9 : 0;
            mesh.world.m[11] = group ? 8 + double(group) * .2 : 3;
            scene.meshes.push_back(std::move(mesh));
        }
    }
    Camera lens;
    lens.projection = 1;
    lens.orthographic_height = 12;
    unsigned width = 256, height = 192;
    auto view = camera_view(lens, {}, width, height);
    TextureDesc desc;
    desc.Name = "FORGE occlusion workload color";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    const auto attachments = [&] {
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(desc, nullptr, &color);
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(desc, nullptr, &depth);
        check(color && depth, "Occlusion workload attachments unavailable");
    };
    attachments();
    MeshSceneRenderer renderer(host);
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline, "Occlusion workload resource stall");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
    };
    settle();
    const auto render = [&](IQuery* statistics = nullptr) {
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto& v = view.viewport;
        Viewport area{float(v.x), float(v.y), float(v.width), float(v.height), 0, 1};
        context->SetViewports(1, &area, width, height);
        if (statistics)
            context->BeginQuery(statistics);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        if (statistics)
            context->EndQuery(statistics);
        if (!renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        context->WaitForIdle();
        context->FinishFrame();
        device->ReleaseStaleResources();
    };
    for (unsigned i = 0; i < 3; ++i)
        render();
    std::vector<double> milliseconds;
    for (unsigned i = 0; i < 9; ++i) {
        const auto start = std::chrono::steady_clock::now();
        render();
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }
    const auto pixels = test_sprite_draw::read_pixels(device, context, color);
    check(pixels.at(width / 2 + height / 2 * width)[2] > 100,
          "Occlusion workload has no visible foreground");
    std::ofstream image(root / "baseline.rgba", std::ios::binary);
    image.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * sizeof(pixels[0]));
    check(bool(image), "Occlusion baseline image write failed");
    const auto stats = renderer.draw_stats();
    QueryDesc query_desc;
    query_desc.Name = "FORGE test-only actual occlusion pipeline work";
    query_desc.Type = QUERY_TYPE_PIPELINE_STATISTICS;
    RefCntAutoPtr<IQuery> query;
    device->CreateQuery(query_desc, &query);
    check(bool(query), "Pipeline work query unavailable");
    render(query);
    QueryDataPipelineStatistics reference_work;
    check(query->GetData(&reference_work, sizeof(reference_work)),
          "Reference GPU work unavailable");
    scene.settings.occlusion_culling.enabled = true;
    for (unsigned i = 0; i < 3; ++i)
        render();
    const auto culled_pixels = test_sprite_draw::read_pixels(device, context, color);
    check(culled_pixels == pixels, "Current-view culling changed the reference workload pixels");
    render(query);
    QueryDataPipelineStatistics culled_work;
    check(query->GetData(&culled_work, sizeof(culled_work)), "Culled GPU work unavailable");
    check(reference_work.InputVertices > 1000000 && reference_work.VSInvocations > 100000 &&
              culled_work.InputVertices < reference_work.InputVertices / 100 &&
              culled_work.VSInvocations < reference_work.VSInvocations / 100 &&
              culled_work.CSInvocations > 0,
          "Native indirect culling did not suppress actual hidden mesh vertex work");
    const auto culled_stats = renderer.draw_stats();
    check(culled_stats.occlusion_queries == 9 && culled_stats.indirect_calls == 9 &&
              culled_stats.occluder_calls == 1 && culled_stats.batched_calls == 8 &&
              culled_stats.instances == 33,
          "Occlusion did not preserve the queue/batches and use one cheap depth occluder");
    std::vector<double> culled_milliseconds;
    for (unsigned i = 0; i < 9; ++i) {
        const auto start = std::chrono::steady_clock::now();
        render();
        culled_milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }
    std::sort(culled_milliseconds.begin(), culled_milliseconds.end());
    std::cout << nlohmann::json{{"occlusion_enabled", true},
                                {"reference_input_vertices", reference_work.InputVertices},
                                {"reference_vs_invocations", reference_work.VSInvocations},
                                {"culled_input_vertices", culled_work.InputVertices},
                                {"culled_vs_invocations", culled_work.VSInvocations},
                                {"reference_pixels_equal", true},
                                {"occluder_calls", culled_stats.occluder_calls},
                                {"queries", culled_stats.occlusion_queries},
                                {"indirect_calls", culled_stats.indirect_calls},
                                {"sorted_frame_ms", culled_milliseconds},
                                {"median_frame_ms", culled_milliseconds[4]}}
                     .dump()
              << '\n';
    check(stats.instances == 33 && stats.calls == 9 && stats.batched_calls == 8,
          "Occlusion baseline lost its actual dense batched workload");
    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << nlohmann::json{{"backend", "software Vulkan"},
                                {"adapter", device->GetAdapterInfo().Description},
                                {"extent", {width, height}},
                                {"instances", stats.instances},
                                {"calls", stats.calls},
                                {"batched_calls", stats.batched_calls},
                                {"dense_vertices_per_instance", 97 * 97},
                                {"dense_indices_per_instance", 96 * 96 * 6},
                                {"warmup_frames", 3},
                                {"sorted_frame_ms", milliseconds},
                                {"median_frame_ms", milliseconds[4]},
                                {"timing_scope", "CPU submission plus serialized software GPU; "
                                                 "excludes resource settling and readback; no "
                                                 "physical hardware performance acceptance"}}
                     .dump()
              << '\n';

    const auto original_meshes = scene.meshes;
    unsigned cases = 0;
    const auto compare = [&](const char* name) {
        scene.settings.occlusion_culling.enabled = false;
        settle();
        render();
        const auto reference = test_sprite_draw::read_pixels(device, context, color);
        scene.settings.occlusion_culling.enabled = true;
        render();
        const auto actual = test_sprite_draw::read_pixels(device, context, color);
        if (actual != reference)
            throw std::runtime_error(std::string("Occlusion changed reference pixels: ") + name);
        ++cases;
        std::cout << nlohmann::json{{"case", name},
                                    {"reference_pixels_equal", true},
                                    {"queries", renderer.draw_stats().occlusion_queries},
                                    {"indirect_calls", renderer.draw_stats().indirect_calls}}
                         .dump()
                  << '\n';
    };
    scene.meshes.front().world.m[3] = 20;
    compare("moving occluder reveals geometry without stale mask");
    check(renderer.draw_stats().occlusion_queries == 0, "No-occluder view reused a previous mask");
    scene.meshes = original_meshes;
    scene.meshes[1].world.m[11] = 2;
    compare("one visible member retains a partially hidden batch");
    scene.meshes = original_meshes;
    scene.meshes.front().renderer.visible = false;
    compare("removed occluder reveals geometry");
    check(renderer.draw_stats().indirect_calls == 0, "Invisible occluder supplied stale depth");
    scene.meshes = original_meshes;
    scene.meshes.front().world.m[11] = lens.near_plane * .5;
    compare("near-plane occluder declines");
    scene.meshes = original_meshes;
    width = 259;
    height = 195;
    attachments();
    lens.viewport_x = .17;
    lens.viewport_y = .11;
    lens.viewport_width = .73;
    lens.viewport_height = .81;
    view = camera_view(lens, {}, width, height);
    compare("odd resize and offset subviewport");
    lens.projection = 0;
    lens.flip_x = lens.flip_y = true;
    view = camera_view(lens, {}, width, height);
    compare("flipped perspective camera");
    AffineTransform camera_world;
    camera_world.m[3] = 1;
    view = camera_view(lens, camera_world, width, height);
    compare("consecutive different camera view");
    const Double3 origin{1e12, -2e12, 3e12};
    for (auto& mesh : scene.meshes)
        for (unsigned r = 0; r < 3; ++r)
            mesh.world.m[4 * r + 3] += origin[r];
    for (unsigned r = 0; r < 3; ++r)
        camera_world.m[4 * r + 3] += origin[r];
    view = camera_view(lens, camera_world, width, height);
    compare("large camera-relative origin");
    scene.meshes = original_meshes;
    lens = Camera{};
    lens.projection = 1;
    lens.orthographic_height = 12;
    view = camera_view(lens, {}, width, height);
    values.alpha = MaterialAlpha::Mask;
    host->preview_material(material, {values, {}, {}, {}});
    compare("masked depth remains ordinary");
    check(renderer.draw_stats().indirect_calls == 0, "Masked surface entered occlusion");
    values.alpha = MaterialAlpha::Blend;
    values.depth_write = false;
    host->preview_material(material, {values, {}, {}, {}});
    compare("transparent depth remains ordinary");
    check(renderer.draw_stats().indirect_calls == 0, "Transparent surface entered occlusion");
    values.alpha = MaterialAlpha::Opaque;
    values.depth_write = true;
    values.depth_test = false;
    host->preview_material(material, {values, {}, {}, {}});
    compare("depth-test-disabled surface remains ordinary");
    check(renderer.draw_stats().indirect_calls == 0,
          "Depth-test-disabled surface entered occlusion");
    values.depth_test = true;
    host->preview_material(material, {values, {}, {}, {}});
    for (auto& mesh : scene.meshes)
        if (mesh.renderer.mesh == dense)
            mesh.renderer.wind.enabled = true;
    compare("wind-deformed batches remain ordinary");
    check(renderer.draw_stats().occlusion_queries == 1, "Wind-deformed batches entered occlusion");
    scene.meshes = original_meshes;
    auto morph = dense_mesh();
    morph.morph_names = {"shape"};
    morph.morph_defaults = {.5f};
    morph.lods[0].parts[0].morph_targets = {
        {{"POSITION", 3, std::vector<float>(97 * 97 * 3, .1f)}}};
    host->preview_mesh(dense, std::move(morph));
    compare("morphed batches remain ordinary");
    check(renderer.draw_stats().occlusion_queries == 1, "Morphed batches entered occlusion");
    auto shifted = dense_mesh();
    auto& positions = std::get<std::vector<float>>(shifted.lods[0].parts[0].streams[0].values);
    for (unsigned i = 0; i < positions.size(); ++i)
        if (i % 3 != 2)
            positions[i] += 1000000;
    shifted.lods[0].parts[0].bounds = mesh_bounds(shifted.lods[0].parts[0]);
    host->preview_mesh(dense, std::move(shifted));
    for (auto& mesh : scene.meshes)
        if (mesh.renderer.mesh == dense) {
            mesh.world.m[3] -= mesh.world.m[0] * 1000000;
            mesh.world.m[7] -= mesh.world.m[5] * 1000000;
        }
    scene.meshes[1].world.m[11] = 2;
    compare("large local/world cancellation with visible member");
    scene.meshes = original_meshes;
    scene.meshes.resize(2);
    auto sequential = dense_mesh();
    auto& part = sequential.lods[0].parts[0];
    const auto source_positions = std::get<std::vector<float>>(part.streams[0].values);
    std::vector<float> expanded;
    expanded.reserve(part.indices.size() * 3);
    for (const auto index : part.indices)
        for (unsigned axis = 0; axis < 3; ++axis)
            expanded.push_back(source_positions[index * 3 + axis]);
    part.vertices = unsigned(part.indices.size());
    part.indices.clear();
    part.streams[0].values = std::move(expanded);
    host->preview_mesh(dense, std::move(sequential));
    compare("actual nonindexed hidden mesh");
    check(renderer.draw_stats().indirect_calls == 2,
          "Nonindexed mesh did not use visibility command");
    std::cout << "Occlusion reference-pixel motion/material/deformation/multiview cases passed: "
              << cases << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected isolated workload directory");
        EngineVkCreateInfo create;
        create.Features.PipelineStatisticsQueries = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Occlusion workload device unavailable");
        DiligentPresentation presentation(device);
        exercise(presentation, context, argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
