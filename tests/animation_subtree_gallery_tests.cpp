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
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .004, "Gallery subtree skin geometry differs");
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
                    "Gallery subtree admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
using Points = std::map<unsigned, std::vector<Double3>>;
Points points(World& world, const ModelSelection& selected,
              const std::map<EntityId, unsigned>& choices) {
    const auto scene = extract_render_scene(world.simulation.presentation(1));
    const ModelSceneIndex index(scene);
    Points result;
    for (const auto& mesh : scene.meshes) {
        const auto* node = index.entity(mesh.entity);
        if (!node || !choices.contains(node->root))
            continue;
        const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
        const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
        const auto instance = prepare_mesh_instance_pose(resource, geometry, mesh, index);
        require(!scene.cameras.empty(), "Gallery subtree has no camera");
        const auto view = camera_view(scene.cameras[0].camera, scene.cameras[0].world, 1280, 720);
        require(bounds_visible(mesh_instance_bounds(instance, view.position), view),
                "Gallery subtree is outside game camera");
        std::vector<Double3> values;
        for (std::size_t p = 0; p < resource.mesh.lods[0].parts.size(); ++p) {
            const auto& part = resource.mesh.lods[0].parts[p];
            const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
            const auto& joints =
                std::get<std::vector<std::uint32_t>>(part.find("JOINTS_0")->values);
            const auto& weights = std::get<std::vector<float>>(part.find("WEIGHTS_0")->values);
            const auto& skin = instance.lods[0][p].skin;
            require(bool(skin) && skin->palette.size() == 2,
                    "Gallery subtree lost its compact two-joint draw palette");
            for (std::size_t v = 0; v < part.vertices; ++v) {
                Double3 value{};
                for (unsigned k = 0; k < 4; ++k) {
                    const auto point =
                        skin->palette.at(joints[v * 4 + k])
                            .point({positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]});
                    for (unsigned c = 0; c < 3; ++c)
                        value[c] += weights[v * 4 + k] * point[c];
                }
                values.push_back(value);
            }
        }
        require(result.emplace(choices.at(node->root), std::move(values)).second,
                "Duplicate Gallery subtree geometry");
    }
    require(result.size() == 3, "Gallery subtree did not render its three models");
    return result;
}
void compare(World& world, const ModelSelection& selected,
             const std::map<EntityId, unsigned>& choices) {
    const auto rest = points(world, selected, choices);
    world.simulation.tick(.5f);
    const auto sampled = points(world, selected, choices);
    const auto c = std::cos(3.141592653589793 / 12), s = std::sin(3.141592653589793 / 12);
    bool changed = false;
    unsigned bottom = 0;
    for (std::size_t i = 0; i < sampled.at(1).size(); ++i) {
        const auto& upper = sampled.at(1)[i];
        const auto& full = sampled.at(2)[i];
        // Same upper pose, with independently specified full-clip root X=.5 and Y=15deg.
        near(full[0], 4 + .5 + c * upper[0] + s * upper[2]);
        near(full[1], upper[1]);
        near(full[2], -s * upper[0] + c * upper[2]);
        if (std::abs(rest.at(1)[i][1]) < .001) {
            ++bottom;
            near(upper[0] - sampled.at(0)[i][0], 4);
            near(upper[1], sampled.at(0)[i][1]);
            near(upper[2], sampled.at(0)[i][2]);
        } else if (std::abs(upper[0] - sampled.at(0)[i][0] - 4) > .05 ||
                   std::abs(upper[1] - sampled.at(0)[i][1]) > .05)
            changed = true;
    }
    require(bottom > 0 && changed,
            "Gallery subtree failed to show base lower skin and changed upper skin");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch");
        const auto project = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(project);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(project / "Scenes/37-animation-subtree-blending.scene.json") >> source;
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, r] : catalog.records())
            if (r.type == ModelAsset::type && r.source == "Assets/MaskColumn.gltf")
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        std::map<EntityId, unsigned> choices;
        for (const auto& row : source["entities"])
            if (row["components"].contains("forge.animator")) {
                const auto& config = row["components"]["forge.animator"];
                const unsigned choice = config["blend_weight"] == 0       ? 0
                                        : !config["blend_root"].is_null() ? 1
                                                                          : 2;
                choices.emplace(row["id"].get<EntityId>(), choice);
            }
        World world(project, source);
        compare(world, selected, choices);
        auto lease = std::make_shared<ProjectLease>(project);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, world.scene.schema());
        const auto package =
            project.parent_path() / ("subtree-package-" + AssetId::generate().str());
        package_runtime_content(project, package, roots, {"windows", "d3d12"}, {}, {},
                                world.scene.schema());
        std::filesystem::rename(project / "Assets", project / "Assets-offline");
        const auto relocated =
            project.parent_path() / ("subtree-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        const auto shipped_model = load_model_selection(relocated, shipped, model);
        World exported(relocated, source);
        compare(exported, shipped_model, choices);
        require(!std::filesystem::exists(relocated / "Assets/MaskColumn.gltf"),
                "Subtree export shipped source model");
        std::cout << "Gallery37 actual four-joint skin, independent full-root equation, "
                     "lower/upper comparison, camera and relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
