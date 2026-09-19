#pragma once
#include <forge/authoring.hpp>
#include <stdexcept>
inline void test_ordered_hierarchy() {
    using namespace forge;
    auto check = [](bool ok, const char* text) {
        if (!ok)
            throw std::runtime_error(text);
    };
    EngineContext engine;
    Scene scene(engine.world());
    Json rows = Json::array();
    for (const char* id : {"z", "a", "m", "child-z", "child-a"}) {
        Json row = {{"id", id}, {"name", id}, {"components", Json::object()}};
        if (std::string(id).starts_with("child"))
            row["parent"] = "z";
        rows.push_back(row);
    }
    scene.reset({{"version", 1}, {"entities", rows}});
    const auto original = scene.document();
    const auto a = scene.canonical_id("a"), z = scene.canonical_id("z");
    authoring_command(scene, "entity.reorder", {{"entity", a}, {"before", z}});
    auto ordered = scene.document();
    check(ordered["entities"][0]["id"] == a && ordered["entities"][1]["id"] == z,
          "Root sibling order did not change");
    check(scene.entity(a).parent().id() == scene.membership(), "Root order not owned by Flecs");
    const auto children = ecs_get_ordered_children(scene.world(), scene.membership());
    check(children.count == 3 && children.ids[0] == scene.entity(a).id(),
          "Persisted order does not match native OrderedChildren");
    check(scene.undo() && scene.document() == original, "Reorder undo mismatch");
    check(scene.redo() && scene.document() == ordered, "Reorder redo mismatch");
    authoring_command(scene, "entity.reorder", {{"entity", "child-a"}, {"before", "child-z"}});
    ordered = scene.document();
    check(ordered["entities"][3]["id"] == scene.canonical_id("child-a"), "Nested reorder failed");
    const auto before = scene.document();
    try {
        authoring_command(scene, "entity.reorder", {{"entity", "child-a"}, {"before", a}});
        throw std::logic_error("Cross-parent reorder accepted");
    } catch (const std::runtime_error&) {
    }
    check(scene.document() == before, "Rejected reorder changed scene");
    Scene restored(engine.world());
    restored.reset(before);
    check(restored.document() == before, "Sibling order did not survive reconstruction");
    auto parent = scene.entity(z);
    auto native = ecs_get_ordered_children(scene.world(), parent);
    std::vector<ecs_entity_t> reversed{native.ids[1], native.ids[0]};
    const auto before_native_order = scene.revision();
    ecs_set_child_order(scene.world(), parent, reversed.data(), 2);
    check(scene.revision() != before_native_order, "Native order did not invalidate UI revision");
    check(scene.document()["entities"][3]["id"] == scene.canonical_id("child-z"),
          "Serialization ignored native reorder");
    authoring_command(scene, "entity.reorder", {{"entity", "a"}, {"before", ""}});
    const auto final_order = ecs_get_ordered_children(scene.world(), scene.membership());
    check(final_order.ids[final_order.count - 1] == scene.entity(a).id(), "Move to end failed");
}
