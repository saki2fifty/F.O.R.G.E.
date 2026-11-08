#include "../src/asset_reference_impact.hpp"
#include "prefab_variant_fixture.hpp"
#include <array>
#include <cmath>
#include <forge/navigation_behavior_components.hpp>
#include <forge/prefab_authoring.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid source contribution was accepted");
}
const Json& member(const Json& source, PrefabMemberId id) {
    for (const auto& row : source.at("members"))
        if (row.at("id") == Json(id))
            return row;
    throw std::runtime_error("Missing source-layer member");
}
struct Family {
    AssetId base = AssetId::generate();
    PrefabMemberId root = PrefabMemberId::generate(), child = PrefabMemberId::generate();
    Json document() const {
        return {
            {"format", "forge.prefab"},
            {"version", 3},
            {"asset_id", base},
            {"revision", 1},
            {"root", root},
            {"members", Json::array({{{"id", root},
                                      {"name", "Common root"},
                                      {"components",
                                       {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                        {"forge.tint", {{"r", .2}, {"g", .3}, {"b", .4}}},
                                        {"forge.navigation_behavior",
                                         {{"graph", nullptr},
                                          {"target", PrefabMemberRef{base, child}},
                                          {"anchor", nullptr},
                                          {"enabled", false}}}}}},
                                     {{"id", child},
                                      {"parent", root},
                                      {"name", "Common child"},
                                      {"components", Json::object()}}})}};
    }
    PrefabDocument variant(AssetId asset, AssetId parent = {}) const {
        return PrefabDocument({{"format", "forge.prefab"},
                               {"version", 4},
                               {"asset_id", asset},
                               {"revision", 1},
                               {"root", root},
                               {"base", parent ? parent : base},
                               {"overrides", Json::array()}});
    }
};
void candidates() {
    Family family;
    const auto primary = AssetId::generate(), other = AssetId::generate(),
               asset = AssetId::generate();
    const auto raw = family.variant(asset, primary);
    const auto layered = raw.with_mixins({other});
    check(raw.source.at("version") == 4 && layered.source.at("version") == 8 &&
              layered.base_assets() == std::vector<AssetId>{primary, other} &&
              layered.dependencies() == layered.base_assets(),
          "Source candidate changed its baseline, order or typed inheritance dependencies");
    const auto cleared = layered.with_mixins({});
    check(cleared.source.at("version") == 8 && !cleared.source.contains("mixins") &&
              cleared.base_assets() == std::vector<AssetId>{primary},
          "Removing additional contributors downgraded the source or retained a dependency");
    check(layered.with_nested_references().source.at("version") == 8 &&
              layered.with_nested_prefab(family.child, family.base).source.at("version") == 8,
          "Existing nesting operations downgraded a newer source profile");
    auto opaque = raw.source;
    opaque["mixins"] = "Older extension";
    check(PrefabDocument(opaque).source == opaque, "Older mixins spelling lost opaque data");
    rejects([&] { PrefabDocument(opaque).with_mixins({other}); });
    rejects([&] { raw.with_mixins({primary}); });
    rejects([&] { raw.with_mixins({other, other}); });
    rejects([&] { raw.with_mixins({asset}); });
    rejects([&] { raw.with_mixins({AssetId{}}); });
    std::vector<AssetId> oversized;
    for (unsigned i = 0; i < 16; ++i)
        oversized.push_back(AssetId::generate());
    rejects([&] { raw.with_mixins(oversized); });
    oversized.pop_back();
    check(raw.with_mixins(oversized).base_assets().size() == 16,
          "The inclusive16-input candidate limit rejected a legal selection");
    auto complete = family.document();
    complete["version"] = 8;
    complete["mixins"] = Json::array();
    rejects([&] { (void)PrefabDocument(complete); });
}
void diamond_values_and_native() {
    Family family;
    const auto a = AssetId::generate(), b = AssetId::generate(), asset = AssetId::generate();
    auto left = family.variant(a).source, right = family.variant(b).source;
    left["overrides"].push_back({{"id", family.root},
                                 {"components", Json::object()},
                                 {"property_overrides", {{"forge.tint", {{"r", .8}}}}}});
    right["overrides"].push_back(
        {{"id", family.root},
         {"components", Json::object()},
         {"property_overrides",
          {{"forge.tint", {{"g", .9}}},
           {"forge.navigation_behavior", {{"target", PrefabMemberRef{b, family.root}}}}}}});
    const auto combined = family.variant(asset, a).with_mixins({b}).source;
    PrefabSources sources{
        {family.base, family.document()}, {a, left}, {b, right}, {asset, combined}};
    const auto before = sources;
    const auto resolved = resolve_prefab_sources(sources);
    const auto& definition = resolved.at(asset);
    const auto& components = member(definition, family.root).at("components");
    check(definition.at("version") == 8 && !definition.contains("base") &&
              !definition.contains("mixins") && definition.at("members").size() == 2 &&
              definition.at("root") == Json(family.root),
          "Completed source retained raw contribution fields or changed inherited identities");
    check(components.at("forge.tint") == Json{{"r", .8}, {"g", .9}, {"b", .4}},
          "A shared ancestor replayed defaults over independent explicit field contributions");
    check(components.at("forge.navigation_behavior").at("target") ==
                  Json(PrefabMemberRef{asset, family.root}) &&
              sources == before,
          "Source composition lost portable reference ownership or rewrote raw sources");
    EngineContext engine;
    Scene scene(engine.world());
    scene.set_prefab_sources(sources);
    const auto instance = instantiate_prefab(scene, asset);
    const auto root = scene.entity(instance);
    check(root.get<NavigationBehavior>().target == scene.reference(instance) &&
              std::abs(root.get<Tint>().r - .8f) < 1e-6f &&
              std::abs(root.get<Tint>().g - .9f) < 1e-6f,
          "The normal compiled template/native member maps lost composed values");
    sources[b]["revision"] = 2;
    sources[b]["overrides"][0]["property_overrides"]["forge.tint"]["g"] = .7;
    scene.set_prefab_sources(sources);
    check(scene.entity(instance).id() != 0 &&
              std::abs(scene.entity(instance).get<Tint>().g - .7f) < 1e-6f &&
              sources.at(asset) == combined,
          "An additional source update did not invalidate the composed template independently");
}
void graph_rejection() {
    Family family, unrelated;
    const auto a = AssetId::generate(), b = AssetId::generate(), asset = AssetId::generate();
    const auto combined = family.variant(asset, a).with_mixins({b}).source;
    PrefabSources sources{{family.base, family.document()},
                          {a, family.variant(a).source},
                          {b, family.variant(b).source},
                          {asset, combined}};
    auto invalid = sources;
    invalid.erase(b);
    rejects([&] { resolve_prefab_sources(invalid); });
    invalid = sources;
    invalid[a]["base"] = asset;
    rejects([&] { resolve_prefab_sources(invalid); });
    invalid = sources;
    invalid[b] = unrelated.variant(b).source;
    invalid.emplace(unrelated.base, unrelated.document());
    rejects([&] { resolve_prefab_sources(invalid); });
    // Matching member UUIDs alone do not establish a common declaration origin.
    auto foreign = family.document();
    foreign["asset_id"] = unrelated.base;
    foreign["members"][0]["components"]["forge.navigation_behavior"]["target"] =
        PrefabMemberRef{unrelated.base, family.child};
    (void)PrefabDocument(foreign);
    invalid = sources;
    invalid[b] = family.variant(b, unrelated.base).source;
    invalid.emplace(unrelated.base, foreign);
    rejects([&] { resolve_prefab_sources(invalid); });
    const auto p = AssetId::generate(), q = AssetId::generate(), top = AssetId::generate();
    sources.emplace(p, family.variant(p, a).with_mixins({b}).source);
    sources.emplace(q, family.variant(q, b).with_mixins({a}).source);
    sources.emplace(top, family.variant(top, p).with_mixins({q}).source);
    rejects([&] { resolve_prefab_sources(sources); });
}
Json added_member(PrefabMemberId id, PrefabMemberId parent) {
    return {{"id", id},
            {"parent", parent},
            {"name", "Local contribution"},
            {"components", Json::object()}};
}
void precedence_and_older_profiles() {
    Family family;
    const auto a = AssetId::generate(), b = AssetId::generate(), asset = AssetId::generate();
    auto left = family.variant(a).source, right = family.variant(b).source;
    // These spellings were opaque in source4 and cannot gain meaning when used
    // as an ancestor of a source8 variant.
    left["mixins"] = "Opaque older extension";
    left["added_members"] = "Opaque older structure";
    left["overrides"].push_back({{"id", family.root},
                                 {"components", Json::object()},
                                 {"removed", true},
                                 {"suppressed_components", Json::array({"forge.tint"})},
                                 {"property_overrides", {{"forge.tint", {{"r", .8}}}}}});
    right["overrides"].push_back({{"id", family.root},
                                  {"components", Json::object()},
                                  {"name", "Additional source"},
                                  {"property_overrides", {{"forge.tint", {{"g", .9}}}}}});
    auto combined = family.variant(asset, a).with_mixins({b}).source;
    combined["overrides"].push_back({{"id", family.root},
                                     {"components", Json::object()},
                                     {"name", "Local source"},
                                     {"property_overrides", {{"forge.tint", {{"g", .6}}}}}});
    PrefabSources sources{
        {family.base, family.document()}, {a, left}, {b, right}, {asset, combined}};
    auto resolved = resolve_prefab_sources(sources);
    check(member(resolved.at(asset), family.root).at("name") == "Local source" &&
              member(resolved.at(asset), family.root).at("components").at("forge.tint") ==
                  Json{{"r", .8}, {"g", .6}, {"b", .4}} &&
              sources.at(a) == left,
          "Local precedence or older opaque field semantics changed");
    sources[asset]["overrides"] = Json::array();
    resolved = resolve_prefab_sources(sources);
    check(member(resolved.at(asset), family.root).at("name") == "Additional source" &&
              member(resolved.at(asset), family.root).at("components").at("forge.tint").at("g") ==
                  .9,
          "Removing local intent did not follow the composed inherited view");
    sources[a] = family.variant(a)
                     .with_component_suppression(family.root, "forge.navigation_behavior", true)
                     .source;
    sources[b]["overrides"][0]["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr},
        {"target", PrefabMemberRef{b, family.root}},
        {"anchor", nullptr},
        {"enabled", false}};
    sources[b]["overrides"][0].erase("property_overrides");
    check(member(resolve_prefab_sources(sources).at(asset), family.root)
                  .at("components")
                  .at("forge.navigation_behavior")
                  .at("target") == Json(PrefabMemberRef{asset, family.root}),
          "A later explicit whole component failed to replace an earlier suppression");
    const auto legacy = AssetId::generate();
    auto old = family.variant(legacy, asset).source;
    old["mixins"] = "Still opaque";
    old["added_members"] = "Still opaque";
    sources.emplace(legacy, old);
    const auto completed = resolve_prefab_sources(sources).at(legacy);
    check(completed.at("version") == 7 && completed.at("mixins") == "Still opaque" &&
              !completed.contains("added_members") && sources.at(legacy) == old,
          "An older descendant activated opaque source8 fields or changed its raw source");
}
void structural_contributions_and_origins() {
    Family family;
    const auto a = AssetId::generate(), b = AssetId::generate(), asset = AssetId::generate();
    const auto local_a = PrefabMemberId::generate(), local_b = PrefabMemberId::generate();
    auto left = family.variant(a).with_added_member(added_member(local_a, family.root));
    auto right = family.variant(b).with_added_member(added_member(local_b, family.root));
    auto combined = family.variant(asset, a)
                        .with_mixins({b})
                        .with_member_parent(family.child, local_b)
                        .with_member_order(family.root, {local_b, local_a});
    PrefabSources sources{{family.base, family.document()},
                          {a, left.source},
                          {b, right.source},
                          {asset, combined.source}};
    const auto resolved = resolve_prefab_sources(sources).at(asset);
    std::vector<PrefabMemberId> siblings;
    for (const auto& row : resolved.at("members"))
        if (row.value("parent", Json()) == Json(family.root))
            siblings.push_back(row.at("id").get<PrefabMemberId>());
    check(resolved.at("members").size() == 4 &&
              member(resolved, family.child).at("parent") == Json(local_b) &&
              siblings == std::vector<PrefabMemberId>{local_b, local_a},
          "Independent structural declarations or local ordering lost their existing owner");
    auto invalid = sources;
    invalid[b]["added_members"][0]["id"] = local_a;
    invalid[b]["added_members"][0]["removed"] = true;
    rejects([&] { resolve_prefab_sources(invalid); });
    for (unsigned kind = 0; kind < 6; ++kind) {
        invalid = sources;
        Json row{{"id", family.child}, {"components", Json::object()}};
        if (kind == 0)
            row["id"] = local_b;
        else if (kind == 1)
            row["parent"] = local_b;
        else if (kind == 2)
            row["spatial"] = {{"mode", "explicit"}, {"member", local_b}};
        else if (kind == 3)
            invalid[a]["member_order"] =
                Json::array({{{"parent", family.root}, {"members", Json::array({local_b})}}});
        else
            row[kind == 4 ? "components" : "property_overrides"]["forge.navigation_behavior"] = {
                {"graph", nullptr},
                {"target", PrefabMemberRef{a, local_b}},
                {"anchor", nullptr},
                {"enabled", false}};
        invalid[a]["overrides"].push_back(row);
        rejects([&] { resolve_prefab_sources(invalid); });
    }
    // A source's own descendant may address either admitted contributor.
    sources[asset]["overrides"][0]["property_overrides"] = {
        {"forge.navigation_behavior", {{"target", PrefabMemberRef{asset, local_b}}}}};
    sources[a]["added_members"][0]["components"]["plugin.opaque"] = {
        {"target", PrefabMemberRef{a, local_b}}};
    check(member(resolve_prefab_sources(sources).at(asset), local_a)
                  .at("components")
                  .at("plugin.opaque")
                  .at("target") == Json(PrefabMemberRef{a, local_b}),
          "An unadmitted plugin payload was interpreted as a reference");
    // Retained addresses in a legacy child must also reserve declarations from
    // additional ancestors, including declarations which are currently removed.
    sources[b] = right.with_member_removed(local_b, true).source;
    sources[asset] = family.variant(asset, a).with_mixins({b}).source;
    const auto descendant = AssetId::generate();
    sources.emplace(descendant, family.variant(descendant, asset)
                                    .with_added_member(added_member(local_b, family.root))
                                    .source);
    rejects([&] { resolve_prefab_sources(sources); });
}
void scoped_reference_namespace() {
    Family family;
    const auto a = AssetId::generate(), b = AssetId::generate(), asset = AssetId::generate();
    Family nested;
    const auto peer_local = nested.root; // A different child namespace may use the same UUID.
    auto base =
        PrefabDocument(family.document()).with_nested_prefab(family.child, nested.base).source;
    auto left = family.variant(a).with_nested_references().source;
    const PrefabMemberPath reference{a, {{family.child, nested.base}}, nested.root};
    left["overrides"].push_back(
        {{"id", family.root},
         {"components", Json::object()},
         {"property_overrides", {{"forge.navigation_behavior", {{"target", reference}}}}}});
    const auto right =
        family.variant(b).with_added_member(added_member(peer_local, family.root)).source;
    PrefabSources sources{{family.base, base},
                          {nested.base, nested.document()},
                          {a, left},
                          {b, right},
                          {asset, family.variant(asset, a).with_mixins({b}).source}};
    const auto resolved = resolve_prefab_sources(sources).at(asset);
    auto expected = reference;
    expected.prefab = asset;
    check(member(resolved, family.root)
                  .at("components")
                  .at("forge.navigation_behavior")
                  .at("target") == Json(expected),
          "Scoped references captured a child leaf as an outer source declaration");
    EngineContext engine;
    Scene scene(engine.world());
    scene.set_prefab_sources(sources);
    const auto root = instantiate_prefab(scene, asset);
    const auto target =
        prefab_member_path_reference(scene.document(), EntityId::parse(root), expected);
    check(target && scene.entity(root).get<NavigationBehavior>().target == *target,
          "Composed scoped references failed through the native instance maps");
}
void contributor_budget() {
    Family family;
    PrefabSources sources{{family.base, family.document()}};
    std::vector<AssetId> groups;
    for (unsigned group = 0; group < 4; ++group) {
        std::vector<AssetId> leaves;
        for (unsigned i = 0; i < (group == 3 ? 13u : 15u); ++i) {
            const auto leaf = AssetId::generate();
            sources.emplace(leaf, family.variant(leaf).source);
            leaves.push_back(leaf);
        }
        const auto asset = AssetId::generate();
        const auto primary = leaves.front();
        leaves.erase(leaves.begin());
        sources.emplace(asset, family.variant(asset, primary).with_mixins(leaves).source);
        groups.push_back(asset);
    }
    const auto top = AssetId::generate();
    sources.emplace(
        top,
        family.variant(top, groups.front()).with_mixins({groups[1], groups[2], groups[3]}).source);
    check(sources.size() == 64 && resolve_prefab_sources(sources).at(top).at("members").size() == 2,
          "The inclusive64-contributor budget rejected a legal shallow graph");
    const auto extra = AssetId::generate();
    sources.emplace(extra, family.variant(extra).source);
    auto mixins = sources.at(groups.back()).at("mixins").get<std::vector<AssetId>>();
    mixins.push_back(extra);
    sources[groups.back()] = PrefabDocument(sources.at(groups.back())).with_mixins(mixins).source;
    rejects([&] { resolve_prefab_sources(sources); });
}
void admitted_custom_contributions() {
    EngineContext engine(WorldRole::Authoring, false, {test::variant_controls_module()});
    Scene scene(engine.world());
    const auto schema = scene.schema();
    const auto type = std::find_if(
        schema.at("components").begin(), schema.at("components").end(),
        [](const Json& value) { return value.at("id") == "project.variant_controls"; });
    check(type != schema.at("components").end(), "Missing custom source-layer fixture admission");
    Family family;
    const auto a = AssetId::generate(), b = AssetId::generate(), asset = AssetId::generate();
    auto base = family.document();
    auto values = type->at("defaults");
    values["$forge"] = type->at("admission");
    values["target"] = PrefabMemberRef{family.base, family.child};
    base["members"][0]["components"]["project.variant_controls"] = values;
    auto left = family.variant(a).source, right = family.variant(b).source;
    left["overrides"].push_back(
        {{"id", family.root},
         {"components", Json::object()},
         {"property_overrides",
          {{"project.variant_controls", {{"$forge", type->at("admission")}, {"speed", 240}}}}}});
    right["overrides"].push_back({{"id", family.root},
                                  {"components", Json::object()},
                                  {"property_overrides",
                                   {{"project.variant_controls",
                                     {{"$forge", type->at("admission")},
                                      {"phase", .75},
                                      {"target", PrefabMemberRef{b, family.root}}}}}}});
    PrefabSources sources{{family.base, base},
                          {a, left},
                          {b, right},
                          {asset, family.variant(asset, a).with_mixins({b}).source}};
    scene.set_prefab_sources(sources);
    const auto instance = instantiate_prefab(scene, asset);
    const auto native = scene.entity(instance).get<test::VariantControls>();
    check(native.speed == 240 && native.phase == .75 && native.target == scene.reference(instance),
          "Admitted custom sparse fields or references did not reach native components");
    const auto local = PrefabMemberId::generate();
    sources[b] = PrefabDocument(right).with_added_member(added_member(local, family.root)).source;
    auto& intent = sources[a]["overrides"][0]["property_overrides"]["project.variant_controls"];
    intent["target"] = PrefabMemberRef{a, local};
    rejects([&] { resolve_prefab_sources(sources, schema); });
    intent["$forge"] = "Unavailable schema extension";
    const auto completed = resolve_prefab_sources(sources, schema).at(asset);
    check(member(completed, family.root)
                      .at("components")
                      .at("project.variant_controls")
                      .at("speed") == 90 &&
              sources[a]["overrides"][0]["property_overrides"]["project.variant_controls"]
                     ["target"] == Json(PrefabMemberRef{a, local}),
          "An unadmitted custom payload gained meaning or was rewritten");
}
void source_copy_and_reference_impact() {
    Family family;
    const auto primary = AssetId::generate(), secondary = AssetId::generate(),
               asset = AssetId::generate();
    const auto local = PrefabMemberId::generate();
    auto source = family.variant(asset, primary).with_mixins({secondary});
    auto row = added_member(local, family.root);
    const Json opaque = PrefabMemberRef{asset, local};
    row["components"]["forge.navigation_behavior"] = {
        {"graph", nullptr},
        {"target", opaque},
        {"anchor", PrefabMemberRef{asset, family.child}},
        {"enabled", false}};
    row["unavailable_extension"] = opaque;
    source = source.with_added_member(row);
    EngineContext engine;
    const auto schema = engine.world().schema();
    const auto copy = source.duplicate(schema);
    const auto& copied = copy.source.at("added_members")[0];
    const auto copied_id = copied.at("id").get<PrefabMemberId>();
    const auto& controls = copied.at("components").at("forge.navigation_behavior");
    check(copy.asset() != asset && copied_id != local && copy.root() == source.root() &&
              copy.base_assets() == source.base_assets() && copy.source.at("version") == 8 &&
              controls.at("target") == Json(PrefabMemberRef{copy.asset(), copied_id}) &&
              controls.at("anchor") == Json(PrefabMemberRef{copy.asset(), family.child}) &&
              copied.at("unavailable_extension") == opaque,
          "Source8 duplication changed contributor identity or lost owned/admitted reference "
          "remapping");
    PrefabSources sources{{family.base, family.document()},
                          {primary, family.variant(primary).source},
                          {secondary, family.variant(secondary).source},
                          {asset, source.source},
                          {copy.asset(), copy.source}};
    const auto resolved = resolve_prefab_sources(sources, schema);
    check(member(resolved.at(asset), local).at("parent") == Json(family.root) &&
              member(resolved.at(copy.asset()), copied_id).at("parent") == Json(family.root),
          "Copied source8 owned declarations could not coexist through the shared resolver");
    const auto inspect = [&](const Json& raw) {
        const std::array documents{AssetReferenceDocument{"Contributions.prefab.json", raw}};
        return collect_asset_references(schema, documents);
    };
    const auto candidate = family.variant(asset, primary).with_mixins({secondary}).source;
    const auto impact = inspect(candidate);
    check(impact.uninspected.empty() && impact.references.size() == 2 &&
              std::any_of(impact.references.begin(), impact.references.end(),
                          [&](const auto& hit) {
                              return hit.target == secondary && hit.property == "mixins.0" &&
                                     hit.expected_type == "prefab";
                          }),
          "Source8 reference inspection omitted the typed additional input");
    auto older = candidate;
    older["version"] = 7;
    const auto old_impact = inspect(older);
    check(old_impact.references.size() == 1 && !old_impact.uninspected.empty() &&
              old_impact.references[0].target == primary,
          "Reference inspection activated an older opaque mixins field");
}
} // namespace
int main() {
    try {
        candidates();
        diamond_values_and_native();
        graph_rejection();
        precedence_and_older_profiles();
        structural_contributions_and_origins();
        scoped_reference_namespace();
        contributor_budget();
        admitted_custom_contributions();
        source_copy_and_reference_impact();
        std::cout << "Prefab source contribution candidates, diamond values, native references and "
                     "graph bounds pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
