#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "editor/automation.hpp"
#include "editor/capture_automation.hpp"
#include "editor/ui_source_editor.hpp"
#include "editor/ui_source_image_input.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "ui_render_readback.hpp"
#include "view_image_capture_reference.hpp"
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
             const std::vector<std::byte>& font, int width, std::string_view server_mode = {}) {
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
    std::vector<std::string> recent;
    EditorFiles files(scene, nullptr, recent);
    files.start(root);
    auto& project = files.document;
    const auto authored = scene.snapshot();
    test::ImGuiSoftwareCapture surface(width, 950);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    UiDiligentRenderer renderer(device);
    std::unique_ptr<UiPresenter> presenter;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(nullptr,
                                                                        SDL_DestroyTexture);
    std::uint64_t uploaded = 0;
    DiligentPresentation presentation(device);
    ui::ViewImageTask images;
    std::string message;
    std::shared_ptr<Diligent::ITextureView> native_image;
    ui::UiSourceEditor editor;
    editor.preview = [&](const auto& path, std::string_view text, int w, int h, float density,
                         bool refresh) {
        if (!presenter)
            presenter = std::make_unique<UiPresenter>(renderer.render_interface(), root, font);
        if (presenter->preview_document({asset.id}, path, text, w, h, density, refresh) &&
            uploaded != presenter->presentation_revision()) {
            native_image = renderer.draw_preview(*presenter, graphics, unsigned(w), unsigned(h));
            const auto pixels = test::readback(device, graphics, native_image.get());
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
    const auto image_input = [&](const Json& observed = Json()) {
        return ui::ui_source_image_input(
            editor, project, [&] { return native_image.get(); }, observed);
    };
    const auto saved = output / ("native-source-preview-" + std::to_string(width) + ".png");
    editor.save_preview = [&] {
        FileDialog::fixture_selection =
            FileDialog::Result{FileDialog::Kind::SaveImage, path_utf8(saved), {}, {}};
        images.choose(ui::ViewImageTask::View::UiSource, nullptr, root, {}, {}, image_input());
    };
    ui::style(1);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    ui::DocumentWorkspace workspace;
    workspace.add(editor.workspace_document(project, context, [] { return false; }));
    const auto frame = [&] {
        images.poll(presentation, graphics, root, scene.asset_id(), message);
        test::ui_targets.clear();
        surface.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), 950}, ImGuiCond_Always);
        workspace.draw();
        surface.render();
        presentation.end_frame(graphics);
        graphics->Flush();
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
    require(test::ui_targets.at("ui-source:preview-save-image").enabled,
            "Native preview image save is unavailable");
    if (!server_mode.empty()) {
        CaptureAutomationProviders providers;
        providers.source_image = image_input;
        ui::AutomationWorkspace automation;
        automation.workflow_methods = [&](const auto& permissions) {
            auto methods =
                UiSourceAutomation::methods(files, editor, permissions, [] { return ""; });
            auto captures =
                CaptureAutomation::methods(images, scene, files, providers, message, permissions);
            methods.insert(methods.end(), std::make_move_iterator(captures.begin()),
                           std::make_move_iterator(captures.end()));
            return methods;
        };
        const bool writable = server_mode == "--mcp";
        ui::AutomationPermissions permissions;
        permissions.assets = permissions.export_game = writable;
        const auto captured = output / "Output";
        std::filesystem::create_directories(captured);
        if (writable)
            permissions.export_root = captured;
        automation.start(scene, files, writable, false, permissions);
        asset_storage::replace(output / "mcp-connection.json",
                               Json{{"connection", automation.connection().connection()},
                                    {"project", path_utf8(root)},
                                    {"output", path_utf8(captured)}}
                                   .dump());
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (automation.active() && std::chrono::steady_clock::now() < end &&
               !std::filesystem::exists(output / "stop")) {
            files.changed = false;
            automation.pump(project, {});
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(!images.busy() && scene.snapshot() == authored,
                "MCP source capture left pending work or changed Scene state");
        editor.discard();
        return;
    }
    surface.save(output / ("ui-source-preview-" + std::to_string(width) + ".png"));
    const auto toggle = [&](float x, float y) {
        const auto current = test::ui_targets.at("ui-source:canvas");
        const float fitted = (current.maximum.x - current.minimum.x) / 1000;
        io.AddMousePosEvent(current.minimum.x + x * fitted, current.minimum.y + y * fitted);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
    };
    toggle(470, 180);
    toggle(760, 260);
    require(editor.state().at("selection").size() == 3 &&
                test::ui_targets.at("ui-source:group-apply").enabled && !editor.dirty(),
            "Native group capture did not select the three unchanged source panels");
    surface.save(output / ("ui-source-group-layout-" + std::to_string(width) + ".png"));
    input.click("ui-source:preview-save-image");
    const auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (images.busy()) {
            frame();
            require(std::chrono::steady_clock::now() < deadline,
                    "Native source image capture timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(images.error().empty(), images.error().c_str());
    };
    wait();
    const auto reference = test::verify_saved_view(device, graphics, native_image.get(), saved);
    require(reference.at("matches_view").get<bool>() && !editor.dirty() &&
                scene.snapshot() == authored,
            "Native source image differs from preview or changed authored state");

    CaptureAutomationProviders providers;
    providers.source_image = image_input;
    ui::AutomationPermissions permissions;
    const auto methods = [&] {
        return CaptureAutomation::methods(images, scene, files, providers, message, permissions);
    };
    const auto find = [&](const auto& entries, const char* name) {
        return std::find_if(entries.begin(), entries.end(),
                            [&](const auto& method) { return method.name == name; });
    };
    auto entries = methods();
    require(find(entries, "capture.ui_source") == entries.end(),
            "Read-only discovery exposed source-image writes");
    permissions.export_game = true;
    entries = methods();
    require(find(entries, "capture.ui_source") == entries.end(),
            "Missing output root exposed source-image writes");
    permissions.export_root = output;
    entries = methods();
    const auto capture_method = find(entries, "capture.ui_source");
    require(capture_method != entries.end(), "Granted capture tool missing");
    const auto observed = editor.image_source(project);
    auto request = Json{{"path", "agent-source-preview-" + std::to_string(width) + ".png"},
                        {"replace", true},
                        {"source", observed}};
    // Protocol validation accepts only the six guard fields, not the extra
    // descriptive viewport data returned by the native image observation.
    for (const auto* key : {"width", "height", "density"})
        request["source"].erase(key);
    const auto rejects = [&](Json value) {
        try {
            capture_method->invoke(value);
        } catch (const AuthoringError&) {
            require(!images.busy(), "Rejected source capture started a task");
            return;
        }
        throw std::runtime_error("Invalid source image guard accepted");
    };
    auto stale = request;
    stale["source"]["revision"] = 999999;
    rejects(stale);
    stale = request;
    stale["source"]["preview_revision"] = 999999;
    rejects(stale);
    stale = request;
    stale["path"] = "../outside.png";
    rejects(stale);
    auto accepted = capture_method->invoke(request);
    require(accepted.at("view") == "ui_source" && accepted.at("source") == observed,
            "Source-image task lost its native observation");
    wait();
    const auto agent_image = output / request.at("path").get<std::string>();
    require(test::verify_saved_view(device, graphics, native_image.get(), agent_image)
                .at("matches_view")
                .get<bool>(),
            "Automation captured different native source pixels");
    const auto image_method = find(entries, "capture.image");
    require(image_method != entries.end() && image_method->result_content == "image/png",
            "Native source capture lost its MCP image projection");
    const auto artifact = image_method->invoke({{"operation", accepted.at("operation")}});
    require(artifact.at("source") == observed && artifact.at("image").at("mimeType") == "image/png",
            "Owned image did not retain the observed native source");
    const auto borrowed = image_input();
    editor.replace_text(editor.state().at("text").get<std::string>() + "<!-- newer draft -->\n");
    try {
        borrowed.current();
        throw std::runtime_error("Changed source retained an old native image guard");
    } catch (const AuthoringError&) {
    }
    editor.undo(false);
    require(!editor.dirty() && scene.snapshot() == authored,
            "Image capture changed source or Scene state");
    editor.discard();
    require(!presenter && !editor.is_open(), "Capture retained its source presentation owner");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4 || (argc == 5 && (std::string_view(argv[4]) == "--mcp" ||
                                            std::string_view(argv[4]) == "--mcp-read")),
                "Need output directory, font, Gallery and optional MCP mode");
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
        if (argc == 5)
            capture(device, graphics, output, argv[3], font, 700, argv[4]);
        else
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
