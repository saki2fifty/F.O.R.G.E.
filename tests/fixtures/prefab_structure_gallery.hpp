#pragma once
#include <array>
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/project_lease.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>

namespace forge::test_prefab_structure_gallery {
inline constexpr auto scene_path = "Scenes/206-prefab-member-structure.scene.json";
inline constexpr auto scene_uuid = "23c4d7a7-6e76-4ab5-a3f4-9b0649136e19";
inline constexpr auto base_path = "Assets/Variants/CappedMarker.prefab.json";
inline constexpr std::array<const char*, 2> prefab_paths{
    "Assets/Variants/ForkedMarker.prefab.json", "Assets/Variants/SingleBeaconMarker.prefab.json"};
inline void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
inline Json read(const std::filesystem::path& path) {
    std::ifstream input(path);
    check(bool(input), "Structural Prefab Gallery source missing");
    return Json::parse(input);
}
inline PrefabSources sources(const std::filesystem::path& root) {
    PrefabSources result;
    for (const char* path : {base_path, prefab_paths[0], prefab_paths[1]}) {
        auto source = read(root / path);
        const auto asset = source.at("asset_id").get<AssetId>();
        result[asset] = std::move(source);
    }
    return result;
}
inline void verify_sources(const std::filesystem::path& root) {
    const auto raw = sources(root), resolved = resolve_prefab_sources(raw);
    const auto base = read(root / base_path).at("asset_id").get<AssetId>();
    const auto forked = read(root / prefab_paths[0]).at("asset_id").get<AssetId>();
    const auto single = read(root / prefab_paths[1]).at("asset_id").get<AssetId>();
    check(raw.at(forked).at("version") == 6 && raw.at(forked).at("base") == Json(base) &&
              raw.at(forked).at("added_members").size() == 3 && raw.at(single).at("version") == 6 &&
              raw.at(single).at("base") == Json(forked),
          "Gallery lost its structural source profile or chained ownership");
    const std::array ids{base, forked, single};
    const std::array counts{3u, 5u, 4u};
    for (unsigned i = 0; i < ids.size(); ++i) {
        const auto& members = resolved.at(ids[i]).at("members");
        unsigned renderers = 0;
        for (const auto& member : members)
            renderers += member.at("components").contains("forge.mesh_renderer");
        check(members.size() == counts[i] && renderers == counts[i] - 1,
              "Gallery lost an effective structural member or renderer");
    }
}
inline void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Gallery206 already exists");
    for (const auto* path : prefab_paths)
        check(!std::filesystem::exists(root / path), "Gallery206 requires new source paths");
    auto lease = std::make_shared<ProjectLease>(root);
    auto catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(
        duplicate_scene_asset(scene.document(), AssetId::parse(scene_uuid), scene.schema()));
    PrefabLibrary library(root);
    library.refresh(scene);
    const auto base = read(root / base_path);
    const auto base_id = base.at("asset_id").get<AssetId>();
    PrefabMemberId body, crown;
    Json components;
    for (const auto& row : base.at("members")) {
        if (row.at("name") == "Body") {
            body = row.at("id").get<PrefabMemberId>();
            components = row.at("components");
        }
        if (row.at("name") == "Crown")
            crown = row.at("id").get<PrefabMemberId>();
    }
    check(bool(body) && bool(crown), "Gallery base marker lacks its body/crown");
    const auto forked = library.create_variant(scene, base_id, prefab_paths[0]);
    auto expected = library.source(forked);
    auto candidate = PrefabDocument(expected).with_member_removed(crown, true);
    const auto bar = PrefabMemberId::generate(), left = PrefabMemberId::generate(),
               right = PrefabMemberId::generate();
    const auto local = [&](PrefabMemberId id, const char* name, PrefabMemberId parent, double x,
                           double y, double width, double height) {
        auto values = components;
        values["forge.local_translation"] = {{"x", x}, {"y", y}, {"z", 0}};
        values["forge.local_scale"] = {{"x", width}, {"y", height}, {"z", .4}};
        return Json{{"id", id},
                    {"name", name},
                    {"parent", parent},
                    {"components", values},
                    {"spatial", {{"mode", "explicit"}, {"member", base.at("root")}}}};
    };
    candidate = candidate.with_added_member(local(bar, "Crossbar", body, 0, .6, 2.4, .2))
                    .with_added_member(local(left, "Left beacon", bar, -.9, 1.1, .6, .8))
                    .with_added_member(local(right, "Right beacon", bar, .9, 1.1, .6, .8))
                    .with_member_order(bar, {left, right});
    library.publish(scene, expected, candidate.source, library.base_sources(scene, expected));
    const auto single = library.create_variant(scene, forked, prefab_paths[1]);
    expected = library.source(single);
    candidate = PrefabDocument(expected)
                    .with_member_removed(right, true)
                    .with_member_parent(left, body)
                    .with_member_order(body, {left, bar});
    for (auto& row : candidate.source["overrides"])
        if (row.at("id") == Json(left))
            row["components"]["forge.local_translation"] = {{"x", 0}, {"y", 1.1}, {"z", 0}};
    library.publish(scene, expected, candidate.source, library.base_sources(scene, expected));
    const std::array assets{base_id, forked, single};
    const std::array names{"Base - inherited crown", "Variant - owned fork",
                           "Derived - reparented single beacon"};
    for (unsigned i = 0; i < assets.size(); ++i) {
        const auto id = instantiate_prefab(scene, assets[i]);
        authoring_command(scene, "entity.rename", {{"entity", id}, {"name", names[i]}});
        authoring_command(scene, "transform.position",
                          {{"entity", id}, {"value", {{"x", -3. + i * 3}, {"y", 0}, {"z", 0}}}});
    }
    authoring_command(scene, "entity.create",
                      {{"recipe", "render.camera"},
                       {"name", "Member structure overview"},
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
} // namespace forge::test_prefab_structure_gallery
