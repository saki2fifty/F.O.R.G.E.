#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "editor/ui_source_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "ui_render_readback.hpp"
#include <RmlUi/Core.h>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <iostream>

namespace {
using namespace forge;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void capture(Diligent::IRenderDevice* device, Diligent::IDeviceContext* graphics,
             const std::filesystem::path& output, const std::filesystem::path& gallery,
             const std::vector<std::byte>& font, int width) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "UI layout preview");
    const auto relative = std::filesystem::path("Assets/UI/Layout/layout.rml");
    std::filesystem::create_directories((root / relative).parent_path());
    for (const auto* name : {"layout.rml", "layout.rcss"})
        std::filesystem::copy_file(gallery / "Assets/UI/Layout" / name,
                                   root / "Assets/UI/Layout" / name);
    const auto asset = register_ui_document(root, relative);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    test::ImGuiSoftwareCapture surface(width, 950);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    UiDiligentRenderer renderer(device);
    std::unique_ptr<UiPresenter> presenter;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(nullptr,
                                                                        SDL_DestroyTexture);
    std::uint64_t uploaded = 0;
    ui::UiSourceEditor editor;
    editor.preview = [&](const auto& path, std::string_view text, int w, int h, float density,
                         bool refresh) {
        if (!presenter)
            presenter = std::make_unique<UiPresenter>(renderer.render_interface(), root, font);
        if (presenter->preview_document({asset.id}, path, text, w, h, density, refresh) &&
            uploaded != presenter->presentation_revision()) {
            auto image = renderer.draw_preview(*presenter, graphics, unsigned(w), unsigned(h));
            const auto pixels = test::readback(device, graphics, image.get());
            texture.reset(SDL_CreateTexture(surface.native_renderer(), SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STATIC, w, h));
            require(bool(texture), SDL_GetError());
            if (!SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), w * 4))
                throw std::runtime_error(SDL_GetError());
            uploaded = presenter->presentation_revision();
        }
        return ui::UiSourcePreviewFrame{presenter->preview_layout(),
                                        reinterpret_cast<ImTextureID>(texture.get())};
    };
    editor.preview_hit = [&](float x, float y) {
        return presenter ? presenter->preview_hit(x, y) : std::string{};
    };
    editor.close_preview = [&] { presenter.reset(); };
    ui::style(1);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    ui::DocumentWorkspace workspace;
    workspace.add(editor.workspace_document(project, context, [] { return false; }));
    const auto frame = [&] {
        test::ui_targets.clear();
        surface.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), 950}, ImGuiCond_Always);
        workspace.draw();
        surface.render();
    };
    editor.open(project, path_utf8(relative));
    frame();
    frame();
    test::ImGuiInputDriver input(frame);
    input.click("ui-source:preview-tab");
    frame();
    const auto canvas = test::ui_targets.at("ui-source:canvas");
    const float scale = (canvas.maximum.x - canvas.minimum.x) / 1000;
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(canvas.minimum.x + 100 * scale, canvas.minimum.y + 130 * scale);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    require(test::ui_targets.at("ui-source:layout-apply").enabled && !editor.dirty() && texture,
            "Capture did not select an editable native panel without changing source");
    surface.save(output / ("ui-source-preview-" + std::to_string(width) + ".png"));
    editor.discard();
    require(!presenter && !editor.is_open(), "Capture retained its source presentation owner");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4, "Need output directory, font and Gallery");
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        auto* factory = Diligent::GetEngineFactoryVk();
        require(factory != nullptr, "Vulkan factory unavailable");
        Diligent::EngineVkCreateInfo info;
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        factory->CreateDeviceAndContextsVk(info, &device, &graphics);
        require(device && graphics, "Offscreen Vulkan device unavailable");
        const auto font = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (const int width : {1180, 700})
            capture(device, graphics, output, argv[3], font, width);
        graphics->WaitForIdle();
        std::cout << "Native-rendered source canvas component captures at1180/700 passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
