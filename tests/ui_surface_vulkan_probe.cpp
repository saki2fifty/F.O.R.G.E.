#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "fixtures/ui_surface_gallery.hpp"
#include "gallery_game_capture.hpp"
#include "ui_surface_input.hpp"
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_protocol.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
using namespace Diligent;
using test_gallery_capture::check;
namespace {
void near(double actual, double expected, const char* message) {
    check(std::abs(actual - expected) < 1e-5, message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, default font and surface scratch root");
        if (std::string_view(argv[1]) == "--author") {
            EngineVkCreateInfo create;
            RefCntAutoPtr<IRenderDevice> device;
            RefCntAutoPtr<IDeviceContext> context;
            GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
            check(device && context, "Surface Gallery inspector device unavailable");
            UiDiligentRenderer renderer(device);
            const auto root = std::filesystem::absolute(argv[2]);
            const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
            const RuntimeUiInspector inspect = [&](AssetId id, std::stop_token) {
                UiPresenter ui(renderer.render_interface(), root, font);
                return ui.inspect_static({id});
            };
            std::cout << test::author_ui_surface_gallery(root, inspect).str() << '\n';
            context->WaitForIdle();
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        const auto root = scratch / "Project";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        std::ofstream(root / "Assets/SurfaceProbe.rml") << R"rml(<rml><head><style>
body {margin:0;width:100%;height:100%;pointer-events:none;font-family:Lato;}
button {position:absolute;left:0;top:0;width:50%;height:50%;margin:0;padding:0;
background-color:#00ff00;pointer-events:auto;tab-index:auto;border:0px;}
</style></head><body><button data-event-click="command('Step')">CLICK</button></body></rml>)rml";
        const auto document = register_ui_document(root, "Assets/SurfaceProbe.rml");
        Json source;
        std::ifstream(root / "Scenes/154-camera-output-ui.scene.json") >> source;
        EngineContext engine;
        Scene authored(engine.world());
        authored.restore_snapshot(source);
        auto scene = extract_render_scene(authored.effective_document());
        auto monitor = std::find_if(scene.meshes.begin(), scene.meshes.end(),
                                    [](const auto& mesh) { return mesh.renderer.layers == 4; });
        check(monitor != scene.meshes.end(), "Surface fixture monitor missing");
        const auto surface = monitor->entity;
        // Editor selection policy must not determine interactive Game UI.
        monitor->selectable = false;
        AssetId output;
        for (const auto& camera : scene.cameras)
            if (camera.camera.layers == 1)
                output = camera.camera.render_texture.id;
        check(bool(output), "Surface fixture output missing");
        scene.ui_surfaces.push_back(
            {surface, {{output}, true}, std::uint32_t(monitor - scene.meshes.begin())});
        const auto baseline = scene;

        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Surface Vulkan device unavailable");
        DiligentPresentation presentation(device);
        auto resources = std::make_shared<MeshResourceHost>(presentation, context, root);
        resources->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        FrameRenderer frame(presentation);
        frame.resources(resources);
        UiDiligentRenderer renderer(device);
        UiPresenter ui(renderer.render_interface(), root,
                       asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
        ui.reset("surface-vulkan", 1);
        ui.surface_input(ui_surface_callbacks([&] { return &frame; }));
        check(ui.accept({{"version", 2},
                         {"session", "surface-vulkan"},
                         {"generation", 1u},
                         {"revision", 1u},
                         {"documents",
                          {{{"entity", EntityId::generate()},
                            {"asset", document.id},
                            {"instance", "surface"},
                            {"visible", true},
                            {"layer", 0u},
                            {"render_texture", output},
                            {"model", {{"tick", 0u}, {"paused", true}}},
                            {"commands", {"Step"}}}}}}),
              "Surface native snapshot rejected");
        unsigned index = 0;
        unsigned draw_case = 0;
        const RenderTextureOverlay overlay = [&](auto* ctx, AssetId id, auto* image, unsigned w,
                                                 unsigned h) {
            renderer.draw_output(ui, {id}, ctx, image, w, h);
        };
        auto draw = [&](const RenderScene& value, bool wait_ready = true) {
            ++draw_case;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            test_gallery_capture::Pixels pixels;
            do {
                resources->pump();
                presentation.begin_frame();
                ui.update(double(++index) / 60, 640, 360);
                auto* image =
                    frame.game(context, value, 640, 360, double(index) / 60, nullptr, &overlay);
                resources->submit();
                check(image, "Surface frame missing");
                pixels = test_sprite_draw::read_pixels(device, context, image->GetTexture());
                presentation.end_frame(context);
                context->Flush();
                context->FinishFrame();
                device->ReleaseStaleResources();
                if (std::chrono::steady_clock::now() >= deadline) {
                    std::string errors;
                    for (const auto& error : frame.diagnostics())
                        errors += error.category + ": " + error.text + "\n";
                    throw std::runtime_error("Surface frame stalled at case " +
                                             std::to_string(draw_case) + ": " + errors);
                }
            } while (wait_ready && frame.pending());
            return pixels;
        };
        const auto pixels = draw(scene);
        for (const auto& error : frame.diagnostics())
            check(error.severity != Severity::Error && error.severity != Severity::Fatal,
                  error.text.c_str());
        const auto green = pixels.at(150 * 640 + 84);
        test_gallery_capture::ppm(scratch / "surface.ppm", pixels, 640, 360);
        check(green[1] > green[0] + 60 && green[1] > green[2] + 60,
              "Visible native control did not occupy its expected image quadrant");
        auto hit = frame.pick_ui_surface(84, 150);
        check(hit && hit->surface == surface && hit->output.id == output && hit->view,
              "Displayed native control did not produce its retained surface hit");
        near(hit->uv[0], .25, "Surface U differs from visible image");
        near(hit->uv[1], .25, "Flipped image V differs from visible control");
        const auto projected = frame.project_ui_surface(*hit);
        check(bool(projected), "Surface reverse projection missing");
        near((*projected)[0], 84, "Surface reverse projection X differs");
        near((*projected)[1], 150, "Surface reverse projection Y differs");
        const auto query_start = std::chrono::steady_clock::now();
        unsigned query_hits = 0;
        constexpr unsigned query_count = 256;
        for (unsigned query = 0; query < query_count; ++query)
            query_hits += bool(frame.pick_ui_surface(84, 150));
        const auto query_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - query_start).count();
        check(query_hits == query_count, "Repeated retained surface query changed its hit");
        std::ofstream(scratch / "surface-query-timing.json")
            << Json{{"queries", query_count},
                    {"scene_meshes", scene.meshes.size()},
                    {"surfaces", scene.ui_surfaces.size()},
                    {"seconds", query_seconds},
                    {"scope", "Initial CPU retained hit-query workload in a Release software "
                              "Vulkan test process; not physical GPU or frame performance"}}
                   .dump(2)
            << '\n';
        check(ui.mouse_move(84, 150, 0) && ui.mouse_button(0, true, 0) &&
                  ui.mouse_button(0, false, 0),
              "Displayed surface control did not consume its native click");
        const auto command = ui.pending_command();
        check(command && command->at("instance") == "surface" && command->at("command") == "Step",
              "Displayed surface click did not reach the existing command owner");
        ui_protocol::CommandGate gate;
        gate.reset("surface-vulkan", 1);
        ui.acknowledge(gate.dispatch(*command, [](const Json&) {}));
        const auto captured = frame.pick_ui_surface(300, 150, hit);
        check(captured && captured->uv[0] > 1 && !frame.pick_ui_surface(300, 150),
              "Retained drag did not preserve its surface outside the Quad");
        auto wrong = *hit;
        wrong.view = EntityId::generate();
        check(!frame.pick_ui_surface(84, 150, wrong) && !frame.project_ui_surface(wrong),
              "Capture crossed camera identity");

        auto blocker = scene.meshes.at(scene.ui_surfaces.front().mesh_index);
        blocker.entity = EntityId::generate();
        blocker.world.m[11] -= 1;
        scene.meshes.push_back(blocker);
        draw(scene);
        check(!frame.pick_ui_surface(84, 150) && frame.pick_ui_surface(84, 150, hit),
              "Geometric blocker or captured gesture occlusion policy differs");
        scene.meshes.back().renderer.visible = false;
        draw(scene);
        check(bool(frame.pick_ui_surface(84, 150)), "Invisible geometry blocked surface input");

        scene = baseline;
        for (auto& camera : scene.cameras)
            if (!camera.camera.render_texture.id) {
                camera.camera.flip_x = true;
                camera.camera.flip_y = true;
            }
        draw(scene);
        const auto flipped = frame.pick_ui_surface(556, 210);
        check(bool(flipped), "Camera flips lost the surface hit");
        near(flipped->uv[0], .25, "Camera X flip disagrees with image U");
        near(flipped->uv[1], .25, "Camera Y flip disagrees with image V");

        scene = baseline;
        for (auto& camera : scene.cameras)
            if (!camera.camera.render_texture.id)
                camera.camera.layers = 8;
        draw(scene);
        check(!frame.pick_ui_surface(84, 150) && !frame.project_ui_surface(*hit),
              "Camera layer exclusion retained surface interaction");

        scene = baseline;
        auto main =
            std::find_if(scene.cameras.begin(), scene.cameras.end(),
                         [](const auto& camera) { return !camera.camera.render_texture.id; });
        check(main != scene.cameras.end(), "Main fixture camera missing");
        auto overlapping = *main;
        overlapping.entity = EntityId::generate();
        scene.cameras.push_back(overlapping);
        draw(scene);
        check(!frame.pick_ui_surface(84, 150) && !frame.project_ui_surface(*hit),
              "Overlapping camera views supplied ambiguous surface input");

        scene = baseline;
        std::erase_if(scene.cameras, [&](const auto& camera) {
            return camera.camera.render_texture.id == output;
        });
        draw(scene, false);
        check(!frame.pick_ui_surface(84, 150) && !frame.project_ui_surface(*hit),
              "A retained image without current producers supplied input");

        scene = baseline;
        scene.ui_surfaces.front().input.render_texture.id = AssetId::generate();
        draw(scene);
        check(!frame.pick_ui_surface(84, 150) &&
                  std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                              [](const auto& value) {
                                  return value.category == "render.ui_surface.profile";
                              }),
              "Mismatched image identity did not diagnose and disable the surface");
        scene = baseline;
        for (auto& mesh : scene.meshes)
            mesh.world.m[3] += 1e12;
        for (auto& camera : scene.cameras)
            camera.world.m[3] += 1e12;
        draw(scene);
        hit = frame.pick_ui_surface(84, 150);
        check(bool(hit), "Large-world surface hit missing");
        // The authored -2.2 translation itself rounds when stored at1e12;
        // retain that representable input accuracy without losing local offsets.
        check(std::abs(hit->uv[0] - .25) < 1e-4, "Large-world surface U lost precision");
        const auto large = frame.project_ui_surface(*hit);
        check(bool(large), "Large-world reverse projection missing");
        near((*large)[0], 84, "Large-world reverse projection lost local precision");
        ui.surface_input({});
        context->WaitForIdle();
        std::cout << "Surface native pixels, UV/camera identity, commands, capture, geometric "
                     "blockers, producer readiness and large-world projection passed: "
                  << scratch << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
