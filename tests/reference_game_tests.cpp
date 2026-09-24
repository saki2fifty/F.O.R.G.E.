#include "../samples/reference_game/ids.hpp"
#include <forge/character_components.hpp>
#include <forge/game_content.hpp>
#include <forge/game_host_controls.hpp>
#include <forge/native_sdk.hpp>
#include <forge/physics_components.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/transform.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need module, input and scratch root");
        const auto root = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        WorldContext author;
        Scene scene(author);
        Json entities = Json::array();
        for (auto id : {reference::player_id, reference::camera_id, reference::switch_id,
                        reference::platform_id})
            entities.push_back({{"id", id},
                                {"name", id},
                                {"components", Json::object()},
                                {"spatial", {{"mode", "world"}}}});
        const auto floor_id = EntityId::generate().str();
        entities.push_back({{"id", floor_id}, {"name", "Floor"}, {"components", Json::object()}});
        scene.replace(
            {{"version", 3}, {"asset_id", reference::level_scene}, {"entities", entities}});
        scene.entity(reference::player_id).set<CharacterController>({});
        scene.entity(reference::player_id).set<LocalTranslation>({0, .05, 0});
        scene.entity(reference::player_id).set<CharacterController>([] {
            CharacterController c;
            c.layer = 1;
            return c;
        }());
        scene.entity(floor_id)
            .set<LocalTranslation>({0, -.5, 0})
            .set<PhysicsBody>({})
            .set<BoxCollider>({30, 1, 30});
        scene.entity(reference::switch_id)
            .set<LocalTranslation>({0, 1.5, 2})
            .set<PhysicsBody>({})
            .set<BoxCollider>({.5f, .5f, .5f});
        PhysicsBody platform;
        platform.motion = 1;
        scene.entity(reference::platform_id)
            .set<LocalTranslation>({4, .15, -3})
            .set<PhysicsBody>(platform)
            .set<BoxCollider>({3, .3f, 3});
        atomic_write(root / "reference.rml", "<rml><body>Reference</body></rml>");
        const auto ui_asset = register_ui_document(root, "reference.rml");
        auto document = scene.snapshot();
        document["entities"].push_back(
            {{"id", reference::ui_id}, {"name", "HUD"}, {"components", Json::object()}});
        scene.restore_snapshot(document);
        scene.entity(reference::ui_id).set<UiDocument>({{ui_asset.id}});
        scene.save(root / "level.scene.json");
        auto catalog = AssetCatalog::open_project(root);
        catalog.add_scene("level.scene.json");
        Scene menu(author);
        menu.replace(
            {{"version", 3}, {"asset_id", reference::menu_scene}, {"entities", Json::array()}});
        menu.save(root / "menu.scene.json");
        catalog.add_scene("menu.scene.json");
        catalog.save(AssetCatalog::project_index(root));
        std::ifstream input_stream(argv[2]);
        Json input;
        input_stream >> input;
        auto queue = std::make_shared<GameControlQueue>();
        GameSessionConfig config;
        config.controls = queue;
        config.input = InputMap(input);
        config.physics.layers[1] = "Player";
        config.content_root = root;
        config.ui = true;
        config.modules = {load_native_sdk(argv[1], "project.reference", "1")};
        GameSession game(config);
        game.activate(game.prepare(scene.snapshot()), RuntimeClock::Time{}, false);
        GameStorage storage(root / "users", "org.forge.reference-test");
        bool cursor = false;
        GamePlatformControls platform_controls;
        platform_controls.cursor = [&](bool capture) { cursor = capture; };
        GameHostControls host(game, queue, storage, root,
                              default_game_settings("org.forge.reference-test", "Reference"),
                              InputMap(input), Json::object(), platform_controls);
        game.control_frame();
        host.pump(RuntimeClock::Time{});
        check(cursor && game.status().at("state") == "running",
              "Reference did not enter gameplay through queued controls");
        game.pause(RuntimeClock::Time{});
        for (unsigned i = 0; i < 30; ++i)
            game.step();
        auto ui_model = [&] {
            const auto service =
                std::static_pointer_cast<UiRuntime>(game.active().engine.services().ui());
            return service->snapshot(game.active().scene, "reference-test", 1, 0, true);
        };
        auto ui_command = [&](const char* value) {
            const auto snapshot = ui_model();
            std::static_pointer_cast<UiRuntime>(game.active().engine.services().ui())
                ->command(game.active().scene,
                          {{"instance", snapshot.at("documents")[0].at("instance")},
                           {"command", "Reference"},
                           {"value", value}},
                          [](const std::string&) {});
            game.control_frame();
            host.pump(RuntimeClock::Time{});
        };
        const auto ref = game.active().scene.reference(reference::player_id);
        auto physics = game.active().physics();
        check(physics->character(ref).ground == CharacterGround::OnGround,
              "Reference character did not land");
        game.input({{"key.e", 1}});
        game.step();
        game.control_frame();
        host.pump(RuntimeClock::Time{});
        check(ui_model().at("documents")[0].at("model").at("interactions") == 1,
              "Reference physics ray did not select the interaction target");
        game.input({{"key.e", 0}, {"key.space", 1}});
        game.step();
        check(physics->character(ref).jump_accepted,
              "Reference jump action did not reach Character");
        game.input({{"key.space", 0}});
        for (unsigned i = 0; i < 120; ++i)
            game.step();
        game.input({{"key.c", 1}});
        game.step();
        check(physics->character(ref).crouched, "Reference crouch action failed");
        for (unsigned i = 0; i < 30; ++i)
            game.step();
        const auto eye = game.active().scene.entity(reference::camera_id).get<LocalTranslation>();
        const auto feet = game.active().scene.entity(reference::player_id).get<LocalTranslation>();
        check(eye.y - feet.y > .74 && eye.y - feet.y < .76,
              "Camera did not transition to accepted crouch height");
        game.input({{"key.c", 0}, {"key.d", 1}});
        const auto before = physics->character(ref).position.x;
        for (unsigned i = 0; i < 30; ++i)
            game.step();
        check(physics->character(ref).position.x > before + 1,
              "Reference movement did not use fixed snapshots");
        game.input({{"key.d", 0}});
        // Control frames remain callable while paused, without advancing simulation.
        const auto tick = game.status().at("clock").at("tick");
        game.input({{"key.escape", 1}});
        game.control_frame();
        host.pump(RuntimeClock::Time{});
        check(!cursor && game.status().at("state") == "paused" &&
                  game.status().at("clock").at("tick") == tick,
              "Pause control advanced simulation or retained capture");
        ui_command("save");
        check(storage.slots() == std::vector<std::string>{"one"},
              "Reference save menu did not write a slot");
        ui_command("load");
        check(game.active().scene.asset_id().str() == reference::level_scene,
              "Reference load menu failed to replace the scene");
        const Json restored = {
            {"position", {1, .1, -2}}, {"yaw", .2}, {"pitch", -.1}, {"interactions", 7}};
        auto ticket = game.prepare(scene.snapshot(), {}, {{"project.reference", restored}});
        game.activate(ticket, RuntimeClock::Time{}, false);
        check(std::abs(game.active().scene.entity(reference::player_id).get<LocalTranslation>().z +
                       2) < .01,
              "Native reference restore did not restore durable position");
        auto invalid = restored;
        invalid["interactions"] = -1;
        bool rejected = false;
        try {
            game.prepare(scene.snapshot(), {}, {{"project.reference", invalid}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected &&
                  game.active().scene.entity(reference::player_id).get<LocalTranslation>().z == -2,
              "Rejected save damaged active reference world");
        game.unload(RuntimeClock::Time{});
        std::cout << "Reference exact module: fixed movement, jump, crouch, pause, restore and "
                     "rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
