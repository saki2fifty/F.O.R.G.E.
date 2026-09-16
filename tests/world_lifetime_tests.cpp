#include <forge/authoring.hpp>
#include <forge/world.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#ifdef FORGE_WRAP_WORLD_INIT
static unsigned world_creations = 0;
extern "C" ecs_world_t* __real_ecs_init();
extern "C" ecs_world_t* __wrap_ecs_init() {
    ++world_creations;
    return __real_ecs_init();
}
#endif
namespace {
using forge::Json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json item(const char* id) {
    return {
        {"id", id},
        {"name", id},
        {"extra", {{"plugin_schema", 92}}},
        {"components",
         {{"forge.position", {{"x", 1}, {"y", 2}, {"z", 3}, {"future", Json::array({4, "keep"})}}},
          {"forge.rotation", {{"x", 10}, {"y", 20}, {"z", 30}}},
          {"forge.scale", {{"x", 1}, {"y", 2}, {"z", 3}}},
          {"forge.tint", {{"r", .25f}, {"g", .5f}, {"b", .75f}}},
          {"forge.primitive", {{"kind", 1}}},
          {"missing.plugin", {{"version", 99}, {"data", Json::array({1, nullptr, "opaque"})}}}}}};
}
Json fixture() {
    return {{"version", 1},
            {"unknown_envelope", {{"keep", true}}},
            {"entities", Json::array({item("a"), item("b"), item("untouched")})}};
}
const Json& by_id(const Json& doc, const std::string& id) {
    for (const auto& e : doc.at("entities"))
        if (e.at("id") == id)
            return e;
    throw std::runtime_error("Missing fixture entity");
}
void lifetime_and_failure() {
    static_assert(!std::is_default_constructible_v<forge::Scene>);
    forge::EngineContext engine;
    auto& context = engine.world();
    forge::Scene scene(context);
    scene.reset(fixture());
    auto* world = scene.world().c_ptr();
    const auto position_type = scene.world().id<forge::Position>();
    const auto member = scene.world().component<forge::Position>().lookup("x").id();
    const auto untouched = scene.entity("untouched").id();
    unsigned events = 0;
    auto observer = scene.world()
                        .observer<forge::Position>()
                        .event(flecs::OnSet)
                        .each([&](const forge::Position&) { ++events; });
    auto query = scene.world().query<const forge::Position>();
    auto system = scene.world().system<const forge::Position>().each([](const forge::Position&) {});
#ifdef FORGE_WRAP_WORLD_INIT
    const auto created = world_creations;
    require(created == 1, "World creation probe is not observing the context constructor");
#endif
    auto stable = [&] {
        require(scene.world().c_ptr() == world, "World replaced");
        require(observer.is_alive() && system.is_alive(), "World-lifetime registration lost");
        require(scene.world().id<forge::Position>() == position_type &&
                    scene.world().component<forge::Position>().lookup("x").id() == member,
                "Schema re-registered");
        require(scene.entity("untouched").id() == untouched, "Unrelated entity handle changed");
        require(query.count() > 0, "External query invalidated");
#ifdef FORGE_WRAP_WORLD_INIT
        require(world_creations == created, "Command created a validation/replacement world");
#endif
    };
    scene.rename_entity("a", "Renamed");
    stable();
    forge::authoring_command(
        scene, "property.set",
        {{"entity", "a"}, {"component", "forge.position"}, {"field", "x"}, {"value", 8}});
    stable();
    require(events == 1, "Typed patch touched an unchanged position");
    scene.reparent_entity("b", "a");
    stable();
    const auto copy = scene.duplicate_subtree("a");
    stable();
    scene.delete_subtree(copy);
    stable();
    require(scene.undo(), "Delete undo missing");
    stable();
    require(scene.redo(), "Delete redo missing");
    stable();
    const auto old_b = scene.entity("b").id();
    scene.delete_subtree("b");
    stable();
    require(scene.undo() && scene.entity("b").get<forge::StableId>().value == "b",
            "Undo lost v1 identity");
    require(scene.entity("b").id() != old_b, "Deleted handle generation was reused");
    stable();
    const auto snapshot = scene.document();
    const auto revision = scene.revision();
    const auto event_count = events;
    require(scene.can_redo(), "Failure test needs a redo branch");
    forge::AuthoringSession api(scene);
    const Json rename = {{"operation", "entity.rename"},
                         {"arguments", {{"entity", "a"}, {"name", "Never committed"}}}};
    const Json bad = {
        {"operation", "property.set"},
        {"arguments",
         {{"entity", "a"}, {"component", "forge.scale"}, {"field", "x"}, {"value", 0}}}};
    for (const auto& commands :
         {Json::array({rename, bad}),
          Json::array({rename,
                       {{"operation", "entity.reparent"},
                        {"arguments", {{"entity", "a"}, {"parent", "b"}}}}})}) {
        auto response = api.handle({{"api", 1},
                                    {"method", "scene.apply"},
                                    {"target", api.target()},
                                    {"expected_revision", revision},
                                    {"commands", commands}});
        require(!response.at("ok"), "Invalid prepared batch accepted");
        require(scene.document() == snapshot && scene.revision() == revision &&
                    events == event_count && scene.can_redo(),
                "Failed preparation leaked a mutation/event/history change");
        stable();
    }
    auto stale = api.handle({{"api", 1},
                             {"method", "scene.apply"},
                             {"target", api.target()},
                             {"expected_revision", revision - 1},
                             {"commands", Json::array({rename})}});
    require(!stale.at("ok") && scene.document() == snapshot && scene.revision() == revision &&
                events == event_count,
            "Stale batch changed state");
    require(scene.redo() && !scene.entity("b"), "Failed batches changed redo contents");
    require(scene.undo() && scene.document() == snapshot, "Failed batches changed undo contents");
    stable();
    // Content replacement can delete a parent while retaining its former child.
    auto replacement = fixture();
    scene.replace(replacement);
    stable();
    require(!scene.entity("b").target(flecs::ChildOf), "Replacement kept obsolete parent");
    {
        forge::Scene other(context);
        other.reset(fixture()); // v1 IDs are scoped to membership, not global strings.
        const auto other_b = other.entity("b").id();
        require(other_b != scene.entity("b").id(), "Scene memberships alias v1 IDs");
        scene.reset({{"version", 1}, {"entities", Json::array()}});
        require(other.entity("b").is_alive() && other.entity("b").id() == other_b &&
                    observer.is_alive(),
                "Unloading one scene deleted another or registration");
        other.translate(1, 0, 0);
        require(other.entity("a").get<forge::Position>().x == 2,
                "Scene-local runtime translate failed");
    }
    require(observer.is_alive() && scene.world().c_ptr() == world, "Scene destructor ended world");
    scene.reset(fixture());
    const auto rev_before_native = scene.revision();
    scene.entity("a")
        .set<forge::Position>({20, 21, 22})
        .set<forge::Rotation>({30, 31, 32})
        .set<forge::Scale>({4, 5, 6})
        .set<forge::Tint>({.1f, .2f, .3f})
        .set<forge::Primitive>({3})
        .set<forge::AuthoredName>({"Native name"})
        .child_of(scene.entity("b"));
    require(scene.revision() != rev_before_native,
            "Native writes do not invalidate authoring reads");
    const auto doc = scene.document();
    const auto& a = by_id(doc, "a");
    require(a.at("name") == "Native name" && a.at("parent") == "b" &&
                a["components"]["forge.position"]["x"] == 20 &&
                a["components"]["forge.rotation"]["y"] == 31 &&
                a["components"]["forge.scale"]["z"] == 6 &&
                a["components"]["forge.tint"]["r"] == Json(.1f) &&
                a["components"]["forge.primitive"]["kind"] == 3,
            "Known serialization ignored Flecs writes");
    require(a["components"]["forge.position"]["future"] ==
                fixture()["entities"][0]["components"]["forge.position"]["future"],
            "Known field merge lost opaque fields");
    scene.entity("a").remove<forge::Rotation>();
    require(!by_id(scene.document(), "a")["components"].contains("forge.rotation"),
            "Removed native component resurrected from JSON");
    const auto view =
        api.handle({{"api", 1}, {"method", "entity.query"}, {"target", api.target()}});
    require(view.at("ok") &&
                by_id(view.at("result"), "a")["components"]["forge.position"]["x"] == 20,
            "API read ignored native state");
    const auto unknown = by_id(scene.document(), "a");
    const auto duplicated = scene.duplicate_subtree("a");
    const auto dup = by_id(scene.document(), duplicated);
    require(dup["components"] == unknown["components"] && dup["extra"] == unknown["extra"],
            "Duplicate lost opaque data");
    scene.delete_subtree(duplicated);
    require(scene.undo() && by_id(scene.document(), duplicated) == dup,
            "Delete/undo lost opaque data");
    const auto round_trip = scene.document();
    const auto path = std::filesystem::current_path() / "world-lifetime.scene.json";
    scene.save(path);
    scene.reset({{"version", 1}, {"entities", Json::array()}});
    scene.load(path);
    std::filesystem::remove(path);
    require(scene.document() == round_trip, "Opaque/live merge does not round trip");
    const auto old_count = scene.entity_count();
    scene.entity("untouched").destruct();
    require(scene.entity_count() + 1 == old_count && !scene.entity("untouched"),
            "Native deletion left stale live identity/count");
    // The query RAII object leaves scope before Scene and EngineContext.
}
void prefabs() {
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    auto doc = fixture();
    doc["entities"][0]["prefab"] = true;
    doc["entities"][1]["base"] = "a";
    doc["entities"][1]["components"] = Json::object();
    scene.reset(doc);
    auto base = scene.entity("a");
    auto instance = scene.entity("b");
    base.set<forge::Scale>({7, 8, 9});
    require(by_id(scene.effective_document(), "b")["components"]["forge.scale"]["x"] == 7 &&
                !by_id(scene.document(), "b")["components"].contains("forge.scale"),
            "Inherited read became an owned save value");
    require(by_id(scene.effective_document(), "b")["components"]["forge.position"]["future"] ==
                doc["entities"][0]["components"]["forge.position"]["future"],
            "Inherited opaque field provenance lost");
    forge::apply_authoring(
        scene,
        Json::array(
            {{{"operation", "property.set"},
              {"arguments",
               {{"entity", "a"}, {"component", "forge.scale"}, {"field", "y"}, {"value", 12}}}},
             {{"operation", "property.set"},
              {"arguments",
               {{"entity", "b"}, {"component", "forge.scale"}, {"field", "x"}, {"value", 10}}}}}),
        scene.revision());
    require(instance.get<forge::Scale>().y == 12 && instance.owns<forge::Scale>(),
            "Batch intent lost pending inherited siblings");
    forge::authoring_command(scene, "component.revert",
                             {{"entity", "b"}, {"component", "forge.scale"}});
    require(!instance.owns<forge::Scale>() && instance.get<forge::Scale>().x == 7,
            "Revert failed to reveal live base");
    require(scene.entity("b").id() == instance.id(), "Prefab override replaced instance");
    // Existing ChildOf prefab children remain Flecs-generated runtime content.
    doc = scene.document();
    auto child = item("child");
    child["parent"] = "a";
    child["prefab"] = true;
    doc["entities"].push_back(child);
    scene.reset(doc);
    unsigned children = 0;
    instance.children([&](flecs::entity) { ++children; });
    require(children == 1, "Existing instance did not reconcile added prefab child");
    scene.rename_entity("untouched", "Unrelated");
    children = 0;
    instance.children([&](flecs::entity) { ++children; });
    require(children == 1, "Unrelated edit duplicated generated children");
    // A child can be implicitly prefab through ownership without writing a new
    // prefab declaration into its existing v1 file.
    auto implicit = scene.document();
    implicit["entities"].back().erase("prefab");
    scene.replace(implicit);
    require(!by_id(scene.document(), "child").contains("prefab"),
            "Implicit Prefab tag leaked into authored JSON");
    scene.rename_entity("untouched", "Another unrelated edit");
    require(scene.entity("child").has(flecs::Prefab),
            "Ordinary edit stripped implicit prefab child flag");
    scene.delete_subtree("child");
    children = 0;
    instance.children([&](flecs::entity) { ++children; });
    require(children == 0, "Removed prefab child left stale generated content");
}
struct HookValue {
    int value = 0;
};
struct Services {
    bool alive = true;
    unsigned removed = 0, finalized = 0;
};
void shutdown() {
    Services services;
    std::weak_ptr<int> callback_state;
    {
        forge::EngineContext engine;
        auto& world = engine.world().world();
        world.component<HookValue>().on_remove([&](flecs::entity, HookValue&) {
            require(services.alive, "Hook executed after service/code lifetime ended");
            ++services.removed;
        });
        auto state = std::make_shared<int>(7);
        callback_state = state;
        world.observer<HookValue>().event(flecs::OnSet).each([state](const HookValue&) {
            require(*state == 7, "Callback context corrupted");
        });
        world.atfini(
            [](ecs_world_t*, void* ptr) {
                auto& s = *static_cast<Services*>(ptr);
                require(s.alive, "World finalized after code/services");
                ++s.finalized;
            },
            &services);
        world.entity().set<HookValue>({1});
        {
            forge::Scene scene(engine.world());
            scene.reset(fixture());
            scene.entity("a").set<HookValue>({2});
        }
        require(services.removed == 1 && !callback_state.expired(),
                "Scene unload destroyed world callback context");
    }
    require(services.removed == 2 && services.finalized == 1 && callback_state.expired(),
            "World teardown did not retire hooks/contexts before services");
    services.alive = false;
}
} // namespace
int main() {
    try {
        lifetime_and_failure();
        prefabs();
        shutdown();
        std::cout
            << "persistent world, authority, preparation, membership, prefab and shutdown passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
