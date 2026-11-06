#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "ImGuiImplDiligent.hpp"
#include "editor/camera_controls.hpp"
#include "editor/paint_texture_editor.hpp"
#include "editor/source_document_automation.hpp"
#include "editor/viewport.hpp"
#include "fixtures/mesh_texture_paint_gallery.hpp"
#include "gallery_game_capture.hpp"
#include "imgui_input_driver.hpp"
#include "paint_texture_preview.hpp"
#include "render_texture_fixture.hpp"
#include "view_image.hpp"
#include "view_image_capture_reference.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void exercise_gallery(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                      const std::filesystem::path& gallery, const std::filesystem::path& output) {
    const auto scratch = output / AssetId::generate().str();
    const auto source = scratch / "SourceProject";
    std::filesystem::create_directories(scratch);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    Json document;
    std::ifstream(source / test::mesh_paint_scene_path) >> document;
    const auto scene_id = document.at("asset_id").get<AssetId>();
    const auto before = test_gallery_capture::render(presentation, graphics, source, scene_id,
                                                     scratch / "authored");
    const std::array roots{scene_id};
    package_runtime_content(source, scratch / "Export", roots, {"windows", "d3d12"});
    std::filesystem::rename(scratch / "Export", scratch / "Relocated");
    std::filesystem::rename(source, scratch / "SourcesOffline");
    check(!std::filesystem::exists(scratch / "Relocated" / test::mesh_paint_image_path),
          "Runtime mesh painting content retained its editable paint source");
    const auto after = test_gallery_capture::render(presentation, graphics, scratch / "Relocated",
                                                    scene_id, scratch / "relocated-images");
    // The shared capture owner emits three moving frames and one portrait view.
    check(before.size() == 4 && before == after,
          "Animated mesh painting pixels differ after source-hidden relocation");
    const Json receipt{
        {"scene", scene_id},
        {"exact_image_pairs", before.size()},
        {"scope",
         "Actual animated RuntimeWorld/FrameRenderer on software Vulkan with source-hidden "
         "relocated Windows-target content; no Windows or physical-device acceptance"}};
    std::ofstream(scratch / "receipt.json") << receipt.dump(2) << '\n';
    std::cout << receipt.dump() << '\n';
    graphics->WaitForIdle();
}
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
              const std::filesystem::path& worker, const std::filesystem::path& output) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "Mesh texture brush");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    auto canvas = PaintTextureDocument::create(
        project.writer_guard(), "Assets/Surface.paint-texture.json", [](AssetId id) {
            return PaintTextureSource{
                PaintTextureValue::create(id, 128, 128, TextureSemantic::Color, {12, 24, 40, 255})};
        });
    const auto texture_id = canvas->source().asset();
    test_render_texture::publish(project.writer_guard(), *canvas, texture_import_registry(worker),
                                 prepare_texture_publication);
    canvas.reset();
    auto material = MaterialDocument::create(
        project.writer_guard(), "Assets/Surface.material.json", [&](AssetId id) {
            auto value = MaterialSource::create(id);
            MaterialData shape;
            shape.model = "forge.gltf.unlit.v1";
            shape.textures["baseColorTexture"].semantic = TextureSemantic::Color;
            shape.textures["baseColorTexture"].sampler.u =
                shape.textures["baseColorTexture"].sampler.v = TextureWrap::ClampEdge;
            value.document["overrides"] = {
                {"model", shape.model},
                {"double_sided", true},
                {"textures",
                 {{"baseColorTexture",
                   {{"asset", texture_id},
                    {"slot",
                     material_values_document(shape).at("textures").at("baseColorTexture")}}}}}};
            return value;
        });
    const auto material_id = material->source().asset();
    test_render_texture::publish(project.writer_guard(), *material, material_import_registry(),
                                 prepare_material_publication);
    material.reset();
    const auto entity =
        authoring_command(scene, "entity.create",
                          {{"recipe", "empty"}, {"name", "Bent two-part paint surface"}})
            .at("selected")
            .get<EntityId>();
    const AssetRef<MeshAsset> mesh_id{AssetId::generate()};
    MeshRenderer renderer;
    renderer.mesh = mesh_id;
    renderer.materials = {{"default", {material_id}}};
    scene.entity(entity.str()).set<MeshRenderer>(renderer).set<LocalTranslation>({0, 0, 0});
    MeshPart left;
    left.vertices = 6;
    left.indices = {0, 3, 1, 1, 3, 4};
    left.streams = {
        {"POSITION", 3,
         std::vector<float>{-1, -1, 0, 0, -1, .2f, 1, -1, 0, -1, 1, 0, 0, 1, .2f, 1, 1, 0}},
        {"TEXCOORD_0", 2, std::vector<float>{0, 0, .5f, 0, 1, 0, 0, 1, .5f, 1, 1, 1}}};
    left.morph_targets = {{{"POSITION", 3,
                            std::vector<float>{.2f, 0, .1f, .2f, 0, .1f, .2f, 0, .1f, .2f, 0, .1f,
                                               .2f, 0, .1f, .2f, 0, .1f}}}};
    left.bounds = mesh_bounds(left);
    auto right = left;
    right.indices = {1, 4, 2, 2, 4, 5};
    MeshData mesh;
    mesh.morph_names = {"Shift"};
    mesh.morph_defaults = {.5f};
    mesh.lods = {{1, {left, right}}};
    auto resources = std::make_shared<MeshResourceHost>(presentation, graphics, root, true);
    resources->preview_mesh(mesh_id, mesh);
    resources->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    Viewport viewport(presentation, true);
    viewport.resources(resources);
    EditorCamera camera;
    camera.target = {0, 0, 0};
    camera.distance = 5;
    PaintTextureEditor editor(worker);
    editor.open(project, "Assets/Surface.paint-texture.json");
    PaintTexturePreview preview(presentation, graphics, root);
    editor.draw_preview = [&](const auto& value, auto session, auto revision, unsigned width,
                              unsigned height) {
        auto* image = preview.render(value, session, revision, width, height);
        return PaintCanvasImage{reinterpret_cast<ImTextureID>(image), preview.current(),
                                preview.error()};
    };
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    io.DeltaTime = 1.f / 60;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(io.Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16), "Missing mesh painting font");
    ui::style(1);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    test::observe_ui = true;
    Diligent::TextureDesc desc;
    desc.Name = "FORGE native model texture painting";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = 1220;
    desc.Height = 850;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> target;
    presentation.device()->CreateTexture(desc, nullptr, &target);
    check(bool(target), "Missing mesh painting UI target");
    auto* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    auto* srv = target->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    io.DisplaySize = {float(desc.Width), float(desc.Height)};
    ImVec2 origin, size;
    bool editable = true, focused = true, show_scene = true;
    std::string message;
    std::optional<MeshTextureSurfaceHit> last_hit;
    Diligent::ITextureView* scene_image = nullptr;
    auto frame = [&] {
        presentation.begin_frame();
        resources->pump();
        editor.poll(project, message);
        if (auto catalog = editor.take_catalog())
            resources->catalog(std::move(catalog));
        test::ui_targets.clear();
        gui.NewFrame(desc.Width, desc.Height, Diligent::SURFACE_TRANSFORM_IDENTITY);
        ImGui::SetWindowSize("Paint Texture###Paint Texture", {520, 850}, ImGuiCond_Always);
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({520, 850}, ImGuiCond_Always);
        editor.draw(project, scene, !editable);
        if (show_scene) {
            ImGui::SetNextWindowPos({520, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({700, 850}, ImGuiCond_Always);
            ImGui::Begin("Scene texture painting", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            ui_context.task.focus(ui::DocumentTask::Scene);
            origin = ImGui::GetCursorScreenPos();
            size = {680, 540};
            ui::ViewportInput input;
            (void)ui::camera_controls(camera, size, focused, &input);
            const auto snapshot = scene.effective_document();
            editor.surface_input(scene, entity, camera, origin, size, focused, editable,
                                 input.hovered, input.activated, input.active,
                                 [&](const MeshTextureSurfaceTarget& selected, double x, double y) {
                                     last_hit = viewport.pick_texture_surface(
                                         snapshot, camera, unsigned(size.x), unsigned(size.y),
                                         entity, selected, x, y);
                                     return last_hit;
                                 });
            auto* image = viewport.render(graphics, snapshot, unsigned(size.x), unsigned(size.y),
                                          camera, scene.revision(), false, {false});
            scene_image = image;
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef{reinterpret_cast<ImTextureID>(image)},
                                                 origin, {origin.x + size.x, origin.y + size.y});
            editor.surface_overlay(origin, size, input.hovered);
            ImGui::TextWrapped(
                "The selected original bent mesh uses the open paint texture. Strokes use current "
                "rendered geometry and UV0. Save / Publish updates the displayed image.");
            ImGui::End();
        }
        graphics->SetRenderTargets(1, &rtv, nullptr,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{.02f, .03f, .04f, 1};
        graphics->ClearRenderTarget(rtv, clear,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        gui.Render(graphics);
        resources->submit();
        presentation.end_frame(graphics);
        graphics->Flush();
        graphics->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            frame();
            check(std::chrono::steady_clock::now() < deadline,
                  "Native surface preparation stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (editor.stroke_active() || !preview.current() || viewport.pending());
    };
    settle();
    test::ImGuiInputDriver input(frame);
    input.click("paint-texture:surface-mode");
    check(editor.surface_enabled(), "Native Mesh paint mode did not activate");
    const auto baseline = editor.document()->source().document;
    test::ViewImageReference before_display;
    before_display.queue(presentation.device(), graphics, scene_image);
    const auto before_pixels = before_display.read(graphics);
    const auto revision = editor.document()->revision();
    const auto drag = [&](bool cancel) {
        const auto y = origin.y + size.y * (cancel ? .43f : .5f);
        io.AddMousePosEvent(origin.x + size.x * .45f, y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        for (unsigned i = 1; i <= 8; ++i) {
            io.AddMousePosEvent(origin.x + size.x * (.45f + .1f * i / 8), y);
            frame();
        }
        if (!editor.stroke_active())
            throw std::runtime_error("Native Scene input did not retain the surface stroke: " +
                                     AssetDocumentAutomation::status(editor, project).dump());
        check(ui_context.task.id() == "paint_texture",
              "Scene brush did not route history to the paint source");
        if (cancel) {
            check(last_hit && editor.document()->source().document.pixel(
                                  std::min(127u, unsigned(last_hit->point.uv[0] * 128)),
                                  std::min(127u, unsigned(last_hit->point.uv[1] * 128))) !=
                                  PaintPixel{255, 255, 255, 255},
                  "Cancellation fixture would not change any pixels");
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
        }
        io.AddMouseButtonEvent(0, false);
        frame();
        settle();
    };
    drag(false);
    check(last_hit && last_hit->mesh == mesh_id.id && editor.document()->revision() == revision + 1,
          "Rendered bent surface did not produce one source edit");
    const auto painted = editor.document()->source().document;
    const auto u = std::min(127u, unsigned(last_hit->point.uv[0] * 128)),
               v = std::min(127u, unsigned(last_hit->point.uv[1] * 128));
    check(painted.pixel(u, v) == PaintPixel{255, 255, 255, 255} && painted != baseline,
          "Surface hit did not paint its source UV pixels");
    input.click("paint-texture:undo");
    settle();
    check(editor.document()->source().document == baseline,
          "Surface stroke Undo lost its exact source");
    input.click("paint-texture:redo");
    settle();
    check(editor.document()->source().document == painted, "Surface stroke Redo changed source");
    const auto after = editor.document()->revision();
    drag(true);
    check(editor.document()->revision() == after && editor.document()->source().document == painted,
          "Cancelled Scene stroke changed source/history");
    // Locked and unfocused Scene input cannot publish a held gesture.
    editable = false;
    io.AddMousePosEvent(origin.x + size.x * .45f, origin.y + size.y * .43f);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    editable = true;
    frame();
    check(!editor.stroke_active() && editor.document()->revision() == after,
          "Locked Scene input accepted a paint stroke");
    io.AddMouseButtonEvent(0, true);
    frame();
    check(editor.stroke_active(), "Focus-loss fixture failed to begin a stroke");
    focused = false;
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    focused = true;
    frame();
    check(!editor.stroke_active() && editor.document()->revision() == after,
          "Lost Scene focus retained or committed a paint stroke");
    io.AddMouseButtonEvent(0, true);
    frame();
    check(editor.stroke_active(), "Camera-change fixture failed to begin a stroke");
    camera.yaw = .1f;
    frame();
    check(!editor.stroke_active() && editor.document()->revision() == after,
          "Changed Scene camera retained or committed a paint stroke");
    io.AddMouseButtonEvent(0, false);
    camera.yaw = 0;
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    check(editor.stroke_active(), "Hidden Scene fixture failed to begin a stroke");
    show_scene = false;
    frame();
    frame();
    frame();
    check(!editor.stroke_active() && editor.document()->revision() == after,
          "Hidden Scene retained or committed a paint stroke");
    io.AddMouseButtonEvent(0, false);
    show_scene = true;
    frame();
    // Replacing the retained Mesh revision cancels rather than connecting to new data.
    io.AddMouseButtonEvent(0, true);
    frame();
    check(editor.stroke_active(), "Mesh replacement fixture failed to begin a stroke");
    auto replacement = mesh;
    replacement.morph_defaults = {.6f};
    resources->preview_mesh(mesh_id, replacement);
    for (unsigned attempt = 0; attempt < 100 && editor.stroke_active(); ++attempt) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!editor.stroke_active() && editor.document()->revision() == after &&
              editor.document()->source().document == painted,
          "Mesh revision replacement retained or committed stale texture coordinates");
    io.AddMouseButtonEvent(0, false);
    frame();
    resources->preview_mesh(mesh_id, mesh);
    settle();
    // Actual foreground geometry blocks painting the selected mesh underneath it.
    const auto blocker = authoring_command(scene, "entity.create",
                                           {{"recipe", "empty"}, {"name", "Opaque paint occluder"}})
                             .at("selected")
                             .get<EntityId>();
    MeshRenderer block;
    block.mesh = engine_primitive(0);
    scene.entity(blocker.str())
        .set<MeshRenderer>(block)
        .set<LocalTranslation>({0, 0, -1})
        .set<LocalScale>({3, 3, .2f});
    settle();
    const MeshTextureSurfaceTarget target_image{{texture_id}};
    check(!viewport.pick_texture_surface(scene.effective_document(), camera, 680, 540, entity,
                                         target_image, 374, 270),
          "Foreground geometry allowed painting through it");
    block.visible = false;
    scene.entity(blocker.str()).set<MeshRenderer>(block);
    settle();
    check(bool(viewport.pick_texture_surface(scene.effective_document(), camera, 680, 540, entity,
                                             target_image, 374, 270)),
          "Hidden geometry blocked the selected paint surface");
    input.click("paint-texture:save");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (editor.dirty()) {
        frame();
        check(std::chrono::steady_clock::now() < deadline, "Surface Save / Publish stalled");
    }
    settle();
    test::ViewImageReference after_display;
    after_display.queue(presentation.device(), graphics, scene_image);
    const auto after_pixels = after_display.read(graphics);
    save_view_png(output / "mesh-surface-before.png", 680, 540, before_pixels);
    save_view_png(output / "mesh-surface-after.png", 680, 540, after_pixels);
    const auto displayed_pixel = (270u * 680u + 374u) * 4;
    Json diagnostics = Json::array();
    for (const auto& diagnostic : viewport.diagnostics())
        diagnostics.push_back(diagnostic.text);
    if (!(after_pixels[displayed_pixel] > before_pixels[displayed_pixel] + 80))
        std::cerr << Json{{"before",
                           {before_pixels[displayed_pixel], before_pixels[displayed_pixel + 1],
                            before_pixels[displayed_pixel + 2]}},
                          {"after",
                           {after_pixels[displayed_pixel], after_pixels[displayed_pixel + 1],
                            after_pixels[displayed_pixel + 2]}},
                          {"diagnostics", diagnostics},
                          {"source", AssetDocumentAutomation::status(editor, project)}}
                         .dump()
                  << '\n';
    check(after_pixels[displayed_pixel] > before_pixels[displayed_pixel] + 80 &&
              after_pixels[displayed_pixel + 1] > before_pixels[displayed_pixel + 1] + 80 &&
              after_pixels[displayed_pixel + 2] > before_pixels[displayed_pixel + 2] + 80,
          "Save / Publish did not update actual displayed Mesh pixels");
    io.AddMousePosEvent(-100, -100);
    frame();
    test::ViewImageReference capture;
    capture.queue(presentation.device(), graphics, srv);
    save_view_png(output / "mesh-texture-painting.png", desc.Width, desc.Height,
                  capture.read(graphics));
    editor.request_close();
    frame();
    check(!editor.is_open(), "Published surface source failed to close");
    graphics->WaitForIdle();
    test::observe_ui = false;
    test::ui_targets.clear();
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool gallery = argc == 4 && std::string_view(argv[1]) == "--gallery";
        check(argc == 3 || gallery, "Need worker/output or --gallery Gallery/output");
        const auto output = std::filesystem::absolute(argv[gallery ? 3 : 2]);
        std::filesystem::create_directories(output);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        info.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Missing surface painting Vulkan device");
        DiligentPresentation presentation(device);
        if (gallery)
            exercise_gallery(presentation, graphics, std::filesystem::absolute(argv[2]), output);
        else
            exercise(presentation, graphics, argv[1], output);
        std::cout << (gallery ? "Animated painted Mesh source-hidden rendering PASS\n"
                              : "Native Scene mesh projection, source pixels, Undo/Redo, "
                                "cancellation and publication PASS\n");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
