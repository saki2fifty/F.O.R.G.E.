#include "model_instance_pose.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <cmath>
#include <forge/authoring.hpp>
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
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline, "Gallery state admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
void inspect_markers(World& world, const Json& source) {
    double duration = 0;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.animator")) {
            const auto id = world.scene.entity(row.at("id")).id();
            duration = std::max(duration,
                                world.animation->presentation(id, 1).at("duration").get<double>());
        }
    std::set<std::uint32_t> observed;
    for (unsigned i = 0; i <= unsigned(std::ceil(duration * 60)); ++i) {
        world.simulation.tick(1.f / 60);
        for (const auto& event : world.animation->events()) {
            require(event.name == "Pulse" && event.state >= 2 && event.state <= 3,
                    "Real-rig marker name/state differs");
            require(event.entity.scene == source.at("asset_id").get<AssetId>() && event.tick > 0 &&
                        event.offset >= 0 && event.offset <= 1. / 60 + 1e-6,
                    "Real-rig marker identity or fixed offset differs");
            require(event.value == (event.marker % 2 ? 1 : 0), "Real-rig marker payload differs");
            observed.insert(event.marker);
        }
    }
    require(observed == std::set<std::uint32_t>{1, 2, 3, 4}, "Real-rig marker coverage incomplete");
}
std::vector<Double3> vertices(const MeshData& data, const MeshInstancePose& pose) {
    std::vector<Double3> result;
    for (std::size_t p = 0; p < data.lods.front().parts.size(); ++p) {
        const auto& part = data.lods.front().parts[p];
        const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& joints = std::get<std::vector<std::uint32_t>>(part.find("JOINTS_0")->values);
        const auto& weights = std::get<std::vector<float>>(part.find("WEIGHTS_0")->values);
        const auto& skin = pose.lods.front()[p].skin;
        require(bool(skin), "Gallery character has no evaluated skin");
        for (std::size_t v = 0; v < part.vertices; ++v) {
            Double3 point{};
            double sum = 0;
            for (unsigned k = 0; k < 4; ++k)
                sum += weights[v * 4 + k];
            require(sum > 0, "Gallery vertex has no skin weight");
            for (unsigned k = 0; k < 4; ++k) {
                const auto& m = skin->palette.at(joints[v * 4 + k]);
                for (unsigned r = 0; r < 3; ++r) {
                    double value = m.m[r * 4 + 3];
                    for (unsigned c = 0; c < 3; ++c)
                        value += m.m[r * 4 + c] * positions[v * 3 + c];
                    point[r] += value * weights[v * 4 + k] / sum;
                }
            }
            result.push_back(point);
        }
    }
    return result;
}
} // namespace
void inspect_exits(World& world, const Json& source, const ModelSelection& selected) {
    constexpr float dt = 1.f / 60;
    struct Clock {
        std::uint32_t state;
        double time = 0;
        std::set<std::uint32_t> seen;
    };
    std::map<std::string, Clock> clocks;
    std::map<std::uint32_t, double> durations;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.animator")) {
            const auto id = row.at("id").get<std::string>();
            const auto state =
                row.at("components").at("forge.animator").at("state").get<std::uint32_t>();
            clocks.emplace(id, Clock{state, 0, {state}});
            durations[state] = world.animation->presentation(world.scene.entity(id).id(), 1)
                                   .at("duration")
                                   .get<double>();
        }
    require(clocks.size() == 3 && durations.size() == 3, "Timed Gallery initial states missing");
    const auto initial = extract_render_scene(world.simulation.presentation(1));
    std::map<EntityId, std::vector<Double3>> points;
    for (const auto& mesh : initial.meshes) {
        if (!selected.member_indices.contains(mesh.renderer.mesh.id))
            continue;
        const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
        const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
        points[mesh.entity] =
            vertices(resource.mesh, prepare_mesh_instance_pose(resource, geometry, mesh,
                                                               ModelSceneIndex(initial)));
    }
    std::set<std::uint32_t> markers;
    for (unsigned tick = 1; tick <= 600; ++tick) {
        world.simulation.tick(dt);
        for (auto& [id, clock] : clocks) {
            clock.time += dt;
            if (clock.time >= durations.at(clock.state)) {
                clock.state = clock.state % 3 + 1;
                clock.time = 0;
                clock.seen.insert(clock.state);
            }
            const auto actor = world.scene.entity(id);
            const auto reference = world.engine.world().reference(actor.id());
            const auto status = reference ? world.animation->playback(*reference) : std::nullopt;
            require(status && status->state == clock.state &&
                        actor.get<Animator>().state == clock.state,
                    "Gallery exit differs from independent fixed-tick clock/typed state");
            require(std::abs(status->time - clock.time) < 2e-5,
                    "Gallery incoming time consumed a residual tick");
        }
        for (const auto& event : world.animation->events()) {
            require(event.marker == event.state && event.phase == 0 && event.value == event.state,
                    "Timed Gallery entry marker lost originating state/payload");
            markers.insert(event.marker);
        }
        if (tick == 17) {
            const auto moved = extract_render_scene(world.simulation.presentation(1));
            unsigned deformed = 0;
            for (const auto& mesh : moved.meshes) {
                if (!points.contains(mesh.entity))
                    continue;
                const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
                const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
                const auto actual =
                    vertices(resource.mesh, prepare_mesh_instance_pose(resource, geometry, mesh,
                                                                       ModelSceneIndex(moved)));
                double displacement = 0;
                for (std::size_t v = 0; v < actual.size(); ++v)
                    for (unsigned c = 0; c < 3; ++c)
                        displacement = std::max(
                            displacement, std::abs(actual[v][c] - points.at(mesh.entity)[v][c]));
                deformed += displacement > .001;
            }
            require(deformed == 3, "Timed Gallery did not deform three actual character meshes");
        }
    }
    for (const auto& [id, clock] : clocks) {
        (void)id;
        require(clock.seen == std::set<std::uint32_t>{1, 2, 3}, "Gallery exit cycle incomplete");
    }
    require(markers == std::set<std::uint32_t>{1, 2, 3}, "Gallery automatic entry markers missing");
    require(world.animation->checkpoint_ready(), "Timed Gallery recovery unavailable");
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(root / "Scenes/10-animation-states.scene.json") >> source;
        const auto catalog = AssetCatalog::open_project(root);
        AssetId model, set;
        for (const auto& [id, r] : catalog.records()) {
            if (r.type == ModelAsset::type && r.source == "Assets/QuaterniusLocomotion.glb")
                model = id;
            if (r.type == AnimationStatesAsset::type && r.source == "Assets/Locomotion.states.json")
                set = id;
        }
        require(bool(model) && bool(set), "Real Gallery assets missing");
        const auto selected = load_model_selection(root, catalog, model);
        require(std::count_if(
                    selected.index.members.begin(), selected.index.members.end(),
                    [](const auto& m) { return m.identity.type == AnimationClipAsset::type; }) == 3,
                "Gallery source did not retain exactly three animations");
        World world(root, source);
        auto before = extract_render_scene(world.simulation.presentation(1));
        std::map<EntityId, std::vector<Double3>> initial;
        for (const auto& mesh : before.meshes) {
            const auto record = catalog.records().find(mesh.renderer.mesh.id);
            if (record == catalog.records().end() || !record->second.subasset ||
                record->second.subasset->owner != model)
                continue;
            const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
            const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
            const auto instance =
                prepare_mesh_instance_pose(resource, geometry, mesh, ModelSceneIndex(before));
            require(!before.cameras.empty(), "Gallery game camera missing");
            const auto view =
                camera_view(before.cameras.front().camera, before.cameras.front().world, 1280, 720);
            require(bounds_visible(mesh_instance_bounds(instance, view.position), view),
                    "Gallery character is outside the game camera");
            initial[mesh.entity] =
                vertices(resource.mesh, prepare_mesh_instance_pose(resource, geometry, mesh,
                                                                   ModelSceneIndex(before)));
        }
        require(initial.size() == 3, "Gallery must show three independently skinned characters");
        world.simulation.tick(.2f);
        const auto after = extract_render_scene(world.simulation.presentation(1));
        unsigned moving = 0;
        for (const auto& mesh : after.meshes)
            if (initial.contains(mesh.entity)) {
                const auto resource = model_mesh_resource(selected, mesh.renderer.mesh);
                const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
                const auto points =
                    vertices(resource.mesh, prepare_mesh_instance_pose(resource, geometry, mesh,
                                                                       ModelSceneIndex(after)));
                double displacement = 0;
                for (std::size_t v = 0; v < points.size(); ++v)
                    for (unsigned c = 0; c < 3; ++c)
                        displacement = std::max(
                            displacement, std::abs(points[v][c] - initial.at(mesh.entity)[v][c]));
                require(displacement > .001,
                        "Gallery clip did not deform actual character vertices");
                ++moving;
            }
        require(moving == 3, "Real character deformation coverage incomplete");
        // Fill the existing evaluated-joint budget with live state transitions,
        // then attempt a direct-clip admission. Both playback paths share this limit.
        Json state_config;
        Animator direct;
        std::size_t tracks = 0;
        for (const auto& row : source.at("entities"))
            if (row.at("components").contains("forge.animator")) {
                state_config = row.at("components").at("forge.animator");
                direct = world.scene.entity(row.at("id")).get<Animator>();
                tracks = world.animation->presentation(world.scene.entity(row.at("id")).id(), 1)
                             .at("local")
                             .size();
                break;
            }
        require(tracks > 0, "Gallery rig has no joints");
        direct.states = {};
        direct.state = 0;
        const auto layer_capacity = 8192 / tracks;
        const auto transitions = (layer_capacity - 1) / 2;
        const auto direct_count = layer_capacity - transitions * 2;
        require(tracks > 0 && transitions > 0 && transitions + direct_count + 1 <= 256,
                "Gallery rig cannot exercise the mixed joint budget");
        auto budget_source = source;
        budget_source["entities"] = Json::array();
        auto direct_config = state_config;
        direct_config["states"] = nullptr;
        direct_config["state"] = 0;
        std::vector<std::string> state_actors;
        for (std::size_t i = 0; i < transitions + direct_count; ++i) {
            const auto id = AssetId::generate().str();
            const auto is_state = i < transitions;
            auto config = is_state ? state_config : direct_config;
            config["state"] = is_state ? 1 : 0;
            budget_source["entities"].push_back({{"id", id},
                                                 {"name", "Budget player"},
                                                 {"spatial", {{"mode", "world"}}},
                                                 {"components", {{"forge.animator", config}}}});
            if (is_state)
                state_actors.push_back(id);
        }
        const auto overflow = AssetId::generate().str();
        budget_source["entities"].push_back({{"id", overflow},
                                             {"name", "Overflow player"},
                                             {"spatial", {{"mode", "world"}}},
                                             {"components", Json::object()}});
        World budget(root, budget_source);
        for (const auto& id : state_actors) {
            auto actor = budget.scene.entity(id);
            auto config = actor.get<Animator>();
            config.state = 2;
            actor.set(config);
        }
        budget.simulation.tick(.01f);
        require(budget.animation->checkpoint_ready(), "Within-budget transitions failed");
        const auto transition_checkpoint = budget.animation->checkpoint();
        auto overflow_actor = budget.scene.entity(overflow);
        overflow_actor.set(direct);
        budget.simulation.tick(.01f);
        require(budget.animation->presentation(overflow_actor.id(), 1).is_null(),
                "Direct-clip admission ignored state transition joint cost");
        require(!budget.animation->checkpoint_ready(), "Over-budget admission lacked a failure");
        overflow_actor.remove<Animator>();
        budget.simulation.tick(.5f);
        overflow_actor.set(direct);
        budget.simulation.tick(.01f);
        require(!budget.animation->presentation(overflow_actor.id(), 1).is_null() &&
                    budget.animation->checkpoint_ready(),
                "Direct clip did not admit after transition layers were released");
        // Restoring old fades while the newly admitted direct player remains would
        // exceed the same aggregate budget. Reject before changing any live clocks.
        const auto admitted_checkpoint = budget.animation->checkpoint();
        auto oversized_recovery = admitted_checkpoint;
        for (auto& entry : oversized_recovery["entries"])
            for (const auto& previous : transition_checkpoint.at("entries"))
                if (entry.at("entity") == previous.at("entity"))
                    entry = previous;
        bool rejected_budget = false;
        try {
            budget.animation->restore(oversized_recovery);
        } catch (const std::exception& error) {
            rejected_budget = std::string_view(error.what()).find("evaluated-joint budget") !=
                              std::string_view::npos;
        }
        require(rejected_budget && budget.animation->checkpoint() == admitted_checkpoint,
                "Over-budget recovery mutated admitted playback or failed for another reason");

        auto lease = std::make_shared<ProjectLease>(root);
        Json marker_source;
        std::ifstream(root / "Scenes/11-animation-markers.scene.json") >> marker_source;
        World marker_world(root, marker_source);
        inspect_markers(marker_world, marker_source);
        Json exit_source;
        std::ifstream(root / "Scenes/30-animation-exits.scene.json") >> exit_source;
        World exit_world(root, exit_source);
        inspect_exits(exit_world, exit_source, selected);
        const std::array roots{source.at("asset_id").get<AssetId>(),
                               marker_source.at("asset_id").get<AssetId>(),
                               exit_source.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, world.scene.schema());
        const auto output = root.parent_path() / ("export-" + AssetId::generate().str());
        package_runtime_content(root, output, roots, {"windows", "d3d12"}, {}, {},
                                world.scene.schema());
        const auto shipped = open_runtime_content(output, {"windows", "d3d12"});
        require(shipped.records().contains(set), "Real exported state set missing");
        for (unsigned i = 0; i < 3; ++i)
            require(shipped.records().contains(
                        selected.bindings.at("/animations/" + std::to_string(i))),
                    "Real export omitted a state clip");
        require(!std::filesystem::exists(output / "Assets/QuaterniusLocomotion.glb"),
                "Export copied source GLB");
        std::filesystem::rename(root / "Assets", root / "Assets-offline");
        const auto relocated = root.parent_path() / ("relocated-" + AssetId::generate().str());
        std::filesystem::rename(output, relocated);
        World exported(relocated, source);
        exported.simulation.tick(.2f);
        for (const auto& row : source.at("entities"))
            if (row.at("components").contains("forge.animator")) {
                const auto actor = exported.scene.entity(row.at("id"));
                auto config = actor.get<Animator>();
                config.state = 2;
                actor.set(config);
            }
        exported.simulation.tick(.1f);
        require(exported.animation->checkpoint_ready(),
                "Relocated character state transition failed");
        World exported_markers(relocated, marker_source);
        inspect_markers(exported_markers, marker_source);
        World exported_exits(relocated, exit_source);
        const auto shipped_selection = load_model_selection(relocated, shipped, model);
        inspect_exits(exported_exits, exit_source, shipped_selection);
        require(!std::filesystem::exists(relocated / "Assets/TimedLocomotion.states.json"),
                "Automatic state source shipped");
        require(!std::filesystem::exists(relocated / "Assets/MarkerPulse.states.json"),
                "Marker source shipped");
        std::cout << "Three Quaternius clips deform independent characters; all state dependencies "
                     "export and play after source removal/relocation\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
