#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "fixtures/ui_mesh_surface_gallery.hpp"
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
        check(argc == 4 || argc == 6,
              "Need Gallery, font and scratch, or mesh author worker/converter");
        if (std::string_view(argv[1]) == "--author" ||
            std::string_view(argv[1]) == "--author-mesh") {
            check(argc == (std::string_view(argv[1]) == "--author-mesh" ? 6 : 4),
                  "Mesh Gallery authoring needs the model worker and converter");
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
            const auto id =
                std::string_view(argv[1]) == "--author-mesh"
                    ? test::author_ui_mesh_surface_gallery(root, inspect, argv[4], argv[5])
                    : test::author_ui_surface_gallery(root, inspect);
            std::cout << id.str() << '\n';
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
        auto resources = std::make_shared<MeshResourceHost>(presentation, context, root, true);
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
        // Original bent two-part surface: UV0 remains authored, while the retained
        // morph pose changes visible X and Z. This is not an Engine Quad identity.
        scene = baseline;
        const AssetRef<MeshAsset> bent_id{AssetId::generate()};
        MeshPart left;
        left.vertices = 6;
        left.indices = {0, 3, 1, 1, 3, 4};
        left.streams = {
            {"POSITION", 3,
             std::vector<float>{-.5f, -.5f, 0, 0, -.5f, .15f, .5f, -.5f, 0, -.5f, .5f, 0, 0, .5f,
                                .15f, .5f, .5f, 0}},
            {"TEXCOORD_0", 2, std::vector<float>{0, 0, .5f, 0, 1, 0, 0, 1, .5f, 1, 1, 1}}};
        left.morph_targets = {{{"POSITION", 3,
                                std::vector<float>{.2f, 0, .1f, .2f, 0, .1f, .2f, 0, .1f, .2f, 0,
                                                   .1f, .2f, 0, .1f, .2f, 0, .1f}}}};
        left.bounds = mesh_bounds(left);
        auto right = left;
        right.indices = {1, 4, 2, 2, 4, 5};
        MeshData bent;
        bent.morph_names = {"Shift"};
        bent.morph_defaults = {.5f};
        bent.lods = {{1, {left, right}}, {.05f, {left, right}}};
        resources->preview_mesh(bent_id, bent);
        auto& screen = scene.meshes.at(scene.ui_surfaces.front().mesh_index);
        screen.renderer.mesh = bent_id;
        screen.renderer.materials.front().slot = "default";
        scene.ui_surfaces.front().input.geometry = std::uint32_t(UiSurfaceGeometry::Mesh);
        const auto bent_pixels = draw(scene);
        for (const auto& error : frame.diagnostics())
            check(error.severity != Severity::Error && error.severity != Severity::Fatal,
                  error.text.c_str());
        const auto bent_control = bent_pixels.at(150 * 640 + 108);
        check(bent_control[1] > bent_control[0] + 60 && bent_control[1] > bent_control[2] + 60,
              "Bent morphed surface did not display its native control");
        auto mesh_hit = frame.pick_ui_surface(108, 150);
        check(mesh_hit && mesh_hit->mesh && mesh_hit->mesh->mesh == bent_id.id &&
                  mesh_hit->mesh->lod == 0 && mesh_hit->mesh->part == 0,
              "Retained non-quad surface did not identify its actual LOD and part");
        near(mesh_hit->uv[0], .25, "Morphed mesh U differs from visible native control");
        near(mesh_hit->uv[1], .25, "Morphed mesh V differs from visible native control");
        const auto mesh_projected = frame.project_ui_surface(*mesh_hit);
        check(bool(mesh_projected), "Bent mesh reverse projection unavailable");
        near((*mesh_projected)[0], 108, "Bent mesh inverse did not use the morphed pose");
        near((*mesh_projected)[1], 150, "Bent mesh inverse lost authored UV0");
        auto other_part = *mesh_hit;
        other_part.uv = {.75, .25};
        const auto other_projection = frame.project_ui_surface(other_part);
        check(bool(other_projection), "IME-style projection did not cross a mesh part boundary");
        near((*other_projection)[0], 228,
             "Second mesh part projected to the wrong native image location");
        const auto other_hit = frame.pick_ui_surface(228, 150);
        check(other_hit && other_hit->mesh && other_hit->mesh->part == 1,
              "Second mesh part was not interactive");
        check(ui.mouse_move(108, 150, 0) && ui.mouse_button(0, true, 0) &&
                  ui.mouse_button(0, false, 0),
              "Visible bent surface did not consume its native click");
        const auto bent_command = ui.pending_command();
        check(bent_command && bent_command->at("command") == "Step",
              "Bent surface click bypassed the existing native command owner");
        ui.acknowledge(gate.dispatch(*bent_command, [](const Json&) {}));
        const auto mesh_drag = frame.pick_ui_surface(320, 150, mesh_hit);
        check(mesh_drag && mesh_drag->mesh && mesh_drag->uv[0] > 1 &&
                  !frame.pick_ui_surface(320, 150),
              "Mesh drag did not continue beyond the retained boundary triangle");
        const auto drag_projected = frame.project_ui_surface(*mesh_drag);
        check(bool(drag_projected), "Outside-image mesh capture lost its reverse plane");
        near((*drag_projected)[0], 320, "Mesh capture extrapolation disagrees with native pointer");
        const auto bent_screen = screen;
        const auto bent_scene = scene;
        for (auto& camera : scene.cameras)
            if (!camera.camera.render_texture.id)
                camera.camera.orthographic_height = 100;
        draw(scene);
        check(!frame.project_ui_surface(*mesh_hit) && !frame.pick_ui_surface(108, 150, mesh_hit),
              "A LOD replacement retained a stale triangle capture");
        scene = bent_scene;
        draw(scene);
        check(bool(frame.project_ui_surface(*mesh_hit)), "Original LOD did not restore the UV map");
        auto revised = bent;
        std::get<std::vector<float>>(revised.lods[0].parts[0].streams[0].values)[2] = .2f;
        revised.lods[0].parts[0].bounds = mesh_bounds(revised.lods[0].parts[0]);
        resources->preview_mesh(bent_id, revised);
        draw(scene);
        check(!frame.project_ui_surface(*mesh_hit) && !frame.pick_ui_surface(108, 150, mesh_hit),
              "A mesh revision replacement retained a stale triangle capture");
        auto overlap = bent;
        auto overlapping_part = left;
        auto& positions = std::get<std::vector<float>>(overlapping_part.streams[0].values);
        for (std::size_t i = 2; i < positions.size(); i += 3)
            positions[i] -= .2f;
        overlapping_part.bounds = mesh_bounds(overlapping_part);
        overlap.lods[0].parts.push_back(overlapping_part);
        resources->preview_mesh(bent_id, overlap);
        draw(scene);
        check(!frame.pick_ui_surface(108, 150),
              "Overlapping UV islands supplied an ambiguous world-space input target");

        auto invalid_uv = bent;
        std::get<std::vector<float>>(invalid_uv.lods[0].parts[0].streams[1].values)[0] = 1.25f;
        resources->preview_mesh(bent_id, invalid_uv);
        draw(scene);
        check(!frame.pick_ui_surface(108, 150) &&
                  std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                              [](const auto& d) {
                                  return d.category == "render.ui_surface.profile" &&
                                         d.text.find("UV0") != std::string::npos;
                              }),
              "Out-of-image UV0 did not diagnose and disable mesh input");
        auto degenerate_uv = bent;
        std::get<std::vector<float>>(degenerate_uv.lods[0].parts[0].streams[1].values)
            .assign(12, 0);
        resources->preview_mesh(bent_id, degenerate_uv);
        draw(scene);
        check(!frame.pick_ui_surface(108, 150) &&
                  std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                              [](const auto& d) {
                                  return d.category == "render.ui_surface.profile" &&
                                         d.text.find("degenerate UV0") != std::string::npos;
                              }),
              "Degenerate UV0 did not diagnose and disable mesh input");
        // A degenerate-position mesh spends CPU query work without a costly
        // raster workload. Exhausting a later surface rejects the earlier Quad
        // candidate too: the complete Scene query never returns partial evidence.
        MeshPart expensive;
        expensive.vertices = 3;
        expensive.streams = {{"POSITION", 3, std::vector<float>(9, 0)},
                             {"TEXCOORD_0", 2, std::vector<float>{0, 0, 1, 0, 0, 1}}};
        expensive.indices.reserve(540000);
        for (unsigned i = 0; i < 180000; ++i)
            expensive.indices.insert(expensive.indices.end(), {0, 1, 2});
        expensive.bounds = mesh_bounds(expensive);
        MeshData bounded;
        bounded.lods = {{1, {std::move(expensive)}}};
        resources->preview_mesh(bent_id, bounded);
        scene = baseline;
        auto costly = bent_screen;
        costly.entity = EntityId::generate();
        scene.meshes.push_back(costly);
        scene.ui_surfaces.push_back(
            {costly.entity, {{output}, true, 1}, std::uint32_t(scene.meshes.size() - 1)});
        draw(scene);
        check(!frame.pick_ui_surface(84, 150),
              "Mesh query budget exhaustion delivered an earlier partial surface hit");

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
