#pragma once
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_view.hpp>
#include <limits>
#include <numbers>
inline void test_render_views() {
    using namespace forge;
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto rejects = [&](auto fn) {
        bool caught = false;
        try {
            fn();
        } catch (const std::exception&) {
            caught = true;
        }
        require(caught, "Invalid camera/light value was accepted");
    };
    auto close = [&](double a, double b) {
        require(std::abs(a - b) < 1e-5, "Camera/light numeric mismatch");
    };
    Camera c;
    auto view = camera_view(c, {}, 800, 600);
    require(view.viewport == PixelViewport{0, 0, 800, 600} && view.forward == Double3{0, 0, 1} &&
                !view.orientation_reversed,
            "Default camera changed FORGE basis or viewport");
    auto depth = [](const CameraView& v, double z) {
        const auto& p = v.projection;
        return (p[10] * z + p[11]) / (p[14] * z + p[15]);
    };
    close(depth(view, c.near_plane), 0);
    close(depth(view, c.far_plane), 1);
    const auto y = 2 * std::tan(c.vertical_fov / 2);
    close(view.projection[5] * y / 2, 1);
    c.basis = std::uint32_t(ViewBasis::GltfNegativeZ);
    view = camera_view(c, {}, 800, 600);
    require(view.forward == Double3{0, 0, -1} && view.view.point({0, 0, -2}) == Double3{0, 0, 2} &&
                view.orientation_reversed,
            "Imported camera did not adapt -Z to positive view depth");
    c.infinite_far = true;
    c.near_plane = 2000; // Ignored finite far is not an authoring restriction.
    view = camera_view(c, {}, 800, 600);
    close(depth(view, c.near_plane), 0);
    close(depth(view, 1e12), 1);
    c = Camera{};
    c.aspect = 1;
    view = camera_view(c, {}, 800, 600);
    require(view.viewport == PixelViewport{100, 0, 600, 600},
            "Fixed camera aspect stretched the image");
    c.viewport_x = .5;
    c.viewport_width = .5;
    view = camera_view(c, {}, 800, 600);
    require(view.viewport == PixelViewport{400, 100, 400, 400},
            "Sub-viewport aspect fit is incorrect");
    c = Camera{};
    c.projection = std::uint32_t(CameraProjection::Orthographic);
    c.near_plane = 0;
    c.orthographic_height = 4;
    c.orthographic_width = 8;
    c.flip_x = true;
    view = camera_view(c, {}, 800, 600);
    require(view.viewport == PixelViewport{0, 100, 800, 400} && view.orientation_reversed,
            "Orthographic fixed size/flip lost viewport semantics");
    close(view.projection[0] * 4, -1);
    close(view.projection[5] * 2, 1);
    close(depth(view, 0), 0);
    close(depth(view, c.far_plane), 1);
    LocalTransform pose;
    pose.translation = {20, 30, 40};
    pose.rotation = rotation_from_euler({0, 90, 0});
    pose.scale = {2, 3, 4};
    view = camera_view(Camera{}, affine_transform(pose), 800, 600);
    close(view.forward[0], 1);
    close(view.view.point({20, 30, 40})[2], 0);
    const auto projection = view.projection;
    pose.scale = {.000001f, 20, 300};
    require(camera_view(Camera{}, affine_transform(pose), 800, 600).projection == projection,
            "Camera world scale altered projection");
    pose.scale = {1, 0, 1};
    rejects([&] { camera_view(Camera{}, affine_transform(pose), 800, 600); });
    pose.scale = {-1, 1, 1};
    rejects([&] { camera_view(Camera{}, affine_transform(pose), 800, 600); });
    AffineTransform shear;
    shear.m[1] = .1;
    rejects([&] { camera_view(Camera{}, shear, 800, 600); });
    c = Camera{};
    c.viewport_width = 1e-20;
    rejects([&] { camera_view(c, {}, 800, 600); });
    c = Camera{};
    c.vertical_fov = std::numeric_limits<double>::denorm_min();
    rejects([&] { camera_view(c, {}, 800, 600); });
    c = Camera{};
    c.near_plane = 0;
    rejects([&] { validate_camera(c); });
    c = Camera{};
    c.near_plane = std::numeric_limits<float>::denorm_min();
    validate_camera(c); // Authored numerical validity is separate from GPU use.
    rejects([&] { camera_view(c, {}, 800, 600); });
    c = Camera{};
    c.aspect = -1;
    rejects([&] { validate_camera(c); });
    c = Camera{};
    c.projection = 1;
    c.infinite_far = true;
    rejects([&] { validate_camera(c); });
    c = Camera{};
    c.far_plane = c.near_plane;
    rejects([&] { validate_camera(c); });
    c = Camera{};
    c.vertical_fov = std::numbers::pi;
    rejects([&] { validate_camera(c); });
    c = Camera{};
    c.background_g = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { validate_camera(c); });
    Light l;
    l.kind = std::uint32_t(LightKind::Spot);
    l.basis = std::uint32_t(ViewBasis::GltfNegativeZ);
    l.intensity = 50;
    l.range = 10;
    pose.scale = {2, 3, 4};
    auto light = light_view(l, affine_transform(pose));
    close(light.direction[0], -1);
    require(light.intensity == 50 && light.range == 10, "Light scale changed physical properties");
    pose.scale = {2, 0, -4};
    light = light_view(l, affine_transform(pose));
    close(light.direction[0], 1); // Reflection changes actual direction; no inverse is needed.
    pose.scale = {0, 0, 0};
    rejects([&] { light_view(l, affine_transform(pose)); });
    l.kind = std::uint32_t(LightKind::Point);
    require(light_view(l, affine_transform(pose)).position == Double3{20, 30, 40},
            "Collapsed point light lost valid world location");
    l.intensity = std::numeric_limits<double>::max();
    rejects([&] { light_view(l, {}); });
    l = Light{};
    l.outer_cone = l.inner_cone;
    rejects([&] { validate_light(l); });
    l = Light{};
    l.kind = 2;
    l.outer_cone = 1e-12;
    rejects([&] { light_view(l, {}); });
    l = Light{};
    l.range = -1;
    rejects([&] { validate_light(l); });
    l = Light{};
    l.range = std::numeric_limits<float>::denorm_min();
    validate_light(l);
    rejects([&] { light_view(l, {}); });

    EngineContext engine;
    Scene scene(engine.world());
    const auto schema = scene.schema();
    Json camera = Json::object(), lamp = Json::object();
    bool angle_units = false, projection_choices = false;
    for (const auto& component : schema.at("components")) {
        if (component.at("id") != "forge.camera" && component.at("id") != "forge.light")
            continue;
        auto& dest = component.at("id") == "forge.camera" ? camera : lamp;
        for (const auto& field : component.at("fields")) {
            dest[field.at("id").get<std::string>()] = field.at("default");
            if (field.at("id") == "projection")
                projection_choices = field.at("choices").size() == 2;
            if (field.at("id") == "vertical_fov")
                angle_units = field.at("unit") == "rad";
            require(!field.at("description").get<std::string>().empty(),
                    "Camera/light contextual help missing");
        }
    }
    require(projection_choices && angle_units,
            "Native camera metadata lost choices or angle units");
    camera["future"] = {{"opaque", true}};
    const auto id = EntityId::generate().str();
    Json doc{{"version", 3},
             {"asset_id", AssetId::generate()},
             {"entities",
              Json::array({{{"id", id},
                            {"name", "Camera and light"},
                            {"components", {{"forge.camera", camera}, {"forge.light", lamp}}}}})}};
    scene.reset(doc);
    require(scene.document() == doc && scene.entity(id).get<Camera>().near_plane == .05 &&
                scene.entity(id).get<Light>().intensity == 1,
            "Typed camera/light roundtrip changed values/unknowns");
    auto revised = scene.entity(id).get<Camera>();
    revised.order = -2;
    scene.entity(id).set<Camera>(revised);
    require(scene.document()["entities"][0]["components"]["forge.camera"]["order"] == -2,
            "Camera serialization did not read native ECS authority");
    scene.reset(doc);
    authoring_command(
        scene, "property.set",
        {{"entity", id}, {"component", "forge.camera"}, {"field", "near_plane"}, {"value", .1}});
    scene.undo();
    require(scene.document() == doc, "Camera edit undo lost unknown data");
    scene.redo();
    require(scene.entity(id).get<Camera>().near_plane == .1, "Camera edit redo failed");
    const auto before = scene.document();
    rejects([&] {
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.camera"},
                           {"field", "far_plane"},
                           {"value", .001}});
    });
    require(scene.document() == before, "Invalid projection modified authored state");
    scene.undo();
    require(scene.document() == doc, "Failed projection edit polluted undo");
    const auto prefab = create_prefab_source(scene, id);
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    require(!scene.entity(instance).owns<Camera>() && !scene.entity(instance).owns<Light>(),
            "Camera/light inheritance was materialized");
    authoring_command(scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.camera"},
                       {"field", "near_plane"},
                       {"value", .05}});
    auto updated = prefab.source;
    updated["revision"] = 2;
    updated["members"][0]["components"]["forge.camera"]["near_plane"] = .2;
    updated["members"][0]["components"]["forge.camera"]["far_plane"] = 2000;
    updated["members"][0]["components"]["forge.light"]["intensity"] = 20;
    scene.publish_prefab_sources({{prefab.asset(), updated}}, [] {});
    require(scene.entity(instance).get<Camera>().near_plane == .05 &&
                scene.entity(instance).get<Camera>().far_plane == 2000 &&
                scene.entity(instance).get<Light>().intensity == 20 &&
                !scene.entity(instance).owns<Light>(),
            "Camera property override blocked independent source propagation");
    authoring_command(
        scene, "property.revert",
        {{"entity", instance}, {"component", "forge.camera"}, {"field", "near_plane"}});
    require(!scene.entity(instance).owns<Camera>() &&
                scene.entity(instance).get<Camera>().near_plane == .2,
            "Camera Revert did not resume inheritance");
    scene.undo();
    require(scene.entity(instance).get<Camera>().near_plane == .05,
            "Camera Revert undo lost equal-value intent");
    const auto good = scene.document();
    updated["revision"] = 3;
    updated["members"][0]["components"]["forge.light"]["inner_cone"] = 2;
    bool wrote = false;
    rejects(
        [&] { scene.publish_prefab_sources({{prefab.asset(), updated}}, [&] { wrote = true; }); });
    require(!wrote && scene.document() == good,
            "Invalid light source candidate changed prefab/scene");
}
