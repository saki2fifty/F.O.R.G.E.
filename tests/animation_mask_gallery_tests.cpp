#include "model_instance_pose.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
const char* phase = "initial pose";
void begin_phase(const char* value) {
    phase = value;
    std::cerr << "Checking Gallery mask " << phase << '\n';
}
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .004,
            std::string(phase) + ": Gallery weighted mask numeric mismatch: " + std::to_string(a) +
                " vs " + std::to_string(b));
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
            require(std::chrono::steady_clock::now() < deadline, "Gallery mask admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
using Points = std::map<std::string, std::vector<Double3>>;
Points geometry(World& world, const ModelSelection& selected, bool check = true,
                Json* observed = nullptr) {
    const auto source = world.scene.snapshot();
    std::map<EntityId, std::string> actors;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.animator")) {
            const auto id = row.at("id").get<EntityId>();
            const auto name = row.at("name").get<std::string>();
            actors.emplace(id, name);
            if (check) {
                const auto pose =
                    world.animation->presentation(world.scene.entity(id.str()).id(), 1);
                const auto root = selected.bindings.at("/nodes/0");
                const auto& ids = pose.at("joint_assets");
                const auto found = std::find(ids.begin(), ids.end(), Json(root));
                require(found != ids.end(), "Gallery mask root identity absent");
                const auto index = std::size_t(found - ids.begin());
                near(pose.at("local")[index].at("translation")[0],
                     name == "Whole skeleton blend"  ? 1
                     : name == "Upper subtree blend" ? 0
                                                     : .25);
                near(pose.at("morphs")[0].at("weights")[0], name == "Whole skeleton blend"  ? 1.5
                                                            : name == "Upper subtree blend" ? .5
                                                                                            : .75);
            }
        }
    const auto snapshot = world.simulation.presentation(1);
    if (observed)
        *observed = snapshot;
    const auto render = extract_render_scene(snapshot);
    require(render.cameras.size() == 1, "Gallery mask game camera absent");
    const auto view = camera_view(render.cameras[0].camera, render.cameras[0].world, 1280, 720);
    const ModelSceneIndex index(render);
    Points result;
    for (const auto& mesh : render.meshes) {
        const auto* node = index.entity(mesh.entity);
        if (!node || !actors.contains(node->root))
            continue;
        const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
        const auto prepared = prepare_mesh_pose_geometry(resource.mesh);
        const auto pose = prepare_mesh_instance_pose(resource, prepared, mesh, index);
        require(bounds_visible(mesh_instance_bounds(pose, view.position), view),
                "Gallery mask geometry is outside the game camera");
        std::vector<Double3> points;
        for (std::size_t p = 0; p < resource.mesh.lods[0].parts.size(); ++p) {
            const auto& part = resource.mesh.lods[0].parts[p];
            const auto& skin = pose.lods[0][p].skin;
            require(bool(skin), "Gallery mask native skin palette absent");
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
                for (unsigned lane = 0; lane < 4; ++lane) {
                    const auto skinned = skin->palette.at(joints[v * 4 + lane]).point(local);
                    for (unsigned c = 0; c < 3; ++c)
                        point[c] += skinned[c] * weights[v * 4 + lane];
                }
                // The admitted skin palette already contains joint world transforms.
                // Match renderer submission: applying mesh.world again would move
                // and rotate a skinned vertex twice.
                points.push_back(point);
            }
        }
        require(result.emplace(actors.at(node->root), std::move(points)).second,
                "Duplicate Gallery mask holder geometry");
    }
    require(result.size() == 3, "Gallery mask lost a comparison holder");
    return result;
}
void same(const Points& a, const Points& b) {
    require(a.size() == b.size(), "Mask exported geometry count differs");
    for (const auto& [id, values] : a) {
        require(b.contains(id) && b.at(id).size() == values.size(),
                "Mask exported geometry shape differs");
        for (std::size_t v = 0; v < values.size(); ++v)
            for (unsigned c = 0; c < 3; ++c)
                near(values[v][c], b.at(id)[v][c]);
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery project and scratch root");
        const auto project = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(project);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(project / "Scenes/42-animation-bone-masks.scene.json") >> source;
        const auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type && record.source == "Assets/OverlayColumn.gltf")
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        World world(project, source);
        const auto rest = geometry(world, selected, false);
        world.simulation.tick(1);
        const auto expected = geometry(world, selected);
        for (const auto& [name, points] : expected) {
            bool changed = false;
            for (std::size_t i = 0; i < points.size(); ++i)
                for (unsigned c = 0; c < 3; ++c)
                    changed |= std::abs(points[i][c] - rest.at(name)[i][c]) > .05;
            require(changed, "Gallery mask comparison has no visible deformation");
        }
        flecs::entity weighted_actor;
        for (const auto& row : source.at("entities"))
            if (row.at("name") == "Weighted bone mask")
                weighted_actor = world.scene.entity(row.at("id").get<std::string>());
        require(bool(weighted_actor), "Gallery weighted actor absent");
        const auto saved_config = weighted_actor.get<Animator>();
        auto config = saved_config;
        config.blend_mask = {};
        weighted_actor.set(config);
        begin_phase("unmasked geometry");
        Json paused_frame;
        const auto unmasked = geometry(world, selected, false, &paused_frame);
        Json full_pose;
        EntityId full_actor;
        for (const auto& row : source.at("entities"))
            if (row.at("name") == "Whole skeleton blend") {
                full_actor = row.at("id").get<EntityId>();
                full_pose = world.animation->presentation(
                    world.scene.entity(row.at("id").get<std::string>()).id(), 1);
            }
        const auto unmasked_pose = world.animation->presentation(weighted_actor.id(), 1);
        require(full_pose.at("local") == unmasked_pose.at("local") &&
                    full_pose.at("morphs") == unmasked_pose.at("morphs"),
                "Paused mask removal differs from whole-skeleton local/morph samples");
        const auto frame = world.simulation.presentation(1);
        require(paused_frame == frame,
                "First paused mask presentation differs from a repeated read");
        std::map<AssetId, Json> whole_nodes, changed_nodes;
        const auto rendered = extract_render_scene(frame);
        const ModelSceneIndex rendered_index(rendered);
        for (const auto& row : frame.at("entities")) {
            const auto id = row.at("id").get<EntityId>();
            const auto* node = rendered_index.entity(id);
            if (!node || !node->source.node.id)
                continue;
            if (node->root == full_actor)
                whole_nodes.emplace(node->source.node.id, row);
            if (world.scene.entity(node->root.str()).id() == weighted_actor.id())
                changed_nodes.emplace(node->source.node.id, row);
        }
        require(whole_nodes.size() == full_pose.at("joint_assets").size() &&
                    changed_nodes.size() == whole_nodes.size(),
                "Paused renderer check lost native joint rows");
        for (const auto& [id, row] : whole_nodes) {
            const auto& changed_row = changed_nodes.at(id);
            for (const char* field :
                 {"forge.local_translation", "forge.local_rotation", "forge.local_scale"})
                require(row.at("components").at(field) == changed_row.at("components").at(field),
                        "Paused mask node's written channel differs: " + id.str() + " " + field);
            const auto& a = row.at("world_affine");
            const auto& b = changed_row.at("world_affine");
            for (std::size_t c = 0; c < a.size(); ++c)
                near(b[c], a[c].get<double>() + (c == 3 ? 8 : 0));
        }
        const auto& full = unmasked.at("Whole skeleton blend");
        const auto& changed = unmasked.at("Weighted bone mask");
        require(full.size() == changed.size(), "Paused mask edit lost renderer geometry");
        for (std::size_t v = 0; v < full.size(); ++v)
            for (unsigned c = 0; c < 3; ++c)
                near(changed[v][c], full[v][c] + (c == 0 ? 8 : 0));
        weighted_actor.set(saved_config);
        begin_phase("restored geometry");
        const auto restore_deadline = std::chrono::steady_clock::now() + 10s;
        do {
            (void)world.simulation.presentation(1);
            require(std::chrono::steady_clock::now() < restore_deadline,
                    "Paused Gallery mask restoration timeout");
            std::this_thread::sleep_for(1ms);
        } while (!world.animation->checkpoint_ready());
        same(expected, geometry(world, selected));
        begin_phase("exported geometry");
        auto lease = std::make_shared<ProjectLease>(project);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, world.scene.schema());
        const auto package = project.parent_path() / ("mask-package-" + AssetId::generate().str());
        package_runtime_content(project, package, roots, {"windows", "d3d12"}, {}, {},
                                world.scene.schema());
        std::filesystem::rename(project / "Assets", project / "Assets-offline");
        const auto relocated =
            project.parent_path() / ("mask-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        const auto shipped_model = load_model_selection(relocated, shipped, model);
        require(!std::filesystem::exists(relocated / "Assets/Weighted.mask.json") &&
                    !std::filesystem::exists(relocated / "Assets/OverlayColumn.gltf"),
                "Gallery export shipped authored mask/model source");
        World exported(relocated, source);
        exported.simulation.tick(1);
        same(expected, geometry(exported, shipped_model));
        std::cout << "Gallery42 weighted native skin/morph geometry, bounds, camera and relocated "
                     "source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
