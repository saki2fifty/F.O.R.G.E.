#pragma once
#include "frame_renderer.hpp"
#include "render_values.hpp"
#include "viewport.hpp"
#include <forge/engine_assets.hpp>
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
        const auto framed_bounds = measured.bounds(scene, {scene.meshes[0].entity}, view.position);
        require(framed_bounds && framed_bounds == measured.bounds(scene, {}, view.position),
                "Selected mesh framing did not use complete retained pose bounds");
        Viewport scene_view(presentation, true);
        scene_view.resources(host);
        EditorCamera scene_camera;
        require(scene_view.frame(document, scene.meshes[0].entity.str(), scene_camera, 2) &&
                    std::abs(scene_camera.target[2] - 3) < 1e-5,
                "Scene camera framing did not use current mesh world bounds");
        require(measured.pick(scene, view, 32, 16) == scene.meshes[0].entity,
                "Retained singular mesh pose was not selectable");
        scene.meshes[0].renderer.visible = false;
        require(!measured.bounds(scene, {}, view.position) &&
                    measured.bounds(scene, {scene.meshes[0].entity}, view.position) ==
                        framed_bounds,
                "Fit scene included hidden geometry or selected-hidden framing was lost");
        auto partial = scene;
        partial.meshes[0].renderer.visible = true;
        auto unready = partial.meshes[0];
        unready.entity = EntityId::generate();
        partial.meshes.push_back(unready);
        require(!measured.bounds(partial, {}, view.position) &&
                    measured.bounds(partial, {scene.meshes[0].entity}, view.position) ==
                        framed_bounds,
                "Framing silently accepted a partial unready group");
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
    {
        auto scene = extract_render_scene(document);
        const auto original = scene.meshes.at(0);
        scene.meshes.clear();
        for (unsigned i = 0; i < 65; ++i) {
            auto copy = original;
            copy.entity = EntityId::generate();
            copy.world.m[0] = 1;
            copy.world.m[10] = 1;
            scene.meshes.push_back(copy);
        }
        MeshSceneRenderer batches(host);
        batches.update(scene);
        require(!batches.pending() && batches.diagnostics().empty() && batches.bundle_count() == 1,
                "Repeated immutable geometry allocated separate native bundles");
        Diligent::TextureDesc desc;
        desc.Name = "FORGE native batch acceptance";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = 64;
        desc.Height = 32;
        desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = Diligent::BIND_RENDER_TARGET;
        Diligent::RefCntAutoPtr<Diligent::ITexture> color, depth;
        presentation.device()->CreateTexture(desc, nullptr, &color);
        desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
        presentation.device()->CreateTexture(desc, nullptr, &depth);
        require(color && depth, "Batch target allocation failed");
        auto* rtv = color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1, 0,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport area{0, 0, 64, 32, 0, 1};
        context->SetViewports(1, &area, 64, 32);
        const auto view = camera_view(base, {}, 64, 32);
        batches.draw(scene, view, UINT32_MAX);
        auto stats = batches.draw_stats();
        require(stats.calls == 2 && stats.instances == 65 && stats.batched_calls == 1 &&
                    batches.diagnostics().empty(),
                "Repeated geometry was not split into bounded native batches");
        scene.meshes.back().world.m[0] = -1;
        batches.update(scene);
        batches.draw(scene, view, UINT32_MAX);
        stats = batches.draw_stats();
        require(stats.calls == 2 && stats.instances == 65 && stats.batched_calls == 1,
                "Reflected instance was combined with the wrong winding group");
        scene.meshes.front().legacy_tint = std::array<float, 3>{-1, 0, 0};
        batches.draw(scene, view, UINT32_MAX);
        require(batches.draw_stats().instances == 64 && batches.diagnostics().size() == 1 &&
                    batches.diagnostics()[0].context.entity == scene.meshes.front().entity,
                "One invalid instance hid valid neighbors or lost its entity diagnostic");
        scene.meshes.clear();
        batches.update(scene);
        require(batches.bundle_count() == 0, "Native bundle cache retained removed geometry");
    }
    {
        auto scene = extract_render_scene(document);
        MeshSceneRenderer capacity(host, Diligent::TEX_FORMAT_RGBA8_UNORM, mesh_pose_payload_limit,
                                   1);
        capacity.update(scene);
        require(capacity.bundle_count() == 1 && capacity.diagnostics().empty(),
                "Native part budget rejected its first complete draw");
        auto extra = scene.meshes[0];
        extra.entity = EntityId::generate();
        extra.renderer.mesh = engine_primitive(1);
        scene.meshes.push_back(extra);
        auto denied = [&] {
            return std::any_of(
                capacity.diagnostics().begin(), capacity.diagnostics().end(),
                [](const auto& d) { return d.text.find("draw-part budget") != std::string::npos; });
        };
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            capacity.update(scene);
            if (denied())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < until);
        require(denied() && capacity.bundle_count() == 1,
                "Native part capacity did not reject before allocating another bundle");
        scene.meshes = {extra};
        capacity.update(scene);
        require(!capacity.pending() && capacity.bundle_count() == 1 &&
                    capacity.diagnostics().empty(),
                "Freed native part capacity did not permit a refused draw to retry");
    }
    {
        auto catalog = std::make_shared<const AssetCatalog>(project);
        const auto catalog_before = catalog->document();
        const AssetRef<MaterialAsset> ref{AssetId::generate()};
        MaterialResourceData material;
        material.values.model = "forge.gltf.unlit.v1";
        material.values.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                         {0, 0, 1, 1}};
        bool denied = false;
        try {
            host->preview_material(ref, material);
        } catch (const std::exception&) {
            denied = true;
        }
        require(denied, "Unsaved material entered a normal scene resource host");
        auto preview_host =
            std::make_shared<MeshResourceHost>(presentation, context, project, true);
        preview_host->catalog(catalog);
        preview_host->preview_material(ref, material);
        FrameRenderer preview_frame(presentation);
        preview_frame.resources(preview_host);
        auto preview_scene = extract_render_scene(document);
        preview_scene.settings.shadows.enabled = false;
        preview_scene.meshes[0].renderer.materials = {{"surface", ref}};
        preview_scene.meshes[0].legacy_tint.reset();
        const auto cameras = prepare_game_cameras(preview_scene, 64, 32);
        auto prepared_preview = [&] {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do {
                preview_frame.render(context, preview_scene, cameras.cameras, 64, 32, 0);
                preview_host->submit();
                if (!preview_frame.pending())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (std::chrono::steady_clock::now() < until);
            require(!preview_frame.pending(), "Material preview preparation stalled");
            return readback(presentation.device(), context, preview_frame.output());
        };
        const auto blue = prepared_preview();
        require(preview_frame.diagnostics().empty() &&
                    blue[16 * 64 + 32][2] > blue[16 * 64 + 32][0] + 50,
                "Unsaved material did not render through shared frame path");
        material.values.parameters["baseColorFactor"].value = {1, 0, 0, 1};
        preview_host->preview_material(ref, material);
        const auto red = prepared_preview();
        require(red[16 * 64 + 32][0] > red[16 * 64 + 32][2] + 50,
                "Material preview retained stale factor values");
        save(red, 64, 32, images / "material-draft-preview.ppm");
        material.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        material.textures["baseColorTexture"] = {AssetId::generate()};
        preview_host->preview_material(ref, material);
        const auto rejected_preview = prepared_preview();
        require(!preview_frame.diagnostics().empty() && rejected_preview == red,
                "Missing draft texture erased last-good preview");
        require(catalog->document() == catalog_before,
                "Unsaved material preview published or mutated catalog state");
        preview_frame.resources({});
    }
    host->submit();
    renderer.resources({});
}
