#include "runtime_package.hpp"
#include <algorithm>
#include <array>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/input_message.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/project.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
constexpr auto source_path = "Scenes/150-input-messages.scene.json";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    return Json::parse(stream);
}
void prefab(const std::filesystem::path& gallery) {
    WorldContext authored;
    Scene scene(authored);
    auto source = read(gallery / source_path);
    const auto row = *std::find_if(
        source.at("entities").begin(), source.at("entities").end(),
        [](const auto& row) { return row.at("components").contains("forge.input_message"); });
    source["entities"] = Json::array({row});
    scene.reset(source);
    auto definition = create_prefab_source(scene, row.at("id"));
    source["entities"] = Json::array();
    scene.reset(source);
    scene.set_prefab_sources({{definition.asset(), definition.source}});
    const auto instance = instantiate_prefab(scene, definition.asset());
    auto entity = scene.entity(instance);
    check(entity.has<InputMessage>() && !entity.owns<InputMessage>() &&
              entity.has<LocalTranslation>() && !entity.owns<LocalTranslation>(),
          "Prefab did not inherit emitter and payload through the existing owner");
    const auto config = entity.get<InputMessage>();
    const auto before = scene.document();
    authoring_command(scene, "component.override",
                      {{"entity", instance}, {"component", "forge.input_message"}});
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.input_message"},
                       {"field", "payload"},
                       {"value", "forge.tint"}});
    entity = scene.entity(instance);
    check(entity.owns<InputMessage>() && entity.get<InputMessage>().payload == "forge.tint",
          "Prefab emitter override did not use typed property owner");
    check(scene.undo() && scene.undo() && scene.document() == before,
          "Prefab override Undo did not restore original intent");
    entity = scene.entity(instance);
    check(!entity.owns<InputMessage>() && entity.get<InputMessage>() == config,
          "Prefab Undo retained a native owned override");
    const auto snapshot = scene.snapshot();
    int calls = 0;
    bool identity_ok = true;
    EngineModule receiver;
    receiver.id = "test.prefab_messages";
    receiver.dependencies = {"forge.core"};
    receiver.runtime_roles = role_mask(WorldRole::Runtime);
    receiver.start = [&](ModuleContext& context) {
        context.world.observer()
            .event<LocalTranslation>()
            .with(flecs::Any)
            .each([&](flecs::iter& it, std::size_t row) {
                const auto* id = it.entity(row).try_get<PersistentEntityId>();
                identity_ok &= id && id->value.str() == instance && it.param<LocalTranslation>();
                ++calls;
            });
    };
    GameSessionConfig session;
    session.input = ProjectSettings(gallery).input();
    session.modules = {receiver};
    GameSession game(session);
    const auto ticket = game.prepare(snapshot);
    game.activate(ticket, RuntimeClock::Clock::now(), false);
    game.input({{"key.space", 1}});
    game.step();
    check(calls == 1 && identity_ok && scene.document() == before && scene.snapshot() == snapshot,
          "Prefab instance message lost copied payload, source identity or authoring isolation");
}
void packaged(const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto stage = scratch / AssetId::generate().str();
    const auto root = stage / "source";
    std::filesystem::create_directories(stage);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto source = read(root / source_path);
    const std::array roots{source.at("asset_id").get<AssetId>()};
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(source);
    {
        ProjectLease lease(root);
        prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
        package_runtime_content(root, stage / "export", roots, {"windows", "d3d12"}, {}, {},
                                scene.schema());
    }
    std::filesystem::rename(stage / "export", stage / "relocated");
    std::filesystem::rename(root, stage / "sources-hidden");
    const auto content = open_runtime_content(stage / "relocated", {"windows", "d3d12"});
    const auto record = content.resolve(AssetRef<SceneAsset>{roots.front()});
    check(record.state == AssetState::Available &&
              read(stage / "relocated" / record.record->source) == source,
          "Relocated content changed message selections or durable Scene IDs");
    check(!std::filesystem::exists(stage / "relocated/CodeExamples") &&
              read(gallery / source_path) == source,
          "Content export retained optional source code or rewrote Gallery");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Provide Gallery and build-owned scratch");
        prefab(std::filesystem::absolute(argv[1]));
        packaged(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
        std::cout << "Input Message Prefab inheritance/history/runtime and relocated content-only "
                     "export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
