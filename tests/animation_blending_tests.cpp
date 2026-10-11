#include "animation_asset.hpp"
#include "animation_preview.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_importer.hpp"
#include "model_placement.hpp"
#include "runtime_package.hpp"
#include <chrono>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, double tolerance = .002) {
    require(std::abs(a - b) < tolerance,
            "Blend numeric mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid blend accepted");
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
        const auto end = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < end, "Blend resource admission timed out");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
    Json pose(double alpha = 1) {
        return animation->presentation(scene.entity(actor.str()).id(), alpha);
    }
    void target(float weight, float seconds = .4f) {
        auto entity = scene.entity(actor.str());
        auto config = entity.get<Animator>();
        config.blend_weight = weight;
        config.transition_seconds = seconds;
        entity.set(config);
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 5 || argc == 6,
                "Need scratch root, model worker, converter and Gallery source");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        std::filesystem::copy_file(argv[4], root / "Assets/BlendColumn.gltf");
        auto registry = std::make_shared<AssetImporterRegistry>();
        const auto converter = std::filesystem::absolute(argv[3]);
        auto importer = model_importer(std::filesystem::absolute(argv[2]), converter);
        if (std::string_view(argv[2]) == "--direct")
            importer = std::make_shared<forge::test::DirectModelImporter>(importer, converter);
        registry->add(importer);
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(root);
        AssetImportService service(lease, registry, {"linux", "none", "cpu"}, 1);
        auto draft = service.prepare("Assets/BlendColumn.gltf");
        service.submit(
            draft,
            [](auto& c, const auto& p, const auto& catalog) {
                prepare_model_publication(c, p, catalog);
            },
            [](const auto&, const auto&) {});
        require(service.wait_idle(60s), "Blend fixture cook timed out");
        auto receipt = service.poll();
        require(receipt.size() == 1 && receipt.front().published,
                receipt.empty() ? "Missing cook receipt" : receipt.front().diagnostic);
        auto catalog = AssetCatalog::open_project(root);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type)
                model = id;
        require(bool(model), "Cooked model identity missing");
        const auto selected = load_model_selection(root, catalog, model);
        const auto primary = selected.bindings.at("/animations/0");
        const auto second = selected.bindings.at("/animations/1");
        EngineContext author;
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.name = "Blend column";
        options.clip = {primary};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
        const auto before = scene.document();
        apply_authoring(scene,
                        Json::array({{{"operation", "property.set"},
                                      {"arguments",
                                       {{"entity", actor.str()},
                                        {"component", "forge.animator"},
                                        {"field", "blend_clip"},
                                        {"value", second}}}}}),
                        scene.revision());
        const auto source = scene.document();
        if (argc == 6) {
            require(std::string(argv[5]) == "--prepare", "Unknown fixture mode");
            atomic_write(root / "main.scene.json", source.dump(2));
            std::cout << root.string() << '\n';
            return 0;
        }
        require(scene.undo() && scene.document() == before, "Blend assignment Undo failed");
        require(scene.redo() && scene.document() == source, "Blend assignment Redo failed");
        auto config = scene.entity(actor.str()).get<Animator>();
        require(config.blend_clip.id == second && config.blend_weight == 0,
                "Reflected blend fields lost");
        rejects([&] {
            authoring_command(scene, "property.set",
                              {{"entity", actor.str()},
                               {"component", "forge.animator"},
                               {"field", "blend_weight"},
                               {"value", 1.1}});
        });
        require(scene.document() == source, "Rejected blend edit mutated scene");
        Fixture f(root, source, actor);
        f.simulation.tick(.5f);
        f.target(1);
        f.simulation.tick(.2f);
        near(f.pose()["blend_weight"], .5);
        near(f.pose(.5)["blend_weight"], .25);
        const auto checkpoint = f.animation->checkpoint();
        for (int i = 0; i < 5; ++i)
            f.pose(.3);
        require(f.animation->checkpoint() == checkpoint, "Presentation advanced blend clock");
        // Interrupt a transition: begin the return from the current evaluated mix.
        f.target(0);
        f.simulation.tick(.2f);
        near(f.pose()["blend_weight"], .25);
        f.simulation.tick(.2f);
        near(f.pose()["blend_weight"], 0);
        f.target(1, 0);
        f.simulation.tick(.1f);
        near(f.pose()["blend_weight"], 1);
        near(f.pose(.5)["blend_weight"], 1);
        f.target(.5f, 0);
        f.simulation.tick(.1f);
        near(f.pose()["blend_weight"], .5);
        // At common normalized midpoint: Idle 10 degrees, Reach 75 degrees.
        SkeletalAnimationPreview preview;
        preview.start(root, source, scene.reference(actor.str()));
        while (!preview.prepare())
            std::this_thread::sleep_for(1ms);
        preview.configure(.5f, 0, true, 1);
        preview.seek(1);
        auto midpoint = preview.pose();
        const double half = 42.5 * 3.141592653589793 / 360;
        near(midpoint["local"][1]["rotation"][2], std::sin(half));
        near(midpoint["local"][1]["rotation"][3], std::cos(half));
        near(midpoint["local"][1]["translation"][0], .175);
        near(midpoint["local"][1]["translation"][1], 1);
        const auto rotation = midpoint["local"][1]["rotation"].get<std::array<float, 4>>();
        double norm = 0;
        for (auto value : rotation)
            norm += double(value) * value;
        near(norm, 1, .0002);
        const auto sampled = preview.snapshot();
        require(scene.document() == source, "Preview mutated authored scene");
        bool rotated = false;
        for (const auto& entity : sampled.at("entities"))
            if (entity.at("components").contains("forge.local_rotation")) {
                const auto q = entity.at("components").at("forge.local_rotation");
                if (q.value("z", 0.) > .3)
                    rotated = true;
            }
        require(rotated, "Blend was not applied to the model's local joint transform");
        preview.configure(0, 0, true, 1);
        preview.seek(1);
        near(preview.pose()["local"][1]["rotation"][2], std::sin(5 * 3.141592653589793 / 180));
        preview.configure(1, 0, true, 1);
        preview.seek(1);
        near(preview.pose()["local"][1]["rotation"][2], std::sin(37.5 * 3.141592653589793 / 180));
        preview.stop();
        require(!preview.active() && scene.document() == source, "Preview stop changed authoring");
        // Pause/speed zero preserve both clocks; Step uses the same fixed update.
        f.target(0, .4f);
        auto entity = f.scene.entity(actor.str());
        auto frozen = entity.get<Animator>();
        frozen.playback_speed = 0;
        entity.set(frozen);
        f.animation->synchronize();
        const auto held = f.animation->checkpoint();
        f.simulation.tick(.1f);
        require(f.animation->checkpoint() == held, "Speed zero advanced blend");
        RuntimeClock clock;
        const auto paused = f.animation->checkpoint();
        clock.pause(RuntimeClock::Time{});
        clock.advance(RuntimeClock::Time{} + 1s, [&](float dt) { f.simulation.tick(dt); });
        require(f.animation->checkpoint() == paused, "Pause advanced blend");
        frozen.playback_speed = 1;
        entity.set(frozen);
        clock.step([&](float dt) { f.simulation.tick(dt); });
        require(f.pose()["blend_weight"].get<double>() < .5, "Step did not advance transition");
        const auto state = f.animation->checkpoint();
        auto recovered_source = source;
        for (auto& item : recovered_source.at("entities"))
            if (item.at("id") == actor.str()) {
                item["components"]["forge.animator"]["blend_weight"] = 0;
                item["components"]["forge.animator"]["transition_seconds"] = .4f;
            }
        Fixture recovered(root, recovered_source, actor);
        recovered.animation->restore(state);
        recovered.simulation.reset_presentation();
        near(recovered.pose()["blend_weight"], f.pose()["blend_weight"]);
        auto bad = state;
        bad["entries"][0]["blend"]["revision"] = "wrong";
        rejects([&] { recovered.animation->restore(bad); });
        require(recovered.animation->checkpoint() == state,
                "Rejected recovery changed blend state");
        bad = state;
        bad["entries"][0]["blend"]["weight"] = .99;
        rejects([&] { recovered.animation->restore(bad); });
        f.simulation.tick(.1f);
        recovered.simulation.tick(.1f);
        near(recovered.pose()["blend_weight"], f.pose()["blend_weight"]);
        // Nonloop completion holds both clocks even during a long fade.
        Fixture terminal(root, source, actor);
        auto ending = terminal.scene.entity(actor.str());
        auto once = ending.get<Animator>();
        once.loop = false;
        once.blend_weight = 1;
        once.transition_seconds = 10;
        ending.set(once);
        terminal.simulation.tick(3);
        near(terminal.pose()["time"], 2);
        near(terminal.pose()["blend_weight"], .3);
        require(!terminal.pose()["playing"].get<bool>(), "Nonloop blend did not stop");
        const auto ended = terminal.animation->checkpoint();
        terminal.simulation.tick(.5f);
        require(terminal.animation->checkpoint() == ended, "Completed blend clocks advanced");
        // Wrong typed secondary selection cannot displace admitted playback.
        auto wrong_type = entity.get<Animator>();
        wrong_type.blend_clip.id = model;
        entity.set(wrong_type);
        f.simulation.tick(.1f);
        require(!f.pose().is_null(), "Wrong secondary type destroyed last-good playback");
        rejects([&] { f.animation->checkpoint(); });
        entity.set(frozen);
        f.simulation.tick(.1f);
        // A failed secondary replacement retains the last admitted pose/player.
        auto missing = entity.get<Animator>();
        missing.blend_clip.id = AssetId::generate();
        entity.set(missing);
        f.simulation.tick(.1f);
        require(!f.pose().is_null(), "Missing secondary destroyed last-good playback");
        rejects([&] { f.animation->checkpoint(); });
        entity.set(frozen);
        f.simulation.tick(.1f);
        require(f.animation->checkpoint_ready(), "Corrected secondary did not recover");
        // The normal saved scene scanner discovers both clips and the skin/model.
        atomic_write(root / "main.scene.json", source.dump(2));
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
        const auto package = root.parent_path() / ("blend-package-" + AssetId::generate().str());
        package_runtime_content(root, package, roots, {"linux", "none"}, {}, {}, scene.schema());
        const auto shipped = open_runtime_content(package, {"linux", "none"});
        require(shipped.records().contains(primary) && shipped.records().contains(second),
                "Scene export omitted second clip");
        require(!std::filesystem::exists(package / "Assets/BlendColumn.gltf"),
                "Authoring model shipped in runtime");
        const auto offline = root / "Assets-offline";
        std::filesystem::rename(root / "Assets", offline);
        const auto relocated =
            root.parent_path() / ("relocated-blend-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        Fixture exported(relocated, source, actor);
        exported.target(1);
        exported.simulation.tick(.2f);
        near(exported.pose()["blend_weight"], .5);
        std::filesystem::rename(offline, root / "Assets");
        std::cout << "two-clip transitions, model poses, preview, recovery and source-free export "
                     "passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
