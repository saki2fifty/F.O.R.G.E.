#include "../src/builtins.hpp"
#include "../src/editor/document.hpp"
#include "../src/prefab_composition.hpp"
#include "../src/prefab_member_choices.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/navigation_behavior_components.hpp>
#include <forge/physics.hpp>
#include <forge/prefab.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
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
    throw std::runtime_error("Invalid composition was admitted");
}
struct Source {
    AssetId asset = AssetId::generate();
    PrefabMemberId root = PrefabMemberId::generate(), mount = PrefabMemberId::generate();
    PrefabDocument document() const {
        return PrefabDocument(
            {{"format", "forge.prefab"},
             {"version", 1},
             {"asset_id", asset},
             {"revision", 1},
             {"root", root},
             {"members",
              Json::array({{{"id", root}, {"name", "Root"}, {"components", Json::object()}},
                           {{"id", mount},
                            {"name", "Mount"},
                            {"parent", root},
                            {"components", Json::object()}}})}});
    }
    Json variant(AssetId identity, unsigned version = 7) const {
        return {{"format", "forge.prefab"},
                {"version", version},
                {"asset_id", identity},
                {"revision", 1},
                {"root", root},
                {"base", AssetRef<PrefabAsset>{asset}},
                {"overrides", Json::array()}};
    }
};
const Json& mount_row(const Json& source, PrefabMemberId member) {
    for (const auto& row : source.at("members"))
        if (row.at("id") == Json(member))
            return row;
    throw std::runtime_error("Missing test member");
}
void member_choice_labels() {
    Source outer, child;
    auto source = outer.document()
                      .with_nested_prefab(outer.root, child.asset)
                      .with_nested_prefab(outer.mount, child.asset)
                      .source;
    for (auto& row : source["members"])
        row["name"] = "Repeated mount";
    PrefabSources definitions{{child.asset, child.document().source}};
    const auto choices = detail::prefab_member_choices(source, definitions);
    std::set<std::string> labels;
    for (const auto& choice : choices) {
        check(labels.insert(choice.label).second,
              "Repeated nested occurrences have indistinguishable picker labels");
        check(detail::prefab_member_preview(source, definitions, choice.reference) == choice.label,
              "Closed picker and discovery disagree about the selected occurrence");
    }
    const PrefabMemberPath missing{outer.asset, {{outer.mount, AssetId::generate()}}, child.root};
    const auto preview = detail::prefab_member_preview(source, definitions, missing);
    check(preview.find("Missing nested occurrence") != std::string::npos &&
              preview.find(outer.mount.str().substr(0, 8)) != std::string::npos,
          "Unavailable scoped target lost its occurrence identity in the picker");
}
void source_candidates() {
    Source outer, child;
    const auto original = outer.document();
    const auto linked = original.with_nested_prefab(outer.mount, child.asset);
    check(original.source.at("version") == 1 && linked.source.at("version") == 7 &&
              detail::nested_prefab_asset(mount_row(linked.source, outer.mount), 7) == child.asset,
          "Nested candidate mutated its baseline or lost its mount");
    check(linked.dependencies() == std::vector<AssetId>{child.asset},
          "Nested source dependency was not exposed");
    const auto duplicate = linked.duplicate();
    check(duplicate.asset() != outer.asset && duplicate.root() != outer.root &&
              duplicate.dependencies() == linked.dependencies(),
          "Source duplication changed child ownership or retained outer identities");
    for (unsigned version = 1; version < 7; ++version) {
        auto older = linked.source;
        older["version"] = version;
        rejects([&] { PrefabDocument::validate(older); });
    }
    auto invalid = linked.source;
    invalid["members"][1]["prefab_instance"] = false;
    rejects([&] { PrefabDocument::validate(invalid); });
    invalid["members"][1]["prefab_instance"] = Json::object();
    rejects([&] { PrefabDocument::validate(invalid); });
    rejects([&] { original.with_nested_prefab(PrefabMemberId::generate(), child.asset); });
    const auto cleared = linked.with_nested_prefab(outer.mount, {});
    check(cleared.dependencies().empty(), "Cleared mount retained an active asset dependency");
    const auto variant_id = AssetId::generate();
    PrefabDocument variant(outer.variant(variant_id, 4));
    auto opaque = variant.source;
    opaque["overrides"].push_back({{"id", outer.mount},
                                   {"components", Json::object()},
                                   {"parent", "Opaque source4 extension"}});
    rejects([&] { PrefabDocument(opaque).with_nested_prefab(outer.mount, child.asset); });
    const auto changed = variant.with_nested_prefab(outer.mount, child.asset);
    check(changed.source.at("version") == 7 &&
              changed.revert_nested_prefab(outer.mount).source.at("overrides").empty(),
          "Nested override could not revert through its source owner");
}
void closure_and_inheritance() {
    Source outer, child;
    const auto linked = outer.document().with_nested_prefab(outer.mount, child.asset);
    PrefabSources sources{{outer.asset, linked.source}, {child.asset, child.document().source}};
    const auto baseline = sources;
    auto resolved = resolve_prefab_sources(sources);
    check(sources == baseline && resolved.at(outer.asset) == linked.source,
          "Dependency preparation rewrote authoritative source data");
    check(detail::expanded_prefab_member_count(outer.asset,
                                               [&](AssetId id) { return &resolved.at(id); }) == 4,
          "Nested instances were not charged to the creation budget");
    const auto derived = AssetId::generate();
    auto variant = outer.variant(derived, 4);
    variant["added_members"] = "Opaque source4 extension";
    variant["member_order"] = {{"note", "Opaque source4 extension"}};
    variant["overrides"].push_back({{"id", outer.mount},
                                    {"components", Json::object()},
                                    {"removed", "Opaque source4 extension"}});
    sources.emplace(derived, variant);
    resolved = resolve_prefab_sources(sources);
    check(resolved.at(derived).at("version") == 7 &&
              mount_row(resolved.at(derived), outer.mount).contains("prefab_instance") &&
              sources.at(derived) == variant && resolved.at(derived).at("members").size() == 2,
          "Inherited links activated old raw structural intent or changed its source");
    sources[derived] =
        PrefabDocument(outer.variant(derived)).with_nested_prefab(outer.mount, {}).source;
    resolved = resolve_prefab_sources(sources);
    check(!detail::nested_prefab_asset(mount_row(resolved.at(derived), outer.mount), 7),
          "Null override did not suppress an inherited link");
    sources[derived] = PrefabDocument(sources.at(derived)).revert_nested_prefab(outer.mount).source;
    resolved = resolve_prefab_sources(sources);
    check(detail::nested_prefab_asset(mount_row(resolved.at(derived), outer.mount), 7) ==
              child.asset,
          "Revert did not restore the inherited child asset");
}
void dependency_admission() {
    Source a, b;
    auto first = a.document().with_nested_prefab(a.mount, b.asset).source;
    rejects([&] { resolve_prefab_sources({{a.asset, first}}); });
    auto second = b.document().with_nested_prefab(b.root, a.asset).source;
    rejects([&] { resolve_prefab_sources({{a.asset, first}, {b.asset, second}}); });
    first = a.document().with_nested_prefab(a.root, a.asset).source;
    rejects([&] { resolve_prefab_sources({{a.asset, first}}); });
    const auto variant = AssetId::generate();
    first = a.document().with_nested_prefab(a.mount, variant).source;
    rejects([&] { resolve_prefab_sources({{a.asset, first}, {variant, a.variant(variant)}}); });
    std::vector<Source> chain(18);
    PrefabSources sources;
    for (std::size_t i = 0; i < chain.size(); ++i)
        sources.emplace(chain[i].asset,
                        i + 1 == chain.size()
                            ? chain[i].document().source
                            : chain[i]
                                  .document()
                                  .with_nested_prefab(chain[i].mount, chain[i + 1].asset)
                                  .source);
    rejects([&] { resolve_prefab_sources(sources); });
    const auto chain_document = [](const Source& source, std::size_t count) {
        auto document = source.document().source;
        auto rows = Json::array();
        auto parent = source.root;
        for (std::size_t i = 0; i < count; ++i) {
            const auto id = i == 0           ? source.root
                            : i + 1 == count ? source.mount
                                             : PrefabMemberId::generate();
            Json row{{"id", id}, {"name", "Structural depth"}, {"components", Json::object()}};
            if (i)
                row["parent"] = parent;
            rows.push_back(row);
            parent = id;
        }
        document["members"] = std::move(rows);
        return PrefabDocument(document);
    };
    const auto deep = chain_document(a, 80).with_nested_prefab(a.mount, b.asset);
    const auto deep_child = chain_document(b, 80);
    rejects(
        [&] { resolve_prefab_sources({{a.asset, deep.source}, {b.asset, deep_child.source}}); });
    check(resolve_prefab_sources(
              {{a.asset, chain_document(a, 20).with_nested_prefab(a.mount, b.asset).source},
               {b.asset, chain_document(b, 20).source}})
                  .size() == 2,
          "Admitted combined structural depth was rejected");
    sources.erase(chain.front().asset);
    check(resolve_prefab_sources(sources).size() == 17,
          "The admitted16-link composition boundary was rejected");
    // Two mounts share source values but represent two distinct occurrences.
    const auto shared =
        a.document().with_nested_prefab(a.root, b.asset).with_nested_prefab(a.mount, b.asset);
    auto resolved =
        resolve_prefab_sources({{a.asset, shared.source}, {b.asset, b.document().source}});
    check(detail::expanded_prefab_member_count(a.asset,
                                               [&](AssetId id) { return &resolved.at(id); }) == 6,
          "A repeated child asset was counted as one occurrence");
    // A small shared source DAG can still expand exponentially.
    sources.clear();
    for (std::size_t i = 0; i < 12; ++i)
        sources.emplace(chain[i].asset,
                        i == 11 ? chain[i].document().source
                                : chain[i]
                                      .document()
                                      .with_nested_prefab(chain[i].root, chain[i + 1].asset)
                                      .with_nested_prefab(chain[i].mount, chain[i + 1].asset)
                                      .source);
    rejects([&] { resolve_prefab_sources(sources); });
}
const Json& entity_row(const Json& scene, const std::string& id) {
    for (const auto& row : scene.at("entities"))
        if (row.at("id") == id)
            return row;
    throw std::runtime_error("Missing test entity");
}
std::string nested_root(const Json& scene, const std::string& root, PrefabMemberId mount,
                        AssetId asset) {
    return entity_row(scene, root)
        .at("prefab_instance")
        .at("nested")
        .at(mount.str())
        .at(asset.str())
        .get<std::string>();
}
void native_lifecycle() {
    Source outer, child, replacement;
    auto linked = outer.document()
                      .with_nested_prefab(outer.root, child.asset)
                      .with_nested_prefab(outer.mount, child.asset)
                      .source;
    PrefabSources sources{{outer.asset, linked},
                          {child.asset, child.document().source},
                          {replacement.asset, replacement.document().source}};
    EngineContext engine;
    Scene scene(engine.world());
    scene.set_prefab_sources(sources);
    const auto root = instantiate_prefab(scene, outer.asset);
    auto document = scene.document();
    check(document.at("version") == 6 && document.at("entities").size() == 6,
          "Nested source did not create its full native group");
    const auto first = nested_root(document, root, outer.mount, child.asset);
    const auto second = nested_root(document, root, outer.root, child.asset);
    const auto mount =
        *prefab_member_reference(document, EntityId::parse(root), {outer.asset, outer.mount});
    check(first != second && scene.entity(first).parent() == scene.entity(mount.entity.str()) &&
              scene.entity(second).parent() == scene.entity(root),
          "Repeated child occurrences shared identity or lost mount parenting");
    validate_prefab_instances(document);
    check(reconcile_prefab_intent(document, sources, scene.schema()) == document,
          "Unchanged reconciliation allocated new identities or values");
    scene.rename_entity(first, "Instance override");
    const auto attachment =
        authoring_command(scene, "entity.create", {{"name", "Attachment"}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    scene.reparent_entity(attachment, first, ReparentMode::KeepLocal);
    document = scene.document();
    const auto snapshot = scene.snapshot();
    EngineContext restored_engine;
    Scene restored(restored_engine.world());
    restored.restore_snapshot(snapshot);
    check(restored.document() == document &&
              restored.entity(first).parent() == restored.entity(mount.entity.str()),
          "Nested private recovery lost identities/overrides/parenting");
    const auto baseline = scene.document();
    rejects([&] { scene.reparent_entity(first, root, ReparentMode::KeepLocal); });
    rejects([&] { authoring_command(scene, "entity.delete", {{"entity", first}}); });
    check(scene.document() == baseline, "Rejected child-root edits mutated the scene");
    auto cleared = PrefabDocument(linked).with_nested_prefab(outer.mount, {}).source;
    cleared["revision"] = 2;
    sources[outer.asset] = cleared;
    scene.set_prefab_sources(sources);
    document = scene.document();
    check(!scene.entity(first) && !scene.entity(attachment) && scene.entity(second) &&
              entity_row(document, first).at("missing_member") == true &&
              nested_root(document, root, outer.mount, child.asset) == first &&
              document.at("entities").size() == baseline.at("entities").size(),
          "Link removal discarded retained values or left its native branch active");
    restored.restore_snapshot(scene.snapshot());
    check(restored.document() == document && !restored.entity(first),
          "Inactive nested branch did not survive private recovery");
    linked["revision"] = 3;
    sources[outer.asset] = linked;
    scene.set_prefab_sources(sources);
    document = scene.document();
    check(scene.entity(first) && scene.entity(attachment) &&
              entity_row(document, first).at("name") == "Instance override" &&
              nested_root(document, root, outer.mount, child.asset) == first,
          "Restoring a link failed to revive its retained identity/overrides/attachments");
    const auto copy = scene.duplicate_subtree(root);
    document = scene.document();
    const auto copied = nested_root(document, copy, outer.mount, child.asset);
    check(copied != first && scene.entity(copied) &&
              entity_row(document, copied).at("prefab_nested").at("root") == copy,
          "Nested duplication retained original occurrence ownership");
    validate_prefab_instances(document);
    auto replaced =
        PrefabDocument(linked).with_nested_prefab(outer.mount, replacement.asset).source;
    replaced["revision"] = 4;
    sources[outer.asset] = replaced;
    scene.set_prefab_sources(sources);
    const auto other = nested_root(scene.document(), root, outer.mount, replacement.asset);
    check(other != first && scene.entity(other) && !scene.entity(first),
          "Retargeting a mount reused another asset's occurrence identity");
    linked["revision"] = 5;
    sources[outer.asset] = linked;
    scene.set_prefab_sources(sources);
    check(nested_root(scene.document(), root, outer.mount, child.asset) == first &&
              scene.entity(first) && !scene.entity(other),
          "Returning a replaced asset failed to restore its own occurrence identity");
    auto unavailable = sources;
    unavailable.erase(child.asset);
    document = scene.document();
    rejects([&] { scene.set_prefab_sources(unavailable); });
    check(scene.document() == document && scene.entity(first),
          "An unavailable nested dependency partially published the candidate");
}
void older_scene_extensions() {
    Source outer, child;
    EngineContext engine;
    Scene scene(engine.world());
    PrefabSources sources{{outer.asset, outer.document().source},
                          {child.asset, child.document().source}};
    scene.set_prefab_sources(sources);
    const auto instance = instantiate_prefab(scene, outer.asset);
    auto document = scene.document();
    document["version"] = 5;
    for (auto& row : document["entities"])
        if (row.at("id") == instance) {
            row["prefab_nested"] = {{"opaque", "Prior extension"}};
            row["prefab_instance"]["nested"] = "Prior extension";
            row["missing_member"] = true;
        }
    Scene::validate_document(document);
    sources[outer.asset] = outer.document().with_nested_prefab(outer.mount, {}).source;
    check(reconcile_prefab_intent(document, sources) == document,
          "Source7 without nesting activated older opaque Scene fields");
    sources[outer.asset] = outer.document().with_nested_prefab(outer.mount, child.asset).source;
    const auto before = document;
    rejects([&] { reconcile_prefab_intent(document, sources); });
    check(document == before, "Rejected Scene6 promotion changed older opaque data");
}
void linked_capture() {
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, unsigned kind) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
            .at("selected")
            .get<std::string>();
    };
    const auto original = create("Linked cube", 0);
    const auto child = create_prefab_source(scene, original);
    scene.set_prefab_sources({{child.asset(), child.source}});
    const auto wrapper = create("Assembly", 4);
    const auto first = instantiate_prefab(scene, child.asset());
    const auto second = instantiate_prefab(scene, child.asset());
    scene.reparent_entity(first, wrapper, ReparentMode::KeepLocal);
    scene.reparent_entity(second, wrapper, ReparentMode::KeepLocal);
    authoring_command(scene, "entity.reorder", {{"entity", second}, {"before", first}});
    authoring_command(scene, "property.set",
                      {{"entity", wrapper},
                       {"component", "forge.local_translation"},
                       {"field", "x"},
                       {"value", 10}});
    authoring_command(scene, "component.add",
                      {{"entity", wrapper}, {"component", "forge.navigation_behavior"}});
    const EntityRef target{scene.asset_id(), EntityId::parse(first)};
    authoring_command(scene, "property.set",
                      {{"entity", wrapper},
                       {"component", "forge.navigation_behavior"},
                       {"field", "target"},
                       {"value", target}});
    const auto before = scene.snapshot();
    const auto captured = create_prefab_source(scene, wrapper);
    check(scene.snapshot() == before && captured.source.at("version") == 7 &&
              captured.source.at("members").size() == 3 &&
              captured.dependencies() == std::vector<AssetId>{child.asset()},
          "Linked capture mutated the original or flattened/dropped child ownership");
    const auto path = mount_row(captured.source, captured.root())
                          .at("components")
                          .at("forge.navigation_behavior")
                          .at("target")
                          .get<PrefabMemberPath>();
    check(path.prefab == captured.asset() && path.instances.size() == 1 &&
              path.instances.front().asset == child.asset() && path.member == child.root(),
          "Linked capture retargeted the original child root to its new mount");
    std::vector<PrefabMemberId> mounts;
    for (const auto& member : captured.source.at("members"))
        if (member.contains("prefab_instance"))
            mounts.push_back(member.at("id").get<PrefabMemberId>());
    check(mounts.size() == 2 && mounts.back() == path.instances.front().mount,
          "Capture replaced native sibling order with UUID ordering");
    scene.set_prefab_sources({{child.asset(), child.source}, {captured.asset(), captured.source}});
    const auto instance = instantiate_prefab(scene, captured.asset());
    const auto reference =
        *prefab_member_path_reference(scene.document(), EntityId::parse(instance), path);
    check(reference != target && scene.entity(reference.entity.str()) &&
              scene.entity(instance).get<NavigationBehavior>().target == reference,
          "Captured nested references did not resolve through their new assembly occurrence");
    engine.world().evaluate_world_transforms();
    check(scene.entity(first).get<WorldTransform>().affine.m ==
              scene.entity(reference.entity.str()).get<WorldTransform>().affine.m,
          "The identity mount changed the linked cube's world transform");
    const auto unchanged = scene.document();
    scene.rename_entity(first, "Scene-only override");
    const auto modified = scene.snapshot();
    rejects([&] { create_prefab_source(scene, wrapper); });
    check(scene.snapshot() == modified, "Rejected linked override capture changed the scene");
    scene.edit(unchanged);
    const auto attachment = create("Scene-only attachment", 4);
    scene.reparent_entity(attachment, first, ReparentMode::KeepLocal);
    const auto attached = scene.snapshot();
    rejects([&] { create_prefab_source(scene, wrapper); });
    check(scene.snapshot() == attached, "Rejected linked attachment capture changed the scene");
}
void scoped_references() {
    Source outer, child, grandchild;
    const PrefabMemberPath path{outer.asset,
                                {{outer.mount, child.asset}, {child.mount, grandchild.asset}},
                                grandchild.mount};
    const auto encoded = Json(path);
    check(encoded.get<PrefabMemberPath>() == path,
          "Scoped member address did not retain its exact occurrence steps");
    auto linked = outer.document()
                      .with_nested_prefab(outer.root, child.asset)
                      .with_nested_prefab(outer.mount, child.asset)
                      .source;
    linked["members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr}, {"target", encoded}, {"anchor", nullptr}, {"enabled", true}};
    linked["members"][0]["components"]["forge.navigation_behavior"]["target"]["note"] =
        "Opaque reference extension";
    auto child_source = child.document().with_nested_prefab(child.mount, grandchild.asset).source;
    PrefabSources sources{{outer.asset, linked},
                          {child.asset, child_source},
                          {grandchild.asset, grandchild.document().source}};
    const auto baseline = sources;
    const auto duplicate = PrefabDocument(linked).duplicate();
    const auto copied_address = duplicate.source.at("members")[0]
                                    .at("components")
                                    .at("forge.navigation_behavior")
                                    .at("target");
    const auto copied_path = copied_address.get<PrefabMemberPath>();
    check(copied_path.prefab == duplicate.asset() && copied_path.member == grandchild.mount &&
              copied_path.instances[0].mount != outer.mount &&
              copied_path.instances[0].asset == child.asset &&
              copied_path.instances[1] == path.instances[1] &&
              copied_address.at("note") == "Opaque reference extension",
          "Duplication remapped another source's member identity or dropped extensions");
    EngineContext engine;
    Scene scene(engine.world());
    scene.set_prefab_sources(sources);
    const auto root = instantiate_prefab(scene, outer.asset);
    const auto second = instantiate_prefab(scene, outer.asset);
    auto document = scene.document();
    const auto target = *prefab_member_path_reference(document, EntityId::parse(root), path);
    const auto other = *prefab_member_path_reference(document, EntityId::parse(second), path);
    check(target != other && scene.entity(target.entity.str()) &&
              scene.entity(root).get<NavigationBehavior>().target == target &&
              scene.entity(second).get<NavigationBehavior>().target == other &&
              !entity_row(document, root).at("components").contains("forge.navigation_behavior") &&
              sources == baseline,
          "Native scoped reference crossed occurrences or became authored ownership");
    // Removing a link keeps the source reference and its own old asset scope.
    auto cleared = PrefabDocument(linked).with_nested_prefab(outer.mount, {}).source;
    cleared["revision"] = 2;
    sources[outer.asset] = cleared;
    scene.set_prefab_sources(sources);
    document = scene.document();
    check(prefab_member_path_reference(document, EntityId::parse(root), path) == target &&
              !scene.entity(target.entity.str()) &&
              scene.entity(root).get<NavigationBehavior>().target == target,
          "Clearing a mount retargeted its unresolved nested reference");
    EngineContext recovery_engine;
    Scene recovered(recovery_engine.world());
    recovered.restore_snapshot(scene.snapshot());
    check(recovered.document() == document &&
              recovered.entity(root).get<NavigationBehavior>().target == target,
          "Scoped unresolved reference lost its exact map on recovery");
    // A new instance must also retain an unresolved address whose mount link
    // was cleared before that instance ever existed.
    const auto fresh = instantiate_prefab(scene, outer.asset);
    document = scene.document();
    const auto reserved = *prefab_member_path_reference(document, EntityId::parse(fresh), path);
    check(!scene.entity(reserved.entity.str()) &&
              scene.entity(fresh).get<NavigationBehavior>().target == reserved,
          "A new instance could not reserve a dormant nested reference");
    linked["revision"] = 3;
    sources[outer.asset] = linked;
    scene.set_prefab_sources(sources);
    check(scene.entity(target.entity.str()) && scene.entity(reserved.entity.str()) &&
              scene.entity(root).get<NavigationBehavior>().target == target &&
              scene.entity(fresh).get<NavigationBehavior>().target == reserved,
          "Restoring the same link did not realize its old and fresh reference tombstones");
    auto absent = grandchild.document().source;
    absent["members"].erase(absent["members"].begin() + 1);
    absent["revision"] = 2;
    sources[grandchild.asset] = absent;
    scene.set_prefab_sources(sources);
    check(!scene.entity(target.entity.str()) &&
              scene.entity(root).get<NavigationBehavior>().target == target,
          "Removing a nested leaf changed a stable scoped EntityRef");
    auto returned = grandchild.document().source;
    returned["revision"] = 3;
    sources[grandchild.asset] = returned;
    scene.set_prefab_sources(sources);
    check(scene.entity(target.entity.str()) &&
              scene.entity(root).get<NavigationBehavior>().target == target,
          "Returning a nested leaf did not restore its original Scene identity");
    const auto variant = AssetId::generate();
    sources[variant] = outer.variant(variant, 4);
    scene.set_prefab_sources(sources);
    const auto derived = instantiate_prefab(scene, variant);
    auto inherited_path = path;
    inherited_path.prefab = variant;
    check(scene.entity(derived).get<NavigationBehavior>().target ==
              *prefab_member_path_reference(scene.document(), EntityId::parse(derived),
                                            inherited_path),
          "A derived source failed to retain the nested address namespace");
    const auto copy = duplicate_scene_asset(scene.document());
    auto checkpoint = scene.snapshot();
    checkpoint["asset_id"] = copy.at("asset_id");
    checkpoint["entities"] = copy.at("entities");
    recovered.restore_snapshot(checkpoint);
    check(recovered.asset_id() != scene.asset_id() && recovered.document() == copy,
          "Scene asset duplication did not preserve its new independent nested maps");
    for (const auto& row : copy.at("entities"))
        if (row.contains("prefab_instance") && !row.contains("prefab_nested") &&
            row.at("prefab_instance").at("asset") == Json(outer.asset)) {
            const auto id = row.at("id").get<EntityId>();
            const auto reference = *prefab_member_path_reference(copy, id, path);
            check(reference.scene == recovered.asset_id() && reference != target &&
                      recovered.entity(id.str()).get<NavigationBehavior>().target == reference,
                  "Scene asset duplication leaked the original nested reference scope");
        }
}
void supporting_source_publication(const std::filesystem::path& scratch) {
    const auto folder = scratch / AssetId::generate().str();
    std::filesystem::create_directories(scratch);
    SceneDocument::create_project(folder, "Nested composition");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(folder);
    auto& library = project.prefabs();
    Source outer, child;
    library.create(scene, child.document(), "Child.prefab.json");
    library.create(scene, outer.document(), "Outer.prefab.json");
    auto expected = library.source(outer.asset);
    auto candidate = PrefabDocument(expected).with_nested_prefab(outer.mount, child.asset).source;
    const auto before = scene.snapshot();
    rejects([&] { library.publish(scene, expected, candidate); });
    check(scene.snapshot() == before && library.source(outer.asset) == expected,
          "An unreviewed new dependency changed the source or live scene");
    auto reviewed = library.base_sources(scene, expected, &candidate);
    check(reviewed.size() == 1 && reviewed.at(child.asset) == child.document().source,
          "Candidate review did not include its new child source");
    library.publish(scene, expected, candidate, reviewed);
    const auto instance = instantiate_prefab(scene, outer.asset);
    const auto nested = nested_root(scene.document(), instance, outer.mount, child.asset);
    check(bool(scene.entity(nested)),
          "Published source link did not create its nested native group");
    expected = library.source(outer.asset);
    reviewed = library.base_sources(scene, expected);
    const auto before_update = scene.document();
    auto child_update = library.source(child.asset);
    child_update["members"][0]["name"] = "Updated child";
    library.publish(scene, library.source(child.asset), child_update);
    std::set<ecs_entity_t> parents;
    for (const auto& row : before_update.at("entities"))
        if (const auto entity = scene.entity(row.at("id")); entity && entity.parent())
            parents.insert(entity.parent().id());
    for (const auto parent : parents) {
        std::set<ecs_entity_t> seen;
        const auto children = ecs_get_ordered_children(scene.world(), parent);
        for (int32_t i = 0; i < children.count; ++i) {
            const auto id = children.ids[i];
            const auto entity = scene.world().entity(id);
            if (!seen.insert(id).second || entity.parent().id() != parent)
                throw std::runtime_error("Invalid native sibling order: parent=" +
                                         std::to_string(parent) + " child=" + std::to_string(id) +
                                         " actual_parent=" + std::to_string(entity.parent().id()));
        }
    }
    const auto retained = scene.snapshot();
    rejects([&] { library.publish(scene, expected, expected, reviewed); });
    check(scene.snapshot() == retained && library.source(outer.asset) == expected,
          "A stale nested source review partially published the outer source");
    const auto fresh = library.base_sources(scene, expected);
    library.publish(scene, expected, expected, fresh);
    check(nested_root(scene.document(), instance, outer.mount, child.asset) == nested,
          "Fresh source publication changed the nested occurrence identity");
    const auto second = instantiate_prefab(scene, outer.asset);
    const auto second_nested = nested_root(scene.document(), second, outer.mount, child.asset);
    const auto target = *prefab_member_reference(scene.document(), EntityId::parse(nested),
                                                 {child.asset, child.mount});
    const auto other_target = *prefab_member_reference(
        scene.document(), EntityId::parse(second_nested), {child.asset, child.mount});
    project.save();
    authoring_command(scene, "component.add",
                      {{"entity", instance}, {"component", "forge.navigation_behavior"}});
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.navigation_behavior"},
                       {"field", "target"},
                       {"value", target}});
    const auto applied = library.prepare_apply(scene, instance);
    const auto address = mount_row(applied.source, outer.root)
                             .at("components")
                             .at("forge.navigation_behavior")
                             .at("target")
                             .get<PrefabMemberPath>();
    check(address == PrefabMemberPath{outer.asset, {{outer.mount, child.asset}}, child.mount},
          "Apply failed to capture a nested inspector link through existing maps");
    project.apply_prefab(applied, scene.revision());
    check(scene.entity(instance).get<NavigationBehavior>().target == target &&
              scene.entity(second).get<NavigationBehavior>().target == other_target,
          "Paired nested-reference Apply crossed occurrence boundaries");
    check(scene.undo() && scene.entity(instance).get<NavigationBehavior>().target == target &&
              !scene.entity(second).has<NavigationBehavior>(),
          "Paired nested-reference Undo lost pre-Apply instance intent");
    check(scene.redo() && scene.entity(second).get<NavigationBehavior>().target == other_target,
          "Paired nested-reference Redo lost its portable scope");
    const auto variant = library.create_variant(scene, outer.asset, "NestedVariant.prefab.json");
    const auto derived = instantiate_prefab(scene, variant);
    const auto derived_nested = nested_root(scene.document(), derived, outer.mount, child.asset);
    const auto derived_target = *prefab_member_reference(
        scene.document(), EntityId::parse(derived_nested), {child.asset, child.mount});
    project.save();
    authoring_command(scene, "property.set",
                      {{"entity", derived},
                       {"component", "forge.navigation_behavior"},
                       {"field", "anchor"},
                       {"value", derived_target}});
    const auto promoted = library.prepare_apply(scene, derived);
    check(promoted.expected.at("version") == 4 && promoted.source.at("version") == 7,
          "Scoped Apply did not explicitly promote an older variant profile");
    project.apply_prefab(promoted, scene.revision());
    check(scene.entity(derived).get<NavigationBehavior>().anchor == derived_target &&
              scene.undo() &&
              scene.entity(derived).get<NavigationBehavior>().anchor == derived_target &&
              scene.redo(),
          "Promoted scoped variant Apply/Undo/Redo lost the selected instance's intent");
    const auto document = scene.document();
    const auto path = folder / "Nested.scene.json";
    write_scene_file(path, document);
    check(read_scene_file(path) == document,
          "Saved nested scene did not round-trip through the file owner");
    const auto duplicate = duplicate_scene_asset(document);
    EngineContext loaded_engine;
    Scene loaded(loaded_engine.world());
    library.load_scene(loaded, duplicate);
    check(loaded.document() == duplicate && loaded.asset_id() != scene.asset_id(),
          "Loading a duplicated nested Scene failed identity/instance admission");
}
void declared_dependency_compatibility(const std::filesystem::path& scratch) {
    const auto folder = scratch / AssetId::generate().str();
    std::filesystem::create_directories(folder);
    EngineContext engine;
    Scene scene(engine.world());
    PrefabLibrary library(folder);
    std::array<Source, 20> sources;
    for (std::size_t i = sources.size(); i-- > 0;) {
        auto document = sources[i].document().source;
        if (i + 1 < sources.size())
            document["dependencies"] = Json::array({sources[i + 1].asset});
        library.create(scene, PrefabDocument(document),
                       "Dependency" + std::to_string(i) + ".prefab.json");
    }
    const auto first = library.source(sources.front().asset);
    check(library.base_sources(scene, first).size() == sources.size() - 1,
          "Supporting review imposed a new nesting limit on older declared dependencies");
}
void root_binding_runtime() {
    Source outer, child;
    const auto defaults = [](const std::string& key) {
        for (const auto& type : detail::builtins())
            if (key == type.name)
                return type.defaults;
        throw std::runtime_error("Missing runtime fixture component");
    };
    const Json identity{{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                        {"forge.local_rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}},
                        {"forge.local_scale", {{"x", 1}, {"y", 1}, {"z", 1}}}};
    auto parent = outer.document().with_nested_prefab(outer.mount, child.asset).source;
    for (auto& row : parent["members"])
        row["components"] = identity;
    parent["members"][0]["components"]["forge.local_translation"]["x"] = 5;
    auto body_source = child.document().source;
    body_source["members"][0]["components"] = identity;
    auto& components = body_source["members"][0]["components"];
    components["forge.local_translation"]["y"] = 3;
    components["forge.physics_body"] = defaults("forge.physics_body");
    components["forge.physics_body"]["motion"] = 2;
    components["forge.box_collider"] = defaults("forge.box_collider");
    PrefabSources sources{{outer.asset, parent}, {child.asset, body_source}};
    EngineContext authoring;
    Scene authored(authoring.world());
    authored.set_prefab_sources(sources);
    const auto instance = instantiate_prefab(authored, outer.asset);
    const auto nested = nested_root(authored.document(), instance, outer.mount, child.asset);
    const auto structural_parent = authored.entity(nested).parent();
    authoring_command(authored, "transform.binding",
                      {{"entity", nested}, {"spatial", {{"mode", "world"}}}});
    check(authored.entity(nested).parent() == structural_parent &&
              authored.entity(nested).get<SpatialBinding>().mode == SpatialMode::World &&
              authored.entity(nested).get<LocalTranslation>().x == 5,
          "Linked root lost independent spatial binding or structural source ownership");
    const auto bound_document = authored.document();
    for (const auto& row : bound_document.at("entities"))
        if (row.at("id") == nested)
            check(!row.at("components").contains("forge.local_rotation") &&
                      !row.at("components").contains("forge.local_scale"),
                  "Changing the binding flattened unchanged inherited transform channels");
    body_source["revision"] = 2;
    body_source["members"][0]["name"] = "Dynamic linked root";
    sources[child.asset] = body_source;
    authored.set_prefab_sources(sources);
    check(authored.entity(nested).get<SpatialBinding>().mode == SpatialMode::World,
          "Child publication overwrote the instance-owned root binding");
    EngineContext live_engine(WorldRole::Runtime, false, {physics_module()});
    Scene live(live_engine.world());
    live.restore_snapshot(authored.snapshot());
    RuntimeSimulation simulation(live_engine.world(), live);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(live_engine.services().physics());
    for (unsigned i = 0; i < 8; ++i)
        simulation.tick(1.f / 60);
    check(physics->status().at("bodies") == 1 && live.entity(nested).get<LocalTranslation>().y < 3,
          "The nested World-space root did not reach actual native physics");
    EngineContext recovered_engine(WorldRole::Runtime, false, {physics_module()});
    Scene recovered(recovered_engine.world());
    recovered.restore_snapshot(live.snapshot());
    RuntimeSimulation resumed(recovered_engine.world(), recovered);
    const auto restored_physics =
        std::static_pointer_cast<PhysicsRuntime>(recovered_engine.services().physics());
    restored_physics->restore(physics->checkpoint());
    for (unsigned i = 0; i < 8; ++i) {
        simulation.tick(1.f / 60);
        resumed.tick(1.f / 60);
    }
    check(recovered.entity(nested).get<SpatialBinding>().mode == SpatialMode::World &&
              std::abs(live.entity(nested).get<LocalTranslation>().y -
                       recovered.entity(nested).get<LocalTranslation>().y) < .0001,
          "Nested root binding/physics continuation changed during recovery");
}
void unavailable_presentation() {
    Source outer, child;
    auto source = outer.document().with_nested_references().source;
    source["members"][0]["components"]["forge.navigation_behavior"] = {
        {"enabled", false},
        {"graph", nullptr},
        {"target", PrefabMemberPath{outer.asset, {{outer.mount, child.asset}}, child.root}},
        {"anchor", nullptr}};
    EngineContext engine(WorldRole::Runtime, false, {animation_module({})});
    Scene scene(engine.world());
    scene.set_prefab_sources({{outer.asset, source}, {child.asset, child.document().source}});
    const auto root = instantiate_prefab(scene, outer.asset);
    const auto ghost = nested_root(scene.document(), root, outer.mount, child.asset);
    check(!scene.entity(ghost), "An unavailable scoped reference fabricated a native entity");
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.tick(1.f / 60);
    const auto frame = simulation.presentation(1);
    bool found = false;
    for (const auto& row : frame.at("entities"))
        if (row.at("id") == ghost) {
            found = true;
            check(row.value("missing_member", false) && !row.at("state_active").get<bool>(),
                  "Unavailable presentation lost its retained identity or activity state");
        }
    check(found, "Presentation discarded the retained unavailable occurrence");
}
} // namespace
int main(int argc, char** argv) {
    try {
        source_candidates();
        member_choice_labels();
        closure_and_inheritance();
        dependency_admission();
        native_lifecycle();
        older_scene_extensions();
        linked_capture();
        scoped_references();
        check(argc == 2, "Provide an isolated composition scratch directory");
        supporting_source_publication(std::filesystem::absolute(argv[1]));
        declared_dependency_compatibility(std::filesystem::absolute(argv[1]));
        root_binding_runtime();
        unavailable_presentation();
        std::cout << "Nested source candidates, inheritance, identities and closure bounds pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
