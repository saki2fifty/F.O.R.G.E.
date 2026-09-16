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
    for (unsigned view = 0; view < 8; ++view) {
        forge::EditorCamera camera;
        camera.target = {0, 0, 0};
        if (view < 6)
            camera.align(view / 2, view % 2 ? -1 : 1);
        else
            camera.orbit(83, -48);
        if (view == 7)
            camera.pan(30, -25, 600);
        for (unsigned axis : {0u, 2u}) {
            const auto line = forge::project_world_axis(camera, axis, 800, 600);
            require(bool(line), "Visible world axis disappeared");
            for (const auto& point : *line)
                require(std::isfinite(point[0]) && std::isfinite(point[1]) && point[0] >= -.01f &&
                            point[0] <= 800.01f && point[1] >= -.01f && point[1] <= 600.01f,
                        "World axis escaped frustum");
            for (float distance : {-1.0f, 0.0f, 1.0f}) {
                forge::Vec3 world{};
                world[axis] = distance;
                const auto p = forge::project_point(camera, world, 800, 600);
                if (!p)
                    continue;
                const auto a = (*line)[0], b = (*line)[1];
                const float length = std::hypot(b[0] - a[0], b[1] - a[1]);
                if (length > .01f)
                    require(std::abs(((*p)[0] - a[0]) * (b[1] - a[1]) -
                                     ((*p)[1] - a[1]) * (b[0] - a[0])) /
                                    length <
                                .01f,
                            "Grid axis drifted away from fixed world coordinates");
            }
        }
    }
    forge::EditorCamera camera;
    camera.target = {0, 0, 0};
    camera.pitch = -.5f;
    const auto original = forge::project_world_axis(camera, 0, 800, 600);
    // Same eye and orientation, different orbit target/distance: projection is identical.
    const auto f = camera.forward();
    for (unsigned i = 0; i < 3; ++i)
        camera.target[i] += f[i] * 10;
    camera.distance += 10;
    const auto shifted = forge::project_world_axis(camera, 0, 800, 600);
    require(original && shifted, "Axis vanished after target change");
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned j = 0; j < 2; ++j)
            require(std::abs((*original)[i][j] - (*shifted)[i][j]) < .01f,
                    "Axis endpoints depend on orbit target rather than world origin");
    require(!forge::project_world_axis(camera, 0, 0, 600), "Zero-width axis projection accepted");
    camera = {};
    camera.target = {0, 10, 0};
    camera.pitch = forge::EditorCamera::pole;
    require(!forge::project_world_axis(camera, 0, 800, 600), "Axis behind camera was drawn");
}
