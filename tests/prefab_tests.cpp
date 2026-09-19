#include <algorithm>
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace forge;
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
Json& row(Json& d, const std::string& id) {
    for (auto& r : d["entities"])
        if (r["id"] == id)
            return r;
    throw std::runtime_error("missing row");
}
Json fixture() {
    auto root = PrefabMemberId::generate(), child = PrefabMemberId::generate();
    Json c = {{"forge.local_translation", {{"x", 0}, {"y", 1}, {"z", 0}}},
              {"forge.local_rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}},
              {"forge.local_scale", {{"x", 1}, {"y", 1}, {"z", 1}}},
              {"forge.tint", {{"r", .2f}, {"g", .6f}, {"b", .7f}}}};
    return {
        {"format", "forge.prefab"},
        {"version", 1},
        {"asset_id", AssetId::generate()},
        {"revision", 1u},
        {"root", root},
        {"members",
         Json::array({{{"id", root}, {"name", "Root"}, {"components", c}},
                      {{"id", child}, {"name", "Child"}, {"parent", root}, {"components", c}}})}};
}
std::string instance(Scene& s, const PrefabDocument& p) {
    auto d = s.document();
    d["version"] = 4;
    const auto id = EntityId::generate().str();
    d["entities"].push_back({{"id", id},
                             {"name", "Instance"},
                             {"components", Json::object()},
                             {"prefab_instance",
                              {{"asset", p.asset()},
                               {"revision", p.revision()},
                               {"members", {{p.root().str(), id}}}}}});
    s.edit(reconcile_prefab_intent(d, s.prefab_sources()));
    return id;
}
void files_and_publication(const std::filesystem::path& base) {
    const auto folder = base / AssetId::generate().str();
    std::filesystem::create_directories(folder);
    WorldContext world;
    Scene scene(world);
    auto original = authoring_command(scene, "entity.create", {{"name", "Source"}})
                        .at("selected")
                        .get<std::string>();
    auto child = authoring_command(scene, "entity.create", {{"name", "Child"}})
                     .at("selected")
                     .get<std::string>();
    scene.reparent_entity(child, original, ReparentMode::KeepLocal);
    const auto authored = scene.document();
    PrefabLibrary library(folder);
    auto definition = create_prefab_source(scene, original);
    const auto asset = library.create(scene, definition, "Original.prefab.json");
    check(scene.document() == authored, "Create changed original scene");
    check(library.source(asset) == definition.source, "source save/reopen");
    auto a = instantiate_prefab(scene, asset);
    auto doc = scene.document();
    const std::string root_member = definition.root().str();
    std::string member;
    for (const auto& m : definition.source["members"])
        if (m["id"] != root_member)
            member = m["id"];
    const std::string ac = row(doc, a)["prefab_instance"]["members"][member];
    const auto ref = scene.reference(ac);
    const PrefabMemberRef known{asset, PrefabMemberId::parse(member)};
    check(Json(known).get<PrefabMemberRef>() == known, "typed member reference codec");
    check(prefab_member_reference(scene.document(), EntityId::parse(a), known) == ref,
          "known member reference resolution");
    const auto new_asset = AssetId::generate();
    const auto new_member = PrefabMemberId::generate();
    const auto remapped =
        remap_prefab_member_reference(known, asset, new_asset, {{known.member, new_member}});
    check(remapped.prefab == new_asset && remapped.member == new_member, "known reference remap");
    auto dynamic = authoring_command(scene, "entity.create", {{"name", "Dynamic"}})
                       .at("selected")
                       .get<std::string>();
    scene.reparent_entity(dynamic, ac, ReparentMode::KeepLocal);
    const auto dynamic_handle = scene.entity(dynamic).id(),
               unrelated_handle = scene.entity(original).id();
    auto expected = library.source(asset), next = expected;
    next["members"][0]["name"] = "Renamed";
    std::reverse(next["members"].begin(), next["members"].end());
    library.publish(scene, expected, next);
    check(world.resolve(ref, scene.membership()).state == WorldContext::ResolveState::Available,
          "rename invalidated EntityRef");
    check(scene.entity(dynamic).id() == dynamic_handle &&
              scene.entity(dynamic).parent() == scene.entity(ac),
          "dynamic attachment lost across publish");
    check(scene.entity(original).id() == unrelated_handle, "unrelated handle changed");
    check(!scene.can_undo() && !scene.can_redo(), "source history boundary unclear");
    scene.rename_entity(a, "Undo remains on failure");
    auto before = scene.snapshot();
    auto revision = scene.revision();
    auto handle = scene.entity(ac).id();
    auto failed = library.source(asset);
    failed["revision"] = failed["revision"].get<std::uint64_t>() + 1;
    failed["members"][0]["name"] = "Must not publish";
    bool rejected = false;
    try {
        scene.publish_prefab_sources({{asset, failed}}, [] {
            throw std::runtime_error("injected disk replacement failure");
        });
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.snapshot() == before && scene.revision() == revision &&
              scene.entity(ac).id() == handle && scene.can_undo(),
          "failed writer changed live state/history/handles");
    auto source_before = library.source(asset);
    auto invalid = source_before;
    invalid["members"][0]["components"]["forge.local_scale"] = {{"x", 0}, {"y", 1}, {"z", 1}};
    rejected = false;
    try {
        library.publish(scene, source_before, invalid);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && library.source(asset) == source_before && scene.snapshot() == before,
          "invalid source published");
    // A real filesystem obstruction must preserve the original usable asset.
    auto pending = folder / "Original.prefab.json.pending";
    std::filesystem::create_directory(pending);
    std::ofstream(pending / "block") << "occupied";
    auto valid = source_before;
    valid["members"][0]["name"] = "Blocked save";
    rejected = false;
    try {
        library.publish(scene, source_before, valid);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.snapshot() == before && scene.entity(ac).id() == handle &&
              library.source(asset) == source_before,
          "filesystem failure lost old revision");
    std::filesystem::remove(pending / "block");
    std::filesystem::remove(pending);
    const auto added = PrefabMemberId::generate().str();
    next = source_before;
    next["members"].push_back(
        {{"id", added},
         {"parent", root_member},
         {"name", "Added"},
         {"components", {{"forge.local_translation", {{"x", 0}, {"y", 4}, {"z", 0}}}}}});
    library.publish(scene, source_before, next);
    doc = scene.document();
    check(row(doc, a)["prefab_instance"]["members"][member] == ac,
          "added member changed old mapping");
    const std::string added_id = row(doc, a)["prefab_instance"]["members"][added];
    check(scene.entity(added_id), "new member not realized");
    expected = library.source(asset);
    next = expected;
    auto kept = Json::array();
    for (const auto& m : next["members"])
        if (m["id"] != member)
            kept.push_back(m);
    next["members"] = kept;
    library.publish(scene, expected, next);
    check(!scene.entity(ac) &&
              world.resolve(ref, scene.membership()).state != WorldContext::ResolveState::Available,
          "removed member retargeted reference");
    check(scene.entity(dynamic).id() == dynamic_handle,
          "dynamic child deleted with removed source member");
    world.evaluate_world_transforms();
    check(!scene.entity(dynamic).get<WorldTransform>().resolved,
          "attachment to removed member falsely resolved");
    auto projected = scene.preview_document(scene.document());
    check(!row(projected, dynamic).value("spatial_resolved", true),
          "detached projection falsely resolved missing structural target");
    check(scene.entity(added_id), "unrelated member removed");
    doc = scene.document();
    check(row(doc, ac).value("missing_member", false), "removed intent missing");
    expected = library.source(asset);
    next = expected;
    for (const auto& m : source_before.at("members"))
        if (m.at("id") == member)
            next["members"].push_back(m);
    library.publish(scene, expected, next);
    world.evaluate_world_transforms();
    check(world.resolve(ref, scene.membership()).state == WorldContext::ResolveState::Available &&
              scene.entity(dynamic).id() == dynamic_handle &&
              scene.entity(dynamic).parent() == scene.entity(ac) &&
              scene.entity(dynamic).get<WorldTransform>().resolved,
          "Restoring the same member identity did not reconnect references/dynamic attachments");
    auto duplicated = library.duplicate(scene, asset, "Copy.prefab.json");
    check(duplicated != asset, "duplicate asset reused identity");
    const auto copy_source = library.source(duplicated);
    check(copy_source["root"] != expected["root"], "duplicate member ids reused");
    const auto before_move = scene.document();
    std::filesystem::rename(folder / "Original.prefab.json", folder / "Moved.prefab.json");
    library.refresh(scene);
    check(library.source(asset)["asset_id"].get<AssetId>() == asset &&
              scene.document() == before_move,
          "asset move changed identity");
    const auto saved = scene.document();
    scene.save(folder / "test.scene.json");
    {
        WorldContext other;
        Scene reopened(other);
        PrefabLibrary reopened_library(folder);
        reopened_library.load_scene(reopened, read_scene_file(folder / "test.scene.json"));
        check(reopened.document() == saved, "scene reopen lost mappings/intent");
    }
    std::filesystem::rename(folder / "Moved.prefab.json", folder / "Unavailable.bin");
    library.refresh(scene);
    doc = scene.document();
    check(row(doc, a)["prefab_instance"]["status"] == "missing asset", "missing asset diagnosis");
    std::filesystem::rename(folder / "Unavailable.bin", folder / "Moved.prefab.json");
    library.refresh(scene);
    check(scene.entity(added_id), "restored asset failed to reconnect stable member");
    auto whole = duplicate_scene_asset(scene.document());
    check(whole["asset_id"].get<AssetId>() != scene.asset_id(), "scene duplication asset identity");
    auto old = scene.document();
    std::set<std::string> old_ids;
    for (const auto& e : old["entities"])
        old_ids.insert(e.at("id"));
    for (const auto& e : whole["entities"])
        check(!old_ids.contains(e.at("id")), "scene duplication entity identity");
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        std::filesystem::remove(entry.path());
    std::filesystem::remove(folder);
}
void many_and_transforms() {
    WorldContext world;
    Scene scene(world);
    auto source = fixture();
    auto p = PrefabDocument(source);
    scene.set_prefab_sources({{p.asset(), source}});
    std::vector<std::string> instances;
    std::set<ecs_table_t*> tables;
    std::set<std::string> ids;
    const std::string member = source["members"][1]["id"];
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 40; ++i) {
        auto root = instantiate_prefab(scene, p.asset());
        instances.push_back(root);
        auto doc = scene.document();
        const std::string child = row(doc, root)["prefab_instance"]["members"][member];
        check(ids.insert(child).second, "instance EntityIds repeated");
        tables.insert(ecs_get_table(world.world().c_ptr(), scene.entity(child).id()));
    }
    check(tables.size() == 1, "Parent member tables exploded per instance");
    auto doc = scene.document();
    const std::string child = row(doc, instances[0])["prefab_instance"]["members"][member];
    authoring_command(scene, "transform.position",
                      {{"entity", instances[0]}, {"value", {{"x", 10}, {"y", 2}, {"z", 0}}}});
    authoring_command(scene, "transform.scale",
                      {{"entity", instances[0]}, {"value", {{"x", 2}, {"y", 3}, {"z", 4}}}});
    world.evaluate_world_transforms();
    check(scene.entity(child).get<WorldTransform>().affine.m[7] == 5,
          "Parent world transform incorrect");
    authoring_command(scene, "transform.rotation",
                      {{"entity", child}, {"value", {{"x", 0}, {"y", 0}, {"z", 0}}}});
    check(scene.entity(child).owns<LocalRotation>() &&
              !scene.entity(child).owns<LocalTranslation>() &&
              !scene.entity(child).owns<LocalScale>(),
          "rotation made unrelated overrides");
    authoring_command(scene, "transform.scale",
                      {{"entity", child}, {"value", {{"x", 1}, {"y", 1}, {"z", 1}}}});
    auto changed = source;
    changed["revision"] = 2u;
    changed["members"][1]["components"]["forge.local_scale"]["y"] = 2;
    const auto rotation = rotation_from_euler({10, 20, 30});
    changed["members"][1]["components"]["forge.local_rotation"] = {
        {"x", rotation.x}, {"y", rotation.y}, {"z", rotation.z}, {"w", rotation.w}};
    scene.set_prefab_sources({{p.asset(), changed}});
    check(scene.entity(child).get<LocalScale>().y == 1, "equal scale override lost");
    check(scene.entity(child).get<LocalRotation>() == LocalRotation{},
          "equal rotation override lost");
    doc = scene.document();
    const std::string following = row(doc, instances[1])["prefab_instance"]["members"][member];
    check(scene.entity(following).get<LocalRotation>() == rotation &&
              scene.entity(following).get<LocalScale>().y == 2 &&
              !scene.entity(following).owns<LocalRotation>() &&
              !scene.entity(following).owns<LocalScale>(),
          "Published rotation/scale failed to propagate through inheritance");
    scene.rename_entity(instances[0], "Named instance");
    authoring_command(scene, "prefab.revert_name", {{"entity", instances[0]}});
    doc = scene.document();
    check(row(doc, instances[0])["name"] == "Root", "name Revert failed");
    check(scene.undo(), "name Revert undo absent");
    doc = scene.document();
    check(row(doc, instances[0])["name"] == "Named instance" &&
              row(doc, instances[0]).value("name_override", false),
          "name Revert undo lost intent");
    check(scene.redo(), "name Revert redo absent");
    const auto milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "40 instances; equivalent member tables=" << tables.size()
              << "; creation + edits + reconciliation " << milliseconds
              << " ms (Debug, small sanity workload)\n";
}
void spatial_and_validation() {
    WorldContext world;
    Scene scene(world);
    auto source = fixture();
    const auto asset = source.at("asset_id").get<AssetId>();
    const std::string root = source["root"], child = source["members"][1]["id"],
                      grand = PrefabMemberId::generate().str();
    source["members"][0]["components"]["forge.local_translation"]["y"] = 10;
    source["members"][1]["components"]["forge.local_translation"]["y"] = 5;
    source["members"].push_back(
        {{"id", grand},
         {"name", "Grand"},
         {"parent", child},
         {"components", {{"forge.local_translation", {{"x", 0}, {"y", 2}, {"z", 0}}}}},
         {"opaque_plugin", {{"id_like_string", child}}}});
    scene.set_prefab_sources({{asset, source}});
    const auto instance_id = instantiate_prefab(scene, asset);
    auto doc = scene.document();
    const std::string gc = row(doc, instance_id)["prefab_instance"]["members"][grand],
                      cc = row(doc, instance_id)["prefab_instance"]["members"][child];
    auto y = [&](const std::string& id) {
        world.evaluate_world_transforms();
        return scene.entity(id).get<WorldTransform>().affine.m[7];
    };
    check(y(gc) == 17, "three-level FollowStructure");
    source["revision"] = 2u;
    source["members"][1]["spatial"] = {{"mode", "world"}};
    scene.set_prefab_sources({{asset, source}});
    check(y(gc) == 7, "World source spatial binding");
    source["revision"] = 3u;
    source["members"][2]["spatial"] = {{"mode", "explicit"}, {"member", root}};
    scene.set_prefab_sources({{asset, source}});
    check(y(gc) == 12, "Explicit member spatial reference");
    const auto duplicate = PrefabDocument(source).duplicate();
    check(duplicate.source["members"][2]["spatial"]["member"] == duplicate.source["root"],
          "known source spatial ref not remapped");
    check(duplicate.source["members"][2]["opaque_plugin"] == source["members"][2]["opaque_plugin"],
          "opaque payload rewritten");
    const auto before = scene.snapshot();
    const auto revision = scene.revision();
    auto rejected = [&](auto action) {
        bool bad = false;
        try {
            action();
        } catch (const std::exception&) {
            bad = true;
        }
        check(bad && scene.snapshot() == before && scene.revision() == revision,
              "rejected structural edit changed state");
    };
    rejected([&] { scene.reparent_entity(cc, ""); });
    rejected([&] { scene.rename_entity(cc, "Wrong place"); });
    rejected([&] { scene.delete_subtree(cc); });
    rejected([&] { scene.duplicate_subtree(cc); });
    auto malformed = scene.document();
    row(malformed, cc)["parent"] = gc;
    rejected([&] { scene.edit(malformed); });
    malformed = scene.document();
    row(malformed, instance_id)["prefab_instance"]["members"][child] = instance_id;
    rejected([&] { scene.edit(malformed); });
    auto bad_source = source;
    bad_source["revision"] = 4u;
    bad_source["members"][0]["spatial"] = {{"mode", "explicit"}, {"member", grand}};
    rejected([&] { scene.set_prefab_sources({{asset, bad_source}}); });
    bad_source = source;
    bad_source["revision"] = 4u;
    bad_source["dependencies"] = Json::array({asset});
    rejected([&] { scene.set_prefab_sources({{asset, bad_source}}); });
    bad_source["dependencies"] = Json::array({AssetId::generate()});
    rejected([&] { scene.set_prefab_sources({{asset, bad_source}}); });
    bad_source = source;
    bad_source["members"][0]["name"] = "revision reused";
    rejected([&] { scene.set_prefab_sources({{asset, bad_source}}); });
    check(scene.document().dump().find("world_affine") == std::string::npos,
          "derived transform serialized");
}
void source_ordering() {
    WorldContext world;
    Scene scene(world);
    auto source = fixture();
    const auto first = source["members"][1]["id"].get<PrefabMemberId>();
    const auto second = PrefabMemberId::generate();
    auto child = source["members"][1];
    child["id"] = second;
    child["name"] = "Second";
    source["members"].push_back(child);
    PrefabDocument initial(source);
    scene.set_prefab_sources({{initial.asset(), source}});
    const auto root = instance(scene, initial);
    auto document = scene.document();
    const auto mapping = row(document, root).at("prefab_instance").at("members");
    const auto a = mapping.at(first.str()).get<std::string>(),
               b = mapping.at(second.str()).get<std::string>();
    auto order = ecs_get_ordered_children(scene.world(), scene.entity(root));
    check(order.count == 2 && order.ids[0] == scene.entity(a), "Initial source order lost");
    auto next = initial.reorder_member(second, first).source;
    next["revision"] = 2u;
    const auto before = scene.snapshot();
    bool failed = false;
    try {
        scene.publish_prefab_sources({{initial.asset(), next}},
                                     [] { throw std::runtime_error("disk failure"); });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    check(failed && scene.snapshot() == before, "Failed order publication changed instances");
    scene.publish_prefab_sources({{initial.asset(), next}}, [] {});
    order = ecs_get_ordered_children(scene.world(), scene.entity(root));
    check(order.count == 2 && order.ids[0] == scene.entity(b),
          "Published source order did not propagate");
    Scene restored(world);
    restored.restore_snapshot(scene.snapshot());
    order = ecs_get_ordered_children(restored.world(), restored.entity(root));
    check(order.count == 2 && order.ids[0] == restored.entity(b),
          "Prefab order lost on reconstruction");
}
int main(int argc, char** argv) {
    try {
        source_ordering();
        check(argc == 2, "test requires scratch directory");
        files_and_publication(argv[1]);
        many_and_transforms();
        spatial_and_validation();
        WorldContext world;
        Scene scene(world);
        PrefabDocument p(fixture());
        scene.set_prefab_sources({{p.asset(), p.source}});
        auto a = instance(scene, p), b = instance(scene, p);
        auto doc = scene.document();
        const std::string member = p.source["members"][1]["id"];
        const std::string ac = row(doc, a)["prefab_instance"]["members"][member],
                          bc = row(doc, b)["prefab_instance"]["members"][member];
        check(scene.entity(ac).has<flecs::Parent>(), "structured member needs Parent");
        check(scene.entity(ac).has<LocalTranslation>() &&
                  !scene.entity(ac).owns<LocalTranslation>(),
              "member must inherit translation");
        check(scene.entity(ac).table() == scene.entity(bc).table(),
              "equivalent interiors fragmented");
        auto cmd = [&](std::string op, Json args) { return authoring_command(scene, op, args); };
        cmd("transform.position", {{"entity", ac}, {"value", {{"x", 0}, {"y", 1}, {"z", 0}}}});
        check(scene.entity(ac).owns<LocalTranslation>() && !scene.entity(ac).owns<LocalScale>() &&
                  !scene.entity(ac).owns<LocalRotation>(),
              "equal translation owns only translation");
        cmd("property.set",
            {{"entity", ac}, {"component", "forge.tint"}, {"field", "r"}, {"value", .2f}});
        doc = scene.document();
        check(row(doc, ac)["property_overrides"]["forge.tint"].contains("r") &&
                  !row(doc, ac)["components"].contains("forge.tint"),
              "property intent flattened");
        auto next = p.source;
        next["revision"] = 2u;
        next["members"][1]["components"]["forge.local_translation"]["y"] = 3;
        next["members"][1]["components"]["forge.tint"]["r"] = .8f;
        next["members"][1]["components"]["forge.tint"]["g"] = .1f;
        scene.set_prefab_sources({{p.asset(), next}});
        check(scene.entity(ac).get<LocalTranslation>().y == 1 &&
                  scene.entity(bc).get<LocalTranslation>().y == 3,
              "source reconciliation lost override");
        check(scene.entity(ac).get<Tint>().r == .2f && scene.entity(ac).get<Tint>().g == .1f,
              "property reconciliation incorrect");
        cmd("property.revert", {{"entity", ac}, {"component", "forge.tint"}, {"field", "r"}});
        check(!scene.entity(ac).owns<Tint>() && scene.entity(ac).get<Tint>().r == .8f,
              "property revert did not remove materialization");
        check(scene.undo() && scene.entity(ac).get<Tint>().r == .2f,
              "property revert undo lost intent");
        cmd("component.revert", {{"entity", ac}, {"component", "forge.local_translation"}});
        check(!scene.entity(ac).owns<LocalTranslation>() &&
                  scene.entity(ac).get<LocalTranslation>().y == 3,
              "revert didn't inherit");
        check(scene.undo() && scene.entity(ac).get<LocalTranslation>().y == 1, "undo revert");
        check(scene.redo() && scene.entity(ac).get<LocalTranslation>().y == 3, "redo revert");
        auto copy = scene.duplicate_subtree(a);
        doc = scene.document();
        check(row(doc, copy)["prefab_instance"]["members"][member] != ac,
              "instance duplicate identity collision");
        auto before = scene.snapshot();
        auto rev = scene.revision();
        auto invalid = next;
        invalid["revision"] = 3u;
        invalid["members"][1]["parent"] = invalid["members"][1]["id"];
        bool rejected = false;
        try {
            scene.set_prefab_sources({{p.asset(), invalid}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.snapshot() == before && scene.revision() == rev,
              "candidate rejection mutated scene");
        {
            WorldContext runtime(WorldRole::Runtime);
            Scene restored(runtime);
            restored.restore_snapshot(scene.snapshot());
            check(restored.document() == scene.document(), "runtime snapshot round trip");
        }
        scene.delete_subtree(a);
        check(scene.undo() && scene.entity(ac), "delete undo lost mapping");
        next["revision"] = 3u;
        next["members"].erase(1);
        scene.set_prefab_sources({{p.asset(), next}});
        doc = scene.document();
        check(!scene.entity(ac) && row(doc, ac).value("missing_member", false),
              "removed member not diagnosed");
        std::cout << "Structured prefab checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
