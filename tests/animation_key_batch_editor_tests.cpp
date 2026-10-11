#include "animation_timeline.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <set>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need key batch UI scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto actor =
                authoring_command(scene, "entity.create", {{"name", "Key batch preview"}})
                    .at("selected")
                    .get<std::string>();
            const auto scene_before = scene.document();
            auto source = TransformAnimationSource::create(AssetId::generate());
            source.document["duration_us"] = 6 * animation_timebase;
            const auto a = source.document.at("keys")[0].at("id").get<AssetId>();
            source.move_key(a, 123);
            TransformAnimationCurves curve;
            curve.cubic = true;
            source.set_key(123, {}, false, a, curve);
            const auto b = source.set_key(animation_timebase + 123, {{2, 0, 0}, {}, {1, 1, 1}},
                                          false, {}, curve);
            const auto c = source.set_key(4 * animation_timebase + 123, {{4, 0, 0}, {}, {1, 1, 1}});
            for (auto& key : source.document.at("keys"))
                key["extension"] = {{"original", true}};
            source.validate();
            atomic_write(root / "Assets/Batch.animation.json", source.document.dump(2));
            const auto baseline = source.document;
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1600, 1400};
            io.DeltaTime = 1.f / 60;
            io.FontGlobalScale = scale;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            ui::interface_scale = scale;
            unsigned char* pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            AnimationTimelineEditor editor;
            editor.open(project, "Assets/Batch.animation.json");
            require(bool(editor.document()), "Key batch source did not open");
            editor.bind(scene, actor);
            std::string message;
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, scene, locked, 1.0 / 60, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 1400}, ImGuiCond_Always);
                editor.draw(project, scene, locked);
                ImGui::Render();
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto target = [&](const std::string& name) {
                frame();
                require(test::ui_targets.contains(name), "Missing target " + name);
                auto box = test::ui_targets.at(name);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Animation Timeline");
                    require(window, "Timeline window missing");
                    ImGui::SetScrollY(window,
                                      window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) *
                                                             .5f);
                    frame();
                    frame();
                    box = test::ui_targets.at(name);
                }
                require(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                            box.maximum.y <= box.clip_maximum.y,
                        "Disabled/clipped target " + name);
                return box;
            };
            auto click = [&](const std::string& name) {
                const auto box = target(name);
                const ImVec2 p{(box.minimum.x + box.maximum.x) * .5f,
                               (box.minimum.y + box.maximum.y) * .5f};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto button = [&](const std::string& name) { click("button:" + name); };
            auto control = [&](bool down) {
                io.AddKeyEvent(ImGuiMod_Ctrl, down);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, down);
                frame();
            };
            auto key = [&](AssetId id, bool add = false) {
                if (add)
                    control(true);
                click("animation:key:" + id.str());
                if (add)
                    control(false);
            };
            auto number = [&](const char* text) {
                const auto box = target("animation:key_offset");
                const ImVec2 p{box.minimum.x + 50, (box.minimum.y + box.maximum.y) * .5f};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                control(true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                control(false);
                io.AddInputCharactersUTF8(text);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            auto point = [&](std::int64_t time) {
                const auto box = target("animation:ruler");
                return ImVec2{box.minimum.x +
                                  float(double(time) / 6000000) * (box.maximum.x - box.minimum.x),
                              box.minimum.y + 50};
            };
            frame();
            frame();
            const auto revision = editor.document()->revision();
            // Selection at a fractional-millisecond key cannot silently quantize authored time.
            const auto first = point(123);
            mouse(first, false);
            mouse(first, true);
            mouse(first, false);
            require(editor.document()->source().document == baseline &&
                        editor.document()->revision() == revision && !editor.can_undo(),
                    "Plain ruler click authored a submillisecond key");
            key(b, true);
            auto start = point(animation_timebase + 123);
            const auto box = target("animation:ruler");
            const auto width = box.maximum.x - box.minimum.x;
            mouse(start, false);
            mouse(start, true);
            const auto origin_x = io.MousePos.x;
            const ImVec2 release{start.x + width / 6, start.y};
            mouse(release, false);
            const auto offset =
                std::int64_t(std::llround((double(io.MousePos.x) - origin_x) / width * 6000)) *
                1000;
            auto expected = source;
            expected.move_keys(std::array{a, b}, offset);
            require(editor.document()->source().document == expected.document && editor.can_undo(),
                    "Sparse final release lost group spacing or metadata");
            const auto shifted = editor.document()->source().document;
            // Held preview and Escape leave source/history intact.
            start = point(animation_timebase + 123 + offset);
            mouse(start, false);
            mouse(start, true);
            mouse({start.x + width / 12, start.y}, true);
            require(editor.document()->source().document == shifted, "Held drag changed source");
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            frame();
            mouse({start.x + width / 12, start.y}, false);
            require(editor.document()->source().document == shifted,
                    "Escape committed a group drag");
            start = point(animation_timebase + 123 + offset);
            mouse(start, false);
            mouse(start, true);
            mouse({start.x + width / 12, start.y}, true);
            locked = true;
            frame();
            mouse({start.x + width / 12, start.y}, false);
            locked = false;
            frame();
            require(editor.document()->source().document == shifted,
                    "Play lock committed a captured group drag");
            // Changing focus or source revision cannot turn a held preview into an edit.
            auto held_drag = [&] {
                const auto at = point(animation_timebase + 123 + offset);
                mouse(at, false);
                mouse(at, true);
                mouse({at.x + width / 12, at.y}, true);
                return ImVec2{at.x + width / 12, at.y};
            };
            auto endpoint = held_drag();
            ImGui::FocusWindow(nullptr);
            frame();
            mouse(endpoint, false);
            require(editor.document()->source().document == shifted,
                    "Window focus loss committed a group drag");
            endpoint = held_drag();
            io.AddFocusEvent(false);
            frame();
            io.AddFocusEvent(true);
            frame();
            mouse(endpoint, false);
            require(editor.document()->source().document == shifted,
                    "Application focus loss committed a group drag");
            endpoint = held_drag();
            editor.undo();
            mouse(endpoint, false);
            require(editor.document()->source().document == baseline,
                    "Source revision change committed a stale group drag");
            editor.redo();
            frame();
            endpoint = held_drag();
            editor.request_close();
            mouse(endpoint, false);
            require(editor.document()->source().document == shifted,
                    "Closing committed a group drag");
            button("Keep editing");
            button("Undo");
            require(editor.document()->source().document == baseline, "Group Undo differs");
            button("Redo");
            require(editor.document()->source().document == shifted, "Group Redo differs");
            button("Undo");
            number("-1");
            const auto invalid_revision = editor.document()->revision();
            button("Move selected keys");
            require(editor.document()->source().document == baseline &&
                        editor.document()->revision() == invalid_revision,
                    "Invalid group offset changed source/history");
            number("1");
            button("Move selected keys");
            expected = source;
            expected.move_keys(std::array{a, b}, animation_timebase);
            require(editor.document()->source().document == expected.document,
                    "Numeric group movement differs");
            button("Undo");
            button("Clear key selection");
            frame();
            require(!test::ui_targets.at("button:Move selected keys").enabled,
                    "Empty selection permits movement");
            button("Select all keys");
            frame();
            require(!test::ui_targets.at("animation:delete_keys").enabled,
                    "All-key deletion enabled");
            key(c);
            number("1");
            button("Duplicate selected keys");
            const auto duplicated = editor.document()->source().document;
            require(duplicated.at("keys").size() == 4, "Actual duplication did not add a key");
            const std::set<AssetId> originals{a, b, c};
            AssetId copy;
            for (const auto& row : duplicated.at("keys"))
                if (!originals.contains(row.at("id").get<AssetId>()))
                    copy = row.at("id").get<AssetId>();
            require(bool(copy), "Duplicate reused original identity");
            button("Undo");
            require(editor.document()->source().document == baseline, "Duplicate Undo differs");
            button("Redo");
            require(editor.document()->source().document == duplicated,
                    "Duplicate Redo changed identities");
            key(copy);
            click("animation:delete_keys");
            require(editor.document()->source().document == baseline,
                    "Deleting duplicate changed original metadata");
            button("Undo");
            require(editor.document()->source().document == duplicated,
                    "Delete Undo changed key identity");
            // Creating a key selects the new identity so Delete acts on that key.
            const auto empty_time = point(5500000);
            mouse(empty_time, false);
            mouse(empty_time, true);
            mouse(empty_time, false);
            button("Add / Update key at playhead");
            require(editor.document()->source().clip().keys.size() == 5 && !editor.can_redo(),
                    "New key did not join the clip or discarded redo branch survived");
            click("animation:delete_keys");
            require(editor.document()->source().document == duplicated,
                    "Deleting immediately after Add did not remove the newly selected key");
            button("Save & Publish");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.pending() && !editor.dirty(), "Key batch publication failed");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Published batch did not close");
            editor.open(project, "Assets/Batch.animation.json");
            frame();
            require(editor.document() && editor.document()->source().document == duplicated,
                    "Key batch save/reopen lost metadata/IDs");
            const auto publish_start = point(5000123);
            mouse(publish_start, false);
            mouse(publish_start, true);
            mouse({publish_start.x + width / 12, publish_start.y}, true);
            editor.request_save();
            frame();
            mouse({publish_start.x + width / 12, publish_start.y}, false);
            const auto publish_deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (editor.pending() && std::chrono::steady_clock::now() < publish_deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.pending() && !editor.dirty() &&
                        editor.document()->source().document == duplicated,
                    "Save during a held drag committed the capture or lost publication");
            require(scene.document() == scene_before, "Key edits changed Scene ownership");
            editor.request_close();
            frame();
            test::observe_ui = false;
            ImGui::DestroyContext();
            std::filesystem::remove_all(root);
        }
        ui::interface_scale = 1;
        std::cout << "Actual "
                     "Ctrl/group/sparse-release/cancel/lock/move/duplicate/delete/history/publish/"
                     "reopen65/100/200% passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
