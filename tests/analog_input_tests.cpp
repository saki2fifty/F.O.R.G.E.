#include <array>
#include <cmath>
#include <cstring>
#include <forge/game_settings.hpp>
#include <forge/native_sdk.hpp>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double value, double expected) {
    check(std::isfinite(value) && std::abs(value - expected) < 1e-12,
          "Analog response disagrees with independent numeric oracle");
}
template <class F> void rejects(F&& f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid analog conditioning accepted");
}
void math() {
    near(input_axis(.2, .2, .1, 2), 0);
    near(input_axis(.55, .2, .1, 2), .25);
    near(input_axis(-.55, .2, .1, 2), -.25);
    near(input_axis(.9, .2, .1, 2), 1);
    near(input_axis(-1, .2, .1, 2), -1);
    near(input_axis(.375, .2, .1, .5), .5);
    const auto diagonal = input_stick(.33, .44, .2, .1, 2);
    near(diagonal.x, .15);
    near(diagonal.y, .2);
    const auto negative = input_stick(-.33, -.44, .2, .1, 2);
    near(negative.x, -.15);
    near(negative.y, -.2);
    const auto saturated = input_stick(.6, .8, .2, .1, 3);
    near(saturated.x, .6);
    near(saturated.y, .8);
    for (double value : {-1., -.8, -.2, 0., .1, .55, 1.}) {
        const double expected =
            std::abs(value) <= .2 ? 0 : std::copysign((std::abs(value) - .2) / .8, value);
        near(input_axis(value, .2), expected);
        near(input_axis(value, .2, 0, 1), expected);
    }
    for (double bad : {-.1, 1., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { (void)input_axis(.5, .2, bad, 1); });
    rejects([] { (void)input_stick(.3, .4, .2, .8, 1); });
    for (double bad : {0., .09, 8.01, std::numeric_limits<double>::infinity()})
        rejects([&] { (void)input_stick(.3, .4, .2, .1, bad); });
    rejects([] { (void)input_axis(1.1, 0); });
}
void gamepad_slot_mapping() {
    const std::array<InputVector, 4> samples{{{.1, 1}, {.6, 0}, {-.75, .25}, {.25, -.75}}};
    std::vector<ActionId> ids;
    Json source{{"version", 1}, {"actions", Json::array()}};
    for (std::size_t slot = 1; slot <= gamepad_slot_limit; ++slot) {
        const auto prefix = "pad." + std::to_string(slot) + ".";
        check(is_gamepad_stick_control(prefix + "left_x") &&
                  is_gamepad_stick_control(prefix + "right_y") &&
                  !is_gamepad_stick_control(prefix + "south") &&
                  input_control(prefix + "left_trigger").unipolar,
              "Indexed gamepad discovery lost control kind or stick pairing");
        ids.push_back(ActionId::generate());
        source["actions"].push_back({{"id", ids.back()},
                                     {"name", "Player " + std::to_string(slot)},
                                     {"kind", "axis1"},
                                     {"bindings", Json::array({{{"control", prefix + "left_x"},
                                                                {"radial", true},
                                                                {"deadzone", .2}}})}});
    }
    const auto legacy = ActionId::generate(), keyboard = ActionId::generate();
    source["actions"].push_back({{"id", legacy},
                                 {"name", "Active pad"},
                                 {"kind", "axis1"},
                                 {"bindings", Json::array({{{"control", "pad.left_x"}}})}});
    source["actions"].push_back({{"id", keyboard},
                                 {"name", "Keyboard"},
                                 {"kind", "digital"},
                                 {"bindings", Json::array({{{"control", "key.space"}}})}});
    RuntimeInput input;
    input.configure(InputMap(source));
    std::vector<InputEvent> events{{"pad.left_x", -.5}, {"key.space", 1}};
    for (std::size_t slot = 1; slot <= gamepad_slot_limit; ++slot) {
        const auto prefix = "pad." + std::to_string(slot) + ".";
        events.push_back({prefix + "left_x", samples[slot - 1].x});
        events.push_back({prefix + "left_y", samples[slot - 1].y});
    }
    input.submit(events);
    auto snapshot = input.latch(1);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto x = samples[i].x, y = samples[i].y;
        const auto length = std::sqrt(x * x + y * y);
        near(snapshot.actions.at(ids[i]).x, x * (std::min(length, 1.) - .2) / (.8 * length));
    }
    input.submit({{"pad.1.left_x", 0}, {"pad.1.left_y", 0}});
    const auto released = input.latch(2);
    near(released.actions.at(ids[0]).x, 0);
    for (std::size_t i = 1; i < ids.size(); ++i)
        near(released.actions.at(ids[i]).x, snapshot.actions.at(ids[i]).x);
    near(released.actions.at(legacy).x, -.5);
    check(released.actions.at(keyboard).held,
          "One logical pad's neutral controls released unrelated input");
    rejects([] { (void)input_control("pad.0.south"); });
    rejects([] { (void)input_control("pad.5.south"); });
    auto invalid = source;
    invalid["actions"][0]["bindings"][0]["control"] = "pad.1.south";
    rejects([&] { (void)InputMap(invalid); });
}
void mapping() {
    const auto move = ActionId::generate(), press = ActionId::generate(),
               radial = ActionId::generate();
    auto bindings = Json::array({{{"control", "pad.left_x"},
                                  {"x", 1},
                                  {"y", 0},
                                  {"deadzone", .2},
                                  {"outer_deadzone", .1},
                                  {"response_exponent", 2},
                                  {"radial", true},
                                  {"extension", "retained"}},
                                 {{"control", "pad.left_y"},
                                  {"x", 0},
                                  {"y", 1},
                                  {"deadzone", .2},
                                  {"outer_deadzone", .1},
                                  {"response_exponent", 2},
                                  {"radial", true}}});
    Json source{
        {"version", 1},
        {"actions",
         Json::array({{{"id", move}, {"name", "Move"}, {"kind", "axis2"}, {"bindings", bindings}},
                      {{"id", press},
                       {"name", "Trigger"},
                       {"kind", "digital"},
                       {"bindings", Json::array({{{"control", "pad.right_trigger"},
                                                  {"outer_deadzone", .1},
                                                  {"response_exponent", 2},
                                                  {"threshold", .25}}})}},
                      {{"id", radial},
                       {"name", "Radial threshold"},
                       {"kind", "digital"},
                       {"bindings", Json::array({{{"control", "pad.left_x"},
                                                  {"radial", true},
                                                  {"deadzone", .2},
                                                  {"outer_deadzone", .1},
                                                  {"response_exponent", 2},
                                                  {"threshold", .21}}})}}})}};
    const InputMap map(source);
    RuntimeInput input;
    input.configure(map);
    input.submit({{"pad.left_x", .33}, {"pad.left_y", .44}, {"pad.right_trigger", .45}});
    auto sample = input.latch(1);
    near(sample.actions.at(move).x, .15);
    near(sample.actions.at(move).y, .2);
    check(sample.actions.at(press).pressed && sample.actions.at(press).held,
          "Digital threshold did not use conditioned trigger");
    check(!input.latch(2).actions.at(press).pressed, "Catch-up repeated trigger edge");
    rejects([&] { input.submit({{"pad.left_x", 1}, {"bad.control", 1}}); });
    near(input.latch(3).actions.at(move).x, .15);
    input.submit({{"pad.left_x", .2}, {"pad.left_y", 0}, {"pad.right_trigger", .4}});
    check(input.latch(4).actions.at(press).released, "Conditioned trigger did not release");
    input.submit({{"pad.left_y", .9}});
    check(input.latch(5).actions.at(radial).pressed,
          "Perpendicular radial change did not update conditioned digital edges");
    input.release_all();
    check(input.latch(6).actions.at(radial).released, "Reset retained conditioned input");
    for (const char* field : {"outer_deadzone", "response_exponent"})
        for (const auto& bad : {Json(true), Json("2"), Json(nullptr)}) {
            auto invalid = bindings;
            invalid[0][field] = bad;
            rejects([&] { (void)map.with_bindings(move, invalid); });
            check(map.source() == source, "Rejected binding changed original map");
        }
    for (const char* control : {"key.w", "mouse.delta_x", "mouse.wheel_y"}) {
        auto invalid = bindings;
        invalid[0]["control"] = control;
        invalid[0]["radial"] = false;
        invalid[0]["deadzone"] = 0;
        rejects([&] { (void)map.with_bindings(move, invalid); });
    }
    auto contexts = source;
    contexts["version"] = 2;
    contexts["contexts"] = Json::array({{{"name", "game"}, {"active", true}},
                                        {{"name", "menu"}, {"active", true}, {"priority", 10}}});
    for (auto& action : contexts["actions"])
        action["context"] = "game";
    const auto menu = ActionId::generate();
    contexts["actions"].push_back({{"id", menu},
                                   {"name", "Claim Y"},
                                   {"kind", "axis1"},
                                   {"context", "menu"},
                                   {"bindings", Json::array({{{"control", "pad.left_y"}}})}});
    input.configure(InputMap(contexts));
    input.submit({{"pad.left_x", .33}, {"pad.left_y", .44}});
    sample = input.latch(7);
    near(sample.actions.at(move).x, (.13 / .7) * (.13 / .7));
    near(sample.actions.at(move).y, 0);
    near(sample.actions.at(menu).x, .44);
    contexts["contexts"][0]["phase"] = "control";
    contexts["contexts"][1]["active"] = false;
    input.configure(InputMap(contexts));
    input.submit({{"pad.left_x", .33}, {"pad.left_y", .44}});
    sample = input.latch_controls(1);
    near(sample.actions.at(move).x, .15);
    near(sample.actions.at(move).y, .2);
    auto defaults = default_game_settings("org.forge.analog-input", "Analog input");
    auto project = ProjectSettings::defaults("Analog input");
    project["input"] = source;
    project["game"] = defaults;
    ProjectSettings::validate(project);
    defaults["input"]["stick_deadzone"] = .95;
    project["game"] = defaults;
    rejects([&] { ProjectSettings::validate(project); });
    defaults["input"]["stick_deadzone"] = .2;
    const auto resolved = resolve_game_settings(defaults, Json::object(), map);
    check(resolved.at("input_map").at("actions")[0].at("bindings")[0].at("extension") == "retained",
          "Settings composition lost binding extension");
    rejects([&] {
        (void)resolve_game_settings(defaults, {{"input", {{"stick_deadzone", .95}}}}, map);
    });
}
} // namespace
int main(int argc, char** argv) {
    try {
        math();
        mapping();
        gamepad_slot_mapping();
        if (argc >= 2) {
            // Prefix of the original public SDK fixture's runtime-only component.
            // It is not a persisted type or part of the FORGE SDK ABI.
            struct ProbeView {
                std::uint64_t ticks, presses;
                double dt, input_x, input_y;
            };
            EngineContext engine(WorldRole::Runtime, false,
                                 {load_native_sdk(argv[1], "project.sdk_probe", "1")});
            Scene scene(engine.world());
            RuntimeSimulation simulation(engine.world(), scene);
            const auto action = ActionId::parse("12345678-1234-4234-8234-123456789abc");
            simulation.input().configure(InputMap(
                {{"version", 1},
                 {"actions",
                  Json::array({{{"id", action},
                                {"name", "SDK analog"},
                                {"kind", "axis2"},
                                {"bindings", Json::array({{{"control", "pad.left_x"},
                                                           {"radial", true},
                                                           {"deadzone", .2},
                                                           {"outer_deadzone", .1},
                                                           {"response_exponent", 2}},
                                                          {{"control", "pad.left_y"},
                                                           {"x", 0},
                                                           {"y", 1},
                                                           {"radial", true},
                                                           {"deadzone", .2},
                                                           {"outer_deadzone", .1},
                                                           {"response_exponent", 2}}})}}})}}));
            simulation.input().submit({{"pad.left_x", .33}, {"pad.left_y", .44}});
            simulation.tick(1.f / 60);
            auto& world = scene.world();
            const auto subject = world.lookup("sdk.subject"), component = world.lookup("sdk.Probe");
            check(subject && component, "Public SDK fixture registration missing");
            const auto* info = ecs_get_type_info(world, component);
            check(info && info->size == sizeof(ProbeView), "Public SDK fixture layout mismatch");
            const auto* value = ecs_get_id(world, subject, component);
            check(value != nullptr, "Public SDK input consumer state missing");
            ProbeView probe{};
            std::memcpy(&probe, value, sizeof(probe));
            check(probe.ticks == 1, "Public SDK input consumer did not execute");
            near(probe.input_x, .15);
            near(probe.input_y, .2);
        }
        if (argc == 3) {
            const ProjectSettings example(argv[2]);
            const auto map = example.input();
            const auto resolved =
                resolve_game_settings(example.document().at("game"), Json::object(), map);
            RuntimeInput input;
            input.configure(InputMap(resolved.at("input_map")));
            input.submit({{"pad.left_x", .525}});
            near(input.latch(1).actions.at(map.actions().front().id).x, .25);
        }
        std::cout << "Analog response math, admission, routing, latches and settings passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
