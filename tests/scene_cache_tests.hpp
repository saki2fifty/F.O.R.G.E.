#pragma once
#include "authoring_tests.hpp"
#include "scene_cache.hpp"
inline void test_scene_cache() {
    forge::Scene scene;
    scene.reset(authoring_fixture());
    forge::AuthoringSnapshot snapshot;
    const auto first = snapshot.document(scene);
    const auto* storage = &snapshot.effective(scene).at("entities").at(0);
    for (int i = 0; i < 20; ++i)
        require(&snapshot.effective(scene).at("entities").at(0) == storage,
                "Unchanged authoring snapshot rebuilt");
    scene.rename_entity("front", "Renamed");
    require(snapshot.document(scene)["entities"][0]["name"] == "Renamed", "Stale cached name");
    require(scene.undo() && snapshot.document(scene) == first, "Cache missed undo");
    require(scene.redo() && snapshot.document(scene)["entities"][0]["name"] == "Renamed",
            "Cache missed redo");
    auto inherited = authoring_fixture();
    inherited["entities"][0]["prefab"] = true;
    inherited["entities"][1].erase("parent");
    inherited["entities"][1]["base"] = "front";
    inherited["entities"][1]["components"] = forge::Json::object();
    scene.reset(inherited);
    require(snapshot.effective(scene)["entities"][1]["components"]["forge.position"]["extra"] ==
                "keep",
            "Cached prefab resolution lost unknown data");
    auto changed = scene.document();
    changed["entities"][0]["components"]["forge.position"]["x"] = 7;
    scene.edit(changed);
    require(snapshot.effective(scene)["entities"][1]["components"]["forge.position"]["x"] == 7,
            "Prefab edit did not invalidate effective snapshot");
    forge::Scene another;
    another.reset(authoring_fixture());
    require(snapshot.document(another) == another.document(), "Cache reused another scene");
    scene.reset({{"version", 1}, {"entities", forge::Json::array()}});
    require(snapshot.effective(scene)["entities"].empty(), "Document reset kept old geometry");

    forge::PreviewSnapshot preview;
    int builds = 0;
    auto build = [&] {
        ++builds;
        return scene.document();
    };
    preview.get(1, false, false, build);
    const auto generation = preview.generation();
    preview.get(1, false, false, build);
    require(builds == 1 && preview.generation() == generation, "Idle preview was rebuilt");
    preview.get(1, false, true, build);
    preview.get(1, false, true, build);
    require(builds == 3, "Transient preview stopped updating");
    preview.get(1, false, false, build);
    require(builds == 4, "Cancelling preview did not restore authoring view");
    preview.get(2, false, false, build);
    preview.get(2, true, false, build);
    preview.get(3, true, false, build);
    preview.get(3, false, false, build);
    require(builds == 8, "Preview missed edit, play entry, runtime update, or stop");
    try {
        preview.get(4, false, false, []() -> forge::Json { throw std::runtime_error("probe"); });
    } catch (const std::runtime_error&) {
    }
    preview.get(4, false, false, build);
    require(builds == 9, "Failed extraction poisoned retry");

    forge::EditorCamera camera;
    const auto key = forge::viewport_frame_key(1, 800, 600, camera);
    require(key == forge::viewport_frame_key(1, 800, 600, camera), "Idle camera cache mismatch");
    require(key != forge::viewport_frame_key(2, 800, 600, camera), "Scene change kept texture");
    require(key != forge::viewport_frame_key(1, 801, 600, camera), "Resize kept texture");
    camera.orbit(12, 9);
    require(key != forge::viewport_frame_key(1, 800, 600, camera), "Orbit kept texture");
    camera = {};
    camera.target[0] += 1;
    require(key != forge::viewport_frame_key(1, 800, 600, camera), "Pan kept texture");
    camera = {};
    camera.distance += 1;
    require(key != forge::viewport_frame_key(1, 800, 600, camera), "Zoom kept texture");
}
inline void test_world_grid_axes() {
    static_assert(sizeof(forge::GridConstants) == 80);
    forge::EditorCamera camera;
    camera.target = {0, 0, 0};
    camera.pitch = -.5f;
    camera.distance = 12;
    for (unsigned motion = 0; motion < 6; ++motion) {
        if (motion == 1)
            camera.look(30, 15);
        if (motion == 2)
            camera.pan(50, -30, 600);
        if (motion == 3)
            camera.orbit(-30, -20);
        if (motion == 4)
            camera.fly(.5f, .5f, .1f, .1f);
        if (motion == 5)
            camera.align(1, 1);
        const auto constants = forge::grid_constants(camera, 800, 600, {});
        for (forge::Vec3 p : {forge::Vec3{0, 0, 3}, forge::Vec3{3, 0, 0}, forge::Vec3{2, 0, 4}}) {
            const auto pixel = forge::project_point(camera, p, 800, 600);
            require(bool(pixel), "Grid fixture behind camera");
            // Reconstruct the ground point using the exact shader uniform layout.
            const float sx = (2 * (*pixel)[0] - constants.viewport_fade[0]) /
                             (constants.right_focal[3] * constants.viewport_fade[1]);
            const float sy = (2 * (*pixel)[1] - constants.viewport_fade[1]) /
                             (constants.right_focal[3] * constants.viewport_fade[1]);
            forge::Vec3 ray;
            for (unsigned i = 0; i < 3; ++i)
                ray[i] = constants.forward_near[i] + constants.right_focal[i] * sx -
                         constants.up_far[i] * sy;
            const float depth = -constants.eye_spacing[1] / ray[1];
            for (unsigned i = 0; i < 3; ++i)
                require(std::abs(constants.eye_spacing[i] + ray[i] * depth - p[i]) < .0001f,
                        "Shader ground projection disagrees with meshes/handles after navigation");
        }
    }
    const auto key = forge::viewport_frame_key(1, 800, 600, camera);
    require(key != forge::viewport_frame_key(1, 800, 600, camera, {false, 1}),
            "Grid visibility did not invalidate retained frame");
    require(key != forge::viewport_frame_key(1, 800, 600, camera, {true, 2}),
            "Grid spacing did not invalidate retained frame");
}
