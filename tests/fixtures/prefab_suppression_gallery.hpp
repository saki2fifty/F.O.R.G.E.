#pragma once
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/project_lease.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>

namespace forge::test_prefab_suppression_gallery {
inline constexpr auto scene_path = "Scenes/205-prefab-component-suppression.scene.json";
inline constexpr auto scene_uuid = "05bf985f-488d-4a4f-9473-105124cfd09a";
inline constexpr std::array<const char*, 3> prefab_paths{
    "Assets/Variants/CappedMarker.prefab.json", "Assets/Variants/UncappedMarker.prefab.json",
    "Assets/Variants/RecappedMarker.prefab.json"};
inline void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
inline Json read(const std::filesystem::path& path) {
    std::ifstream input(path);
    check(bool(input), "Prefab suppression Gallery source missing");
    return Json::parse(input);
}
inline void verify_sources(const std::filesystem::path& root) {
    PrefabSources sources;
    std::array<AssetId, 3> ids;
    for (unsigned i = 0; i < ids.size(); ++i) {
        auto source = read(root / prefab_paths[i]);
        ids[i] = source.at("asset_id").get<AssetId>();
        sources[ids[i]] = std::move(source);
    }
    check(sources.at(ids[1]).at("version") == 5 && sources.at(ids[1]).at("base") == Json(ids[0]) &&
              sources.at(ids[2]).at("base") == Json(ids[1]),
          "Gallery lost its explicit source5 or chained base relationship");
    const auto resolved = resolve_prefab_sources(sources);
    for (unsigned i = 0; i < ids.size(); ++i) {
        unsigned renderers = 0;
        for (const auto& member : resolved.at(ids[i]).at("members"))
            renderers += member.at("components").contains("forge.mesh_renderer");
        check(renderers == (i == 1 ? 1 : 2),
              "Gallery did not compose component absence/restoration");
    }
}
inline void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Gallery205 already exists");
    for (const auto* path : prefab_paths)
        check(!std::filesystem::exists(root / path),
              "Gallery205 authoring requires new source paths");
    auto lease = std::make_shared<ProjectLease>(root);
    auto catalog = AssetCatalog::open_project(root);
    AssetId material;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/Planar/UnlitChecker.material.json")
            material = id;
    check(bool(material), "Gallery requires the existing unlit checker material");
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(
        duplicate_scene_asset(scene.document(), AssetId::parse(scene_uuid), scene.schema()));
    const auto anchor =
        authoring_command(
            scene, "entity.create",
            {{"kind", 4}, {"name", "Capped marker"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}}})
            .at("selected")
            .get<std::string>();
    const auto mesh = [&](const char* name, float y, LocalScale scale) {
        const auto id =
            authoring_command(
                scene, "entity.create",
                {{"recipe", "render.mesh"},
                 {"name", name},
                 {"position", {{"x", 0}, {"y", y}, {"z", 0}}},
                 {"component_values",
                  {{"mesh", engine_primitive(0)},
                   {"cast_shadows", false},
                   {"materials", Json::array({{{"slot", "surface"}, {"material", material}}})}}}})
                .at("selected")
                .get<std::string>();
        scene.entity(id).set<LocalScale>(scale);
        scene.reparent_entity(id, anchor, ReparentMode::KeepLocal);
    };
    mesh("Body", -.7f, {1.4f, 1.4f, .4f});
    mesh("Crown", .6f, {2, .6f, .4f});
    PrefabLibrary library(root);
    library.refresh(scene);
    const auto source = create_prefab_source(scene, anchor);
    PrefabMemberId crown;
    Json crown_renderer;
    for (const auto& member : source.source.at("members"))
        if (member.at("name") == "Crown") {
            crown = member.at("id").get<PrefabMemberId>();
            crown_renderer = member.at("components").at("forge.mesh_renderer");
        }
    check(bool(crown), "Gallery capture lost the stable crown member");
    const auto base = library.create(scene, source, prefab_paths[0]);
    const auto suppressed = library.create_variant(scene, base, prefab_paths[1]);
    auto expected = library.source(suppressed);
    const auto absent = PrefabDocument(expected)
                            .with_component_suppression(crown, "forge.mesh_renderer", true)
                            .source;
    library.publish(scene, expected, absent, library.base_sources(scene, expected));
    const auto restored = library.create_variant(scene, suppressed, prefab_paths[2]);
    expected = library.source(restored);
    auto added = expected;
    added["overrides"] =
        Json::array({{{"id", crown}, {"components", {{"forge.mesh_renderer", crown_renderer}}}}});
    library.publish(scene, expected, added, library.base_sources(scene, expected));
    authoring_command(scene, "entity.delete", {{"entity", anchor}});
    const std::array assets{base, suppressed, restored};
    const std::array names{"Base - inherited crown", "Variant - crown suppressed",
                           "Derived variant - crown added"};
    for (unsigned i = 0; i < assets.size(); ++i) {
        const auto id = instantiate_prefab(scene, assets[i]);
        authoring_command(scene, "entity.rename", {{"entity", id}, {"name", names[i]}});
        authoring_command(scene, "transform.position",
                          {{"entity", id}, {"value", {{"x", -3. + i * 3}, {"y", 0}, {"z", 0}}}});
    }
    authoring_command(scene, "entity.create",
                      {{"recipe", "render.camera"},
                       {"name", "Composition overview"},
                       {"position", {{"x", 0}, {"y", 0}, {"z", -8}}},
                       {"component_values", {{"projection", 1}, {"orthographic_height", 5}}}});
    scene.save(root / scene_path);
    for (const auto& [id, record] : library.records())
        if (!catalog.records().contains(id))
            catalog.add(record);
    (void)catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    verify_sources(root);
}
} // namespace forge::test_prefab_suppression_gallery
