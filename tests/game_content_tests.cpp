#include <forge/game_content.hpp>
#include <forge/game_session.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected bootstrap rejection");
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need scratch directory");
        const auto base = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto root = base / "original";
        std::filesystem::create_directories(root);
        WorldContext world;
        Scene scene(world);
        scene.replace(
            {{"version", 1},
             {"entities",
              Json::array({{{"id", "one"}, {"name", "One"}, {"components", Json::object()}}})}});
        scene.save(root / "main.scene.json");
        AssetCatalog catalog(root);
        auto record = catalog.add_scene("main.scene.json");
        catalog.save(AssetCatalog::project_index(root));
        const auto original = bytes(root / "main.scene.json");
        const auto loaded = load_game_scene(root, {record.id});
        GameSession game(GameSessionConfig{});
        game.activate(game.prepare(loaded), RuntimeClock::Clock::now(), false);
        check(game.active().scene.asset_id() == record.id &&
                  game.active().scene.entity_count() == 1,
              "Bootstrap changed scene/entity identity");
        check(bytes(root / "main.scene.json") == original, "Bootstrap rewrote authored scene");
        rejects([&] { load_game_scene(root, {AssetId::generate()}); });
        auto invalid = Json::parse(original);
        invalid["asset_id"] = AssetId::generate();
        atomic_write(root / "main.scene.json", invalid.dump());
        rejects([&] { load_game_scene(root, {record.id}); });
        atomic_write(root / "main.scene.json", "{\"version\":3,");
        rejects([&] { load_game_scene(root, {record.id}); });
        atomic_write(root / "main.scene.json", original);
        // Structured prefab snapshots retain independent inherited transform channels.
        const auto prefab = AssetId::generate();
        const auto member = PrefabMemberId::generate();
        const auto entity = EntityId::generate();
        const Json definition{
            {"format", "forge.prefab"},
            {"version", 2},
            {"asset_id", prefab},
            {"revision", 1u},
            {"root", member},
            {"members",
             Json::array({{{"id", member},
                           {"name", "Inherited"},
                           {"components",
                            {{"forge.local_translation", {{"x", 2}, {"y", 0}, {"z", 0}}},
                             {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 1}}}}}}})}};
        atomic_write(root / "shape.prefab.json", definition.dump());
        catalog.add({prefab, PrefabAsset::type, "shape.prefab.json"});
        catalog.save(AssetCatalog::project_index(root));
        auto structured = Json::parse(original);
        structured["version"] = 5;
        structured["entities"] = Json::array(
            {{{"id", entity},
              {"name", "Instance"},
              {"components", Json::object()},
              {"prefab_instance",
               {{"asset", prefab}, {"revision", 1u}, {"members", {{member.str(), entity}}}}}}});
        atomic_write(root / "main.scene.json", structured.dump());
        auto candidate = load_game_scene(root, {record.id});
        check(candidate.at("_prefab_sources").size() == 1, "Prefab source closure missing");
        game.activate(game.prepare(candidate), RuntimeClock::Clock::now(), false);
        auto instance = game.active().scene.entity(entity.str());
        check(instance.get<LocalScale>().x == -1 && !instance.owns<LocalScale>() &&
                  !instance.owns<LocalTranslation>(),
              "Bootstrap materialized prefab overrides");
        std::filesystem::rename(root / "shape.prefab.json", root / "missing.prefab.json");
        rejects([&] { load_game_scene(root, {record.id}); });
        check(game.active().scene.entity(entity.str()).is_alive(),
              "Failed source read changed live scene");
        atomic_write(root / "main.scene.json", original);
        // Missing structured sources cannot silently become stripped instances.
        invalid = Json::parse(original);
        invalid["entities"][0]["prefab_instance"] = {{"asset", AssetId::generate()}};
        atomic_write(root / "main.scene.json", invalid.dump());
        rejects([&] { load_game_scene(root, {record.id}); });
        atomic_write(root / "main.scene.json", original);
        const auto moved = base / "relocated";
        std::filesystem::rename(root, moved);
        const auto relocated = load_game_scene(moved, {record.id});
        check(relocated == loaded, "Read-only bootstrap depended on original absolute path");
        std::filesystem::remove_all(base);
        std::cout << "Read-only scene bootstrap identity, rejection and relocation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
