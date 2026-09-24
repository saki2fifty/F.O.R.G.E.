// Acceptance content assembly through production scene, placement and bake services.
#include "../samples/reference_game/ids.hpp"
#include "model_placement.hpp"
#include "runtime_dependencies.hpp"
#include <forge/audio_components.hpp>
#include <forge/character_components.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_settings.hpp>
#include <forge/navigation_build.hpp>
#include <forge/project.hpp>
#include <forge/render_components.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("Need project, navigation worker and sample source");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto source = std::filesystem::absolute(argv[3]);
        WorldContext world;
        Scene scene(world);
        scene.load(root / "main.scene.json");
        auto data = scene.snapshot();
        data["asset_id"] = reference::level_scene;
        data.erase("legacy_ids");
        std::erase_if(data["entities"].get_ref<Json::array_t&>(),
                      [](const auto& e) { return e.at("name") == "Character reference envelope"; });
        for (auto& e : data["entities"]) {
            if (e.at("name") == "Character")
                e["id"] = reference::player_id;
            if (e.at("name") == "Moving platform")
                e["id"] = reference::platform_id;
        }
        auto add = [&](const char* id, const char* name) {
            data["entities"].push_back({{"id", id},
                                        {"name", name},
                                        {"components", Json::object()},
                                        {"spatial", {{"mode", "world"}}}});
        };
        add(reference::camera_id, "Player camera");
        add(reference::ui_id, "Game UI");
        add(reference::switch_id, "Beacon");
        add(reference::door_id, "Exit door");
        const auto light_id = EntityId::generate().str(), npc_id = EntityId::generate().str();
        const auto speaker_id = EntityId::generate().str();
        add(light_id.c_str(), "Sun");
        add(npc_id.c_str(), "Navigation guide");
        add(speaker_id.c_str(), "Beacon audio");
        scene.restore_snapshot(data);
        for (const auto& e : data["entities"]) {
            const auto entity = scene.entity(e.at("id"));
            entity.set<Primitive>({no_primitive});
            if (entity.has<PhysicsBody>() && !entity.get<PhysicsBody>().sensor) {
                entity.set<MeshRenderer>({engine_primitive(0), {{"surface", engine_material()}}});
                entity.set<Primitive>({0}); // Navigation's authored box recipe.
                if (entity.get<PhysicsBody>().motion == 0)
                    entity.set<NavigationSurface>({true});
            }
        }
        auto character = scene.entity(reference::player_id).get<CharacterController>();
        character.layer = 1;
        scene.entity(reference::player_id).set(character).set<LocalTranslation>({0, .1, -6});
        scene.entity(reference::camera_id)
            .set<Camera>({})
            .set<LocalTranslation>({0, 1.6, -6})
            .set<AudioListener>({});
        Light sun;
        sun.intensity = 3;
        scene.entity(light_id).set<LocalTranslation>({}).set(sun).set<LocalRotation>(
            {-.3f, .2f, 0, .9327379f});
        for (auto [id, x] : {std::pair{reference::switch_id, 0.0}, {reference::door_id, 3.0}})
            scene.entity(id)
                .set<LocalTranslation>({x, 1.5, -4})
                .set<PhysicsBody>({})
                .set<BoxCollider>({1, 1, 1})
                .set<MeshRenderer>({engine_primitive(0), {{"surface", engine_material()}}})
                .set<LocalScale>({.5f, .5f, .5f});
        // A UI asset is shared; authored entity IDs are unique across logical scenes.
        std::filesystem::copy_file(source / "reference.rml", root / "reference.rml",
                                   std::filesystem::copy_options::overwrite_existing);
        auto ui = register_ui_document(root, "reference.rml");
        scene.entity(reference::ui_id).set<UiDocument>({{ui.id}});
        auto catalog = AssetCatalog::open_project(root);
        for (const auto& [id, asset] : catalog.records()) {
            if (asset.type == AudioClipAsset::type) {
                AudioSource sound;
                sound.clip.id = id;
                sound.play_on_start = true;
                sound.loop = true;
                sound.spatialized = true;
                scene.entity(speaker_id).set(sound).set<LocalTranslation>({0, 1, -4});
            }
            if (asset.type == ModelAsset::type && asset.source == "Actor/SimpleSkin.gltf") {
                asset_detail::ModelPlacementOptions options;
                options.name = "Animated flag";
                options.transform.translation = {-3, 0, -3};
                options.transform.rotation = {0, 1, 0, 0};
                for (const auto& [member, record] : catalog.records())
                    if (record.type == AnimationClipAsset::type && record.subasset &&
                        record.subasset->owner == id)
                        options.clip.id = member;
                const auto selected = asset_detail::load_model_selection(root, catalog, id);
                auto candidate = asset_detail::prepare_model_placement(selected, scene.asset_id(),
                                                                       scene.revision(), options);
                asset_detail::instantiate_model(scene, catalog, candidate);
            }
        }
        scene.save(root / "level.scene.json");
        auto navigation = prepare_navigation(root, scene.effective_document(), {},
                                             std::filesystem::absolute(argv[2]));
        const auto nav = navigation.publish(scene.effective_document());
        scene.entity(npc_id)
            .set<LocalTranslation>({6, .05, -6})
            .set<NavigationAgent>({{nav.id}, true, true, 1, .2f, 6, 0, 6})
            .set<MeshRenderer>({engine_primitive(0), {{"surface", engine_material()}}})
            .set<LocalScale>({.4f, 1, .4f});
        scene.save(root / "level.scene.json");
        Scene menu(world);
        const auto menu_ui = EntityId::generate(), menu_camera = EntityId::generate();
        menu.replace({{"version", 3},
                      {"asset_id", reference::menu_scene},
                      {"entities", Json::array({{{"id", menu_ui},
                                                 {"name", "Main menu UI"},
                                                 {"components", Json::object()}},
                                                {{"id", menu_camera},
                                                 {"name", "Menu background"},
                                                 {"components", Json::object()}}})}});
        menu.entity(menu_ui.str()).set<UiDocument>({{ui.id}});
        menu.entity(menu_camera.str())
            .set<LocalTranslation>({})
            .set<Camera>({})
            .set<Primitive>({no_primitive});
        menu.save(root / "menu.scene.json");
        catalog = AssetCatalog::open_project(root);
        catalog.add_scene("level.scene.json");
        catalog.add_scene("menu.scene.json");
        catalog.save(AssetCatalog::project_index(root));
        ProjectLease lease(root);
        declare_runtime_dependencies(lease, AssetId::parse(reference::menu_scene),
                                     {{AssetId::parse(reference::level_scene),
                                       "scene",
                                       AssetDependencyKind::Runtime,
                                       "declared:reference gameplay scene",
                                       {}}},
                                     catalog.document());
        auto project = ProjectSettings::defaults("FORGE Field Test");
        project["startup_scene"] = {{"asset", reference::menu_scene},
                                    {"source", "menu.scene.json"}};
        project["physics"]["layers"][1] = "Player";
        std::ifstream input(source / "input.json");
        input >> project["input"];
        project["game"] = default_game_settings("org.forge.reference-" + AssetId::generate().str(),
                                                "FORGE Field Test");
        atomic_write(root / "forge.project.json", project.dump(2));
        std::cout << root.string() << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
