#pragma once
#include <array>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/navigation_behavior_components.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <set>

namespace forge::test_prefab_source_layers_gallery {
inline constexpr auto scene_path = "Scenes/208-prefab-source-contributions.scene.json";
inline constexpr auto scene_uuid = "5c2cd1e1-e220-445a-aba2-a99a0d43fbc6";
inline constexpr auto common_path = "Assets/Variants/CappedMarker.prefab.json";
inline constexpr std::array<const char*, 3> prefab_paths{
    "Assets/Contributions/WideMarker.prefab.json", "Assets/Contributions/BrightMarker.prefab.json",
    "Assets/Contributions/WideBrightMarker.prefab.json"};
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
inline AssetId asset_at(const AssetCatalog& catalog, const std::filesystem::path& path) {
    for (const auto& [asset, record] : catalog.records())
        if (record.source == path && record.type == PrefabAsset::type)
            return asset;
    throw std::runtime_error("Source contribution Gallery asset is unavailable");
}
inline void author(const std::filesystem::path& project) {
    check(!std::filesystem::exists(project / scene_path), "Gallery208 already exists");
    for (const auto* path : prefab_paths)
        check(!std::filesystem::exists(project / path), "Gallery208 requires new source paths");
    auto lease = std::make_shared<ProjectLease>(project);
    auto catalog = AssetCatalog::open_project(project);
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(
        duplicate_scene_asset(scene.document(), AssetId::parse(scene_uuid), scene.schema()));
    PrefabLibrary library(project);
    library.refresh(scene);
    const auto common = asset_at(catalog, common_path);
    const auto original = library.source(common);
    const auto root = original.at("root").get<PrefabMemberId>();
    PrefabMemberId body, crown;
    for (const auto& row : original.at("members")) {
        if (row.at("name") == "Body")
            body = row.at("id").get<PrefabMemberId>();
        if (row.at("name") == "Crown")
            crown = row.at("id").get<PrefabMemberId>();
    }
    check(bool(body) && bool(crown), "Common Gallery source requires Body and Crown members");
    const auto wide = library.create_variant(scene, common, prefab_paths[0]);
    auto expected = library.source(wide), candidate = expected;
    for (const auto& row : original.at("members"))
        if (row.at("id") == Json(body) || row.at("id") == Json(crown)) {
            auto scale = row.at("components").at("forge.local_scale");
            scale["x"] = row.at("id") == Json(body) ? 2.2 : 2.8;
            candidate["overrides"].push_back(
                {{"id", row.at("id")}, {"components", {{"forge.local_scale", scale}}}});
        }
    library.publish(scene, expected, candidate, library.base_sources(scene, expected, &candidate));
    const auto bright = library.create_variant(scene, common, prefab_paths[1]);
    expected = library.source(bright);
    candidate = expected;
    const Json color_parameters =
        Json::array({{{"slot", "surface"},
                      {"key", "baseColorFactor"},
                      {"parameter", {{"type", 5}, {"value", Json::array({0.0, 1.0, 1.0, 1.0})}}}}});
    for (const auto member : {body, crown})
        candidate["overrides"].push_back(
            {{"id", member},
             {"components", Json::object()},
             {"property_overrides",
              {{"forge.mesh_renderer", {{"material_parameters", color_parameters}}}}}});
    candidate["overrides"].push_back({{"id", root},
                                      {"components",
                                       {{"forge.navigation_behavior",
                                         {{"enabled", false},
                                          {"graph", nullptr},
                                          {"target", PrefabMemberRef{bright, body}},
                                          {"anchor", PrefabMemberRef{bright, crown}}}}}}});
    library.publish(scene, expected, candidate, library.base_sources(scene, expected, &candidate));
    const auto combined = library.create_variant(scene, wide, prefab_paths[2]);
    expected = library.source(combined);
    candidate = PrefabDocument(expected).with_mixins({bright}).source;
    library.publish(scene, expected, candidate, library.base_sources(scene, expected, &candidate));
    check(library.source(common) == original, "Gallery authoring changed its shared source");
    const std::array assets{common, wide, bright, combined};
    const std::array names{"Common source", "Wide contribution", "Bright contribution",
                           "Wide + bright contributions"};
    for (unsigned i = 0; i < assets.size(); ++i) {
        const auto instance = instantiate_prefab(scene, assets[i]);
        authoring_command(scene, "entity.rename", {{"entity", instance}, {"name", names[i]}});
        authoring_command(
            scene, "transform.position",
            {{"entity", instance}, {"value", {{"x", -3.9 + 2.6 * i}, {"y", 0}, {"z", 0}}}});
    }
    authoring_command(scene, "entity.create",
                      {{"recipe", "render.camera"},
                       {"name", "Source contribution overview"},
                       {"position", {{"x", 0}, {"y", 0}, {"z", -8}}},
                       {"component_values", {{"projection", 1}, {"orthographic_height", 6.6}}}});
    scene.save(project / scene_path);
    for (const auto& [asset, record] : library.records())
        if (!catalog.records().contains(asset))
            catalog.add(record);
    (void)catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(project));
}
inline void verify(const std::filesystem::path& root) {
    const auto snapshot = load_game_scene(root, {AssetId::parse(scene_uuid)});
    PrefabSources sources;
    AssetId combined;
    for (const auto& raw : snapshot.at("_prefab_sources"))
        if (const auto asset = raw.at("asset_id").get<AssetId>();
            sources.emplace(asset, raw).second && raw.at("version") == 8) {
            check(!combined, "Gallery requires one combined source");
            combined = asset;
        }
    check(bool(combined) && sources.size() == 4, "Gallery lost its four source definitions");
    // Export rewrites source paths. The admitted raw graph retains the same
    // AssetIds; use those addresses for both authored and relocated consumers.
    const PrefabDocument composite(sources.at(combined));
    const auto inputs = composite.base_assets();
    check(inputs.size() == 2 && composite.source.at("overrides").empty(),
          "Gallery lost explicit ordered source intent");
    const auto& wide = sources.at(inputs[0]);
    const auto& bright = sources.at(inputs[1]);
    check(wide.at("base") == bright.at("base") && wide.at("root") == bright.at("root"),
          "Gallery contributions lost their common source identity");
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.restore_snapshot(snapshot);
    RuntimeSimulation simulation(engine.world(), scene);
    for (unsigned tick = 0; tick < 3; ++tick)
        simulation.tick(1.f / 60);
    check(extract_render_scene(simulation.presentation(1)).meshes.size() == 8,
          "Gallery lost the four composed marker render groups");
    const auto document = scene.document();
    unsigned groups = 0;
    std::set<std::string> members;
    for (const auto& row : document.at("entities")) {
        if (!row.contains("prefab_instance"))
            continue;
        ++groups;
        const auto& instance = row.at("prefab_instance");
        check(instance.at("members").size() == 3, "Gallery changed common member identity count");
        for (const auto& [source_member, target] : instance.at("members").items()) {
            (void)source_member;
            check(members.insert(target.get<std::string>()).second,
                  "Gallery reused a Scene member between distinct instances");
        }
        if (instance.at("asset") != Json(combined))
            continue;
        const auto controls =
            scene.entity(row.at("id").get<std::string>()).get<NavigationBehavior>();
        check(controls.target.scene == scene.asset_id() &&
                  controls.anchor.scene == scene.asset_id(),
              "Composed Gallery references lost their instance scope");
        const auto body = scene.entity(controls.target.entity.str());
        const auto crown = scene.entity(controls.anchor.entity.str());
        check(body && crown, "Gallery references lost their native member targets");
        const auto parameters = body.get<MeshRenderer>().material_parameters;
        check(std::abs(body.get<LocalScale>().x - 2.2) < 1e-6 &&
                  std::abs(crown.get<LocalScale>().x - 2.8) < 1e-6 && parameters.size() == 1 &&
                  parameters[0].slot == "surface" && parameters[0].key == "baseColorFactor" &&
                  parameters[0].parameter.type == MaterialParameterType::LinearColor4 &&
                  parameters[0].parameter.value[0] == 0 && parameters[0].parameter.value[1] == 1 &&
                  parameters[0].parameter.value[2] == 1,
              "Gallery composed sources lost shape, color or portable native references");
    }
    check(groups == 4 && members.size() == 12, "Gallery lost a complete example group");
}
} // namespace forge::test_prefab_source_layers_gallery
