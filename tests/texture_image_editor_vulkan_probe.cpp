#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "ImGuiImplDiligent.hpp"
#include "editor/document.hpp"
#include "editor/texture_image_input.hpp"
#include "editor/texture_viewer.hpp"
#include "imgui_input_driver.hpp"
#include "published_texture_fixture.hpp"
#include "view_image_editor_tests.hpp"
#include <iostream>

using namespace forge;
namespace {
using test_view_image_ui::check;
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
              const std::filesystem::path& gallery, const std::filesystem::path& output,
              float scale, bool narrow) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "Texture export");
    test::published_texture_fixture(gallery, root);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto found =
        std::find_if(catalog->records().begin(), catalog->records().end(), [](const auto& row) {
            return row.second.source == "Assets/Sprites/Badge.png";
        });
    check(found != catalog->records().end(), "Missing cooked Texture fixture");
    const AssetRef<TextureAsset> asset{found->first};
    bool foreign = false;
    try {
        (void)ui::texture_image_input(root / "other-project", catalog, asset);
    } catch (const std::exception&) {
        foreign = true;
    }
    check(foreign, "Texture pixel input admitted a catalog from another project");
    const auto original = ui::texture_image_input(root, catalog, asset).load();
    const auto scene = AssetId::generate();
    const float width = (narrow ? 480 : 720) * scale;
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    Diligent::TextureDesc desc;
    desc.Name = "FORGE actual Texture viewer capture";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = unsigned(width);
    desc.Height = unsigned(650 * scale);
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> image;
    presentation.device()->CreateTexture(desc, nullptr, &image);
    check(bool(image), "Texture viewer capture target unavailable");
    auto* rtv = image->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    auto* srv = image->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    io.DisplaySize = {float(desc.Width), float(desc.Height)};
    io.DeltaTime = 1.f / 60;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(io.Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16), "Missing capture font");
    ui::style(scale);
    ui::EditorUiContext editor;
    ui::ContextScope scope(editor);
    test::observe_ui = true;
    TextureViewer viewer(presentation, graphics, root);
    ui::ViewImageTask task;
    std::string message;
    bool can_export = true;
    const auto frame = [&] {
        presentation.begin_frame();
        test::ui_targets.clear();
        gui.NewFrame(desc.Width, desc.Height, Diligent::SURFACE_TRANSFORM_IDENTITY);
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, 650 * scale}, ImGuiCond_Always);
        ImGui::Begin("Texture", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        viewer.draw(catalog, asset, false, can_export);
        ImGui::End();
        task.draw(message);
        graphics->SetRenderTargets(1, &rtv, nullptr,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[] = {.035f, .06f, .07f, 1};
        graphics->ClearRenderTarget(rtv, clear,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        gui.Render(graphics);
        presentation.end_frame(graphics);
        graphics->Flush();
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        frame();
        check(std::chrono::steady_clock::now() < deadline, "Texture viewer did not become ready");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (!test::ui_targets.contains("button:Export stored pixels...") ||
             !test::ui_targets.at("button:Export stored pixels...").enabled);
    check(viewer.error().empty(), "Published Texture preview failed");
    can_export = false;
    frame();
    check(!test::ui_targets.at("button:Export stored pixels...").enabled,
          "Busy shared image task did not disable Texture export");
    can_export = true;
    test::ImGuiInputDriver input(frame);
    input.click("button:Export stored pixels...");
    auto request = viewer.take_export();
    check(request && request->project == root && request->texture == asset &&
              request->catalog == catalog && !request->semantic && request->slice.mip == 0 &&
              !viewer.take_export(),
          "Texture button did not emit one owned published selection");
    auto pixels = ui::texture_image_input(request->project, request->catalog, request->texture,
                                          request->semantic, request->slice);
    const auto target = root / "export.png";
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SaveImage, path_utf8(target), {}, {}};
    task.choose(ui::ViewImageTask::View::Texture, nullptr, root, {}, std::move(pixels));
    task.poll(presentation, graphics, root, scene, message);
    check(task.busy() && !presentation.readback_pending(),
          "Texture picker did not use a detached CPU job");
    while (task.busy()) {
        task.poll(presentation, graphics, root, AssetId::generate(), message);
        check(std::chrono::steady_clock::now() < deadline, "Texture export stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(message.starts_with("Image saved:"), "Native Texture image export did not complete");
    const auto saved = read_view_preview(target);
    ForgeImagePixels decoded{};
    const bool exact =
        forge_png_decode(saved.png.data(), saved.png.size(), 4096, 64 * 1024 * 1024, 0, &decoded) &&
        decoded.size == original.rgba.size() &&
        std::memcmp(decoded.pixels, original.rgba.data(), decoded.size) == 0;
    forge_image_pixels_free(&decoded);
    check(exact, "Texture image task changed the selected stored pixels");
    unsigned loads = 0;
    const auto counted = [&] {
        return ui::ViewImageTask::PixelInput{{{"fixture", true}}, [&] {
                                                 ++loads;
                                                 return original;
                                             }};
    };
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SaveImage, path_utf8(root / "stale.png"), {}, {}};
    task.choose(ui::ViewImageTask::View::Texture, nullptr, root, {}, counted());
    task.poll(presentation, graphics, root / "different-project", scene, message);
    check(!task.busy() && !loads && message.find("project changed") != std::string::npos,
          "A stale Texture picker started its producer");
    check(task.selected_path(ui::ViewImageTask::View::Texture, root / "cancelled.png", root, {},
                             message, {}, counted()) &&
              task.cancel(message),
          "Pending Texture export could not be cancelled");
    task.poll(presentation, graphics, root, scene, message);
    check(!loads && !std::filesystem::exists(root / "cancelled.png"),
          "Cancelled Texture export loaded or published pixels");
    check(task.selected_path(ui::ViewImageTask::View::Texture, root / "pending.png", root, {},
                             message, {}, counted()),
          "Pending Texture input was rejected");
    task.poll(presentation, graphics, root / "different-project", scene, message);
    check(!task.busy() && !loads && !std::filesystem::exists(root / "pending.png"),
          "Changed project queued pending Texture pixels before cancellation");
    frame();
    frame();
    const auto& box = test::ui_targets.at("button:Export stored pixels...");
    check(box.maximum.y <= box.clip_maximum.y && box.maximum.x <= box.clip_maximum.x,
          "Texture export control is clipped");
    test::ViewImageReference reference;
    reference.queue(presentation.device(), graphics, srv);
    const auto native_pixels = reference.read(graphics);
    save_view_png(output / (std::string("texture-export-") + (narrow ? "narrow-" : "wide-") +
                            std::to_string(int(scale * 100)) + ".png"),
                  desc.Width, desc.Height, native_pixels);
    test::observe_ui = false;
    test::ui_targets.clear();
    graphics->WaitForIdle();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and output paths");
        const auto output = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Texture export Vulkan device unavailable");
        DiligentPresentation presentation(device);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                exercise(presentation, graphics, argv[1], output, scale, narrow);
        std::cout << "Texture export controls, native chooser, stored pixels and project "
                     "cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
