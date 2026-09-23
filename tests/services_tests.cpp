#include <forge/assets.hpp>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <forge/schema.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> static void reject(F&& f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Expected rejection");
}
static Json action(ActionId id, const char* kind, Json bindings) {
    return {{"id", id}, {"name", "Test"}, {"kind", kind}, {"bindings", bindings}};
}
static void input_tests() {
    const auto button = ActionId::generate(), move = ActionId::generate(),
               look = ActionId::generate();
    InputMap map(
        {{"version", 1},
         {"actions",
          Json::array(
              {action(button, "digital",
                      Json::array({{{"control", "key.space"}}, {{"control", "pad.south"}}})),
               action(move, "axis2",
                      Json::array({{{"control", "key.w"}, {"y", 1}, {"x", 0}},
                                   {{"control", "pad.left_x"}, {"deadzone", .2}}})),
               action(look, "axis1", Json::array({{{"control", "mouse.delta_x"}}}))})}});
    RuntimeInput input;
    input.configure(map);
    input.submit({{"key.space", 1}, {"key.w", 1}, {"mouse.delta_x", 4}});
    auto first = input.latch(1);
    check(first.actions.at(button).held && first.actions.at(button).pressed &&
              !first.actions.at(button).released,
          "Digital press missing");
    check(first.actions.at(move).y == 1 && first.actions.at(look).x == 4, "Analog mapping missing");
    auto second = input.latch(2);
    check(second.actions.at(button).held && !second.actions.at(button).pressed &&
              second.actions.at(look).x == 0,
          "Catch-up repeated edge or delta");
    check(first.actions.at(button).pressed, "Prior snapshot mutated");
    input.submit({{"key.space", 0}});
    check(input.latch(3).actions.at(button).released && !input.latch(4).actions.at(button).released,
          "Release repeated");
    input.submit({{"key.space", 1}, {"key.space", 0}});
    auto tap = input.latch(5).actions.at(button);
    check(tap.pressed && tap.released && !tap.held, "Quick tap lost");
    input.submit({{"pad.south", 1}, {"key.w", 0}, {"pad.left_x", .6}});
    check(std::abs(input.latch(6).actions.at(move).x - .5) < 1e-9, "Gamepad deadzone incorrect");
    input.release_all();
    auto released = input.latch(7);
    check(released.actions.at(button).released && !released.actions.at(button).pressed &&
              released.actions.at(move).x == 0,
          "Disconnect did not neutralize held state");
    input.submit({{"key.space", 1}, {"mouse.delta_x", 5}});
    input.release_all();
    check(!input.latch(8).actions.at(button).pressed && input.snapshot().actions.at(look).x == 0,
          "Focus loss replayed pending press/delta");
    reject([&] { input.submit({{"key.space", 1}, {"unknown", 0}}); });
    check(!input.latch(9).actions.at(button).held, "Rejected input batch partially applied");
    auto invalid = map.source();
    invalid["actions"][0]["bindings"][0]["control"] = "mouse.delta_x";
    reject([&] { (void)InputMap(invalid); });
    const auto t = RuntimeClock::Time{};
    RuntimeClock clock;
    input.submit({{"key.space", 1}});
    unsigned ticks = 0, presses = 0;
    auto tick = [&](float) {
        ++ticks;
        presses += input.latch(ticks).actions.at(button).pressed ? 1 : 0;
    };
    clock.advance(t, tick);
    check(ticks == 0, "Pause advanced input");
    clock.step(tick);
    check(ticks == 1 && presses == 1, "Paused Step failed input");
    clock.resume(t);
    clock.advance(t + std::chrono::milliseconds(100), tick);
    check(ticks == 7 && presses == 1, "Catch-up repeated digital edge");
    clock.pause(t + std::chrono::milliseconds(100));
    input.submit({{"key.space", 0}});
    clock.step(tick);
    check(input.snapshot().actions.at(button).released, "Paused release failed");
    Module module;
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    RuntimeSimulation sim(engine.world(), scene, module);
    sim.input().configure(map);
    sim.input().submit({{"key.space", 1}});
    sim.tick(1.0f / 60);
    sim.tick(1.0f / 60);
    check(sim.input_status()["actions"][0]["presses"] == 1,
          "Fixed-pipeline monitor did not consume input");
    check(!scene.document().contains("input"), "Transient input entered scene persistence");
}
static void project_tests(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Scenes");
    const auto scene = empty_scene();
    atomic_write(root / "Scenes/main.scene.json", scene.dump());
    Json legacy{{"version", 1},
                {"name", "Test"},
                {"startup_scene", "Scenes/main.scene.json"},
                {"plugin", {{"opaque", 4}}}};
    atomic_write(root / "forge.project.json", legacy.dump());
    ProjectSettings project(root);
    check(project.simulation_hz() == 60 && project.document()["plugin"] == legacy["plugin"],
          "Project migration/defaults lost unknown fields");
    check(project.startup() == root / "Scenes/main.scene.json",
          "Startup identity resolution failed");
    auto changed = project.document();
    check(project.physics().gravity == Double3{0, -9.81, 0}, "Legacy project gravity default");
    changed["physics"] = {{"version", 1}, {"gravity", {0, -3, 0}}, {"plugin", {{"opaque", true}}}};
    auto invalid_physics = changed;
    invalid_physics["physics"]["gravity"] = {0, 0};
    reject([&] { ProjectSettings::validate(invalid_physics); });
    invalid_physics = changed;
    invalid_physics["physics"]["gravity"] = {0, 1e9, 0};
    reject([&] { ProjectSettings::validate(invalid_physics); });
    changed["simulation_hz"] = 120;
    const auto baseline = project.document();
    project.save(changed, &baseline);
    reject([&] { project.save(changed, &baseline); });
    check(ProjectSettings(root).simulation_hz() == 120 &&
              std::filesystem::exists(root / "forge.project.json.v1.backup"),
          "Settings persistence/backup missing");
    check(ProjectSettings(root).physics().gravity == Double3{0, -3, 0} &&
              ProjectSettings(root).document().at("physics").at("plugin").at("opaque") == true,
          "Gravity/unknown physics settings roundtrip");
    const auto pending = root / "forge.project.json.pending";
    std::filesystem::create_directory(pending);
    reject([&] { project.save(project.document()); });
    std::filesystem::remove(pending);
    check(project.simulation_hz() == 120, "Failed atomic save changed loaded settings");
    auto missing = project.document();
    missing["startup_scene"]["asset"] = AssetId::generate();
    reject([&] { project.save(missing); });
    changed["simulation_hz"] = 0;
    reject([&] { project.save(changed); });
    check(project.simulation_hz() == 120, "Invalid setting changed live data");
    std::filesystem::rename(root / "Scenes/main.scene.json", root / "Scenes/Moved.scene.json");
    check(ProjectSettings(root).startup() == root / "Scenes/Moved.scene.json",
          "Startup follows filename instead of AssetId");
    ProjectPaths paths(root);
    check(paths.resolve("Scenes\\Moved.scene.json") == paths.resolve("Scenes/./Moved.scene.json"),
          "Cross-platform locator normalization failed");
    reject([&] { paths.resolve("../outside.json"); });
    reject([&] { paths.resolve("C:\\outside.json"); });
    reject([&] { paths.resolve("\\\\server\\outside.json"); });
    reject([&] { paths.resolve("/outside.json"); });
    reject([&] { paths.resolve("Assets/NUL.json"); });
#ifdef _WIN32
    check(paths.same_locator("Scenes/Moved.scene.json", "scenes/moved.scene.json"),
          "Windows locator case semantics wrong");
#else
    check(!paths.same_locator("Scenes/Moved.scene.json", "scenes/moved.scene.json"),
          "Linux locators incorrectly case-folded");
#endif
    AssetCatalog catalog(root);
    const auto record = catalog.add_scene("Scenes\\Moved.scene.json");
    check(record.id == scene.at("asset_id").get<AssetId>(), "Locator changed identity");
    reject(
        [&] { catalog.add({AssetId::generate(), "scene", "Scenes/./Moved.scene.json", 3, {}}); });
    atomic_write(root / "Scenes/IllegalCopy.scene.json", scene.dump());
    reject([&] { (void)project.startup(); });
    std::filesystem::remove(root / "Scenes/IllegalCopy.scene.json");
    auto external = project.document();
    external["other"] = 2;
    atomic_write(root / "forge.project.json", external.dump());
    reject([&] { project.save(project.document()); });
    external["version"] = 99;
    atomic_write(root / "forge.project.json", external.dump());
    reject([&] { ProjectSettings unsupported(root); });
    auto registry = core_document_schemas();
    check(registry.prepare("scene", scene) == scene, "Schema registry altered existing scene");
    check(registry.describe().size() == 5, "Schema discovery missing current formats");
    reject([&] { registry.prepare("project", external); });
    reject([&] {
        registry.add({"scene", 1, {1}, "", [](const Json&) {}, [](const Json& j) { return j; }});
    });
}
static void service_tests() {
    ServiceAccess expired;
    double now = 1;
    {
        EngineServices owner(true, [&] { return now; });
        auto access = owner.access();
        expired = access;
        access.require(Capability::Diagnostics);
        check(!access.available(static_cast<Capability>(3)) &&
                  !access.available(static_cast<Capability>(128)),
              "Invalid service capability accepted");
        check(!access.restricted(1).available(Capability::Profiling), "Optional capability leaked");
        reject([&] { access.restricted(0).require(Capability::Diagnostics); });
        Diagnostic diagnostic{Severity::Error, "assets", "Missing asset", {}};
        diagnostic.context.entity = EntityId::generate();
        diagnostic.context.asset = AssetId::generate();
        access.emit(diagnostic);
        const auto record = access.diagnostics().back();
        check(record["severity"] == "error" && record["context"].contains("entity") &&
                  record["context"].contains("asset"),
              "Diagnostic context lost");
        reject([&] { fail_invariant(access, "test invariant"); });
        check(access.diagnostics().back()["severity"] == "fatal",
              "Invariant lacked fatal diagnostic");
        bool wrong_thread = false, foreign_available = true;
        std::thread worker([&] {
            foreign_available = access.available(Capability::Diagnostics);
            try {
                access.emit(diagnostic);
            } catch (...) {
                wrong_thread = true;
            }
        });
        worker.join();
        check(!foreign_available, "Foreign-thread availability leaked");
        check(wrong_thread, "Worker accessed owner-thread service");
        {
            auto outer = access.profile("test", "outer", 3);
            now = 2;
            {
                auto inner = access.profile("test", "inner", 3);
                now = 3;
            }
            now = 4;
        }
#ifndef FORGE_DISABLE_PROFILING
        check(access.profiles().size() == 2 && access.profiles().back()["seconds"] == 3,
              "Nested profiling wrong");
        for (int i = 0; i < 600; ++i) {
            auto scope = access.profile("test", "bounded");
        }
        check(access.profiles().size() == 512, "Profile storage unbounded");
#endif
        const auto count = access.profiles().size();
        access.profiling(false);
        {
            auto disabled = access.profile("test", "disabled");
        }
        check(access.profiles().size() == count, "Disabled profiling recorded work");
        for (int i = 0; i < 300; ++i)
            access.emit(diagnostic);
        check(access.diagnostics().size() == 256, "Diagnostics unbounded");
        const auto records = access.diagnostics();
        for (std::size_t i = 1; i < records.size(); ++i)
            check(records[i].at("sequence").get<std::uint64_t>() ==
                      records[i - 1].at("sequence").get<std::uint64_t>() + 1,
                  "Diagnostic ring lost sequence ordering");
    }
    check(!expired.available(Capability::Diagnostics), "Service outlived EngineContext owner");
    reject([&] { expired.emit({Severity::Info, "test", "expired", {}}); });
    const std::vector<ModuleRequirement> modules{{"runtime", {"input", "transforms"}, 1},
                                                 {"input", {"core"}, 0},
                                                 {"transforms", {"core"}, 0},
                                                 {"core", {}, 0}};
    const auto order = module_order(modules, 1);
    check(order.front() == "core" && order.back() == "runtime", "Module order wrong");
    reject([&] { module_order(modules, 0); });
    reject([&] { module_order({{"a", {"b"}, 0}}, 0); });
    reject([&] { module_order({{"a", {"b"}, 0}, {"b", {"a"}, 0}}, 0); });
    EngineContext headless(WorldRole::Validation);
    check(headless.services().available(Capability::Diagnostics), "Headless services absent");
}
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Test directory required");
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        input_tests();
        project_tests(root);
        service_tests();
        std::cout << "Core services/input/settings/path/schema tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
