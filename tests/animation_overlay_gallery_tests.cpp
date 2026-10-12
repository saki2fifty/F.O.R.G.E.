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
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .005, "Gallery overlay geometry differs");
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
                    "Gallery overlay admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
std::map<EntityId, unsigned> actors(const Json& source, bool named) {
    std::map<EntityId, unsigned> result;
    for (const auto& r : source["entities"])
        if (r["components"].contains("forge.animator")) {
            const auto& c = r["components"]["forge.animator"];
            const unsigned choice = named                       ? c.at("state").get<unsigned>() - 1
                                    : c.at("blend_weight") == 0 ? 0
                                    : c.at("blend_mode") == 0   ? 1
                                                                : 2;
            require(result.emplace(r["id"].get<EntityId>(), choice).second,
                    "Duplicate Gallery actor");
        }
    require(result.size() == 3, "Gallery overlay needs three actors");
    return result;
}
using Deformations = std::map<unsigned, std::vector<Double3>>;
Deformations geometry(World& world, const ModelSelection& model,
                      const std::map<EntityId, unsigned>& choices, bool named) {
    const auto scene = extract_render_scene(world.simulation.presentation(1));
    const ModelSceneIndex index(scene);
    Deformations output;
    require(scene.cameras.size() == 1, "Gallery overlay needs a game camera");
    const auto view = camera_view(scene.cameras[0].camera, scene.cameras[0].world, 1280, 720);
    for (const auto& mesh : scene.meshes) {
        const auto* node = index.entity(mesh.entity);
        if (!node || !choices.contains(node->root))
            continue;
        const auto choice = choices.at(node->root);
        const auto resource = model_mesh_resource(model, mesh.renderer.mesh);
        const auto prepared = prepare_mesh_pose_geometry(resource.mesh);
        const auto pose = prepare_mesh_instance_pose(resource, prepared, mesh, index);
        const auto bounds = mesh_instance_bounds(pose, view.position);
        require(bounds_visible(bounds, view), "Gallery overlay outside game camera");
        require(pose.morph_weights.size() == 2, "Gallery overlay lost its two morph targets");
        const std::array<std::array<double, 2>, 3> weights =
            named ? std::array<std::array<double, 2>, 3>{{{.3, -.1}, {.75, .1}, {.25, 0}}}
                  : std::array<std::array<double, 2>, 3>{{{.5, -.5}, {1.5, .25}, {1.75, -.25}}};
        near(pose.morph_weights[0], weights[choice][0]);
        near(pose.morph_weights[1], weights[choice][1]);
        std::vector<Double3> points;
        for (std::size_t pi = 0; pi < resource.mesh.lods[0].parts.size(); ++pi) {
            const auto& part = resource.mesh.lods[0].parts[pi];
            const auto& skin = pose.lods[0][pi].skin;
            require(skin && skin->palette.size() == 2 && part.morph_targets.size() == 2,
                    "Gallery overlay skin/targets differ");
            const auto& base = std::get<std::vector<float>>(part.find("POSITION")->values);
            const auto& joints =
                std::get<std::vector<std::uint32_t>>(part.find("JOINTS_0")->values);
            const auto& influences = std::get<std::vector<float>>(part.find("WEIGHTS_0")->values);
            const auto& wide = std::get<std::vector<float>>(part.morph_targets[0][0].values);
            const auto& lift = std::get<std::vector<float>>(part.morph_targets[1][0].values);
            for (std::size_t v = 0; v < part.vertices; ++v) {
                Double3 local{base[v * 3], base[v * 3 + 1], base[v * 3 + 2]};
                for (unsigned c = 0; c < 3; ++c)
                    local[c] += double(wide[v * 3 + c]) * pose.morph_weights[0] +
                                double(lift[v * 3 + c]) * pose.morph_weights[1];
                Double3 point{};
                for (unsigned k = 0; k < 4; ++k) {
                    const auto p = skin->palette.at(joints[v * 4 + k]).point(local);
                    for (unsigned c = 0; c < 3; ++c)
                        point[c] += influences[v * 4 + k] * p[c];
                }
                for (unsigned c = 0; c < 3; ++c)
                    require(point[c] >= bounds.minimum[c] - .005 &&
                                point[c] <= bounds.maximum[c] + .005,
                            "Morphed skin escaped renderer bounds");
                if (!named) {
                    // Independent original-source equations: morph first, then rest-relative bone
                    // skin.
                    const bool top = base[v * 3 + 1] > .5;
                    const Double3 original{base[v * 3] * (1 + (top ? .6 * weights[choice][0] : 0)),
                                           base[v * 3 + 1] + (top ? .6 * weights[choice][1] : 0),
                                           base[v * 3 + 2]};
                    const double angle = (choice == 0   ? 10
                                          : choice == 1 ? 75
                                                        : 85) *
                                         3.141592653589793 / 180;
                    const double yaw = (choice == 0 ? 0 : 30) * 3.141592653589793 / 180;
                    Double3 expected{};
                    for (unsigned k = 0; k < 4; ++k) {
                        auto p = original;
                        require(part.joint_palette.at(joints[v * 4 + k]) <= 1,
                                "Gallery source palette order changed");
                        if (part.joint_palette.at(joints[v * 4 + k]) == 1) {
                            const double x = p[0], y = p[1] - 1;
                            p[0] =
                                std::cos(angle) * x - std::sin(angle) * y + (choice == 0 ? 0 : .35);
                            p[1] = std::sin(angle) * x + std::cos(angle) * y + 1;
                        }
                        const double x = p[0], z = p[2];
                        p[0] = std::cos(yaw) * x + std::sin(yaw) * z + (choice == 0 ? 0 : 1) +
                               double(choice) * 4 - 4;
                        p[2] = -std::sin(yaw) * x + std::cos(yaw) * z + (choice == 1 ? 0 : 1);
                        for (unsigned c = 0; c < 3; ++c)
                            expected[c] += influences[v * 4 + k] * p[c];
                    }
                    for (unsigned c = 0; c < 3; ++c)
                        near(point[c], expected[c]);
                }
                points.push_back(point);
            }
        }
        require(output.emplace(choice, std::move(points)).second, "Duplicate Gallery geometry");
    }
    require(output.size() == 3, "Gallery overlay did not render all three actors");
    return output;
}
void same(const Deformations& a, const Deformations& b) {
    require(a.size() == b.size(), "Export geometry count differs");
    for (const auto& [id, points] : a) {
        require(points.size() == b.at(id).size(), "Export vertex count differs");
        for (std::size_t v = 0; v < points.size(); ++v)
            for (unsigned c = 0; c < 3; ++c)
                near(points[v][c], b.at(id)[v][c]);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch");
        const auto project = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(project);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        Json direct, named;
        std::ifstream(project / "Scenes/38-animation-additive-overlays.scene.json") >> direct;
        std::ifstream(project / "Scenes/39-animation-morph-states.scene.json") >> named;
        const auto catalog = AssetCatalog::open_project(project);
        AssetId id;
        for (const auto& [asset, r] : catalog.records())
            if (r.type == ModelAsset::type && r.source == "Assets/OverlayColumn.gltf")
                id = asset;
        const auto selected = load_model_selection(project, catalog, id);
        const auto dc = actors(direct, false), nc = actors(named, true);
        World a(project, direct), b(project, named);
        a.simulation.tick(1);
        b.simulation.tick(.2f);
        const auto expected = geometry(a, selected, dc, false),
                   named_expected = geometry(b, selected, nc, true);
        // The ordinary automatic exits keep morph states running without a fixture script.
        std::map<EntityId, std::set<unsigned>> visited;
        for (unsigned tick = 0; tick < 20; ++tick) {
            b.simulation.tick(.25f);
            for (const auto& [actor, choice] : nc) {
                (void)choice;
                const auto status = b.animation->playback(b.scene.reference(actor.str()));
                require(bool(status), "Morph state playback disappeared");
                visited[actor].insert(status->state);
            }
        }
        for (const auto& [actor, states] : visited) {
            (void)actor;
            require(states.size() == 3, "Gallery morph exits did not visit all states");
        }
        auto lease = std::make_shared<ProjectLease>(project);
        const std::array roots{direct.at("asset_id").get<AssetId>(),
                               named.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, a.scene.schema());
        const auto package =
            project.parent_path() / ("overlay-package-" + AssetId::generate().str());
        package_runtime_content(project, package, roots, {"windows", "d3d12"}, {}, {},
                                a.scene.schema());
        std::filesystem::rename(project / "Assets", project / "Assets-offline");
        const auto relocated =
            project.parent_path() / ("overlay-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        const auto shipped_model = load_model_selection(relocated, shipped, id);
        World ea(relocated, direct), eb(relocated, named);
        ea.simulation.tick(1);
        eb.simulation.tick(.2f);
        same(expected, geometry(ea, shipped_model, dc, false));
        same(named_expected, geometry(eb, shipped_model, nc, true));
        require(!std::filesystem::exists(relocated / "Assets/OverlayColumn.gltf"),
                "Overlay export shipped source model");
        std::cout << "Gallery38/39 actual morph+skin geometry, bounds, cameras, automatic exits "
                     "and relocated source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
