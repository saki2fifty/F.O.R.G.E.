#pragma once
#include "ImGuiImplDiligent.hpp"
#include "editor/camera_controls.hpp"
#include "editor/foliage_placement.hpp"
#include "editor/viewport.hpp"
#include "gallery_game_capture.hpp"
#include "imgui_input_driver.hpp"
#include "view_image.hpp"
#include "view_image_capture_reference.hpp"
#include <chrono>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <thread>

namespace forge::test_foliage_brush {
inline constexpr auto scene_path = "Scenes/201-foliage-surface-brush.scene.json";
inline Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Foliage brush fixture is missing");
    return Json::parse(file);
}
inline void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
inline AssetId native(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                      const std::filesystem::path& root, const std::filesystem::path& output) {
    // Keep the shipped Gallery immutable on repeat runs. The first authoring
    // proof can supply the Gallery; later runs use a separate private Scene.
    const auto authored_path = std::filesystem::exists(root / scene_path)
                                   ? "Scenes/FoliageBrushTest.scene.json"
                                   : scene_path;
    check(!std::filesystem::exists(root / authored_path), "Private brush Scene already exists");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    auto document = read(root / "Scenes/200-mesh-surface-foliage.scene.json");
    std::erase_if(document["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        const auto& components = row.at("components");
        const auto name = row.at("name").get<std::string>();
        return !components.contains("forge.camera") && !components.contains("forge.light") &&
               !name.starts_with("Mesh surface -") && !name.starts_with("Blade template");
    });
    scene.reset(document);
    project.save_as(root / authored_path);
    std::string prototype, left, right;
    const auto saved_base = scene.document();
    for (const auto& row : saved_base.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Blade template"))
            prototype = row.at("id");
        if (name.starts_with("Mesh surface - tilted"))
            left = row.at("id");
        if (name.starts_with("Mesh surface - mirrored"))
            right = row.at("id");
    }
    check(!prototype.empty() && !left.empty() && !right.empty(), "Brush template/surfaces missing");
    auto resources = std::make_shared<MeshResourceHost>(presentation, graphics, root);
    resources->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    Viewport viewport(presentation, true);
    viewport.resources(resources);
    EditorCamera camera;
    camera.target = {-7, 0, 0};
    camera.distance = 30;
    camera.pitch = -.6435011f;
    camera.projection = CameraProjection::Orthographic;
    camera.orthographic_height = 16;
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTrickleEventQueue = false;
    io.DeltaTime = 1.f / 60;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(io.Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16), "Missing native brush font");
    ui::style(1);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    ui_context.selection.select_entity(prototype);
    ui::FoliagePlacement panel;
    panel.open = true;
    test::observe_ui = true;
    Diligent::TextureDesc desc;
    desc.Name = "FORGE native foliage surface brush";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = 1400;
    desc.Height = 1000;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> target;
    presentation.device()->CreateTexture(desc, nullptr, &target);
    check(bool(target), "Missing native foliage brush render target");
    auto* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    auto* srv = target->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    ImVec2 origin{}, size{780, 720};
    io.DisplaySize = {float(desc.Width), float(desc.Height)};
    auto frame = [&] {
        presentation.begin_frame();
        resources->pump();
        test::ui_targets.clear();
        gui.NewFrame(desc.Width, desc.Height, Diligent::SURFACE_TRANSFORM_IDENTITY);
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetWindowSize("Foliage placement", {600, 1000}, ImGuiCond_Always);
        panel.draw(scene, ui_context.selection, false, &project);
        ImGui::SetNextWindowPos({600, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({800, 1000}, ImGuiCond_Always);
        ImGui::Begin("Scene foliage painting", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ui_context.task.focus(ui::DocumentTask::Scene);
        origin = ImGui::GetCursorScreenPos();
        ui::ViewportInput input;
        (void)ui::camera_controls(camera, size, true, &input);
        const auto snapshot = scene.effective_document();
        panel.surface_input(scene, ui_context.selection, &project, camera, origin, size, true, true,
                            input.hovered, input.activated, input.active, [&](double x, double y) {
                                return viewport.pick_surface(
                                    snapshot, camera, unsigned(size.x), unsigned(size.y),
                                    EntityId::parse(ui_context.selection.entity()), x, y);
                            });
        auto* image = viewport.render(graphics, scene.effective_document(), unsigned(size.x),
                                      unsigned(size.y), camera, scene.revision(), false, {false});
        ImGui::GetWindowDrawList()->AddImage(ImTextureRef{reinterpret_cast<ImTextureID>(image)},
                                             origin, {origin.x + size.x, origin.y + size.y});
        panel.surface_overlay(origin, size, input.hovered);
        ImGui::TextWrapped("Green circles preview sampled stroke centers. Release places one "
                           "ordinary group; Escape cancels. Save the Scene before export.");
        ImGui::End();
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
    auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            frame();
            check(std::chrono::steady_clock::now() < deadline, "Native brush resources stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (viewport.pending());
        frame();
    };
    test::ImGuiInputDriver input(frame);
    settle();
    input.click("foliage:source");
    input.click("foliage:projected");
    input.click("foliage:surface_kind");
    io.AddKeyEvent(ImGuiKey_Home, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Home, false);
    frame();
    io.AddKeyEvent(ImGuiKey_DownArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_DownArrow, false);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    input.type("foliage:radius", ".65");
    input.type("foliage:spacing", ".15");
    input.type("foliage:count", "12");
    input.type("foliage:scale_min", ".65");
    input.type("foliage:scale_max", "1.15");
    input.click("foliage:align_slope");
    input.click("foliage:brush");
    input.type("foliage:dab_distance", "1.2");
    check(panel.brush_enabled(), "Actual brush control failed to arm");
    unsigned stroke_count = 0;
    for (const auto& surface : {left, right}) {
        ui_context.selection.select_entity(surface);
        camera.target[0] = surface == left ? -7 : 7;
        input.type("foliage:name", surface == left ? "Tilted surface brush stroke"
                                                   : "Mirrored surface brush stroke");
        input.click("foliage:terrain");
        input.type("foliage:dab_distance", "1.2");
        settle();
        const auto before = scene.document();
        auto move = [&](float x) {
            io.AddMousePosEvent(origin.x + x, origin.y + 360);
            frame();
        };
        move(270);
        io.AddMouseButtonEvent(0, true);
        frame();
        for (const float x : {330.f, 390.f, 450.f})
            move(x);
        check(scene.document() == before, "Native brush preview changed Scene intent");
        if (!stroke_count) {
            test::ViewImageReference reference;
            reference.queue(presentation.device(), graphics, srv);
            save_view_png(output / "foliage-brush-native.png", desc.Width, desc.Height,
                          reference.read(graphics));
            test::verify_saved_view(presentation.device(), graphics, srv,
                                    output / "foliage-brush-native.png");
        }
        io.AddMouseButtonEvent(0, false);
        frame();
        const auto after = scene.document();
        check(after.at("entities").size() == before.at("entities").size() + 53,
              "Native stroke did not create 48 objects/four dabs/root: " + panel.diagnostic());
        check(scene.undo() && scene.document() == before && scene.redo() &&
                  scene.document() == after,
              "Native projected stroke did not use one Undo/Redo");
        ++stroke_count;
    }
    project.save();
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(authored_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*project.writer_guard(), roots, {}, scene.schema());
    graphics->WaitForIdle();
    test::observe_ui = false;
    test::ui_targets.clear();
    return record.id;
}
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                     const std::filesystem::path& gallery, const std::filesystem::path& output) {
    const auto scratch = output / AssetId::generate().str();
    std::filesystem::create_directories(scratch);
    const auto source = scratch / "SourceProject";
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    const auto shipped = std::filesystem::exists(source / scene_path)
                             ? std::optional<Json>{read(source / scene_path)}
                             : std::nullopt;
    const auto id = native(presentation, graphics, source, scratch);
    if (shipped)
        check(read(source / scene_path) == *shipped,
              "Repeat native proof changed the shipped brush Gallery");
    const auto locator = AssetCatalog::open_project(source).records().at(id).source;
    const auto before = test_gallery_capture::render(presentation, graphics, source, id,
                                                     scratch / "authored", 21, false);
    const std::array roots{id};
    package_runtime_content(source, scratch / "Export", roots, {"windows", "d3d12"});
    std::filesystem::rename(scratch / "Export", scratch / "Relocated");
    std::filesystem::rename(source, scratch / "SourcesOffline");
    const auto after = test_gallery_capture::render(presentation, graphics, scratch / "Relocated",
                                                    id, scratch / "relocated", 21, false);
    check(before.size() == 4 && before == after, "Brush export pixels changed after source hiding");
    const Json receipt{{"scene", id},
                       {"scene_path", path_utf8(locator)},
                       {"native_strokes", 2},
                       {"saved_plants", 96},
                       {"exact_image_pairs", before.size()},
                       {"scope",
                        "Actual native controls/retained Vulkan picking, atomic Undo/Redo, "
                        "save and source-hidden relocated Windows-target rendering on "
                        "software Vulkan; no Windows or physical GPU acceptance"}};
    std::ofstream(scratch / "receipt.json") << receipt.dump(2) << '\n';
    std::cout << receipt.dump() << '\n';
}
} // namespace forge::test_foliage_brush
