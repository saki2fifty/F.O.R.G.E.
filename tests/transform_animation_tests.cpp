#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include "transform_animation_preview.hpp"
#include <chrono>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/physics_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <numbers>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::abs(a - b) < .00005,
            "Animation numeric mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid animation operation accepted");
}
AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& source,
                           AssetId id) {
    service.submit(
        service.prepare(source, {}, id),
        [](auto& c, const auto& p, const auto&) { prepare_transform_animation_publication(c, p); },
        [](const auto&, const auto&) {});
    if (!service.wait_idle(10s)) {
        std::string detail = "Animation publication stalled: " + source.generic_string();
        for (const auto& job : service.jobs())
            detail += " | job=" + std::to_string(job.id) +
                      " state=" + std::to_string(unsigned(job.state)) + " stage=" + job.stage +
                      " diagnostic=" + job.diagnostic;
        throw std::runtime_error(detail);
    }
    auto result = service.poll();
    require(result.size() == 1, "Animation publication receipt missing");
    return std::move(result.front());
}
struct Fixture {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    explicit Fixture(const std::filesystem::path& root)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {}
    void load(const Json& source) {
        scene.restore_snapshot(source);
        simulation.reset_presentation();
    }
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Initial animation resource timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
};
} // namespace
#include "animation_particle_properties_tests.hpp"
#include "animation_property_tests.hpp"
#include "transform_animation_key_batch_tests.hpp"
#include "transform_animation_timing_tests.hpp"
#include "transform_animation_value_tests.hpp"
#include "transform_curve_tests.hpp"
#include "transform_tangent_preset_tests.hpp"
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need animation scratch root");
        animation_key_batch_tests();
        animation_tangent_preset_tests();
        animation_timing_tests();
        animation_value_tests();
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        animation_property_tests(root / "property-tests");
        animation_particle_property_tests(root / "particle-property-tests");
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        auto document = TransformAnimationDocument::create(lease, "Assets/Motion.animation.json");
        const auto id = document->source().asset();
        const auto initial = document->source().document;
        const LocalTransform end{{4, 2, 0}, rotation_from_euler({0, 90, 0}), {3, 2, 1}};
        document->edit(document->revision(), "Add pose", [&](Json& j) {
            TransformAnimationSource source{j};
            source.set_key(animation_timebase, end);
            source.set_key(2 * animation_timebase, {});
            j = source.document;
        });
        const auto authored = document->source().document;
        const auto sample = document->source().clip().sample(.5 * animation_timebase);
        near(sample.translation.x, 2);
        near(sample.translation.y, 1);
        near(sample.scale.x, 2);
        const auto expected = rotation_from_euler({0, 45, 0});
        near(sample.rotation.y, expected.y);
        near(sample.rotation.w, expected.w);
        near(document->source().clip().sample(-1).translation.x, 0);
        near(document->source().clip().sample(3 * animation_timebase).translation.x, 0);
        document->undo();
        require(document->source().document == initial && !document->dirty(),
                "Animation Undo did not restore source");
        document->redo();
        require(document->source().document == authored,
                "Animation Redo lost stable key identities");
        const auto revision = document->revision();
        rejects(
            [&] { document->edit(revision - 1, "Stale", [](auto& j) { j["duration_us"] = 0; }); });
        rejects([&] {
            document->edit(revision, "Bad duration", [](auto& j) { j["duration_us"] = 0; });
        });
        require(document->revision() == revision && document->source().document == authored,
                "Rejected edit mutated source/history");
        auto changed = document->source();
        const auto middle = changed.document["keys"][1]["id"].get<AssetId>();
        changed.move_key(middle, 750000);
        require(changed.document["keys"][1]["id"] == Json(middle), "Retime reminted key identity");
        const auto before = changed.document;
        rejects([&] { changed.move_key(middle, 0); });
        require(changed.document == before, "Rejected duplicate time mutated source");
        changed.erase_key(middle);
        require(changed.document["keys"].size() == 2, "Key deletion failed");
        auto held = document->source();
        held.document["keys"][0]["interpolation"] = "held";
        held.validate();
        near(held.clip().sample(999999).translation.x, 0);
        near(held.clip().sample(1000000).translation.x, 4);
        auto antipodal = document->source();
        antipodal.document["keys"][1]["rotation"] = {0, 0, 0, -1};
        antipodal.validate();
        near(antipodal.clip().sample(500000).rotation.w, 1);
        auto bad = document->source();
        bad.document["keys"][1]["rotation"] = {0, 0, 0, 0};
        rejects([&] { bad.validate(); });
        bad = document->source();
        bad.document["keys"][1]["scale"] = {10001, 1, 1};
        rejects([&] { bad.validate(); });
        bad.document["keys"][1]["scale"] = {-1, 1, 1};
        bad.validate();
        near(bad.clip().sample(500000).scale.x, 0);
        bad.document["keys"][0]["interpolation"] = "held";
        bad.document["keys"][1]["interpolation"] = "held";
        bad.validate();
        bad = document->source();
        bad.document["keys"][1]["id"] = bad.document["keys"][0]["id"];
        rejects([&] { bad.validate(); });
        bad = document->source();
        bad.document["channels"].push_back("forge.local_translation");
        rejects([&] { bad.validate(); });
        rejects(
            [&] { decode_transform_animation(document->source().cook(), AssetId::generate()); });
        document->save();
        TransformAnimationDocument reopened(lease, "Assets/Motion.animation.json");
        require(reopened.source().document == authored, "Animation save/reopen differs");
        AssetImportService service(lease, transform_animation_import_registry(),
                                   {"linux", "none", "cpu"});
        auto receipt = publish(service, document->locator(), id);
        require(receipt.published, "Animation publication failed: " + receipt.diagnostic);
        EngineContext author;
        Scene scene(author.world());
        const auto actor = authoring_command(scene, "entity.create", {{"name", "Moving cube"}})
                               .at("selected")
                               .get<std::string>();
        authoring_command(scene, "component.add",
                          {{"entity", actor}, {"component", "forge.transform_animator"}});
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.transform_animator"},
                           {"field", "clip"},
                           {"value", id}});
        const auto source = scene.document();
        const auto scene_revision = scene.revision();
        auto preview = scene.effective_document();
        require(apply_transform_animation_preview(preview,
                                                  {scene.asset_id(), EntityId::parse(actor)},
                                                  document->source().clip(), 500000),
                "Preview target missing");
        require(scene.document() == source && scene.revision() == scene_revision,
                "Scrub changed authored scene/history");
        for (const auto& e : preview["entities"])
            if (e["id"] == actor)
                near(e["components"]["forge.local_translation"]["x"].get<double>(), 2);
        Fixture live(root);
        live.load(source);
        live.ready();
        live.simulation.tick(.25f);
        near(live.scene.entity(actor).get<LocalTranslation>().x, 1);
        near(live.scene.entity(actor).get<LocalScale>().x, 1.5);
        near(live.simulation.presentation(.5)["entities"][0]["world_affine"][3].get<double>(), .5);
        const auto checkpoint = live.animation->checkpoint();
        require(checkpoint["transform_entries"].size() == 1,
                "Transform playback absent from checkpoint");
        auto wrong = checkpoint;
        wrong["transform_entries"][0]["time_us"] = -1;
        rejects([&] { live.animation->restore(wrong); });
        require(live.animation->checkpoint() == checkpoint,
                "Failed recovery changed playback clock");
        wrong = checkpoint;
        wrong["transform_entries"][0]["revision"] = "bad";
        rejects([&] { live.animation->restore(wrong); });
        Fixture recovered(root);
        recovered.load(live.scene.document());
        recovered.ready();
        recovered.animation->restore(checkpoint);
        near(recovered.scene.entity(actor).get<LocalTranslation>().x, 1);
        recovered.simulation.tick(.25f);
        near(recovered.scene.entity(actor).get<LocalTranslation>().x, 2);
        for (int i = 0; i < 7; ++i)
            live.simulation.tick(.25f);
        near(live.scene.entity(actor).get<LocalTranslation>().x, 0);
        auto config = live.scene.entity(actor).get<TransformAnimator>();
        config.loop = false;
        live.scene.entity(actor).set<TransformAnimator>(config);
        for (int i = 0; i < 9; ++i)
            live.simulation.tick(.25f);
        near(live.scene.entity(actor).get<LocalTranslation>().x, 0);
        near(live.animation->checkpoint()["transform_entries"][0]["time_us"].get<double>(),
             2000000);
        Fixture renamed(root);
        renamed.load(source);
        renamed.ready();
        authoring_command(renamed.scene, "entity.rename",
                          {{"entity", actor}, {"name", "Renamed platform"}});
        const auto parent =
            authoring_command(renamed.scene, "entity.create", {{"name", "New parent"}})
                .at("selected")
                .get<std::string>();
        authoring_command(renamed.scene, "entity.reparent",
                          {{"entity", actor}, {"parent", parent}, {"mode", "keep_local"}});
        renamed.simulation.tick(.25f);
        near(renamed.scene.entity(actor).get<LocalTranslation>().x, 1);
        config = renamed.scene.entity(actor).get<TransformAnimator>();
        const auto valid = config.clip;
        config.clip.id = AssetId::generate();
        renamed.scene.entity(actor).set<TransformAnimator>(config);
        renamed.simulation.tick(.25f);
        near(renamed.scene.entity(actor).get<LocalTranslation>().x, 2);
        require(!renamed.animation->checkpoint_ready(), "Invalid replacement admitted recovery");
        config.clip = valid;
        renamed.scene.entity(actor).set<TransformAnimator>(config);
        renamed.simulation.tick(.25f);
        near(renamed.scene.entity(actor).get<LocalTranslation>().x, 3);
        require(renamed.animation->checkpoint_ready(),
                "Corrected asset did not clear failed selection");
        Fixture frozen(root);
        frozen.load(source);
        config = frozen.scene.entity(actor).get<TransformAnimator>();
        config.playback_speed = 0;
        frozen.scene.entity(actor).set<TransformAnimator>(config);
        frozen.ready();
        frozen.simulation.tick(.25f);
        near(frozen.scene.entity(actor).get<LocalTranslation>().x, 0);
        Fixture cancelled(root);
        cancelled.load(source);
        cancelled.animation->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        cancelled.animation->synchronize();
        cancelled.scene.entity(actor).remove<TransformAnimator>();
        cancelled.animation->synchronize();
        require(cancelled.animation->checkpoint_ready() &&
                    cancelled.animation->checkpoint()["transform_entries"].empty(),
                "Removed pending player survived cancellation");
        auto position_only = TransformAnimationSource::create(AssetId::generate());
        position_only.document["channels"] = {"forge.local_translation"};
        position_only.set_key(animation_timebase, end);
        atomic_write(root / "Assets/Position.animation.json", position_only.document.dump(2));
        receipt = publish(service, "Assets/Position.animation.json", position_only.asset());
        require(receipt.published, "Position-only clip failed to publish");
        auto selective = source;
        selective["entities"][0]["components"]["forge.transform_animator"]["clip"] =
            position_only.asset();
        selective["entities"][0]["components"]["forge.local_scale"] = {
            {"x", 5}, {"y", 5}, {"z", 5}};
        Fixture selected_channels(root);
        selected_channels.load(selective);
        selected_channels.ready();
        selected_channels.simulation.tick(.25f);
        near(selected_channels.scene.entity(actor).get<LocalTranslation>().x, 1);
        near(selected_channels.scene.entity(actor).get<LocalScale>().x, 5);
        auto jump_source = position_only;
        jump_source.document["keys"][0]["interpolation"] = "held";
        atomic_write(root / "Assets/Position.animation.json", jump_source.document.dump(2));
        receipt = publish(service, "Assets/Position.animation.json", position_only.asset());
        require(receipt.published, "Held clip failed to publish");
        Fixture jump(root);
        jump.load(selective);
        jump.ready();
        jump.simulation.reset_presentation();
        for (int i = 0; i < 4; ++i)
            jump.simulation.tick(.25f);
        near(jump.simulation.presentation(.5)["entities"][0]["world_affine"][3].get<double>(), 4);
        for (int i = 0; i < 4; ++i)
            jump.simulation.tick(.25f);
        near(jump.simulation.presentation(.5)["entities"][0]["world_affine"][3].get<double>(), 0);
        Fixture conflict(root);
        conflict.load(source);
        PhysicsBody body;
        body.motion = 2;
        conflict.scene.entity(actor).set<PhysicsBody>(body);
        rejects([&] { conflict.ready(); });
        near(conflict.scene.entity(actor).get<LocalTranslation>().x, 0);
        // Export from a saved scene: its component reference discovers the clip.
        atomic_write(root / "main.scene.json", source.dump(2));
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
        const auto package = root.parent_path() / ("package-" + AssetId::generate().str());
        (void)package_runtime_content(root, package, roots, {"linux", "none"}, {}, {},
                                      scene.schema());
        const auto shipped = open_runtime_content(package, {"linux", "none"});
        require(shipped.records().contains(id),
                "Scene export did not discover animation dependency");
        require(!std::filesystem::exists(package / "Assets/Motion.animation.json"),
                "Animation source shipped instead of cooked data");
        const auto offline = root / "source-offline";
        std::filesystem::rename(root / "Assets", offline);
        const auto relocated = root.parent_path() / ("relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        {
            Fixture exported(relocated);
            exported.load(source);
            exported.ready();
            exported.simulation.tick(.25f);
            near(exported.scene.entity(actor).get<LocalTranslation>().x, 1);
        }
        std::filesystem::rename(offline, root / "Assets");
        const auto prior = AssetCatalog::open_project(root).document();
        atomic_write(root / document->locator(), "{ invalid animation");
        rejects([&] { reopened.save(); });
        bool failed = false;
        try {
            failed = !publish(service, document->locator(), id).published;
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed && AssetCatalog::open_project(root).document() == prior,
                "Failed import displaced last good clip");
        transform_curve_tests(root, lease);
        animation_tangent_preset_pipeline_tests(root, lease);
        animation_timing_pipeline_tests(root, lease);
        std::cout << "transform animation source, history, preview, runtime, recovery and "
                     "source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
