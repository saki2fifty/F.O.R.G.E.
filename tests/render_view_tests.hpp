#pragma once
#include "light_image_frame_tests.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_view.hpp>
#include <limits>
#include <numbers>
inline void test_render_views() {
    test_light_image_frames();
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

    l = Light{};
    l.shadow_source_radius = .25f;
    l.shadow_angular_radius = .01f;
    pose.scale = {2, 3, 4};
    const auto extended = light_view(l, affine_transform(pose));
    require(extended.shadow_source_radius == .25f && extended.shadow_angular_radius == .01f,
            "Light scale changed shadow source dimensions");
    for (float invalid : {-1.f, 10001.f, std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()}) {
        auto bad = l;
        bad.shadow_source_radius = invalid;
        rejects([&] { validate_light(bad); });
    }
    for (float invalid : {-1.f, 1.f, std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()}) {
        auto bad = l;
        bad.shadow_angular_radius = invalid;
        rejects([&] { validate_light(bad); });
    }
    l.shadow_source_radius = 10000;
    l.shadow_angular_radius = float(std::numbers::pi / 4);
    validate_light(l);

    static_assert(unsigned(LightKind::Directional) == 0 && unsigned(LightKind::Point) == 1 &&
                  unsigned(LightKind::Spot) == 2 && unsigned(LightKind::Rectangle) == 3 &&
                  unsigned(LightKind::Sphere) == 4);
    l = Light{};
    l.kind = unsigned(LightKind::Rectangle);
    l.emitter_width = 2;
    l.emitter_height = 3;
    pose = LocalTransform{};
    pose.rotation = rotation_from_euler({0, 0, 90});
    pose.scale = {2, 0, 4};
    const auto rectangle = light_view(l, affine_transform(pose));
    close(rectangle.right[0], 0);
    close(rectangle.right[1], 1);
    close(rectangle.up[0], 1);
    close(rectangle.direction[2], 1);
    require(rectangle.emitter_width == 2 && rectangle.emitter_height == 3,
            "Rectangle size changed with node scale");
    pose.scale = {0, 0, 4};
    rejects([&] { light_view(l, affine_transform(pose)); });
    l.kind = unsigned(LightKind::Sphere);
    l.emitter_radius = 2;
    pose.scale = {0, 0, 0};
    require(light_view(l, affine_transform(pose)).emitter_radius == 2,
            "Spherical emitter incorrectly requires a node orientation");
    for (float invalid : {0.f, -1.f, .0001f, 10001.f, std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()}) {
        for (auto field : {&Light::emitter_radius, &Light::emitter_width, &Light::emitter_height}) {
            auto bad = l;
            bad.*field = invalid;
            rejects([&] { validate_light(bad); });
        }
    }

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
    require(lamp.at("shadow_source_radius") == 0 && lamp.at("shadow_angular_radius") == 0,
            "Existing light defaults unexpectedly enable soft shadows");
    auto legacy = doc;
    legacy["entities"][0]["components"]["forge.light"].erase("shadow_source_radius");
    legacy["entities"][0]["components"]["forge.light"].erase("shadow_angular_radius");
    for (const auto* field : {"emitter_radius", "emitter_width", "emitter_height", "texture",
                              "image_width", "image_height", "image_tiled"})
        legacy["entities"][0]["components"]["forge.light"].erase(field);
    scene.reset(legacy);
    require(scene.entity(id).get<Light>().shadow_source_radius == 0 &&
                scene.entity(id).get<Light>().shadow_angular_radius == 0,
            "Existing light documents did not retain fixed shadow filtering");
    require(scene.entity(id).get<Light>().emitter_radius == .5f &&
                scene.entity(id).get<Light>().emitter_width == 1 &&
                scene.entity(id).get<Light>().emitter_height == 1,
            "Existing lights lost area dimension defaults");
    require(!scene.entity(id).get<Light>().texture.id &&
                scene.entity(id).get<Light>().image_width == 1 &&
                scene.entity(id).get<Light>().image_height == 1 &&
                !scene.entity(id).get<Light>().image_tiled,
            "Legacy lights did not retain disabled image defaults");
    scene.reset(doc);
    const auto image = AssetId::generate();
    apply_authoring(scene,
                    Json::array({{{"operation", "property.set"},
                                  {"arguments",
                                   {{"entity", id},
                                    {"component", "forge.light"},
                                    {"field", "texture"},
                                    {"value", image}}}},
                                 {{"operation", "property.set"},
                                  {"arguments",
                                   {{"entity", id},
                                    {"component", "forge.light"},
                                    {"field", "image_width"},
                                    {"value", 4}}}},
                                 {{"operation", "property.set"},
                                  {"arguments",
                                   {{"entity", id},
                                    {"component", "forge.light"},
                                    {"field", "image_tiled"},
                                    {"value", true}}}}}),
                    scene.revision());
    require(scene.entity(id).get<Light>().texture.id == image &&
                scene.entity(id).get<Light>().image_width == 4 &&
                scene.entity(id).get<Light>().image_tiled,
            "Light image authoring lost typed values");
    const auto image_doc = scene.document();
    for (const auto& patch : {Json{{"field", "image_height"}, {"value", 0}},
                              Json{{"field", "texture"}, {"value", "invalid-uuid"}},
                              Json{{"field", "kind"}, {"value", unsigned(LightKind::Sphere)}}}) {
        auto args = patch;
        args["entity"] = id;
        args["component"] = "forge.light";
        rejects([&] { authoring_command(scene, "property.set", args); });
        require(scene.document() == image_doc, "Rejected light image edit changed source");
    }
    require(scene.undo() && scene.document() == doc && scene.redo() &&
                scene.document() == image_doc,
            "Light image batch history lost unrelated source data");
    scene.reset(image_doc);
    require(scene.entity(id).get<Light>().texture.id == image,
            "Light image source roundtrip lost its asset identity");
    scene.reset(doc);
    const auto edit_revision = scene.revision();
    apply_authoring(scene,
                    Json::array({{{"operation", "property.set"},
                                  {"arguments",
                                   {{"entity", id},
                                    {"component", "forge.light"},
                                    {"field", "shadow_source_radius"},
                                    {"value", .25}}}},
                                 {{"operation", "property.set"},
                                  {"arguments",
                                   {{"entity", id},
                                    {"component", "forge.light"},
                                    {"field", "shadow_angular_radius"},
                                    {"value", .01}}}}}),
                    edit_revision);
    require(scene.entity(id).get<Light>().shadow_source_radius == .25f &&
                scene.entity(id).get<Light>().shadow_angular_radius == .01f,
            "Shared property operations did not update shadow source dimensions");
    scene.undo();
    require(scene.document() == doc, "Soft shadow Undo changed unrelated light data");
    scene.redo();
    require(scene.entity(id).get<Light>().shadow_source_radius == .25f,
            "Soft shadow Redo did not restore source dimensions");
    scene.reset(doc);
    apply_authoring(
        scene,
        Json::array(
            {{{"operation", "property.set"},
              {"arguments",
               {{"entity", id}, {"component", "forge.light"}, {"field", "kind"}, {"value", 3}}}},
             {{"operation", "property.set"},
              {"arguments",
               {{"entity", id},
                {"component", "forge.light"},
                {"field", "emitter_width"},
                {"value", 2}}}}}),
        scene.revision());
    require(scene.entity(id).get<Light>().kind == unsigned(LightKind::Rectangle) &&
                scene.entity(id).get<Light>().emitter_width == 2,
            "Area light authoring did not use the shared property owner");
    scene.undo();
    require(scene.document() == doc, "Area light Undo changed unrelated authored data");
    scene.redo();
    require(scene.entity(id).get<Light>().emitter_width == 2,
            "Area light Redo lost source dimensions");
    scene.reset(doc);
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
    // Optional projected-color data uses the ordinary schema/property/history owner.
    DecalProjector projector;
    validate_decal(projector);
    require(projector.color_influence == 1 && projector.normal_influence == 0 &&
                projector.roughness_influence == 0,
            "Default projector channels changed existing color intent");
    for (const auto field : {&DecalProjector::color_influence, &DecalProjector::normal_influence,
                             &DecalProjector::roughness_influence})
        for (float bad : {-.1f, 1.1f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
            auto invalid = projector;
            invalid.*field = bad;
            rejects([&] { validate_decal(invalid); });
        }
    for (const auto field :
         {&DecalProjector::width, &DecalProjector::height, &DecalProjector::depth})
        for (float bad : {0.f, -1.f, .0001f, 10001.f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
            auto invalid = projector;
            invalid.*field = bad;
            rejects([&] { validate_decal(invalid); });
        }
    for (const auto field : {&DecalProjector::tint_r, &DecalProjector::tint_g,
                             &DecalProjector::tint_b, &DecalProjector::opacity}) {
        auto invalid = projector;
        invalid.*field = 1.1f;
        rejects([&] { validate_decal(invalid); });
    }
    auto invalid_projector = projector;
    invalid_projector.angle_fade_end = invalid_projector.angle_fade_start;
    rejects([&] { validate_decal(invalid_projector); });
    invalid_projector.angle_fade_end = decal_max_angle + .1f;
    rejects([&] { validate_decal(invalid_projector); });
    invalid_projector.angle_fade_start = 0;
    invalid_projector.angle_fade_end = 1e-8f;
    validate_decal(invalid_projector);
    rejects([&] { (void)decal_view(invalid_projector, {}); });
    projector.width = 2;
    projector.height = 4;
    projector.depth = 6;
    LocalTransform projector_pose;
    projector_pose.translation = {3, -2, 9};
    projector_pose.rotation = rotation_from_euler({20, 30, 60});
    projector_pose.scale = {-2, 3, 4};
    auto projector_world = affine_transform(projector_pose);
    projector_world.m[1] += .2;
    const auto projected = decal_view(projector, projector_world);
    const auto normalized_point =
        projected.projector_from_world.point(projector_world.point({.5, -1, 1.5}));
    for (unsigned axis = 0; axis < 3; ++axis)
        require(std::abs(normalized_point[axis] - (axis == 1 ? -.25 : .25)) < 1e-12,
                "Decal normalized projection lost scale/reflection/shear or local dimensions");
    const Double3 projector_origin{100, -200, 300};
    const auto gpu_projector = decal_gpu_transform(projected, projector_origin);
    const auto projector_point = projector_world.point({.5, -1, 1.5});
    for (unsigned row = 0; row < 3; ++row) {
        double coordinate = gpu_projector[row][3];
        for (unsigned column = 0; column < 3; ++column)
            coordinate +=
                gpu_projector[row][column] * (projector_point[column] - projector_origin[column]);
        require(std::abs(coordinate - normalized_point[row]) < 1e-5,
                "Camera-relative GPU decal transform lost reflected/sheared coordinates");
    }
    AffineTransform tiny_projector;
    tiny_projector.m[0] = tiny_projector.m[5] = tiny_projector.m[10] = 5e-39;
    tiny_projector.m[11] = 3;
    const auto tiny_projection = decal_view(DecalProjector{}, tiny_projector);
    rejects([&] { (void)decal_gpu_transform(tiny_projection, {}); });
    const auto relative_tiny = decal_gpu_transform(tiny_projection, {0, 0, 3});
    require(relative_tiny[2][3] == 0 && relative_tiny[2][2] > 1e38f,
            "Decal GPU admission rejected a representable camera-relative translation");
    tiny_projector.m[0] = tiny_projector.m[5] = tiny_projector.m[10] = 1e-100;
    const auto unrepresentable_projection = decal_view(DecalProjector{}, tiny_projector);
    rejects([&] { (void)decal_gpu_transform(unrepresentable_projection, {0, 0, 3}); });
    auto collapsed_projector = projector_world;
    for (unsigned row = 0; row < 3; ++row)
        collapsed_projector.m[row * 4] = 0;
    rejects([&] { (void)decal_view(projector, collapsed_projector); });
    bool decal_metadata = false, material_reference = false, decal_dimensions = false,
         decal_angles = false;
    for (const auto& component : schema.at("components"))
        if (component.at("id") == "forge.decal") {
            decal_metadata =
                component.at("optional").get<bool>() && component.at("category") == "Rendering";
            for (const auto& field : component.at("fields")) {
                if (field.at("id") == "material")
                    material_reference = field.at("type") == "asset_ref" &&
                                         field.at("asset_type") == MaterialAsset::type &&
                                         field.at("default").is_null();
                if (field.at("id") == "width")
                    decal_dimensions = field.at("unit") == "m" &&
                                       field.at("minimum").get<double>() > 0 &&
                                       field.at("maximum") == 10000;
                if (field.at("id") == "angle_fade_end")
                    decal_angles = field.at("unit") == "rad";
            }
        }
    require(decal_metadata && material_reference && decal_dimensions && decal_angles,
            "Decal schema lost optionality, asset ownership, dimensions or native units");
    scene.reset(doc);
    authoring_command(scene, "transform.local",
                      {{"entity", id},
                       {"translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                       {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}},
                       {"scale", {{"x", 1}, {"y", 1}, {"z", 1}}}});
    authoring_command(scene, "component.add", {{"entity", id}, {"component", "forge.decal"}});
    require(scene.entity(id).has<DecalProjector>() &&
                extract_render_scene(scene.effective_document()).decals.empty(),
            "Empty decal assignment unexpectedly published a render source");
    const auto decal_defaults = scene.document();
    auto legacy_decal = decal_defaults;
    auto& legacy_values = legacy_decal["entities"][0]["components"]["forge.decal"];
    for (const auto* field : {"color_influence", "normal_influence", "roughness_influence"})
        legacy_values.erase(field);
    EngineContext legacy_engine;
    Scene legacy_scene(legacy_engine.world());
    legacy_scene.restore_snapshot(legacy_decal);
    const auto& legacy_value = legacy_scene.entity(id).get<DecalProjector>();
    require(legacy_scene.document() == decal_defaults && legacy_value.color_influence == 1 &&
                legacy_value.normal_influence == 0 && legacy_value.roughness_influence == 0,
            "Older projector did not normalize to equivalent default channel behavior");
    authoring_command(scene, "property.set_many",
                      {{"entity", id},
                       {"component", "forge.decal"},
                       {"values",
                        {{"material", engine_material().id},
                         {"width", 2},
                         {"height", 4},
                         {"depth", 6},
                         {"opacity", .25},
                         {"color_influence", .6},
                         {"normal_influence", .7},
                         {"roughness_influence", .8},
                         {"priority", 3},
                         {"angle_fade_start", .5},
                         {"angle_fade_end", 1.}}}});
    const auto decal_state = scene.document();
    const auto decal_revision = scene.revision();
    auto admitted_decals = extract_render_scene(scene.effective_document());
    require(admitted_decals.decals.size() == 1 &&
                admitted_decals.decals.front().projector.values.material == engine_material() &&
                admitted_decals.decals.front().projector.values.width == 2 &&
                admitted_decals.decals.front().projector.values.priority == 3 &&
                scene.document() == decal_state,
            "Decal extraction lost typed values or changed authored data");
    rejects([&] {
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.decal"},
                           {"field", "angle_fade_end"},
                           {"value", .1}});
    });
    require(scene.document() == decal_state && scene.revision() == decal_revision,
            "Invalid decal angle consumed history/revision or changed values");
    scene.undo();
    require(scene.document() == decal_defaults, "Decal Undo changed unrelated components");
    scene.redo();
    require(scene.document() == decal_state, "Decal Redo lost values");
    auto extended_decal = decal_state;
    extended_decal["entities"][0]["components"]["forge.decal"]["extension"] = {{"retain", 17}};
    scene.reset(extended_decal);
    authoring_command(
        scene, "property.set",
        {{"entity", id}, {"component", "forge.decal"}, {"field", "opacity"}, {"value", 0.}});
    require(extract_render_scene(scene.effective_document()).decals.empty() &&
                scene.document()["entities"][0]["components"]["forge.decal"]["extension"] ==
                    Json{{"retain", 17}},
            "Zero decal did not bypass extraction or lost opaque fields");
    EngineContext runtime_engine(WorldRole::Runtime);
    Scene runtime(runtime_engine.world());
    runtime.restore_snapshot(decal_state);
    const auto untuned_revision = runtime.revision();
    const auto authored_revision = scene.revision();
    const auto authored_values = scene.document();
    require(runtime.tune_runtime_property(id, "forge.decal", "normal_influence", .5) == .5 &&
                runtime.entity(id).get<DecalProjector>().normal_influence == .5f &&
                runtime.tune_runtime_property(id, "forge.decal", "width", 3.) == 3. &&
                runtime.entity(id).get<DecalProjector>().width == 3 &&
                runtime.revision() > untuned_revision && !runtime.undo() &&
                scene.revision() == authored_revision && scene.document() == authored_values &&
                scene.entity(id).get<DecalProjector>().width == 2,
            "Live decal tuning changed source/history or failed shared typed validation");
    const auto live_values = runtime.document();
    const auto runtime_revision = runtime.revision();
    rejects([&] { runtime.tune_runtime_property(id, "forge.decal", "angle_fade_end", .1); });
    rejects([&] { runtime.tune_runtime_property(id, "forge.decal", "roughness_influence", 1.1); });
    rejects([&] {
        runtime.tune_runtime_property(id, "forge.decal", "material", engine_material().id);
    });
    require(runtime.document() == live_values && runtime.revision() == runtime_revision,
            "Rejected live decal tuning changed runtime values or revision");
    EngineContext recipe_engine;
    Scene recipes(recipe_engine.world());
    const auto created = authoring_command(recipes, "entity.create", {{"recipe", "render.decal"}});
    const auto created_id = created.at("selected").get<std::string>();
    require(recipes.entity(created_id).has<DecalProjector>() &&
                recipes.entity(created_id).has<LocalTranslation>(),
            "Shared decal creation recipe omitted its component or transform");
    recipes.undo();
    require(!recipes.entity(created_id), "Shared decal recipe did not Undo atomically");
    recipes.redo();
    require(recipes.entity(created_id).has<DecalProjector>(),
            "Decal recipe Redo lost its component");
}
