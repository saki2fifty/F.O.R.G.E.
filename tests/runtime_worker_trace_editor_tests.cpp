#include "editor/cpu_profiler.hpp"
#include "editor/editor_state.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void exercise(const Json& captured, float scale, bool compact, const std::filesystem::path& output,
              const Json& worker_access) {
    const unsigned width = unsigned((compact ? 600 : 1240) * scale);
    const unsigned height = unsigned(760 * scale);
    test::ImGuiSoftwareCapture capture(width, height);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    ui::style(scale);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    EngineServices editor;
    ui::CpuProfiler profiler;
    Telemetry telemetry;
    bool open = true;
    Json shown = captured;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        if (auto* window = ImGui::FindWindowByName("Profiler###CpuProfiler"))
            ImGui::SetWindowSize(window, {float(width), float(height)}, ImGuiCond_Always);
        (void)profiler.draw(
            open, editor.access(), telemetry, shown, true, false, "",
            [](bool, bool) { return false; }, Json::object(), {}, [](bool) { return false; });
        capture.render();
    };
    test::ImGuiInputDriver input(frame);
    frame();
    frame();
    input.click("profiler:game-tab");
    input.type("profiler:game:filter", "ParallelMotion");
    std::string name;
    for (const auto& row : captured.at("samples"))
        if (row.value("native_stage", 0u) == 1 &&
            row.at("name").get<std::string>().find("ParallelMotion") != std::string::npos)
            name = row.at("name").get<std::string>();
    check(!name.empty(), "Need an actual ParallelMotion runtime capture");
    for (unsigned stage : {1u, 2u, 3u})
        check(test::ui_targets.contains("profiler:game:row:" + name +
                                        ":stage:" + std::to_string(stage)),
              "Summary omitted a measured worker stage");
    input.click("profiler:game:timeline");
    for (unsigned stage : {1u, 2u, 3u})
        check(test::ui_targets.contains("profiler:game:span:" + name +
                                        ":stage:" + std::to_string(stage)),
              "Timeline omitted a measured worker stage");
    if (!compact && scale == 1 && !output.empty()) {
        std::filesystem::create_directories(output);
        capture.save(output / "native-worker-traces.png");
    }
    input.click("profiler:game:systems-tab");
    input.click("profiler:game:access-tab");
    std::string system;
    for (const auto& row : captured.at("ecs").at("entries"))
        if (row.value("name", "").ends_with("ParallelMotionUpdate"))
            system = row.at("name").get<std::string>();
    check(!system.empty() && test::ui_targets.contains("profiler:game:access:" + system + ":1"),
          "Declared access view omitted actual SDK translation access");
    if (!compact && scale == 1 && !output.empty())
        capture.save(output / "native-declared-access.png");
    input.type("profiler:game:system-filter", "local_translation");
    input.click("profiler:game:plan-tab");
    check(test::ui_targets.contains("profiler:game:system:" + system),
          "Component-name filtering omitted the actual SDK system from Plan");
    input.click("profiler:game:dependencies-tab");
    check(test::ui_targets.contains("profiler:game:dependency:" + system + ":0"),
          "Component filtering omitted the actual SDK ordering target");
    input.type("profiler:game:system-filter", "ParallelMotion");
    if (!compact && scale == 1 && !output.empty())
        capture.save(output / "native-system-dependencies.png");
    input.click("profiler:game:dependency-nodes");
    check(test::ui_targets.contains("profiler:game:dependency-node:" + system),
          "Node view omitted the actual SDK system");
    input.click("profiler:game:dependency-nodes");
    shown["ecs"]["dependencies"]["links"][0]["target"] = std::uint64_t(0);
    frame();
    check(!test::ui_targets.contains("profiler:game:dependencies-tab"),
          "Malformed ordering target reached a native table");
    shown = captured;
    frame();
    input.type("profiler:game:system-filter", "ParallelMotion");
    input.click("profiler:game:overlaps-tab");
    for (const auto& pair : captured.at("ecs").at("access_diagnostics").at("pairs")) {
        const auto& entries = captured.at("ecs").at("entries");
        const auto a =
            entries.at(pair.at("first").get<std::size_t>()).at("name").get<std::string>();
        const auto b =
            entries.at(pair.at("second").get<std::size_t>()).at("name").get<std::string>();
        if (a.find("ParallelMotion") != std::string::npos ||
            b.find("ParallelMotion") != std::string::npos)
            check(test::ui_targets.contains("profiler:game:overlap:" + a + ":" + b),
                  "Overlap view omitted a matching actual native declaration pair");
    }
    shown["ecs"]["access_diagnostics"]["copied_terms"] = 513;
    frame();
    check(!test::ui_targets.contains("profiler:game:access-tab"),
          "Malformed native access metadata reached an unfinished native table");
    if (!worker_access.is_null()) {
        shown = {{"compiled", true},
                 {"enabled", true},
                 {"available", true},
                 {"ecs", worker_access},
                 {"samples", Json::array()}};
        frame();
        input.type("profiler:game:system-filter", "WorkerSharedDeclaration");
        input.click("profiler:game:worker-writes-tab");
        std::string writer;
        for (const auto& write : worker_access.at("worker_access").at("writes")) {
            const auto& row = worker_access.at("entries").at(write.at("system").get<std::size_t>());
            writer = row.at("name").get<std::string>();
            check(test::ui_targets.contains("profiler:game:worker-write:" + writer + ":1"),
                  "Actual native fixed-source write was omitted from controls");
        }
        check(!writer.empty(), "Actual native worker capture has no declared shared write");
        if (!compact && scale == 1 && !output.empty())
            capture.save(output / "native-worker-writes.png");
        shown["ecs"]["worker_access"]["writes"][0]["term"] = 0;
        frame();
        check(!test::ui_targets.contains("profiler:game:worker-writes-tab"),
              "Redirected worker write reference reached a native table");
    }
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 2 && argc <= 4,
              "Need actual runtime profile, optional capture directory and worker access profile");
        const auto source = std::filesystem::u8path(argv[1]);
        check(std::filesystem::file_size(source) <= 2 * 1024 * 1024,
              "Profile input exceeds fixture bound");
        std::ifstream input(source);
        const auto profile = Json::parse(input);
        const auto timeline = cpu_scope_timeline(profile.at("samples"));
        check(timeline.native_workers && timeline.measured_threads == 4,
              "Capture does not contain all four actual native stages");
        Json worker_access;
        if (argc == 4) {
            const auto path = std::filesystem::u8path(argv[3]);
            check(std::filesystem::file_size(path) <= 2 * 1024 * 1024,
                  "Worker access capture exceeds fixture bound");
            std::ifstream stream(path);
            worker_access = Json::parse(stream);
        }
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(profile, scale, compact, argc >= 3 ? argv[2] : "", worker_access);
        std::cout << "Production worker/access/dependency views from actual runtime observations "
                     "passed six layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
