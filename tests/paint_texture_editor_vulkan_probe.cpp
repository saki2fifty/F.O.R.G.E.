#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "ImGuiImplDiligent.hpp"
#include "editor/paint_texture_editor.hpp"
#include "imgui_input_driver.hpp"
#include "paint_texture_preview.hpp"
#include "view_image.hpp"
#include "view_image_capture_reference.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
              const std::filesystem::path& worker, const std::filesystem::path& output, float scale,
              bool narrow) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "Paint canvas");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto scene_before = scene.document();
    auto document = PaintTextureDocument::create(
        project.writer_guard(), "Assets/Canvas.paint-texture.json",
        [](AssetId id) { return PaintTextureSource{PaintTextureValue::create(id, 67, 35)}; });
    const auto asset = document->source().asset();
    document.reset();
    PaintTextureEditor editor(worker);
    editor.open(project, "Assets/Canvas.paint-texture.json");
    check(editor.document(), "Paint document did not open");
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    io.DeltaTime = 1.f / 60;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(io.Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16), "Missing paint capture font");
    ui::style(scale);
    ui::EditorUiContext ui_context;
    ui::ContextScope scope(ui_context);
    test::observe_ui = true;
    Diligent::TextureDesc desc;
    desc.Name = "FORGE actual paint texture canvas controls";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = unsigned((narrow ? 520 : 900) * scale);
    desc.Height = unsigned(800 * scale);
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    io.DisplaySize = {float(desc.Width), float(desc.Height)};
    Diligent::RefCntAutoPtr<Diligent::ITexture> target;
    presentation.device()->CreateTexture(desc, nullptr, &target);
    check(bool(target), "Paint capture target unavailable");
    auto* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    auto* srv = target->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    PaintTexturePreview preview(presentation, graphics, root);
    if (scale == 1 && !narrow) {
        TextureAssetPreview rejected(presentation, graphics, root);
        const std::string key = "rejected-source-request";
        rejected.select_draft({asset}, asset_detail::content_digest(std::as_bytes(std::span(key))),
                              1, {});
        check(!rejected.error().empty() && !rejected.current() && !rejected.pending(),
              "Invalid draft preview request escaped its diagnostic boundary");
    }
    editor.draw_preview = [&](const auto& value, auto session, auto revision, unsigned width,
                              unsigned height) {
        auto* image = preview.render(value, session, revision, width, height);
        return PaintCanvasImage{reinterpret_cast<ImTextureID>(image), preview.current(),
                                preview.error()};
    };
    bool locked = false;
    std::string message;
    const auto frame = [&] {
        presentation.begin_frame();
        editor.poll(project, message);
        test::ui_targets.clear();
        gui.NewFrame(desc.Width, desc.Height, Diligent::SURFACE_TRANSFORM_IDENTITY);
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(desc.Width), float(desc.Height)}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        graphics->SetRenderTargets(1, &rtv, nullptr,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{.035f, .06f, .07f, 1};
        graphics->ClearRenderTarget(rtv, clear,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        gui.Render(graphics);
        presentation.end_frame(graphics);
        graphics->Flush();
        graphics->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            frame();
            check(std::chrono::steady_clock::now() < deadline, "Paint canvas preparation stalled");
            check(preview.error().empty(), "Paint preview failed: " + preview.error());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (editor.stroke_active() || !preview.current());
    };
    settle();
    const auto box = test::ui_targets.at("paint-texture:canvas");
    check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
              box.minimum.y >= box.clip_minimum.y && box.maximum.x <= box.clip_maximum.x + 1 &&
              box.maximum.y <= box.clip_maximum.y + 1,
          "Paint canvas is clipped at the tested layout");
    const auto point = [&](float x, float y) {
        return ImVec2{box.minimum.x + (box.maximum.x - box.minimum.x) * x,
                      box.minimum.y + (box.maximum.y - box.minimum.y) * y};
    };
    const auto start = point(.3f, .5f), finish = point(.7f, .5f);
    io.AddMousePosEvent(start.x, start.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    for (unsigned i = 1; i <= 6; ++i) {
        io.AddMousePosEvent(start.x + (finish.x - start.x) * i / 6, start.y);
        frame();
    }
    check(editor.stroke_active() && editor.document()->revision() == 1,
          "A held stroke committed before release or lost capture");
    io.AddMouseButtonEvent(0, false);
    frame();
    settle();
    const auto painted = editor.document()->source().document;
    check(editor.document()->revision() == 2 &&
              painted.pixel(33, 17) == PaintPixel{255, 255, 255, 255} &&
              painted.pixel(0, 0) == PaintPixel{},
          "Native canvas stroke coordinates or one-gesture revision disagree");
    test::ImGuiInputDriver input(frame);
    input.click("paint-texture:undo");
    settle();
    check(editor.document()->source().document.pixel(33, 17) == PaintPixel{},
          "Native stroke Undo lost pixels");
    input.click("paint-texture:redo");
    settle();
    check(editor.document()->source().document == painted,
          "Native stroke Redo changed its exact snapshot");
    input.click("paint-texture:fill");
    settle();
    check(editor.document()->source().document.pixel(0, 0) == PaintPixel{255, 255, 255, 255},
          "Native Fill did not cover the canvas");
    input.click("paint-texture:undo");
    settle();
    check(editor.document()->source().document == painted, "Native Fill Undo lost the stroke");
    // Escape cancels a held gesture, and locked input cannot start a gesture.
    const auto revision = editor.document()->revision();
    io.AddMousePosEvent(start.x, start.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    io.AddMouseButtonEvent(0, false);
    frame();
    check(!editor.stroke_active() && editor.document()->revision() == revision,
          "Escape committed or retained a held stroke");
    locked = true;
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    locked = false;
    check(editor.document()->revision() == revision, "Locked canvas accepted input");
    io.AddMousePosEvent(-100, -100);
    settle();
    test::ViewImageReference capture;
    capture.queue(presentation.device(), graphics, srv);
    const auto rgba = capture.read(graphics);
    const auto suffix =
        std::string(narrow ? "narrow-" : "wide-") + std::to_string(int(scale * 100)) + ".png";
    save_view_png(output / ("paint-texture-canvas-" + suffix), desc.Width, desc.Height, rgba);
    input.click("paint-texture:save");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (editor.dirty()) {
        frame();
        check(std::chrono::steady_clock::now() < deadline, "Native paint Save / Publish stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(AssetCatalog::open_project(root).records().at(asset).type == "texture",
          "Native paint did not publish an ordinary Texture");
    input.click("paint-texture:fill");
    settle();
    input.click("paint-texture:copy-panel");
    input.click("asset-picker:texture:Published image");
    input.select(("picker-option:" + asset.str()).c_str());
    frame();
    {
        test::ViewImageReference reference;
        reference.queue(presentation.device(), graphics, srv);
        save_view_png(output / ("paint-texture-copy-ready-" + suffix), desc.Width, desc.Height,
                      reference.read(graphics));
    }
    input.click("paint-texture:copy");
    settle();
    const auto copied = editor.document()->source().document;
    check(copied.texture().subresources == painted.texture().subresources &&
              copied.asset() == painted.asset(),
          "Native copy did not restore the immutable published pixels");
    input.click("paint-texture:undo");
    settle();
    check(editor.document()->source().document.pixel(0, 0) == PaintPixel{255, 255, 255, 255},
          "Native copy Undo lost the replaced canvas");
    input.click("paint-texture:redo");
    settle();
    check(editor.document()->source().document == copied,
          "Native copy Redo lost exact stored pixels");
    input.click("paint-texture:save");
    const auto copy_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (editor.dirty()) {
        frame();
        check(std::chrono::steady_clock::now() < copy_deadline, "Native copy publication stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(scene.document() == scene_before, "Paint canvas changed the authored Scene");
    editor.request_close();
    frame();
    check(!editor.is_open(), "Published paint source failed to close cleanly");
    graphics->WaitForIdle();
    test::observe_ui = false;
    test::ui_targets.clear();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need worker and output directory");
        const auto output = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Paint Vulkan device unavailable");
        DiligentPresentation presentation(device);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                exercise(presentation, graphics, argv[1], output, scale, narrow);
        std::cout << "Native paint input, detached stroke, Undo/Redo, Fill, cancellation, lock, "
                     "publication and six layouts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
