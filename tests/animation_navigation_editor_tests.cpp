#include "animation_timeline.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) { check(std::abs(a - b) < .00005, "Timeline view numeric mismatch"); }
void view_math() {
    AnimationTimelineView view;
    view.reconcile(64000000);
    view.zoom(2, .25);
    near(view.first, 8000000);
    near(view.span, 32000000);
    near(view.time(.25), 16000000);
    view.pan(-1e20);
    near(view.first, 0);
    view.pan(1e20);
    near(view.first, 32000000);
    view.fit(8000000, 16000000);
    near(view.first, 7200000);
    near(view.span, 9600000);
    check(view.visible(8000000) && !view.visible(0), "View visibility differs");
    const auto before = view;
    view.zoom(std::numeric_limits<double>::quiet_NaN(), .5);
    view.pan(std::numeric_limits<double>::infinity());
    view.fit(5, 4);
    view.reconcile(0);
    near(view.first, before.first);
    near(view.span, before.span);
    view.fit(0, 0);
    check(view.first == 0 && view.span > 1 && view.span < view.duration,
          "Single-key fit missing margin");
    view.reconcile(1);
    near(view.first, 0);
    near(view.span, 1);
    view.zoom(1e20, .5);
    near(view.span, 1);
    view.fit_clip();
    view.reconcile(3600000000.);
    near(view.span, 3600000000.);
    view.zoom(1e20, 1);
    near(view.first, 3599999999.);
    near(view.span, 1);
    near(view.time(1), 3600000000.);
    view.reconcile(100);
    near(view.first, 99);
    view.fit_clip();
    view.reconcile(200);
    near(view.span, 200);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need animation navigation scratch root");
        view_math();
        for (const auto [scale, count] :
             {std::pair{1.f, 64u}, {1.5f, 64u}, {2.f, 64u}, {1.f, 4096u}}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto actor = authoring_command(scene, "entity.create", {{"name", "View preview"}})
                                   .at("selected")
                                   .get<std::string>();
            const auto authored_scene = scene.document();
            auto owner = TransformAnimationDocument::create(project.writer_guard(),
                                                            "Assets/View.animation.json");
            owner->edit(owner->revision(), "Authored key fixture", [&](Json& draft) {
                const auto key = draft.at("keys")[0];
                draft["duration_us"] = count == 4096 ? 3600000000LL : 64000000LL;
                draft["keys"] = Json::array();
                for (unsigned i = 0; i < count; ++i) {
                    auto item = key;
                    item["id"] = AssetId::generate();
                    item["time_us"] = std::int64_t(i) * (count == 4096 ? 500000 : 1000000);
                    item["translation"][0] = double(i);
                    draft["keys"].push_back(std::move(item));
                }
            });
            owner->save();
            const auto ids = owner->source().clip().keys;
            owner.reset();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2500, 2200};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            // Match the pinned production backend's base-vertex capability.
            // This fixture verifies generated draw data, not GPU execution.
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            unsigned char* pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            ui::style(scale);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            AnimationTimelineEditor editor;
            editor.open(project, "Assets/View.animation.json");
            editor.bind(scene, actor);
            bool locked = false, popup = false;
            float window_width = 2400;
            std::string message;
            auto frame = [&] {
                editor.poll(project, scene, locked, io.DeltaTime, message);
                test::ui_targets.clear();
                ImGui::NewFrame();
                if (std::exchange(popup, false))
                    ImGui::OpenPopup("Navigation test");
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({window_width, 2100}, ImGuiCond_Always);
                editor.draw(project, scene, locked);
                if (ImGui::BeginPopup("Navigation test")) {
                    ImGui::TextUnformatted("Gesture interruption");
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::Render();
                if (count == 4096) {
                    const auto* data = ImGui::GetDrawData();
                    for (const auto* list : data->CmdLists) {
                        bool split = false;
                        for (const auto& command : list->CmdBuffer) {
                            split |= command.VtxOffset != 0;
                            const auto offset = std::size_t(command.IdxOffset);
                            check(offset <= std::size_t(list->IdxBuffer.Size) &&
                                      command.ElemCount <=
                                          std::size_t(list->IdxBuffer.Size) - offset,
                                  "Dense draw index range exceeds buffer");
                            for (unsigned i = 0; i < command.ElemCount; ++i)
                                check(std::size_t(command.VtxOffset) +
                                              list->IdxBuffer[int(offset + i)] <
                                          std::size_t(list->VtxBuffer.Size),
                                      "Dense draw vertex offset exceeds buffer");
                        }
                        check(list->VtxBuffer.Size <= 65535 || split,
                              "Dense draw list did not split large vertex data");
                    }
                }
            };
            auto target = [&](const std::string& name) {
                check(test::ui_targets.contains(name), "Missing navigation control " + name);
                const auto box = test::ui_targets.at(name);
                check(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y,
                      "Clipped navigation control " + name);
                return box;
            };
            auto mouse = [&](ImVec2 p, int button, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(button, down);
                frame();
            };
            auto click = [&](const std::string& name) {
                const auto box = target("button:" + name);
                check(box.enabled, "Disabled navigation control " + name);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                mouse(p, 0, false);
                mouse(p, 0, true);
                mouse(p, 0, false);
                frame();
            };
            auto point = [&](double fraction, float row = 58) {
                const auto box = target("animation:ruler");
                return ImVec2{box.minimum.x + float(fraction) * (box.maximum.x - box.minimum.x),
                              box.minimum.y + row};
            };
            auto drag = [&](ImVec2 a, ImVec2 b, int button = 0) {
                mouse(a, button, false);
                mouse(a, button, true);
                mouse(b, button, true);
                mouse(b, button, false);
                frame();
            };
            auto modifiers = [&](bool shift, bool ctrl = false, bool alt = false) {
                io.AddKeyEvent(ImGuiMod_Shift, shift);
                io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
                io.AddKeyEvent(ImGuiMod_Alt, alt);
                frame();
            };
            frame();
            frame();
            auto source = editor.document()->source().document;
            auto revision = editor.document()->revision();
            auto unchanged = [&] {
                check(editor.document()->source().document == source &&
                          editor.document()->revision() == revision &&
                          scene.document() == authored_scene,
                      "Navigation/selection changed source or authored Scene");
            };
            if (count == 4096) {
                modifiers(true);
                drag(point(0, 28), point(1, 90));
                modifiers(false);
                check(editor.selected_keys().size() == 4096, "Dense box lost key identities");
                click("Fit selected keys");
                click("Zoom in");
                click("Fit clip");
                unchanged();
                ImGui::DestroyContext();
                continue;
            }
            modifiers(true);
            drag(point(.118, 28), point(.26, 90));
            modifiers(false);
            std::set<AssetId> selected;
            for (unsigned i = 8; i <= 16; ++i)
                selected.insert(ids[i].id);
            check(editor.selected_keys() == selected, "Box replace selected wrong keys");
            unchanged();
            click("Fit selected keys");
            auto scrub = [&](double fraction, double first, double span) {
                const auto box = target("animation:ruler");
                const auto p = point(fraction);
                mouse(p, 0, false);
                mouse(p, 0, true);
                mouse(p, 0, false);
                const auto expected = first + (double(io.MousePos.x) - box.minimum.x) /
                                                  (box.maximum.x - box.minimum.x) * span;
                const auto preview = editor.preview(scene.document());
                for (const auto& row : preview.at("entities"))
                    if (row.at("id").get<EntityId>().str() == actor)
                        near(row.at("components")
                                 .at("forge.local_translation")
                                 .at("x")
                                 .get<double>(),
                             expected);
            };
            scrub(.35, 7.2, 9.6);
            click("Fit clip");
            modifiers(true);
            drag(point(.118, 28), point(.26, 90));
            modifiers(true, true);
            drag(point(40.2 / 64, 28), point(45.8 / 64, 90));
            for (unsigned i = 41; i <= 45; ++i)
                selected.insert(ids[i].id);
            check(editor.selected_keys() == selected, "Box add lost baseline selection");
            modifiers(true, true, true); // Alt takes precedence over Ctrl.
            drag(point(11.8 / 64, 28), point(14.2 / 64, 90));
            for (unsigned i = 12; i <= 14; ++i)
                selected.erase(ids[i].id);
            check(editor.selected_keys() == selected, "Box removal selected wrong keys");
            modifiers(true);
            const auto a = point(.5, 28), b = point(.8, 90);
            mouse(a, 0, false);
            mouse(a, 0, true);
            mouse(b, 0, true);
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            mouse(b, 0, false);
            check(editor.selected_keys() == selected, "Escape lost box baseline");
            mouse(a, 0, true);
            mouse(b, 0, true);
            editor.undo();
            mouse(b, 0, false);
            check(editor.selected_keys() == selected, "Undo lost box baseline");
            mouse(a, 0, true);
            mouse(b, 0, true);
            io.AddFocusEvent(false);
            frame();
            io.AddFocusEvent(true);
            frame();
            mouse(b, 0, false);
            check(editor.selected_keys() == selected, "Focus loss committed box capture");
            modifiers(true); // Platform focus loss clears held modifier keys.
            mouse(a, 0, true);
            mouse(b, 0, true);
            check(editor.selected_keys() != selected, "Resize fixture did not begin box selection");
            window_width -= 100;
            frame();
            mouse(b, 0, false);
            check(editor.selected_keys() == selected, "Resize committed box capture");
            window_width += 100;
            frame();
            mouse(point(.5, 28), 0, true);
            mouse(point(.8, 90), 0, true);
            locked = true;
            frame();
            mouse(point(.8, 90), 0, false);
            check(editor.selected_keys() == selected, "Play guard committed box capture");
            check(!target("button:Zoom in").enabled, "Locked timeline allowed view button");
            locked = false;
            modifiers(false);
            frame();
            click("Fit clip");
            click("Zoom in");
            scrub(.35, 16, 32);
            click("Fit clip");
            const auto anchor = point(.25);
            mouse(anchor, 0, false);
            modifiers(false, true);
            const auto wheel_box = target("animation:ruler");
            const auto wheel_first = (double(io.MousePos.x) - wheel_box.minimum.x) /
                                     (wheel_box.maximum.x - wheel_box.minimum.x) * 32;
            io.AddMouseWheelEvent(0, 2);
            frame();
            modifiers(false);
            scrub(.35, wheel_first, 32);
            click("Fit clip");
            click("Zoom in");
            const auto pan_box = target("animation:ruler");
            const auto pan_a = point(.5), pan_b = point(.25);
            mouse(pan_a, 2, false);
            mouse(pan_a, 2, true);
            const auto pan_first = double(io.MousePos.x);
            mouse(pan_b, 2, true);
            const auto pan_end = double(io.MousePos.x);
            mouse(pan_b, 2, false);
            scrub(.35, 16 + (pan_first - pan_end) / (pan_box.maximum.x - pan_box.minimum.x) * 32,
                  32);
            unchanged();
            click("Fit clip");
            click("Zoom in");
            mouse(point(.5), 2, false);
            mouse(point(.5), 2, true);
            mouse(point(.25), 2, true);
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            mouse(point(.25), 2, false);
            scrub(.35, 16, 32);
            // Group movement uses the visible32-second interval, not the64-second clip.
            const auto move_box = target("animation:ruler");
            const auto move_a = point(4. / 32), move_b = point(4.25 / 32);
            mouse(move_a, 0, false);
            mouse(move_a, 0, true);
            const auto move_first = double(io.MousePos.x);
            mouse(move_b, 0, false);
            const auto offset = std::llround((double(io.MousePos.x) - move_first) /
                                             (move_box.maximum.x - move_box.minimum.x) * 32000) *
                                1000;
            auto moved = source;
            moved["keys"][20]["time_us"] = 20000000 + offset;
            check(editor.document()->source().document == moved &&
                      editor.document()->revision() == revision + 1,
                  "Zoomed sparse-release drag used wrong time range/history");
            editor.undo();
            frame();
            revision = editor.document()->revision();
            unchanged();
            click("Fit clip");
            modifiers(true);
            // The final release position is processed even without a held move frame.
            const auto release_a = point(.118, 28), release_b = point(.26, 90);
            mouse(release_a, 0, false);
            mouse(release_a, 0, true);
            mouse(release_b, 0, false);
            modifiers(false);
            selected.clear();
            for (unsigned i = 8; i <= 16; ++i)
                selected.insert(ids[i].id);
            check(editor.selected_keys() == selected, "Release-only box position was lost");
            unchanged();
            modifiers(true);
            mouse(point(.5, 28), 0, true);
            mouse(point(.8, 90), 0, true);
            popup = true;
            frame();
            mouse(point(.8, 90), 0, false);
            check(editor.selected_keys() == selected, "Popup committed box capture");
            frame();
            mouse(point(.5, 28), 0, true);
            mouse(point(.8, 90), 0, true);
            // An incoming edit uses this same document owner, not a second writer.
            auto& document = *const_cast<TransformAnimationDocument*>(editor.document());
            document.edit(document.revision(), "Incoming metadata",
                          [](Json& draft) { draft["external_metadata"] = true; });
            source = document.source().document;
            revision = document.revision();
            frame();
            mouse(point(.8, 90), 0, false);
            check(editor.selected_keys() == selected, "Source revision committed box capture");
            modifiers(false);
            unchanged();
            ImGui::DestroyContext();
        }
        std::cout << "Timeline view math, scaled zoom/pan/scrub, box selection/cancel/guards, "
                     "source isolation and4096-key interaction passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
