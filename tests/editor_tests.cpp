#include "authoring_tests.hpp"
#include "automation_tests.hpp"
#include "blockout_tests.hpp"
#include "camera.hpp"
#include "camera_controls.hpp"
#include "command_workspace_tests.hpp"
#include "document_tests.hpp"
#include "help.hpp"
#include "interaction_tests.hpp"
#include "play.hpp"
#include "prefab_editor_tests.hpp"
#include "scene_cache_tests.hpp"
#include "sdl_input_tests.hpp"
#include "status_bar.hpp"
#include "widgets.hpp"
#include "workspace_tests.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void test_camera_input() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900, 500};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    forge::EditorCamera camera;
    bool scene_focused = false;
    auto frame = [&](bool focus_world = false, bool application_focused = true) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({250, 400});
        if (focus_world)
            ImGui::SetNextWindowFocus();
        ImGui::Begin("World test");
        ImGui::TextUnformatted("World selection");
        ImGui::End();
        ImGui::SetNextWindowPos({300, 0});
        ImGui::SetNextWindowSize({500, 400});
        ImGui::Begin("Scene test");
        forge::ui::camera_controls(camera, {450, 320}, application_focused);
        scene_focused = ImGui::IsWindowFocused();
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame(true);
    require(!scene_focused, "Fixture did not focus World");
    io.AddMousePosEvent(500, 150);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    frame();
    require(scene_focused, "MMB did not acquire Scene from World");
    io.AddMousePosEvent(480, 170);
    frame();
    require(camera.eye()[0] > 0 && camera.eye()[1] > 1,
            "MMB left/down must reveal right/top faces");
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    frame();
    camera = {};
    frame(true);
    io.AddKeyEvent(ImGuiMod_Shift, true);
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    frame();
    require(scene_focused, "Shift+MMB did not acquire Scene from World");
    io.AddMousePosEvent(460, 190);
    frame();
    require(camera.eye()[0] > 0 && camera.eye()[1] > 1 && camera.yaw == 0 && camera.pitch == 0,
            "Shift+MMB left/down must pan right/up without rotating");
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    frame();
    camera = {};
    frame(true);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    require(scene_focused, "RMB did not acquire Scene from World");
    const auto position = camera.eye();
    io.AddMousePosEvent(490, 200);
    frame();
    for (unsigned i = 0; i < 3; ++i)
        require(std::abs(camera.eye()[i] - position[i]) < 0.00001f,
                "RMB look moved camera position");
    require(camera.yaw > 0 && camera.pitch < 0, "RMB look direction failed");
    io.AddKeyEvent(ImGuiKey_W, true);
    frame();
    require(camera.eye() != position, "RMB+W did not fly");
    io.AddKeyEvent(ImGuiKey_W, false);
    const auto before_up = camera.eye();
    io.AddKeyEvent(ImGuiKey_Space, true);
    frame();
    require(camera.eye()[1] > before_up[1] && std::abs(camera.eye()[0] - before_up[0]) < 0.00001f &&
                std::abs(camera.eye()[2] - before_up[2]) < 0.00001f,
            "RMB+Space must ascend in world Y");
    io.AddKeyEvent(ImGuiKey_Space, false);
    const auto before_down = camera.eye();
    io.AddKeyEvent(ImGuiMod_Shift, true);
    frame();
    require(camera.eye()[1] < before_down[1], "RMB+Shift must descend");
    io.AddKeyEvent(ImGuiKey_Space, true);
    const auto opposed = camera.eye();
    frame();
    require(camera.eye() == opposed, "Space and Shift should cancel altitude movement");
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    frame();
    const auto stopped = camera.eye();
    frame();
    require(camera.eye() == stopped, "Flight continued after RMB release");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    io.AddFocusEvent(false);
    const auto before_focus_loss = camera.eye();
    frame(false, false);
    require(camera.eye() == before_focus_loss, "Camera moved after application focus loss");
    io.AddFocusEvent(true);
    io.AddKeyEvent(ImGuiKey_W, false);
    io.AddKeyEvent(ImGuiKey_Space, false);
    frame();
    io.AddMousePosEvent(100, 100);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    io.AddKeyEvent(ImGuiKey_W, true);
    io.AddMousePosEvent(500, 150);
    frame();
    require(camera.eye() == before_focus_loss, "Drag begun outside viewport acquired flight");
    ImGui::DestroyContext();
}
void test_telemetry_and_status() {
    forge::FrameAverages averages;
    for (int i = 0; i < 3; ++i)
        require(!averages.add(0.125), "Averages sampled too early");
    require(averages.add(0.125) && averages.fps == 8 && averages.milliseconds == 125,
            "Frame rate/average frame time calculation failed");
    require(!averages.add(std::numeric_limits<double>::quiet_NaN()),
            "Invalid frame sample accepted");
    forge::ProcessCounters before, after;
    before.cpu_ticks = 100;
    after.cpu_ticks = 10000100;
    after.processors = 8;
    require(forge::cpu_percent(before, after, 0.5) == 25, "CPU capacity normalization failed");
    require(!forge::cpu_percent(after, before, 0.5), "Counter reset accepted");
    require(!forge::cpu_percent(before, after, 0), "Zero sampling period accepted");
#ifdef _WIN32
    const auto native = forge::process_counters();
    require(native.cpu_ticks.has_value() && native.processors > 0,
            "Windows CPU counters unavailable");
    require(native.working_set.value_or(0) > 0 && native.private_bytes.value_or(0) > 0,
            "Windows process memory counters unavailable");
#endif
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    forge::Telemetry stats;
    stats.frames = averages;
    stats.cpu = 25;
    stats.process.working_set = 512 * 1048576ULL;
    for (float scale : {0.65f, 0.9f, 1.0f, 2.0f}) {
        forge::ui::style(scale);
        for (float window_width : {1440.0f, 640.0f}) {
            io.DisplaySize = {window_width, 900};
            for (int frame = 0; frame < 3; ++frame) {
                ImGui::NewFrame();
                forge::ui::status_bar(stats, false, 17);
                ImGui::DockSpaceOverViewport();
                if (forge::ui::begin_toolbar()) {
                    ImGui::Button("Save");
                    const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
                    const auto* toolbar = ImGui::GetCurrentWindow();
                    require(minimum.y - toolbar->Pos.y >= 5 * scale &&
                                toolbar->Pos.y + toolbar->Size.y - maximum.y >= 5 * scale,
                            "Toolbar lacks padding above/below controls");
                    forge::ui::end_toolbar();
                }
                ImGui::Render();
            }
            const auto* bar = ImGui::FindWindowByName("##FORGE-status");
            const auto* viewport = ImGui::GetMainViewport();
            require(bar && bar->Active, "Permanent status bar is missing");
            const auto* toolbar = ImGui::FindWindowByName("##FORGE-toolbar");
            require(toolbar && viewport->WorkPos.y >= toolbar->Pos.y + toolbar->Size.y - 1,
                    "Docking work area overlaps toolbar");
            require(std::abs(bar->Pos.y + bar->Size.y - viewport->Size.y) < 1,
                    "Status bar is not pinned to the bottom");
            require(viewport->WorkPos.y + viewport->WorkSize.y <= bar->Pos.y + 1,
                    "Docking work area overlaps status bar");
            require(bar->ContentSize.x <= bar->Size.x - 2 * ImGui::GetStyle().WindowPadding.x + 1,
                    "Status fields overflow at supported scale/width");
        }
    }
    ImGui::DestroyContext();
}
void test_tooltip_placement() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 0.05f;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    for (float scale : {0.65f, 0.9f, 1.0f, 2.0f}) {
        forge::ui::style(scale);
        for (bool bottom : {false, true}) {
            const ImVec2 anchor{400, bottom ? 520.0f : 60.0f};
            bool disabled = false;
            float wrap_limit = 0;
            ImRect item;
            auto frame = [&]() {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Tooltip fixture", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
                ImGui::SetCursorScreenPos(anchor);
                ImGui::BeginDisabled(disabled);
                ImGui::Button("Delete subtree");
                item = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
                wrap_limit = ImGui::GetFontSize() * 30 + 2 * ImGui::GetStyle().WindowPadding.x + 1;
                forge::ui::help("Delete this entity and all descendants. Undo restores them. "
                                "Prefabs used outside the subtree cannot be deleted.");
                ImGui::EndDisabled();
                ImGui::End();
                ImGui::Render();
            };
            auto tooltip = [&]() -> ImGuiWindow* {
                auto& context = *ImGui::GetCurrentContext();
                for (auto* window : context.Windows)
                    if ((window->Flags & ImGuiWindowFlags_Tooltip) &&
                        window->LastFrameActive == context.FrameCount)
                        return window;
                return nullptr;
            };
            io.AddMousePosEvent(5, 5);
            frame();
            frame();
            io.AddMousePosEvent(anchor.x + 10, anchor.y + 10);
            frame();
            frame();
            require(!tooltip(), "Tooltip appeared without hover delay");
            for (int i = 0; i < 16; ++i)
                frame();
            auto* tip = tooltip();
            require(tip, "Delayed tooltip did not appear");
            const ImRect bounds{tip->Pos, {tip->Pos.x + tip->Size.x, tip->Pos.y + tip->Size.y}};
            require(!bounds.Overlaps(item), "Tooltip covers the hovered control");
            require(bounds.Min.x >= 0 && bounds.Min.y >= 0 && bounds.Max.x <= 801 &&
                        bounds.Max.y <= 601,
                    "Tooltip leaves the screen");
            require(tip->Size.x <= wrap_limit,
                    ("Long tooltip did not wrap: scale=" + std::to_string(scale) + " width=" +
                     std::to_string(tip->Size.x) + " limit=" + std::to_string(wrap_limit))
                        .c_str());
            forge::ui::tooltips = false;
            frame();
            require(!tooltip(), "Global tooltip toggle ignored");
            forge::ui::tooltips = true;
            disabled = true;
            for (int i = 0; i < 16; ++i)
                frame();
            require(tooltip(), "Disabled control lost contextual help");
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
            frame();
            require(!tooltip(), "Tooltip appeared during a mouse gesture");
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
            frame();
        }
    }
    ImGui::DestroyContext();
}
int main(int argc, char** argv) {
    try {
        test_sdl_input_backend();
        require(argc == 3, "Expected runtime and fault worker paths");
        ImGui::CreateContext();
        forge::ui::style(1.0f);
        const auto padding = ImGui::GetStyle().FramePadding.x;
        for (int i = 0; i < 20; ++i) {
            forge::ui::style(2.0f);
            forge::ui::style(1.0f);
        }
        require(ImGui::GetStyle().FramePadding.x == padding, "Scale accumulated rounding drift");
        forge::ui::style(std::numeric_limits<float>::infinity());
        require(forge::ui::interface_scale == 1, "Nonfinite preference not reset");
        forge::ui::style(10);
        require(forge::ui::interface_scale == 2, "Scale maximum failed");
        forge::ui::style(-1);
        require(forge::ui::interface_scale == 0.65f, "Scale minimum failed");
        ImGui::DestroyContext();
        require(forge::ui::local_file_url(std::filesystem::current_path() / "space #%.html")
                        .find("space%20%23%25.html") != std::string::npos,
                "Manual URL did not escape path characters");
        test_documents();
        test_prefab_editor_documents();
        for (float scale : {0.65f, 1.0f, 2.0f})
            test_automation_workspace(scale);
        test_authoring();
        test_blockout();
        test_scene_cache();
        test_world_grid_axes();
        for (float scale : {0.65f, 1.0f, 2.0f})
            test_command_workspace(scale);
        for (float scale : {0.65f, 1.0f, 2.0f})
            for (const char* component : {"forge.rotation", "forge.position", "forge.scale"})
                test_property_drag(scale, component);
        for (float scale : {0.65f, 1.0f, 2.0f})
            test_authoring_input(scale);
        test_workspace_startup();
        test_transforms();
        for (float scale : {0.65f, 1.0f, 2.0f})
            test_interaction_input(scale);
        test_camera_input();
        test_telemetry_and_status();
        test_tooltip_placement();
        forge::EngineContext authored_engine;
        forge::Scene authored(authored_engine.world());
        auto original = authored.document();
        original["entities"].push_back(
            {{"id", "66666666-6666-4666-8666-666666666666"},
             {"name", "Test"},
             {"components", {{"forge.local_translation", {{"x", 0}, {"y", 1}, {"z", 0}}}}}});
        authored.replace(original);
        {
            forge::EditorCamera camera;
            const auto before = original;
            require(camera.frame(authored.effective_document(),
                                 "66666666-6666-4666-8666-666666666666", 0.4f),
                    "Frame selected failed");
            const auto portrait_distance = camera.distance;
            require(camera.frame(authored.effective_document(),
                                 "66666666-6666-4666-8666-666666666666", 2.0f),
                    "Landscape frame failed");
            require(portrait_distance > camera.distance, "Framing ignored narrow aspect");
            camera.orbit(130, 70);
            require(camera.frame(authored.effective_document(), "", 0.4f), "Fit scene failed");
            const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
                       forward = camera.forward();
            for (float x : {-0.5f, 0.5f})
                for (float y : {0.5f, 1.5f})
                    for (float z : {-0.5f, 0.5f}) {
                        const forge::EditorCamera::Vec offset{x - eye[0], y - eye[1], z - eye[2]};
                        auto dot = [&](auto axis) {
                            return offset[0] * axis[0] + offset[1] * axis[1] + offset[2] * axis[2];
                        };
                        const auto depth = dot(forward);
                        require(depth > camera.near_plane, "Framed cube crosses near plane");
                        require(std::abs(dot(right) * camera.focal / (depth * 0.4f)) < 1 &&
                                    std::abs(dot(up) * camera.focal / depth) < 1,
                                "Framed cube is clipped");
                    }
            const auto target = camera.target;
            require(!camera.frame(authored.effective_document(), "missing", 1),
                    "Missing frame target accepted");
            require(camera.target == target, "Failed frame moved camera");
            camera.pan(10, 0, 800);
            require(camera.target != target, "Pan did not move target");
            for (int i = 0; i < 100; ++i)
                camera.zoom(20);
            require(camera.distance == 0.25f, "Zoom minimum failed");
            for (int i = 0; i < 100; ++i)
                camera.zoom(-20);
            require(camera.distance == 100000, "Zoom maximum failed");
            camera.orbit(0, 100000);
            require(camera.pitch == -forge::EditorCamera::pole, "Orbit crossed pole");
            camera.zoom(std::numeric_limits<float>::quiet_NaN());
            require(std::isfinite(camera.distance), "Nonfinite wheel damaged camera");
            for (const auto& input : {std::array<float, 2>{0, 1}, {0, -1}, {-1, 0}, {1, 0}}) {
                forge::EditorCamera flying;
                const auto start = flying.eye();
                flying.fly(input[0], input[1], 0.1f);
                require(std::abs(flying.eye()[0] - start[0] - input[0] * 0.5f) < 0.00001f &&
                            std::abs(flying.eye()[2] - start[2] - input[1] * 0.5f) < 0.00001f,
                        "WASD flight axis/sign failed");
            }
            forge::EditorCamera diagonal, single_axis;
            diagonal.fly(1, 1, 0.1f);
            single_axis.fly(0, 1, 0.1f);
            require(std::abs(std::hypot(diagonal.eye()[0], diagonal.eye()[2] + 6) -
                             (single_axis.eye()[2] + 6)) < 0.00001f,
                    "Diagonal flight is faster");
            require(original == before, "Camera mutated scene");
        }
        forge::PlaySession play;
        play.start(argv[1], original);
        const auto deadline = SDL_GetTicks() + 1000;
        while (play.active() && SDL_GetTicks() < deadline) {
            play.pump();
            SDL_Delay(1);
        }
        require(play.active(), "Runtime unexpectedly stopped");
        require(play.status().find("Playing") != std::string::npos,
                ("Play handshake failed: " + play.status() + " | " + play.log()).c_str());
        require(play.snapshot() == original, "Play snapshot round trip failed");
        require(authored.document() == original, "Play changed authoring");
        auto edited = original;
        edited["entities"][0]["components"]["forge.local_translation"]["x"] = 10;
        authored.edit(edited);
        play.pump();
        require(play.snapshot() == original, "Authoring edit leaked into running play scene");
        play.stop();
        require(authored.document() == edited, "Stop discarded authored edits");
        require(!play.active(), "Stop failed");
        play.start("/missing-forge-runtime", original);
        require(!play.active(), "Missing runtime accepted");
        for (const char* mode : {"crash", "malformed", "hang", "stale_session", "stale_id"}) {
            auto request = original;
            request["test_failure"] = mode;
            play.start(argv[2], request);
            const auto timeout = SDL_GetTicks() + 6500;
            while (play.active() && SDL_GetTicks() < timeout) {
                play.pump();
                SDL_Delay(1);
            }
            require(!play.active(), "Faulty runtime was not stopped");
            require(play.log().find("fault-worker diagnostic") != std::string::npos,
                    "Runtime stderr was not captured");
            require(play.status().find("Authored scene is safe") != std::string::npos,
                    "Fault diagnostic missing");
        }
        play.start(argv[1], original);
        require(play.active(), "Restart after fault failed");
        play.stop();
        std::cout << "Editor scale and process tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
