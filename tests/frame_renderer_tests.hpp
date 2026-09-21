#pragma once
#include "frame_renderer.hpp"
#include "render_values.hpp"
#include <fstream>
void check_frame_renderer(forge::DiligentPresentation& presentation,
                          Diligent::IDeviceContext* context, const std::filesystem::path& images) {
    using namespace forge;
    using Json = nlohmann::json;
    FrameRenderer renderer(presentation);
    Json document{{"asset_id", AssetId::generate()}, {"entities", Json::array()}};
    const auto first = EntityId::parse("10000000-0000-4000-8000-000000000001");
    const auto second = EntityId::parse("10000000-0000-4000-8000-000000000002");
    auto row = [&](EntityId id, const Camera& camera) {
        return Json{{"id", id},
                    {"spatial_resolved", true},
                    {"world_affine", AffineTransform{}.m},
                    {"components", {{"forge.camera", detail::render_value(camera)}}}};
    };
    Camera base;
    base.background_r = base.background_g = 0;
    base.background_b = 1;
    Camera overlay = base;
    overlay.viewport_width = .5;
    overlay.background_b = 0;
    overlay.background_r = 1;
    overlay.clear_depth = false;
    overlay.order = 1;
    document["entities"] = Json::array({row(second, overlay), row(first, base)});
    std::string stage = "camera composition";
    auto game = [&] {
        try {
            return renderer.game(context, document, 64, 32);
        } catch (const std::exception& error) {
            std::ofstream(images / "game-camera-failed-source.json") << document.dump(2);
            throw std::runtime_error(stage + ": " + error.what());
        }
    };
    auto render = [&] {
        auto* output = game();
        return readback(presentation.device(), context, output);
    };
    const auto split = render();
    save(split, 64, 32, images / "game-camera-composition.ppm");
    require(split[16 * 64 + 16][0] > 180 && split[16 * 64 + 16][2] < 100 &&
                split[16 * 64 + 48][2] > 180 && split[16 * 64 + 48][0] < 100,
            "Camera rectangle clear erased another camera or ignored composition order");
    require(renderer.diagnostics().empty(), "Valid game cameras emitted a diagnostic");
    overlay.clear_color = false;
    overlay.clear_depth = true;
    stage = "depth-only clear";
    document["entities"] = Json::array({row(second, overlay), row(first, base)});
    const auto depth_only = render();
    require(depth_only[16 * 64 + 16] == depth_only[16 * 64 + 48] &&
                depth_only[16 * 64 + 16][2] > 180,
            "Depth-only camera clear rewrote the previous camera color");
    base.aspect = 1;
    stage = "fixed aspect";
    document["entities"] = Json::array({row(first, base)});
    const auto fitted = render();
    require(fitted[16 * 64 + 4] == std::array<unsigned char, 4>{0, 0, 0, 255} &&
                fitted[16 * 64 + 32][2] > 180 &&
                fitted[16 * 64 + 60] == std::array<unsigned char, 4>{0, 0, 0, 255},
            "Fixed-aspect camera stretched or cleared its letterbox");
    base.enabled = false;
    stage = "disabled camera";
    document["entities"] = Json::array({row(first, base)});
    const auto empty = render();
    require(empty[16 * 64 + 32] == std::array<unsigned char, 4>{0, 0, 0, 255} &&
                renderer.diagnostics().size() == 1 &&
                renderer.diagnostics()[0].category == "render.camera.missing",
            "Missing game camera silently substituted an editor camera");
    base.enabled = true;
    base.background_r = base.background_g = base.background_b = .18f;
    stage = "neutral exposure";
    document["entities"] = Json::array({row(first, base)});
    const auto neutral = render();
    document["rendering"] = {{"version", 1u}, {"exposure", 1}};
    stage = "positive exposure";
    const auto brighter = render();
    require(brighter[16 * 64 + 32][0] > neutral[16 * 64 + 32][0],
            "Authored game exposure did not reach frame composition");
    const auto old = renderer.output();
    bool rejected = false;
    try {
        renderer.game(context, document, 0, 32);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && renderer.output() == old,
            "Invalid frame size destroyed the previous display");
    const auto project = images / "engine-frame-project";
    std::filesystem::create_directories(project);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project);
    host->catalog(std::make_shared<AssetCatalog>(project));
    renderer.resources(host);
    base = Camera{};
    base.background_r = base.background_g = base.background_b = 0;
    document.erase("rendering");
    auto object_world = AffineTransform{};
    object_world.m[11] = 3;
    Json object{{"id", EntityId::generate()},
                {"spatial_resolved", true},
                {"world_affine", object_world.m},
                {"components",
                 {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 3}}},
                  {"forge.tint", {{"r", .7f}, {"g", .1f}, {"b", .05f}}}}}};
    stage = "engine primitive";
    document["entities"] = Json::array({row(first, base), object});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        game();
        host->submit();
        if (!renderer.pending())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    const auto cube = render();
    require(!renderer.pending() && renderer.diagnostics().empty() &&
                cube[16 * 64 + 32][0] > cube[16 * 64 + 32][1] + 30,
            "Legacy blockout disappeared behind the authored Game camera");
    save(cube, 64, 32, images / "game-engine-primitive.ppm");
    object_world.m[0] = -1;
    stage = "mirrored engine primitive";
    document["entities"][1]["world_affine"] = object_world.m;
    const auto reflected = render();
    require(reflected[16 * 64 + 32][0] > reflected[16 * 64 + 32][1] + 30,
            "Negative engine primitive scale lost color/visibility");
    object_world.m[10] = 0;
    stage = "rank-two engine primitive";
    document["entities"][1]["world_affine"] = object_world.m;
    const auto collapsed = render();
    require(renderer.diagnostics().empty() && collapsed[16 * 64 + 32][0] > 50,
            "Rank-two engine primitive failed safe normal/visibility handling");
    {
        auto scene = extract_render_scene(document);
        require(scene.meshes.size() == 1, "Pose-budget fixture requires one engine mesh");
        MeshSceneRenderer measured(host);
        measured.update(scene); // Its engine CPU revision is already resident.
        require(!measured.pending() && measured.diagnostics().empty(),
                "Pose-budget baseline was not ready");
        const auto view = camera_view(base, {}, 64, 32);
        require(measured.pick(scene, view, 32, 16) == scene.meshes[0].entity,
                "Retained singular mesh pose was not selectable");
        scene.meshes[0].renderer.visible = false;
        require(measured.pick(scene, view, 32, 16) == scene.meshes[0].entity,
                "Hidden geometry lost independent selectability");
        scene.meshes[0].selectable = false;
        require(!measured.pick(scene, view, 32, 16),
                "Unselectable geometry was selected by the retained mesh consumer");
        scene.meshes[0].selectable = true;
        scene.meshes[0].renderer.visible = true;
        const auto one = measured.pose_payload_bytes();
        require(one > 0, "Retained mesh pose payload was not accounted");
        MeshSceneRenderer limited(host, Diligent::TEX_FORMAT_RGBA8_UNORM, 2 * one);
        limited.update(scene);
        require(limited.pose_payload_bytes() == one && limited.diagnostics().empty(),
                "Fitting pose was rejected by the scene budget");
        const auto original = scene.meshes[0];
        for (unsigned i = 0; i < 2; ++i) {
            auto copy = original;
            copy.entity = EntityId::generate();
            scene.meshes.push_back(copy);
        }
        limited.update(scene);
        const auto budget_error = std::any_of(
            limited.diagnostics().begin(), limited.diagnostics().end(), [](const auto& value) {
                return value.text.find("payload budget") != std::string::npos;
            });
        require(budget_error && limited.pose_payload_bytes() >= one &&
                    limited.pose_payload_bytes() <= 2 * one,
                "Aggregate scene pose admission exceeded its budget or lost retained poses");
        // The last object was refused. Removing the others releases their
        // payload before its retry, without changing source IDs or catalog.
        scene.meshes = {scene.meshes.back()};
        limited.update(scene);
        require(!limited.pending() && limited.diagnostics().empty() &&
                    limited.pose_payload_bytes() == one,
                "Freed scene pose payload did not permit the refused instance to retry");
        scene.meshes.clear();
        limited.update(scene);
        require(limited.pose_payload_bytes() == 0, "Removed mesh poses retained payload");
    }
    host->submit();
    renderer.resources({});
}
