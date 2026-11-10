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
    bool source_locked = false, other_panel = false;
    char other_text[64] = "Other panel text";
    int other_cursor = -1;
    ui::DocumentWorkspace workspace;
    workspace.add(editor.workspace_document(project, context, [&] { return source_locked; }));
    const auto frame = [&] {
        images.poll(presentation, graphics, root, scene.asset_id(), message);
        test::ui_targets.clear();
        surface.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), 950}, ImGuiCond_Always);
        workspace.draw();
        if (other_panel) {
            ImGui::SetNextWindowPos({40, 40}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({300, 160}, ImGuiCond_Always);
            ImGui::Begin("Other focused panel");
            ImGui::InputText(
                "Text", other_text, sizeof(other_text), ImGuiInputTextFlags_CallbackAlways,
                [](ImGuiInputTextCallbackData* data) {
                    *static_cast<int*>(data->UserData) = data->CursorPos;
                    return 0;
                },
                &other_cursor);
            FORGE_UI_PROBE("fixture:other-text");
            ImGui::End();
        }
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
    const auto sizing_start = editor.state();
    input.click("ui-source:group-action");
    input.click("ui-source:group-choice:match_size");
    input.click("ui-source:group-apply");
    const auto sized = editor.state();
    const auto sized_preview = editor.preview_state(project, 1000, 700, 1, false);
    const std::array ids{"panel", "layout-b", "layout-c"};
    const std::array x{60.f, 430.f, 720.f}, y{90.f, 140.f, 220.f};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& rows = sized_preview.at("elements");
        const auto found = std::find_if(rows.begin(), rows.end(),
                                        [&](const auto& row) { return row.at("id") == ids[i]; });
        require(found != rows.end() && found->at("editable").get<bool>() &&
                    found->at("bounds") == Json::array({x[i], y[i], 220, 140}),
                "Native group sizing moved a member or ignored the active reference size");
    }
    require(sized.at("text") != sizing_start.at("text") &&
                sized.at("selection") == sizing_start.at("selection") &&
                scene.snapshot() == authored,
            "Group sizing lost selection or changed Scene state");
    frame();
    surface.save(output / ("ui-source-group-sizing-" + std::to_string(width) + ".png"));
    input.click("button:Undo");
    require(editor.state().at("text") == sizing_start.at("text"),
            "Group sizing needed multiple Undo steps");
    input.click("button:Redo");
    require(editor.state().at("text") == sized.at("text"), "Group sizing Redo differs");
    input.click("button:Undo");
    toggle(100, 130);
    toggle(470, 180);
    toggle(760, 260);
    frame();
    require(!editor.dirty(), "Group sizing fixture did not restore original source");
    const auto positioning_start = editor.state();
    input.click("ui-source:group-action");
    input.click("ui-source:group-choice:snap_grid");
    input.type("ui-source:grid-spacing", "8");
    input.click("ui-source:group-apply");
    const auto snapped = editor.state();
    const auto snapped_preview = editor.preview_state(project, 1000, 700, 1, false);
    const std::array grid_x{64.f, 432.f, 720.f}, grid_y{88.f, 144.f, 224.f};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& rows = snapped_preview.at("elements");
        const auto found = std::find_if(rows.begin(), rows.end(),
                                        [&](const auto& row) { return row.at("id") == ids[i]; });
        require(found != rows.end() &&
                    found->at("bounds") ==
                        Json::array({grid_x[i], grid_y[i], i == 0 ? 300 : 220, i == 0 ? 220 : 140}),
                "Native grid alignment ignored viewport coordinates or changed sizes");
    }
    frame();
    surface.save(output / ("ui-source-grid-" + std::to_string(width) + ".png"));
    input.click("button:Undo");
    require(editor.state().at("text") == positioning_start.at("text"),
            "Grid alignment needed multiple Undo steps");
    input.click("button:Redo");
    require(editor.state().at("text") == snapped.at("text"), "Grid alignment Redo differs");
    input.click("button:Undo");
    toggle(100, 130);
    frame(); // Register canvas keyboard routing before a key press.
    const auto before_nudge = editor.state();
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    frame();
    require(editor.state().at("text") == before_nudge.at("text"),
            "Keyboard positioning changed source after image submission");
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
    frame();
    frame();
    const auto nudged = editor.state();
    const auto border = [&] {
        const auto shown = editor.preview_state(project, 1000, 700, 1, false);
        for (const auto& row : shown.at("elements"))
            if (row.at("id") == "panel")
                return row.at("bounds");
        throw std::runtime_error("Keyboard fixture panel has no native bounds");
    };
    require(nudged.at("text") != before_nudge.at("text") &&
                border() == Json::array({61, 90, 300, 220}),
            "Focused canvas arrow did not move one physical pixel");
    surface.save(output / ("ui-source-keyboard-" + std::to_string(width) + ".png"));
    input.click("button:Undo");
    require(editor.state().at("text") == before_nudge.at("text"),
            "Keyboard positioning needed multiple Undo steps");
    toggle(100, 130);
    frame();
    io.AddKeyEvent(ImGuiMod_Shift, true);
    io.AddKeyEvent(ImGuiKey_DownArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_DownArrow, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    frame();
    frame();
    require(border() == Json::array({60, 100, 300, 220}),
            "Shift canvas arrow did not move ten physical pixels");
    input.click("button:Undo");
    toggle(100, 130);
    frame();
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    require(editor.state().at("text") == before_nudge.at("text"),
            "Escape did not cancel a queued keyboard edit");
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    frame();
    source_locked = true;
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
    frame();
    require(editor.state().at("text") == before_nudge.at("text"),
            "Task lock did not cancel a queued keyboard edit");
    source_locked = false;
    frame();
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    frame();
    const auto keyboard_parent = editor.state().at("selection").at(0).get<std::size_t>();
    editor.style(keyboard_parent, "background-color", "#285d88");
    const auto keyboard_intervening = editor.state();
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
    frame();
    require(editor.state().at("text") == keyboard_intervening.at("text"),
            "A source edit did not cancel queued keyboard positioning");
    input.click("button:Undo");
    toggle(100, 130);
    input.click("ui-source:grid-spacing");
    io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    frame();
    require(editor.state().at("text") == before_nudge.at("text"),
            "Canvas shortcut stole an arrow from an active numeric field");
    input.click("ui-source:source-tab");
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
    frame();
    require(editor.state().at("text") == before_nudge.at("text"),
            "Canvas shortcut changed source from the Source text view");
    input.click("ui-source:preview-tab");
    other_panel = true;
    frame();
    input.click("fixture:other-text");
    io.AddKeyEvent(ImGuiKey_End, true);
    frame();
    io.AddKeyEvent(ImGuiKey_End, false);
    frame();
    const auto cursor_before = other_cursor;
    io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    frame();
    require(cursor_before > 0 && other_cursor == cursor_before - 1 &&
                editor.state().at("text") == before_nudge.at("text"),
            "Another focused panel did not receive its native text-caret arrow");
    other_panel = false;
    frame();
    frame();
    toggle(470, 180);
    toggle(760, 260);
    require(editor.state().at("selection").size() == 3 && !editor.dirty(),
            "Positioning fixture did not restore original source/group");
    frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    frame();
    frame();
    require(border() == Json::array({59, 90, 300, 220}) &&
                editor.state().at("selection").size() == 3,
            "Canvas keyboard positioning did not preserve its group selection");
    input.click("button:Undo");
    require(editor.state().at("text") == positioning_start.at("text"),
            "Group keyboard positioning needed multiple Undo steps");
    toggle(100, 130);
    toggle(470, 180);
    toggle(760, 260);
    const auto drag_layout = [&](float start_x, float start_y, float dx, float dy, bool shift,
                                 const char* capture_name, bool cancel = false) {
        const auto before = editor.state();
        const auto canvas = test::ui_targets.at("ui-source:canvas");
        const float fitted = (canvas.maximum.x - canvas.minimum.x) / 1000;
        io.AddMousePosEvent(canvas.minimum.x + start_x * fitted,
                            canvas.minimum.y + start_y * fitted);
        frame();
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddMouseButtonEvent(0, true);
        frame();
        const auto pressed = ImGui::GetMousePos();
        io.AddMousePosEvent(pressed.x + dx * fitted, pressed.y + dy * fitted);
        frame();
        const auto moved = ImGui::GetMousePos();
        require(editor.gesture_active() && editor.state().at("text") == before.at("text"),
                "Mouse transform published source before release");
        const std::array delta{(moved.x - pressed.x) / fitted, (moved.y - pressed.y) / fitted};
        if (cancel) {
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
        }
        io.AddMouseButtonEvent(0, false);
        frame(); // Keep Shift through the release frame's final projection.
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
        frame();
        require(!editor.gesture_active() && scene.snapshot() == authored,
                "Mouse transform retained a gesture or changed Scene state");
        if (capture_name && *capture_name)
            surface.save(output / (std::string("ui-source-") + capture_name + "-" +
                                   std::to_string(width) + ".png"));
        return delta;
    };
    const auto plain_delta = drag_layout(100, 130, 12, 9, false, "");
    const auto plain_bounds = border();
    require(std::abs(plain_bounds.at(0).get<float>() - (60 + plain_delta[0])) < .02f &&
                std::abs(plain_bounds.at(1).get<float>() - (90 + plain_delta[1])) < .02f &&
                plain_bounds.at(2) == 300 && plain_bounds.at(3) == 220,
            "Default unsnapped movement changed its source/native behavior");
    input.click("button:Undo");
    toggle(100, 130);
    toggle(470, 180);
    toggle(760, 260);
    input.click("ui-source:snap-dragging");
    drag_layout(100, 130, 17, 5, false, "drag-grid");
    require(border() == Json::array({76, 94, 300, 220}) &&
                editor.state().at("selection").size() == 3,
            "Drag grid did not snap the active group anchor with spacing preserved");
    input.click("button:Undo");
    require(editor.state().at("text") == positioning_start.at("text"),
            "Snapped group drag needed multiple Undo steps");
    input.click("ui-source:snap-dragging");
    toggle(100, 130);
    toggle(470, 180);
    toggle(760, 260);
    const auto axis_delta = drag_layout(100, 130, 20, 50, true, "");
    const auto axis_bounds = border();
    require(axis_bounds.at(0) == 60 &&
                std::abs(axis_bounds.at(1).get<float>() - (90 + axis_delta[1])) < .02f,
            "Shift movement did not preserve its inactive axis");
    input.click("button:Undo");
    toggle(100, 130);
    const auto plain_resize = drag_layout(360, 310, 30, 15, false, "");
    const auto resized_bounds = border();
    require(resized_bounds.at(0) == 60 && resized_bounds.at(1) == 90 &&
                std::abs(resized_bounds.at(2).get<float>() - (300 + plain_resize[0])) < .02f &&
                std::abs(resized_bounds.at(3).get<float>() - (220 + plain_resize[1])) < .02f,
            "Default unsnapped resizing changed its source/native behavior");
    input.click("button:Undo");
    toggle(100, 130);
    const auto aspect_delta = drag_layout(360, 310, 60, 5, true, "drag-aspect");
    const auto aspect_bounds = border();
    require(aspect_bounds.at(0) == 60 && aspect_bounds.at(1) == 90 &&
                std::abs(aspect_bounds.at(2).get<float>() - (300 + aspect_delta[0])) < .02f &&
                std::abs(aspect_bounds.at(2).get<float>() / aspect_bounds.at(3).get<float>() -
                         300.f / 220.f) < .0001f,
            "Shift resize did not retain native visible aspect and position");
    input.click("button:Undo");
    toggle(100, 130);
    input.click("ui-source:snap-dragging");
    drag_layout(360, 310, 54, 5, true, "");
    const auto aspect_grid_bounds = border();
    require(aspect_grid_bounds.at(2) == 352 &&
                std::abs(aspect_grid_bounds.at(3).get<float>() - 352.f * 220 / 300) < .02f,
            "Aspect/grid priority ignored the dominant snapped native size");
    input.click("button:Undo");
    toggle(100, 130);
    drag_layout(100, 130, 24, 16, true, "", true);
    require(editor.state().at("text") == positioning_start.at("text") && !editor.dirty(),
            "Escape did not cancel a constrained snapped drag");
    input.click("ui-source:snap-dragging");
    drag_layout(360, 310, -1000, -1000, false, "");
    require(editor.state().at("text") == positioning_start.at("text") && !editor.dirty(),
            "Native-clamped resize published a source candidate");
    toggle(470, 180);
    toggle(760, 260);
    require(editor.state().at("selection").size() == 3,
            "Precision drag fixture did not restore the original group");
    const auto selection_start = editor.state();
    const auto members = selection_start.at("selection").get<std::vector<std::size_t>>();
    input.click("ui-source:box-select");
    enum class BoxCancel { None, Escape, Focus, Lock };
    const auto box_select = [&](float x1, float y1, float x2, float y2, bool add,
                                BoxCancel cancel = BoxCancel::None, const char* image = "") {
        const auto canvas = test::ui_targets.at("ui-source:canvas");
        const float fitted = (canvas.maximum.x - canvas.minimum.x) / 1000;
        io.AddMousePosEvent(canvas.minimum.x + x1 * fitted, canvas.minimum.y + y1 * fitted);
        io.AddKeyEvent(ImGuiMod_Ctrl, add);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(canvas.minimum.x + x2 * fitted, canvas.minimum.y + y2 * fitted);
        frame();
        require(editor.gesture_active() && editor.state().at("text") == selection_start.at("text"),
                "Rectangle selection changed source before release");
        try {
            editor.selection({});
            throw std::runtime_error("Selection accepted during a native rectangle gesture");
        } catch (const AuthoringError&) {
        }
        if (*image)
            surface.save(output / (std::string("ui-source-") + image + "-" + std::to_string(width) +
                                   ".png"));
        if (cancel == BoxCancel::Escape) {
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
        } else if (cancel == BoxCancel::Focus) {
            io.AddFocusEvent(false);
            frame();
            io.AddFocusEvent(true);
        } else if (cancel == BoxCancel::Lock) {
            source_locked = true;
            frame();
            source_locked = false;
        }
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        frame();
        require(!editor.gesture_active() && scene.snapshot() == authored,
                "Rectangle selection retained a gesture or changed Scene state");
        for (const auto* key : {"text", "revision", "state", "dirty", "can_undo", "can_redo"})
            require(editor.state().at(key) == selection_start.at(key),
                    "Rectangle selection changed source/history");
    };
    box_select(425, 135, 655, 285, false);
    require(editor.state().at("selection") == Json::array({members[1]}),
            "Rectangle replacement did not select its fully enclosed panel");
    box_select(55, 85, 365, 315, true);
    require(editor.state().at("selection") == Json::array({members[1], members[0]}),
            "Additive rectangle selection lost existing member order");
    box_select(950, 370, 50, 80, false, BoxCancel::None, "box-drag");
    require(editor.state().at("selection") == selection_start.at("selection"),
            "Reverse rectangle did not select all panels in source order");
    surface.save(output / ("ui-source-box-selection-" + std::to_string(width) + ".png"));
    for (const auto cancel : {BoxCancel::Escape, BoxCancel::Focus, BoxCancel::Lock}) {
        box_select(425, 135, 655, 285, false, cancel);
        require(editor.state().at("selection") == selection_start.at("selection"),
                "Cancelled rectangle changed selection membership");
    }
    box_select(470, 180, 470, 180, false);
    require(editor.state().at("selection") == Json::array({members[1]}),
            "Box-mode click did not select its hit control");
    box_select(760, 260, 760, 260, true);
    require(editor.state().at("selection") == Json::array({members[1], members[2]}),
            "Box-mode Ctrl-click did not add its hit control");
    box_select(470, 180, 470, 180, true);
    require(editor.state().at("selection") == Json::array({members[2]}),
            "Box-mode Ctrl-click did not remove its hit control");
    box_select(50, 80, 950, 370, false);
    input.click("ui-source:box-select");
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
    const auto click_canvas = [&](float x, float y) {
        const auto current = test::ui_targets.at("ui-source:canvas");
        const float fitted = (current.maximum.x - current.minimum.x) / 1000;
        io.AddMousePosEvent(current.minimum.x + x * fitted, current.minimum.y + y * fitted);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        const auto pressed = ImGui::GetMousePos();
        const std::array point{(pressed.x - current.minimum.x) / fitted,
                               (pressed.y - current.minimum.y) / fitted};
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        return point;
    };
    click_canvas(100, 130);
    input.click("ui-source:create-controls");
    require(test::ui_targets.at("ui-source:child-id").enabled &&
                !test::ui_targets.at("ui-source:place-control").enabled,
            "Placement inputs or empty-ID blocking differ for the native container");
    input.type("ui-source:child-id", "preview-control");
    input.type("ui-source:child-text", "New UI");
    input.type("ui-source:place-width", "100");
    input.type("ui-source:place-height", "36");
    input.click("ui-source:place-control");
    const auto original_placement = editor.state();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    click_canvas(180, 170);
    require(editor.state().at("text") == original_placement.at("text") && !editor.dirty(),
            "Escape did not cancel Preview placement");
    input.click("ui-source:place-control");
    const auto queued_canvas = test::ui_targets.at("ui-source:canvas");
    const float queued_scale = (queued_canvas.maximum.x - queued_canvas.minimum.x) / 1000;
    io.AddMousePosEvent(queued_canvas.minimum.x + 180 * queued_scale,
                        queued_canvas.minimum.y + 170 * queued_scale);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    require(editor.state().at("text") == original_placement.at("text"),
            "Canvas placement changed source in the image-submission frame");
    const auto parent_index = editor.state().at("selection").at(0).get<std::size_t>();
    editor.style(parent_index, "background-color", "#285d88");
    const auto intervening = editor.state();
    io.AddMouseButtonEvent(0, false);
    frame();
    require(editor.state().at("text") == intervening.at("text"),
            "A source change did not cancel queued placement");
    input.click("button:Undo");
    require(editor.state().at("text") == original_placement.at("text") && !editor.dirty(),
            "Cancellation fixture did not restore the original source");
    click_canvas(100, 130);
    input.click("ui-source:place-control");
    const auto before_placement = editor.state();
    frame();
    surface.save(output / ("ui-source-place-control-" + std::to_string(width) + ".png"));
    const auto point = click_canvas(180, 170);
    const auto placed = editor.state();
    require(placed.at("text") != before_placement.at("text") && editor.dirty(),
            "Native canvas placement did not publish its configured source");
    const auto preview = editor.preview_state(project, 1000, 700, 1, false);
    const auto found =
        std::find_if(preview.at("elements").begin(), preview.at("elements").end(),
                     [](const auto& row) { return row.at("id") == "preview-control"; });
    require(found != preview.at("elements").end() && found->at("editable").get<bool>() &&
                std::abs(found->at("bounds").at(0).get<float>() - point[0]) < .02f &&
                std::abs(found->at("bounds").at(1).get<float>() - point[1]) < .02f &&
                placed.at("selection") == Json::array({found->at("source_index")}) &&
                scene.snapshot() == authored,
            "Native placement corner/eligibility/selection or Scene ownership differs");
    frame();
    surface.save(output / ("ui-source-placed-control-" + std::to_string(width) + ".png"));
    input.click("button:Undo");
    require(editor.state().at("text") == before_placement.at("text"),
            "Preview control placement needed multiple Undo steps");
    input.click("button:Redo");
    require(editor.state().at("text") == placed.at("text"), "Preview placement Redo differs");
    editor.save(project);
    require(!editor.dirty() && scene.snapshot() == authored,
            "Preview placement Save changed Scene state");
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
