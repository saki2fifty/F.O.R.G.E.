#pragma once
#include <forge/game_session.hpp>
#include <forge/native_sdk.hpp>
#include <forge/sdk_client.hpp>
#include <thread>

inline void test_sdk_game(const std::filesystem::path& library) {
    using namespace forge;
    auto check = [](bool value, const char* error) {
        if (!value)
            throw std::runtime_error(error);
    };
    auto controls = std::make_shared<GameControlQueue>();
    GameSessionConfig config;
    config.controls = controls;
    config.modules = {load_native_sdk(library, "project.sdk_probe", "1")};
    WorldContext authored;
    Scene scene(authored);
    scene.replace({{"version", 1}, {"entities", Json::array()}});
    GameSession game(config);
    auto ticket = game.prepare(scene.snapshot());
    game.activate(ticket, RuntimeClock::Time{}, false);
    auto& world = game.active().engine.world().world();
    const auto* host = *static_cast<const ForgeSdkWorldV1* const*>(ecs_get_id(
        world.c_ptr(), world.lookup("sdk.host").id(), world.lookup("sdk.HostProbe").id()));
    sdk::Client client(host);
    check(client.available(sdk::Capability::Game) &&
              !client.callable(sdk::Capability::ControlInput),
          "SDK game/control availability incorrect outside callback");
    check(host->game_request(host->context, R"({"operation":"pause"})") == 0,
          "SDK requested session mutation outside callback");
    game.control_frame();
    const auto token = world.lookup("sdk.game_token").get<uint64_t>();
    const auto needed = host->game_inspect(host->context, token, nullptr, 0);
    check(needed > 0 && needed < 1024, "SDK query did not return bounded copied size");
    char small[2]{'x', 'y'};
    check(host->game_inspect(host->context, token, small, 2) == needed && small[0] == 'x',
          "SDK query wrote a truncated response");
    std::vector<char> output(needed);
    check(host->game_inspect(host->context, token, output.data(), needed) == needed &&
              Json::parse(output.data()).at("state") == "queued",
          "SDK request executed inline");
    bool foreign = false;
    std::thread worker([&] { foreign = !host->game_inspect(host->context, token, nullptr, 0); });
    worker.join();
    check(foreign, "Foreign thread inspected SDK session token");
    unsigned migrated = 0;
    controls->drain([&](const Json& command, const GameSaveSchema* schema, const std::string&) {
        check(command.at("operation") == "pause" && schema && schema->version == 2,
              "SDK request lost module save schema");
        auto save = schema->migrations.at(1)({scene.asset_id(), {{"score", 4}}});
        schema->validate(save);
        check(save.data == Json{{"counter", 4}}, "Native save migration returned wrong state");
        ++migrated;
        game.pause(RuntimeClock::Time{});
        return Json{{"ok", true}};
    });
    check(migrated == 1, "Native migration did not execute");
    check(host->game_release(host->context, token) &&
              !host->game_inspect(host->context, token, nullptr, 0),
          "Released SDK token remained callable");
    game.control_frame();
    game.active().engine.world().modules().stop();
    controls->drain([&](const Json&, const GameSaveSchema*, const std::string&) {
        ++migrated;
        return Json();
    });
    check(migrated == 1 && !client.available(sdk::Capability::Game) &&
              !host->game_query(host->context, nullptr, 0),
          "Stopped module retained game operations or callbacks");
}
