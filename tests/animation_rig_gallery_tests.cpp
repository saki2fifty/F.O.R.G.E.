#include "model_instance_pose.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <cmath>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .005, "Gallery IK value differs");
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& project, const Json& source)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline, "Gallery IK admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
using Geometry = std::map<float, std::vector<Double3>>;
Geometry geometry(World& world, const ModelSelection& model, bool aim) {
    std::map<EntityId, float> actors;
    const auto source = world.scene.snapshot();
    for (const auto& row : source["entities"]) {
        const auto& c = row["components"];
        if (!c.contains("forge.animator"))
            continue;
        const auto actor = row["id"].get<EntityId>();
        const auto& constraint = c.at(aim ? "forge.aim_ik" : "forge.two_bone_ik");
        const auto weight = constraint.at("weight").get<float>();
        actors.emplace(actor, weight);
        const auto pose = world.animation->presentation(world.scene.entity(actor.str()).id(), 1);
        const auto joint = constraint.at(aim ? "joint" : "end");
        const auto& ids = pose.at("joint_assets");
        const auto found = std::find(ids.begin(), ids.end(), joint);
        require(found != ids.end(), "Gallery IK durable node absent");
        const auto index = std::size_t(found - ids.begin());
        if (weight == 1) {
            if (aim) {
                const auto& q = pose["local"][index]["rotation"];
                near(q[2], -std::sin(.392699081698724));
                near(q[3], std::cos(.392699081698724));
            } else {
                const auto& m = pose["model"][index];
                near(m[12], .8);
                near(m[13], 1.1);
                near(m[14], 0);
            }
        }
    }
    const auto render = extract_render_scene(world.simulation.presentation(1));
    require(render.cameras.size() == 1, "Gallery IK camera absent");
    const auto camera = camera_view(render.cameras[0].camera, render.cameras[0].world, 1280, 720);
    const ModelSceneIndex index(render);
    Geometry result;
    for (const auto& mesh : render.meshes) {
        const auto* node = index.entity(mesh.entity);
        if (!node || !actors.contains(node->root))
            continue;
        const auto resource = model_mesh_resource(model, mesh.renderer.mesh);
        const auto prepared = prepare_mesh_pose_geometry(resource.mesh);
        const auto pose = prepare_mesh_instance_pose(resource, prepared, mesh, index);
        const auto bounds = mesh_instance_bounds(pose, camera.position);
        require(bounds_visible(bounds, camera), "Gallery IK geometry outside camera");
        std::vector<Double3> points;
        for (std::size_t pi = 0; pi < resource.mesh.lods[0].parts.size(); ++pi) {
            const auto& part = resource.mesh.lods[0].parts[pi];
            const auto& skin = pose.lods[0][pi].skin;
            require(bool(skin), "Gallery IK skin absent");
            const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
            const auto& joints =
                std::get<std::vector<std::uint32_t>>(part.find("JOINTS_0")->values);
            const auto& weights = std::get<std::vector<float>>(part.find("WEIGHTS_0")->values);
            for (std::size_t v = 0; v < part.vertices; ++v) {
                Double3 local{positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]};
                for (std::size_t target = 0; target < part.morph_targets.size(); ++target) {
                    const auto& delta =
                        std::get<std::vector<float>>(part.morph_targets[target][0].values);
                    for (unsigned c = 0; c < 3; ++c)
                        local[c] += delta[v * 3 + c] * pose.morph_weights.at(target);
                }
                Double3 point{};
                for (unsigned k = 0; k < 4; ++k) {
                    const auto p = skin->palette.at(joints[v * 4 + k]).point(local);
                    for (unsigned c = 0; c < 3; ++c)
                        point[c] += weights[v * 4 + k] * p[c];
                }
                for (unsigned c = 0; c < 3; ++c)
                    require(point[c] >= bounds.minimum[c] - .005 &&
                                point[c] <= bounds.maximum[c] + .005,
                            "IK skin escaped renderer bounds");
                points.push_back(point);
            }
        }
        require(result.emplace(actors.at(node->root), std::move(points)).second,
                "Duplicate Gallery IK actor");
    }
    require(result.size() == 3, "Gallery IK did not render three weights");
    // Remove the known holder translations and require distinct deformations.
    bool differs = false;
    for (std::size_t i = 0; i < result.at(0).size(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            differs = differs ||
                      std::abs(result.at(0)[i][c] + (c == 0 ? 8 : 0) - result.at(1)[i][c]) > .01;
    require(differs, "Gallery IK did not change visible geometry");
    return result;
}
void same(const Geometry& a, const Geometry& b) {
    require(a.size() == b.size(), "Export IK actor count differs");
    for (const auto& [weight, points] : a) {
        require(points.size() == b.at(weight).size(), "Export IK vertex count differs");
        for (std::size_t v = 0; v < points.size(); ++v)
            for (unsigned c = 0; c < 3; ++c)
                near(points[v][c], b.at(weight)[v][c]);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch");
        const auto project = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(project);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        Json reach, aim;
        std::ifstream(project / "Scenes/40-two-bone-ik.scene.json") >> reach;
        std::ifstream(project / "Scenes/41-joint-aim.scene.json") >> aim;
        const auto catalog = AssetCatalog::open_project(project);
        AssetId id;
        for (const auto& [asset, r] : catalog.records())
            if (r.type == ModelAsset::type && r.source == "Assets/OverlayColumn.gltf")
                id = asset;
        const auto selected = load_model_selection(project, catalog, id);
        World a(project, reach), b(project, aim);
        a.simulation.tick(.1f);
        b.simulation.tick(.1f);
        const auto expected = geometry(a, selected, false),
                   aim_expected = geometry(b, selected, true);
        auto lease = std::make_shared<ProjectLease>(project);
        const std::array roots{reach.at("asset_id").get<AssetId>(),
                               aim.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, a.scene.schema());
        const auto package = project.parent_path() / ("rig-package-" + AssetId::generate().str());
        package_runtime_content(project, package, roots, {"windows", "d3d12"}, {}, {},
                                a.scene.schema());
        std::filesystem::rename(project / "Assets", project / "Assets-offline");
        const auto relocated =
            project.parent_path() / ("rig-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        const auto shipped_model = load_model_selection(relocated, shipped, id);
        World ea(relocated, reach), eb(relocated, aim);
        ea.simulation.tick(.1f);
        eb.simulation.tick(.1f);
        same(expected, geometry(ea, shipped_model, false));
        same(aim_expected, geometry(eb, shipped_model, true));
        require(!std::filesystem::exists(relocated / "Assets/OverlayColumn.gltf"),
                "IK export shipped source model");
        std::cout << "Gallery40/41 actual weighted IK skin geometry, bounds, cameras and relocated "
                     "source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
