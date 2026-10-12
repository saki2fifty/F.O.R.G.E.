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
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    require(std::isfinite(actual) && std::abs(actual - expected) < .004,
            "Gallery root-motion geometry differs from expected world movement");
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
            require(std::chrono::steady_clock::now() < deadline,
                    "Gallery root-motion admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
std::vector<Double3> vertices(const MeshData& geometry, const MeshInstancePose& instance) {
    std::vector<Double3> result;
    for (std::size_t p = 0; p < geometry.lods[0].parts.size(); ++p) {
        const auto& part = geometry.lods[0].parts[p];
        const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& joints = std::get<std::vector<std::uint32_t>>(part.find("JOINTS_0")->values);
        const auto& weights = std::get<std::vector<float>>(part.find("WEIGHTS_0")->values);
        const auto& skin = instance.lods[0][p].skin;
        require(bool(skin), "Root Gallery lost actual skin deformation");
        for (std::size_t v = 0; v < part.vertices; ++v) {
            Double3 value{};
            for (unsigned k = 0; k < 4; ++k) {
                const auto& matrix = skin->palette.at(joints[v * 4 + k]);
                const auto point =
                    matrix.point({positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]});
                for (unsigned c = 0; c < 3; ++c)
                    value[c] += weights[v * 4 + k] * point[c];
            }
            result.push_back(value);
        }
    }
    return result;
}
using Points = std::map<std::uint32_t, std::vector<Double3>>;
Points points(World& world, const ModelSelection& selected,
              const std::map<EntityId, std::uint32_t>& modes) {
    const auto scene = extract_render_scene(world.simulation.presentation(1));
    const ModelSceneIndex index(scene);
    Points result;
    for (const auto& mesh : scene.meshes) {
        const auto* node = index.entity(mesh.entity);
        if (!node || !modes.contains(node->root))
            continue;
        const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
        const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
        const auto instance = prepare_mesh_instance_pose(resource, geometry, mesh, index);
        require(!scene.cameras.empty(), "Root Gallery has no game camera");
        const auto view = camera_view(scene.cameras[0].camera, scene.cameras[0].world, 1280, 720);
        require(bounds_visible(mesh_instance_bounds(instance, view.position), view),
                "Root Gallery comparison is outside its game camera");
        require(result.emplace(modes.at(node->root), vertices(resource.mesh, instance)).second,
                "Root Gallery has duplicate mode geometry");
    }
    require(result.size() == 3, "Root Gallery did not render three actual skinned models");
    return result;
}
void compare(World& world, const ModelSelection& model,
             const std::map<EntityId, std::uint32_t>& modes) {
    const auto before = points(world, model, modes);
    for (unsigned i = 0; i < 15; ++i)
        world.simulation.tick(1.f / 60);
    const auto moved = points(world, model, modes);
    for (std::size_t v = 0; v < moved.at(0).size(); ++v) {
        // Off's skeleton and Apply's object produce identical geometry apart
        // from the authored four-meter comparison spacing: no double movement.
        near(moved.at(1)[v][0] - moved.at(0)[v][0], 4);
        near(moved.at(1)[v][1] - moved.at(0)[v][1], 0);
        near(moved.at(1)[v][2] - moved.at(0)[v][2], 0);
        for (unsigned c = 0; c < 3; ++c)
            near(moved.at(2)[v][c], before.at(2)[v][c]);
    }
    for (unsigned i = 15; i < 240; ++i)
        world.simulation.tick(1.f / 60);
    const auto looped = points(world, model, modes);
    for (std::size_t v = 0; v < looped.at(1).size(); ++v)
        for (unsigned c = 0; c < 3; ++c)
            near(looped.at(1)[v][c], before.at(1)[v][c]);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto project = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(project);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(project / "Scenes/36-animation-root-motion.scene.json") >> source;
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type && record.source == "Assets/LocomotionColumn.gltf")
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        std::map<EntityId, std::uint32_t> modes;
        for (const auto& row : source.at("entities"))
            if (row.at("components").contains("forge.animator"))
                modes.emplace(row.at("id").get<EntityId>(),
                              row.at("components").at("forge.animator").at("root_motion"));
        World world(project, source);
        compare(world, selected, modes);
        // Package the real scene closure, then make all authoring source unavailable.
        auto lease = std::make_shared<ProjectLease>(project);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, world.scene.schema());
        const auto package =
            project.parent_path() / ("root-gallery-package-" + AssetId::generate().str());
        package_runtime_content(project, package, roots, {"windows", "d3d12"}, {}, {},
                                world.scene.schema());
        std::filesystem::rename(project / "Assets", project / "Assets-offline");
        const auto relocated =
            project.parent_path() / ("root-gallery-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        const auto exported_model = load_model_selection(relocated, shipped, model);
        World exported(relocated, source);
        compare(exported, exported_model, modes);
        require(!std::filesystem::exists(relocated / "Assets/LocomotionColumn.gltf"),
                "Root Gallery shipped editable source");
        std::cout << "Gallery36 actual skinned vertices, no double motion, four-loop square, "
                     "camera visibility and relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
