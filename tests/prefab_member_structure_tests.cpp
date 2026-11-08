#include "../src/asset_reference_impact.hpp"
#include "../src/editor/document.hpp"
#include "../src/prefab_member_structure.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/navigation_behavior_components.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid structural candidate was admitted");
}
struct Fixture {
    AssetId asset = AssetId::generate();
    PrefabMemberId root = PrefabMemberId::generate(), a = PrefabMemberId::generate(),
                   b = PrefabMemberId::generate(), c = PrefabMemberId::generate(),
                   local = PrefabMemberId::generate(), child = PrefabMemberId::generate(),
                   missing = PrefabMemberId::generate();
    Json row(PrefabMemberId id, const char* name, PrefabMemberId parent = {}) const {
        Json value{{"id", id}, {"name", name}, {"components", Json::object()}};
        if (parent)
            value["parent"] = parent;
        return value;
    }
    Json inherited() const {
        auto rows = Json::array(
            {row(root, "Root"), row(a, "A", root), row(b, "B", root), row(c, "Child of A", a)});
        rows[0]["components"]["forge.navigation_behavior"] = {{"graph", nullptr},
                                                              {"target", PrefabMemberRef{asset, a}},
                                                              {"anchor", nullptr},
                                                              {"enabled", true}};
        rows[2]["removed"] = "Opaque older member extension";
        return rows;
    }
    Json intent() const {
        return {{"root", root},
                {"added_members",
                 Json::array({row(local, "Local", b), row(child, "Local child", local)})},
                {"overrides",
                 Json::array({{{"id", a}, {"components", Json::object()}, {"removed", true}}})},
                {"member_order",
                 Json::array({{{"parent", root}, {"members", Json::array({missing, b, a})}}})}};
    }
    std::set<std::string> reserved() const {
        return {root.str(), a.str(), b.str(), c.str(), missing.str()};
    }
    Json compose(const Json& intent) const {
        return detail::compose_prefab_member_structure(inherited(), intent, reserved());
    }
    Json definition(const Json& members, unsigned revision = 1) const {
        // Exercise the existing complete-definition/native consumers. Raw new
        // format admission is a separate owner integration, not implied here.
        return {{"format", "forge.prefab"}, {"version", 5}, {"asset_id", asset},
                {"revision", revision},     {"root", root}, {"members", members}};
    }
};
void composition() {
    Fixture f;
    const auto raw = f.intent(), base = f.inherited();
    const auto active = f.compose(raw);
    check(active.size() == 4 && raw == f.intent() && base == f.inherited(),
          "Composition mutated intent or lost the active local branch");
    check(active[1].at("id") == Json(f.b) && active[1].at("removed").is_string() &&
              active[2].at("id") == Json(f.local) && active[3].at("id") == Json(f.child),
          "Removal reinterpreted an old extension or changed local identity");
    PrefabDocument::validate(f.definition(active));
    auto restored = raw;
    restored["overrides"][0].erase("removed");
    const auto returned = f.compose(restored);
    check(returned.size() == 6 && returned[1].at("id") == Json(f.b) &&
              returned[2].at("id") == Json(f.a),
          "Restore or sibling prefix failed to follow stable inherited members");
    auto dormant = raw;
    dormant["added_members"][0]["parent"] = f.missing;
    check(f.compose(dormant).size() == 2,
          "A missing parent left its local branch partially active");
    dormant["added_members"][0]["parent"] = f.root;
    check(f.compose(dormant).size() == 4, "Reparent did not repair a dormant local branch");
    auto missing_cycle = raw;
    missing_cycle["added_members"][0]["parent"] = f.missing;
    missing_cycle["overrides"].push_back(
        {{"id", f.missing}, {"components", Json::object()}, {"parent", f.local}});
    rejects([&] { (void)f.compose(missing_cycle); });
    missing_cycle["overrides"].back().erase("parent");
    missing_cycle["overrides"].back()["spatial"] = {{"mode", "explicit"}, {"member", f.local}};
    missing_cycle["added_members"][0]["parent"] = f.root;
    missing_cycle["added_members"][0]["spatial"] = {{"mode", "explicit"}, {"member", f.missing}};
    rejects([&] { (void)f.compose(missing_cycle); });
    auto removed_local = raw;
    removed_local["added_members"][0]["removed"] = true;
    check(f.compose(removed_local).size() == 2 && removed_local.at("added_members").size() == 2,
          "Local removal lost authored definitions or retained an active descendant");
    auto missing_spatial = raw;
    missing_spatial["overrides"].push_back({{"id", f.b},
                                            {"components", Json::object()},
                                            {"spatial", {{"mode", "explicit"}, {"member", f.a}}}});
    check(f.compose(missing_spatial).size() == 1,
          "A removed spatial target did not retire its dependent structural branch");
    auto reparented = raw;
    reparented["overrides"].push_back(
        {{"id", f.c}, {"components", Json::object()}, {"parent", f.root}});
    check(f.compose(reparented).size() == 5,
          "A branch reparented away from removal was incorrectly retired");
    auto cycle = raw;
    cycle["overrides"][0]["parent"] = f.c;
    rejects([&] { (void)f.compose(cycle); });
    auto spatial_cycle = raw;
    spatial_cycle["overrides"].push_back(
        {{"id", f.b},
         {"components", Json::object()},
         {"spatial", {{"mode", "explicit"}, {"member", f.local}}}});
    rejects([&] { (void)f.compose(spatial_cycle); });
    auto collision = raw;
    collision["added_members"][0]["id"] = f.missing;
    rejects([&] { (void)f.compose(collision); });
    auto duplicate_local = raw;
    duplicate_local["added_members"].push_back(duplicate_local.at("added_members")[0]);
    rejects([&] { (void)f.compose(duplicate_local); });
    auto duplicate_order = raw;
    duplicate_order["member_order"][0]["members"].push_back(f.b);
    rejects([&] { (void)f.compose(duplicate_order); });
    auto root_removed = raw;
    root_removed["overrides"][0]["id"] = f.root;
    rejects([&] { (void)f.compose(root_removed); });
    auto competing_owner = raw;
    competing_owner["overrides"][0]["id"] = f.local;
    rejects([&] { (void)f.compose(competing_owner); });
}
void native_projection() {
    Fixture f;
    EngineContext engine;
    Scene scene(engine.world());
    auto raw = f.intent();
    scene.set_prefab_sources({{f.asset, f.definition(f.compose(raw))}});
    const auto instance = instantiate_prefab(scene, f.asset);
    const auto absent_target = scene.entity(instance).get<NavigationBehavior>().target;
    const auto local =
        *prefab_member_reference(scene.document(), EntityId::parse(instance), {f.asset, f.local});
    const auto child =
        *prefab_member_reference(scene.document(), EntityId::parse(instance), {f.asset, f.child});
    check(scene.entity_count() == 4 && !scene.entity(absent_target.entity.str()) &&
              scene.entity(child.entity.str()).parent() == scene.entity(local.entity.str()),
          "Native templates lost composed structure or retargeted an absent member");
    raw["overrides"][0].erase("removed");
    scene.set_prefab_sources({{f.asset, f.definition(f.compose(raw), 2)}});
    check(scene.entity_count() == 6 && scene.entity(absent_target.entity.str()) &&
              scene.entity(instance).get<NavigationBehavior>().target == absent_target &&
              prefab_member_reference(scene.document(), EntityId::parse(instance),
                                      {f.asset, f.local}) == local,
          "Native restoration replaced stable reference/member identities");
    const auto before = scene.snapshot();
    auto invalid = raw;
    invalid["overrides"][0]["parent"] = f.c;
    rejects([&] { (void)f.compose(invalid); });
    check(scene.snapshot() == before, "Rejected detached composition changed the live Scene");
}
void raw_sources() {
    Fixture f;
    const auto base_id = AssetId::generate();
    auto base = f.definition(f.inherited());
    base["asset_id"] = base_id;
    base["members"][0]["components"]["forge.navigation_behavior"]["target"]["prefab"] = base_id;
    auto source = f.intent();
    source["format"] = "forge.prefab";
    source["version"] = 6;
    source["asset_id"] = f.asset;
    source["base"] = base_id;
    source["revision"] = 1;
    source["added_members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr},
        {"target", PrefabMemberRef{f.asset, f.child}},
        {"anchor", nullptr},
        {"enabled", true}};
    source["member_order"].push_back({{"parent", f.local}, {"members", Json::array({f.child})}});
    const PrefabSources inputs{{base_id, base}, {f.asset, source}};
    const auto definitions = resolve_prefab_sources(inputs);
    const auto& selected = definitions.at(f.asset);
    check(inputs.at(f.asset) == source && selected.at("version") == 6 &&
              selected.at("members").size() == 4 && !selected.contains("added_members") &&
              !selected.contains("member_order"),
          "Raw source6 resolution mutated or leaked structural authority");
    const auto copy = PrefabDocument(source).duplicate();
    const auto owned = copy.source.at("added_members")[0].at("id").get<PrefabMemberId>();
    const auto descendant = copy.source.at("added_members")[1].at("id").get<PrefabMemberId>();
    check(copy.asset() != f.asset && owned != f.local && descendant != f.child &&
              copy.source.at("root") == Json(f.root) && copy.source.at("base") == Json(base_id) &&
              copy.source.at("added_members")[1].at("parent") == Json(owned) &&
              copy.source.at("added_members")[0]
                      .at("components")
                      .at("forge.navigation_behavior")
                      .at("target") == Json(PrefabMemberRef{copy.asset(), descendant}) &&
              copy.source.at("member_order")[1].at("parent") == Json(owned) &&
              copy.source.at("member_order")[1].at("members")[0] == Json(descendant),
          "Variant duplication failed to separate local IDs/references from inherited identity");
    auto no_order = source;
    no_order.erase("member_order");
    check(!PrefabDocument(no_order).duplicate().source.contains("member_order"),
          "Duplication manufactured missing sibling-order intent");
    auto inherited_reference = inputs;
    inherited_reference[base_id]["members"][0]["components"]["forge.navigation_behavior"]["target"]
                       ["member"] = f.missing;
    inherited_reference[f.asset]["added_members"][0]["id"] = f.missing;
    rejects([&] { (void)resolve_prefab_sources(inherited_reference); });
    auto own_reference = inputs;
    own_reference[f.asset]["overrides"].push_back(
        {{"id", f.root},
         {"components", Json::object()},
         {"property_overrides",
          {{"forge.navigation_behavior", {{"target", PrefabMemberRef{f.asset, f.local}}}}}}});
    check(resolve_prefab_sources(own_reference)
                  .at(f.asset)
                  .at("members")[0]
                  .at("components")
                  .at("forge.navigation_behavior")
                  .at("target") == Json(PrefabMemberRef{f.asset, f.local}),
          "Inherited reservations incorrectly rejected this source's explicit reference to its "
          "local member");
    EngineContext engine;
    Scene scene(engine.world());
    scene.set_prefab_sources(inputs);
    const auto instance = instantiate_prefab(scene, f.asset);
    const auto pending = scene.entity(instance).get<NavigationBehavior>().target;
    check(scene.entity_count() == 4 && !scene.entity(pending.entity.str()),
          "Raw source6 compiled the wrong active members or resolved a removed target");
    auto updated = inputs;
    updated[f.asset]["overrides"][0].erase("removed");
    updated[f.asset]["revision"] = 2;
    scene.set_prefab_sources(updated);
    check(scene.entity_count() == 6 && scene.entity(pending.entity.str()) &&
              scene.entity(instance).get<NavigationBehavior>().target == pending,
          "Raw source6 restoration lost the existing Scene member map");
    auto old = source;
    old["version"] = 5;
    old["overrides"] = Json::array();
    const auto old_result = resolve_prefab_sources({{base_id, base}, {f.asset, old}}).at(f.asset);
    check(old_result.at("members").size() == 4 && old_result.contains("added_members") &&
              old_result.contains("member_order"),
          "Older opaque structural arrays gained new semantics");
}
void candidate_operations() {
    Fixture f;
    const auto base_id = AssetId::generate();
    auto base = f.definition(f.inherited());
    base["asset_id"] = base_id;
    base["members"][0]["components"]["forge.navigation_behavior"]["target"]["prefab"] = base_id;
    const Json original{
        {"format", "forge.prefab"}, {"version", 4},    {"asset_id", f.asset},       {"revision", 1},
        {"root", f.root},           {"base", base_id}, {"overrides", Json::array()}};
    const auto added = PrefabDocument(original).with_added_member(f.row(f.local, "Local", f.root));
    check(added.source.at("version") == 6 && added.asset() == f.asset && added.root() == f.root &&
              added.revision() == 1,
          "Structural opt-in changed source identity or publication revision");
    const auto edited = added.with_member_parent(f.a, f.b)
                            .with_member_spatial(f.local, {{"mode", "world"}})
                            .with_member_removed(f.a, true)
                            .with_member_order(f.root, {f.local, f.b});
    const auto selected =
        resolve_prefab_sources({{base_id, base}, {f.asset, edited.source}}).at(f.asset);
    check(
        selected.at("members").size() == 3 && selected.at("members")[1].at("id") == Json(f.local) &&
            selected.at("members")[1].at("spatial").at("mode") == "world",
        "Shared structural candidates did not compose removal, local spatial ownership and order");
    const auto restored = edited.with_member_removed(f.a, false);
    check(resolve_prefab_sources({{base_id, base}, {f.asset, restored.source}})
                  .at(f.asset)
                  .at("members")
                  .size() == 5,
          "Member Restore did not resume the latest inherited subtree");
    const auto local_removed = restored.with_member_removed(f.local, true);
    check(local_removed.source.at("added_members")[0].at("removed") == true &&
              local_removed.with_member_removed(f.local, false).source == restored.source,
          "Local removal/restoration lost its exact authored definition");
    const auto inherited = restored.revert_member_structure(f.a, "parent");
    check(std::none_of(inherited.source.at("overrides").begin(),
                       inherited.source.at("overrides").end(),
                       [&](const Json& row) { return row.at("id") == Json(f.a); }),
          "Revert parent retained a sparse override with no remaining intent");
    rejects([&] { (void)restored.revert_member_structure(f.local, "spatial"); });
    rejects([&] { (void)restored.revert_member_structure(f.a, "name"); });
    check(!edited.with_member_order(f.root, {}).source.contains("member_order"),
          "Reset sibling order retained a known prefix");
    rejects([&] { (void)added.with_member_parent(f.root, f.b); });
    rejects([&] { (void)added.with_member_removed(f.root, true); });
    rejects(
        [&] { (void)added.with_component_suppression(f.local, "forge.navigation_surface", true); });
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto old = original;
        if (mode == 0)
            old["added_members"] = "Preserve opaque payload";
        if (mode == 1)
            old["member_order"] = Json::array({"Preserve opaque payload"});
        if (mode >= 2)
            old["overrides"] = Json::array(
                {{{"id", f.a},
                  {"components", Json::object()},
                  {mode == 2 ? "removed" : "suppressed_components", "Preserve opaque payload"}}});
        const PrefabDocument parsed(old);
        rejects([&] { (void)parsed.with_added_member(f.row(f.local, "Local", f.root)); });
        check(parsed.source == old && parsed.with_member_removed(f.a, false).source == old &&
                  parsed.with_member_order(f.root, {}).source == old,
              "Older source structural Restore/reset reinterpreted or discarded opaque values");
    }
    const PrefabDocument complete(f.definition(f.inherited()));
    const auto positive = complete.with_added_member(f.row(f.local, "Local", f.root));
    check(positive.source.at("version") == 5 && positive.source.at("members").size() == 5 &&
              positive.with_member_removed(f.a, true).source.at("members").size() == 3,
          "Complete-source member operations changed the format or removal subtree");
}
void reference_rows() {
    Fixture f;
    EngineContext engine;
    const auto base = AssetId::generate(), mesh = AssetId::generate();
    auto source = f.intent();
    source["format"] = "forge.prefab";
    source["version"] = 6;
    source["asset_id"] = f.asset;
    source["revision"] = 1;
    source["base"] = base;
    source["added_members"][0]["removed"] = true;
    const auto schema = engine.world().schema();
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.mesh_renderer")
            for (const auto& field : type.at("fields"))
                source["added_members"][0]["components"]["forge.mesh_renderer"]
                      [field.at("id").get<std::string>()] = field.at("default");
    source["added_members"][0]["components"]["forge.mesh_renderer"]["mesh"] = mesh;
    const auto inspect = [&](const Json& value) {
        const std::array documents{AssetReferenceDocument{"Structure.prefab.json", value}};
        return collect_asset_references(engine.world().schema(), documents);
    };
    const auto references = inspect(source);
    check(
        references.uninspected.empty() && references.references.size() == 2 &&
            std::any_of(
                references.references.begin(), references.references.end(),
                [&](const auto& hit) { return hit.target == mesh && hit.expected_type == "mesh"; }),
        "Dependency inspection missed a retained local definition or treated member IDs as assets");
    auto older = source;
    older["version"] = 5;
    const auto old_refs = inspect(older);
    check(!old_refs.uninspected.empty() && old_refs.references.size() == 1 &&
              old_refs.references[0].target == base,
          "Older opaque local definitions gained dependency semantics");
    auto opaque = source;
    opaque["member_order"][0]["future_payload"] = {{"asset", AssetId::generate()}};
    check(!inspect(opaque).uninspected.empty(), "Unknown order metadata lost opaque coverage");
    opaque = source;
    opaque["entities"] =
        Json::array({{{"id", EntityId::generate()},
                      {"name", "Opaque Scene-like data"},
                      {"components", {{"forge.mesh_renderer", {{"mesh", AssetId::generate()}}}}}}});
    const auto collision = inspect(opaque);
    check(!collision.uninspected.empty() && collision.references.size() == 2,
          "An unknown Scene-like array displaced the Prefab format's owned arrays");
    opaque = source;
    opaque["added_members"][0]["spatial"] = {{"mode", "explicit"}, {"member", f.root}};
    const auto known_spatial = inspect(opaque);
    check(known_spatial.uninspected.empty() && known_spatial.references.size() == 2,
          "Known member attachment fields gained an asset dependency or opaque warning");
    opaque = source;
    opaque["added_members"][0]["spatial"] = {{"mode", "world"}, {"target", 42}};
    const auto local_spatial = inspect(opaque);
    check(!local_spatial.uninspected.empty() && local_spatial.references.size() == 2,
          "Opaque local spatial data was interpreted as a Scene attachment");
    auto complete = f.definition(Json::array({f.row(f.root, "Root")}));
    complete["members"][0]["spatial"] = {
        {"mode", "follow_structure"},
        {"target", {{"scene", AssetId::generate()}, {"entity", EntityId::generate()}}}};
    const auto complete_spatial = inspect(complete);
    check(!complete_spatial.uninspected.empty() && complete_spatial.references.empty(),
          "Opaque complete-source spatial data produced a false Scene dependency");
}
void changing_bases_and_bounds() {
    Fixture f;
    const auto base_id = AssetId::generate(), chained_id = AssetId::generate();
    auto base = f.definition(Json::array({f.row(f.root, "Root")}));
    base["asset_id"] = base_id;
    const Json initial{
        {"format", "forge.prefab"}, {"version", 4},    {"asset_id", f.asset},       {"revision", 1},
        {"root", f.root},           {"base", base_id}, {"overrides", Json::array()}};
    const auto source = PrefabDocument(initial)
                            .with_added_member(f.row(f.local, "Local", f.a))
                            .with_added_member(f.row(f.child, "Local child", f.local));
    PrefabSources sources{{base_id, base}, {f.asset, source.source}};
    check(resolve_prefab_sources(sources).at(f.asset).at("members").size() == 1,
          "Missing base parent did not retain its dormant local branch");
    sources[base_id]["members"].push_back(f.row(f.a, "Returning parent", f.root));
    check(resolve_prefab_sources(sources).at(f.asset).at("members").size() == 4 &&
              sources.at(f.asset) == source.source,
          "Returning base parent changed local identity or authored intent");
    const Json chained{
        {"format", "forge.prefab"}, {"version", 4},    {"asset_id", chained_id},    {"revision", 1},
        {"root", f.root},           {"base", f.asset}, {"overrides", Json::array()}};
    sources[chained_id] = PrefabDocument(chained)
                              .with_member_removed(f.local, true)
                              .with_added_member(f.row(f.b, "Chained local", f.a))
                              .source;
    check(resolve_prefab_sources(sources).at(chained_id).at("members").size() == 3,
          "Chained structural variant ignored inherited-local removal or its own addition");
    sources[chained_id] =
        PrefabDocument(sources.at(chained_id)).with_member_removed(f.local, false).source;
    check(resolve_prefab_sources(sources).at(chained_id).at("members").size() == 5,
          "Chained Restore did not recover an upstream-owned local branch");
    sources[base_id]["members"].erase(1);
    sources[f.asset] = source.with_member_parent(f.local, f.root).source;
    check(resolve_prefab_sources(sources).at(chained_id).at("members").size() == 3,
          "Reparent-away from a missing upstream member did not retain the repaired branch");
    sources[base_id]["members"].push_back(f.row(f.local, "Conflicting later base", f.root));
    rejects([&] { (void)resolve_prefab_sources(sources); });

    // Spatial chains have an independent transform contract. Flat structural
    // siblings may form a longer explicit attachment chain than Flecs parenting.
    auto spatial = initial;
    spatial["version"] = 6;
    spatial["added_members"] = Json::array();
    auto previous = f.root;
    for (unsigned i = 0; i < FLECS_DAG_DEPTH_MAX + 8; ++i) {
        const auto id = PrefabMemberId::generate();
        auto row = f.row(id, "Spatial sibling", f.root);
        row["spatial"] = {{"mode", "explicit"}, {"member", previous}};
        spatial["added_members"].push_back(row);
        previous = id;
    }
    check(resolve_prefab_sources({{base_id, base}, {f.asset, spatial}})
                  .at(f.asset)
                  .at("members")
                  .size() == FLECS_DAG_DEPTH_MAX + 9,
          "Independent spatial depth inherited the structural Flecs limit");
    auto deep = spatial;
    previous = f.root;
    for (auto& row : deep["added_members"]) {
        row["parent"] = previous;
        row["removed"] = true;
        previous = row.at("id").get<PrefabMemberId>();
    }
    rejects([&] { (void)resolve_prefab_sources({{base_id, base}, {f.asset, deep}}); });

    auto maximum = initial;
    maximum["version"] = 6;
    maximum["added_members"] = Json::array();
    for (unsigned i = 0; i < 4095; ++i)
        maximum["added_members"].push_back(f.row(PrefabMemberId::generate(), "Local", f.root));
    check(resolve_prefab_sources({{base_id, base}, {f.asset, maximum}})
                  .at(f.asset)
                  .at("members")
                  .size() == 4096,
          "Maximum effective structural variant was rejected");
    maximum["added_members"].push_back(f.row(PrefabMemberId::generate(), "Local", f.root));
    rejects([&] { (void)resolve_prefab_sources({{base_id, base}, {f.asset, maximum}}); });
    for (auto& row : maximum["added_members"])
        row["removed"] = true;
    check(resolve_prefab_sources({{base_id, base}, {f.asset, maximum}})
                  .at(f.asset)
                  .at("members")
                  .size() == 1,
          "Retained removed definitions incorrectly counted as effective members");
    maximum["added_members"].push_back(f.row(PrefabMemberId::generate(), "Over budget", f.root));
    rejects([&] { (void)PrefabDocument(maximum); });
    auto oversized_order = initial;
    oversized_order["version"] = 6;
    std::vector<PrefabMemberId> order;
    for (unsigned i = 0; i < 4097; ++i)
        order.push_back(PrefabMemberId::generate());
    oversized_order["member_order"] = Json::array({{{"parent", f.root}, {"members", order}}});
    rejects([&] { (void)PrefabDocument(oversized_order); });
}
void local_apply_history(const std::filesystem::path& scratch) {
    Fixture f;
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    SceneDocument::create_project(root, "Structural member Apply");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto base = project.prefabs().create(scene, PrefabDocument(f.definition(f.inherited())),
                                               "Assets/Base.prefab.json");
    const auto variant =
        project.prefabs().create_variant(scene, base, "Assets/Variant.prefab.json");
    const auto expected = project.prefabs().source(variant);
    auto local_row = f.row(f.local, "Local color", f.root);
    local_row["components"]["forge.tint"] = {{"r", .2}, {"g", .5}, {"b", .8}};
    const auto candidate = PrefabDocument(expected).with_added_member(local_row).source;
    project.prefabs().publish(scene, expected, candidate,
                              project.prefabs().base_sources(scene, expected));
    const auto first = instantiate_prefab(scene, variant),
               second = instantiate_prefab(scene, variant);
    const auto a =
        *prefab_member_reference(scene.document(), EntityId::parse(first), {variant, f.local});
    const auto b =
        *prefab_member_reference(scene.document(), EntityId::parse(second), {variant, f.local});
    project.save();
    const auto base_before = project.prefabs().source(base);
    authoring_command(
        scene, "property.set",
        {{"entity", a.entity.str()}, {"component", "forge.tint"}, {"field", "r"}, {"value", .9}});
    const auto review = project.prefabs().prepare_apply(scene, first);
    check(review.source.at("overrides").empty() &&
              !review.source.at("added_members")[0].contains("property_overrides") &&
              std::abs(review.source.at("added_members")[0]
                           .at("components")
                           .at("forge.tint")
                           .at("r")
                           .get<double>() -
                       .9) < 1e-6,
          "Apply wrote local values to inherited override storage");
    project.apply_prefab(review, scene.revision());
    check(std::abs(scene.entity(b.entity.str()).get<Tint>().r - .9f) < 1e-6f &&
              project.prefabs().source(base) == base_before,
          "Local-member Apply did not propagate or changed the base source");
    check(scene.undo() && std::abs(scene.entity(a.entity.str()).get<Tint>().r - .9f) < 1e-6f &&
              std::abs(scene.entity(b.entity.str()).get<Tint>().r - .2f) < 1e-6f,
          "Paired local-member Undo lost pre-Apply instance intent");
    check(scene.redo() && std::abs(scene.entity(b.entity.str()).get<Tint>().r - .9f) < 1e-6f &&
              prefab_member_reference(scene.document(), EntityId::parse(first),
                                      {variant, f.local}) == a,
          "Paired local-member Redo replaced its durable member identity");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide a structural member scratch directory");
        composition();
        native_projection();
        raw_sources();
        candidate_operations();
        reference_rows();
        changing_bases_and_bounds();
        local_apply_history(std::filesystem::absolute(argv[1]));
        std::cout << "Detached structural composition and existing native projection passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
