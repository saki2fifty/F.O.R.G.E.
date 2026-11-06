#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "render_texture_fixture.hpp"
#include "runtime_reflection_captures.hpp"
#include "sprite_draw_render_tests.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/transform_components.hpp>
#include <iostream>
#include <thread>
using namespace Diligent;
using namespace forge;
namespace {
const auto check = test_bloom::check;
RenderScene fixture() {
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    scene.settings.environment.sky = false;
    RenderMesh source;
    source.entity = EntityId::generate();
    source.renderer.mesh = engine_primitive(0);
    source.renderer.materials = {{"surface", engine_material()}};
    source.renderer.layers = 1;
    source.renderer.cast_shadows = false;
    source.world.m[0] = source.world.m[5] = source.world.m[10] = 4;
    source.world.m[3] = 5;
    source.world.m[11] = 3;
    using T = MaterialParameterType;
    source.renderer.material_parameters = {
        {"surface", "baseColorFactor", {T::LinearColor4, {0, 0, 0, 1}}},
        {"surface", "emissiveFactor", {T::LinearColor3, {1, 0, 0}}},
        {"surface", "emissiveStrength", {T::Scalar, {8}}}};
    scene.meshes.push_back(source);
    RenderMesh receiver;
    receiver.entity = EntityId::generate();
    receiver.renderer.mesh = engine_primitive(0);
    receiver.renderer.materials = {{"surface", engine_material()}};
    receiver.renderer.layers = 2;
    receiver.world.m[11] = 3;
    receiver.renderer.material_parameters = {
        {"surface", "baseColorFactor", {T::LinearColor4, {1, 1, 1, 1}}},
        {"surface", "metallicFactor", {T::Scalar, {0}}},
        {"surface", "roughnessFactor", {T::Scalar, {1}}}};
    scene.meshes.push_back(receiver);
    Camera camera;
    camera.layers = 2;
    camera.background_r = camera.background_g = camera.background_b = 0;
    scene.cameras.push_back({EntityId::generate(), camera, {}});
    ReflectionProbe probe;
    probe.runtime_capture = true;
    probe.capture_size = 16;
    probe.capture_interval = 1;
    probe.capture_layers = 1;
    probe.diffuse_intensity = 1;
    probe.intensity = 1;
    AffineTransform center;
    center.m[11] = 3;
    scene.reflection_probes.push_back({EntityId::generate(), probe, center});
    return scene;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected runtime reflection scratch");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Runtime reflection device unavailable");
        DiligentPresentation presentation(device);
        auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
        host->catalog(std::make_shared<const AssetCatalog>(root));
        auto scene = fixture();
        auto& probe = scene.reflection_probes.front();
        MeshSceneRenderer materials(host, TEX_FORMAT_RGBA16_FLOAT);
        RuntimeReflectionCaptures capture(presentation, host);
        const auto end = [&] {
            capture.submit();
            host->submit();
            context->Flush();
            context->FinishFrame();
            device->ReleaseStaleResources();
        };
        const auto update = [&](double seconds) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            do {
                presentation.begin_frame();
                scene.simulation_seconds = seconds;
                materials.update(scene);
                capture.render(context, scene, materials, seconds, nullptr);
                presentation.end_frame(context);
                end();
                check(std::chrono::steady_clock::now() < deadline,
                      "Runtime capture resources stalled");
            } while (capture.pending());
            check(capture.diagnostics().empty(), "Runtime capture issued unexpected diagnostic");
            return capture.render(context, scene, materials, seconds, nullptr);
        };
        auto first = update(0);
        check(first.size() == 1, "Unassigned runtime probe did not capture");
        auto original = first.at(probe.entity);
        const auto radiance = test_bloom::read(device, context, original.get().source);
        check(radiance[8 * 16 + 8][0] > 7 && radiance[8 * 16 + 8][1] == 0 &&
                  radiance[8 * 16 + 8][3] == 1,
              "Runtime +X face lost linear HDR or opacity");
        check(complete_environment_cube(
                  original.get().specular->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)) &&
                  complete_environment_cube(
                      original.get().diffuse->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)),
              "Runtime capture did not convolve complete lighting maps");
        scene.meshes[0].renderer.material_parameters[1].parameter.value = {0, 1, 0};
        auto held = update(.5);
        check(held.at(probe.entity).source() == original.source() &&
                  test_bloom::read(device, context, held.at(probe.entity).get().source) == radiance,
              "Runtime refresh advanced before its simulation interval");
        auto refreshed = update(1);
        const auto green =
            test_bloom::read(device, context, refreshed.at(probe.entity).get().source);
        check(green[8 * 16 + 8][1] > 7 && green[8 * 16 + 8][0] == 0 &&
                  refreshed.at(probe.entity).source() != original.source(),
              "Runtime source change did not publish a new complete cube");
        check(test_bloom::read(device, context, original.get().source) == radiance,
              "Capture overwrote the last ready cube while staging");
        original = {};
        first.clear();
        held.clear();
        refreshed.clear();
        probe.probe.capture_size = 32;
        auto resized = update(1.1);
        check(resized.at(probe.entity).get().source->GetDesc().Width == 32,
              "Capture size change retained wrong configuration");
        resized.clear();
        capture.invalidate();
        probe.probe.runtime_capture = false;
        check(update(2).empty(), "Disabled runtime capture retained its map");
        probe.probe.runtime_capture = true;
        auto second_probe = probe;
        second_probe.entity = EntityId::generate();
        auto third_probe = probe;
        third_probe.entity = EntityId::generate();
        scene.reflection_probes.push_back(second_probe);
        auto two = update(2.5);
        check(two.size() == 2 && two.at(scene.reflection_probes[0].entity).get().source !=
                                     two.at(scene.reflection_probes[1].entity).get().source,
              "Two runtime probes shared mutable capture pixels or omitted a producer");
        auto inactive = third_probe;
        inactive.probe.enabled = false;
        for (unsigned i = 0; i < 9; ++i) {
            inactive.entity = EntityId::generate();
            scene.reflection_probes.push_back(inactive);
        }
        check(update(2.6).size() == 2, "Inactive probes consumed the runtime profile");
        scene.reflection_probes.resize(2);
        auto failed_scene = scene;
        failed_scene.simulation_seconds = 4;
        failed_scene.settings.distance_fog.end = -1;
        const auto retained = capture.render(context, failed_scene, materials, 4, nullptr);
        check(retained.size() == 2 && !capture.diagnostics().empty() &&
                  retained.at(scene.reflection_probes[0].entity).source() ==
                      two.at(scene.reflection_probes[0].entity).source(),
              "Failed refresh discarded the previous complete cube");
        two.clear();
        scene.reflection_probes.push_back(third_probe);
        materials.update(scene);
        check(capture.render(context, scene, materials, 3, nullptr).empty() &&
                  !capture.diagnostics().empty(),
              "Runtime capacity selected an arbitrary subset");
        scene.reflection_probes.resize(1);
        // The complete frame consumes the same runtime map for ordinary diffuse shading.
        FrameRenderer frame(presentation);
        frame.resources(host);
        const auto draw = [&](double seconds) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            do {
                presentation.begin_frame();
                scene.simulation_seconds = seconds;
                const auto cameras = prepare_game_cameras(scene, 32, 32);
                frame.render(context, scene, cameras.cameras, 32, 32, 0, nullptr, seconds, nullptr,
                             FrameRenderer::Output::LinearHdr);
                presentation.end_frame(context);
                host->submit();
                context->Flush();
                context->FinishFrame();
                device->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline,
                      "Runtime reflection frame stalled");
            } while (frame.pending());
            if (!frame.diagnostics().empty())
                throw std::runtime_error(frame.diagnostics().front().text);
            return test_bloom::read(device, context, frame.output()->GetTexture());
        };
        const auto green_frame = draw(4);
        scene.meshes[0].renderer.material_parameters[1].parameter.value = {1, 0, 0};
        const auto red_frame = draw(5);
        double red = 0, green_sum = 0;
        for (unsigned i = 0; i < red_frame.size(); ++i) {
            red += red_frame[i][0] - green_frame[i][0];
            green_sum += green_frame[i][1] - red_frame[i][1];
        }
        check(red > 1 && green_sum > 1, "Runtime map changes did not reach diffuse receivers");
        frame.invalidate_temporal_history();
        const auto reset = draw(0);
        check(reset == red_frame, "Session reset retained stale runtime illumination");
        scene.meshes[0].renderer.material_parameters[1].parameter.value = {0, 1, 0};
        check(draw(0) == reset, "Paused capture advanced before an explicit session reset");
        frame.invalidate_temporal_history();
        check(draw(0) != reset, "Same-time session reset retained the preceding world's capture");
        scene.meshes[0].renderer.material_parameters[1].parameter.value = {1, 0, 0};
        EngineContext editor_engine;
        Scene authored(editor_engine.world());
        authoring_command(authored, "scene.rendering.set", {{"shadows", {{"enabled", false}}}});
        std::string source_id;
        for (const auto& mesh : scene.meshes) {
            const std::string id = authoring_command(authored, "entity.create",
                                                     {{"name", "Capture geometry"}, {"kind", 0}})
                                       .at("selected");
            authored.entity(id).set<MeshRenderer>(mesh.renderer);
            authored.entity(id).set<LocalTranslation>(
                {mesh.world.m[3], mesh.world.m[7], mesh.world.m[11]});
            authored.entity(id).set<LocalScale>(
                {float(mesh.world.m[0]), float(mesh.world.m[5]), float(mesh.world.m[10])});
            if (source_id.empty())
                source_id = id;
        }
        const std::string probe_id =
            authoring_command(authored, "entity.create",
                              {{"name", "Preview probe"}, {"recipe", "render.reflection_probe"}})
                .at("selected");
        authored.entity(probe_id).set<ReflectionProbe>(scene.reflection_probes[0].probe);
        authored.entity(probe_id).set<LocalTranslation>({0, 0, 3});
        forge::Viewport preview(presentation, true);
        preview.resources(host);
        preview.preview_lighting(false);
        EditorCamera orbit;
        orbit.target = {0, 0, 3};
        orbit.distance = 3;
        orbit.yaw = orbit.pitch = 0;
        const auto draw_preview = [&](double seconds, unsigned generation) {
            auto document = authored.preview_document(authored.document());
            document["simulation_seconds"] = seconds;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            ITextureView* output = nullptr;
            do {
                presentation.begin_frame();
                output =
                    preview.render(context, document, 32, 32, orbit, generation, false, {false});
                presentation.end_frame(context);
                host->submit();
                context->Flush();
                context->FinishFrame();
                device->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline, "Scene preview capture stalled");
            } while (preview.pending());
            if (!preview.diagnostics().empty())
                throw std::runtime_error(preview.diagnostics().front().text);
            return test_sprite_draw::read_pixels(device, context, output->GetTexture());
        };
        const auto preview_red = draw_preview(0, 1);
        auto source_mesh = authored.entity(source_id).get<MeshRenderer>();
        source_mesh.material_parameters[1].parameter.value = {0, 1, 0};
        authored.entity(source_id).set<MeshRenderer>(source_mesh);
        const auto preview_held = draw_preview(.5, 2);
        const auto preview_green = draw_preview(1, 3);
        unsigned changed = 0;
        for (unsigned y = 10; y < 22; ++y)
            for (unsigned x = 10; x < 22; ++x) {
                const auto at = y * 32 + x;
                check(preview_red[at] == preview_held[at],
                      "Scene preview bypassed simulation refresh interval");
                changed += preview_red[at] != preview_green[at];
            }
        check(changed > 20, "Scene preview retained the old capture after a completed refresh");
        preview.invalidate_temporal_history();
        const auto preview_zero_green = draw_preview(0, 4);
        source_mesh.material_parameters[1].parameter.value = {1, 0, 0};
        authored.entity(source_id).set<MeshRenderer>(source_mesh);
        const auto preview_zero_held = draw_preview(0, 5);
        preview.invalidate_temporal_history();
        const auto preview_zero_red = draw_preview(0, 6);
        unsigned reset_changed = 0;
        for (unsigned y = 10; y < 22; ++y)
            for (unsigned x = 10; x < 22; ++x) {
                const auto at = y * 32 + x;
                check(preview_zero_held[at] == preview_zero_green[at],
                      "Paused preview ignored its interval");
                reset_changed += preview_zero_green[at] != preview_zero_red[at];
            }
        check(reset_changed > 20, "Same-time Scene session reset retained old runtime lighting");
        // Camera-output views borrow the parent's completed map and never recapture.
        std::filesystem::create_directories(root / "Assets");
        atomic_write(root / "forge.assets.json",
                     Json{{"version", 1}, {"assets", Json::array()}}.dump());
        const auto lease = std::make_shared<ProjectLease>(root);
        const auto output_document =
            test_render_texture::output(lease, "Assets/Lighting.rendertexture.json");
        const auto output_id = output_document->source().asset();
        const auto screen =
            test_render_texture::material(lease, "Assets/Feedback.material.json", output_id);
        host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        auto red_maps = update(10);
        auto output_scene = scene;
        auto output_camera = output_scene.cameras[0];
        output_camera.entity = EntityId::generate();
        output_camera.camera.render_texture = {output_id};
        output_scene.cameras.push_back(output_camera);
        CameraRenderTextures outputs(presentation, host);
        MeshSceneRenderer output_materials(host, TEX_FORMAT_RGBA16_FLOAT);
        const auto draw_output = [&](const RuntimeReflectionImages& maps, double seconds) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (true) {
                presentation.begin_frame();
                outputs.render(context, output_scene, output_materials, 2, seconds, nullptr,
                               nullptr, &maps);
                presentation.end_frame(context);
                outputs.submit();
                host->submit();
                context->Flush();
                context->FinishFrame();
                device->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline,
                      "Runtime-lit camera output stalled");
                if (!outputs.pending())
                    break;
            }
            if (!outputs.diagnostics().empty())
                throw std::runtime_error(outputs.diagnostics().front().text);
            check(outputs.inputs().produced.contains(output_id),
                  "Runtime-lit output was not produced");
            return test_bloom::read(device, context, outputs.inputs().images.at(output_id).get());
        };
        const auto red_output = draw_output(red_maps, 10);
        scene.meshes[0].renderer.material_parameters[1].parameter.value = {0, 1, 0};
        auto green_maps = update(11);
        const auto green_output = draw_output(green_maps, 11);
        double output_red = 0, output_green = 0;
        for (unsigned i = 0; i < red_output.size(); ++i) {
            output_red += red_output[i][0] - green_output[i][0];
            output_green += green_output[i][1] - red_output[i][1];
            check(red_output[i][3] == 1 && green_output[i][3] == 1,
                  "Runtime-lit camera output lost opaque alpha");
        }
        check(output_red > 1 && output_green > 1,
              "Camera-output views did not consume runtime lighting");
        auto& feedback_mesh = scene.meshes[0].renderer;
        feedback_mesh.materials = {{"surface", {screen}}};
        feedback_mesh.material_parameters.clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (true) {
            materials.update(scene);
            try {
                if (!materials.render_texture_reads(scene, 1, true).empty())
                    break;
            } catch (const RenderTexturePending&) {
            }
            check(std::chrono::steady_clock::now() < deadline,
                  "Feedback material selection stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        scene.simulation_seconds = 12;
        const auto failed_maps = capture.render(context, scene, materials, 12, nullptr);
        check(failed_maps.size() == 1 && !capture.diagnostics().empty() &&
                  capture.diagnostics().front().text.find("camera-output reader") !=
                      std::string::npos &&
                  failed_maps.begin()->second.source() == green_maps.begin()->second.source(),
              "Capture feedback failed to retain the prior complete map with a useful error");
        RuntimeReflectionCaptures empty_capture(presentation, host);
        check(empty_capture.render(context, scene, materials, 12, nullptr).empty() &&
                  !empty_capture.diagnostics().empty(),
              "Initial capture feedback published invalid pixels");
        RuntimeReflectionResidency constrained(device, context, 1, 1,
                                               RuntimeReflectionRealization{&presentation});
        bool budget_rejected = false;
        try {
            (void)constrained.acquire({EntityId::generate(), 1, 0}, 16);
        } catch (const std::exception&) {
            budget_rejected = true;
        }
        check(budget_rejected && constrained.statistics().payload_bytes == 0,
              "Runtime target exceeded its physical owner budget");
        std::cout << "Runtime GPU "
                     "cube/HDR/convolution/interval/staging/resize/capacity/receiver/reset PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
