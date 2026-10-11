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
    const auto render = extract_render_scene(w.simulation.presentation(1));
    require(render.cameras.size() == 1 && render.meshes.size() == 5 && !render.lights.empty(),
            "Gallery render consumer missing actors/camera/light");
    const auto& camera = render.cameras.front();
    const auto view = camera_view(camera.camera, camera.world, 1280, 720);
    for (const auto& mesh : render.meshes) {
        if (mesh.entity != w.engine.world().reference(w.named("Distance bob"))->entity &&
            mesh.entity != w.engine.world().reference(w.named("Hinge pendulum"))->entity)
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
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch roots");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / "Scenes/20-physics-joints.scene.json");
        World live(root, source);
        motion(live);
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
        motion(game);
        std::cout << "Original Gallery joint motion/limits/scaled pivots/render camera and "
                     "relocated source-free content export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
