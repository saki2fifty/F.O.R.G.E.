#pragma once
#include "asset_reimport.hpp"
#include "editor/document.hpp"
#include "material_authoring.hpp"
#include "runtime_package.hpp"
#include "texture_authoring.hpp"
#include "tile_asset_reimport_routes.hpp"
#include "transform_animation_authoring.hpp"
#include <algorithm>
#include <chrono>
#include <forge/authoring.hpp>
#include <fstream>
#include <thread>
namespace forge::test_moving_one_way_scene {
inline constexpr const char* source = "Scenes/119-moving-one-way-platforms.scene.json";
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Prepared {
    std::filesystem::path root;
    AssetId scene;
    std::size_t prior_records{};
    bool created{};
};
inline Prepared prepare(const std::filesystem::path& gallery,
                        const std::filesystem::path& scratch) {
    Prepared result;
    result.root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(result.root.parent_path());
    std::filesystem::copy(gallery, result.root, std::filesystem::copy_options::recursive);
    const auto prior = AssetCatalog::open_project(result.root).document().at("assets");
    result.prior_records = prior.size();
    result.created = !std::filesystem::exists(result.root / source);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(result.root, true);
    const auto settings = project.settings().document();
    if (result.created) {
        project.open_scene(result.root / "Scenes/86-one-way-tile-maps.scene.json");
        project.save_as(result.root / source);
        const auto find = [&](const char* name) {
            const auto document = scene.document();
            for (const auto& row : document.at("entities"))
                if (row.at("name") == name)
                    return row.at("id").get<std::string>();
            throw std::runtime_error(std::string("Missing Gallery source entity: ") + name);
        };
        const auto set = [&](const std::string& id, const char* type, const Json& values) {
            authoring_command(scene, "property.set_many",
                              {{"entity", id}, {"component", type}, {"values", values}});
        };
        const auto animate = [&](const std::string& id) {
            authoring_command(scene, "component.add",
                              {{"entity", id}, {"component", "forge.transform_animator"}});
            set(id, "forge.transform_animator",
                {{"clip", "da50e938-f21b-4a87-b116-860bbbb7fdf3"},
                 {"enabled", true},
                 {"loop", true},
                 {"play_on_start", true}});
        };
        const auto map = find("One-way painted platforms - select to see solid-face arrows");
        scene.rename_entity(map, "Moving one-way painted course - edit its translation clip");
        set(map, "forge.physics_body_2d", {{"motion", 1}, {"one_way", true}});
        set(map, "forge.local_translation", {{"x", -3}, {"y", -2}, {"z", 0}});
        animate(map);
        const auto amber = find("Amber box - left painted platform");
        scene.rename_entity(amber, "Amber box - falling onto the inline one-way ledge");
        set(amber, "forge.local_translation", {{"x", -1.5}, {"y", 4}, {"z", 0}});
        const auto cyan = find("Cyan circle - rising through the painted upper platform");
        scene.rename_entity(cyan, "Cyan circle - rising through both moving one-way surfaces");
        set(cyan, "forge.local_translation", {{"x", 1.5}, {"y", -4}, {"z", 0}});
        scene.delete_subtree(find("Rose box - decoration has no collision"));
        const auto ledge = scene.duplicate_subtree(find("Ordinary catch ceiling"));
        scene.rename_entity(ledge, "Moving one-way inline ledge - same translation clip");
        const auto parent = authoring_command(scene, "entity.create",
                                              {{"recipe", "empty"},
                                               {"name", "Inline ledge - fixed animation origin"},
                                               {"position", {{"x", 0}, {"y", 4}, {"z", 0}}}})
                                .at("selected")
                                .get<std::string>();
        scene.reparent_entity(ledge, parent, ReparentMode::KeepLocal);
        set(ledge, "forge.local_translation", {{"x", -3}, {"y", -2}, {"z", 0}});
        set(ledge, "forge.physics_body_2d", {{"motion", 1}, {"one_way", true}, {"restitution", 0}});
        set(ledge, "forge.box_collider_2d", {{"x", 4}, {"y", .2}});
        set(ledge, "forge.sprite_renderer",
            {{"width", 4}, {"height", .2}, {"tint_r", .2}, {"tint_g", .7}, {"tint_b", .8}});
        animate(ledge);
        project.save();
    } else {
        project.open_scene(result.root / source);
    }
    result.scene = scene.asset_id();
    const std::array roots{result.scene};
    prepare_runtime_content_catalog(*project.writer_guard(), roots, {});
    check(project.settings().document() == settings, "Gallery generation changed project settings");
    const auto after = AssetCatalog::open_project(result.root).document().at("assets");
    check(after.size() == prior.size() + std::size_t(result.created),
          "Moving one-way Gallery changed unrelated asset counts");
    for (const auto& row : prior)
        check(std::find(after.begin(), after.end(), row) != after.end(),
              "Moving one-way Gallery changed a prior catalog record");
    std::ofstream receipt(result.root.parent_path() / (result.scene.str() + "-source.json"));
    receipt << Json{{"project", result.root.string()},
                    {"scene", result.scene},
                    {"source", source},
                    {"prior_records", prior.size()},
                    {"current_records", after.size()},
                    {"all_prior_records_exact", true}}
                   .dump(2)
            << '\n';
    check(bool(receipt), "Moving one-way Gallery source receipt write failed");
    return result;
}
inline void settle(const Prepared& prepared, const std::filesystem::path& worker) {
    auto lease = std::make_shared<ProjectLease>(prepared.root);
    SourceScanOptions options;
    options.include_project_root = true;
    const auto target = desktop_texture_target();
    auto routes = tile_asset_reimport_routes(worker, target);
    routes.push_back({"forge.texture.image", target, texture_import_registry(worker),
                      [](auto& candidate, const auto& plan, const auto&) {
                          prepare_texture_publication(candidate, plan);
                      }});
    routes.push_back({"forge.material.builtin", target, material_import_registry(),
                      [](auto& candidate, const auto& plan, const auto&) {
                          prepare_material_publication(candidate, plan);
                      }});
    routes.push_back({"forge.animation.transform", target, transform_animation_import_registry(),
                      [](auto& candidate, const auto& plan, const auto&) {
                          prepare_transform_animation_publication(candidate, plan);
                      }});
    AssetReimportService imports(lease, std::move(routes), options);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(150);
    do {
        (void)imports.poll();
        check(std::chrono::steady_clock::now() < deadline, "Gallery source imports did not settle");
        if (!imports.settled())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (!imports.settled());
}
} // namespace forge::test_moving_one_way_scene
