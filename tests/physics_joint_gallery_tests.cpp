#include "model_instance_pose.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/game_session.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<PhysicsRuntime> physics;
    World(const std::filesystem::path& root, const Json& source)
        : engine{WorldRole::Runtime, false, {physics_module({}, root)}}, scene{engine.world()},
          simulation{engine.world(), scene},
          physics{std::static_pointer_cast<PhysicsRuntime>(engine.services().physics())} {
        scene.restore_snapshot(source);
        physics->synchronize(0);
        simulation.reset_presentation();
    }
    flecs::entity named(const std::string& name) {
        const auto document = scene.document();
        for (const auto& row : document.at("entities"))
            if (row.at("name") == name)
                return scene.entity(row.at("id"));
        throw std::runtime_error("Missing Gallery actor: " + name);
    }
    Double3 point(const std::string& name, Double3 local = {}) {
        engine.world().evaluate_world_transforms();
        return named(name).get<WorldTransform>().affine.point(local);
    }
};
double distance(Double3 a, Double3 b) {
    double sum = 0;
    for (unsigned i = 0; i < 3; ++i)
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(sum);
}
void render_actors(World& w, const std::vector<std::string>& names, std::size_t mesh_count = 5) {
    const auto render = extract_render_scene(w.simulation.presentation(1));
    require(render.cameras.size() == 1 && render.meshes.size() == mesh_count &&
                !render.lights.empty(),
            "Gallery render consumer missing actors/camera/light");
    const auto& camera = render.cameras.front();
    const auto view = camera_view(camera.camera, camera.world, 1280, 720);
    for (const auto& mesh : render.meshes) {
        bool selected = false;
        for (const auto& name : names)
            selected |= mesh.entity == w.engine.world().reference(w.named(name))->entity;
        if (!selected)
            continue;
        ResourcePool<MeshAsset> pool;
        const auto ticket = request_model_mesh(pool, {}, nullptr, mesh.renderer.mesh);
        require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                "Builtin Gallery mesh failed real resource admission");
        const auto loaded = pool.acquire(ticket);
        const auto geometry = prepare_mesh_pose_geometry(loaded->mesh);
        const auto pose =
            prepare_mesh_instance_pose(loaded.get(), geometry, mesh, ModelSceneIndex(render));
        require(bounds_visible(mesh_instance_bounds(pose, view.position), view),
                "Joint Gallery actor outside camera");
    }
}
void motion(World& w) {
    const auto before = w.point("Distance bob");
    const auto hinge_before = w.point("Hinge pendulum");
    require(w.physics->status().at("joints") == 2, "Gallery joints absent");
    for (unsigned i = 0; i < 120; ++i) {
        w.simulation.tick(1.f / 60);
        require(std::abs(distance(w.point("Distance anchor"), w.point("Distance bob")) - 2) < .04,
                "Gallery tether drift");
        require(distance(w.point("Hinge anchor"), w.point("Hinge pendulum", {0, .5, 0})) < .04,
                "Gallery hinge pivot drift");
        const auto q = w.named("Hinge pendulum").get<LocalRotation>();
        require(std::abs(2 * std::atan2(q.z, q.w)) < .9, "Gallery hinge limit drift");
        require(std::abs(q.x) + std::abs(q.y) < .002, "Gallery hinge off-axis rotation");
    }
    require(distance(before, w.point("Distance bob")) > .1 &&
                distance(hinge_before, w.point("Hinge pendulum")) > .1,
            "Gallery bodies did not move");
    render_actors(w, {"Distance bob", "Hinge pendulum"});
}
void point_fixed_motion(World& w) {
    const auto before = w.point("Point pendulum"), upper = w.point("Fixed upper");
    const auto vector = [&](const char* name, Double3 local) {
        const auto o = w.point(name), p = w.point(name, local);
        return Double3{p[0] - o[0], p[1] - o[1], p[2] - o[2]};
    };
    require(w.physics->status().at("joints") == 2, "Point/fixed Gallery joints absent");
    for (unsigned i = 0; i < 120; ++i) {
        w.simulation.tick(1.f / 60);
        require(distance(w.point("Point anchor"), w.point("Point pendulum", {0, .5, 0})) < .04,
                "Point Gallery scaled pivot drift");
        require(distance(w.point("Fixed upper", {0, -1, 0}), w.point("Fixed lower", {0, .625, 0})) <
                    .04,
                "Fixed Gallery scaled anchor drift");
        const auto a = vector("Fixed upper", {2, 0, 0}), b = vector("Fixed lower", {2, 0, 0});
        require(distance(a, b) < .01, "Fixed Gallery frame diverged");
        if (i < 10) {
            const auto q = w.named("Point pendulum").get<LocalRotation>();
            require(std::abs(q.x) > .1 && std::abs(q.z) > .1, "Point Gallery locked free rotation");
        }
    }
    require(distance(before, w.point("Point pendulum")) > .1 &&
                upper[1] - w.point("Fixed upper")[1] > 1,
            "Point/fixed Gallery actors did not move");
    render_actors(w, {"Point pendulum", "Fixed upper", "Fixed lower"});
}
void continuous_motion(World& w) {
    const std::string continuous = "Continuous - fast falling cube",
                      discrete = "Discrete - tunneling comparison";
    require(w.named(continuous).get<PhysicsBody>().continuous_collision &&
                !w.named(discrete).get<PhysicsBody>().continuous_collision,
            "Gallery CCD choices not admitted");
    for (unsigned tick = 0; tick < 240; ++tick)
        w.simulation.tick(1.f / 60);
    const auto stopped = w.point(continuous), tunneled = w.point(discrete);
    const auto support = w.point("Continuous thin platform", {0, .5, 0});
    require(std::abs(stopped[1] - support[1] - .1) < .025 && tunneled[1] < -2 && tunneled[1] > -2.3,
            "Gallery thin-platform CCD comparison differs: " + Json(stopped).dump() + ", " +
                Json(tunneled).dump());
    render_actors(w, {continuous, discrete});
}
void tuned_motion(World& w) {
    const std::vector<std::string> names{
        "No damping - free fall",  "Damping 3 per second", "Speed limit 2 metres per second",
        "Zero linear speed limit", "Sleeping permitted",   "Sleeping disabled"};
    // Before contact: independent native integration equations, not just order.
    const double dt = double(1.f / 60);
    double free_velocity = 0, damped_velocity = 0, capped_velocity = 0;
    double free_y = 12, damped_y = 12, capped_y = 12;
    for (unsigned tick = 0; tick < 60; ++tick) {
        free_velocity -= 9.81 * dt;
        damped_velocity = (damped_velocity - 9.81 * dt) * (1. - 3 * dt);
        capped_velocity = std::max(-2., capped_velocity - 9.81 * dt);
        free_y += free_velocity * dt;
        damped_y += damped_velocity * dt;
        capped_y += capped_velocity * dt;
        w.simulation.tick(float(dt));
        require(std::abs(w.point(names[0])[1] - free_y) < 2e-5 &&
                    std::abs(w.point(names[1])[1] - damped_y) < 2e-5 &&
                    std::abs(w.point(names[2])[1] - capped_y) < 2e-5 && w.point(names[3])[1] == 12,
                "Gallery damping/speed/zero choices failed independent fall equations");
    }
    render_actors(w, names, 7);
    for (unsigned tick = 60; tick < 600; ++tick)
        w.simulation.tick(float(dt));
    require(w.physics->status().at("active_dynamic") == 1 &&
                w.physics->status().at("sleeping_dynamic") == 5 &&
                std::abs(w.point(names[4])[1] - .5) < .02 &&
                std::abs(w.point(names[5])[1] - .5) < .02,
            "Gallery sleeping comparison did not preserve stationary active/settled bodies");
    render_actors(w, names, 7);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "Need Gallery and scratch roots, optional extended mode");
        const std::string mode = argc == 4 ? argv[3] : "joints";
        require(mode == "joints" || mode == "extended" || mode == "ccd" || mode == "tuning",
                "Unknown Gallery mode");
        const auto validate = [&](World& world) {
            if (mode == "tuning")
                tuned_motion(world);
            else if (mode == "ccd")
                continuous_motion(world);
            else if (mode == "extended")
                point_fixed_motion(world);
            else
                motion(world);
        };
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source =
            read(root / (mode == "tuning"     ? "Scenes/23-body-motion-tuning.scene.json"
                         : mode == "ccd"      ? "Scenes/22-continuous-collision.scene.json"
                         : mode == "extended" ? "Scenes/21-point-fixed-joints.scene.json"
                                              : "Scenes/20-physics-joints.scene.json"));
        World live(root, source);
        validate(live);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {});
            const auto out = root.parent_path() / (root.filename().string() + "-export");
            (void)package_runtime_content(root, out, roots, {"windows", "d3d12"});
            const auto moved = std::filesystem::path(out.string() + "-relocated");
            std::filesystem::rename(out, moved);
            const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
            require(shipped.records().size() == 1 && shipped.records().contains(roots[0]),
                    "Joint scene included unrelated project dependencies");
            require(!std::filesystem::exists(moved / "Scenes") &&
                        !std::filesystem::exists(moved / "Assets"),
                    "Joint export included editable project sources");
            auto game_source = load_game_scene(moved, {roots[0]});
            require(game_source.at("_prefab_sources").empty(), "Unexpected prefab closure");
            game_source.erase("_prefab_sources");
            require(game_source == source, "Packaged scene joint values/identities changed");
        }
        std::filesystem::rename(root, root.string() + "-source-offline");
        const auto moved = root.parent_path() / (root.filename().string() + "-export-relocated");
        const auto game_source = load_game_scene(moved, {roots[0]});
        World game(moved, game_source);
        validate(game);
        std::cout << "Original Gallery joint motion/limits/scaled pivots/render camera and "
                     "relocated source-free content export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
