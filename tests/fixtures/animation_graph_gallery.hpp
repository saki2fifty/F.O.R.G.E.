#pragma once
#include "animation_graph_authoring.hpp"
#include "animation_graph_layout.hpp"
#include "animation_mask_authoring.hpp"
#include "model_placement.hpp"
#include "model_skeleton_binding.hpp"
#include "render_texture_fixture.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>

namespace forge::test {
inline constexpr auto animation_graph_scene_path = "Scenes/180-animation-graphs.scene.json";
inline constexpr auto animation_graph_source_path = "Assets/AnimationGraphs/Layered.animgraph.json";
inline AssetId author_animation_graph_gallery(const std::filesystem::path& root) {
    if (std::filesystem::exists(root / animation_graph_scene_path) ||
        std::filesystem::exists(root / animation_graph_source_path))
        throw std::runtime_error("Animation Graph Gallery sources already exist");
    auto catalog = AssetCatalog::open_project(root);
    const auto before = catalog.document().at("assets");
    AssetId target_id, clip;
    for (const auto& [id, record] : catalog.records()) {
        if (record.type == "model" && record.source == "Assets/AnimationRetarget/TargetBeams.gltf")
            target_id = id;
        if (record.type == AnimationClipAsset::type &&
            record.source == "Assets/AnimationRetarget/Swing.retarget.json")
            clip = id;
    }
    if (!target_id || !clip)
        throw std::runtime_error(
            "Animation Graph Gallery needs the existing published target rig and motion");
    const auto target = asset_detail::load_model_selection(root, catalog, target_id);
    const auto skeleton = target.bindings.at("/rig/skeleton");
    const auto lease = std::make_shared<ProjectLease>(root);
    std::filesystem::create_directories(root / "Assets/AnimationGraphs");
    auto mask = AnimationMaskDocument::create(
        lease, "Assets/AnimationGraphs/Upper.mask.json", [&](AssetId id) {
            auto source = AnimationMaskSource::create(id);
            source.document["skeleton"] = skeleton;
            source.document["default_weight"] = 0;
            source.document["entries"] = {
                {{"joint", target.bindings.at("/nodes/1")}, {"weight", 1}, {"descendants", true}}};
            return source;
        });
    test_render_texture::publish(lease, *mask, animation_mask_import_registry(),
                                 prepare_animation_mask_publication);
    const auto mask_id = mask->source().asset();
    auto document =
        AnimationGraphDocument::create(lease, animation_graph_source_path, [&](AssetId id) {
            auto source = AnimationGraphSource::create(id);
            const auto rest = AssetId::generate(), motion = AssetId::generate(),
                       blend = AssetId::generate(), layer = AssetId::generate();
            source.document["skeleton"] = skeleton;
            source.document["cycle_seconds"] = 2;
            source.document["inputs"] = {
                {{"slot", 1}, {"name", "Motion amount"}, {"initial", .8}},
                {{"slot", 2}, {"name", "Layer strength"}, {"initial", .5}}};
            source.document["nodes"] = {
                {{"id", rest},
                 {"kind", "clip"},
                 {"name", "Imported rest"},
                 {"clip", target.bindings.at("/animations/0")}},
                {{"id", motion}, {"kind", "clip"}, {"name", "Mapped Swing"}, {"clip", clip}},
                {{"id", blend},
                 {"kind", "blend"},
                 {"name", "Motion blend"},
                 {"first", rest},
                 {"second", motion},
                 {"mask", mask_id},
                 {"weight", {{"input", 1}, {"scale", 1}, {"bias", 0}}}},
                {{"id", layer},
                 {"kind", "additive"},
                 {"name", "Upper layer"},
                 {"first", rest},
                 {"second", blend},
                 {"mask", mask_id},
                 {"weight", {{"input", 2}, {"scale", 1}, {"bias", 0}}}}};
            source.document["output"] = layer;
            asset_detail::set_animation_graph_position(source.document, rest, {0, 0});
            asset_detail::set_animation_graph_position(source.document, motion, {0, 180});
            asset_detail::set_animation_graph_position(source.document, blend, {340, 0});
            asset_detail::set_animation_graph_position(source.document, layer, {680, 80});
            return source;
        });
    test_render_texture::publish(lease, *document, animation_graph_import_registry(),
                                 prepare_animation_graph_publication);
    const auto graph = document->source().asset();
    catalog = AssetCatalog::open_project(root);
    EngineContext engine;
    Scene scene(engine.world());
    const auto place = [&](const char* name, double x, bool override, float strength) {
        asset_detail::ModelPlacementOptions options;
        options.name = name;
        options.transform.translation = {x, -2.2, 0};
        options.clip = {target.bindings.at("/animations/0")};
        const auto actor = asset_detail::instantiate_model(
            scene, catalog,
            asset_detail::prepare_model_placement(target, scene.asset_id(), scene.revision(),
                                                  options));
        authoring_command(scene, "property.set_many",
                          {{"entity", actor.str()},
                           {"component", "forge.animator"},
                           {"values",
                            {{"graph", graph},
                             {"clip", nullptr},
                             {"graph_override_1", override},
                             {"graph_input_1", 1},
                             {"graph_override_2", override},
                             {"graph_input_2", strength}}}});
    };
    place("Left - source defaults, gentle upper motion", -2.5, false, 0);
    place("Middle - overrides, full upper motion", 0, true, 1);
    place("Right - override layer zero, imported rest", 2.5, true, 0);
    const auto camera =
        authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", "Game camera"}})
            .at("selected")
            .get<std::string>();
    Camera lens;
    lens.far_plane = 100;
    scene.entity(camera).set<Camera>(lens).set<LocalTranslation>({0, .1, -11.5});
    scene.save(root / animation_graph_scene_path);
    catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(animation_graph_scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    for (const auto& row : before)
        if (std::find(after.begin(), after.end(), row) == after.end())
            throw std::runtime_error("Animation Graph Gallery changed an existing asset record");
    return record.id;
}
} // namespace forge::test
