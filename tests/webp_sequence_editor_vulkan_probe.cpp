#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "ImGuiImplDiligent.hpp"
#include "imgui_input_driver.hpp"
#include "sequence_frame_image_tests.hpp"
#include "view_image_readback_tests.hpp"
#include "webp_sequence_check.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
using test_view_image_ui::check;
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
              const std::filesystem::path& worker, const std::filesystem::path& output, float scale,
              bool narrow) {
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    ui::interface_scale = scale;
    ui::style(scale);
    ui::EditorUiContext editor;
    ui::ContextScope scope(editor);
    test::observe_ui = true;
    test_sequence_frame_image::Host host(presentation, graphics, output);
    ui::ViewImageTask task(worker);
    Diligent::TextureDesc desc;
    desc.Name = "FORGE sequence export options capture";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = unsigned((narrow ? 480 : 720) * scale);
    desc.Height = unsigned(780 * scale);
    ImGui::GetIO().DisplaySize = {float(desc.Width), float(desc.Height)};
    ImGui::GetIO().DeltaTime = 1.f / 60;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(ImGui::GetIO().Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16),
          "Missing sequence options font");
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    presentation.device()->CreateTexture(desc, nullptr, &texture);
    check(bool(texture), "Sequence options target unavailable");
    auto* rtv = texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    auto* srv = texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    const auto frame = [&] {
        presentation.begin_frame();
        task.poll(presentation, graphics, host.project, host.scene(), host.message);
        test::ui_targets.clear();
        gui.NewFrame(desc.Width, desc.Height, Diligent::SURFACE_TRANSFORM_IDENTITY);
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(desc.Width), float(desc.Height)}, ImGuiCond_Always);
        ImGui::Begin("Sequence export", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        task.draw(host.message);
        ImGui::End();
        task.capture(ui::ViewImageTask::View::Game, presentation, graphics, host.view(),
                     host.message);
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
        task.poll(presentation, graphics, host.project, host.scene(), host.message);
    };
    check(task.choose_sequence(host.source, nullptr, host.project, host.scene(), host.message),
          "Sequence options did not open");
    for (unsigned i = 0; i < 8; ++i)
        frame();
    test::ImGuiInputDriver input(frame);
    input.type("frame-export:count", "3");
    input.type("frame-export:fps", "1");
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SequenceFramesParent, path_utf8(host.project), {}, {}};
    input.click("frame-export:browse");
    input.click("frame-export:webp");
    for (unsigned i = 0; i < 4; ++i)
        frame();
    for (const char* id : {"frame-export:webp", "frame-export:start", "frame-export:cancel"}) {
        const auto& box = test::ui_targets.at(id);
        check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              "Sequence format/start/cancel control is disabled or clipped");
    }
    test::ViewImageReference capture;
    capture.queue(presentation.device(), graphics, srv);
    const auto rgba = capture.read(graphics);
    std::size_t varied = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        varied += std::memcmp(rgba.data(), rgba.data() + i, 4) != 0;
    check(varied > 100, "Sequence options capture has no visible UI pixels");
    const auto suffix =
        std::string(narrow ? "narrow-" : "wide-") + std::to_string(int(scale * 100)) + ".png";
    save_view_png(output / ("webp-sequence-options-" + suffix), desc.Width, desc.Height, rgba);
    input.click("frame-export:start");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (task.busy()) {
        check(std::chrono::steady_clock::now() < deadline, "Sequence animation export stalled");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto folder = host.project / "SequenceFrames";
    const auto receipt = host.receipt(folder, "complete", 3);
    check(receipt.at("output_format") == "webp" &&
              receipt.at("animation").at("duration_ms") == 3000 &&
              host.message.starts_with("WebP animation and PNG frames saved:"),
          "Native format choice did not produce the animation/receipt/status");
    std::array<Rgba8Pixels, 3> expected;
    for (unsigned i = 0; i < 3; ++i) {
        host.exact(folder / ui::SequenceFramePlan{0, 1, 3}.filename(i), i);
        expected[i] = {7, 3, host.pixels[i], true};
    }
    const auto encoded =
        asset_detail::read_bytes(folder / "animation.webp", FORGE_VIEW_IMAGE_ENCODED_LIMIT);
    const std::array<unsigned, 3> durations{1000, 1000, 1000};
    test_webp_sequence::inspect(encoded, expected, durations);
    check(host.source.cursor == 1.25 && host.source.playing && !host.source.held &&
              host.source.begins == host.source.ends,
          "WebP finalization did not restore its sequence source");
    test::observe_ui = false;
    test::ui_targets.clear();
    graphics->WaitForIdle();
}
void cancel_encoding(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                     const std::filesystem::path& worker, const std::filesystem::path& output) {
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = {720, 780};
    ImGui::GetIO().DeltaTime = 1.f / 60;
    ui::EditorUiContext editor;
    ui::ContextScope scope(editor);
    test::observe_ui = true;
    test_sequence_frame_image::Host host(presentation, graphics, output);
    ui::ViewImageTask task(worker);
    ui::SequenceFrameOutput format;
    format.webp = true;
    const auto folder = host.project / "CancelledAnimation";
    host.start(task, folder, {0, 1, 3}, format);
    host.until(task, [&] { return task.status() == "Encoding lossless WebP animation…"; });
    check(!host.source.held && host.source.cursor == 1.25 && host.source.playing,
          "Source was not restored before animation encoding");
    gui.NewFrame(720, 780, Diligent::SURFACE_TRANSFORM_IDENTITY);
    ImGui::OpenPopup("Export sequence frames");
    task.draw(host.message);
    check(test::ui_targets.at("frame-export:cancel").enabled,
          "Native Cancel export is disabled during animation encoding");
    ImGui::Render();
    task.cancel_sequence(host.message);
    host.finish(task);
    test::observe_ui = false;
    test::ui_targets.clear();
    const auto receipt = host.receipt(folder, "cancelled", 3);
    check(!receipt.contains("animation") && !std::filesystem::exists(folder / "animation.webp") &&
              host.message.starts_with("Frame export cancelled:"),
          "Cancelled encoding published an animation or lost cancellation status");
    for (unsigned i = 0; i < 3; ++i)
        host.exact(folder / ui::SequenceFramePlan{0, 1, 3}.filename(i), i);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need asset worker and output directory");
        const auto output = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Sequence options Vulkan device unavailable");
        DiligentPresentation presentation(device);
        test_sequence_frame_image::exercise(presentation, graphics, output / "existing-task");
        cancel_encoding(presentation, graphics, argv[1], output / "cancel-encoding");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                exercise(presentation, graphics, argv[1], output, scale, narrow);
        std::cout << "Native WebP sequence option, GPU frames, encoded pixels and restore passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
