#include "animation_additive.hpp"
#include "animation_morph_blend.hpp"
#include "animation_preview.hpp"
#include "animation_states_authoring.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_placement.hpp"
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace forge::animation_detail;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double a, double b, double tolerance = .003) {
    require(std::isfinite(a) && std::abs(a - b) < tolerance,
            "Overlay value differs: " + std::to_string(a) + " vs " + std::to_string(b));
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid overlay or morph recovery accepted");
}
void arithmetic() {
    LocalPose rest{{2, 3, 4}, {-2, 0, 4}, {.5f, 0, 0, .8660254f}};
    LocalPose sample{{5, 1, 5}, {-4, 0, 2}, {0, .5f, 0, .8660254f}};
    const auto delta = additive_delta(sample, rest);
    require(delta.translation == std::array<float, 3>{3, -2, 1} &&
                delta.scale == std::array<float, 3>{2, 1, .5f},
            "Additive translation/signed scale differs");
    // inverse(X60) * Y60, independently expanded quaternion product.
    near(delta.rotation[0], -.4330127);
    near(delta.rotation[1], .4330127);
    near(delta.rotation[2], -.25);
    near(delta.rotation[3], .75);
    sample.scale[1] = 1;
    rejects([&] { (void)additive_delta(sample, rest); });
    MorphPose defaults{{2, {.25f, 0}}, {5, {.5f}}};
    MorphPose a{{2, {.5f, -.5f}}}, b{{2, {1.5f, .25f}}, {5, {-1.f}}};
    auto pose = blend_morph_samples(defaults, a, b, .5);
    near(pose[0].weights[0], 1);
    near(pose[0].weights[1], -.125);
    near(pose[1].weights[0], -.25);
    pose = blend_morph_samples(defaults, a, b, .5, true);
    near(pose[0].weights[0], 1.125);
    near(pose[0].weights[1], -.375);
    near(pose[1].weights[0], -.25);
    b[0].weights.pop_back();
    rejects([&] { (void)blend_morph_samples(defaults, a, b, .5); });
    b = a;
    b.push_back(a[0]);
    rejects([&] { (void)blend_morph_samples(defaults, a, b, .5); });
    const MorphAnimation curves(Json::array({{{"node", 5},
                                              {"components", 1},
                                              {"interpolation", 0},
                                              {"times", {0, 1}},
                                              {"values", {.5, -1}}},
                                             {{"node", 2},
                                              {"components", 2},
                                              {"interpolation", 0},
                                              {"times", {0, 1}},
                                              {"values", {.25, 0, .5, -.5}}}}));
    const auto sampled = curves.sample(.5);
    pose = blend_morph_samples(defaults, {}, sampled, 1);
    near(pose[0].weights[0], .375);
    near(pose[0].weights[1], -.25);
    near(pose[1].weights[0], -.25);
}
struct World {
    EngineContext engine;
    Scene scene;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    std::uint64_t tick_id = 0;
    World(const std::filesystem::path& project, const Json& source, EntityId id)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          animation(animation_runtime(engine.world())), actor(id) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline, "Overlay admission timeout");
            std::this_thread::sleep_for(1ms);
        }
    }
    flecs::entity entity() { return scene.entity(actor.str()); }
    EntityRef ref() { return scene.reference(actor.str()); }
    void tick(float seconds) { animation->tick(seconds, ++tick_id); }
    Json pose() { return animation->presentation(entity().id(), 1); }
    void state(unsigned id) {
        auto c = entity().get<Animator>();
        c.state = id;
        entity().set(c);
    }
};
std::size_t joint(const Json& pose, AssetId id) {
    for (std::size_t i = 0; i < pose.at("joint_assets").size(); ++i)
        if (pose["joint_assets"][i] == Json(id))
            return i;
    throw std::runtime_error("Overlay durable node absent");
}
Json config(Json source, EntityId actor, unsigned mode, float weight, AssetId mask = {}) {
    for (auto& row : source["entities"])
        if (row["id"] == Json(actor)) {
            auto& c = row["components"]["forge.animator"];
            c["blend_mode"] = mode;
            c["blend_weight"] = weight;
            c["blend_root"] = mask ? Json(mask) : Json();
        }
    return source;
}
std::array<float, 2> morph(const Json& pose) {
    require(pose.at("morphs").size() == 1, "Overlay explicit morph union differs");
    return pose["morphs"][0]["weights"].get<std::array<float, 2>>();
}
void compare(const Json& a, const Json& b) {
    require(a.at("local") == b.at("local") && a.at("morphs") == b.at("morphs"),
            "Recovery local/morph pose differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 5 || argc == 6, "Need scratch, worker, converter and original source");
        arithmetic();
        const auto project = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(project / "Assets");
        std::filesystem::copy_file(argv[4], project / "Assets/OverlayColumn.gltf");
        auto registry = std::make_shared<AssetImporterRegistry>();
        const auto converter = std::filesystem::absolute(argv[3]);
        auto importer = model_importer(std::filesystem::absolute(argv[2]), converter);
        if (std::string_view(argv[2]) == "--direct")
            importer = std::make_shared<forge::test::DirectModelImporter>(importer, converter);
        registry->add(importer);
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(project);
        AssetImportService imports(lease, registry, {"linux", "none", "cpu"}, 1);
        imports.submit(
            imports.prepare("Assets/OverlayColumn.gltf"),
            [](auto& c, const auto& p, const auto& catalog) {
                prepare_model_publication(c, p, catalog);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(60s), "Overlay import timeout");
        const auto receipts = imports.poll();
        require(receipts.size() == 1 && receipts[0].published,
                receipts.empty() ? "Missing overlay receipt" : receipts[0].diagnostic);
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, r] : catalog.records())
            if (r.type == ModelAsset::type)
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        const auto base = selected.bindings.at("/animations/0"),
                   overlay = selected.bindings.at("/animations/1"),
                   rest = selected.bindings.at("/animations/2"),
                   upper = selected.bindings.at("/nodes/1"), rig = selected.bindings.at("/nodes/0");
        EngineContext author;
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.clip = {base};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
        auto c = scene.entity(actor.str()).get<Animator>();
        require(c.blend_mode == 0, "New Animator additive mode is not opt-in");
        c.blend_clip = {overlay};
        c.blend_weight = 1;
        c.transition_seconds = 0;
        scene.entity(actor.str()).set(c);
        const auto before = scene.document();
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.animator"},
                           {"field", "blend_mode"},
                           {"value", 1}});
        const auto source = scene.document();
        require(scene.undo() && scene.document() == before && scene.redo() &&
                    scene.document() == source,
                "Additive mode typed history differs");
        if (argc == 6) {
            require(std::string_view(argv[5]) == "--prepare", "Unknown overlay fixture mode");
            atomic_write(project / "main.scene.json", source.dump(2));
            std::cout << project.string() << '\n';
            return 0;
        }
        World primary(project, config(source, actor, 0, 0), actor),
            blend(project, config(source, actor, 0, 1), actor), additive(project, source, actor),
            half(project, config(source, actor, 1, .5f), actor),
            masked(project, config(source, actor, 1, 1, upper), actor),
            root_masked(project, config(source, actor, 1, 1, rig), actor);
        for (auto* w : {&primary, &blend, &additive, &half, &masked, &root_masked})
            w->tick(1);
        const auto pose = additive.pose();
        compare(pose, root_masked.pose());
        const auto u = joint(pose, upper), r = joint(pose, rig);
        near(pose["local"][u]["rotation"][2], std::sin(85 * 3.141592653589793 / 360));
        near(pose["local"][u]["translation"][0], .35);
        near(pose["local"][r]["translation"][0], 1);
        near(pose["local"][r]["translation"][2], 1);
        near(morph(primary.pose())[0], .5);
        near(morph(primary.pose())[1], -.5);
        near(morph(blend.pose())[0], 1.5);
        near(morph(blend.pose())[1], .25);
        near(morph(pose)[0], 1.75);
        near(morph(pose)[1], -.25);
        near(morph(half.pose())[0], 1.125);
        near(morph(half.pose())[1], -.375);
        auto motion_source = source;
        for (auto& row : motion_source["entities"])
            if (row["id"] == Json(actor))
                row["components"]["forge.animator"]["root_motion"] = 2;
        World extracted(project, motion_source, actor);
        extracted.tick(1);
        const auto motion = extracted.animation->root_motion(extracted.ref());
        require(bool(motion), "Additive root extraction unavailable");
        near(motion->translation[0], 1);
        near(motion->translation[2], 1);
        near(motion->rotation[1], std::sin(3.141592653589793 / 12));
        near(extracted.pose()["local"][r]["translation"][0], 0);
        near(extracted.pose()["local"][r]["translation"][2], 0);
        require(masked.pose()["local"][r] == primary.pose()["local"][r] &&
                    morph(masked.pose()) == morph(primary.pose()),
                "Upper overlay modified excluded root/morph mesh");
        auto changed = additive.entity().get<Animator>();
        changed.blend_mode = 0;
        additive.entity().set(changed);
        additive.animation->synchronize();
        additive.tick(.1f);
        near(additive.pose()["time"], 1.1);
        near(morph(additive.pose())[0], 1.375);
        changed.blend_mode = 1;
        additive.entity().set(changed);
        additive.tick(.1f);
        near(additive.pose()["time"], 1.2);
        near(morph(additive.pose())[0], 1.45);
        const auto cp = additive.animation->checkpoint();
        World recovered(project, additive.scene.document(), actor);
        recovered.animation->restore(cp);
        compare(additive.pose(), recovered.pose());
        additive.tick(.15f);
        recovered.tick(.15f);
        compare(additive.pose(), recovered.pose());
        SkeletalAnimationPreview preview;
        preview.start(project, source, scene.reference(actor.str()));
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!preview.prepare()) {
            require(std::chrono::steady_clock::now() < deadline, "Overlay preview timeout");
            std::this_thread::sleep_for(1ms);
        }
        preview.seek(1);
        compare(pose, preview.pose());
        require(scene.document() == source, "Overlay preview wrote authored source");
        World seek(project, source, actor);
        seek.tick(.3f);
        require(seek.animation->control(seek.ref(), AnimationControl::Pause, 2) &&
                    seek.animation->seek(seek.ref(), .6, 2),
                "Overlay pause/seek rejected");
        seek.tick(.1f);
        near(seek.pose()["time"], .6);
        near(morph(seek.pose())[0], 1.15);
        near(morph(seek.pose())[1], -.15);
        // Named states own their independent outgoing clocks and freeze the whole evaluated pose.
        const auto graph = AssetId::generate();
        auto states = AnimationStatesSource::create(graph);
        states.document["skeleton"] = c.skeleton.id;
        states.document["entry"] = 1;
        states.document["next_id"] = 4;
        states.document["states"] = Json::array();
        for (unsigned i = 1; i <= 3; ++i)
            states.document["states"].push_back({{"id", i},
                                                 {"name", i == 1   ? "Base"
                                                          : i == 2 ? "Overlay"
                                                                   : "Rest"},
                                                 {"clip", i == 1   ? base
                                                          : i == 2 ? overlay
                                                                   : rest},
                                                 {"loop", true},
                                                 {"speed", 1},
                                                 {"transition_seconds", .4}});
        states.validate();
        atomic_write(project / "Assets/Morph.states.json", states.document.dump(2));
        AssetImportService state_imports(lease, animation_states_import_registry(),
                                         {"linux", "none", "cpu"}, 1);
        state_imports.submit(
            state_imports.prepare("Assets/Morph.states.json", {}, graph),
            [](auto& x, const auto& p, const auto&) { prepare_animation_states_publication(x, p); },
            [](const auto&, const auto&) {});
        require(state_imports.wait_idle(10s), "Morph states publication timeout");
        const auto sr = state_imports.poll();
        require(sr.size() == 1 && sr[0].published,
                sr.empty() ? "Missing states receipt" : sr[0].diagnostic);
        auto named = config(source, actor, 0, 0);
        for (auto& row : named["entities"])
            if (row["id"] == Json(actor)) {
                auto& v = row["components"]["forge.animator"];
                v["blend_clip"] = nullptr;
                v["states"] = graph;
                v["state"] = 1;
            }
        World state(project, named, actor);
        state.tick(1);
        state.state(2);
        state.tick(.1f);
        // Outgoing Base at1.1 is(.475,-.45), incoming Overlay at.1 is(.5,.05), fade=.25.
        near(morph(state.pose())[0], .48125);
        near(morph(state.pose())[1], -.325);
        const auto interrupted = state.pose();
        state.state(3);
        state.tick(.1f);
        // Rest has no curve; its model defaults(.25,0) fade from the frozen expression.
        near(morph(state.pose())[0], .4234375);
        near(morph(state.pose())[1], -.24375);
        const auto checkpoint = state.animation->checkpoint();
        require(checkpoint["entries"][0]["states"]["frozen_morphs"].size() == 1,
                "Interrupted state omitted morph source from recovery");
        require(checkpoint["entries"][0]["states"]["frozen_morphs"][0]["weights"] ==
                    interrupted["morphs"][0]["weights"],
                "Interrupted fade froze a different morph expression");
        World state_recovery(project, state.scene.document(), actor);
        state_recovery.animation->restore(checkpoint);
        compare(state.pose(), state_recovery.pose());
        state.tick(.1f);
        state_recovery.tick(.1f);
        compare(state.pose(), state_recovery.pose());
        auto bad = checkpoint;
        bad["entries"][0]["states"]["frozen_morphs"][0]["weights"] = {1};
        const auto safe = state.animation->checkpoint();
        rejects([&] { state.animation->restore(bad); });
        require(state.animation->checkpoint() == safe, "Rejected morph recovery changed clocks");
        state.tick(.3f);
        near(morph(state.pose())[0], .25);
        near(morph(state.pose())[1], 0);
        require(state.animation->control(state.ref(), AnimationControl::Restart, state.tick_id + 1),
                "State restart rejected");
        require(state.animation->checkpoint()["entries"][0]["states"]["frozen_morphs"].empty(),
                "Restart retained an orphan morph source");
        std::cout << "Additive local/root channels, signed morph mix, preview, typed history and "
                     "interrupted recovery passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
