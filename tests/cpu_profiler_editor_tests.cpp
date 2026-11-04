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
    auto parallel = Json::array();
    for (unsigned stage : {0u, 1u, 2u}) {
        Json value{{"category", "ecs.system"},
                   {"name", "Shared query"},
                   {"seconds", .001 * (stage + 1)},
                   {"tick", 1},
                   {"count", 1},
                   {"start_seconds", 1},
                   {"sequence", stage + 1}};
        if (stage)
            value["native_stage"] = stage;
        parallel.push_back(std::move(value));
    }
    const auto parallel_rows = ui::summarize_cpu_profiles(parallel);
    check(parallel_rows.size() == 3 && parallel_rows[0].native_stage == 2 &&
              parallel_rows[1].native_stage == 1 && parallel_rows[2].native_stage == 0 &&
              parallel_rows[0].count == 1,
          "CPU summary merged separate native worker stages into one serial scope");
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
    Json game{{"compiled", true},        {"enabled", false},
              {"native_systems", false}, {"native_systems_available", true},
              {"available", true},       {"world_generation", 9},
              {"samples", Json::array()}};
    bool open = true, ready = true, pending = false, watch = false;
    std::string error;
    Json gpu{{"compiled", true}, {"available", true},    {"enabled", false},
             {"pending", 0},     {"query_capacity", 64}, {"samples", Json::array()}};
    unsigned gpu_sends = 0;
    unsigned sends = 0;
    unsigned native_sends = 0;
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
                if (clear) {
                    game["samples"] = Json::array();
                    game["ecs"] = {{"available", false}};
                }
                return true;
            },
            gpu,
            [&](bool enabled, bool clear) {
                ++gpu_sends;
                gpu["enabled"] = enabled;
                if (clear)
                    services.access().clear_profiles();
                return true;
            },
            [&](bool native) {
                ++native_sends;
                game["native_systems"] = native;
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
    services.access().record_profile_span("editor", "Measured interval", 10, .004, 2);
    frame();
    check(test::ui_targets.contains("profiler:editor:row:TestScope"), "CPU scope row missing");
    std::string clipboard;
    auto& platform = ImGui::GetPlatformIO();
    platform.Platform_ClipboardUserData = &clipboard;
    platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        *static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData) = text;
    };
    click("profiler:editor:timeline");
    frame();
    click("profiler:editor:trace-copy");
    const auto copied = Json::parse(clipboard);
    check(copied.at("otherData").at("measured_spans") == 1 &&
              copied.at("otherData").at("duration_only_records") == 1 &&
              copied.at("otherData").at("captured_at").get<std::string>().size() >= 20,
          "Native trace copy lost measured-only semantics or capture date/time");
    auto scroll = [&](float direction) {
        const auto* window = ImGui::FindWindowByName("Profiler###CpuProfiler");
        check(window != nullptr, "Profiler window missing");
        // Hover the window padding, outside the nested table, as a user would.
        io.AddMousePosEvent(window->InnerRect.Min.x + 1,
                            (window->InnerRect.Min.y + window->InnerRect.Max.y) / 2);
        frame();
        io.AddMouseWheelEvent(0, direction);
        for (int i = 0; i < 3; ++i)
            frame();
    };
    const auto measured_visible = [&] {
        const auto found = test::ui_targets.find("profiler:editor:span:Measured interval");
        return found != test::ui_targets.end() &&
               found->second.minimum.y >= found->second.clip_minimum.y &&
               found->second.maximum.y <= found->second.clip_maximum.y;
    };
    for (int i = 0; i < 4 && !measured_visible(); ++i)
        scroll(-10);
    check(measured_visible() && !test::ui_targets.contains("profiler:editor:span:TestScope"),
          "Timeline invented a duration-only interval or its measured row cannot be reached");
    scroll(20);
    click("profiler:editor:timeline");
    platform.Platform_SetClipboardTextFn = nullptr;
    platform.Platform_ClipboardUserData = nullptr;
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
    click("profiler:game:native-systems");
    check(native_sends == 1 && game.at("native_systems") == true && game.at("enabled") == true &&
              !services.access().profiling_systems_enabled(),
          "Native trace control bypassed its runtime callback or changed Editor collection");
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
              !test::ui_targets.at("profiler:game:clear").enabled &&
              !test::ui_targets.at("profiler:game:native-systems").enabled,
          "Pending runtime controls enabled");
    pending = false;
    click("profiler:game:clear");
    check(sends == 2 && requested_enabled && requested_clear && game.at("samples").empty(),
          "Game clear changed enabled policy");
    const Json native{{"available", true},
                      {"tick", std::uint64_t(12)},
                      {"timing", "native_system_lifetime"},
                      {"truncated", false},
                      {"total_entries", 2},
                      {"entries", Json::array({{{"kind", "system"},
                                                {"id", std::uint64_t(101)},
                                                {"name", "MissingGameplay"},
                                                {"phase", "Gameplay"},
                                                {"cpu_seconds", .25},
                                                {"immediate", true},
                                                {"multi_threaded", false}},
                                               {{"kind", "merge"}}})}};
    game["ecs"] = native;
    click("profiler:game:systems-tab");
    frame();
    check(watch && test::ui_targets.contains("profiler:game:system:MissingGameplay") &&
              test::ui_targets.contains("profiler:game:system:Merge"),
          "Native system/merge table missing or watch not retained");
    click("profiler:game:dependencies-tab");
    check(!test::ui_targets.contains("profiler:game:dependency-nodes"),
          "Legacy snapshot invented ordering target metadata");
    click("profiler:game:plan-tab");
    game["ecs"]["entries"][0]["cpu_seconds"] = "invalid";
    frame();
    check(!test::ui_targets.contains("profiler:game:system:MissingGameplay"),
          "Malformed native timing was displayed");
    // Controlled copied-data layout oracle; manual captures use real runtime data.
    Json declarations = native;
    Json term{{"index", 0u},
              {"id", std::uint64_t(201)},
              {"name", "ProbeValue"},
              {"access", "write"},
              {"operator", "and"},
              {"source", "self"},
              {"source_id", std::uint64_t(0)},
              {"source_name", ""},
              {"data", true},
              {"reads", false},
              {"writes", true},
              {"analysis", "included"}};
    declarations["entries"][0]["access"] = {
        {"total_terms", 1}, {"truncated", false}, {"terms", Json::array({term})}};
    auto reader = declarations["entries"][0];
    reader["id"] = std::uint64_t(102);
    reader["name"] = "MissingReadback";
    reader["access"]["terms"][0]["access"] = "read";
    reader["access"]["terms"][0]["reads"] = true;
    reader["access"]["terms"][0]["writes"] = false;
    declarations["entries"][1] = reader;
    declarations["access_diagnostics"] =
        detail::native_system_access_overlaps(declarations["entries"]);
    detail::validate_native_system_access(declarations["entries"],
                                          &declarations["access_diagnostics"]);
    game["ecs"] = declarations;
    frame();
    click("profiler:game:access-tab");
    frame();
    if (!test::ui_targets.contains("profiler:game:access:MissingGameplay:0")) {
        // At 960x640/200% the parent panel scrolls; reach its data like a user.
        io.AddMousePosEvent(30 * scale, 50 * scale);
        frame();
        io.AddMouseWheelEvent(0, -8);
        frame();
        frame();
    }
    if (!test::ui_targets.contains("profiler:game:access:MissingGameplay:0") ||
        !test::ui_targets.contains("profiler:game:access:MissingReadback:0")) {
        std::string found;
        for (const auto& [key, target] : test::ui_targets)
            if (key.starts_with("profiler:game:"))
                found += key + "@" + std::to_string(target.minimum.y) + ".." +
                         std::to_string(target.maximum.y) + " ";
        auto* window = ImGui::FindWindowByName("Profiler###CpuProfiler");
        if (window)
            found += "window=" + std::to_string(window->Size.x) + "," +
                     std::to_string(window->Size.y) +
                     " scroll=" + std::to_string(window->Scroll.y) + "/" +
                     std::to_string(window->ScrollMax.y) +
                     " cursor=" + std::to_string(window->DC.CursorPos.y) +
                     " inner=" + std::to_string(window->InnerRect.Max.y);
        throw std::runtime_error("Declared access rows missing at scale " + std::to_string(scale) +
                                 " display=" + std::to_string(display.x) + "," +
                                 std::to_string(display.y) + ": " + found);
    }
    click("profiler:game:overlaps-tab");
    frame();
    check(test::ui_targets.contains("profiler:game:overlap:MissingGameplay:MissingReadback"),
          "Positive write/read overlap was absent from native controls");
    declarations["dependencies"] = {{"lookup", "native_depends_on_targets"},
                                    {"node_capacity", std::size_t(512)},
                                    {"link_capacity", std::size_t(512)},
                                    {"target_capacity", 8u},
                                    {"truncated", false},
                                    {"nodes", Json::array({{{"id", std::uint64_t(101)},
                                                            {"name", "MissingGameplay"},
                                                            {"root", true},
                                                            {"phase_tag", false},
                                                            {"truncated", false}},
                                                           {{"id", std::uint64_t(102)},
                                                            {"name", "MissingReadback"},
                                                            {"root", true},
                                                            {"phase_tag", false},
                                                            {"truncated", false}},
                                                           {{"id", std::uint64_t(103)},
                                                            {"name", "MissingPhase"},
                                                            {"root", false},
                                                            {"phase_tag", true},
                                                            {"truncated", false}}})},
                                    {"links", Json::array({{{"source", std::uint64_t(101)},
                                                            {"target", std::uint64_t(102)},
                                                            {"slot", 0u},
                                                            {"pair_owned", true}},
                                                           {{"source", std::uint64_t(102)},
                                                            {"target", std::uint64_t(103)},
                                                            {"slot", 0u},
                                                            {"pair_owned", false}}})}};
    game["ecs"] = declarations;
    click("profiler:game:dependencies-tab");
    frame();
    check(test::ui_targets.contains("profiler:game:dependency:MissingGameplay:0") &&
              test::ui_targets.contains("profiler:game:dependency:MissingReadback:0"),
          "System/phase target rows missing from native controls");
    click("profiler:game:dependency-nodes");
    check(test::ui_targets.contains("profiler:game:dependency-node:MissingPhase"),
          "Referenced phase node missing from native controls");
    click("profiler:game:dependency-nodes");
    game["ecs"]["dependencies"]["links"][0]["target"] = std::uint64_t(9999);
    frame();
    check(!test::ui_targets.contains("profiler:game:dependencies-tab"),
          "Dangling ordering target reached a native table");
    game["ecs"] = declarations;
    game["ecs"]["access_diagnostics"]["pairs"][0]["second"] = 9999;
    frame();
    check(!test::ui_targets.contains("profiler:game:access-tab"),
          "Invalid overlap reached a native table");
    game["ecs"] = native;
    frame();
    io.AddMousePosEvent(30 * scale, 50 * scale);
    frame();
    io.AddMouseWheelEvent(0, 8);
    frame();
    frame();
    click("profiler:game:clear");
    check(!game["ecs"]["available"].get<bool>() && requested_enabled && requested_clear,
          "Native view Clear bypassed existing collection control");
    game["ecs"]["available"] = "invalid";
    frame();
    game["ecs"] = {{"available", false}};
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
    const auto sends_before_close = sends;
    open = false;
    frame();
    check(!watch && sends == sends_before_close, "Closing profiler changed runtime collection");
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
    performance.scene_submission(performance.start, submit2);
    performance.finish(submit2, present2, services.access());
    const auto frame_timeline = cpu_scope_timeline(services.access().profiles());
    check(frame_timeline.spans.size() == 3 && frame_timeline.duration_only == 1,
          "Frame producer fabricated exclusive Update/UI or lost Scene/UI/Present intervals");
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
