#pragma once
#include <array>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <set>

namespace forge::test_prefab_nested_gallery {
inline constexpr auto scene_path = "Scenes/207-nested-prefab-assemblies.scene.json";
inline constexpr auto scene_uuid = "f48fc291-7f91-488a-b99c-f332916b1d18";
inline constexpr auto child_path = "Assets/Variants/CappedMarker.prefab.json";
inline constexpr auto alternate_path = "Assets/Variants/ForkedMarker.prefab.json";
inline constexpr std::array<const char*, 3> prefab_paths{
    "Assets/Assemblies/PairedMarkers.prefab.json",
    "Assets/Assemblies/SingleLinkedMarker.prefab.json",
    "Assets/Assemblies/MixedLinkedMarkers.prefab.json"};
inline void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
inline Json read(const std::filesystem::path& path) {
    std::ifstream input(path);
    check(bool(input), "Nested assembly Gallery source missing");
    return Json::parse(input);
}
inline Json transform(double x = 0, double scale = 1) {
    return {{"forge.primitive", {{"kind", no_primitive}}},
            {"forge.local_translation", {{"x", x}, {"y", 0}, {"z", 0}}},
            {"forge.local_rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}},
            {"forge.local_scale", {{"x", scale}, {"y", scale}, {"z", scale}}}};
}
inline void author(const std::filesystem::path& project) {
    check(!std::filesystem::exists(project / scene_path), "Gallery207 already exists");
    for (const auto* path : prefab_paths)
        check(!std::filesystem::exists(project / path), "Gallery207 requires new source paths");
    auto lease = std::make_shared<ProjectLease>(project);
    auto catalog = AssetCatalog::open_project(project);
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(
        duplicate_scene_asset(scene.document(), AssetId::parse(scene_uuid), scene.schema()));
    PrefabLibrary library(project);
    library.refresh(scene);
    const auto child = read(project / child_path);
    const auto child_id = child.at("asset_id").get<AssetId>();
    const auto alternate = read(project / alternate_path).at("asset_id").get<AssetId>();
    PrefabMemberId body, crown;
    for (const auto& row : child.at("members")) {
        if (row.at("name") == "Body")
            body = row.at("id").get<PrefabMemberId>();
        if (row.at("name") == "Crown")
            crown = row.at("id").get<PrefabMemberId>();
    }
    check(bool(body) && bool(crown), "Linked marker lacks its named demonstration members");
    const auto asset = AssetId::generate();
    const auto root = PrefabMemberId::generate(), left = PrefabMemberId::generate(),
               right = PrefabMemberId::generate();
    const auto reference_values = [&](AssetId right_asset, PrefabMemberId target) {
        return Json{{"enabled", false},
                    {"graph", nullptr},
                    {"target", PrefabMemberPath{asset, {{right, right_asset}}, target}},
                    {"anchor", PrefabMemberPath{asset, {{left, child_id}}, body}}};
    };
    auto right_values = transform(.8, .65);
    right_values["forge.navigation_behavior"] = reference_values(child_id, crown);
    library.create(
        scene,
        PrefabDocument(
            {{"format", "forge.prefab"},
             {"version", 7},
             {"asset_id", asset},
             {"revision", 1},
             {"root", root},
             {"members",
              Json::array({{{"id", root}, {"name", "Paired markers"}, {"components", transform()}},
                           {{"id", left},
                            {"parent", root},
                            {"name", "Left linked marker"},
                            {"components", transform(-.8, .65)},
                            {"prefab_instance", {{"asset", child_id}}}},
                           {{"id", right},
                            {"parent", root},
                            {"name", "Right linked marker"},
                            {"components", right_values},
                            {"prefab_instance", {{"asset", child_id}}}}})}}),
        prefab_paths[0]);
    const auto single = library.create_variant(scene, asset, prefab_paths[1]);
    auto expected = library.source(single);
    auto candidate = PrefabDocument(expected).with_nested_prefab(right, {}).source;
    library.publish(scene, expected, candidate, library.base_sources(scene, expected, &candidate));
    const auto mixed = library.create_variant(scene, asset, prefab_paths[2]);
    expected = library.source(mixed);
    candidate = PrefabDocument(expected).with_nested_prefab(right, alternate).source;
    auto values = reference_values(alternate, body);
    values["target"]["prefab"] = mixed;
    values["anchor"]["prefab"] = mixed;
    candidate["overrides"][0]["components"]["forge.navigation_behavior"] = values;
    library.publish(scene, expected, candidate, library.base_sources(scene, expected, &candidate));
    const std::array assets{asset, single, mixed};
    const std::array names{"Assembly - repeated child asset", "Variant - cleared right link",
                           "Variant - replaced right child"};
    for (unsigned i = 0; i < assets.size(); ++i) {
        const auto instance = instantiate_prefab(scene, assets[i]);
        authoring_command(scene, "entity.rename", {{"entity", instance}, {"name", names[i]}});
        authoring_command(
            scene, "transform.position",
            {{"entity", instance}, {"value", {{"x", -3.3 + 3.3 * i}, {"y", 0}, {"z", 0}}}});
    }
    authoring_command(scene, "entity.create",
                      {{"recipe", "render.camera"},
                       {"name", "Nested assembly overview"},
                       {"position", {{"x", 0}, {"y", 0}, {"z", -8}}},
                       {"component_values", {{"projection", 1}, {"orthographic_height", 5.7}}}});
    scene.save(project / scene_path);
    for (const auto& [id, record] : library.records())
        if (!catalog.records().contains(id))
            catalog.add(record);
    (void)catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(project));
}
inline void verify(const std::filesystem::path& root) {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {AssetId::parse(scene_uuid)}));
    RuntimeSimulation simulation(engine.world(), scene);
    for (unsigned tick = 0; tick < 3; ++tick)
        simulation.tick(1.f / 60);
    const auto frame = simulation.presentation(1);
    const auto render = extract_render_scene(frame);
    check(render.meshes.size() == 12, "Nested Gallery lost its linked renderers");
    const auto document = scene.document();
    std::set<std::string> roots;
    unsigned linked = 0;
    for (const auto& row : document.at("entities"))
        if (row.contains("prefab_nested") && !row.value("missing_member", false)) {
            check(roots.insert(row.at("id").get<std::string>()).second,
                  "Gallery reused a nested occurrence identity");
            ++linked;
        }
    check(linked == 5 && document.at("version") == 6,
          "Gallery did not retain repeated, cleared and replaced composition scopes");
}
} // namespace forge::test_prefab_nested_gallery
