#include "../src/authored_component.hpp"
#include "../src/runtime_entity_creation.hpp"
#include "prefab_variant_fixture.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation_components.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/schema.hpp>
#include <iostream>

using namespace forge;
namespace {
constexpr auto component = "forge.navigation_surface";
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
    throw std::runtime_error("Invalid component suppression was admitted");
}
struct Fixture {
    AssetId base = AssetId::generate(), variant = AssetId::generate();
    PrefabMemberId root = PrefabMemberId::generate(), child = PrefabMemberId::generate();
    Json original{{"format", "forge.prefab"},
                  {"version", 3},
                  {"asset_id", base},
                  {"revision", 1},
                  {"root", root},
                  {"members", Json::array({{{"id", root},
                                            {"name", "Root"},
                                            {"components", {{component, {{"enabled", true}}}}}},
                                           {{"id", child},
                                            {"name", "Child"},
                                            {"parent", root},
                                            {"components", {{component, {{"enabled", true}}}}}}})}};
    Json derived{{"format", "forge.prefab"},
                 {"version", 4},
                 {"asset_id", variant},
                 {"revision", 1},
                 {"root", root},
                 {"base", base},
                 {"overrides", Json::array()}};
    Json suppressed() const {
        return PrefabDocument(derived).with_component_suppression(root, component, true).source;
    }
    PrefabSources sources() const { return {{base, original}, {variant, suppressed()}}; }
};
void source_intent() {
    Fixture f;
    const auto suppressed = f.suppressed();
    check(suppressed.at("version") == 5 && suppressed.at("asset_id") == Json(f.variant) &&
              suppressed.at("revision") == 1 && suppressed.at("root") == Json(f.root) &&
              suppressed.at("overrides")[0].at("suppressed_components") == Json::array({component}),
          "Opt-in changed identity/revision or failed to retain explicit suppression");
    check(core_document_schemas().prepare("prefab", suppressed) == suppressed,
          "Schema preparation flattened source5 intent");
    const auto raw = f.sources();
    const auto resolved = resolve_prefab_sources(raw);
    check(raw == f.sources() && !resolved.at(f.variant).contains("overrides") &&
              !resolved.at(f.variant).at("members")[0].at("components").contains(component) &&
              resolved.at(f.variant).at("members")[1].at("components").contains(component),
          "Resolution mutated source or suppressed another stable member");
    const auto restored =
        PrefabDocument(suppressed).with_component_suppression(f.root, component, false).source;
    check(restored.at("version") == 5 && restored.at("overrides").empty(),
          "Restore retained suppression or silently downgraded the source profile");
    const auto copy = PrefabDocument(suppressed).duplicate();
    check(copy.asset() != f.variant && copy.source.at("base") == Json(f.base) &&
              copy.source.at("overrides") == suppressed.at("overrides"),
          "Duplicate changed stable suppression targets");
    auto with_overrides = f.derived;
    with_overrides["overrides"] =
        Json::array({{{"id", f.root},
                      {"name", "Keep name"},
                      {"components", {{component, {{"enabled", false}}}}},
                      {"property_overrides", {{component, {{"enabled", false}}}}}}});
    const auto replaced =
        PrefabDocument(with_overrides).with_component_suppression(f.root, component, true).source;
    check(replaced.at("overrides")[0].at("components").empty() &&
              !replaced.at("overrides")[0].contains("property_overrides") &&
              replaced.at("overrides")[0].at("name") == "Keep name",
          "Suppress did not replace only the selected component's explicit overrides");
    // Every older profile keeps this spelling opaque. Upgrading a different
    // member cannot accidentally activate that data.
    for (unsigned version : {1u, 2u, 3u, 4u}) {
        auto old = f.original;
        old["version"] = version;
        old["members"][0]["suppressed_components"] = Json::array({component});
        check(resolve_prefab_sources({{f.base, old}}).at(f.base) == old,
              "An older positive source gained suppression semantics");
    }
    auto old = f.derived;
    old["overrides"] = Json::array({{{"id", f.child},
                                     {"components", Json::object()},
                                     {"suppressed_components", Json::array({component})}}});
    check(resolve_prefab_sources({{f.base, f.original}, {f.variant, old}})
              .at(f.variant)
              .at("members")[1]
              .at("components")
              .contains(component),
          "Source4 opaque extension was interpreted as suppression");
    rejects([&] { PrefabDocument(old).with_component_suppression(f.root, component, true); });
    check(PrefabDocument(old).with_component_suppression(f.child, component, false).source == old,
          "Source4 Revert discarded an opaque extension");
    for (const auto* key :
         {"forge.tint", "forge.local_translation", "forge.local_rotation", "forge.local_scale",
          "forge.world_transform", "forge.position", "unavailable.component"})
        rejects([&] { PrefabDocument(f.derived).with_component_suppression(f.root, key, true); });
    for (unsigned mode = 0; mode < 9; ++mode) {
        auto bad = suppressed;
        auto& row = bad["overrides"][0];
        if (mode == 0)
            row["suppressed_components"] = component;
        if (mode == 1)
            row["suppressed_components"].push_back(component);
        if (mode == 2)
            row["suppressed_components"] = Json::array({42});
        if (mode == 3)
            row["suppressed_components"] = Json::array({""});
        if (mode == 4)
            row["suppressed_components"] = Json::array({std::string(257, 'x')});
        if (mode == 5)
            row["components"][component] = {{"enabled", true}};
        if (mode == 6)
            row["property_overrides"][component] = {{"enabled", true}};
        if (mode == 7)
            row["suppressed_components"] = Json::array({"forge.tint"});
        if (mode == 8) {
            row["suppressed_components"] = Json::array();
            for (unsigned i = 0; i < 257; ++i)
                row["suppressed_components"].push_back("unavailable." + std::to_string(i));
        }
        rejects([&] { (void)PrefabDocument(bad); });
    }
    auto dormant = suppressed;
    dormant["overrides"][0]["suppressed_components"] = Json::array({"unavailable.component"});
    check(PrefabDocument(dormant).source == dormant, "Unknown suppression intent was discarded");
    Json admitted{
        {"components", Json::array({{{"id", "unavailable.component"}, {"optional", false}}})}};
    rejects(
        [&] { resolve_prefab_sources({{f.base, f.original}, {f.variant, dormant}}, admitted); });
}
void chained_sources() {
    Fixture f;
    auto chain = f.sources();
    const auto leaf = AssetId::generate();
    chain[leaf] = f.derived;
    chain[leaf]["asset_id"] = leaf;
    chain[leaf]["base"] = f.variant;
    check(!resolve_prefab_sources(chain)
               .at(leaf)
               .at("members")[0]
               .at("components")
               .contains(component),
          "A source4 child did not inherit a source5 base's suppression");
    chain[leaf]["overrides"] =
        Json::array({{{"id", f.root}, {"components", {{component, {{"enabled", false}}}}}}});
    check(resolve_prefab_sources(chain)
              .at(leaf)
              .at("members")[0]
              .at("components")
              .contains(component),
          "A derived child could not explicitly own a suppressed base component");
    chain[f.variant]["base"] = leaf;
    rejects([&] { (void)resolve_prefab_sources(chain); });
    auto complete5 = f.original;
    complete5["version"] = 5;
    check(PrefabDocument(complete5).source == complete5,
          "Source5 complete definitions changed during validation");
    auto unsupported = f.suppressed();
    unsupported["version"] = PrefabDocument::current_version + 1;
    rejects([&] { (void)PrefabDocument(unsupported); });
}
void native_lifecycle() {
    Fixture f;
    EngineContext engine(WorldRole::Authoring, false, {test::variant_controls_module()});
    Scene scene(engine.world());
    auto sources = f.sources();
    const auto& codec = engine.world().authored_codecs().front();
    const auto custom = codec.key();
    sources[f.base]["members"][0]["components"][custom] = codec.defaults();
    sources[f.variant] = PrefabDocument(sources.at(f.variant))
                             .with_component_suppression(f.root, custom, true, scene.schema())
                             .source;
    scene.set_prefab_sources(sources);
    const auto base = instantiate_prefab(scene, f.base);
    const auto variant = instantiate_prefab(scene, f.variant);
    check(scene.entity(base).has<NavigationSurface>() &&
              !scene.entity(variant).has<NavigationSurface>() &&
              scene.entity(base).has<test::VariantControls>() &&
              !scene.entity(variant).has<test::VariantControls>(),
          "Compiled templates retained suppressed builtin/custom components");
    auto invalid = sources;
    invalid[f.variant]["revision"] = 2;
    invalid[f.variant]["overrides"][0]["components"][component] = {{"enabled", true}};
    const auto before = scene.snapshot();
    rejects([&] { scene.set_prefab_sources(invalid); });
    check(scene.snapshot() == before, "Rejected source5 publication lost last-good native state");
    EngineContext runtime(WorldRole::Runtime, false, {test::variant_controls_module()});
    Scene resumed(runtime.world());
    resumed.restore_snapshot(before);
    check(!resumed.entity(variant).has<NavigationSurface>() &&
              !resumed.entity(variant).has<test::VariantControls>(),
          "Snapshot recovery resurrected suppressed components");
    const auto request = detail::request_runtime_prefab(runtime.world(), "project.suppression", {},
                                                        f.variant, "Spawned suppressed variant");
    detail::publish_runtime_entities(runtime.world());
    const auto spawned =
        detail::inspect_runtime_entity(runtime.world(), "project.suppression", request);
    check(spawned.state == detail::RuntimeEntityState::Ready &&
              !runtime.world().world().entity(spawned.native_entity).has<NavigationSurface>() &&
              !runtime.world().world().entity(spawned.native_entity).has<test::VariantControls>(),
          "Runtime creation resurrected suppressed components");
    sources[f.base]["revision"] = 2;
    sources[f.base]["members"][0]["components"].erase(component);
    scene.set_prefab_sources(sources);
    sources[f.base]["revision"] = 3;
    sources[f.base]["members"][0]["components"][component] = {{"enabled", false}};
    scene.set_prefab_sources(sources);
    check(!scene.entity(variant).has<NavigationSurface>() &&
              scene.prefab_sources().at(f.variant) == sources.at(f.variant),
          "Base removal/return erased suppression or restored native storage");
    sources[f.variant] = PrefabDocument(sources.at(f.variant))
                             .with_component_suppression(f.root, component, false)
                             .source;
    sources[f.variant]["revision"] = 2;
    scene.set_prefab_sources(sources);
    check(scene.entity(variant).has<NavigationSurface>() &&
              !scene.entity(variant).get<NavigationSurface>().enabled &&
              !scene.entity(variant).has<test::VariantControls>(),
          "Restore failed to inherit the latest base or restored another component");
    sources[f.variant]["revision"] = 3;
    sources[f.variant] = PrefabDocument(sources.at(f.variant))
                             .with_component_suppression(f.child, component, true)
                             .source;
    const auto member = sources.at(f.base).at("members")[1];
    sources[f.base]["members"].erase(1);
    sources[f.base]["revision"] = 4;
    scene.set_prefab_sources(sources);
    sources[f.base]["members"].push_back(member);
    sources[f.base]["revision"] = 5;
    scene.set_prefab_sources(sources);
    const auto child =
        *prefab_member_reference(scene.document(), EntityId::parse(variant), {f.variant, f.child});
    check(!scene.entity(child.entity.str()).has<NavigationSurface>(),
          "Returning member identity lost its dormant suppression");
}
} // namespace
int main() {
    try {
        source_intent();
        chained_sources();
        native_lifecycle();
        std::cout << "Prefab source5 suppression, compatibility and native recovery passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
