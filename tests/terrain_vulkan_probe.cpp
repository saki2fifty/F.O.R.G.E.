#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "frame_renderer.hpp"
#include "terrain_authoring.hpp"
#include <forge/engine_assets.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Ordinary host accepted private Mesh draft");
}
std::vector<std::array<std::uint8_t, 4>> read(IRenderDevice* device, IDeviceContext* context,
                                              ITexture* texture) {
    auto desc = texture->GetDesc();
    require(desc.Format == TEX_FORMAT_RGBA8_UNORM, "Unexpected terrain display readback format");
    desc.Name = "FORGE terrain readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    require(bool(staging), "Terrain readback allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({texture, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    require(mapped.pData, "Terrain readback map failed");
    std::vector<std::array<std::uint8_t, 4>> pixels(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + std::size_t(y) * desc.Width,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    std::size_t(desc.Width) * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels;
}
std::size_t green(const std::vector<std::array<std::uint8_t, 4>>& pixels) {
    return std::count_if(pixels.begin(), pixels.end(), [](const auto& p) {
        return p[1] > 30 && p[1] > p[0] + 15 && p[1] > p[2] + 15;
    });
}
void image(const std::filesystem::path& path, unsigned width, unsigned height,
           const std::vector<std::array<std::uint8_t, 4>>& pixels) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& p : pixels)
        out.write(reinterpret_cast<const char*>(p.data()), 3);
    require(bool(out.flush()), "Terrain image write failed");
}
void execute(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TerrainDocument::create(lease, "Assets/Native.terrain.json", [](AssetId id) {
        return TerrainSource::create(id, 17, 4);
    });
    document->edit(document->revision(), "Original terrain hills", [](auto& draft) {
        TerrainSource source{draft};
        source.add_tile({1, 0});
        source.brush(source.raster().layers[0].id, std::array<MeshBrushSample, 1>{{{{4, 0, 2}, 1}}},
                     2, TerrainBrush::Raise, 1);
        draft = source.document;
    });
    document->save();
    AssetImportService importer(lease, terrain_import_registry(), {"linux", "none", "cpu"});
    importer.submit(
        importer.prepare(document->locator(), {}, document->source().asset()),
        [](auto& c, const auto& p, const auto&) { prepare_terrain_publication(c, p); },
        [](const auto&, const auto&) {});
    require(importer.wait_idle(std::chrono::seconds(15)), "Native terrain cook timed out");
    const auto outcomes = importer.poll();
    require(outcomes.size() == 1 && outcomes[0].published,
            outcomes.empty() ? "Native terrain receipt missing" : outcomes[0].diagnostic);
    const auto catalog = std::make_shared<const AssetCatalog>(outcomes[0].publication->catalog);
    const auto saved =
                   asset_detail::read_bytes(root / document->locator(), terrain_source_byte_limit),
               catalog_bytes =
                   asset_detail::read_bytes(root / "forge.assets.json", 32 * 1024 * 1024);
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    require(device && context, "Terrain Vulkan device/context unavailable");
    std::cout << "Terrain native adapter: " << device->GetAdapterInfo().Description << '\n';
    DiligentPresentation presentation(device);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(catalog);
    auto private_host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    private_host->catalog(catalog);
    FrameRenderer frame(presentation);
    frame.resources(host);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    RenderMesh mesh;
    mesh.entity = EntityId::generate();
    mesh.renderer.mesh = {document->source().asset()};
    mesh.legacy_tint = {{.05f, .8f, .08f}};
    mesh.renderer.materials = {{"default", engine_material(EngineMaterial::LegacyBlockout)}};
    scene.meshes = {mesh};
    Light light;
    light.intensity = 3;
    auto sun = light_view(light, {});
    sun.direction = {0, -1, 0};
    scene.lights = {{EntityId::generate(), sun}};
    Camera lens;
    lens.projection = 1;
    lens.orthographic_height = 6;
    lens.orthographic_width = 10;
    auto transform = affine_transform({{4, 15, 2}, rotation_from_euler({90, 0, 0}), {1, 1, 1}});
    const auto camera_id = EntityId::generate();
    std::shared_ptr<MeshResourceHost> active_host = host;
    auto render = [&](unsigned width, unsigned height) {
        const std::array cameras{
            PreparedCamera{camera_id, lens, camera_view(lens, transform, width, height)}};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            (void)frame.render(context, scene, cameras, width, height, 0);
            if (!frame.diagnostics().empty())
                throw std::runtime_error(frame.diagnostics().front().text);
            active_host->submit();
            context->Flush();
            require(std::chrono::steady_clock::now() < deadline,
                    "Native terrain renderer timed out");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        return read(device, context, frame.output()->GetTexture());
    };
    const auto unlit = render(256, 256);
    require(green(unlit) > 5000, "Actual published terrain had no substantial Unlit pixels");
    image(root / "terrain-unlit.ppm", 256, 256, unlit);
    auto pick_center = [&] {
        const auto& view = frame.cameras()[0].view;
        return frame.pick_point(scene, view, view.viewport.x + view.viewport.width * .5,
                                view.viewport.y + view.viewport.height * .5);
    };
    const auto point = pick_center();
    require(point && std::abs((*point)[0] - 4) < 1e-5 && std::abs((*point)[2] - 2) < 1e-5 &&
                std::abs((*point)[1] - 1) < 1e-5,
            "Shared terrain picking disagreed with its actual hill triangles");
    const auto bounds = frame.bounds();
    require(bounds && bounds->minimum[0] == 0 && bounds->maximum[0] == 8 && bounds->maximum[2] == 4,
            "Native terrain bounds lost an adjoining tile");
    scene.meshes[0].renderer.materials.clear();
    const auto pbr = render(256, 256);
    require(green(pbr) > 5000, "Actual lit terrain had no substantial PBR pixels");
    image(root / "terrain-pbr.ppm", 256, 256, pbr);
    require(green(render(384, 192)) > 5000, "Terrain resize lost native geometry");
    scene.meshes[0].renderer.layers = 0;
    require(green(render(256, 256)) == 0, "Layer-hidden terrain still drew");
    scene.meshes[0].renderer.layers = UINT32_MAX;
    scene.meshes[0].world.m[3] = 10000;
    require(green(render(256, 256)) == 0, "Off-camera terrain did not cull");
    scene.meshes[0].world.m[3] = 0;
    auto draft = document->source();
    draft.brush(draft.raster().layers[0].id, std::array<MeshBrushSample, 1>{{{{4, 0, 2}, 1}}}, 1,
                TerrainBrush::Raise, 2);
    reject([&] { host->preview_mesh({draft.asset()}, draft.mesh()); });
    private_host->preview_mesh({draft.asset()}, draft.mesh());
    active_host = private_host;
    frame.resources(private_host);
    const auto preview = render(256, 256);
    require(green(preview) > 5000,
            "Private unsaved terrain preview did not use shared native geometry");
    const auto raised = pick_center();
    require(raised && std::abs((*raised)[1] - 3) < 1e-5,
            "Private draft picking did not adopt the raised surface");
    image(root / "terrain-private-preview.ppm", 256, 256, preview);
    require(
        asset_detail::read_bytes(root / document->locator(), terrain_source_byte_limit) == saved &&
            asset_detail::read_bytes(root / "forge.assets.json", 32 * 1024 * 1024) == catalog_bytes,
        "Private terrain preview modified source/catalog");
    active_host = host;
    frame.resources(host);
    (void)render(256, 256);
    const auto original = pick_center();
    require(original && std::abs((*original)[1] - 1) < 1e-5,
            "Private draft leaked into the ordinary host's selected Mesh");
    // Oblique readback exposes the raised silhouette and the adjoining tile;
    // the top-down oracle above independently proves exact picked heights.
    lens.projection = 0;
    transform = affine_transform({{4, 7, -6}, rotation_from_euler({35, 0, 0}), {1, 1, 1}});
    scene.lights[0].light.intensity = 1;
    scene.lights[0].light.direction = {.4, -.8, .4};
    const auto oblique = render(384, 256);
    require(green(oblique) > 2000, "Oblique terrain lost its native surface");
    image(root / "terrain-oblique.ppm", 384, 256, oblique);
    context->WaitForIdle();
    std::cout << "Terrain shared native Unlit/PBR geometry, bounds, picking, resize, layers, "
                 "culling and isolated draft passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need native terrain scratch root");
        execute(std::filesystem::absolute(argv[1]) / AssetId::generate().str());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
