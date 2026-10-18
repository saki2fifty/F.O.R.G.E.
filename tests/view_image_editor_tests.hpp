#pragma once
#include "editor/view_image.hpp"
#include "texture_decode.h"
#include "view_image_capture_reference.hpp"
#include <cstring>
#include <fstream>
#include <thread>
namespace forge::test_view_image_ui {
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
inline std::vector<unsigned char> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Image task output is missing");
    return {std::istreambuf_iterator<char>(file), {}};
}
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                     const std::filesystem::path& root, float scale, ImVec2 display) {
    using namespace Diligent;
    const auto project = root / AssetId::generate().str();
    std::filesystem::create_directories(project);
    const auto scene = AssetId::generate();
    const auto other_scene = AssetId::generate();
    using View = ui::ViewImageTask::View;
    auto* previous_context = ImGui::GetCurrentContext();
    const float previous_scale = ui::interface_scale;
    const bool previous_observe = test::observe_ui;
    ImGui::CreateContext();
    struct Restore {
        ImGuiContext* context;
        float scale;
        bool observe;
        ~Restore() {
            ImGui::DestroyContext();
            ImGui::SetCurrentContext(context);
            ui::interface_scale = scale;
            test::observe_ui = observe;
            test::ui_targets.clear();
        }
    } restore{previous_context, previous_scale, previous_observe};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* font_pixels;
    int font_width, font_height;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    ui::style(scale);
    ui::EditorUiContext editor;
    ui::ContextScope editor_scope(editor);
    ui::ViewImageTask task;
    std::string message;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);
        ImGui::Begin("Image export fixture");
        task.draw(message);
        ImGui::End();
        ImGui::Render();
    };
    auto click = [&](const std::string& name) {
        frame();
        frame();
        check(test::ui_targets.contains(name), "Image export button is missing");
        const auto box = test::ui_targets.at(name);
        check(box.enabled && box.minimum.x >= 0 && box.minimum.y >= 0 &&
                  box.maximum.x <= display.x && box.maximum.y <= display.y &&
                  box.minimum.x >= box.clip_minimum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.x <= box.clip_maximum.x && box.maximum.y <= box.clip_maximum.y,
              "Image export confirmation is clipped");
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                            (box.minimum.y + box.maximum.y) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    const std::vector<std::uint8_t> expected{255, 0,  31,  0,   0,   255, 71, 128,
                                             23,  42, 255, 255, 128, 64,  32, 255};
    TextureDesc desc;
    desc.Name = "FORGE image task known display pixels";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 2;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    TextureSubResData row{expected.data(), 8};
    Diligent::TextureData initial{&row, 1};
    RefCntAutoPtr<ITexture> texture;
    presentation.device()->CreateTexture(desc, &initial, &texture);
    check(bool(texture), "Image task GPU target unavailable");
    auto* view = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    auto finish = [&] {
        presentation.end_frame(graphics);
        graphics->Flush(); // Offscreen test host submits the existing frame marker.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (task.busy()) {
            task.poll(presentation, graphics, project / "after-capture", other_scene, message);
            check(std::chrono::steady_clock::now() < deadline, "Image task completion timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    auto exact_image = [&](const std::filesystem::path& path) {
        const auto bytes = read(path);
        ForgeImagePixels decoded{};
        const bool ok = forge_png_decode(bytes.data(), bytes.size(), 4096, 1024, false, &decoded);
        const bool equal = ok && decoded.width == 2 && decoded.height == 2 &&
                           decoded.size == expected.size() &&
                           std::memcmp(decoded.pixels, expected.data(), expected.size()) == 0;
        forge_image_pixels_free(&decoded);
        check(equal, "Image task saved different pixels or dimensions");
    };
    const auto output = project / "render.png";
    check(!task.selected_path(View::Scene, project / "bad.jpg", project, scene, message) &&
              !task.busy() && message.find("failed") != std::string::npos,
          "Image task accepted a misleading file extension");
    const auto deliveries = FileDialog::fixture_deliveries;
    FileDialog::fixture_selection = FileDialog::Result{FileDialog::Kind::SaveImage, {}, {}, {}};
    task.choose(View::Scene, nullptr, project, scene);
    task.poll(presentation, graphics, project, scene, message);
    check(!task.busy() && message == "Image save cancelled" &&
              FileDialog::fixture_deliveries == deliveries + 1,
          "Cancelled native selection left image task busy");
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SaveImage, path_utf8(output), {}, {}};
    task.choose(View::Scene, nullptr, project, scene);
    task.poll(presentation, graphics, project, other_scene, message);
    check(!task.busy() && message.find("Scene changed") != std::string::npos &&
              !std::filesystem::exists(output),
          "Native selection was accepted after the Scene changed");
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SaveImage, path_utf8(output), {}, {}};
    task.choose(View::Scene, nullptr, project, scene);
    task.poll(presentation, graphics, project, scene, message);
    check(task.busy() && FileDialog::fixture_deliveries == deliveries + 3,
          "Image task did not accept the native picker adapter's PNG destination");
    check(!task.selected_path(View::Game, project / "other.png", project, scene, message),
          "Busy image task replaced its destination");
    task.capture(View::Game, presentation, graphics, view, message);
    check(!presentation.readback_pending(), "Image task captured a different view");
    task.poll(presentation, graphics, project, other_scene, message);
    check(!task.busy() && !std::filesystem::exists(output),
          "Pending image task survived a Scene change");
    check(task.selected_path(View::Scene, output, project, scene, message),
          "Image task did not recover from stale selection");
    task.capture(View::Scene, presentation, graphics, view, message);
    check(task.busy() && presentation.readback_pending(), "Image task did not queue a GPU copy");
    finish(); // Already captured pixels finish independently of later Scene selection.
    exact_image(output);
    const auto reference = test::verify_saved_view(presentation.device(), graphics, view, output);
    check(reference.at("matches_view").get<bool>() && reference.at("width") == 2 &&
              reference.at("height") == 2,
          "Independent GPU reference differs from the saved image");
    check(message.starts_with("Image saved:") && !editor.log.empty() &&
              editor.log.back().find("  Image export: Image saved:") != std::string::npos,
          "Image completion did not use the timestamped editor log");
    const auto previous = read(output);
    check(task.selected_path(View::Scene, project / "render", project, scene, message),
          "Image task did not stage appended PNG overwrite");
    task.capture(View::Scene, presentation, graphics, view, message);
    check(!presentation.readback_pending(), "Appended PNG overwrite skipped confirmation");
    click("button:Cancel");
    check(!task.busy() && read(output) == previous, "Cancelled overwrite changed the PNG");
    check(task.selected_path(View::Scene, project / "render", project, scene, message),
          "Image task did not reopen overwrite confirmation");
    frame();
    task.poll(presentation, graphics, project, other_scene, message);
    frame();
    frame();
    check(!task.busy() && !test::ui_targets.contains("button:Replace") && read(output) == previous,
          "Stale overwrite popup remained active after Scene change");
    check(task.selected_path(View::Game, project / "render", project, scene, message),
          "Image task did not stage Game overwrite");
    click("button:Replace");
    task.capture(View::Game, presentation, graphics, view, message);
    finish();
    exact_image(output);
    check(
        task.selected_path(View::Scene, project / "absent" / "failed.png", project, scene, message),
        "Image task did not stage IO failure case");
    task.capture(View::Scene, presentation, graphics, view, message);
    finish();
    check(message.starts_with("Image save failed:") && read(output) == previous &&
              !presentation.readback_pending(),
          "Image file failure changed an existing output or left native ownership pending");
    check(task.selected_path(View::Game, project / "unavailable.png", project, scene, message),
          "Image task did not recover after file failure");
    task.unavailable(View::Game, message);
    check(!task.busy() && message.starts_with("Image save failed:"),
          "Missing view did not terminate its pending request");
}
} // namespace forge::test_view_image_ui
