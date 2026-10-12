#include "animation_root_motion_math_tests.hpp"
#include "animation_states_authoring.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_importer.hpp"
#include "model_placement.hpp"
#include "runtime_package.hpp"
#include <chrono>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <source_location>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected, double tolerance = .003,
          const std::source_location& where = std::source_location::current()) {
    require(std::isfinite(actual) && std::abs(actual - expected) < tolerance,
            "Root motion mismatch at line " + std::to_string(where.line()) + ": " +
                std::to_string(actual) + " vs " + std::to_string(expected));
}
struct Fixture {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    Fixture(const std::filesystem::path& project, const Json& doc, EntityId id)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())),
          actor(id) {
        scene.restore_snapshot(doc);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline, "Root motion admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
    flecs::entity entity() { return scene.entity(actor.str()); }
    LocalTransform local() { return engine.world().get_local_transform(entity()); }
    void clip(AssetId value) {
        auto config = entity().get<Animator>();
        config.clip.id = value;
        entity().set(config);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            const auto ready = animation->prepare_initial_pose();
            const auto realized = animation->playback(scene.reference(actor.str()));
            if (ready && realized && realized->clip == value)
                break;
            require(std::chrono::steady_clock::now() < deadline, "Root clip replacement timeout");
            std::this_thread::sleep_for(1ms);
        }
    }
    void mode(std::uint32_t value) {
        auto config = entity().get<Animator>();
        config.root_motion = value;
        entity().set(config);
    }
    std::optional<AnimationRootMotion> motion() {
        return animation->root_motion(scene.reference(actor.str()));
    }
    Json pose(double alpha = 1) { return animation->presentation(entity().id(), alpha); }
};
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 5 || argc == 6,
                "Need scratch, model worker, converter and original locomotion source");
        forge::test::root_motion_math_tests();
        const auto project = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(project / "Assets");
        std::filesystem::copy_file(argv[4], project / "Assets/LocomotionColumn.gltf");
        auto registry = std::make_shared<AssetImporterRegistry>();
        const auto converter = std::filesystem::absolute(argv[3]);
        auto importer = model_importer(std::filesystem::absolute(argv[2]), converter);
        if (std::string_view(argv[2]) == "--direct")
            importer = std::make_shared<forge::test::DirectModelImporter>(importer, converter);
        registry->add(importer);
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(project);
        AssetImportService service(lease, registry, {"linux", "none", "cpu"}, 1);
        auto draft = service.prepare("Assets/LocomotionColumn.gltf");
        service.submit(
            draft,
            [](auto& c, const auto& p, const auto& catalog) {
                prepare_model_publication(c, p, catalog);
            },
            [](const auto&, const auto&) {});
        require(service.wait_idle(60s), "Root motion cook timeout");
        const auto receipts = service.poll();
        require(receipts.size() == 1 && receipts[0].published,
                receipts.empty() ? "Missing root cook" : receipts[0].diagnostic);
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type)
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        const auto forward = selected.bindings.at("/animations/0");
        const auto strafe = selected.bindings.at("/animations/1");
        const auto turn = selected.bindings.at("/animations/2");
        const auto offset = selected.bindings.at("/animations/3");
        const auto joint = selected.bindings.at("/nodes/0");
        EngineContext author;
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.name = "Locomotion column";
        options.clip = {forward};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
        const auto before = scene.document();
        apply_authoring(scene,
                        Json::array({{{"operation", "property.set"},
                                      {"arguments",
                                       {{"entity", actor.str()},
                                        {"component", "forge.animator"},
                                        {"field", "root_motion"},
                                        {"value", 1}}}}}),
                        scene.revision());
        const auto source = scene.document();
        if (argc == 6) {
            require(std::string_view(argv[5]) == "--prepare", "Unknown root fixture mode");
            atomic_write(project / "main.scene.json", source.dump(2));
            std::cout << project.string() << '\n';
            return 0;
        }
        require(scene.undo() && scene.document() == before, "Root mode Undo failed");
        require(scene.redo() && scene.document() == source, "Root mode Redo failed");
        for (const unsigned hz : {30u, 60u, 120u}) {
            Fixture f(project, source, actor);
            for (unsigned i = 0; i < hz * 3; ++i) {
                f.simulation.tick(1.f / hz);
                const auto observation = f.motion();
                require(observation && observation->applied && observation->joint == joint &&
                            observation->clip == forward,
                        "Missing applied root observation");
                near(observation->translation[2], 2. / hz);
                // Render samples in arbitrary order must not alter fixed movement/cache.
                for (const double alpha : {.7, .1, 1.}) {
                    const auto pose = f.pose(alpha);
                    near(pose["local"][0]["translation"][2], 0);
                    near(pose["local"][0]["rotation"][3], 1);
                }
            }
            near(f.local().translation.z, 6);
            near(f.local().translation.x, 0);
        }
        {
            Fixture f(project, source, actor);
            f.mode(0);
            f.simulation.tick(.25f);
            near(f.local().translation.z, 0);
            near(f.pose()["local"][0]["translation"][2], .5);
            require(!f.motion(), "Off published a root delta");
            f.mode(2);
            f.simulation.tick(.25f);
            near(f.local().translation.z, 0);
            near(f.motion()->translation[2], .5);
            require(!f.motion()->applied, "Extract moved actor");
            f.mode(1);
            f.simulation.tick(.25f);
            near(f.local().translation.z, .5);
            const auto ref = f.scene.reference(actor.str());
            require(f.animation->control(ref, AnimationControl::Pause, 4), "Root pause failed");
            f.simulation.tick(.25f);
            near(f.local().translation.z, .5);
            near(f.motion()->translation[2], 0);
            require(f.animation->control(ref, AnimationControl::Restart, 5), "Root restart failed");
            f.simulation.tick(.25f);
            near(f.local().translation.z, 1);
            f.clip(offset);
            f.simulation.tick(.25f);
            near(f.local().translation.z, 1.5);
            near(f.local().translation.x, 0);
            auto config = f.entity().get<Animator>();
            config.loop = false;
            f.entity().set(config);
            f.animation->tick(3.f, 6);
            near(f.local().translation.z, 3);
            f.animation->tick(.25f, 7);
            near(f.local().translation.z, 3);
            near(f.motion()->translation[2], 0);
        }
        {
            Fixture f(project, source, actor);
            f.clip(turn);
            f.animation->tick(4.f, 1);
            near(f.local().translation.x, 0);
            near(f.local().translation.z, 0);
            near(std::abs(f.local().rotation.w), 1);
            f.animation->tick(2.f, 2);
            near(f.local().translation.x, 2);
            near(f.local().translation.z, 2);
        }
        {
            auto blended_source = source;
            for (auto& row : blended_source.at("entities"))
                if (row.at("id") == actor.str()) {
                    auto& animator = row.at("components").at("forge.animator");
                    animator["blend_clip"] = strafe;
                    animator["blend_weight"] = .5;
                    animator["transition_seconds"] = 0;
                }
            Fixture f(project, blended_source, actor);
            auto config = f.entity().get<Animator>();
            f.simulation.tick(.25f);
            near(f.local().translation.x, .25);
            near(f.local().translation.z, .25);
            config.blend_weight = 1;
            config.transition_seconds = .5f;
            f.entity().set(config);
            f.simulation.tick(.25f);
            near(f.motion()->translation[0], .3125);
            near(f.motion()->translation[2], .1875);
        }
        {
            Fixture f(project, source, actor);
            f.entity().set(LocalScale{2, 3, -4});
            f.simulation.tick(.25f);
            near(f.local().translation.z, -2);
            require(f.local().scale == LocalScale{2, 3, -4}, "Root movement changed signed scale");
            const auto previous = f.local();
            f.clip(turn);
            f.simulation.tick(.25f);
            require(f.local() == previous && !f.motion(),
                    "Sheared root candidate partially committed");
            f.mode(2);
            f.simulation.tick(.25f);
            require(f.motion() && !f.motion()->applied, "Extract failed on visual signed scale");
        }
        {
            Fixture f(project, source, actor);
            f.simulation.tick(.25f);
            const auto checkpoint = f.animation->checkpoint();
            Fixture restored(project, f.scene.snapshot(), actor);
            restored.animation->restore(checkpoint);
            restored.animation->reset_presentation();
            f.simulation.tick(.25f);
            restored.simulation.tick(.25f);
            near(restored.local().translation.z, f.local().translation.z);
            near(restored.motion()->translation[2], .5);
            auto config = restored.entity().get<Animator>();
            config.root_joint.id = selected.bindings.at("/nodes/1");
            restored.entity().set(config);
            const auto prior = restored.local();
            restored.simulation.tick(.25f);
            require(restored.local() == prior && !restored.motion(),
                    "Non-root binding changed actor");
        }
        {
            Fixture f(project, source, actor);
            unsigned observed = 0;
            auto system = f.engine.world()
                              .world()
                              .system("root-motion-consumer")
                              .kind(f.engine.world().world().entity("forge.runtime.PostAnimation"))
                              .immediate()
                              .run([&](flecs::iter&) {
                                  const auto motion = f.motion();
                                  require(motion && motion->tick == observed + 1,
                                          "PostAnimation saw stale root delta");
                                  ++observed;
                              });
            system.add<FixedSimulation>();
            f.simulation.tick(.25f);
            require(observed == 1, "PostAnimation consumer did not run");
            system.destruct();
            f.animation->pose_validator([&](const auto& candidate) {
                if (candidate.at(f.entity().id()).local != f.local())
                    throw std::runtime_error("Solver-owned actor rejected");
            });
            const auto prior = f.local();
            f.simulation.tick(.25f);
            require(f.local() == prior && !f.motion(),
                    "Pose validator failed atomic root rejection");
            f.mode(2);
            f.simulation.tick(.25f);
            require(f.motion() && f.local() == prior, "Extraction violated transform ownership");
        }
        {
            // Actual state resource, automatic exit and outgoing/frozen layers.
            const auto graph = AssetId::generate();
            auto definition = AnimationStatesSource::create(graph);
            definition.document["skeleton"] = selected.bindings.at("/rig/skeleton");
            definition.document["entry"] = 1;
            definition.document["next_id"] = 4;
            definition.document["states"] = Json::array({{{"id", 1},
                                                          {"name", "Forward"},
                                                          {"clip", forward},
                                                          {"loop", true},
                                                          {"speed", 1},
                                                          {"transition_seconds", .5},
                                                          {"exit", {{"state", 2}, {"phase", .25}}}},
                                                         {{"id", 2},
                                                          {"name", "Strafe"},
                                                          {"clip", strafe},
                                                          {"loop", true},
                                                          {"speed", 1},
                                                          {"transition_seconds", .5}},
                                                         {{"id", 3},
                                                          {"name", "Turn"},
                                                          {"clip", turn},
                                                          {"loop", true},
                                                          {"speed", 1},
                                                          {"transition_seconds", .5}}});
            definition.validate();
            const auto path = "Assets/Root.states.json";
            atomic_write(project / path, definition.document.dump(2));
            AssetImportService states(lease, animation_states_import_registry(),
                                      {"linux", "none", "cpu"}, 1);
            states.submit(
                states.prepare(path, {}, graph),
                [](auto& c, const auto& p, const auto&) {
                    prepare_animation_states_publication(c, p);
                },
                [](const auto&, const auto&) {});
            require(states.wait_idle(10s), "Root state publication timeout");
            const auto results = states.poll();
            require(results.size() == 1 && results[0].published,
                    results.empty() ? "Missing root state cook" : results[0].diagnostic);
            auto state_source = source;
            for (auto& row : state_source.at("entities"))
                if (row.at("id") == actor.str()) {
                    auto& config = row.at("components").at("forge.animator");
                    config["states"] = graph;
                    config["state"] = 1;
                }
            Fixture f(project, state_source, actor);
            f.simulation.tick(.25f);
            require(f.entity().get<Animator>().state == 2 && f.motion() &&
                        f.motion()->clip == forward,
                    "Automatic exit lost source-interval root motion");
            near(f.local().translation.z, .5);
            f.simulation.tick(.25f);
            near(f.motion()->translation[0], .125);
            near(f.motion()->translation[2], .375);
            const auto checkpoint = f.animation->checkpoint();
            Fixture restored(project, f.scene.snapshot(), actor);
            restored.animation->restore(checkpoint);
            f.simulation.tick(.25f);
            restored.simulation.tick(.25f);
            near(f.motion()->translation[0], .375);
            near(f.motion()->translation[2], .125);
            near(restored.local().translation.x, f.local().translation.x);
            near(restored.local().translation.z, f.local().translation.z);
            auto config = f.entity().get<Animator>();
            config.state = 3;
            f.entity().set(config);
            f.simulation.tick(.25f);
            near(f.motion()->translation[0], .375);
            near(f.motion()->translation[2], .125);
            config.state = 1;
            f.entity().set(config);
            f.simulation.tick(.25f);
            near(f.motion()->translation[0], 0);
            near(f.motion()->translation[2], .125);
            near(f.motion()->rotation[3], 1);
        }
        atomic_write(project / "main.scene.json", source.dump(2));
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
        const auto package = project.parent_path() / ("root-package-" + AssetId::generate().str());
        package_runtime_content(project, package, roots, {"linux", "none"}, {}, {}, scene.schema());
        std::filesystem::rename(project / "Assets", project / "Assets-offline");
        Fixture exported(package, source, actor);
        exported.simulation.tick(.25f);
        near(exported.local().translation.z, .5);
        near(exported.pose()["local"][0]["translation"][2], 0);
        require(!std::filesystem::exists(package / "Assets/LocomotionColumn.gltf"),
                "Root export included authoring source");
        std::cout << "imported root motion, loop turns, blends, extraction, atomic validation, "
                     "recovery and source-free export passed\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
