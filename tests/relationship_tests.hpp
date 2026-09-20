#pragma once
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <set>
inline void test_relationship_depth() {
    using namespace forge;
    auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto chain = [](unsigned count) {
        auto doc = empty_scene();
        std::string parent;
        for (unsigned i = 0; i < count; ++i) {
            auto id = EntityId::generate().str();
            Json row{{"id", id}, {"name", "Node"}, {"components", Json::object()}};
            if (!parent.empty())
                row["parent"] = parent;
            doc["entities"].push_back(row);
            parent = id;
        }
        return doc;
    };
    EngineContext engine;
    Scene scene(engine.world());
    auto valid = chain(FLECS_DAG_DEPTH_MAX);
    scene.reset(valid);
    require(scene.entity_count() == FLECS_DAG_DEPTH_MAX,
            "Native supported hierarchy boundary was rejected");
    const auto before = scene.document();
    auto invalid = before;
    invalid["entities"].push_back({{"id", EntityId::generate()},
                                   {"name", "Too deep"},
                                   {"parent", invalid["entities"].back().at("id")},
                                   {"components", Json::object()}});
    bool rejected = false;
    try {
        scene.edit(invalid);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("Flecs limit") != std::string::npos;
    }
    require(rejected && scene.document() == before && !scene.can_undo(),
            "Over-depth hierarchy reached native realization or changed history");
    // The same source limit applies before structured native Parent realization.
    const auto prefab = create_prefab_source(scene, valid["entities"][0].at("id"));
    auto bad_prefab = prefab.source;
    bad_prefab["members"].push_back({{"id", PrefabMemberId::generate()},
                                     {"name", "Too deep"},
                                     {"parent", bad_prefab["members"].back().at("id")},
                                     {"components", Json::object()}});
    // Members are not stored in parent order. Explicitly find the actual leaf.
    std::set<std::string> parents;
    for (const auto& row : prefab.source.at("members"))
        if (row.contains("parent"))
            parents.insert(row.at("parent"));
    for (const auto& row : prefab.source.at("members"))
        if (!parents.contains(row.at("id")))
            bad_prefab["members"].back()["parent"] = row.at("id");
    rejected = false;
    try {
        PrefabDocument p(bad_prefab);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("Flecs limit") != std::string::npos;
    }
    require(rejected, "Over-depth prefab was not rejected before native compilation");
    scene.reset(empty_scene());
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    require(scene.entity_count() == FLECS_DAG_DEPTH_MAX && scene.entity(instance).is_alive(),
            "Supported structured Parent / IsA boundary did not instantiate");
    scene.undo();
    require(scene.entity_count() == 0, "Deep instance Undo did not retire its members");
    scene.redo();
    require(scene.entity_count() == FLECS_DAG_DEPTH_MAX,
            "Deep instance Redo did not restore its members");
    scene.reset(empty_scene());
    scene.publish_prefab_sources({}, [] {});
    // Inheritance has the same native DAG profile, even with a flat ChildOf tree.
    auto bases = chain(FLECS_DAG_DEPTH_MAX);
    for (auto& row : bases["entities"]) {
        row["prefab"] = true;
        if (row.contains("parent")) {
            row["base"] = row["parent"];
            row.erase("parent");
        }
    }
    scene.reset(bases);
    require(scene.entity_count() == FLECS_DAG_DEPTH_MAX, "Supported native IsA depth was rejected");
    auto over_bases = bases;
    over_bases["entities"].push_back({{"id", EntityId::generate()},
                                      {"name", "Too deep"},
                                      {"base", bases["entities"].back().at("id")},
                                      {"components", Json::object()}});
    rejected = false;
    try {
        scene.edit(over_bases);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("Flecs limit") != std::string::npos;
    }
    require(rejected && scene.document() == bases && !scene.can_undo(),
            "Over-depth IsA chain changed native state or history");
    // An instance's IsA link does not add another structural level. Native
    // instantiation starts at depth zero for the base (observable.c1447).
    auto legacy = chain(FLECS_DAG_DEPTH_MAX);
    for (auto& row : legacy["entities"])
        row["prefab"] = true;
    const auto legacy_root = legacy["entities"][0].at("id");
    const auto legacy_instance = EntityId::generate();
    legacy["entities"].push_back({{"id", legacy_instance},
                                  {"name", "Legacy instance"},
                                  {"base", legacy_root},
                                  {"components", Json::object()}});
    scene.reset(legacy);
    require(scene.entity(legacy_instance.str()).is_alive(),
            "IsA source link incorrectly consumed a structural expansion level");
    auto attached = legacy;
    const auto holder = EntityId::generate();
    attached["entities"].back()["parent"] = holder;
    attached["entities"].push_back(
        {{"id", holder}, {"name", "Holder"}, {"components", Json::object()}});
    rejected = false;
    try {
        scene.edit(attached);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("Flecs limit") != std::string::npos;
    }
    require(rejected && scene.document() == legacy && !scene.can_undo(),
            "Hidden expanded descendants exceeded the supported structural depth");
    // Large invalid input is handled iteratively, regardless of UUID sort order.
    rejected = false;
    try {
        Scene::validate_document(chain(10000));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Unbounded deep document reached recursive native admission");
    scene.reset(empty_scene());
    require(scene.entity_count() == 0, "Supported deep hierarchy did not retire");
}
