#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/reflection_capture_host.hpp"
#include "editor/view_image.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include "texture_authoring.hpp"
#include <forge/game_content.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
using Pixels = test_sprite_draw::Pixels;
std::pair<Pixels, Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                                 const std::filesystem::path& root, AssetId scene,
                                 unsigned width = 320, unsigned height = 180) {
    auto resources = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    resources->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    EngineContext engine(WorldRole::Runtime, false);
    Scene source(engine.world());
    source.restore_snapshot(load_game_scene(root, {scene}));
    const auto snapshot = extract_render_scene(source.effective_document());
    FrameRenderer frame(presentation);
    frame.resources(resources);
    const auto draw = [&](const RenderScene& value) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = frame.game(context, value, width, height, 1);
            presentation.end_frame(context);
            resources->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, output->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Captured map consumer stalled");
        } while (frame.pending());
        check(frame.diagnostics().empty(), "Captured map consumer reported a rendering error");
        return pixels;
    };
    const auto enabled = draw(snapshot);
    check(draw(snapshot) == enabled, "Captured map did not produce a stable Scene image");
    auto disabled = snapshot;
    disabled.reflection_probes.clear();
    const auto off = draw(disabled);
    unsigned changed = 0;
    for (std::size_t i = 0; i < enabled.size(); ++i) {
        changed += enabled[i] != off[i];
        check(enabled[i][3] == off[i][3], "Captured reflection changed destination alpha");
    }
    check(changed > 20, "Captured map has no visible local reflection consumer");
    std::cout << "Captured map Scene consumer: " << changed << " changed pixels\n";
    return {enabled, off};
}
void save(const std::filesystem::path& path, const Pixels& pixels, unsigned width,
          unsigned height) {
    std::ofstream stream(path, std::ios::binary);
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& pixel : pixels)
        stream.write(reinterpret_cast<const char*>(pixel.data()), 3);
    check(bool(stream.flush()), "Capture Gallery image write failed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || (argc == 5 && std::string(argv[4]) == "--prepare-gallery"),
              "Need Gallery, asset worker, scratch directory and optional --prepare-gallery");
        const bool prepare_gallery = argc == 5;
        const auto gallery = std::filesystem::absolute(argv[1]);
        const auto worker = std::filesystem::absolute(argv[2]);
        const auto scratch = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        const auto root = scratch / "Project";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Capture workflow Vulkan device unavailable");
        DiligentPresentation presentation(device);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root, true);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const auto prior_records = catalog->document().at("assets");
        auto scene_id = AssetId::parse("03d7f430-77ba-4c78-b75a-ff3a638ea3d9");
        project.open_scene(root / catalog->records().at(scene_id).source);
        if (prepare_gallery) {
            project.save_as(root / "Scenes/110-reflection-map-capture.scene.json");
            scene_id = scene.asset_id();
            authoring_command(scene, "scene.rendering.set",
                              {{"screen_space_reflections", {{"enabled", false}}}});
            const auto field = [&](const std::string& entity, const char* component,
                                   const char* name, const Json& value) {
                authoring_command(scene, "property.set",
                                  {{"entity", entity},
                                   {"component", component},
                                   {"field", name},
                                   {"value", value}});
            };
            const auto entities = scene.document().at("entities");
            Json mirror;
            for (const auto& row : entities)
                if (row.value("name", std::string{}).starts_with("Rough metal floor"))
                    mirror =
                        row.at("components").at("forge.mesh_renderer").at("material_parameters");
            check(mirror.is_array(), "Capture fixture lost its existing typed metal parameters");
            for (auto& parameter : mirror) {
                if (parameter.at("key") == "baseColorFactor")
                    parameter["parameter"]["value"] = {.9, .9, .9, 1.};
                if (parameter.at("key") == "roughnessFactor")
                    parameter["parameter"]["value"][0] = .08;
            }
            for (const auto& row : entities) {
                if (row.at("components").contains("forge.camera"))
                    field(row.at("id"), "forge.camera", "layers", 2u);
                if (!row.at("components").contains("forge.mesh_renderer"))
                    continue;
                const bool floor =
                    row.value("name", std::string{}).starts_with("Rough metal floor");
                field(row.at("id"), "forge.mesh_renderer", "layers", 3u);
                if (floor) {
                    authoring_command(
                        scene, "entity.rename",
                        {{"entity", row.at("id")},
                         {"name", "Captured-map metal floor - compare the saved reflection"}});
                    field(row.at("id"), "forge.mesh_renderer", "material_parameters", mirror);
                }
            }
            for (int side : {-1, 1}) {
                const auto sphere =
                    authoring_command(scene, "entity.create",
                                      {{"recipe", "primitive.1"},
                                       {"name", side < 0 ? "Left captured-map sphere - layer 2"
                                                         : "Right captured-map sphere - layer 2"},
                                       {"position", {{"x", side * 1.3}, {"y", -.15}, {"z", -.3}}}})
                        .at("selected")
                        .get<std::string>();
                for (const auto axis : {"x", "y", "z"})
                    field(sphere, "forge.local_scale", axis, 1.7);
                field(sphere, "forge.mesh_renderer", "layers", 2u);
                field(sphere, "forge.mesh_renderer", "material_parameters", mirror);
            }
            for (int side : {-1, 1}) {
                const auto panel =
                    authoring_command(scene, "entity.create",
                                      {{"recipe", "primitive.0"},
                                       {"name", side < 0 ? "Amber capture source - layer 1"
                                                         : "Blue capture source - layer 1"},
                                       {"position", {{"x", side * 2.5}, {"y", .5}, {"z", -4.}}}})
                        .at("selected")
                        .get<std::string>();
                field(panel, "forge.local_scale", "x", 5.);
                field(panel, "forge.local_scale", "y", 5.);
                field(panel, "forge.local_scale", "z", .1);
                field(panel, "forge.mesh_renderer", "layers", 1u);
                const Json color =
                    side < 0 ? Json::array({.9, .22, .015, 0.}) : Json::array({.015, .3, .95, 0.});
                const Json parameters =
                    Json::array({{{"slot", "surface"},
                                  {"key", "baseColorFactor"},
                                  {"parameter",
                                   {{"type", unsigned(MaterialParameterType::LinearColor4)},
                                    {"value", {0., 0., 0., 1.}}}}},
                                 {{"slot", "surface"},
                                  {"key", "emissiveFactor"},
                                  {"parameter",
                                   {{"type", unsigned(MaterialParameterType::LinearColor3)},
                                    {"value", color}}}},
                                 {{"slot", "surface"},
                                  {"key", "emissiveStrength"},
                                  {"parameter",
                                   {{"type", unsigned(MaterialParameterType::Scalar)},
                                    {"value", {2., 0., 0., 0.}}}}}});
                field(panel, "forge.mesh_renderer", "material_parameters", parameters);
            }
        }
        const auto probe =
            authoring_command(scene, "entity.create",
                              {{"recipe", "render.reflection_probe"},
                               {"name", "Captured reflection volume - create a new snapshot"}})
                .at("selected")
                .get<std::string>();
        for (const auto field : {"half_extent_x", "half_extent_y", "half_extent_z"})
            authoring_command(scene, "property.set",
                              {{"entity", probe},
                               {"component", "forge.reflection_probe"},
                               {"field", field},
                               {"value", 20.}});
        authoring_command(scene, "property.set",
                          {{"entity", probe},
                           {"component", "forge.reflection_probe"},
                           {"field", "blend_distance"},
                           {"value", 0.}});
        if (prepare_gallery)
            authoring_command(scene, "property.set",
                              {{"entity", probe},
                               {"component", "forge.reflection_probe"},
                               {"field", "box_projection"},
                               {"value", false}});
        project.save();
        auto settings = project.settings().document();
        settings["startup_scene"] = {
            {"asset", scene_id},
            {"source", path_text(ProjectPaths(root).relative(project.path()))}};
        project.save_settings(settings);
        const auto before = scene.document();
        auto resources = std::make_shared<MeshResourceHost>(presentation, context, root, true);
        resources->catalog(catalog);
        SourceImport source;
        const auto registry = texture_import_registry(worker);
        const AssetReimportRoute route{"forge.texture.container",
                                       {"windows", "d3d12", "desktop"},
                                       registry,
                                       [](auto& candidate, const auto& plan, const auto&) {
                                           prepare_texture_publication(candidate, plan);
                                       }};
        source.routes = [route] { return std::vector{route}; };
        source.published = [&](AssetId, auto value) {
            catalog = std::move(value);
            resources->catalog(catalog);
        };
        ContentImports imports;
        imports.routes = source.routes;
        std::string message;
        ui::ReflectionCaptureRequest request;
        request.settings.size = prepare_gallery ? 128 : 33;
        request.settings.layers = prepare_gallery ? 1u : UINT32_MAX;
        request.destination =
            prepare_gallery ? "Assets/ReflectionCapture" : "Assets/WorkflowCapturedMap";
        auto work = std::make_unique<ui::ReflectionCaptureTask>(
            scene, project, source, probe, request,
            [&](RenderScene snapshot, const auto& options, EntityId id) {
                check(snapshot.meshes.size() == (prepare_gallery ? 17 : 13) &&
                          snapshot.lights.size() == 1,
                      "Capture workflow lost the actual lit/material Scene producers");
                return std::make_unique<ui::ReflectionCaptureHost>(
                    presentation, resources, std::move(snapshot), options, id);
            },
            [&] { return catalog; }, [] { return std::string{}; });
        auto* result = work.get();
        work->reveal(false);
        ui::ViewImageTask images;
        check(images.begin_work(std::move(work), root, scene_id, message),
              "Capture image owner is busy");
        const auto tick = [&] {
            presentation.begin_frame();
            images.poll_work(context, message);
            presentation.end_frame(context);
            resources->submit();
            context->Flush();
            context->FinishFrame();
            device->ReleaseStaleResources();
            source.poll(project, imports, message);
            imports.suspend(source.busy());
            imports.poll(project, message);
        };
        const auto wait = [&](auto done) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
            while (!done()) {
                tick();
                check(std::chrono::steady_clock::now() < deadline &&
                          result->state().at("error") == "",
                      "Capture workflow failed/stalled: " + result->state().dump());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        wait([&] { return source.review_ready(); });
        check(scene.document() == before && !std::filesystem::exists(root / request.destination),
              "Native capture/review mutated authored content before commit");
        const auto review = source.operation();
        source.automation_commit(review.at("batch"), review.at("review"), true);
        wait([&] { return !images.busy(); });
        check(result->state().at("assigned") == true && source.published_count() == 1,
              "Native capture did not publish and assign through the image owner");
        const auto map = result->state().at("asset").get<AssetId>();
        const auto loaded = asset_detail::load_texture_selection(root, *catalog, {map});
        const auto& texture = *loaded.value;
        check(texture.dimension == TextureDimension::Cube &&
                  texture.format == TextureFormat::RGBA16Float &&
                  texture.semantic == TextureSemantic::HdrColor && texture.subresources.size() == 6,
              "Native capture publication/resource selection changed HDR cube shape");
        const auto size = request.settings.size;
        const auto middle = (std::size_t(size / 2) * size + size / 2) * 8;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto at = middle + channel * 2;
            const auto bits = std::to_integer<unsigned>(texture.subresources[2][at]) |
                              (std::to_integer<unsigned>(texture.subresources[2][at + 1]) << 8);
            check(bits >= 0x31c2 && bits <= 0x31c4,
                  "Hidden display sky did not supply the source environment's 0.18 HDR radiance");
        }
        auto shadow_scene = extract_render_scene(scene.effective_document());
        check(!shadow_scene.settings.environment.sky && !shadow_scene.settings.shadows.enabled,
              "The authored capture fixture lost its hidden sky or isolated shadows");
        shadow_scene.settings.shadows = {true, 128, 1, 1, 25};
        for (auto& light : shadow_scene.lights)
            light.light.cast_shadows = true;
        for (auto& mesh : shadow_scene.meshes)
            mesh.renderer.cast_shadows = mesh.renderer.receive_shadows = true;
        auto shadow_options = request.settings;
        shadow_options.position = {0, 1, 0};
        ReflectionCapture shadow_capture(presentation, resources, std::move(shadow_scene),
                                         shadow_options, EntityId::parse(probe));
        const auto shadow_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (shadow_capture.busy()) {
            presentation.begin_frame();
            shadow_capture.step(context);
            presentation.end_frame(context);
            resources->submit();
            context->Flush();
            context->FinishFrame();
            device->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < shadow_deadline,
                  "Shadowed reflection capture stalled");
        }
        check(shadow_capture.state() == ReflectionCapture::State::Ready,
              "Shadowed reflection capture failed: " + shadow_capture.diagnostic());
        const auto shadowed = shadow_capture.take();
        unsigned shadow_changes = 0;
        for (unsigned face = 0; face < 6; ++face)
            for (std::size_t pixel = 0; pixel < shadowed.subresources[face].size(); pixel += 8)
                shadow_changes += !std::equal(shadowed.subresources[face].begin() + pixel,
                                              shadowed.subresources[face].begin() + pixel + 6,
                                              texture.subresources[face].begin() + pixel);
        check(shadow_changes > 20, "Source shadows did not reach the captured HDR map");
        std::cout << "Source shadows: " << shadow_changes
                  << " changed HDR pixels; hidden sky radiance PASS\n";
        const auto selected_bytes = encode_texture(texture);
        project.save();
        source.automation_close(review.at("batch"));
        const std::array roots{scene_id};
        prepare_runtime_content_catalog(*project.writer_guard(), roots, {});
        if (prepare_gallery) {
            const auto records = AssetCatalog::open_project(root).document().at("assets");
            check(records.size() == prior_records.size() + 2,
                  "Capture Gallery added unrelated project asset identities");
            for (const auto& record : prior_records)
                check(std::find(records.begin(), records.end(), record) != records.end(),
                      "Capture Gallery changed an existing asset record");
        }
        const unsigned width = prepare_gallery ? 640 : 320, height = prepare_gallery ? 360 : 180;
        const auto authored = render(presentation, context, root, scene_id, width, height);
        const auto package = scratch / "Export";
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated";
        std::filesystem::rename(package, moved);
        const auto runtime_catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(!std::filesystem::exists(moved / request.destination / "Reflection.dds"),
              "Source-free runtime package copied the generated DDS source");
        const auto runtime = asset_detail::load_texture_selection(moved, runtime_catalog, {map});
        check(encode_texture(*runtime.value) == selected_bytes,
              "Source-free relocation changed the captured HDR payload or identity");
        check(render(presentation, context, moved, scene_id, width, height) == authored,
              "Relocated runtime did not render both captured-map states exactly");
        if (prepare_gallery) {
            save(scratch / "reflection-capture-enabled-640x360.ppm", authored.first, width, height);
            save(scratch / "reflection-capture-disabled-640x360.ppm", authored.second, width,
                 height);
            const auto tall = render(presentation, context, root, scene_id, 360, 640);
            check(render(presentation, context, moved, scene_id, 360, 640) == tall,
                  "Capture Gallery source-free tall renders differ");
            save(scratch / "reflection-capture-enabled-360x640.ppm", tall.first, 360, 640);
            save(scratch / "reflection-capture-disabled-360x640.ppm", tall.second, 360, 640);
            const Json receipt{{"project", path_text(root)},
                               {"scene", scene_id},
                               {"map", map},
                               {"probe", probe},
                               {"size", size},
                               {"unchanged_records", prior_records.size()},
                               {"added_records", 2},
                               {"exact_relocated_images", 4},
                               {"execution", "software Vulkan; no Windows/D3D12 acceptance"}};
            asset_storage::replace(scratch / "gallery-preparation.json", receipt.dump(2));
            std::cout << "Capture Gallery preparation: "
                      << path_text(scratch / "gallery-preparation.json") << '\n';
        }
        std::cout
            << "Actual lit Scene HDR capture → generated review/import → probe assignment/save → "
               "typed resource → source-free relocated on/off render exact PASS; software Vulkan "
               "only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
