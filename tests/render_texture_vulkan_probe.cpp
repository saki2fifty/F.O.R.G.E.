#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "camera_render_textures.hpp"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "render_texture_fixture.hpp"
#include "runtime_package.hpp"
#include <cstring>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/prefab_authoring.hpp>
#include <iostream>

using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
std::array<std::uint16_t, 4> center(const DiligentPresentation::HdrPixels& image,
                                    double horizontal = .5) {
    std::array<std::uint16_t, 4> result;
    const auto offset =
        (std::size_t(image.height / 2) * image.width + std::size_t(image.width * horizontal)) * 8;
    std::memcpy(result.data(), image.rgba.data() + offset, 8);
    return result;
}
RenderCamera camera(AssetId output, std::uint32_t layers) {
    RenderCamera value{EntityId::generate(), {}, {}};
    value.world.m[11] = -2;
    value.camera.projection = unsigned(CameraProjection::Orthographic);
    value.camera.orthographic_height = 1;
    value.camera.render_texture = {output};
    value.camera.layers = layers;
    return value;
}
RenderMesh quad(AssetId material, std::uint32_t layers) {
    RenderMesh value;
    value.entity = EntityId::generate();
    value.renderer.mesh = engine_primitive(8);
    value.renderer.materials = {{"surface", {material}}};
    value.renderer.layers = layers;
    value.renderer.cast_shadows = false;
    return value;
}
std::string errors(const FrameRenderer& renderer) {
    std::string text;
    for (const auto& error : renderer.diagnostics())
        text += error.category + ": " + error.text + "\n";
    return text;
}
void camera_history(AssetId output) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto id = authoring_command(scene, "entity.create",
                                      {{"kind", no_primitive}, {"name", "Output camera"}})
                        .at("selected")
                        .get<std::string>();
    scene.entity(id).set<Camera>(Camera{});
    const auto before = scene.document();
    authoring_command(scene, "property.set",
                      {{"entity", id},
                       {"component", "forge.camera"},
                       {"field", "render_texture"},
                       {"value", output}});
    check(scene.entity(id).get<Camera>().render_texture.id == output,
          "Reflected Camera output did not reach native authority");
    scene.undo();
    check(scene.document() == before, "Camera output Undo changed unrelated Scene data");
    scene.redo();
    const auto source = create_prefab_source(scene, id);
    scene.publish_prefab_sources({{source.asset(), source.source}}, [] {});
    const auto instance = instantiate_prefab(scene, source.asset());
    check(!scene.entity(instance).owns<Camera>() &&
              scene.entity(instance).get<Camera>().render_texture.id == output,
          "Prefab output reference was lost or materialized as an owned Camera");
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.camera"},
                       {"field", "render_texture"},
                       {"value", nullptr}});
    check(!scene.entity(instance).get<Camera>().render_texture.id,
          "Prefab Camera output could not override to main presentation");
    authoring_command(
        scene, "property.revert",
        {{"entity", instance}, {"component", "forge.camera"}, {"field", "render_texture"}});
    check(!scene.entity(instance).owns<Camera>() &&
              scene.entity(instance).get<Camera>().render_texture.id == output,
          "Output Revert did not restore native Camera inheritance");
    scene.undo();
    check(!scene.entity(instance).get<Camera>().render_texture.id,
          "Output Revert Undo lost the explicit main-output choice");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need camera output scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        atomic_write(root / "forge.assets.json",
                     Json{{"version", 1}, {"assets", Json::array()}}.dump());
        const auto lease = std::make_shared<ProjectLease>(root);
        auto output = test_render_texture::output(lease, "Assets/Monitor.rendertexture.json");
        const auto id = output->source().asset();
        camera_history(id);
        const auto screen =
            test_render_texture::material(lease, "Assets/Monitor.material.json", id);
        const auto color = test_render_texture::material(lease, "Assets/Color.material.json");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Camera output Vulkan device unavailable");
        DiligentPresentation presentation(device);
        auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
        auto active_host = host;
        host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        FrameRenderer renderer(presentation);
        renderer.resources(host);
        RenderScene scene;
        scene.scene = AssetId::generate();
        scene.cameras = {camera(id, 1), camera({}, 2)};
        scene.meshes = {quad(color, 1), quad(screen, 2)};
        const auto finish = [&] {
            presentation.end_frame(context);
            active_host->submit();
            context->Flush();
            context->WaitForIdle();
        };
        const auto capture = [&](ITextureView* image, double horizontal = .5) {
            presentation.readback_hdr(context, image);
            finish();
            const auto pixels = presentation.take_hdr_readback(context);
            check(bool(pixels), "Completed camera output readback is unavailable");
            return center(*pixels, horizontal);
        };
        const auto sample = [&](FrameRenderer& frame, const RenderScene& snapshot,
                                double horizontal = .5) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (true) {
                presentation.begin_frame();
                const auto main = prepare_game_cameras(snapshot, 64, 64);
                auto* image = frame.render(context, snapshot, main.cameras, 64, 64, 0, nullptr, 0,
                                           nullptr, FrameRenderer::Output::LinearHdr);
                if (!frame.pending() && frame.diagnostics().empty())
                    return capture(image, horizontal);
                finish();
                check(std::chrono::steady_clock::now() < deadline,
                      "Camera outputs did not become ready:\n" + errors(frame));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        const std::array<std::uint16_t, 4> green{0x3000, 0x3800, 0x3400, 0x3c00};
        check(sample(renderer, scene) == green,
              "Published Camera -> render texture -> Material -> mesh changed linear color");
        auto split = scene;
        auto& left = split.cameras[0].camera;
        left.layers = 0;
        left.viewport_width = .5;
        left.background_r = 1;
        left.background_g = left.background_b = 0;
        auto right = split.cameras[0];
        right.entity = EntityId::generate();
        right.camera.viewport_x = .5;
        right.camera.order = 1;
        right.camera.background_r = 0;
        right.camera.background_g = 1;
        split.cameras.push_back(right);
        check(sample(renderer, split, .25) == std::array<std::uint16_t, 4>{0x3c00, 0, 0, 0x3c00} &&
                  sample(renderer, split, .75) ==
                      std::array<std::uint16_t, 4>{0, 0x3c00, 0, 0x3c00},
              "Shared output cameras did not preserve ordered split viewports and independent "
              "clears");
        check(sample(renderer, scene) == green, "Removed output writer left stale composition");
        scene.meshes[0].renderer.material_parameters = {
            {"surface",
             "baseColorFactor",
             {MaterialParameterType::LinearColor4, {.5f, .125f, .25f, 1.f}}}};
        const std::array<std::uint16_t, 4> red{0x3800, 0x3000, 0x3400, 0x3c00};
        check(sample(renderer, scene) == red,
              "Live producer value did not reach the same-frame screen");
        output->edit(output->revision(), "Resize monitor", [](Json& d) {
            d["width"] = 96;
            d["height"] = 32;
        });
        test_render_texture::publish(lease, *output, render_texture_import_registry(),
                                     prepare_render_texture_publication);
        host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        check(sample(renderer, scene) == red,
              "Output resize lost material identity or sampled retired pixels");

        const auto invalid_frame = [&](RenderScene invalid, const char* reason) {
            presentation.begin_frame();
            const auto main = prepare_game_cameras(invalid, 64, 64);
            renderer.render(context, invalid, main.cameras, 64, 64, 0, nullptr, 0, nullptr,
                            FrameRenderer::Output::LinearHdr);
            check(errors(renderer).find(reason) != std::string::npos,
                  "Invalid output did not explain its routing failure: " + errors(renderer));
            finish();
        };
        auto invalid = scene;
        invalid.cameras[0].camera.layers = 3;
        invalid_frame(invalid, "feedback cycle");
        invalid.cameras.erase(invalid.cameras.begin());
        invalid_frame(invalid, "no enabled camera writer");
        check(sample(renderer, scene) == red,
              "Correcting output routing did not recover rendering");

        auto chained_output =
            test_render_texture::output(lease, "Assets/Chained.rendertexture.json");
        const auto chained_id = chained_output->source().asset();
        const auto chained_material =
            test_render_texture::material(lease, "Assets/Chained.material.json", chained_id);
        host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        auto chained = scene;
        chained.cameras.push_back(camera(chained_id, 4));
        chained.meshes.push_back(quad(screen, 4));
        chained.meshes[1].renderer.materials = {{"surface", {chained_material}}};
        check(sample(renderer, chained) == red,
              "Two camera outputs did not sample in dependency order");
        auto cyclic = chained;
        cyclic.cameras[0].camera.layers = 2;
        invalid_frame(cyclic, "feedback cycle");
        check(sample(renderer, scene) == red, "Removing an output chain retained stale bindings");

        EngineContext authored_engine;
        Scene authored(authored_engine.world());
        const auto add = [&](const std::string& name, Double3 position) {
            const auto actor = authoring_command(authored, "entity.create",
                                                 {{"kind", no_primitive}, {"name", name}})
                                   .at("selected")
                                   .get<std::string>();
            authored.entity(actor).set<LocalTranslation>({position[0], position[1], position[2]});
            authored.entity(actor).set<LocalRotation>(LocalRotation{});
            authored.entity(actor).set<LocalScale>({1, 1, 1});
            return authored.entity(actor);
        };
        // The navigation view cannot see source geometry, only the rendered screen.
        add("Producer", {4, 0, -2}).set<Camera>(scene.cameras[0].camera);
        add("Game camera", {0, 0, -2}).set<Camera>(scene.cameras[1].camera);
        add("Source quad", {4, 0, 0}).set<MeshRenderer>(scene.meshes[0].renderer);
        add("Monitor", {0, 0, 0}).set<MeshRenderer>(scene.meshes[1].renderer);
        const auto snapshot = authored.effective_document();
        forge::Viewport viewport(presentation, true);
        viewport.resources(host);
        EditorCamera navigation;
        navigation.target = {0, 0, 0};
        navigation.distance = 2;
        navigation.projection = CameraProjection::Orthographic;
        navigation.orthographic_height = 1;
        const auto view_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool view_ready = false;
        while (!view_ready) {
            presentation.begin_frame();
            auto* image = viewport.render(context, snapshot, 64, 64, navigation, 1, false,
                                          {false, 1, GridPlane::XZ});
            if (viewport.meshes() && !viewport.pending() && viewport.diagnostics().empty()) {
                presentation.readback(context, image);
                finish();
                const auto pixels = presentation.take_readback(context);
                check(bool(pixels), "Scene output display capture never completed");
                const auto at = (32 * 64 + 32) * 4;
                check(pixels->rgba[at] > pixels->rgba[at + 2] &&
                          pixels->rgba[at + 2] > pixels->rgba[at + 1],
                      "Scene navigation view did not display the Camera-produced red image");
                view_ready = true;
            } else
                finish();
            if (std::chrono::steady_clock::now() >= view_deadline) {
                std::string detail = "Scene camera output did not become ready; frame pending=" +
                                     std::to_string(viewport.pending()) + "; mesh pending=" +
                                     std::to_string(viewport.meshes()->pending());
                for (const auto& diagnostic : viewport.diagnostics())
                    detail += "\n" + diagnostic.category + ": " + diagnostic.text;
                atomic_write(root / "scene-view-failure.json", snapshot.dump(2));
                throw std::runtime_error(detail);
            }
        }

        // Physical images belong to each view, even when CPU catalog selections are shared.
        CameraRenderTextures first(presentation, host), second(presentation, host);
        MeshSceneRenderer first_materials(host, TEX_FORMAT_RGBA16_FLOAT),
            second_materials(host, TEX_FORMAT_RGBA16_FLOAT);
        auto background = scene;
        background.meshes[0].renderer.visible = false;
        auto& clear = background.cameras[0].camera;
        clear.background_r = .125f;
        clear.background_g = .5f;
        clear.background_b = .25f;
        clear.background_a = .25f;
        const auto prepare = [&](CameraRenderTextures& scope, MeshSceneRenderer& materials) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (true) {
                presentation.begin_frame();
                scope.render(context, background, materials, 2, 0, nullptr);
                scope.submit();
                if (scope.inputs().produced.contains(id))
                    return;
                finish();
                check(std::chrono::steady_clock::now() < deadline,
                      "Scoped camera output never completed");
            }
        };
        prepare(first, first_materials);
        const auto first_image = first.inputs().images.at(id);
        check(capture(first_image.get()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)) == green,
              "Opaque output retained camera background transparency");
        clear.background_r = .5f;
        clear.background_g = .125f;
        prepare(second, second_materials);
        check(first_image.get() != second.inputs().images.at(id).get(),
              "Independent views shared mutable output pixels");
        check(capture(first_image.get()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)) == green,
              "Another view overwrote a retained camera output");

        // A UI/presentation overlay must complete before the producer image is
        // current. A callback failure invalidates this frame, even with old leases.
        presentation.begin_frame();
        unsigned overlays = 0;
        const RenderTextureOverlay rejected_overlay =
            [&](IDeviceContext*, AssetId asset, ITextureView* image, unsigned w, unsigned h) {
                check(asset == id && image && w == output->source().compiled().width &&
                          h == output->source().compiled().height,
                      "Output overlay did not receive actual producer dimensions");
                ++overlays;
                throw std::runtime_error("fixture.overlay.failure");
            };
        first.render(context, background, first_materials, 2, 0, nullptr, &rejected_overlay);
        check(overlays == 1 && first.inputs().produced.empty() &&
                  std::any_of(first.diagnostics().begin(), first.diagnostics().end(),
                              [](const auto& d) {
                                  return d.text.find("fixture.overlay.failure") !=
                                         std::string::npos;
                              }),
              "Failed output UI composition published stale pixels or lost its diagnostic");
        finish();
        prepare(first, first_materials);
        check(first.inputs().produced.contains(id), "A corrected overlay did not recover output");
        finish();

        const auto package = root.parent_path() / (root.filename().string() + "-package");
        std::filesystem::create_directories(root / "Scenes");
        authored.save(root / "Scenes/Monitor.scene.json");
        auto authored_catalog = AssetCatalog::open_project(root);
        const auto saved_scene = authored_catalog.add_scene("Scenes/Monitor.scene.json");
        authored_catalog.save(AssetCatalog::project_index(root));
        const std::array roots{saved_scene.id};
        prepare_runtime_content_catalog(*lease, roots, {}, authored.schema());
        bool rejected = false;
        try {
            package_runtime_content(root, package, roots, {"windows", "d3d12"});
        } catch (const std::exception& e) {
            rejected = std::string(e.what()).find("revision disagrees") != std::string::npos;
        }
        check(rejected && !std::filesystem::exists(package),
              "Export promoted an outdated Material dependency after output resize");
        MaterialDocument refreshed(lease, "Assets/Monitor.material.json");
        test_render_texture::publish(lease, refreshed, material_import_registry(),
                                     prepare_material_publication);
        check(refreshed.source().asset() == screen,
              "Dependency refresh replaced the Material UUID");
        std::filesystem::rename(root / "Assets", root / "Sources-offline");
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        auto packaged = open_runtime_content(package, {"windows", "d3d12"});
        check(packaged.records().size() == 4 && packaged.records().contains(id),
              "Saved Scene closure omitted its Material or camera output configuration");
        auto relocated = package;
        relocated += "-relocated";
        std::filesystem::rename(package, relocated);
        std::filesystem::rename(root, root.parent_path() /
                                          (root.filename().string() + "-source-offline"));
        auto runtime_host =
            std::make_shared<MeshResourceHost>(presentation, context, relocated, true);
        runtime_host->catalog(std::make_shared<const AssetCatalog>(
            open_runtime_content(relocated, {"windows", "d3d12"})));
        FrameRenderer runtime(presentation);
        runtime.resources(runtime_host);
        active_host = runtime_host;
        EngineContext runtime_engine(WorldRole::Runtime, false);
        Scene runtime_scene(runtime_engine.world());
        runtime_scene.restore_snapshot(load_game_scene(relocated, {saved_scene.id}));
        check(sample(runtime, extract_render_scene(runtime_scene.effective_document())) == red,
              "Relocated saved Scene required authoring source or lost Camera/Material output");
        context->WaitForIdle();
        context->FinishFrame();
        device->ReleaseStaleResources();
        std::cout << "Published camera images, live values, resize, cycles, scope isolation and "
                     "cooked-only relocation passed\n"
                  << "Adapter: " << device->GetAdapterInfo().Description << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
