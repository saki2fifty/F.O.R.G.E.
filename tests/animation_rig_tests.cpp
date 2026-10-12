#include "animation_preview.hpp"
#include "animation_rig_binding.hpp"
#include "animation_states_authoring.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_placement.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace forge::animation_detail;
using namespace std::chrono_literals;
namespace {
const char* stage = "initial reach";
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = .003) {
    require(std::isfinite(a) && std::abs(a - b) < tolerance,
            std::string(stage) + ": Rig value differs: " + std::to_string(a) + " vs " +
                std::to_string(b));
}
template <class T>
void offsets(flecs::world world,
             std::initializer_list<std::pair<const char*, std::size_t>> fields) {
    for (const auto& [name, offset] : fields) {
        const auto* member = ecs_struct_get_member(world.c_ptr(), world.id<T>(), name);
        require(member && member->offset == offset,
                std::string("IK native/C++ field layout differs: ") + name);
    }
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    World(const std::filesystem::path& project, const Json& source, EntityId id,
          bool prepare = true)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())),
          actor(id) {
        scene.restore_snapshot(source);
        if (!prepare)
            return;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline, "Rig admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
    flecs::entity entity() { return scene.entity(actor.str()); }
    Json pose() { return animation->presentation(entity().id(), 1); }
    void update(bool valid = true) {
        animation->synchronize();
        try {
            (void)animation->prepare_initial_pose();
        } catch (const std::exception&) {
            if (valid)
                throw;
        }
    }
};
std::size_t joint(const Json& pose, AssetId id) {
    for (std::size_t i = 0; i < pose.at("joint_assets").size(); ++i)
        if (pose["joint_assets"][i] == Json(id))
            return i;
    throw std::runtime_error("Rig joint absent");
}
Double3 position(const Json& pose, AssetId id) {
    const auto& m = pose.at("model").at(joint(pose, id));
    return {m[12], m[13], m[14]};
}
Json locals(const Scene& scene) {
    Json result = Json::object();
    const auto snapshot = scene.snapshot();
    for (const auto& row : snapshot.at("entities")) {
        const auto& c = row.at("components");
        result[row.at("id").get<std::string>()] = {
            {"translation", c.value("forge.local_translation", Json())},
            {"rotation", c.value("forge.local_rotation", Json())},
            {"scale", c.value("forge.local_scale", Json())}};
    }
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 5 || argc == 6,
                "Need scratch, worker, converter and original OverlayColumn");
        const auto project = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(project / "Assets");
        std::filesystem::copy_file(argv[4], project / "Assets/OverlayColumn.gltf");
        const auto converter = std::filesystem::absolute(argv[3]);
        auto importer = model_importer(std::filesystem::absolute(argv[2]), converter);
        if (std::string_view(argv[2]) == "--direct")
            importer = std::make_shared<forge::test::DirectModelImporter>(importer, converter);
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(importer);
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(project);
        AssetImportService service(lease, registry, {"linux", "none", "cpu"}, 1);
        auto draft = service.prepare("Assets/OverlayColumn.gltf");
        service.submit(
            draft,
            [](auto& c, const auto& p, const auto& catalog) {
                prepare_model_publication(c, p, catalog);
            },
            [](const auto&, const auto&) {});
        require(service.wait_idle(60s), "Rig import timeout");
        const auto receipts = service.poll();
        require(receipts.size() == 1 && receipts[0].published,
                receipts.empty() ? "Missing rig receipt" : receipts[0].diagnostic);
        const auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type)
                model = id;
        const auto selection = load_model_selection(project, catalog, model);
        const auto root = selection.bindings.at("/nodes/0"),
                   middle = selection.bindings.at("/nodes/1"),
                   end = selection.bindings.at("/nodes/4"),
                   side = selection.bindings.at("/nodes/3");
        EngineContext author;
        offsets<TwoBoneIK>(author.world().world(),
                           {{"start", offsetof(TwoBoneIK, start)},
                            {"middle", offsetof(TwoBoneIK, middle)},
                            {"end", offsetof(TwoBoneIK, end)},
                            {"enabled", offsetof(TwoBoneIK, enabled)},
                            {"weight", offsetof(TwoBoneIK, weight)},
                            {"soften", offsetof(TwoBoneIK, soften)},
                            {"twist_degrees", offsetof(TwoBoneIK, twist_degrees)},
                            {"target_x", offsetof(TwoBoneIK, target_x)},
                            {"target_y", offsetof(TwoBoneIK, target_y)},
                            {"target_z", offsetof(TwoBoneIK, target_z)},
                            {"pole_x", offsetof(TwoBoneIK, pole_x)},
                            {"pole_y", offsetof(TwoBoneIK, pole_y)},
                            {"pole_z", offsetof(TwoBoneIK, pole_z)},
                            {"axis_x", offsetof(TwoBoneIK, axis_x)},
                            {"axis_y", offsetof(TwoBoneIK, axis_y)},
                            {"axis_z", offsetof(TwoBoneIK, axis_z)}});
        offsets<AimIK>(author.world().world(), {{"joint", offsetof(AimIK, joint)},
                                                {"enabled", offsetof(AimIK, enabled)},
                                                {"weight", offsetof(AimIK, weight)},
                                                {"twist_degrees", offsetof(AimIK, twist_degrees)},
                                                {"target_x", offsetof(AimIK, target_x)},
                                                {"target_y", offsetof(AimIK, target_y)},
                                                {"target_z", offsetof(AimIK, target_z)},
                                                {"pole_x", offsetof(AimIK, pole_x)},
                                                {"pole_y", offsetof(AimIK, pole_y)},
                                                {"pole_z", offsetof(AimIK, pole_z)},
                                                {"forward_x", offsetof(AimIK, forward_x)},
                                                {"forward_y", offsetof(AimIK, forward_y)},
                                                {"forward_z", offsetof(AimIK, forward_z)},
                                                {"up_x", offsetof(AimIK, up_x)},
                                                {"up_y", offsetof(AimIK, up_y)},
                                                {"up_z", offsetof(AimIK, up_z)}});
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.clip = {selection.bindings.at("/animations/2")};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selection, scene.asset_id(), scene.revision(), options));
        TwoBoneIK reach;
        reach.start = {root};
        reach.middle = {middle};
        reach.end = {end};
        reach.enabled = true;
        scene.entity(actor.str()).set(reach);
        const auto source = scene.snapshot();
        if (argc == 6) {
            require(std::string_view(argv[5]) == "--prepare", "Unknown rig fixture mode");
            atomic_write(project / "main.scene.json", source.dump(2));
            std::cout << project.string() << '\n';
            return 0;
        }
        World full(project, source, actor);
        for (unsigned c = 0; c < 3; ++c)
            near(position(full.pose(), end)[c], std::array<double, 3>{1, 1, 0}[c]);
        World interpolated(project, source, actor);
        auto moving = reach;
        moving.target_x = .8f;
        interpolated.entity().set(moving);
        interpolated.simulation.tick(.1f);
        const auto between = interpolated.animation->presentation(interpolated.entity().id(), .5);
        near(position(between, end)[0], .9);
        near(position(between, end)[1], 1);
        near(between.at("time"), .05, 1e-6);
        World crossing(project, source, actor);
        moving.target_x = -1;
        crossing.entity().set(moving);
        crossing.simulation.tick(.1f);
        const auto stable = crossing.animation->presentation(crossing.entity().id(), .5);
        near(position(stable, end)[0], -1);
        near(position(stable, end)[1], 1);
        near(stable.at("time"), .1, 1e-6);
        const auto crossing_locals = locals(crossing.scene);
        (void)crossing.simulation.presentation(.5);
        require(locals(crossing.scene) == crossing_locals,
                "Intermediate IK fallback mutated gameplay transforms");
        World limits(project, source, actor);
        auto distant = reach;
        distant.target_x = 3;
        limits.entity().set(distant);
        limits.update();
        const auto extended = position(limits.pose(), end);
        near(std::sqrt(extended[0] * extended[0] + extended[1] * extended[1] +
                       extended[2] * extended[2]),
             1.5);
        distant.soften = .5f;
        distant.target_x = 1.2f;
        limits.entity().set(distant);
        limits.update();
        const auto softened = position(limits.pose(), end);
        require(std::sqrt(softened[0] * softened[0] + softened[1] * softened[1] +
                          softened[2] * softened[2]) < 1.49,
                "IK softening did not reduce near-extension reach");
        distant = reach;
        const auto ordinary_middle = position(full.pose(), middle);
        distant.twist_degrees = 180;
        limits.entity().set(distant);
        limits.update();
        for (unsigned c = 0; c < 3; ++c)
            near(position(limits.pose(), end)[c], std::array<double, 3>{1, 1, 0}[c]);
        require(position(limits.pose(), middle) != ordinary_middle,
                "IK twist did not change the bend plane");
        auto disabled = source;
        for (auto& row : disabled["entities"])
            if (row["id"] == Json(actor))
                row["components"]["forge.two_bone_ik"]["enabled"] = false;
        World base(project, disabled, actor);
        near(position(base.pose(), end)[0], 0);
        near(position(base.pose(), end)[1], 1.5);
        const auto unconstrained_rest = base.pose()["local"];
        auto half = reach;
        half.weight = .5f;
        full.entity().set(half);
        full.update();
        const auto hp = position(full.pose(), end);
        require(hp != position(base.pose(), end) && hp != Double3{1, 1, 0},
                "IK weight did not affect pose");
        const auto before = scene.snapshot();
        authoring_command(scene, "property.set",
                          {{"entity", actor.str()},
                           {"component", "forge.two_bone_ik"},
                           {"field", "target_x"},
                           {"value", .8}});
        require(scene.undo() && scene.snapshot() == before, "IK Undo failed");
        require(scene.redo(), "IK Redo failed");
        // Weight0 and disable/remove restore the original sampled rotation exactly once.
        half.weight = 0;
        full.entity().set(half);
        full.update();
        require(full.pose()["local"] == base.pose()["local"], "Weight0 left a constrained pose");
        full.entity().set(reach);
        full.update();
        full.entity().remove<TwoBoneIK>();
        full.update();
        require(full.pose()["local"] == base.pose()["local"],
                "Removing IK left a constrained pose");
        flecs::entity root_entity;
        full.engine.world().world().each([&](flecs::entity e, const ModelSource& s) {
            if (s.node.id == root)
                root_entity = e;
        });
        const auto authored_rotation = rotation_about_axis({0, 1, 0}, 22);
        root_entity.set(authored_rotation);
        full.simulation.tick(.1f);
        require(equivalent(root_entity.get<LocalRotation>(), authored_rotation),
                "Removed IK kept owning an unanimated rotation");
        full.entity().set(reach);
        root_entity.set(LocalScale{-1, 1, 1});
        stage = "mirrored reach";
        full.update();
        require(full.animation->model_pose_ready(full.entity().id()), "Mirrored IK rejected");
        for (unsigned c = 0; c < 3; ++c)
            near(position(full.pose(), end)[c], std::array<double, 3>{1, 1, 0}[c]);
        root_entity.set(LocalScale{2, 1, 1});
        stage = "nonuniform reach";
        const auto nonuniform = locals(full.scene);
        full.update(false);
        require(!full.animation->model_pose_ready(full.entity().id()) &&
                    locals(full.scene) == nonuniform,
                "Nonuniform IK was silently accepted or partially written");
        root_entity.set(LocalScale{2, 2, 2});
        stage = "uniform scaled reach";
        full.update();
        for (unsigned c = 0; c < 3; ++c)
            near(position(full.pose(), end)[c], std::array<double, 3>{1, 1, 0}[c]);
        root_entity.set(LocalScale{0, 1, 1});
        const auto singular = locals(full.scene);
        full.update(false);
        require(!full.animation->model_pose_ready(full.entity().id()) &&
                    locals(full.scene) == singular,
                "Singular IK was silently accepted or partially written");
        root_entity.set(LocalScale{});
        full.update();
        flecs::entity middle_entity;
        full.engine.world().world().each([&](flecs::entity e, const ModelSource& s) {
            if (s.node.id == middle)
                middle_entity = e;
        });
        for (unsigned signs = 0; signs < 8; ++signs) {
            const LocalScale reflected{signs & 1 ? -1.f : 1.f, signs & 2 ? -1.f : 1.f,
                                       signs & 4 ? -1.f : 1.f};
            for (auto e : {root_entity, middle_entity}) {
                stage = "signed joint reach";
                e.set(reflected);
                full.update();
                for (unsigned c = 0; c < 3; ++c)
                    near(position(full.pose(), end)[c], std::array<double, 3>{1, 1, 0}[c]);
                e.set(LocalScale{});
                full.update();
            }
        }
        // Aim the middle's local +Y towards (1,2,0): a -45degree Z rotation.
        stage = "joint aim";
        AimIK aim;
        aim.joint = {middle};
        aim.enabled = true;
        aim.target_x = 1;
        aim.target_y = 2;
        aim.target_z = 0;
        aim.forward_x = 0;
        aim.forward_y = 1;
        aim.up_y = 0;
        aim.up_z = 1;
        aim.pole_y = 0;
        aim.pole_z = 1;
        base.entity().set(aim);
        base.update();
        const auto aim_pose = base.pose();
        const auto mi = joint(aim_pose, middle);
        near(aim_pose["local"][mi]["rotation"][2], -std::sin(.392699081698724));
        near(aim_pose["local"][mi]["rotation"][3], std::cos(.392699081698724));
        auto weighted = aim;
        weighted.weight = .5f;
        base.entity().set(weighted);
        base.update();
        near(base.pose()["local"][mi]["rotation"][2], -std::sin(.196349540849362));
        flecs::entity aimed_joint;
        base.engine.world().world().each([&](flecs::entity e, const ModelSource& s) {
            if (s.node.id == middle)
                aimed_joint = e;
        });
        base.entity().set(aim);
        for (unsigned signs = 0; signs < 8; ++signs) {
            stage = "signed joint aim";
            aimed_joint.set(
                LocalScale{signs & 1 ? -1.f : 1.f, signs & 2 ? -1.f : 1.f, signs & 4 ? -1.f : 1.f});
            base.update();
            const auto signed_pose = base.pose();
            const auto& m = signed_pose["model"][mi];
            // The transformed local +Y points along the independently known
            // target direction from the middle's unchanged (0,1,0) origin.
            near(m[4], std::sqrt(.5));
            near(m[5], std::sqrt(.5));
            near(m[6], 0);
        }
        aimed_joint.set(LocalScale{});
        base.update();
        // Aim follows reach without moving its endpoint. Partial weights are sampled anew.
        full.entity().set(reach);
        aim.joint = {end};
        aim.target_x = 1;
        aim.target_y = 2;
        aim.target_z = 1;
        aim.forward_x = 0;
        aim.forward_y = 1;
        aim.up_x = 1;
        aim.up_z = 0;
        full.entity().set(aim);
        full.update();
        for (unsigned c = 0; c < 3; ++c)
            near(position(full.pose(), end)[c], std::array<double, 3>{1, 1, 0}[c]);
        const auto unchanged = full.pose();
        full.update();
        require(full.pose() == unchanged, "Repeated IK accumulated correction");
        const auto checkpoint = full.animation->checkpoint();
        World restored(project, full.scene.snapshot(), actor);
        restored.animation->restore(checkpoint);
        require(restored.pose() == full.pose(), "IK recovery changed the complete pose");
        World cold(project, full.scene.snapshot(), actor, false);
        cold.animation->restore(checkpoint);
        require(cold.pose() == full.pose(), "Cold recovery omitted derived rig bindings");
        full.simulation.tick(.1f);
        restored.simulation.tick(.1f);
        cold.simulation.tick(.1f);
        require(restored.pose() == full.pose(), "IK continuation changed after recovery");
        require(cold.pose() == full.pose(), "Cold IK recovery continuation diverged");
        SkeletalAnimationPreview preview;
        preview.start(project, full.scene.snapshot(), full.scene.reference(actor.str()));
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!preview.prepare()) {
            require(std::chrono::steady_clock::now() < deadline, "IK preview timeout");
            std::this_thread::sleep_for(1ms);
        }
        preview.seek(.1);
        require(preview.pose()["local"] == full.pose()["local"], "Preview omitted constraints");
        preview.stop();
        // Invalid bindings/planes and downstream physics veto all transform writes.
        for (unsigned bad = 0; bad < 4; ++bad) {
            auto invalid = reach;
            if (bad == 0)
                invalid.middle = {side};
            if (bad == 1)
                invalid.end = {AssetId::generate()};
            if (bad == 2) {
                invalid.pole_x = 1;
                invalid.pole_y = 1;
            }
            if (bad == 3) {
                invalid.axis_z = 0;
            }
            const auto old = locals(full.scene);
            full.entity().set(invalid);
            full.update(false);
            require(!full.animation->model_pose_ready(full.entity().id()) &&
                        locals(full.scene) == old,
                    "Invalid IK partially wrote ECS transforms");
        }
        full.entity().set(reach);
        full.update();
        const auto old = locals(full.scene);
        full.animation->pose_validator(
            [](const auto&) { throw std::runtime_error("Physics veto fixture"); });
        reach.target_x = .8;
        full.entity().set(reach);
        full.update(false);
        require(locals(full.scene) == old, "Physics veto partially applied IK");
        full.animation->pose_validator({});
        full.update();
        require(full.animation->model_pose_ready(full.entity().id()),
                "IK did not recover from veto");
        auto motion = full.entity().get<Animator>();
        motion.root_motion = 2;
        full.entity().set(motion);
        const auto last = locals(full.scene);
        full.update(false);
        require(!full.animation->model_pose_ready(full.entity().id()) && locals(full.scene) == last,
                "Root motion and IK shared the same rotation owner");
        // Interrupted fades capture the expression before IK, even at half weight.
        const auto graph = AssetId::generate();
        auto states = AnimationStatesSource::create(graph);
        states.document["skeleton"] = scene.entity(actor.str()).get<Animator>().skeleton.id;
        states.document["entry"] = 1;
        states.document["next_id"] = 4;
        states.document["states"] = Json::array();
        for (unsigned i = 1; i <= 3; ++i)
            states.document["states"].push_back(
                {{"id", i},
                 {"name", i == 1   ? "Rest"
                          : i == 2 ? "Base"
                                   : "Overlay"},
                 {"clip", selection.bindings.at(i == 1   ? "/animations/2"
                                                : i == 2 ? "/animations/0"
                                                         : "/animations/1")},
                 {"loop", true},
                 {"speed", 1},
                 {"transition_seconds", .4}});
        states.validate();
        atomic_write(project / "Assets/Rig.states.json", states.document.dump(2));
        AssetImportService state_imports(lease, animation_states_import_registry(),
                                         {"linux", "none", "cpu"}, 1);
        state_imports.submit(
            state_imports.prepare("Assets/Rig.states.json", {}, graph),
            [](auto& x, const auto& p, const auto&) { prepare_animation_states_publication(x, p); },
            [](const auto&, const auto&) {});
        require(state_imports.wait_idle(10s), "Rig states publication timeout");
        const auto state_receipts = state_imports.poll();
        require(state_receipts.size() == 1 && state_receipts[0].published,
                "Rig states publication failed");
        auto named = source, plain = source;
        for (auto* document : {&named, &plain})
            for (auto& row : (*document)["entities"])
                if (row["id"] == Json(actor)) {
                    auto& v = row["components"]["forge.animator"];
                    v["states"] = graph;
                    v["state"] = 1;
                    row["components"]["forge.two_bone_ik"]["weight"] = document == &named ? .5 : 0.;
                }
        World constrained_states(project, named, actor), plain_states(project, plain, actor);
        for (unsigned selected : {2u, 3u})
            for (auto* world : {&constrained_states, &plain_states}) {
                auto v = world->entity().get<Animator>();
                v.state = selected;
                world->entity().set(v);
                world->simulation.tick(.1f);
            }
        const auto rig_checkpoint = constrained_states.animation->checkpoint();
        const auto frozen = rig_checkpoint["entries"][0]["states"]["frozen"];
        require(!frozen.empty() &&
                    frozen ==
                        plain_states.animation->checkpoint()["entries"][0]["states"]["frozen"],
                "Interrupted fade froze an already constrained expression");
        World recovered_states(project, constrained_states.scene.snapshot(), actor);
        recovered_states.animation->restore(rig_checkpoint);
        require(recovered_states.pose() == constrained_states.pose(),
                "Interrupted IK recovery changed pose");
        constrained_states.simulation.tick(.1f);
        recovered_states.simulation.tick(.1f);
        require(recovered_states.pose() == constrained_states.pose(),
                "Interrupted IK recovery continuation diverged");
        World reloaded(project, source, actor);
        const auto previous_generation = reloaded.pose().at("model_generation");
        const auto reimport = service.prepare("Assets/OverlayColumn.gltf");
        service.submit(
            reimport,
            [](auto& c, const auto& p, const auto& catalog) {
                prepare_model_publication(c, p, catalog);
            },
            [](const auto&, const auto&) {});
        require(service.wait_idle(60s), "Rig reimport timeout");
        const auto rebuilt = service.poll();
        require(rebuilt.size() == 1 && rebuilt[0].published, "Rig reimport publication failed");
        reloaded.entity().remove<TwoBoneIK>();
        reloaded.animation->refresh_assets();
        const auto reload_deadline = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            const auto current = reloaded.pose();
            if (!current.is_null() && current.at("model_generation") != previous_generation)
                break;
            require(std::chrono::steady_clock::now() < reload_deadline, "Rig reload timeout");
            reloaded.simulation.tick(.01f);
            std::this_thread::sleep_for(1ms);
        }
        require(reloaded.pose()["local"] == unconstrained_rest,
                "Model revision retained former constraint rotations");
        std::cout << "Actual two-bone/aim, weighting, reset ownership, preview, recovery and "
                     "atomic rejection passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
