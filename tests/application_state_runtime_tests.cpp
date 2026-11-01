#include <forge/application_states.hpp>
#include <forge/game_session.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string active(GameSession& game) {
    return observe_application_states(game.active().engine.world().world()).at(0).active;
}
void session_boundaries() {
    WorldContext authored;
    Scene scene(authored);
    scene.replace({{"version", 1}, {"entities", Json::array()}});
    const auto snapshot = scene.snapshot();
    const ApplicationStatesDefinition definition{
        {{"app",
          "Game mode",
          "menu",
          {{"menu", "Menu", ""}, {"playing", "Playing", ""}},
          false,
          {},
          {}}}};
    bool request_control = false;
    unsigned gameplay_ticks = 0;
    EngineModule project;
    project.id = "test.application_states";
    project.dependencies = {"forge.core", "forge.input"};
    project.runtime_roles = role_mask(WorldRole::Runtime);
    project.schemas = [](ModuleContext& context) {
        const auto domain = application_state_domain(context.world, "app");
        const auto playing = application_state_entity(context.world, "app", "playing");
        // Query registration precedes admitted state configuration.
        check(!domain.has<ApplicationStateActive>(playing), "Registration activated state");
    };
    project.start = [&](ModuleContext& context) {
        const auto domain = application_state_domain(context.world, "app");
        const auto playing = application_state_entity(context.world, "app", "playing");
        context.world.system("test.StateConditionedGameplay")
            .with<ApplicationStateMember>(playing)
            .src(domain)
            .kind(context.world.entity("forge.runtime.Gameplay"))
            .run([&](flecs::iter& it) {
                while (it.next())
                    ++gameplay_ticks;
            })
            .add<FixedSimulation>();
    };
    project.scene_ready = [&](ModuleContext& context) {
        configure_application_states(context.world, definition);
    };
    project.controls = [&](ModuleContext& context) {
        if (request_control) {
            request_application_state(context.world, "app", "playing");
            request_control = false;
        }
    };
    GameSessionConfig config;
    config.modules = {project};
    GameSession game(config);
    const RuntimeClock::Time now{};
    auto ticket = game.prepare(snapshot);
    check(gameplay_ticks == 0, "Candidate initialization progressed gameplay");
    game.activate(ticket, now, false);
    check(active(game) == "menu", "Initial state not published before activation");
    game.step();
    check(gameplay_ticks == 0, "State-conditioned system ran in menu");
    request_control = true;
    const auto tick = game.status().at("clock").at("tick");
    game.control_frame();
    check(active(game) == "playing" && game.status().at("clock").at("tick") == tick &&
              gameplay_ticks == 0,
          "Paused control transition advanced time/gameplay");
    game.step();
    check(gameplay_ticks == 1, "Step did not execute state-conditioned gameplay");
    request_application_state(game.active().engine.world().world(), "app", "menu");
    game.step();
    check(active(game) == "menu" && gameplay_ticks == 1,
          "Fixed boundary did not apply request before gameplay");
    const auto authored_after = game.active().scene.snapshot();
    check(authored_after == snapshot, "Runtime application state contaminated authored scene");
    request_application_state(game.active().engine.world().world(), "app", "playing");
    game.control_frame();
    ticket = game.prepare(snapshot);
    check(active(game) == "playing", "Preparing replacement disturbed active world");
    game.activate(ticket, now, false);
    check(active(game) == "menu", "Replacement world retained retired logical state");
}
} // namespace
int main() {
    try {
        session_boundaries();
        std::cout << "Application states: real session preparation, Pause, Step, fixed boundary, "
                     "conditional gameplay and scene replacement passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
