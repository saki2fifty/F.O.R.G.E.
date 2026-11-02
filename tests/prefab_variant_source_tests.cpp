#include "../src/asset_reference_impact.hpp"
#include "../src/authored_component.hpp"
#include "prefab_variant_fixture.hpp"
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/navigation_behavior_components.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/schema.hpp>
#include <iostream>
#include <stdexcept>

using namespace forge;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid variant source was admitted");
}
struct Fixture {
    AssetId base = AssetId::generate(), variant = AssetId::generate();
    PrefabMemberId root = PrefabMemberId::generate(), child = PrefabMemberId::generate();
    Json original = {
        {"format", "forge.prefab"},
        {"version", 1},
        {"asset_id", base},
        {"revision", 1},
        {"root", root},
        {"members",
         Json::array({{{"id", root},
                       {"name", "Base"},
                       {"components", {{"forge.tint", {{"r", 1}, {"g", .5}, {"b", .25}}}}}},
                      {{"id", child},
                       {"name", "Child"},
                       {"parent", root},
                       {"components", Json::object()}}})}};
    Json derived = {
        {"format", "forge.prefab"},
        {"version", 4},
        {"asset_id", variant},
        {"revision", 1},
        {"root", root},
        {"base", base},
        {"overrides",
         Json::array({{{"id", root},
                       {"name", "Variant"},
                       {"components", Json::object()},
                       {"property_overrides", {{"forge.tint", {{"r", 1}}}}}},
                      {{"id", child},
                       {"components", {{"forge.navigation_surface", {{"enabled", false}}}}}}})}};
    PrefabSources sources() const { return {{base, original}, {variant, derived}}; }
};
void inheritance() {
    Fixture f;
    const auto registry = core_document_schemas();
    for (const unsigned version : {1u, 2u, 3u, 4u}) {
        auto complete = f.original;
        complete["version"] = version;
        check(registry.prepare("prefab", complete) == complete,
              "Schema discovery rejected or implicitly converted a complete positive source");
    }
    check(registry.prepare("prefab", f.derived) == f.derived,
          "Schema discovery flattened variant intent or changed identity");
    const auto raw = f.sources();
    const auto selected = resolve_prefab_sources(raw);
    check(raw == f.sources(), "Resolution mutated authored sources");
    check(selected.at(f.base) == f.original, "A positive legacy definition changed");
    const auto& value = selected.at(f.variant);
    check(value.at("asset_id").get<AssetId>() == f.variant &&
              value.at("root") == f.original.at("root") &&
              value.at("members").at(0).at("id") == f.original.at("members").at(0).at("id") &&
              value.at("members").at(0).at("name") == "Variant" &&
              value.at("members")
                      .at(1)
                      .at("components")
                      .at("forge.navigation_surface")
                      .at("enabled") == false,
          "Variant identity, structure, name or component intent changed");
    auto changed = raw;
    changed[f.base]["revision"] = 2;
    changed[f.base]["members"][0]["components"]["forge.tint"]["r"] = .9;
    changed[f.base]["members"][0]["components"]["forge.tint"]["g"] = .75;
    const auto updated = resolve_prefab_sources(changed).at(f.variant);
    const auto& color = updated.at("members").at(0).at("components").at("forge.tint");
    check(color.at("r") == 1 && color.at("g") == .75 && updated.at("revision") == 1,
          "Equal-value override disappeared or an unmodified field stopped inheriting");
    check(value.at("$forge.variant") != updated.at("$forge.variant"),
          "Base changes did not invalidate derived template evidence");
    auto revision_only = raw;
    revision_only[f.base]["revision"] = 2;
    check(resolve_prefab_sources(revision_only).at(f.variant).at("$forge.variant") !=
              value.at("$forge.variant"),
          "A base revision change was invisible to derived preparation");
    changed[f.variant]["overrides"][0]["property_overrides"]["forge.tint"].erase("r");
    check(resolve_prefab_sources(changed)
                  .at(f.variant)
                  .at("members")
                  .at(0)
                  .at("components")
                  .at("forge.tint")
                  .at("r") == .9,
          "Revert did not restore inherited field value");
    const auto child = changed.at(f.base).at("members").at(1);
    changed[f.base]["members"].erase(1);
    check(resolve_prefab_sources(changed).at(f.variant).at("members").size() == 1 &&
              changed.at(f.variant).at("overrides").size() == 2,
          "Missing-member intent was interpreted as a replacement or erased");
    changed[f.base]["members"].push_back(child);
    check(resolve_prefab_sources(changed)
              .at(f.variant)
              .at("members")
              .at(1)
              .at("components")
              .contains("forge.navigation_surface"),
          "Returning member identity did not restore its retained variant intent");
}
void references() {
    Fixture f;
    f.original["version"] = 3;
    const Json target{{"prefab", f.base}, {"member", f.child}, {"note", "Keep extension"}};
    const Json opaque{{"uuid_like", target}};
    f.original["members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr}, {"target", target}, {"anchor", nullptr}, {"enabled", true}};
    f.original["members"][0]["components"]["unavailable.plugin"] = opaque;
    const Json anchor{{"prefab", f.variant}, {"member", f.root}};
    f.derived["overrides"][0]["property_overrides"]["forge.navigation_behavior"] = {
        {"anchor", anchor}};
    const auto resolved = resolve_prefab_sources(f.sources()).at(f.variant);
    const auto& values = resolved.at("members").at(0).at("components");
    check(values.at("forge.navigation_behavior").at("target").at("prefab").get<AssetId>() ==
                  f.variant &&
              values.at("forge.navigation_behavior").at("target").at("member") ==
                  target.at("member") &&
              values.at("forge.navigation_behavior").at("target").at("note") == target.at("note") &&
              values.at("forge.navigation_behavior").at("anchor") == anchor,
          "Typed reference inheritance did not retain member/extension semantics");
    check(values.at("unavailable.plugin") == opaque, "Opaque UUID-like data was rewritten");
    const auto copy = PrefabDocument(f.derived).duplicate();
    check(copy.asset() != f.variant && copy.source.at("base") == f.derived.at("base") &&
              copy.source.at("root") == f.derived.at("root") &&
              copy.source.at("overrides")
                      .at(0)
                      .at("property_overrides")
                      .at("forge.navigation_behavior")
                      .at("anchor")
                      .at("prefab")
                      .get<AssetId>() == copy.asset(),
          "Variant duplication changed base member identity or retained old reference namespace");
    f.derived["overrides"][0]["property_overrides"]["forge.navigation_behavior"]["anchor"]
             ["prefab"] = f.base;
    rejects([&] { (void)resolve_prefab_sources(f.sources()); });
    Fixture legacy;
    const Json absolute = EntityRef{AssetId::generate(), EntityId::generate()};
    legacy.original["members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr}, {"target", absolute}, {"anchor", nullptr}, {"enabled", true}};
    const Json member{{"prefab", legacy.variant}, {"member", legacy.child}};
    legacy.derived["overrides"][0]["property_overrides"]["forge.navigation_behavior"] = {
        {"anchor", member}};
    const auto mixed = resolve_prefab_sources(legacy.sources()).at(legacy.variant);
    check(mixed.at("members").at(0).at("components").at("forge.navigation_behavior").at("anchor") ==
              member,
          "A variant could not add a member link while retaining a legacy Scene link");
    check(resolve_prefab_sources(legacy.sources())
                  .at(legacy.variant)
                  .at("members")
                  .at(0)
                  .at("components")
                  .at("forge.navigation_behavior")
                  .at("target") == absolute,
          "Legacy absolute reference semantics changed implicitly");
    auto ambiguous = legacy.sources();
    ambiguous[legacy.variant]["overrides"][0]["property_overrides"]["forge.navigation_behavior"]
             ["anchor"]["scene"] = AssetId::generate();
    rejects([&] { (void)resolve_prefab_sources(ambiguous); });
    auto strict = legacy.original;
    strict["version"] = 3;
    rejects([&] { (void)PrefabDocument(strict); });
}
void invalid_graphs() {
    Fixture f;
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto sources = f.sources();
        auto& value = sources.at(f.variant);
        if (mode == 0)
            sources.erase(f.base);
        if (mode == 1)
            value["root"] = PrefabMemberId::generate();
        if (mode == 2)
            value["reference_profile"] = "member";
        if (mode == 3)
            value["overrides"].push_back(value["overrides"][0]);
        if (mode == 4)
            value["overrides"][0]["parent"] = f.child;
        if (mode == 5)
            sources[f.base] = {{"format", "foreign.asset"}};
        const auto before = sources;
        rejects([&] { (void)resolve_prefab_sources(sources); });
        check(sources == before, "Rejected graph mutated authored inputs");
    }
    auto cyclic = f.sources();
    cyclic[f.base] = f.derived;
    cyclic[f.base]["asset_id"] = f.base;
    cyclic[f.base]["base"] = f.variant;
    rejects([&] { (void)resolve_prefab_sources(cyclic); });
    auto chain = PrefabSources{{f.base, f.original}};
    auto base = f.base;
    for (unsigned index = 1; index <= 17; ++index) {
        auto value = f.derived;
        const auto asset = AssetId::generate();
        value["asset_id"] = asset;
        value["base"] = base;
        chain.emplace(asset, std::move(value));
        base = asset;
        if (index == 16)
            check(resolve_prefab_sources(chain).size() == 17, "Sixteen base links were rejected");
    }
    rejects([&] { (void)resolve_prefab_sources(chain); });
}
void native_instances() {
    Fixture f;
    EngineContext engine;
    Scene scene(engine.world());
    const auto unrelated =
        authoring_command(scene, "entity.create", {{"kind", 4}}).at("selected").get<std::string>();
    const auto unrelated_handle = scene.entity(unrelated).id();
    auto sources = f.sources();
    scene.set_prefab_sources(sources);
    const auto base = instantiate_prefab(scene, f.base);
    const auto first = instantiate_prefab(scene, f.variant);
    const auto second = instantiate_prefab(scene, f.variant);
    check(scene.entity(first).get<Tint>().r == 1 && scene.entity(second).get<Tint>().g == .5f,
          "Native variant did not inherit prepared component values");
    authoring_command(
        scene, "property.set",
        {{"entity", first}, {"component", "forge.tint"}, {"field", "g"}, {"value", .25}});
    sources[f.base]["revision"] = 2;
    sources[f.base]["members"][0]["components"]["forge.tint"]["r"] = .9;
    sources[f.base]["members"][0]["components"]["forge.tint"]["g"] = .75;
    scene.set_prefab_sources(sources);
    const auto first_value = scene.entity(first).get<Tint>();
    const auto second_value = scene.entity(second).get<Tint>();
    check(first_value.r == 1 && first_value.g == .25f && second_value.r == 1 &&
              second_value.g == .75f && scene.entity(base).get<Tint>().r == .9f &&
              scene.entity(unrelated).id() == unrelated_handle,
          "Base update lost variant/instance intent or replaced an unrelated native entity");
    const auto before = scene.snapshot();
    auto invalid = sources;
    // Equal effective value still changes raw override intent and needs a revision.
    invalid[f.variant]["overrides"][0]["property_overrides"]["forge.tint"]["g"] = .75;
    rejects([&] { scene.set_prefab_sources(invalid); });
    check(scene.snapshot() == before && scene.prefab_sources() == sources,
          "Rejected immutable variant edit changed source or native state");
    const auto& definitions = before.at("_prefab_sources");
    check(definitions.size() == 2, "Private snapshot omitted the variant's base source");
    for (const auto& definition : definitions)
        check(!definition.contains("$forge.variant"),
              "Derived cache evidence entered authored recovery sources");
    EngineContext resumed;
    Scene recovered(resumed.world());
    recovered.restore_snapshot(before);
    check(recovered.document() == scene.document() &&
              recovered.entity(first).get<Tint>() == first_value &&
              recovered.entity(second).get<Tint>() == second_value,
          "Recovery changed inherited or explicitly overridden native values");
}
void native_member_links() {
    Fixture f;
    f.original["version"] = 3;
    f.original["members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr},
        {"target", {{"prefab", f.base}, {"member", f.child}}},
        {"anchor", nullptr},
        {"enabled", true}};
    EngineContext engine;
    Scene scene(engine.world());
    auto sources = f.sources();
    scene.set_prefab_sources(sources);
    const auto first = instantiate_prefab(scene, f.variant),
               second = instantiate_prefab(scene, f.variant);
    const auto reference = [&](const std::string& root) {
        return *prefab_member_reference(scene.document(), EntityId::parse(root),
                                        {f.variant, f.child});
    };
    const auto first_child = reference(first), second_child = reference(second);
    check(first_child != second_child &&
              scene.entity(first).get<NavigationBehavior>().target == first_child &&
              scene.entity(second).get<NavigationBehavior>().target == second_child,
          "Native variant references escaped their own instance map");
    const auto child = sources.at(f.base).at("members").at(1);
    sources[f.base]["revision"] = 2;
    sources[f.base]["members"].erase(1);
    scene.set_prefab_sources(sources);
    check(reference(first) == first_child && reference(second) == second_child &&
              !scene.entity(first_child.entity.str()) && !scene.entity(second_child.entity.str()),
          "Removed variant member was retargeted instead of retaining its unresolved address");
    sources[f.base]["revision"] = 3;
    sources[f.base]["members"].push_back(child);
    scene.set_prefab_sources(sources);
    check(scene.entity(first_child.entity.str()) && scene.entity(second_child.entity.str()) &&
              scene.entity(first).get<NavigationBehavior>().target == first_child &&
              scene.entity(second).get<NavigationBehavior>().target == second_child,
          "Returning base member identity did not restore independent native references");
}
void native_base_reference_upgrade() {
    Fixture f;
    EngineContext engine;
    Scene scene(engine.world());
    const auto external =
        authoring_command(scene, "entity.create", {{"kind", 4}}).at("selected").get<std::string>();
    const auto absolute = scene.reference(external);
    f.original["members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr}, {"target", absolute}, {"anchor", nullptr}, {"enabled", true}};
    f.derived["overrides"][0]["property_overrides"]["forge.navigation_behavior"] = {
        {"anchor", {{"prefab", f.variant}, {"member", f.child}}}};
    auto sources = f.sources();
    scene.set_prefab_sources(sources);
    const auto instance = instantiate_prefab(scene, f.variant);
    const auto child =
        *prefab_member_reference(scene.document(), EntityId::parse(instance), {f.variant, f.child});
    check(scene.entity(instance).get<NavigationBehavior>().target == absolute &&
              scene.entity(instance).get<NavigationBehavior>().anchor == child,
          "Native composition conflated absolute and member reference domains");
    sources[f.base]["version"] = 3;
    sources[f.base]["revision"] = 2;
    sources[f.base]["members"][0]["components"]["forge.navigation_behavior"]["target"] = {
        {"prefab", f.base}, {"member", f.child}};
    scene.set_prefab_sources(sources);
    check(scene.prefab_sources().at(f.variant) == f.derived &&
              scene.entity(instance).get<NavigationBehavior>().target == child &&
              scene.entity(instance).get<NavigationBehavior>().anchor == child,
          "Base reference upgrade required a variant rewrite or lost its reference intent");
}
void library_and_content(const std::filesystem::path& scratch) {
    const auto folder = scratch / AssetId::generate().str();
    std::filesystem::create_directories(folder);
    Fixture f;
    EngineContext engine;
    Scene scene(engine.world());
    PrefabLibrary library(folder);
    library.create(scene, PrefabDocument(f.original), "Base.prefab.json");
    library.create(scene, PrefabDocument(f.derived), "Variant.prefab.json");
    const auto blank = library.create_variant(scene, f.variant, "Child.prefab.json");
    const auto duplicate = library.duplicate(scene, f.variant, "Duplicate.prefab.json");
    check(library.source(blank).at("base") == Json(f.variant) &&
              library.source(blank).at("overrides").empty() &&
              library.source(duplicate).at("base") == Json(f.base) &&
              library.source(duplicate).at("root") == Json(f.root),
          "Creation or duplication replaced the variant's base/member identities");
    const auto first = instantiate_prefab(scene, f.variant);
    const auto second = instantiate_prefab(scene, f.variant);
    authoring_command(
        scene, "property.set",
        {{"entity", first}, {"component", "forge.tint"}, {"field", "g"}, {"value", .75}});
    const auto apply = library.prepare_apply(scene, first);
    check(!apply.source.contains("members") && apply.source.at("base") == Json(f.base) &&
              apply.source.at("overrides").at(0).at("components").empty() &&
              apply.source.at("overrides").at(0).at("property_overrides").at("forge.tint") ==
                  Json({{"r", 1}, {"g", .75}}),
          "Apply flattened a variant or replaced inherited fields with whole values");
    library.publish_apply(
        scene, apply, [&] { atomic_write(folder / "Variant.prefab.json", apply.source.dump(2)); });
    check(library.source(f.base) == f.original && scene.entity(first).get<Tint>().g == .75f &&
              scene.entity(second).get<Tint>().g == .75f,
          "Variant Apply wrote its base or failed to update independent instances");
    const auto expected = library.source(f.variant);
    const auto reviewed = library.base_sources(scene, expected);
    auto base_update = f.original;
    base_update["members"][0]["components"]["forge.tint"]["b"] = .8;
    library.publish(scene, f.original, base_update);
    check(scene.entity(second).get<Tint>().b == .8f,
          "Apply stored an inherited field as explicit variant intent");
    const auto retained = scene.snapshot();
    rejects([&] { library.publish(scene, expected, expected, reviewed); });
    rejects([&] { library.publish_apply(scene, apply, [] {}); });
    check(scene.snapshot() == retained && library.source(f.variant) == expected,
          "A stale base review changed files or native values");
    const auto fresh_bases = library.base_sources(scene, expected);
    auto next = expected;
    next["overrides"][0]["name"] = "Reviewed variant";
    library.publish(scene, expected, next, fresh_bases);
    const auto disk_base = library.source(f.base);
    auto external = disk_base;
    external["revision"] = 3;
    atomic_write(folder / "Base.prefab.json", external.dump(2));
    const auto disk_variant = library.source(f.variant);
    rejects([&] { library.publish(scene, disk_variant, disk_variant, fresh_bases); });
    check(library.source(f.variant) == disk_variant,
          "An external base edit was overwritten by variant publication");
    atomic_write(folder / "Base.prefab.json", disk_base.dump(2));
    library.refresh(scene);
    scene.save(folder / "main.scene.json");
    AssetCatalog catalog(folder);
    const auto scene_record = catalog.add_scene("main.scene.json");
    for (const auto& [id, record] : library.records()) {
        (void)id;
        catalog.add(record);
    }
    catalog.save(AssetCatalog::project_index(folder));
    const auto& edges = catalog.dependency_graph().dependencies(f.variant);
    check(std::any_of(edges.begin(), edges.end(),
                      [&](const auto& edge) {
                          return edge.target == f.base && edge.expected_type == PrefabAsset::type &&
                                 edge.kind == AssetDependencyKind::Runtime &&
                                 edge.role == "document:base";
                      }),
          "Variant base was not indexed as a typed Runtime dependency");
    const auto loaded = load_game_scene(folder, {scene_record.id});
    check(loaded.at("_prefab_sources").size() == 2,
          "Runtime scene loading omitted the base or included unrelated variant assets");
    EngineContext runtime;
    Scene restored(runtime.world());
    restored.restore_snapshot(loaded);
    check(restored.entity(second).get<Tint>().b == .8f,
          "Runtime closure did not realize inherited base values");
    auto reflected = f.derived;
    reflected["overrides"][0]["property_overrides"]["forge.navigation_behavior"] = {
        {"target", {{"scene", scene.asset_id()}, {"entity", EntityId::parse(first)}}},
        {"anchor", {{"prefab", f.variant}, {"member", f.child}}}};
    const std::array documents{AssetReferenceDocument{"Variant.prefab.json", reflected}};
    const auto references = collect_asset_references(scene.schema(), documents);
    check(references.uninspected.empty() && references.references.size() == 2 &&
              std::any_of(references.references.begin(), references.references.end(),
                          [&](const auto& hit) {
                              return hit.target == f.base && hit.property == "base";
                          }) &&
              std::any_of(references.references.begin(), references.references.end(),
                          [&](const auto& hit) { return hit.target == scene.asset_id(); }),
          "Reference inspection missed the base/absolute reference or exposed an internal link");
    auto oversized = library.source(f.variant);
    oversized["retained_extension"] = std::string(PrefabLibrary::max_source_bytes, 'x');
    const auto before_large = scene.snapshot();
    rejects([&] {
        library.publish(scene, library.source(f.variant), oversized,
                        library.base_sources(scene, oversized));
    });
    oversized["asset_id"] = AssetId::generate();
    oversized["revision"] = 1;
    rejects([&] { library.create(scene, PrefabDocument(oversized), "TooLarge.prefab.json"); });
    check(scene.snapshot() == before_large &&
              !std::filesystem::exists(folder / "TooLarge.prefab.json"),
          "An oversized source changed files or the admitted world");
    std::filesystem::remove_all(folder);
}
void complete_source_reference_apply(const std::filesystem::path& scratch) {
    const auto folder = scratch / AssetId::generate().str();
    std::filesystem::create_directories(folder);
    Fixture f;
    EngineContext engine;
    Scene scene(engine.world());
    const auto external =
        authoring_command(scene, "entity.create", {{"kind", 4}}).at("selected").get<std::string>();
    f.original["version"] = 4;
    const Json absolute = scene.reference(external);
    f.original["members"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr},
        {"target", absolute},
        {"anchor", {{"prefab", f.base}, {"member", f.child}}},
        {"enabled", true}};
    PrefabLibrary library(folder);
    library.create(scene, PrefabDocument(f.original), "Complete.prefab.json");
    const auto instance = instantiate_prefab(scene, f.base);
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.navigation_behavior"},
                       {"field", "anchor"},
                       {"value", scene.reference(instance)}});
    const auto candidate = library.prepare_apply(scene, instance);
    const auto& component =
        candidate.source.at("members")[0].at("components").at("forge.navigation_behavior");
    check(candidate.source.at("version") == 4 && component.at("target") == absolute &&
              component.at("anchor") == Json(PrefabMemberRef{f.base, f.root}),
          "Complete source4 Apply converted an untouched absolute reference or downgraded its "
          "format");
    library.publish_apply(scene, candidate, [&] {
        atomic_write(folder / "Complete.prefab.json", candidate.source.dump(2));
    });
    check(scene.entity(instance).get<NavigationBehavior>().target == scene.reference(external) &&
              scene.entity(instance).get<NavigationBehavior>().anchor == scene.reference(instance),
          "Complete source4 Apply lost explicitly addressed native reference domains");
    // A formerly opaque root extension keeps its legacy meaning; it is not
    // retroactively interpreted as a variant base in source1–3.
    Fixture legacy;
    legacy.original["base"] = {{"unavailable_extension", "before"}};
    library.create(scene, PrefabDocument(legacy.original), "Legacy.prefab.json");
    auto changed = legacy.original;
    changed["base"] = {{"unavailable_extension", "after"}};
    library.publish(scene, legacy.original, changed);
    check(library.source(legacy.base).at("base") == changed.at("base"),
          "Variant publication rules reinterpreted a legacy opaque extension");
    std::filesystem::remove_all(folder);
}
void custom_variant_intent() {
    Fixture f;
    EngineContext engine(WorldRole::Authoring, false, {test::variant_controls_module()});
    Scene scene(engine.world());
    const auto& codec = engine.world().authored_codecs().front();
    f.original["version"] = 3;
    auto value = codec.defaults();
    value["target"] = PrefabMemberRef{f.base, f.child};
    const Json opaque{{"prefab", f.base}, {"member", f.child}};
    value["unavailable_extension"] = opaque;
    f.original["members"][0]["components"][codec.key()] = value;
    f.derived["overrides"][0]["property_overrides"][codec.key()] = {
        {"$forge", codec.stamp()},
        {"speed", 90},
        {"target", PrefabMemberRef{f.variant, f.root}},
        {"future.intent", opaque}};
    auto sources = f.sources();
    scene.set_prefab_sources(sources);
    const auto first = instantiate_prefab(scene, f.variant),
               second = instantiate_prefab(scene, f.variant);
    auto own = scene.entity(second).get<test::VariantControls>();
    check(own.speed == 90 && own.phase == .5 && own.target == scene.reference(second),
          "Admitted custom variant mask/reference failed native realization");
    const auto duplicate = PrefabDocument(f.derived).duplicate(scene.schema());
    check(
        duplicate.source.at("overrides")[0].at("property_overrides").at(codec.key()).at("target") ==
                Json(PrefabMemberRef{duplicate.asset(), f.root}) &&
            duplicate.source.at("overrides")[0]
                    .at("property_overrides")
                    .at(codec.key())
                    .at("future.intent") == opaque,
        "Custom duplication lost reference ownership or rewrote unknown intent");
    authoring_command(
        scene, "property.set",
        {{"entity", first}, {"component", codec.key()}, {"field", "speed"}, {"value", 7}});
    sources[f.base]["revision"] = 2;
    sources[f.base]["members"][0]["components"][codec.key()]["speed"] = 180;
    sources[f.base]["members"][0]["components"][codec.key()]["phase"] = 2.5;
    scene.set_prefab_sources(sources);
    check(scene.entity(first).get<test::VariantControls>().speed == 7 &&
              scene.entity(second).get<test::VariantControls>().speed == 90 &&
              scene.entity(second).get<test::VariantControls>().phase == 2.5,
          "Base/custom variant/instance masks did not compose independently");
    sources[f.variant]["revision"] = 2;
    sources[f.variant]["overrides"][0]["property_overrides"][codec.key()]["$forge"]["digest"] =
        "unavailable-schema";
    scene.set_prefab_sources(sources);
    const auto child =
        *prefab_member_reference(scene.document(), EntityId::parse(second), {f.variant, f.child});
    own = scene.entity(second).get<test::VariantControls>();
    check(own.speed == 180 && own.target == child &&
              scene.prefab_sources().at(f.variant) == sources.at(f.variant),
          "An unavailable variant mask was interpreted or discarded");
    const auto effective = scene.effective_document();
    for (const auto& row : effective.at("entities"))
        if (row.at("id") == second)
            check(row.at("components").at(codec.key()).at("unavailable_extension") == opaque,
                  "Custom extension was retargeted during variant reference projection");
    EngineContext runtime(WorldRole::Runtime, false, {test::variant_controls_module()});
    Scene recovered(runtime.world());
    recovered.restore_snapshot(scene.snapshot());
    check(recovered.entity(first).get<test::VariantControls>().speed == 7 &&
              recovered.entity(second).get<test::VariantControls>().target == child,
          "Runtime custom variant recovery lost masks or member identity");
}
} // namespace
int main(int argc, char** argv) {
    try {
        inheritance();
        references();
        invalid_graphs();
        native_instances();
        native_member_links();
        native_base_reference_upgrade();
        check(argc == 2, "Need a variant scratch directory");
        library_and_content(std::filesystem::absolute(argv[1]));
        complete_source_reference_apply(std::filesystem::absolute(argv[1]));
        custom_variant_intent();
        std::cout << "Detached prefab variant identity, intent, references and graph bounds pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
