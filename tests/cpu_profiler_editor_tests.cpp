#include "editor/cpu_profiler.hpp"
#include "editor/performance.hpp"
#include "editor/workspace.hpp"
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
void summary_oracles() {
    const Json samples = Json::array({{{"category", "runtime"},
                                       {"name", "Physics"},
                                       {"seconds", .002},
                                       {"tick", 1},
                                       {"count", 1}},
                                      {{"category", "runtime"},
                                       {"name", "Physics"},
                                       {"seconds", .006},
                                       {"tick", 2},
                                       {"count", 1}},
                                      {{"category", "editor"},
                                       {"name", "Present"},
                                       {"seconds", .01},
                                       {"tick", 0},
                                       {"count", 1}}});
    const auto rows = ui::summarize_cpu_profiles(samples);
    check(rows.size() == 2 && rows[0].name == "Present" && rows[1].count == 2 &&
              std::abs(rows[1].total / rows[1].count - .004) < 1e-12 && rows[1].maximum == .006 &&
              rows[1].latest == .006 && rows[1].tick == 2,
          "Independent profile aggregate oracle failed");
    auto gpu = samples;
    for (auto& sample : gpu) {
        sample["clock"] = "gpu";
        sample["frame"] = sample.at("tick").get<unsigned>() + 10;
        sample.erase("tick");
    }
    const auto gpu_rows = ui::summarize_cpu_profiles(gpu, true);
    check(gpu_rows.size() == 2 && gpu_rows[1].count == 2 && gpu_rows[1].total == .008 &&
              gpu_rows[1].maximum == .006 && gpu_rows[1].latest == .006 && gpu_rows[1].tick == 12,
          "GPU duration aggregation or latest frame oracle failed");
    for (bool gpu_clock : {false, true}) {
        bool rejected = false;
        try {
            ui::summarize_cpu_profiles(gpu_clock ? samples : gpu, gpu_clock);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Profiler admitted samples from the wrong clock");
    }
    for (auto invalid : {Json(), Json::array({Json::object()}), Json::array()}) {
        if (invalid.is_array() && invalid.empty())
            for (int i = 0; i < 513; ++i)
                invalid.push_back(samples[0]);
        bool rejected = false;
        try {
            ui::summarize_cpu_profiles(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid/oversized profile snapshot admitted");
    }
}
void interactions(float scale, ImVec2 display) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    EngineServices services;
    ui::CpuProfiler profiler;
    Telemetry telemetry;
    telemetry.frames.fps = 120;
    telemetry.frames.milliseconds = 1000. / 120;
    Json game{{"compiled", true},
              {"enabled", false},
              {"available", true},
              {"world_generation", 9},
              {"samples", Json::array()}};
    bool open = true, ready = true, pending = false, watch = false;
    std::string error;
    Json gpu{{"compiled", true}, {"available", true},    {"enabled", false},
             {"pending", 0},     {"query_capacity", 64}, {"samples", Json::array()}};
    unsigned gpu_sends = 0;
    unsigned sends = 0;
    bool requested_enabled = false, requested_clear = false;
    ui::Workspace prefs;
    bool show_menu = false;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        if (show_menu && ImGui::BeginMainMenuBar()) {
            prefs.menu();
            ImGui::EndMainMenuBar();
        }
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            {std::min(1000 * scale, display.x - 20), std::min(700 * scale, display.y - 20)},
            ImGuiCond_Always);
        gpu["samples"] = services.access().gpu_profiles();
        watch = profiler.draw(
            open, services.access(), telemetry, game, ready, pending, error,
            [&](bool enabled, bool clear) {
                ++sends;
                requested_enabled = enabled;
                requested_clear = clear;
                game["enabled"] = enabled;
                if (clear)
                    game["samples"] = Json::array();
                return true;
            },
            gpu,
            [&](bool enabled, bool clear) {
                ++gpu_sends;
                gpu["enabled"] = enabled;
                if (clear)
                    services.access().clear_profiles();
                return true;
            });
        ImGui::Render();
    };
    auto click = [&](const char* key) {
        frame();
        const auto target = test::ui_targets.at(key);
        const ImVec2 center{(target.minimum.x + target.maximum.x) / 2,
                            (target.minimum.y + target.maximum.y) / 2};
        // Main-menu item bounds include the menu's one-pixel outer border.
        const bool menu_bar_item = std::string(key) == "workspace:window-menu";
        const bool clipped =
            menu_bar_item ? center.x < target.clip_minimum.x || center.x > target.clip_maximum.x ||
                                center.y < target.clip_minimum.y || center.y > target.clip_maximum.y
                          : target.minimum.x < target.clip_minimum.x ||
                                target.maximum.x > target.clip_maximum.x ||
                                target.minimum.y < target.clip_minimum.y ||
                                target.maximum.y > target.clip_maximum.y;
        if (!target.enabled || clipped)
            throw std::runtime_error(
                std::string(key) + " clipped or disabled at scale " + std::to_string(scale) +
                " item " + std::to_string(target.minimum.x) + "," +
                std::to_string(target.minimum.y) + ".." + std::to_string(target.maximum.x) + "," +
                std::to_string(target.maximum.y) + " clip " +
                std::to_string(target.clip_minimum.x) + "," +
                std::to_string(target.clip_minimum.y) + ".." +
                std::to_string(target.clip_maximum.x) + "," +
                std::to_string(target.clip_maximum.y));
        io.AddMousePosEvent(center.x, center.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    for (int i = 0; i < 4; ++i)
        frame();
    check(!watch, "Editor tab requested runtime records");
    click("profiler:editor:collection");
    check(services.access().profiling_enabled(), "Editor collection click failed");
    services.access().record_profile("editor", "TestScope", .002, 1);
    frame();
    check(test::ui_targets.contains("profiler:editor:row:TestScope"), "CPU scope row missing");
    click("profiler:editor:clear");
    check(services.access().profiles().empty() && services.access().profiling_enabled(),
          "Clear stopped collection or retained records");
    click("profiler:editor:collection");
    check(!services.access().profiling_enabled(), "Editor collection stop failed");
    click("profiler:game-tab");
    frame();
    check(watch, "Game tab did not request runtime observations");
    click("profiler:game:collection");
    check(sends == 1 && requested_enabled && !requested_clear, "Game start sent incorrect control");
    game["samples"] = Json::array({{{"category", "runtime"},
                                    {"name", "Physics"},
                                    {"seconds", .001},
                                    {"tick", 12},
                                    {"count", 1}}});
    frame();
    check(test::ui_targets.contains("profiler:game:row:Physics"), "Game scope not shown");
    click("profiler:game:filter");
    io.AddInputCharactersUTF8("Missing");
    frame();
    check(!test::ui_targets.contains("profiler:game:row:Physics"),
          "Actual filter input did not filter rows");
    pending = true;
    frame();
    check(!test::ui_targets.at("profiler:game:collection").enabled &&
              !test::ui_targets.at("profiler:game:clear").enabled,
          "Pending runtime controls enabled");
    pending = false;
    click("profiler:game:clear");
    check(sends == 2 && requested_enabled && requested_clear && game.at("samples").empty(),
          "Game clear changed enabled policy");
    error = "Stale runtime generation";
    frame();
    ready = false;
    frame();
    check(!test::ui_targets.contains("profiler:game:collection"),
          "Unavailable runtime exposed controls");
    ready = true;
    game["compiled"] = false;
    frame();
    check(!test::ui_targets.contains("profiler:game:collection"),
          "Compile-out runtime exposed controls");
    click("profiler:gpu-tab");
    frame();
    check(!watch, "GPU tab requested unrelated runtime CPU transport");
    click("profiler:gpu:collection");
    check(gpu_sends == 1 && gpu.at("enabled") == true && !services.access().profiling_enabled(),
          "GPU start changed CPU collection policy");
    services.access().record_gpu_profile(services.access().profile_epoch(), "Scene", "Missing",
                                         .004, 42);
    frame();
    check(test::ui_targets.contains("profiler:gpu:row:Missing"), "GPU duration row missing");
    gpu["pending"] = 64;
    frame();
    check(test::ui_targets.at("profiler:gpu:collection").enabled,
          "In-flight GPU queries disabled Stop");
    click("profiler:gpu:collection");
    check(gpu.at("enabled") == false && services.access().gpu_profiles().size() == 1,
          "GPU Stop erased completed samples");
    gpu["available"] = false;
    gpu["reason"] = "GPU timing failed; rendering remains available.";
    frame();
    check(!test::ui_targets.at("profiler:gpu:collection").enabled &&
              test::ui_targets.at("profiler:gpu:clear").enabled,
          "Unavailable timing allowed Start or prevented clearing history");
    click("profiler:gpu:clear");
    check(services.access().gpu_profiles().empty(), "GPU Clear retained completed records");
    gpu["compiled"] = false;
    frame();
    check(!test::ui_targets.contains("profiler:gpu:collection"),
          "Compile-out GPU controls enabled");
    open = false;
    frame();
    check(!watch && sends == 2, "Closing profiler changed runtime collection");
    check(!prefs.cpu_profiler, "Profiler appeared in default layout");
    show_menu = true;
    for (int i = 0; i < 4; ++i)
        frame();
    click("workspace:window-menu");
    click("workspace:panel:Profiler");
    check(prefs.cpu_profiler, "Window menu did not open profiler");
    ui::Workspace restored;
    restored.load({{"panels", prefs.settings()}});
    check(restored.cpu_profiler, "Profiler visibility not persisted");
    click("workspace:window-menu");
    click("workspace:panel:Profiler");
    check(!prefs.cpu_profiler, "Window menu did not hide profiler");
    ui::Performance performance;
    performance.begin();
    const auto submit = ui::Performance::Clock::now();
    const auto present = submit;
    performance.finish(submit, present, services.access());
    check(services.access().profiles().empty(), "Disabled existing frame timing collected records");
    services.access().profiling(true);
    performance.begin();
    const auto submit2 = ui::Performance::Clock::now();
    const auto present2 = ui::Performance::Clock::now();
    performance.finish(submit2, present2, services.access());
    check(services.access().profiles().size() == 4,
          "Existing frame sections not recorded exactly once");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        summary_oracles();
        for (float scale : {1.f, 1.5f, 2.f})
            for (const auto display : {ImVec2{2400, 1700}, ImVec2{960, 640}})
                interactions(scale, display);
        std::cout << "CPU profiler real-input/aggregation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
