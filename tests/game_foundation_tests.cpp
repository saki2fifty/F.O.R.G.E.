#include <forge/game_session.hpp>
#include <forge/game_settings.hpp>
#include <forge/game_storage.hpp>
#include <forge/project.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
void settings() {
    const auto id = ActionId::generate();
    InputMap input(
        {{"version", 1},
         {"actions", Json::array({{{"id", id},
                                   {"name", "Jump"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}});
    const auto defaults = default_game_settings("org.forge.test", "Test game");
    const Json user = {{"display", {{"vsync", true}, {"mode", "borderless"}}},
                       {"input",
                        {{"mouse_sensitivity", 2},
                         {"bindings", {{id.str(), Json::array({{{"control", "key.j"}}})}}}}}};
    const auto resolved = resolve_game_settings(defaults, user, input);
    check(resolved.at("display").at("width") == 1280 && resolved.at("display").at("vsync") == true,
          "Display defaults/overrides not merged");
    RuntimeInput runtime;
    runtime.configure(InputMap(resolved.at("input_map")));
    runtime.submit({{"key.j", 1}});
    check(runtime.latch(1).actions.at(id).pressed, "Rebinding did not reach actual runtime input");
    check(input.source().at("actions")[0].at("bindings")[0].at("control") == "key.space",
          "User binding mutated project input");
    rejects(
        [&] { resolve_game_settings(defaults, {{"application_id", "org.foreign.game"}}, input); });
    rejects([&] { resolve_game_settings(defaults, {{"display", {{"width", 1.5}}}}, input); });
    rejects([&] { resolve_game_settings(defaults, {{"audio", {{"master_volume", 2}}}}, input); });
    rejects([&] {
        resolve_game_settings(defaults, {{"input", {{"mouse_sensitivity", nullptr}}}}, input);
    });
    auto bad = user;
    bad["input"]["bindings"][id.str()][0]["control"] = "key.unsupported";
    rejects([&] { resolve_game_settings(defaults, bad, input); });
    bad["input"]["bindings"] = {{ActionId::generate().str(), Json::array()}};
    rejects([&] { resolve_game_settings(defaults, bad, input); });
    for (const char* id_text : {"", "../game", "GAME", "a..b", "a/b.c", "a.b/", "a.b\\c"})
        check(!valid_application_id(id_text), "Unsafe application ID admitted");
    auto project = ProjectSettings::defaults("Test");
    project["game"] = defaults;
    ProjectSettings::validate(project);
    project["game"]["version"] = 1.0;
    rejects([&] { ProjectSettings::validate(project); });
}
void storage(const std::filesystem::path& base) {
    const auto scene = AssetId::generate();
    const GameSave original{scene, {{"health", 75}, {"entity", EntityId::generate()}}};
    GameSaveSchema v1{1,
                      [scene](const GameSave& value) {
                          if (value.scene != scene ||
                              !value.data.at("health").is_number_integer() ||
                              value.data.at("health") < 0 || value.data.at("health") > 100)
                              throw std::runtime_error("Unsupported scene or health");
                      },
                      {}};
    std::filesystem::path root;
    {
        GameStorage store(base, "org.forge.test");
        root = store.root();
        rejects([&] { GameStorage competing(base, "org.forge.test"); });
        store.save("one", original, v1);
        store.save("two", {scene, {{"health", 20}}}, v1);
        check(store.slots() == std::vector<std::string>{"one", "two"},
              "Save slots not independent");
        const auto disk = bytes(root / "slot-one.json");
        check(store.load("one", v1).data == original.data, "Save round trip lost fields");
        rejects([&] { store.save("one", {scene, {{"health", -1}}}, v1); });
        check(bytes(root / "slot-one.json") == disk, "Rejected save changed previous slot");
        for (const char* slot : {"", "../x", "a/b", "a\\b", "a:b", "a.b", "One"})
            rejects([&] { store.save(slot, original, v1); });
        auto bad_schema = v1;
        bad_schema.validate = {};
        rejects([&] { store.save("one", original, bad_schema); });
        auto v2 = v1;
        v2.version = 2;
        rejects([&] { store.load("one", v2); });
        v2.migrations[1] = [](GameSave value) {
            value.data["inventory"] = Json::array({"key"});
            return value;
        };
        check(store.load("one", v2).data.at("inventory")[0] == "key", "Save migration not applied");
        check(bytes(root / "slot-one.json") == disk, "Load migration rewrote original save");
        v2.migrations[1] = [](GameSave value) {
            value.data["health"] = 999;
            return value;
        };
        rejects([&] { store.load("one", v2); });
        check(bytes(root / "slot-one.json") == disk, "Rejected migration damaged source");
        store.save("newer", original, GameSaveSchema{2, v1.validate, {}});
        rejects([&] { store.load("newer", v1); });
        auto invalid = original;
        invalid.data["bad"] = std::numeric_limits<double>::infinity();
        rejects([&] { store.save("one", invalid, v1); });
        invalid = original;
        invalid.data["large"] = std::string(8 * 1024 * 1024, 'x');
        rejects([&] { store.save("one", invalid, v1); });
        std::filesystem::create_directory(root / "slot-blocked.json");
        rejects([&] { store.save("blocked", original, v1); });
        check(bytes(root / "slot-one.json") == disk, "Failed replacement damaged other save");
        std::filesystem::remove(root / "slot-blocked.json");
        // Unpublished interrupted-write debris must never be mistaken for a slot.
        atomic_write(root / ".interrupted.pending", "incomplete");
        check(store.slots().size() == 3, "Staging debris appeared in slots");
        auto broken = Json::parse(disk);
        broken["payload"]["data"]["health"] = 10;
        atomic_write(root / "slot-corrupt.json", broken.dump());
        rejects([&] { store.load("corrupt", v1); });
        atomic_write(root / "slot-corrupt.json", "{\"version\":1," + disk.substr(1));
        rejects([&] { store.load("corrupt", v1); });
        atomic_write(root / "slot-corrupt.json", "{\"payload\":");
        rejects([&] { store.load("corrupt", v1); });
        const auto defaults = default_game_settings("org.forge.test", "Test");
        auto validator = [&](const Json& data) {
            (void)resolve_game_settings(defaults, data, InputMap{});
        };
        check(store.load_settings(validator).empty(), "Missing preferences did not use defaults");
        const Json preferences = {{"display", {{"vsync", true}}}};
        store.save_settings(preferences, validator);
        rejects([&] { store.save_settings({{"display", {{"width", -1}}}}, validator); });
        check(store.load_settings(validator) == preferences,
              "Rejected preferences replaced valid ones");
        bool refused = false;
        std::thread foreign([&] {
            try {
                (void)store.slots();
            } catch (const std::exception&) {
                refused = true;
            }
        });
        foreign.join();
        check(refused, "Foreign-thread storage access succeeded");
        store.erase("two");
        rejects([&] { store.load("two", v1); });
    }
    // A fresh application process/session reopens existing data without editor state.
    GameStorage reopened(base, "org.forge.test");
    check(reopened.load("one", v1).data == original.data, "Relaunch lost save state");
    GameStorage other(base, "org.forge.other");
    atomic_write(other.root() / "slot-one.json", bytes(root / "slot-one.json"));
    rejects([&] { other.load("one", v1); });
#ifndef _WIN32
    std::filesystem::create_symlink(root / "slot-one.json", root / "slot-linked.json");
    rejects([&] { reopened.load("linked", v1); });
    rejects([&] { reopened.save("linked", original, v1); });
    check(reopened.load("one", v1).data == original.data, "Redirected save changed target");
#endif
}
RuntimeClock::Time at(int ms) { return RuntimeClock::Time{} + std::chrono::milliseconds(ms); }
Json snapshot(const char* name) {
    WorldContext world;
    Scene scene(world);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", name},
                        {"name", name},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}}})}});
    return scene.snapshot();
}
void session() {
    GameSession game(GameSessionConfig{});
    check(game.status().at("state") == "empty", "New game session not empty");
    rejects([&] { game.resume(at(0)); });
    auto first = game.prepare(snapshot("first"));
    check(game.status().at("state") == "empty", "Preparation activated scene");
    game.activate(first, at(0), true);
    game.advance(at(20));
    check(game.status().at("clock").at("tick") == 1, "Game fixed tick not advanced");
    const auto active = game.active().scene.snapshot();
    rejects([&] { game.prepare({{"version", 999}}); });
    check(game.active().scene.snapshot() == active, "Failed load changed active scene");
    rejects([&] {
        game.prepare(snapshot("restore"), [](Scene& candidate) {
            candidate.entity("restore").set<LocalTranslation>({100, 0, 0});
            throw std::runtime_error("Game restore rejected a missing quest/content reference");
        });
    });
    check(game.active().scene.snapshot() == active, "Rejected game restore changed active world");
    auto second = game.prepare(snapshot("second"));
    check(game.active().scene.snapshot() == active, "Preload changed active scene");
    game.cancel(second);
    rejects([&] { game.activate(second, at(25), true); });
    second = game.prepare(snapshot("second"));
    auto third = game.prepare(snapshot("third"));
    rejects([&] { game.activate(second, at(25), true); });
    game.activate(third, at(10000), false);
    check(game.active().scene.entity("third").is_alive(), "Scene activation failed");
    check(game.status().at("clock").at("tick") == 1, "Scene change reset session tick");
    game.step();
    check(game.status().at("clock").at("tick") == 2, "Paused step failed");
    game.resume(at(11000));
    game.advance(at(11001));
    check(game.status().at("clock").at("tick") == 2, "Transition created catch-up debt");
    game.pause(at(11002));
    auto invalid_body = game.active().scene.entity("third");
    invalid_body.set<PhysicsBody>({});
    invalid_body.set<BoxCollider>({0, 1, 1});
    rejects([&] { game.step(); });
    check(game.status().at("state") == "faulted", "Failed physics tick did not fault session");
    rejects([&] { game.resume(at(11003)); });
    const auto recovery = game.prepare(snapshot("recovered"));
    game.activate(recovery, at(11003), false);
    check(game.status().at("state") == "paused", "Replacement did not recover faulted session");
    game.unload(at(11003));
    check(game.status().at("state") == "empty", "Unload retained active world");
    rejects([&] { game.step(); });
    bool refused = false;
    std::thread foreign([&] {
        try {
            (void)game.status();
        } catch (const std::exception&) {
            refused = true;
        }
    });
    foreign.join();
    check(refused, "Foreign-thread game access succeeded");
}
void gameplay_persistence(const std::filesystem::path& root, bool write) {
    std::filesystem::create_directories(root);
    const auto source = root / "authored.scene.json";
    if (write)
        atomic_write(source, snapshot("player").dump());
    const auto authored_bytes = bytes(source);
    const auto authored = Json::parse(authored_bytes);
    GameStorage store(root / "user", "org.forge.reference");
    GameSession game(GameSessionConfig{});
    game.activate(game.prepare(authored), at(0), false);
    const auto ref = game.active().scene.reference("player");
    const GameSaveSchema schema{
        1,
        [ref](const GameSave& value) {
            if (value.scene != ref.scene || value.data.at("player").get<EntityRef>() != ref)
                throw std::runtime_error(
                    "Reference-game save targets missing scene/player content");
            const auto position = value.data.at("position").get<std::array<double, 3>>();
            for (auto v : position)
                if (!std::isfinite(v) || std::abs(v) > 10000)
                    throw std::runtime_error("Reference-game player position is invalid");
        },
        {}};
    if (write) {
        auto entity = game.active().scene.entity(ref.entity.str());
        entity.set<LocalTranslation>({3, 2, 1});
        const auto p = entity.get<LocalTranslation>();
        store.save("progress", {ref.scene, {{"player", ref}, {"position", {p.x, p.y, p.z}}}},
                   schema);
    } else {
        const auto saved = store.load("progress", schema);
        const auto ready = game.prepare(authored, [&](Scene& candidate) {
            const auto position = saved.data.at("position").get<std::array<double, 3>>();
            candidate.entity(ref.entity.str())
                .set<LocalTranslation>({position[0], position[1], position[2]});
        });
        check(game.active().scene.entity(ref.entity.str()).get<LocalTranslation>().x == 0,
              "Save restore changed active scene before publication");
        game.activate(ready, at(10), false);
        const auto p = game.active().scene.entity(ref.entity.str()).get<LocalTranslation>();
        check(p.x == 3 && p.y == 2 && p.z == 1, "Relaunched world did not restore supported state");
    }
    check(bytes(source) == authored_bytes, "Gameplay persistence modified authored scene");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--game-probe") {
            const std::string operation = argv[2];
            if (operation != "write" && operation != "read")
                throw std::runtime_error("Unknown gameplay persistence probe");
            gameplay_persistence(std::filesystem::absolute(argv[3]), operation == "write");
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--store-probe") {
            const std::string operation = argv[2];
            GameStorage store(std::filesystem::absolute(argv[3]), "org.forge.process");
            const auto scene = AssetId::parse("12345678-1234-4123-8123-123456789abc");
            const GameSaveSchema contract{1,
                                          [scene](const GameSave& value) {
                                              if (value.scene != scene ||
                                                  value.data != Json{{"score", 42}})
                                                  throw std::runtime_error("Invalid fixture save");
                                          },
                                          {}};
            if (operation == "write")
                store.save("main", {scene, {{"score", 42}}}, contract);
            else if (operation == "read")
                check(store.load("main", contract).data.at("score") == 42, "Relaunch load failed");
            else if (operation == "hold") {
                atomic_write(store.root() / ".interrupted.pending", "incomplete replacement");
                std::cout << "ready" << std::endl;
                std::string line;
                std::getline(std::cin, line);
            } else
                throw std::runtime_error("Unknown storage probe");
            return 0;
        }
        if (argc != 2)
            throw std::runtime_error("Test requires scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        settings();
        storage(root);
        session();
        std::filesystem::remove_all(root);
        std::cout << "Game configuration, persistence and session tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
