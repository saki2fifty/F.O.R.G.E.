#pragma once
#include <forge/authoring.hpp>
#include <forge/fog_volume.hpp>
#include <forge/prefab_authoring.hpp>
namespace test_fog_prefab {
template <class Check> void exercise(Check check) {
    using namespace forge;
    WorldContext world;
    Scene scene(world);
    const auto create = [&](const char* name) {
        return authoring_command(scene, "entity.create",
                                 {{"recipe", "render.fog_volume"}, {"name", name}})
            .at("selected")
            .template get<std::string>();
    };
    const auto parent = create("Outer mist");
    const auto child = create("Child mist");
    scene.reparent_entity(child, parent, ReparentMode::KeepLocal);
    const auto outer = create_prefab_source(scene, parent);
    PrefabSources sources{{outer.asset(), outer.source}};
    auto base = outer.asset();
    for (double density : {.5, .75}) {
        const auto id = AssetId::generate();
        Json variant{
            {"format", "forge.prefab"},
            {"version", 4},
            {"asset_id", id},
            {"revision", 1},
            {"root", outer.root()},
            {"base", base},
            {"overrides", Json::array({{{"id", outer.root()},
                                        {"components", Json::object()},
                                        {"property_overrides",
                                         {{"forge.fog_volume", {{"density", density}}}}}}})}};
        PrefabDocument::validate(variant);
        sources.emplace(id, variant);
        base = id;
    }
    scene.set_prefab_sources(sources);
    const auto instance = instantiate_prefab(scene, base);
    check(scene.entity(instance).template get<FogVolume>().density == .75,
          "Chained Fog variant did not retain inherited native values");
    const auto inherited = scene.document();
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.fog_volume"},
                       {"field", "density"},
                       {"value", 0}});
    const auto authored = scene.document();
    check(scene.entity(instance).template get<FogVolume>().density == 0 && scene.undo() &&
              scene.document() == inherited && scene.redo() && scene.document() == authored,
          "Explicit zero Fog override or history was lost");
    const auto snapshot = scene.snapshot();
    check(snapshot.at("_prefab_sources").size() == 3,
          "Fog snapshot omitted a base or chained variant source");
    WorldContext recovered_world;
    Scene recovered(recovered_world);
    recovered.restore_snapshot(snapshot);
    check(recovered.document() == authored &&
              recovered.entity(instance).template get<FogVolume>().density == 0,
          "Fog recovery flattened intent or lost explicit disabled density");
    unsigned child_count = 0;
    const auto effective = recovered.effective_document();
    for (const auto& row : effective.at("entities"))
        if (row.value("parent", std::string{}) == instance &&
            row.at("components").contains("forge.fog_volume")) {
            ++child_count;
            check(row.at("components").at("forge.fog_volume").at("density") ==
                      Json(FogVolume{}.density),
                  "Fog child lost its independent inherited defaults");
        }
    check(child_count == 1, "Fog prefab instance did not recover its child");
}
} // namespace test_fog_prefab
