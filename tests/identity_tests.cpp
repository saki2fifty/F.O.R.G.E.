#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace forge;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Expected rejection");
}
Json read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    return Json::parse(stream);
}
Json legacy() {
    return {
        {"version", 1},
        {"future", {{"entity", "entity-4"}}},
        {"entities",
         Json::array(
             {{{"id", "root"}, {"name", "Group"}, {"components", Json::object()}},
              {{"id", "prototype"},
               {"name", "Base"},
               {"parent", "root"},
               {"prefab", true},
               {"components",
                {{"forge.position", {{"x", 1}, {"y", 2}, {"z", 3}, {"unknown", "keep"}}}}}},
              {{"id", "instance"},
               {"name", "Instance"},
               {"parent", "root"},
               {"base", "prototype"},
               {"components", {{"missing.plugin", {{"entity", "prototype"}, {"schema", 97}}}}}}})}};
}
int main() {
    try {
        static_assert(sizeof(EntityId) == 16 && sizeof(AssetId) == 16);
        static_assert(!std::is_convertible_v<EntityId, AssetId>);
        const auto example = EntityId::parse("919108f7-52d1-4320-9bac-f847db4148a8");
        require(example.str() == "919108f7-52d1-4320-9bac-f847db4148a8",
                "UUID bytes/text mismatch");
        for (const auto* text :
             {"", "919108F7-52d1-4320-9bac-f847db4148a8", "00000000-0000-0000-0000-000000000000",
              "919108f7-52d1-5320-9bac-f847db4148a8", "919108f7-52d1-4320-7bac-f847db4148a8"})
            rejects([&] { (void)EntityId::parse(text); });
        std::set<EntityId> ids;
        for (int i = 0; i < 4096; ++i) {
            auto id = EntityId::generate();
            require(ids.insert(id).second && Json(id).get<EntityId>() == id,
                    "UUID generation/round trip");
        }
        const auto v1 = legacy();
        for (const auto version :
             {Json(1.5), Json(2.5), Json(3), Json("2"), Json(4294967297ULL), Json(4294967298ULL)}) {
            auto bad = v1;
            bad["version"] = version;
            rejects([&] { (void)migrate_scene(bad); });
        }
        const auto migrated = migrate_scene(v1);
        require(migrated.at("version") == 3 && migrate_scene(migrated) == migrated,
                "Migration idempotence");
        require(migrate_scene(v1, &migrated) == migrated, "Retained assignment changed");
        require(migrated["future"] == v1["future"] &&
                    migrated["entities"][2]["components"] == v1["entities"][2]["components"],
                "Opaque payload rewritten");
        require(migrated["entities"][1]["components"]["forge.local_translation"] ==
                    v1["entities"][1]["components"]["forge.position"],
                "Transform or unknown field changed");
        require(migrated["entities"][2]["base"] == migrated["entities"][1]["id"],
                "Known base not migrated");
        EngineContext engine;
        auto& world = engine.world();
        Scene scene(world);
        scene.reset(migrated);
        const auto root = scene.reference("root"), instance = scene.reference("instance");
        require(Json(instance).get<EntityRef>() == instance, "Typed reference round trip");
        require(world.resolve(instance).entity == scene.entity("instance").id(), "World resolver");
        require(world.reference(scene.entity("instance").id()) == instance, "Reverse resolver");
        require(scene.entity("instance").owns<PersistentEntityId>() &&
                    scene.reference("prototype") != instance,
                "Instance copied prefab identity");
        auto generated = scene.world().entity().is_a(scene.entity("prototype"));
        require(!generated.has<PersistentEntityId>(),
                "Prototype identity inherited by native instance");
        generated.destruct();
        auto pointer = scene.world().c_ptr();
        auto handle = scene.entity("instance").id();
        scene.rename_entity("instance", "Renamed");
        scene.reparent_entity("instance", "");
        authoring_command(scene, "transform.position",
                          {{"entity", "instance"}, {"value", {{"x", 9}, {"y", 8}, {"z", 7}}}});
        require(scene.reference("instance") == instance && scene.world().c_ptr() == pointer &&
                    scene.entity("instance").id() == handle,
                "Ordinary edits replaced identity/world");
        scene.delete_subtree("instance");
        require(world.resolve(instance).state == WorldContext::ResolveState::Missing,
                "Deleted reference silently rebound");
        require(scene.undo() && scene.reference("instance") == instance &&
                    scene.entity("instance").id() != handle,
                "Undo logical identity");
        scene.reset(migrated);
        const auto before = scene.document();
        const auto duplicate = scene.duplicate_subtree("root");
        const auto after = scene.document();
        require(after["entities"].size() == 6 && duplicate != root.entity.str(),
                "Subtree identities copied");
        std::set<std::string> copied;
        for (const auto& entity : after["entities"])
            require(copied.insert(entity.at("id")).second, "Duplicate ID collision");
        require(after["entities"][5]["base"] == after["entities"][4]["id"] &&
                    after["entities"][5]["parent"] == duplicate,
                "Subtree references not remapped");
        require(after["entities"][5]["components"] == before["entities"][2]["components"],
                "Subtree changed unknown internals");
        require(scene.undo() && scene.document() == before && scene.redo() &&
                    scene.document() == after,
                "Duplicate history IDs changed");
        auto copy = duplicate_scene_asset(before);
        require(copy.at("asset_id") != before.at("asset_id"), "Scene copy reused AssetId");
        for (std::size_t i = 0; i < 3; ++i)
            require(copy["entities"][i]["id"] != before["entities"][i]["id"],
                    "Scene copy reused EntityId");
        require(copy["entities"][2]["components"] == before["entities"][2]["components"] &&
                    copy["future"] == before["future"],
                "Scene copy changed opaque data");
        require(copy["entities"][2]["base"] == copy["entities"][1]["id"],
                "Scene copy base reference");
        const std::map<EntityId, EntityId> remap{
            {root.entity, copy["entities"][0]["id"].get<EntityId>()}};
        const auto copy_asset = copy.at("asset_id").get<AssetId>();
        require(remap_entity_ref(root, root.scene, copy_asset, remap) ==
                    EntityRef{copy_asset, copy["entities"][0]["id"].get<EntityId>()},
                "Known EntityRef remap");
        require(remap_entity_ref(instance, root.scene, copy_asset, remap) == instance,
                "External-to-set reference changed");
        const EntityRef other_scene{AssetId::generate(), root.entity};
        require(remap_entity_ref(other_scene, root.scene, copy_asset, remap) == other_scene,
                "Cross-scene reference changed");
        const auto missing = EntityRef{scene.asset_id(), EntityId::generate()};
        require(world.resolve(missing).state == WorldContext::ResolveState::Missing,
                "Missing target guessed");
        require(world.resolve({AssetId::generate(), instance.entity}).state ==
                    WorldContext::ResolveState::Unresolved,
                "Unloaded scene guessed");
        {
            Scene second(world);
            second.reset(before);
            require(world.resolve(instance).state == WorldContext::ResolveState::Ambiguous,
                    "Repeated loaded asset silently bound");
            require(world.resolve(instance, second.membership()).entity ==
                        second.entity("instance").id(),
                    "Explicit instance scope failed");
        }
        require(world.resolve(instance).state == WorldContext::ResolveState::Available,
                "Unload destroyed another instance");
        const auto folder =
            std::filesystem::current_path() / ("identity-test-" + AssetId::generate().str());
        std::filesystem::create_directory(folder);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{folder};
        const auto path = folder / "original.scene.json";
        const auto original_bytes = v1.dump(4);
        atomic_write(path, original_bytes);
        const auto first = read_scene_file(path);
        require(read(path) == v1 && first == read_scene_file(path),
                "Open replaced source or regenerated UUIDs");
        Scene reopened(world);
        reopened.load(path);
        require(reopened.document() == first, "Reopen changed assigned identity");
        const auto backup_pending = path.string() + ".v1.backup.pending";
        std::filesystem::create_directory(backup_pending);
        rejects([&] { write_scene_file(path, first); });
        require(read(path) == v1 && !std::filesystem::exists(path.string() + ".v1.backup"),
                "Failed backup publication damaged source or left partial final backup");
        std::filesystem::remove(backup_pending);
        // Failed replacement preserves original source and backup; retry uses the same assignment.
#ifdef _WIN32
        const auto locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Windows lock fixture failed");
        rejects([&] { write_scene_file(path, first); });
        CloseHandle(locked);
#else
        std::filesystem::create_directory(path.string() + ".pending");
        rejects([&] { write_scene_file(path, first); });
        std::filesystem::remove(path.string() + ".pending");
#endif
        require(read(path) == v1 && read(path.string() + ".v1.backup") == v1,
                "Failed migration destroyed original/backup");
        require(read_scene_file(path) == first, "Failed migration retry changed IDs");
        write_scene_file(path, first);
        require(read_scene_file(path) == first && read(path.string() + ".v1.backup") == v1,
                "Saved migration lost identity/backup");
        write_scene_file(path, first);
        {
            std::ifstream backup(path.string() + ".v1.backup", std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(backup), {}};
            require(bytes == original_bytes, "Backup changed original formatting/bytes");
        }
        const auto moved = folder / "renamed.scene.json";
        std::filesystem::rename(path, moved);
        require(read_scene_file(moved) == first, "Move changed scene identity");
        AssetCatalog assets(folder);
        auto record = assets.add_scene("renamed.scene.json");
        AssetRef<SceneAsset> ref{record.id};
        require(Json(ref).get<AssetRef<SceneAsset>>() == ref, "AssetRef round trip");
        require(assets.resolve(ref).state == AssetState::Available, "Scene asset unavailable");
        require(assets.resolve(AssetRef<PrefabAsset>{record.id}).state == AssetState::Incompatible,
                "Asset type mismatch accepted");
        require(assets.resolve(AssetRef<SceneAsset>{AssetId::generate()}).state ==
                    AssetState::Unresolved,
                "Unknown asset guessed");
        const auto index = folder / "assets.json";
        assets.save(index);
        AssetCatalog restored(folder);
        restored.load(index);
        require(restored.resolve(ref).state == AssetState::Available,
                "Asset metadata lost identity");
        const auto renamed = folder / "new-name.scene.json";
        std::filesystem::rename(moved, renamed);
        require(restored.resolve(ref).state == AssetState::Missing, "Missing source bound by name");
        restored.relocate(record.id, "new-name.scene.json");
        require(restored.resolve(ref).state == AssetState::Available, "Relocation lost identity");
        write_scene_file(moved, duplicate_scene_asset(first));
        require(assets.resolve(ref).state == AssetState::Incompatible,
                "Different asset at same path silently bound");
        rejects([&] { restored.add_scene("new-name.scene.json"); });
        rejects([&] { restored.relocate(record.id, "../outside.json"); });
        auto bad_index = read(index);
        bad_index["assets"].push_back(bad_index["assets"][0]);
        atomic_write(index, bad_index.dump());
        rejects([&] { restored.load(index); });
        require(restored.resolve(ref).state == AssetState::Available,
                "Bad metadata load destroyed usable catalog");
        atomic_write(path, v1.dump());
        auto external = v1;
        external["entities"][0]["name"] = "Changed externally";
        atomic_write(path, external.dump());
        rejects([&] { (void)read_scene_file(path); });
        require(read(path) == external, "Identity conflict rewrote source");
        auto invalid = before;
        invalid["entities"][1]["id"] = invalid["entities"][0]["id"];
        const auto unchanged = scene.document();
        const auto revision = scene.revision();
        rejects([&] { scene.edit(invalid); });
        require(scene.document() == unchanged && scene.revision() == revision,
                "Invalid identity leaked mutation");
        std::cout << "UUID, references, world scopes, migration recovery, duplication and asset "
                     "metadata passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
