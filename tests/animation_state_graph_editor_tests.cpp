#include "editor/animation_states_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and graph UI scratch root");
        for (const float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
            std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root);
            const auto path = std::filesystem::path("Assets/TimedLocomotion.states.json");
            AnimationStatesEditor editor;
            editor.open(project, path);
            require(editor.document() && !editor.dirty(), "Published states did not open cleanly");
            const auto original = editor.document()->source().document;
            const auto original_cook = editor.document()->source().cook();
            const auto authored = scene.document();
            const auto catalog = AssetCatalog::open_project(root);
            const auto skeleton =
                catalog.resolve(original.at("skeleton").get<AssetId>(), SkeletonAsset::type);
            const auto clips = asset_picker_entries(catalog, AnimationClipAsset::type, "", false,
                                                    skeleton.record->subasset->owner);
            require(clips.size() == 3, "State clip scope does not match the imported rig");
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2400, 2600};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::style(scale);
            test::observe_ui = true;
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            std::string message;
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({2300, 2500}, ImGuiCond_Always);
                editor.draw(project, scene, locked);
                ImGui::Render();
            };
            auto target = [&](const std::string& id) {
                const auto found = test::ui_targets.find(id);
                require(found != test::ui_targets.end(), "Missing UI target " + id);
                const auto box = found->second;
                const ImVec2 center{(box.minimum.x + box.maximum.x) / 2,
                                    (box.minimum.y + box.maximum.y) / 2};
                // ImGui Selectable expands its hit rectangle through half the row spacing.
                // The clipped list margin is expected; click its visible center.
                if (id.starts_with("picker-option:")) {
                    require(box.enabled && center.x >= box.clip_minimum.x &&
                                center.x <= box.clip_maximum.x && center.y >= box.clip_minimum.y &&
                                center.y <= box.clip_maximum.y,
                            "Picker option has no visible click center " + id);
                    return center;
                }
                require(
                    box.enabled && box.minimum.y >= box.clip_minimum.y &&
                        box.maximum.y <= box.clip_maximum.y &&
                        box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x,
                    "Disabled/clipped graph control " + id + " at scale " + std::to_string(scale));
                return center;
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& id) {
                frame();
                const auto p = target(id);
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto number = [&](const std::string& id, const char* text) {
                frame();
                const auto p = target(id);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(text);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            frame();
            frame();
            for (const auto id : {1, 2, 3}) {
                target("animation_graph:node:" + std::to_string(id));
                target("animation_graph:edge:" + std::to_string(id));
            }
            click("animation_graph:node:2");
            number("animation_states:name", "Walking");
            require(editor.document()->source().compiled().state(2).name == "Walking",
                    "Graph selection does not share the state inspector");
            editor.undo();
            frame();
            require(editor.document()->source().document == original, "State rename Undo failed");
            click("asset-picker:animation_clip:Clip");
            frame();
            frame();
            const auto alternative =
                std::find_if(clips.begin(), clips.end(), [&](const auto& clip) {
                    return Json(clip.id) != original.at("states")[1].at("clip");
                });
            require(alternative != clips.end(), "No compatible alternative state clip");
            for (const auto& [id, record] : catalog.records())
                if (record.type == AnimationClipAsset::type &&
                    !asset_picker_scope_matches(record, skeleton.record->subasset->owner))
                    require(!test::ui_targets.contains("picker-option:" + id.str()),
                            "State clip picker exposed another model's clip");
            click("picker-option:" + alternative->id.str());
            require(editor.document()->source().compiled().state(2).clip.id == alternative->id,
                    "Actual scoped clip picker did not edit the graph's selected state");
            editor.undo();
            frame();
            click("animation_graph:out:1");
            number("animation_states:name", "Idle renamed");
            require(!test::ui_targets.at("animation_graph:in:3").enabled,
                    "Source edits retained a stale pending connection");
            editor.undo();
            frame();
            click("animation_graph:out:1");
            require(!test::ui_targets.at("animation_graph:in:1").enabled,
                    "Self transition input was enabled");
            click("animation_graph:in:3");
            require(editor.document()->source().compiled().state(1).exit_state == 3,
                    "Graph connection did not rewire the existing exit");
            number("animation_states:exit_phase", "0.625");
            require(editor.document()->source().compiled().state(1).exit_phase == .625,
                    "Graph exit did not use the existing phase inspector");
            click("animation_graph:out:1");
            click("animation_graph:in:2");
            require(editor.document()->source().compiled().state(1).exit_state == 2 &&
                        editor.document()->source().compiled().state(1).exit_phase == .625,
                    "Rewiring changed the existing exit time");
            editor.undo();
            editor.undo();
            editor.undo();
            frame();
            require(editor.document()->source().document == original,
                    "Connection edits were not individual source Undo steps");
            click("animation_graph:edge:1");
            click("animation_graph:remove-exit");
            require(editor.document()->source().compiled().state(1).exit_state == 0,
                    "Removing the selected exit failed");
            editor.undo();
            frame();
            click("animation_graph:node:3");
            click("button:Make entry state");
            require(editor.document()->source().compiled().entry == 3,
                    "Graph selection did not set the entry");
            editor.undo();
            frame();
            const auto start = target("animation_graph:node:2");
            const auto revision = editor.document()->revision();
            mouse(start, false);
            mouse(start, true);
            const ImVec2 moved{start.x + 60 * scale, start.y + 35 * scale};
            mouse(moved, true);
            frame();
            require(editor.dirty() && editor.document()->revision() == revision,
                    "Dragging committed before release or hid dirty intent");
            mouse(moved, false);
            frame();
            require(editor.document()->revision() == revision + 1 &&
                        editor.document()->source().cook() == original_cook,
                    "Card drag was not one editor-only history step");
            const auto layout = editor.document()->source().document;
            require(asset_detail::has_state_graph_layout(layout),
                    "Card move did not persist source-owned layout");
            editor.undo();
            frame();
            require(editor.document()->source().document == original, "Card move Undo failed");
            editor.redo();
            frame();
            require(editor.document()->source().document == layout,
                    "Card move Redo lost stable layout");
            editor.undo();
            frame();
            const auto cancel_start = target("animation_graph:node:2");
            mouse(cancel_start, false);
            mouse(cancel_start, true);
            mouse({cancel_start.x + 40, cancel_start.y + 20}, true);
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            frame();
            mouse({cancel_start.x + 40, cancel_start.y + 20}, false);
            frame();
            require(editor.document()->source().document == original,
                    "Escape committed a canceled card move");
            const auto focus_start = target("animation_graph:node:2");
            mouse(focus_start, false);
            mouse(focus_start, true);
            mouse({focus_start.x + 35, focus_start.y + 25}, true);
            io.AddFocusEvent(false);
            frame();
            io.AddFocusEvent(true);
            frame();
            mouse(focus_start, false);
            frame();
            require(editor.document()->source().document == original,
                    "Focus loss committed a held card move");
            const auto close_start = target("animation_graph:node:2");
            mouse(close_start, false);
            mouse(close_start, true);
            mouse({close_start.x + 35 * scale, close_start.y + 25 * scale}, true);
            editor.request_close();
            mouse({close_start.x + 35 * scale, close_start.y + 25 * scale}, false);
            frame();
            frame();
            require(editor.document()->source().document != original && editor.dirty(),
                    "Close lost an active graph gesture before its dirty guard");
            click("button:Keep editing");
            require(editor.close_cancelled && editor.is_open() && editor.dirty(),
                    "Dirty graph close could not keep editing");
            editor.undo();
            frame();
            require(editor.document()->source().document == original,
                    "Close gesture was not one history entry");
            click("animation_graph:arrange");
            const auto arranged = editor.document()->source().document;
            require(arranged.at("editor_layout").at("positions").size() == 3 &&
                        editor.document()->source().cook() == original_cook,
                    "Arrange did not preserve compiled state behavior");
            click("animation_graph:frame");
            const auto nav_revision = editor.document()->revision();
            const auto nav = target("animation_graph:node:1");
            io.AddMousePosEvent(nav.x, nav.y);
            io.AddMouseWheelEvent(0, 1);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
            frame();
            io.AddMousePosEvent(nav.x + 20, nav.y + 10);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
            frame();
            require(editor.document()->revision() == nav_revision &&
                        editor.document()->source().document == arranged,
                    "View navigation changed the authored graph");
            click("animation_graph:frame");
            locked = true;
            frame();
            require(!test::ui_targets.at("animation_graph:out:1").enabled,
                    "Locked graph permitted a source connection");
            locked = false;
            frame();
            require(scene.document() == authored, "Graph authoring edited Scene history");
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty() && bool(editor.take_catalog()),
                    "Graph layout publication failed");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Published graph failed to close");
            editor.open(project, path);
            frame();
            frame();
            require(editor.document()->source().document == arranged &&
                        editor.document()->source().cook() == original_cook,
                    "Graph reopen changed layout or animation semantics");
            editor.request_close();
            frame();
            frame();
            require(!editor.is_open(), "Clean graph close failed");
            editor.open(project, path);
            frame();
            frame();
            const auto discard_start = target("animation_graph:node:2");
            mouse(discard_start, false);
            mouse(discard_start, true);
            mouse({discard_start.x + 30, discard_start.y + 20}, true);
            mouse({discard_start.x + 30, discard_start.y + 20}, false);
            editor.request_close();
            frame();
            frame();
            click("button:Discard");
            require(!editor.is_open(), "Dirty graph could not discard its draft");
            editor.open(project, path);
            frame();
            frame();
            require(editor.document()->source().document == arranged,
                    "Discard wrote the draft graph to its saved source");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual animation graph at100/150/200%: selection, timed connections, entry, "
                     "drag/Undo/cancel, layout, navigation, locking and publication passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
