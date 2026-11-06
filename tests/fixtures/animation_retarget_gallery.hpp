#pragma once
#include "animation_retarget_authoring.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include "model_placement.hpp"
#include "model_skeleton_binding.hpp"
#include "render_texture_fixture.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>

namespace forge::test {
inline constexpr auto retarget_scene_path = "Scenes/179-animation-retargeting.scene.json";
inline constexpr auto retarget_recipe_path = "Assets/AnimationRetarget/Swing.retarget.json";
inline AssetId author_animation_retarget_gallery(const std::filesystem::path& root,
                                                 const std::filesystem::path& worker,
                                                 const std::filesystem::path& converter) {
    if (std::filesystem::exists(root / retarget_scene_path) ||
        std::filesystem::exists(root / retarget_recipe_path))
        throw std::runtime_error("Retarget Gallery sources already exist");
    const auto before = AssetCatalog::open_project(root).document().at("assets");
    const auto lease = std::make_shared<ProjectLease>(root);
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(asset_detail::model_importer(worker, converter));
    registry->seal();
    AssetImportService imports(lease, registry, {"windows", "d3d12", "desktop"}, 1);
    const auto publish_model = [&](const char* path) {
        const auto request = imports.prepare(path);
        const auto id = request.request.asset;
        imports.submit(
            request,
            [](auto& candidate, const auto& plan, const auto& catalog) {
                prepare_model_publication(candidate, plan, catalog);
            },
            [](const auto&, const auto&) {});
        if (!imports.wait_idle(std::chrono::seconds(30)))
            throw std::runtime_error("Gallery model import timed out");
        const auto outcomes = imports.poll();
        if (outcomes.size() != 1 || !outcomes.front().published)
            throw std::runtime_error(outcomes.empty() ? "Missing Gallery import receipt"
                                                      : outcomes.front().diagnostic);
        return id;
    };
    const auto source_id = publish_model("Assets/AnimationRetarget/SourceBeams.gltf");
    const auto target_id = publish_model("Assets/AnimationRetarget/TargetBeams.gltf");
    auto catalog = AssetCatalog::open_project(root);
    const auto source = asset_detail::load_model_selection(root, catalog, source_id);
    const auto target = asset_detail::load_model_selection(root, catalog, target_id);
    const auto from = source.bindings.at("/rig/skeleton");
    const auto to = target.bindings.at("/rig/skeleton");
    auto recipe = AnimationRetargetDocument::create(lease, retarget_recipe_path, [&](AssetId id) {
        auto value = AnimationRetargetSource::create(id);
        value.document["source_skeleton"] = from;
        value.document["source_clip"] = source.bindings.at("/animations/0");
        value.document["target_skeleton"] = to;
        for (const auto joint : asset_detail::model_skeleton_joints(source, from).assets)
            value.document["entries"].push_back(
                {{"source", joint},
                 {"target", target.bindings.at(source.member(joint).identity.address)}});
        return value;
    });
    test_render_texture::publish(lease, *recipe, animation_retarget_import_registry(),
                                 prepare_animation_retarget_publication);
    const auto clip = recipe->source().asset();
    recipe.reset();
    catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    const auto place = [&](const auto& model, const char* name, double x, AssetId animation) {
        asset_detail::ModelPlacementOptions options;
        options.name = name;
        options.transform.translation = {x, -1.8, 0};
        options.clip = {model.bindings.at("/animations/0")};
        const auto actor = asset_detail::instantiate_model(
            scene, catalog,
            asset_detail::prepare_model_placement(model, scene.asset_id(), scene.revision(),
                                                  options));
        if (animation)
            authoring_command(scene, "property.set",
                              {{"entity", actor.str()},
                               {"component", "forge.animator"},
                               {"field", "clip"},
                               {"value", animation}});
    };
    place(source, "Blue source - original Swing clip", -2, {});
    place(target, "Gold target - mapped Swing, different rest and proportions", 2, clip);
    const auto camera =
        authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", "Game camera"}})
            .at("selected")
            .get<std::string>();
    Camera lens;
    lens.far_plane = 100;
    scene.entity(camera).set<Camera>(lens).set<LocalTranslation>({.7, .4, -12});
    scene.save(root / retarget_scene_path);
    catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(retarget_scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    for (const auto& row : before)
        if (std::find(after.begin(), after.end(), row) == after.end())
            throw std::runtime_error("Retarget Gallery changed an existing asset record");
    return record.id;
}
} // namespace forge::test
