#include <forge/assets.hpp>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <forge/schema.hpp>
#include <fstream>
#include <iostream>
#include <limits>
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
static void input_context_tests() {
    const auto game = ActionId::generate(), menu = ActionId::generate(),
               pass = ActionId::generate();
    auto game_action = action(game, "digital", Json::array({{{"control", "key.space"}}}));
    auto menu_action = action(menu, "digital", Json::array({{{"control", "key.space"}}}));
    auto pass_action = action(pass, "digital", Json::array({{{"control", "key.space"}}}));
    game_action["context"] = "game";
    menu_action["context"] = "menu";
    pass_action["context"] = "overlay";
    Json source = {
        {"version", 2},
        {"contexts", Json::array({{{"name", "game"}, {"active", true}},
                                  {{"name", "menu"}, {"priority", 10}},
                                  {{"name", "overlay"}, {"priority", 20}, {"consume", false}}})},
        {"actions", Json::array({game_action, menu_action, pass_action})}};
    RuntimeInput input;
    input.configure(InputMap(source));
    input.submit({{"key.space", 1}});
    check(input.latch(1).actions.at(game).pressed, "Default game context inactive");
    input.activate_contexts({"game", "menu", "overlay"});
    const auto neutral = input.latch(2);
    check(neutral.actions.at(game).released && !neutral.actions.at(menu).held,
          "Context switch leaked held input");
    input.submit({{"key.space", 1}});
    auto routed = input.latch(3);
    check(!routed.actions.at(game).held && routed.actions.at(menu).pressed &&
              routed.actions.at(pass).pressed,
          "Context priority/consumption/pass-through failed");
    input.activate_contexts({"overlay", "menu", "game"});
    check(input.latch(4).actions.at(menu).held, "Equivalent stack reset input");
    reject([&] { input.activate_contexts({"game", "unknown"}); });
    reject([&] { input.activate_contexts({"game", "game"}); });
    check(input.context_active("menu") && input.latch(5).actions.at(menu).held,
          "Rejected context change modified live state");
    input.activate_contexts({});
    input.submit({{"key.space", 1}});
    check(!input.latch(6).actions.at(game).held, "Inactive context received input");
    source["contexts"][1]["priority"] = 0;
    input.configure(InputMap(source));
    input.activate_contexts({"menu", "game"});
    input.submit({{"key.space", 1}});
    routed = input.latch(7);
    check(routed.actions.at(game).held && !routed.actions.at(menu).held,
          "Equal priority must follow declaration order, not activation order");
    auto invalid = source;
    invalid["actions"][0]["context"] = "missing";
    reject([&] { (void)InputMap(invalid); });
    invalid = source;
    invalid["contexts"][0]["priority"] = 1.5;
    reject([&] { (void)InputMap(invalid); });
    invalid = source;
    invalid["contexts"][1]["name"] = "game";
    reject([&] { (void)InputMap(invalid); });
    const InputMap map(source);
    const auto conflicts = map.binding_conflicts(menu, "key.space");
    check(conflicts.size() == 3 && !conflicts[0].same_context && conflicts[1].same_context,
          "Conflict query lost context ownership");
    const auto cleared = map.with_bindings(game, Json::array());
    check(cleared.actions()[0].bindings.empty() && !map.actions()[0].bindings.empty(),
          "Binding candidate mutated original map");
    reject([&] {
        (void)map.with_bindings(game,
                                Json::array({{{"control", "key.w"}}, {{"control", "key.w"}}}));
    });
    reject([&] { (void)map.with_bindings(ActionId::generate(), Json::array()); });
    input.begin_rebind(); // space was held when the dialog opened
    input.submit({{"key.space", 1}, {"mouse.delta_x", 40}, {"pad.left_x", .1}});
    check(input.rebinding() && !input.take_rebind(), "Rebind captured opening button or noise");
    input.submit({{"key.space", 0}, {"pad.south", 1}, {"key.space", 1}});
    check(!input.rebinding() && input.take_rebind()->control == "pad.south",
          "Gamepad button capture failed");
    check(!input.latch(8).actions.at(game).held && !input.take_rebind(),
          "Captured batch leaked into gameplay or repeated result");
    input.begin_rebind();
    input.submit({{"key.escape", 1}, {"key.w", 1}});
    check(!input.rebinding() && !input.take_rebind(), "Escape did not cancel listening");
    input.begin_rebind();
    reject([&] { input.submit({{"key.w", 1}, {"unknown", 0}}); });
    check(input.rebinding() && !input.take_rebind(), "Invalid batch partially captured");
    input.submit({{"", 0, true}});
    check(!input.rebinding(), "Focus/reset did not cancel listening");
    source["contexts"][1]["phase"] = "control";
    source["contexts"][1]["consume"] = false;
    source["contexts"][1]["priority"] = 10;
    input.configure(InputMap(source));
    input.activate_contexts({"menu", "game"});
    input.submit({{"key.space", 1}});
    const auto controls = input.latch_controls(1);
    check(controls.actions.size() == 1 && controls.actions.at(menu).pressed &&
              input.snapshot().actions.empty(),
          "Control frame mutated fixed snapshot");
    check(!input.latch_controls(2).actions.at(menu).pressed &&
              input.latch(1).actions.at(game).pressed && !input.snapshot().actions.contains(menu),
          "Control frame consumed gameplay edge or entered fixed snapshot");
    source["actions"][0]["kind"] = "axis1";
    source["actions"][1]["kind"] = "axis1";
    source["actions"][0]["bindings"] = Json::array({{{"control", "mouse.delta_x"}}});
    source["actions"][1]["bindings"] = source["actions"][0]["bindings"];
    input.configure(InputMap(source));
    input.activate_contexts({"menu", "game"});
    input.submit({{"mouse.delta_x", 5}});
    check(input.latch_controls(1).actions.at(menu).x == 5 &&
              input.latch_controls(2).actions.at(menu).x == 0 &&
              input.latch(1).actions.at(game).x == 5 && input.latch(2).actions.at(game).x == 0,
          "Pass-through relative input lost its domain consumption cursor");
}
static void input_tests() {
    input_context_tests();
    check(input_stick(0, 0, 0).x == 0 && input_stick(.1, .1, .2).y == 0,
          "Stick center/dead zone invalid");
    check(std::abs(input_stick(.6, 0, .2).x - .5) < 1e-9,
          "Radial dead zone did not remap magnitude");
    {
        const auto id = ActionId::generate();
        RuntimeInput radial;
        auto source =
            Json{{"version", 1},
                 {"actions", Json::array({{{"id", id},
                                           {"name", "Single stick axis"},
                                           {"kind", "axis1"},
                                           {"bindings", Json::array({{{"control", "pad.left_x"},
                                                                      {"radial", true},
                                                                      {"deadzone", .2}}})}}})}};
        radial.configure(InputMap(source));
        radial.submit({{"pad.left_x", .1}, {"pad.left_y", 1}});
        check(std::abs(radial.latch(1).actions.at(id).x - input_stick(.1, 1, .2).x) < 1e-9,
              "Single-axis radial binding ignored its perpendicular stick sample");
        check(!radial.map().binding_conflicts(id, "pad.left_y").empty(),
              "Radial binding hid its perpendicular control dependency");
        source["actions"][0]["kind"] = "digital";
        source["actions"][0]["bindings"][0]["threshold"] = .09;
        radial.configure(InputMap(source));
        radial.submit({{"pad.left_x", .1}});
        check(!radial.latch(1).actions.at(id).held, "Radial button crossed threshold at rest");
        radial.submit({{"pad.left_y", 1}});
        check(radial.latch(2).actions.at(id).pressed,
              "Perpendicular stick change did not update a radial button edge");
    }
    const auto corner = input_stick(1, 1, .2);
    check(std::abs(std::hypot(corner.x, corner.y) - 1) < 1e-9 && corner.x == corner.y,
          "Stick diagonal saturation changed direction");
    reject([] { (void)input_stick(0, 0, 1); });
    reject([] { (void)input_stick(0, 0, -.1); });
    reject([] { (void)input_stick(std::numeric_limits<double>::quiet_NaN(), 0, .2); });
    reject([] { (void)input_stick(0, std::numeric_limits<double>::infinity(), .2); });
    reject([] { (void)input_stick(1.01, 0, .2); });
    {
        const auto trigger = ActionId::generate(), left = ActionId::generate(),
                   wheel = ActionId::generate();
        RuntimeInput analog_buttons;
        analog_buttons.configure(InputMap(
            {{"version", 1},
             {"actions",
              Json::array(
                  {action(trigger, "digital",
                          Json::array({{{"control", "pad.left_trigger"}, {"threshold", .6}}})),
                   action(left, "digital",
                          Json::array({{{"control", "pad.left_x"}, {"direction", -1}}})),
                   action(wheel, "digital", Json::array({{{"control", "mouse.wheel_y"}}}))})}}));
        analog_buttons.submit(
            {{"pad.left_trigger", .7}, {"pad.left_x", -.8}, {"mouse.wheel_y", 1}});
        const auto snapshot = analog_buttons.latch(1);
        check(snapshot.actions.at(trigger).pressed && snapshot.actions.at(left).pressed &&
                  snapshot.actions.at(wheel).pressed && snapshot.actions.at(wheel).released &&
                  !snapshot.actions.at(wheel).held,
              "Analog digital thresholds/direction/wheel pulses failed");
        check(!analog_buttons.latch(2).actions.at(wheel).pressed,
              "Wheel pulse repeated on catch-up tick");
        analog_buttons.submit({{"pad.left_trigger", .4}, {"pad.left_x", .8}});
        const auto released = analog_buttons.latch(3);
        check(released.actions.at(trigger).released && released.actions.at(left).released,
              "Analog digital actions remained held past their threshold");
    }
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
