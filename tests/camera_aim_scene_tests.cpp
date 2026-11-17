#include <algorithm>
#include <cctype>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Double3 actual, Double3 expected) {
    for (unsigned i = 0; i < 3; ++i)
        check(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) < 1e-6,
              "Camera Aim snapshot direction/position differs from its oracle");
}
Json& row(Json& source, const std::string& id) {
    auto& rows = source.at("entities");
    const auto at = std::find_if(rows.begin(), rows.end(), [&](const auto& value) {
        return value.at("id").template get<EntityId>() == EntityId::parse(id);
    });
    check(at != rows.end(), "Camera Aim fixture row is missing");
    return *at;
}
const RenderCamera& camera(const RenderScene& scene, const std::string& id) {
    const auto at =
        std::find_if(scene.cameras.begin(), scene.cameras.end(),
                     [&](const auto& value) { return value.entity == EntityId::parse(id); });
    check(at != scene.cameras.end(), "Camera Aim view was omitted");
    return *at;
}
struct Fixture {
    EngineContext engine;
    Scene scene{engine.world()};
    std::string view, other, target, child;
    Fixture() {
        const auto create = [&](const char* recipe) {
            return authoring_command(scene, "entity.create", {{"recipe", recipe}})
                .at("selected")
                .get<std::string>();
        };
        view = create("render.camera");
        other = create("render.camera");
        target = create("empty");
        child = create("empty");
        scene.entity(view).set<LocalTranslation>({0, 0, 0}).set<LocalRotation>({});
        scene.entity(other).set<LocalTranslation>({0, 0, 10}).set<LocalRotation>({});
        scene.entity(target).set<LocalTranslation>({10, 0, 0});
        scene.entity(target).set<LocalRotation>(rotation_from_euler({0, 0, 90}));
        scene.entity(target).set<LocalScale>({2, 3, 4});
        scene.entity(child).set<LocalTranslation>({0, 0, 1});
        scene.reparent_entity(child, view, ReparentMode::KeepLocal);
        MeshRenderer mesh;
        mesh.mesh = engine_primitive(0);
        scene.entity(view).set(mesh);
        scene.entity(child).set(mesh);
        Light light;
        light.kind = unsigned(LightKind::Point);
        scene.entity(view).set(light);
        CameraAim aim;
        aim.target = {scene.asset_id(), EntityId::parse(target)};
        aim.offset_x = 1;
        scene.entity(view).set(aim);
    }
};
#include "camera_aim_authoring_tests.hpp"
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Camera Aim scene scratch root");
        Fixture f;
        const auto authored = f.scene.document();
        const auto revision = f.scene.revision();
        auto source = f.scene.effective_document();
        auto result = extract_render_scene(source);
        check(result.cameras.size() == 2 && result.diagnostics.empty(),
              "Camera Aim omitted an independent camera or emitted unexpected diagnostics");
        const auto& aimed = camera(result, f.view);
        near(camera_view(aimed.camera, aimed.world, 800, 600).forward,
             {10 / std::sqrt(104.), 2 / std::sqrt(104.), 0});
        near(aimed.world.point({0, 0, 0}), {0, 0, 0});
        for (const auto& [id, position] :
             {std::pair{f.view, Double3{0, 0, 0}}, std::pair{f.child, Double3{0, 0, 1}}}) {
            const auto at =
                std::find_if(result.meshes.begin(), result.meshes.end(), [&](const auto& value) {
                    return value.entity == EntityId::parse(id);
                });
            check(at != result.meshes.end(), "Camera Aim omitted an attached mesh");
            near(at->world.point({0, 0, 0}), position);
        }
        check(result.lights.size() == 1, "Camera Aim omitted an attached light");
        near(result.lights[0].light.position, {0, 0, 0});
        check(f.scene.document() == authored && f.scene.revision() == revision,
              "Camera Aim extraction changed source or authored history");

        // Resolve a target that occurs after the camera, without changing identity bytes.
        const auto fixed = EntityId::parse("abcdefab-cdef-4abc-8def-abcdefabcdef");
        auto target_row = row(source, f.target);
        source["entities"].erase(
            std::find_if(source["entities"].begin(), source["entities"].end(),
                         [&](const auto& value) { return value.at("id") == f.target; }));
        std::string upper = fixed.str();
        for (auto& value : upper)
            value = char(std::toupper(static_cast<unsigned char>(value)));
        target_row["id"] = fixed;
        source["entities"].push_back(target_row);
        row(source, f.view)["components"]["forge.camera_aim"]["target"]["entity"] = fixed;
        result = extract_render_scene(source);
        near(camera_view(camera(result, f.view).camera, camera(result, f.view).world, 800, 600)
                 .forward,
             {10 / std::sqrt(104.), 2 / std::sqrt(104.), 0});
        check(result.diagnostics.empty() && source["entities"].back()["id"] == fixed.str(),
              "Camera Aim row indexing changed identity bytes or failed a forward reference");
        auto bad_identity = source;
        bad_identity["entities"].back()["id"] = upper;
        bool identity_rejected = false;
        try {
            (void)extract_render_scene(bad_identity);
        } catch (const std::exception& error) {
            identity_rejected =
                std::string(error.what()).find("canonical lowercase") != std::string::npos;
        }
        check(identity_rejected, "Camera Aim changed the canonical lowercase identity contract");

        const auto invalid = [&](Json document) {
            const auto value = extract_render_scene(document);
            check(value.cameras.size() == 1 && value.cameras[0].entity == EntityId::parse(f.other),
                  "Invalid Camera Aim affected an independent camera");
            check(std::any_of(value.diagnostics.begin(), value.diagnostics.end(),
                              [&](const auto& d) {
                                  return d.category == "render.camera_aim.invalid" &&
                                         d.context.entity == EntityId::parse(f.view) &&
                                         d.context.asset == f.scene.asset_id() &&
                                         d.context.property == "forge.camera_aim";
                              }),
                  "Invalid Camera Aim lacked a contextual diagnostic");
            check(value.meshes.size() == result.meshes.size() && value.lights.size() == 1,
                  "Invalid Camera Aim removed another render producer");
        };
        auto missing = source;
        row(missing, f.view)["components"]["forge.camera_aim"]["target"]["entity"] =
            EntityId::generate();
        invalid(missing);
        auto noncanonical_reference = source;
        row(noncanonical_reference, f.view)["components"]["forge.camera_aim"]["target"]["entity"] =
            upper;
        invalid(noncanonical_reference);
        auto foreign = source;
        row(foreign, f.view)["components"]["forge.camera_aim"]["target"]["scene"] =
            AssetId::generate();
        invalid(foreign);
        auto unresolved = source;
        row(unresolved, fixed.str())["spatial_resolved"] = false;
        invalid(unresolved);
        auto prefab = source;
        row(prefab, fixed.str())["prefab"] = true;
        // Prefab declarations also correctly omit their own legacy renderer, if any.
        const auto prefab_result = extract_render_scene(prefab);
        check(prefab_result.cameras.size() == 1 && !prefab_result.diagnostics.empty(),
              "Camera Aim accepted a Prefab declaration as a target");
        auto coincident = source;
        auto& aim = row(coincident, f.view)["components"]["forge.camera_aim"];
        aim["offset_x"] = 0;
        for (unsigned i : {3u, 7u, 11u})
            row(coincident, fixed.str())["world_affine"][i] = 0.;
        invalid(coincident);
        for (const auto mode : {0, 1, 2}) {
            auto inactive = missing;
            auto& value = row(inactive, f.view)["components"]["forge.camera_aim"];
            if (mode == 0)
                value["enabled"] = false;
            else if (mode == 1)
                value["weight"] = 0.;
            else
                value["target"] = nullptr;
            const auto base = extract_render_scene(inactive);
            check(base.cameras.size() == 2 && base.diagnostics.empty(),
                  "Inactive Camera Aim attempted target resolution");
            near(camera_view(camera(base, f.view).camera, camera(base, f.view).world, 800, 600)
                     .forward,
                 {0, 0, 1});
        }
        // Mutual targets use only the original node poses, without recursive views.
        auto mutual = f.scene.effective_document();
        auto& first_aim = row(mutual, f.view)["components"]["forge.camera_aim"];
        first_aim["target"]["entity"] = EntityId::parse(f.other);
        first_aim["offset_x"] = 0.;
        row(mutual, f.other)["components"]["forge.camera_aim"] = {
            {"target", EntityRef{f.scene.asset_id(), EntityId::parse(f.view)}}};
        const auto paired = extract_render_scene(mutual);
        check(paired.diagnostics.empty(), "Mutual camera targets formed a shadow dependency cycle");
        near(camera_view(camera(paired, f.view).camera, camera(paired, f.view).world, 800, 600)
                 .forward,
             {0, 0, 1});
        near(camera_view(camera(paired, f.other).camera, camera(paired, f.other).world, 800, 600)
                 .forward,
             {0, 0, -1});
        authoring_and_instances(f, std::filesystem::absolute(argv[1]) / AssetId::generate().str());
        std::cout
            << "Camera Aim history/Prefab/reopen, typed/forward targets, local offsets, producer "
               "isolation and scoped failure PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
