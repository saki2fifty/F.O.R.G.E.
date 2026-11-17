#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Double3 actual, Double3 expected) {
    for (unsigned index = 0; index < 3; ++index)
        check(std::isfinite(actual[index]) && std::abs(actual[index] - expected[index]) < 1e-6,
              "Camera Follow differs from its independent coordinate oracle");
}
Json& row(Json& source, const std::string& id) {
    for (auto& value : source.at("entities"))
        if (value.at("id") == id)
            return value;
    throw std::runtime_error("Camera Follow fixture row is missing");
}
const RenderCamera& view(const RenderScene& scene, const std::string& id) {
    const auto at =
        std::find_if(scene.cameras.begin(), scene.cameras.end(),
                     [&](const auto& value) { return value.entity == EntityId::parse(id); });
    check(at != scene.cameras.end(), "Camera Follow view is missing");
    return *at;
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto create = [&](const char* recipe) {
            return authoring_command(scene, "entity.create", {{"recipe", recipe}})
                .at("selected")
                .get<std::string>();
        };
        const auto camera = create("render.camera"), other = create("render.camera"),
                   target = create("empty"), child = create("empty");
        scene.entity(camera)
            .set<LocalTranslation>({1, 2, -10})
            .set<LocalRotation>({})
            .set<LocalScale>({2, 3, 4});
        scene.entity(target)
            .set<LocalTranslation>({10, 0, 0})
            .set<LocalRotation>(rotation_from_euler({0, 0, 90}))
            .set<LocalScale>({2, 3, 4});
        scene.entity(child).set<LocalTranslation>({0, 0, 1});
        scene.reparent_entity(child, camera, ReparentMode::KeepLocal);
        MeshRenderer mesh;
        mesh.mesh = engine_primitive(0);
        scene.entity(camera).set(mesh);
        scene.entity(child).set(mesh);
        CameraFollow follow;
        follow.target = {scene.asset_id(), EntityId::parse(target)};
        follow.offset_x = 1;
        follow.offset_y = 2;
        follow.offset_z = 3;
        for (const auto basis : {ViewBasis::ForgePositiveZ, ViewBasis::GltfNegativeZ}) {
            auto settings = scene.entity(camera).get<Camera>();
            settings.basis = unsigned(basis);
            scene.entity(camera).set(settings);
            for (const auto space : {CameraFollowSpace::World, CameraFollowSpace::Target})
                for (const double weight : {0., .25, .5, 1.}) {
                    follow.offset_space = unsigned(space);
                    follow.weight = weight;
                    scene.entity(camera).set(follow);
                    const auto source = scene.document();
                    const auto render = extract_render_scene(scene.effective_document());
                    check(render.diagnostics.empty() && render.cameras.size() == 2,
                          "Camera Follow affected an independent Camera");
                    const auto& camera_view_data = view(render, camera);
                    const auto prepared = camera_view(settings, camera_view_data.world, 800, 600);
                    const Double3 desired =
                        space == CameraFollowSpace::World ? Double3{11, 2, 3} : Double3{4, 2, 12};
                    near(prepared.position,
                         {1 + (desired[0] - 1) * weight, 2, -10 + (desired[2] + 10) * weight});
                    near(prepared.forward, {0, 0, basis == ViewBasis::ForgePositiveZ ? 1. : -1.});
                    check(source == scene.document(), "Camera Follow extraction changed source");
                    for (const auto& [id, expected] : {std::pair{camera, Double3{1, 2, -10}},
                                                       std::pair{child, Double3{1, 2, -6}}}) {
                        const auto at = std::find_if(
                            render.meshes.begin(), render.meshes.end(),
                            [&](const auto& value) { return value.entity == EntityId::parse(id); });
                        check(at != render.meshes.end(), "Camera Follow omitted an attached Mesh");
                        near(at->world.point({0, 0, 0}), expected);
                    }
                }
            scene.entity(camera).set<CameraAim>({true, follow.target});
            const auto render = extract_render_scene(scene.effective_document());
            const auto& composed = view(render, camera);
            const auto direction = camera_view(composed.camera, composed.world, 800, 600).forward;
            const auto distance = std::sqrt(184.);
            near(direction, {6 / distance, -2 / distance, -12 / distance});
            scene.entity(camera).remove<CameraAim>();
        }
        const auto source = scene.effective_document();
        const auto invalid = [&](Json candidate,
                                 const char* category = "render.camera_follow.invalid") {
            const auto render = extract_render_scene(candidate);
            check(render.cameras.size() == 1 && render.cameras[0].entity == EntityId::parse(other),
                  "Invalid Camera Follow removed an independent Camera");
            check(std::any_of(render.diagnostics.begin(), render.diagnostics.end(),
                              [&](const auto& d) {
                                  return d.category == category &&
                                         d.context.entity == EntityId::parse(camera);
                              }),
                  "Invalid Camera Follow lost its scoped diagnostic");
            check(render.meshes.size() == 2, "Invalid Camera Follow removed attached Meshes");
        };
        auto missing = source;
        row(missing, camera)["components"]["forge.camera_follow"]["target"]["entity"] =
            EntityId::generate();
        invalid(missing);
        for (const auto* field : {"enabled", "weight", "target"}) {
            auto inactive = missing;
            auto& values = row(inactive, camera)["components"]["forge.camera_follow"];
            if (std::string_view(field) == "enabled")
                values[field] = false;
            else if (std::string_view(field) == "weight")
                values[field] = 0;
            else
                values[field] = nullptr;
            const auto render = extract_render_scene(inactive);
            check(render.cameras.size() == 2 && render.diagnostics.empty(),
                  "Inactive Camera Follow looked up its target");
            near(view(render, camera).world.point({0, 0, 0}), {1, 2, -10});
        }
        for (const auto& [field, value] :
             {std::pair{"offset_space", 2.}, std::pair{"offset_space", .5},
              std::pair{"weight", 1.01}}) {
            auto bad = source;
            row(bad, camera)["components"]["forge.camera_follow"][field] = value;
            invalid(bad);
        }
        auto wrapped = source;
        row(wrapped, camera)["components"]["forge.camera_follow"]["offset_space"] = std::uint64_t(1)
                                                                                    << 32;
        invalid(wrapped);
        auto self = source;
        auto& tracking = row(self, camera)["components"]["forge.camera_follow"];
        tracking["target"]["entity"] = camera;
        tracking["offset_space"] = 0u;
        near(view(extract_render_scene(self), camera).world.point({0, 0, 0}), {2, 4, -7});
        auto mutual = source;
        auto& primary = row(mutual, camera)["components"]["forge.camera_follow"];
        primary["target"]["entity"] = other;
        primary["offset_space"] = 0u;
        primary["offset_x"] = primary["offset_y"] = primary["offset_z"] = 0.;
        auto secondary = primary;
        secondary["target"]["entity"] = camera;
        row(mutual, other)["components"]["forge.camera_follow"] = secondary;
        const auto mutual_views = extract_render_scene(mutual);
        const auto& other_pose = row(mutual, other).at("world_affine");
        near(view(mutual_views, camera).world.point({0, 0, 0}),
             {other_pose[3].get<double>(), other_pose[7].get<double>(),
              other_pose[11].get<double>()});
        near(view(mutual_views, other).world.point({0, 0, 0}), {1, 2, -10});
        auto coincident = source;
        auto& at_target = row(coincident, camera)["components"]["forge.camera_follow"];
        at_target["offset_space"] = 0u;
        at_target["offset_x"] = at_target["offset_y"] = at_target["offset_z"] = 0.;
        near(view(extract_render_scene(coincident), camera).world.point({0, 0, 0}), {10, 0, 0});
        row(coincident, camera)["components"]["forge.camera_aim"] = {
            {"enabled", true}, {"target", at_target.at("target")},
            {"weight", 1},     {"offset_x", 0},
            {"offset_y", 0},   {"offset_z", 0}};
        invalid(coincident, "render.camera_aim.invalid");
        auto overflow = source;
        auto& too_far = row(overflow, camera)["components"]["forge.camera_follow"];
        too_far["offset_space"] = 0u;
        too_far["offset_x"] = std::numeric_limits<double>::max();
        row(overflow, target)["world_affine"][3] = std::numeric_limits<double>::max();
        invalid(overflow);
        auto foreign = source;
        row(foreign, camera)["components"]["forge.camera_follow"]["target"]["scene"] =
            AssetId::generate();
        invalid(foreign);
        auto unresolved = source;
        row(unresolved, target)["spatial_resolved"] = false;
        invalid(unresolved);
        auto reflected = source;
        row(reflected, camera)["world_affine"][0] = -2;
        invalid(reflected, "render.camera.invalid");
        std::cout << "Camera Follow weighted world/target offsets, both bases, Aim composition, "
                     "node isolation and scoped failures passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
