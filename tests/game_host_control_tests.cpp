#include <forge/game_content.hpp>
#include <forge/game_host_controls.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* error) {
    if (!value)
        throw std::runtime_error(error);
}
void run(const std::filesystem::path& root) {
    const auto content = root / "content";
    std::filesystem::create_directories(content);
    WorldContext authoring;
    Scene scene(authoring);
    scene.replace({{"version", 1}, {"entities", Json::array()}});
    scene.save(content / "main.scene.json");
    AssetCatalog catalog(content);
    const auto asset = catalog.add_scene("main.scene.json").id;
    catalog.save(AssetCatalog::project_index(content));
    const auto action = ActionId::generate();
    InputMap input(
        {{"version", 1},
         {"actions", Json::array({{{"id", action},
                                   {"name", "Jump"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}});
    const auto defaults = default_game_settings("org.forge.host-test", "Host test");
    auto queue = std::make_shared<GameControlQueue>();
    GameSessionConfig config;
    config.controls = queue;
    config.input = input;
    EngineModule module;
    module.id = "game.test";
    module.dependencies = {"forge.game"};
    module.allowed_services = capability(Capability::Game);
    module.runtime_roles = role_mask(WorldRole::Runtime);
    GameSaveSchema schema{1,
                          [](const GameSave& save) {
                              if (!save.data.at("counter").is_number_integer() ||
                                  save.data.at("counter") < 0)
                                  throw std::runtime_error("Invalid counter");
                          },
                          {}};
    module.start = [schema](ModuleContext& c) {
        c.services.game()->save_schema(c.id, schema, c.code);
    };
    module.scene_ready = [](ModuleContext& c) {
        check(!c.input && !c.controls, "Candidate initialization received gameplay input");
        c.world.entity("game.prepared").set<int>(1);
    };
    module.restore = [](ModuleContext& c, const Json& data) {
        check(c.world.lookup("game.prepared").get<int>() == 1,
              "Restore ran before scene initialization");
        c.world.entity("game.saved").set<int>(data.at("counter").get<int>());
    };
    config.modules.push_back(module);
    GameSession game(config);
    game.activate(game.prepare(load_game_scene(content, {asset})), RuntimeClock::Time{}, false);
    GameStorage storage(root / "users", "org.forge.host-test");
    unsigned applications = 0;
    GamePlatformControls platform;
    platform.settings = [&](const Json& value) {
        ++applications;
        if (value.at("display").at("width") == 777)
            throw std::runtime_error("Rejected display fixture");
    };
    GameHostControls host(game, queue, storage, content, defaults, input, Json::object(), platform);
    auto request = [&](Json command) {
        auto service = game.active().engine.services().game();
        const auto token = service->request("game.test", command);
        host.pump(RuntimeClock::Time{});
        const auto result = service->inspect("game.test", token);
        service->release("game.test", token);
        return result;
    };
    check(request({{"operation", "resume"}}).state == "succeeded" &&
              game.status().at("state") == "running",
          "Queued resume did not reach session");
    check(request({{"operation", "pause"}}).state == "succeeded" &&
              game.status().at("state") == "paused",
          "Queued pause did not reach session");
    check(request(
              {{"operation", "set_settings"}, {"values", {{"input", {{"mouse_sensitivity", 2}}}}}})
                  .state == "succeeded",
          "Settings update failed");
    check(storage.load_settings([](const Json&) {}).at("input").at("mouse_sensitivity") == 2,
          "User settings not persisted");
    check(request({{"operation", "set_settings"}, {"values", {{"display", {{"width", 777}}}}}})
                      .state == "failed" &&
              host.settings().at("display").at("width") == 1280 && applications == 2,
          "Failed setting was committed or platform was not restored");
    check(request({{"operation", "rebind_begin"}, {"action", action}, {"index", 0}}).state ==
              "succeeded",
          "Could not begin player rebind");
    game.input({{"key.j", 1}});
    host.pump(RuntimeClock::Time{});
    check(request({{"operation", "rebind_commit"}}).state == "succeeded", "Captured rebind failed");
    game.input({{"key.j", 1}});
    check(game.active().simulation.input().latch(1).actions.at(action).pressed,
          "Persisted rebind did not reach runtime");
    check(request({{"operation", "rebind_reset"}, {"action", action}}).state == "succeeded",
          "Restore defaults failed");
    game.input({{"key.space", 1}});
    check(game.active().simulation.input().latch(2).actions.at(action).pressed,
          "Default binding not restored");
    check(
        request(
            {{"operation", "save"}, {"slot", "one"}, {"scene", asset}, {"data", {{"counter", 43}}}})
                .state == "succeeded",
        "Save request did not reach storage");
    check(
        request(
            {{"operation", "save"}, {"slot", "one"}, {"scene", asset}, {"data", {{"counter", -1}}}})
                    .state == "failed" &&
            storage.load("one", schema).data.at("counter") == 43,
        "Invalid save replaced previous state");
    const auto slots = request({{"operation", "slots"}}).value;
    check(slots.size() == 1 && slots[0].at("valid") == true, "Slot metadata missing");
    check(request({{"operation", "prepare"}, {"asset", AssetId::generate()}}).state == "failed" &&
              game.active().scene.asset_id() == asset,
          "Failed scene request lost current scene");
    auto old = game.active().engine.services().game();
    old->request("game.test", {{"operation", "load"}, {"slot", "one"}, {"run", false}});
    host.pump(RuntimeClock::Time{});
    check(game.active().scene.asset_id() == asset &&
              game.active().engine.world().world().lookup("game.saved").get<int>() == 43,
          "Save did not restore into prepared candidate");
    check(request({{"operation", "quit"}}).state == "succeeded" && host.quit_requested(),
          "Quit request did not reach host");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        run(root);
        std::filesystem::remove_all(root);
        std::cout << "Game host session, settings, rebind and save/load requests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
