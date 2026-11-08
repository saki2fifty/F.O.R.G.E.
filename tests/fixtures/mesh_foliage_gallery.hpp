#pragma once
#include "asset_storage.hpp"
#include "editable_mesh_authoring.hpp"
#include "editable_mesh_document.hpp"
#include "mesh_scatter_authoring.hpp"
#include "model_render_resource.hpp"
#include "terrain_foliage_authoring.hpp"
#include <cmath>
#include <forge/scene_identity.hpp>
#include <fstream>

namespace forge::test_mesh_foliage_gallery {
inline constexpr auto scene_path = "Scenes/200-mesh-surface-foliage.scene.json";
inline constexpr auto mesh_path = "Assets/Foliage/PlacementRamp.mesh.json";
inline Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Mesh foliage Gallery file missing");
    return Json::parse(file);
}
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline void imported_profile(const ProjectLease& lease) {
    const auto catalog = AssetCatalog::open_project(lease.root());
    const auto member = [&](const char* path) {
        AssetId owner;
        for (const auto& [id, row] : catalog.records())
            if (!row.subasset && row.source == path && row.type == "model")
                owner = id;
        for (const auto& [id, row] : catalog.records())
            if (row.subasset && !row.subasset->removed && row.subasset->owner == owner &&
                row.type == MeshAsset::type)
                return id;
        throw std::runtime_error("Original Model Mesh member unavailable");
    };
    const auto model = member("Assets/FBX/Static.fbx");
    const auto data = asset_detail::load_mesh_selection(lease.root(), catalog, {model});
    const auto& part = data.value->mesh.lods[0].parts[0];
    const auto* stream = part.find("POSITION");
    check(stream && stream->components == 3 &&
              std::holds_alternative<std::vector<float>>(stream->values),
          "Original static Model position stream missing");
    const auto& positions = std::get<std::vector<float>>(stream->values);
    const auto point = [&](unsigned corner) {
        const auto i = part.indices.empty() ? corner : part.indices[corner];
        return Double3{positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
    };
    const auto a = point(0), b = point(1), c = point(2);
    const Double3 ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
        ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]},
        normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
               ab[0] * ac[1] - ab[1] * ac[0]};
    unsigned axis = 0;
    for (unsigned i = 1; i < 3; ++i)
        if (std::abs(normal[i]) > std::abs(normal[axis]))
            axis = i;
    check(normal[axis] != 0, "Original static Model first triangle is degenerate");
    const double sign = normal[axis] > 0 ? 1 : -1;
    Double3 euler{};
    if (axis == 0)
        euler[2] = 90 * sign;
    else if (axis == 1 && sign < 0)
        euler[0] = 180;
    else if (axis == 2)
        euler[0] = -90 * sign;
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&] {
        return authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
            .at("selected")
            .get<std::string>();
    };
    const auto surface = create(), prototype = create();
    const auto assign = [&](AssetId id) {
        authoring_command(scene, "property.set",
                          {{"entity", surface},
                           {"component", "forge.mesh_renderer"},
                           {"field", "mesh"},
                           {"value", id}});
    };
    assign(model);
    authoring_command(
        scene, "transform.rotation",
        {{"entity", surface}, {"value", {{"x", euler[0]}, {"y", euler[1]}, {"z", euler[2]}}}});
    AffineTransform world;
    const auto effective = scene.effective_document();
    for (const auto& entity : effective.at("entities"))
        if (entity.at("id") == surface)
            world.m = entity.at("world_affine").get<std::array<double, 12>>();
    const auto center =
        world.point({(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3});
    detail::MeshScatterSettings settings;
    settings.count = 1;
    settings.radius = .01;
    settings.spacing = 0;
    const auto offset = detail::mesh_scatter_layout(settings)[0].offset;
    settings.center = {center[0] - offset[0], 0, center[2] - offset[1]};
    const auto arguments =
        detail::mesh_scatter_arguments(prototype, settings, "Imported Mesh test");
    const auto before = scene.document();
    const auto plan = prepare_mesh_foliage(lease, scene, arguments, surface, 0, {}, true);
    check(plan.at("surface_mesh").get<AssetId>() == model &&
              plan.at("arguments").at("world_heights")[0].get<double>() >= center[1] - 1e-8 &&
              scene.document() == before,
          "Actual static Model projection failed or mutated Scene");
    assign(member("Assets/FBX/Skin.fbx"));
    bool rejected = false;
    try {
        (void)prepare_mesh_foliage(lease, scene, arguments, surface);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        rejected = message.find("skin") != std::string::npos ||
                   message.find("static triangle") != std::string::npos;
    }
    check(rejected, "Actual skinned Model was not rejected by the static surface profile");
}
inline void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path), "Mesh foliage Gallery already exists");
    auto lease = std::make_shared<ProjectLease>(root);
    imported_profile(*lease);
    auto mesh = EditableMeshDocument::create(lease, mesh_path, [](AssetId id) {
        auto source = EditableMeshSource::create_cube(id);
        for (auto& vertex : source.document["vertices"]) {
            auto p = vertex["position"].get<Double3>();
            p[0] *= 12;
            p[2] *= 12;
            p[1] = p[1] * .25 - .125 + .25 * p[0] + .125 * p[2];
            vertex["position"] = p;
        }
        source.validate();
        return source;
    });
    mesh->save();
    const auto id = mesh->source().asset();
    AssetImportService imports(lease, editable_mesh_import_registry(),
                               {"windows", "d3d12", "desktop"});
    imports.submit(
        imports.prepare(mesh_path, {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_editable_mesh_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(30)), "Mesh foliage import stalled");
    const auto published = imports.poll();
    check(published.size() == 1 && published[0].published, "Mesh foliage import failed");
    Json blade;
    const auto original_gallery = read(root / "Scenes/147-terrain-foliage.scene.json");
    for (const auto& entity : original_gallery.at("entities"))
        if (entity.at("name").get<std::string>().starts_with("Blade template"))
            blade = entity.at("components").at("forge.mesh_renderer");
    check(!blade.is_null(), "Original Gallery blade template missing");
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(duplicate_scene_asset(scene.document(), AssetId::generate(), scene.schema()));
    const auto create = [&](const char* name, const char* recipe, const Json& values, Double3 p) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name},
                                  {"recipe", recipe},
                                  {"component_values", values},
                                  {"position", {{"x", p[0]}, {"y", p[1]}, {"z", p[2]}}}})
            .at("selected")
            .get<std::string>();
    };
    const auto rotate = [&](const std::string& entity, Double3 axis, double degrees,
                            Double3 scale = {1, 1, 1}) {
        const auto q = rotation_about_axis(axis, degrees);
        authoring_command(scene, "transform.local",
                          {{"entity", entity},
                           {"rotation", {{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}}},
                           {"scale", {{"x", scale[0]}, {"y", scale[1]}, {"z", scale[2]}}}});
    };
    const auto camera = create("Mesh meadow camera", "render.camera",
                               {{"projection", 1}, {"orthographic_height", 22}}, {0, 18, -24});
    rotate(camera, {1, 0, 0}, 36.87f);
    const auto light = create("Directional meadow light", "render.light",
                              {{"kind", 0}, {"intensity", 3}}, {0, 15, -10});
    rotate(light, {1, 0, 0}, 45);
    const auto prototype =
        create("Blade template - select for Mesh placement", "render.mesh", blade, {0, 0, -1000});
    Json ground{{"mesh", id},
                {"material_parameters",
                 Json::array({{{"slot", "default"},
                               {"key", "baseColorFactor"},
                               {"parameter", {{"type", 5}, {"value", {.24, .28, .34, 1}}}}},
                              {{"slot", "default"},
                               {"key", "roughnessFactor"},
                               {"parameter", {{"type", 0}, {"value", {.85, 0, 0, 0}}}}}})}};
    const auto left =
        create("Mesh surface - tilted, aligned blades", "render.mesh", ground, {-7, 0, 0});
    const auto right =
        create("Mesh surface - mirrored, upright blades", "render.mesh", ground, {7, 0, 0});
    rotate(left, {0, 0, 1}, 20);
    rotate(right, {1, 0, 0}, -12, {-1, 1, 1});
    for (unsigned index = 0; index < 2; ++index) {
        detail::MeshScatterSettings settings;
        settings.center = {index ? 7. : -7., 0, 0};
        settings.radius = 4.6;
        settings.spacing = .55;
        settings.count = 128;
        settings.seed = index ? 73 : 91;
        settings.scale_min = .65;
        settings.scale_max = 1.15;
        const auto& surface = index ? right : left;
        const auto arguments = detail::mesh_scatter_arguments(
            prototype, settings, index ? "World-up Mesh meadow" : "Slope-aligned Mesh meadow");
        const auto plan =
            prepare_mesh_foliage(*lease, scene, arguments, surface, 0, {}, index == 0);
        const auto before = scene.document();
        const auto result = authoring_command(scene, "mesh.scatter", plan.at("arguments"));
        const auto after = scene.document();
        check(result.at("created").size() == 128 && scene.undo() && scene.document() == before &&
                  scene.redo() && scene.document() == after,
              "Mesh meadow did not use one ordinary Scene history entry");
        const auto effective = scene.effective_document();
        AffineTransform transform;
        for (const auto& entity : effective.at("entities"))
            if (entity.at("id") == surface)
                transform.m = entity.at("world_affine").get<std::array<double, 12>>();
        const auto inverse_surface = inverse(transform);
        for (const auto& placed : result.at("created"))
            for (const auto& entity : effective.at("entities"))
                if (entity.at("id") == placed) {
                    const auto& m = entity.at("world_affine");
                    const auto p = inverse_surface.point({m[3], m[7], m[11]});
                    check(std::abs(p[1] - .25 * p[0] - .125 * p[2]) < 1e-8,
                          "Saved Mesh meadow origin left its independent local ramp plane");
                }
    }
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    (void)catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
} // namespace forge::test_mesh_foliage_gallery
