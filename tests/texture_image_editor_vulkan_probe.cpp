#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "ImGuiImplDiligent.hpp"
#include "editor/document.hpp"
#include "editor/texture_image_input.hpp"
#include "editor/texture_viewer.hpp"
#include "imgui_input_driver.hpp"
#include "published_texture_fixture.hpp"
#include "view_image_editor_tests.hpp"
#include "view_image_hdr.hpp"
#include <iostream>

using namespace forge;
namespace {
using test_view_image_ui::check;
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
              const std::filesystem::path& gallery, const std::filesystem::path& output,
              float scale, bool narrow, ViewImageFormat preferred, bool precision = false,
              bool blocks = false, const std::filesystem::path& worker = {},
              unsigned signed_channels = 0) {
    const bool hdr = preferred == ViewImageFormat::Hdr;
    const bool jpeg = preferred == ViewImageFormat::Jpeg;
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "Texture export");
    test::published_texture_fixture(gallery, root);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto found =
        std::find_if(catalog->records().begin(), catalog->records().end(), [&](const auto& row) {
            return row.second.source ==
                   (signed_channels == 1   ? "Assets/TextureBlocks/SignedChannel.dds"
                    : signed_channels == 2 ? "Assets/TextureBlocks/SignedPair.dds"
                    : blocks               ? "Assets/TextureBlocks/BlockColors.dds"
                    : hdr                  ? "Assets/ReflectionCapture/Reflection.dds"
                    : precision            ? "Assets/TexturePrecision/Normalized16.png"
                    : jpeg                 ? "Assets/ReflectionProbes/WarmRoom.png"
                                           : "Assets/Sprites/Badge.png");
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
    const auto semantic = signed_channels ? std::optional{TextureSemantic::Data} : std::nullopt;
    const auto original =
        ui::texture_image_input(root, catalog, asset, semantic, {}, {}, worker).load(preferred);
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
        graphics->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    if (signed_channels) {
        frame();
        test::ImGuiInputDriver variant(frame);
        variant.click("texture:variant");
        variant.click("texture:variant:Data");
    }
    do {
        frame();
        check(std::chrono::steady_clock::now() < deadline, "Texture viewer did not become ready");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (!test::ui_targets.contains("button:Export image...") ||
             !test::ui_targets.at("button:Export image...").enabled);
    check(viewer.error().empty(), "Published Texture preview failed");
    can_export = false;
    frame();
    check(!test::ui_targets.at("button:Export image...").enabled,
          "Busy shared image task did not disable Texture export");
    can_export = true;
    test::ImGuiInputDriver input(frame);
    input.click("button:Export image...");
    auto request = viewer.take_export();
    check(request && request->project == root && request->texture == asset &&
              request->catalog == catalog && request->semantic == semantic &&
              request->slice.mip == 0 &&
              request->only_format == (hdr ? std::optional{ViewImageFormat::Hdr}
                                       : precision || signed_channels
                                           ? std::optional{ViewImageFormat::Png}
                                           : std::nullopt) &&
              !viewer.take_export(),
          "Texture button did not emit one owned published selection");
    auto pixels =
        ui::texture_image_input(request->project, request->catalog, request->texture,
                                request->semantic, request->slice, request->only_format, worker);
    const auto target = root / (std::string("export") + view_image_suffix(preferred));
    FileDialog::fixture_selection = FileDialog::Result{FileDialog::Kind::SaveImage,
                                                       path_utf8(target),
                                                       {},
                                                       {},
                                                       hdr || precision || signed_channels ? 0
                                                       : jpeg                              ? 2
                                                                                           : -1};
    task.choose(ui::ViewImageTask::View::Texture, nullptr, root, {}, std::move(pixels));
    task.poll(presentation, graphics, root, scene, message);
    check(task.busy() && !presentation.readback_pending(),
          "Texture picker did not use a detached CPU job");
    if (jpeg) {
        frame();
        frame();
        check(test::ui_targets.contains("jpeg-export:quality") && !std::filesystem::exists(target),
              "Native JPEG picker skipped its options");
        input.type("jpeg-export:quality", "72");
        // Settle layout and font updates, then require actual geometry and pixels.
        for (unsigned settle = 0; settle < 4; ++settle)
            frame();
        check(ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0,
              "JPEG modal capture has no rendered UI geometry");
        const auto& save = test::ui_targets.at("jpeg-export:save");
        check(save.enabled && save.maximum.x <= save.clip_maximum.x &&
                  save.maximum.y <= save.clip_maximum.y,
              "JPEG save control is clipped");
        test::ViewImageReference reference;
        reference.queue(presentation.device(), graphics, srv);
        const auto pixels = reference.read(graphics);
        std::size_t varied = 0;
        for (std::size_t at = 0; at < pixels.size(); at += 4)
            varied += std::memcmp(pixels.data(), pixels.data() + at, 4) != 0;
        if (varied <= 100) {
            const auto* draw = ImGui::GetDrawData();
            Json diagnostic{{"scale", scale},
                            {"width", desc.Width},
                            {"height", desc.Height},
                            {"vertices", draw->TotalVtxCount},
                            {"indices", draw->TotalIdxCount},
                            {"display", {draw->DisplaySize.x, draw->DisplaySize.y}},
                            {"origin", {draw->DisplayPos.x, draw->DisplayPos.y}},
                            {"framebuffer", {draw->FramebufferScale.x, draw->FramebufferScale.y}},
                            {"commands", Json::array()}};
            for (const auto* list : draw->CmdLists)
                for (const auto& command : list->CmdBuffer)
                    diagnostic["commands"].push_back({{"elements", command.ElemCount},
                                                      {"clip",
                                                       {command.ClipRect.x, command.ClipRect.y,
                                                        command.ClipRect.z, command.ClipRect.w}}});
            throw std::runtime_error("JPEG modal capture is clear: " + diagnostic.dump());
        }
        save_view_png(output / (std::string("jpeg-quality-") + (narrow ? "narrow-" : "wide-") +
                                std::to_string(int(scale * 100)) + ".png"),
                      desc.Width, desc.Height, pixels);
        input.click("jpeg-export:save");
    }
    while (task.busy()) {
        task.poll(presentation, graphics, root, AssetId::generate(), message);
        check(std::chrono::steady_clock::now() < deadline, "Texture export stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(message.starts_with("Image saved:"), "Native Texture image export did not complete");
    if (hdr) {
        check(asset_detail::read_bytes(target, FORGE_VIEW_IMAGE_ENCODED_LIMIT) ==
                  encode_hdr_image(std::get<Rgb32Pixels>(original)),
              "Native HDR chooser/task changed the selected radiance");
        check(!task.selected_path(ui::ViewImageTask::View::Scene, root / "display.hdr", root, scene,
                                  message),
              "Native display capture accepted an HDR filename");
    } else if (precision || signed_channels) {
        const auto file = asset_detail::read_bytes(target, FORGE_UINT16_IMAGE_ENCODED_LIMIT);
        ForgeImagePixels decoded{};
        const auto& exact = std::get<Rgba16Pixels>(original);
        const bool equal = forge_png_decode(file.data(), file.size(), 4096,
                                            FORGE_UINT16_IMAGE_PIXEL_LIMIT, 0, &decoded) &&
                           decoded.component_bytes == 2 && !decoded.srgb &&
                           decoded.size == exact.rgba.size() * sizeof(std::uint16_t) &&
                           std::memcmp(decoded.pixels, exact.rgba.data(), decoded.size) == 0;
        forge_image_pixels_free(&decoded);
        check(equal, "Native PNG16 output changed sample precision, order, label or alpha");
        check(message.find(signed_channels ? "signed block channels mapped" : "16-bit PNG") !=
                  std::string::npos,
              "Completed native precision export did not describe16-bit PNG");
    } else if (jpeg) {
        const auto saved = read_view_preview(target);
        check(saved.width == std::get<Rgba8Pixels>(original).width &&
                  saved.height == std::get<Rgba8Pixels>(original).height,
              "Native JPEG output changed the selected Texture dimensions");
        const auto file = asset_detail::read_bytes(target, FORGE_VIEW_IMAGE_ENCODED_LIMIT);
        ForgeImagePixels decoded{};
        check(forge_jpeg_decode(file.data(), file.size(), 4096, FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0,
                                &decoded),
              decoded.error);
        forge_image_pixels_free(&decoded);
    } else {
        const auto& rgba = std::get<Rgba8Pixels>(original);
        const auto saved = read_view_preview(target);
        ForgeImagePixels decoded{};
        const bool exact = forge_png_decode(saved.png.data(), saved.png.size(), 4096,
                                            64 * 1024 * 1024, 0, &decoded) &&
                           decoded.size == rgba.rgba.size() &&
                           std::memcmp(decoded.pixels, rgba.rgba.data(), decoded.size) == 0;
        forge_image_pixels_free(&decoded);
        check(exact, "Texture image task changed the selected stored pixels");
    }
    if (blocks) {
        check(std::get<Rgba8Pixels>(original).block_decoded &&
                  message.find("decoded block texels") != std::string::npos,
              "Native compressed export did not identify decoded texels");
    }
    unsigned loads = 0;
    const auto counted = [&] {
        return ui::ViewImageTask::PixelInput{{{"fixture", true}}, [&](ViewImageFormat) {
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
    const auto& box = test::ui_targets.at("button:Export image...");
    check(box.maximum.y <= box.clip_maximum.y && box.maximum.x <= box.clip_maximum.x,
          "Texture export control is clipped");
    test::ViewImageReference reference;
    reference.queue(presentation.device(), graphics, srv);
    const auto native_pixels = reference.read(graphics);
    save_view_png(output /
                      (std::string(signed_channels == 1   ? "signed-channel-export-"
                                   : signed_channels == 2 ? "signed-pair-export-"
                                   : blocks               ? "compressed-texture-export-"
                                   : hdr                  ? "hdr-texture-export-"
                                   : precision            ? "uint16-texture-export-"
                                   : jpeg                 ? "jpeg-texture-export-"
                                                          : "texture-export-") +
                       (narrow ? "narrow-" : "wide-") + std::to_string(int(scale * 100)) + ".png"),
                  desc.Width, desc.Height, native_pixels);
    test::observe_ui = false;
    test::ui_targets.clear();
    graphics->WaitForIdle();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--jpeg-200") || argc == 5,
              "Need Gallery/output, optional --jpeg-200 or --blocks worker");
        const auto output = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Texture export Vulkan device unavailable");
        DiligentPresentation presentation(device);
        if (argc == 5 && std::string_view(argv[3]) == "--signed-blocks") {
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool narrow : {false, true})
                    for (unsigned channels : {1u, 2u})
                        exercise(presentation, graphics, argv[1], output, scale, narrow,
                                 ViewImageFormat::Png, false, false, argv[4], channels);
            std::cout << "Signed block Texture native chooser, mapping, PNG16 and twelve layouts "
                         "passed\n";
            return 0;
        }
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                for (auto format :
                     {ViewImageFormat::Png, ViewImageFormat::Hdr, ViewImageFormat::Jpeg})
                    if (argc != 4 || (scale == 2.f && format == ViewImageFormat::Jpeg))
                        exercise(presentation, graphics, argv[1], output, scale, narrow, format);
        if (argc != 4)
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool narrow : {false, true})
                    exercise(presentation, graphics, argv[1], output, scale, narrow,
                             ViewImageFormat::Png, true);
        if (argc == 5) {
            check(std::string_view(argv[3]) == "--blocks", "Unknown Texture export profile");
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool narrow : {false, true})
                    exercise(presentation, graphics, argv[1], output, scale, narrow,
                             ViewImageFormat::Png, false, true, argv[4]);
        }
        std::cout << "Texture export controls, native chooser, stored pixels and project "
                     "cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
