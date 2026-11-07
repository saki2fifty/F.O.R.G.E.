#pragma once
#include "model_placement.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <array>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>

namespace forge::test {
inline constexpr auto skeletal_sequence_scene_path =
    "Scenes/185-skeletal-sequence-tracks.scene.json";
inline constexpr auto sequence_rate_scene_path = "Scenes/186-nested-sequence-rates.scene.json";
inline constexpr auto sequence_collection_scene_path =
    "Scenes/187-sequence-clip-collections.scene.json";
inline constexpr auto sequence_child_loop_scene_path = "Scenes/188-child-sequence-loops.scene.json";
inline constexpr auto sequence_placement_scene_path = "Scenes/189-controller-placements.scene.json";
inline constexpr auto sequence_multiway_scene_path =
    "Scenes/190-multiway-sequence-blends.scene.json";
enum class SequenceGalleryProfile {
    Direct,
    Rates,
    Collections,
    ChildLoops,
    ControllerPlacements,
    Multiway
};
inline const char* sequence_gallery_path(SequenceGalleryProfile profile) {
    switch (profile) {
    case SequenceGalleryProfile::Direct:
        return skeletal_sequence_scene_path;
    case SequenceGalleryProfile::Rates:
        return sequence_rate_scene_path;
    case SequenceGalleryProfile::Collections:
        return sequence_collection_scene_path;
    case SequenceGalleryProfile::ChildLoops:
        return sequence_child_loop_scene_path;
    case SequenceGalleryProfile::ControllerPlacements:
        return sequence_placement_scene_path;
    case SequenceGalleryProfile::Multiway:
        return sequence_multiway_scene_path;
    }
    throw std::runtime_error("Unknown skeletal sequence Gallery profile");
}
inline AssetId
author_skeletal_sequence_gallery(const std::filesystem::path& root,
                                 SequenceGalleryProfile profile = SequenceGalleryProfile::Direct) {
    const bool placement_rates = profile == SequenceGalleryProfile::Rates,
               collections = profile == SequenceGalleryProfile::Collections,
               child_loops = profile == SequenceGalleryProfile::ChildLoops,
               placements = profile == SequenceGalleryProfile::ControllerPlacements,
               multiway = profile == SequenceGalleryProfile::Multiway;
    const auto path = sequence_gallery_path(profile);
    if (std::filesystem::exists(root / path))
        throw std::runtime_error("Skeletal sequence Gallery already exists");
    auto catalog = AssetCatalog::open_project(root);
    const auto before = catalog.document().at("assets");
    const auto asset = [&](const char* type, const char* source) {
        for (const auto& [id, record] : catalog.records())
            if (record.type == type && record.source == source)
                return id;
        throw std::runtime_error(std::string("Missing published Gallery asset: ") + source);
    };
    const auto locomotion = asset_detail::load_model_selection(
        root, catalog, asset("model", "Assets/LocomotionColumn.gltf"));
    const auto morphs = asset_detail::load_model_selection(
        root, catalog, asset("model", "Assets/OverlayColumn.gltf"));
    const auto target = asset_detail::load_model_selection(
        root, catalog, asset("model", "Assets/AnimationRetarget/TargetBeams.gltf"));
    const auto mapped =
        asset(AnimationClipAsset::type, "Assets/AnimationRetarget/Swing.retarget.json");
    EngineContext engine;
    Scene scene(engine.world());
    const auto controller =
        authoring_command(scene, "entity.create",
                          {{"recipe", "animation.sequence"},
                           {"name", multiway      ? "Four-second layered clip overlaps"
                                    : placements  ? "Eight-second repeated placements"
                                    : child_loops ? "Eight-second repeating sections"
                                    : collections ? "Sixteen-second clip collection"
                                                  : "Four-second shared sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = child_loops || placements ? 8 : collections ? 16 : 4;
    sequence.loop = true;
    scene.entity(controller).set(sequence);
    const auto reference = scene.reference(controller);
    std::array<EntityRef, 3> bindings{reference, reference, reference};
    if (placement_rates) {
        const auto child = [&](const char* name, EntityRef parent, double rate, double start) {
            const auto id = authoring_command(scene, "entity.create",
                                              {{"recipe", "animation.sequence"},
                                               {"name", name},
                                               {"component_values",
                                                {{"duration", 4},
                                                 {"sequence", parent},
                                                 {"sequence_start_seconds", start},
                                                 {"sequence_rate", rate},
                                                 {"playback_speed", 0},
                                                 {"play_on_start", false}}}})
                                .at("selected")
                                .get<std::string>();
            return scene.reference(id);
        };
        bindings[0] = child("Fast child - 2x", reference, 2, .5);
        bindings[1] = child("Slow child - 0.5x", reference, .5, 0);
        bindings[2] = child("Nested child - 0.5x of fast parent", bindings[0], .5, .5);
    }
    if (child_loops || placements) {
        const auto child = [&](const char* name, EntityRef parent, double duration, double rate,
                               double start) {
            const auto id = authoring_command(scene, "entity.create",
                                              {{"recipe", "animation.sequence"},
                                               {"name", name},
                                               {"component_values",
                                                {{"duration", duration},
                                                 {"sequence", parent},
                                                 {"sequence_start_seconds", start},
                                                 {"sequence_rate", rate},
                                                 {"sequence_loop", true},
                                                 {"loop", false},
                                                 {"playback_speed", 0},
                                                 {"play_on_start", false}}}})
                                .at("selected")
                                .get<std::string>();
            return scene.reference(id);
        };
        bindings[0] = child(placements ? "Locomotion - finite, slow loop, normal loop"
                                       : "Fast repeating section - 2x",
                            reference, 2, 2, .5);
        bindings[1] = child(placements ? "Morph - finite and fast repeated placement"
                                       : "Repeating morph section",
                            reference, 4, 1, 1);
        bindings[2] = child(placements ? "Mapped - repeated inside locomotion"
                                       : "Repeating mapped section inside fast parent",
                            bindings[0], 1.5, 1, .25);
        if (placements) {
            const std::array<Json, 3> extras{
                Json::array({{{"start_seconds", 2.5}, {"rate", .5}, {"loop", true}},
                             {{"start_seconds", 5.5}, {"rate", 1.}, {"loop", true}}}),
                Json::array({{{"start_seconds", 5.5}, {"rate", 2.}, {"loop", false}}}),
                Json::array({{{"start_seconds", 1.75}, {"rate", 1.}, {"loop", true}}})};
            for (std::size_t i = 0; i < bindings.size(); ++i)
                authoring_command(
                    scene, "property.set_many",
                    {{"entity", bindings[i].entity.str()},
                     {"component", "forge.animation_sequence"},
                     {"values",
                      {{"sequence_loop", false}, {"sequence_extra_placements", extras[i]}}}});
        }
    }
    const auto place = [&](const auto& model, const char* name, double x, AssetId first,
                           AssetId next, double next_start, EntityRef binding) {
        asset_detail::ModelPlacementOptions options;
        options.name = name;
        options.transform.translation = {x, -1.5, 0};
        options.clip = {first};
        const auto id = asset_detail::instantiate_model(
            scene, catalog,
            asset_detail::prepare_model_placement(model, scene.asset_id(), scene.revision(),
                                                  options));
        authoring_command(scene, "property.set_many",
                          {{"entity", id.str()},
                           {"component", "forge.animator"},
                           {"values",
                            {{"clip", first},
                             {"sequence", binding},
                             {"sequence_next_clip", next},
                             {"sequence_next_start_seconds", next_start},
                             {"playback_speed", 0},
                             {"play_on_start", false},
                             {"root_motion", 0}}}});
        if (collections || multiway) {
            auto extras = Json::array();
            for (std::size_t i = 2; i < sequence_clip_limit; ++i)
                extras.push_back(
                    {{"clip", i % 2 ? next : first}, {"start_seconds", i * next_start}});
            authoring_command(
                scene, "property.set_many",
                {{"entity", id.str()},
                 {"component", "forge.animator"},
                 {"values", {{"sequence_extra_clips", extras}, {"sequence_multiway", multiway}}}});
        }
    };
    place(locomotion,
          multiway      ? "Left - Eight layered locomotion phases"
          : collections ? "Left - Eight Forward and Strafe clips"
                        : "Left - Forward to Strafe",
          -4, locomotion.bindings.at("/animations/0"), locomotion.bindings.at("/animations/1"),
          multiway ? .125 : .5, bindings[0]);
    place(morphs,
          multiway      ? "Center - Eight layered morph phases"
          : collections ? "Center - Eight Base and Overlay morph clips"
                        : "Center - Base to Overlay morphs",
          0, morphs.bindings.at(multiway ? "/animations/1" : "/animations/0"),
          morphs.bindings.at("/animations/1"), multiway ? .125 : 1.5, bindings[1]);
    place(target,
          multiway      ? "Right - Eight layered mapped poses"
          : collections ? "Right - Eight Rest and mapped Swing clips"
                        : "Right - Rest to mapped Swing",
          4, target.bindings.at("/animations/0"), mapped,
          multiway      ? .25
          : collections ? 2
                        : 1,
          bindings[2]);
    const auto camera = authoring_command(scene, "entity.create",
                                          {{"recipe", "render.camera"}, {"name", "Game camera"}})
                            .at("selected")
                            .get<std::string>();
    Camera lens;
    lens.far_plane = 100;
    scene.entity(camera).set(lens).set<LocalTranslation>({1, .5, -17});
    const auto light =
        authoring_command(scene, "entity.create",
                          {{"recipe", "render.light"}, {"name", "Directional light"}})
            .at("selected")
            .get<std::string>();
    scene.entity(light).set<LocalRotation>({-.2, -.3, 0, .9327379053});
    scene.save(root / path);
    catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
    const auto lease = std::make_shared<ProjectLease>(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    for (const auto& row : before)
        if (std::find(after.begin(), after.end(), row) == after.end())
            throw std::runtime_error("Skeletal sequence Gallery changed an existing asset record");
    return record.id;
}
} // namespace forge::test
