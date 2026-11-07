#include "animation_graph_authoring.hpp"
#include "animation_graph_resource.hpp"
#include "animation_mask_authoring.hpp"
#include "animation_mask_resource.hpp"
#include "animation_playback_access.hpp"
#include "animation_pose_compare.hpp"
#include "animation_preview.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include "model_placement.hpp"
#include "model_selection.hpp"
#include "model_skeleton_binding.hpp"
#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected graph pipeline rejection");
}
AssetId publish(AssetImportService& service, const std::filesystem::path& source,
                AssetImportService::PreparePublication prepare, AssetId authored = {}) {
    auto draft = service.prepare(source, {}, authored);
    const auto id = draft.request.asset;
    service.submit(std::move(draft), std::move(prepare), [](const auto&, const auto&) {});
    check(service.wait_idle(60s), "Graph pipeline import timed out");
    const auto receipts = service.poll();
    check(receipts.size() == 1 && receipts.front().published,
          receipts.empty() ? "Missing graph import receipt" : receipts.front().diagnostic);
    return id;
}
void source_bounds(AnimationGraphDocument& document) {
    const auto before = document.source().document;
    const auto revision = document.revision();
    for (unsigned mode = 0; mode < 3; ++mode) {
        rejects([&] {
            document.edit(revision, "Invalid extension", [&](auto& d) {
                if (mode == 0) {
                    Json value = true;
                    for (unsigned i = 0; i < 14; ++i)
                        value = {{"child", std::move(value)}};
                    d["opaque"] = std::move(value);
                } else if (mode == 1)
                    d["opaque"] = std::string(animation_graph_byte_limit, 'x');
                else {
                    d["opaque"] = Json::array();
                    for (unsigned i = 0; i < 12000; ++i)
                        d["opaque"].push_back(0);
                }
            });
        });
        check(document.revision() == revision && document.source().document == before &&
                  !document.dirty() && !document.can_undo() && !document.can_redo(),
              "Rejected source bounds changed graph content, revision or history");
    }
}
struct World {
    EngineContext engine;
    Scene scene;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    std::uint64_t tick_id = 0;
    World(const std::filesystem::path& project, const Json& source, EntityId id, bool admit = true)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          animation(animation_runtime(engine.world())), actor(id) {
        scene.restore_snapshot(source);
        if (admit)
            ready();
    }
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline, "Graph runtime admission timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
    EntityRef ref() { return scene.reference(actor.str()); }
    Animator config() { return scene.entity(actor.str()).get<Animator>(); }
    void config(const Animator& value) { scene.entity(actor.str()).set(value); }
    double time() { return animation->playback(ref())->time; }
    void step(float dt) { animation->tick(dt, ++tick_id); }
    Json current_pose() { return animation->presentation(scene.entity(actor.str()).id(), 1); }
    Json pose(double phase) {
        const auto status = animation->playback(ref());
        check(bool(status), "Graph player is unavailable after admission");
        check(animation->seek(ref(), phase * status->duration, tick_id + 1),
              "Graph phase seek rejected");
        return current_pose();
    }
};
void same_pose(const Json& a, const Json& b, const char* context = "") {
    check(a.at("joint_assets") == b.at("joint_assets") &&
              a.at("local").size() == b.at("local").size(),
          "Graph runtime changed durable joint binding or pose count");
    for (std::size_t i = 0; i < a.at("local").size(); ++i)
        check(test::same_blended_local_pose(a.at("local")[i], b.at("local")[i]),
              std::string(context) + " joint " + std::to_string(i) +
                  " differs: " + a.at("local")[i].dump() + " versus " + b.at("local")[i].dump());
    check(a.at("morphs") == b.at("morphs"), "Graph dropped or changed morph channels");
}
std::pair<Json, EntityId> runtime_cases(const std::filesystem::path& root,
                                        const AssetCatalog& catalog,
                                        const asset_detail::ModelSelection& selected, AssetId mask,
                                        AssetId graph) {
    EngineContext author;
    Scene scene(author.world());
    asset_detail::ModelPlacementOptions options;
    options.name = "Graph actor";
    options.clip = {selected.bindings.at("/animations/0")};
    const auto actor =
        asset_detail::instantiate_model(scene, catalog,
                                        asset_detail::prepare_model_placement(
                                            selected, scene.asset_id(), scene.revision(), options));
    auto source = scene.document();
    auto manual = source;
    for (auto& row : source["entities"])
        if (row["id"] == Json(actor)) {
            auto& c = row["components"]["forge.animator"];
            c["clip"] = nullptr;
            c["graph"] = graph;
        }
    for (auto& row : manual["entities"])
        if (row["id"] == Json(actor)) {
            auto& c = row["components"]["forge.animator"];
            c["blend_clip"] = selected.bindings.at("/animations/1");
            c["blend_weight"] = .5;
            c["blend_mode"] = 1;
            c["blend_mask"] = mask;
            c["transition_seconds"] = 0;
        }
    World graph_world(root, source, actor), reference(root, manual, actor);
    for (const double phase : {0., .25, .5, .75})
        same_pose(graph_world.pose(phase), reference.pose(phase));
    const auto baseline = graph_world.pose(.5);
    const auto preview_source = graph_world.scene.document();
    SkeletalAnimationPreview preview;
    preview.start(root, preview_source, graph_world.ref());
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!preview.prepare()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Graph copied preview admission timed out");
        std::this_thread::sleep_for(1ms);
    }
    preview.seek(1.25);
    same_pose(preview.pose(), baseline, "Copied preview baseline");
    preview.select_graph_inputs({{"graph_override_1", true}, {"graph_input_1", .25}});
    check(preview.pose().at("time") == 1.25 && graph_world.scene.document() == preview_source,
          "Copied graph input preview changed its clock or authored World");
    const auto preview_good = preview.pose();
    rejects(
        [&] { preview.select_graph_inputs({{"graph_override_1", false}, {"graph_input_2", .5}}); });
    same_pose(preview.pose(), preview_good);
    check(preview.pose().at("graph_inputs")[0].at("override") == true,
          "Rejected copied input batch changed its override flag");
    const auto live_clock = graph_world.time();
    check(AnimationPlaybackAccess::tune_graph_input(*graph_world.animation, graph_world.ref(),
                                                    "graph_override_1", true) == true,
          "Live graph override was not adopted");
    (void)AnimationPlaybackAccess::tune_graph_input(*graph_world.animation, graph_world.ref(),
                                                    "graph_input_1", .25);
    same_pose(graph_world.current_pose(), preview_good, "Live input versus copied preview");
    const auto live_good = graph_world.current_pose();
    const auto live_config = graph_world.config();
    const auto live_status =
        AnimationPlaybackAccess::status(*graph_world.animation, graph_world.ref());
    check(live_status.at("graph").get<AssetId>() == graph &&
              live_status.at("graph_inputs")[0].at("name") == "Upper body" &&
              live_status.at("graph_inputs")[0].at("value") == .25 &&
              graph_world.time() == live_clock,
          "Live graph status lost names, effective values or retained clock");
    const Json invalid_values{{"graph_input_1", 1000001},
                              {"graph_override_1", 1},
                              {"graph_override_2", true},
                              {"graph", nullptr}};
    for (const auto& [field, value] : invalid_values.items())
        rejects([&] {
            (void)AnimationPlaybackAccess::tune_graph_input(*graph_world.animation,
                                                            graph_world.ref(), field, value);
        });
    check(graph_world.config() == live_config && graph_world.time() == live_clock,
          "Rejected live graph values changed configuration or time");
    same_pose(graph_world.current_pose(), live_good);
    graph_world.animation->pose_validator(
        [](const auto&) { throw std::runtime_error("Reject graph pose"); });
    rejects([&] {
        (void)AnimationPlaybackAccess::tune_graph_input(*graph_world.animation, graph_world.ref(),
                                                        "graph_input_1", .75);
    });
    graph_world.animation->pose_validator({});
    check(graph_world.config() == live_config && graph_world.time() == live_clock,
          "Rejected graph pose changed runtime configuration or time");
    same_pose(graph_world.current_pose(), live_good);
    (void)AnimationPlaybackAccess::tune_graph_input(*graph_world.animation, graph_world.ref(),
                                                    "graph_override_1", false);
    same_pose(graph_world.current_pose(), baseline);
    RuntimeWorld routed({}, {}, std::nullopt, root, false);
    routed.scene.restore_snapshot(preview_source);
    const auto routed_animation = animation_runtime(routed.engine.world());
    const auto routed_deadline = std::chrono::steady_clock::now() + 10s;
    while (!routed_animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < routed_deadline,
              "Routed graph admission timed out");
        std::this_thread::sleep_for(1ms);
    }
    check(routed_animation->seek(graph_world.ref(), 1.25, 1), "Routed graph seek rejected");
    (void)routed.tune_property(graph_world.ref(), "forge.animator", "graph_override_1", true);
    (void)routed.tune_property(graph_world.ref(), "forge.animator", "graph_input_1", .25);
    same_pose(routed_animation->presentation(routed.scene.entity(actor.str()).id(), 1),
              preview_good, "Routed input versus copied preview");
    rejects(
        [&] { (void)routed.tune_property(graph_world.ref(), "forge.animator", "graph", graph); });
    const auto before = graph_world.time();
    auto c = graph_world.config();
    c.graph_input_1 = .9f; // An unchecked override must preserve the source's .5.
    graph_world.config(c);
    graph_world.ready();
    check(graph_world.time() == before, "Changing a disabled graph override advanced its clock");
    same_pose(graph_world.pose(.5), baseline);
    c.graph_override_1 = true;
    c.graph_input_1 = .25f;
    graph_world.config(c);
    graph_world.ready();
    auto expected = reference.config();
    expected.blend_weight = .25f;
    reference.config(expected);
    reference.ready();
    same_pose(graph_world.pose(.5), reference.pose(.5));
    const auto good = graph_world.config();
    c.graph_override_2 = true;
    graph_world.config(c);
    rejects([&] { graph_world.ready(); });
    graph_world.config(good);
    graph_world.ready();
    same_pose(graph_world.pose(.5), reference.pose(.5));
    c = good;
    c.blend_space.id = AssetId::generate();
    graph_world.config(c);
    rejects([&] { graph_world.ready(); });
    graph_world.config(good);
    graph_world.ready();
    same_pose(graph_world.pose(.5), reference.pose(.5));
    c = good;
    c.graph_override_1 = false;
    graph_world.config(c);
    graph_world.ready();
    same_pose(graph_world.pose(.5), baseline);
    check(graph_world.animation->checkpoint_ready(),
          "A fully admitted graph is not checkpoint ready");
    const auto checkpoint = graph_world.animation->checkpoint();
    check(checkpoint.at("version") == 9 && checkpoint.at("animation_profile") == 3,
          "Graph recovery did not preserve its underlying animation profile");
    World recovered(root, graph_world.scene.document(), actor);
    recovered.animation->restore(checkpoint);
    same_pose(recovered.current_pose(), graph_world.current_pose());
    const auto recovered_time = recovered.time();
    auto invalid_checkpoint = checkpoint;
    invalid_checkpoint["entries"][0]["graph"]["inputs"][0] = .75;
    rejects([&] { recovered.animation->restore(invalid_checkpoint); });
    check(recovered.time() == recovered_time, "Rejected graph recovery changed its clock");
    same_pose(recovered.current_pose(), graph_world.current_pose());
    invalid_checkpoint = checkpoint;
    invalid_checkpoint["version"] = 3;
    invalid_checkpoint.erase("animation_profile");
    rejects([&] { recovered.animation->restore(invalid_checkpoint); });
    invalid_checkpoint = checkpoint;
    invalid_checkpoint["entries"][0]["graph"]["revision"] = std::string(64, '0');
    rejects([&] { recovered.animation->restore(invalid_checkpoint); });
    // Graph root motion retains the existing per-layer interval policy. This
    // additive DAG must agree with ordinary masked additive playback, including
    // an input change and a loop seam at a differently sized graph cycle.
    World motion(root, source, actor), motion_reference(root, manual, actor);
    auto motion_config = motion.config();
    motion_config.root_motion = 2;
    motion.config(motion_config);
    motion.ready();
    auto reference_config = motion_reference.config();
    reference_config.root_motion = 2;
    motion_reference.config(reference_config);
    motion_reference.ready();
    (void)motion.pose(0);
    (void)motion_reference.pose(0);
    const auto clock_scale =
        motion_reference.animation->playback(motion_reference.ref())->duration /
        motion.animation->playback(motion.ref())->duration;
    const auto interval = [&](float advance) {
        motion.step(advance);
        motion_reference.step(float(advance * clock_scale));
        const auto actual = motion.animation->root_motion(motion.ref());
        const auto expected = motion_reference.animation->root_motion(motion_reference.ref());
        check(actual && expected && actual->entity == motion.ref() &&
                  actual->tick == motion.tick_id && !actual->applied,
              "Graph extraction lost its fixed-tick observation");
        for (unsigned i = 0; i < 3; ++i)
            check(std::abs(actual->translation[i] - expected->translation[i]) < .00002,
                  "Graph root displacement differs from existing per-layer interval semantics");
        double dot = 0;
        for (unsigned i = 0; i < 4; ++i)
            dot += actual->rotation[i] * expected->rotation[i];
        check(std::abs(std::abs(dot) - 1) < .00002,
              "Graph root rotation differs from existing per-layer interval semantics");
    };
    interval(.25f);
    motion_config.graph_override_1 = true;
    motion_config.graph_input_1 = .25f;
    reference_config.blend_weight = .25f;
    motion.config(motion_config);
    motion.ready();
    motion_reference.config(reference_config);
    motion_reference.ready();
    interval(.25f);
    (void)motion.pose(.9);
    (void)motion_reference.pose(.9);
    interval(.5f);
    graph_world.config(reference.config());
    graph_world.ready();
    check(graph_world.animation->playback(graph_world.ref())->duration ==
              reference.animation->playback(reference.ref())->duration,
          "Leaving graph mode retained its graph cycle clock");
    same_pose(graph_world.pose(.5), reference.pose(.5));
    return {std::move(source), actor};
}
void camera_profile_recovery(const std::filesystem::path& root, const Json& source, EntityId actor,
                             AssetId primary_clip) {
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(source);
    const auto create = [&](const char* recipe, const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", recipe}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto controller = create("animation.sequence", "Graph camera director");
    auto sequence = scene.entity(controller).get<AnimationSequence>();
    sequence.duration = 3;
    scene.entity(controller).set(sequence);
    const EntityRef group{scene.asset_id(), EntityId::parse(controller)};
    for (unsigned i = 0; i < 2; ++i) {
        const auto camera = create("render.camera", i == 0 ? "Outgoing camera" : "Incoming camera");
        auto lens = scene.entity(camera).get<Camera>();
        lens.enabled = false;
        scene.entity(camera).set(lens).set<LocalTranslation>({double(i) * 10, 0, 0});
        const auto shot =
            create("animation.camera_shot", i == 0 ? "Outgoing shot" : "Incoming shot");
        scene.entity(shot).set<CameraShot>({group,
                                            {scene.asset_id(), EntityId::parse(camera)},
                                            i == 0 ? 0. : 1.,
                                            i == 0 ? 1. : 3.,
                                            true,
                                            i == 0 ? 0. : 1.});
        scene.reparent_entity(shot, controller, ReparentMode::KeepLocal);
    }
    const auto authored = scene.document();
    World live(root, authored, actor);
    AnimationPlaybackAccess::control(*live.animation, group,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    const auto expected = live.pose(.25);
    const auto checkpoint = live.animation->checkpoint();
    check(checkpoint.at("version") == 9 && checkpoint.at("animation_profile") == 8 &&
              checkpoint.at("sequence_profile") == 4 && checkpoint.at("camera_shots").size() == 2,
          "Graph wrapper lost its camera-blend/sequence recovery profiles");
    World restored(root, authored, actor);
    restored.animation->restore(checkpoint);
    same_pose(restored.current_pose(), expected, "Graph plus camera recovery");
    check(AnimationPlaybackAccess::status(*restored.animation, group).at("time") == 1.5 &&
              restored.animation->checkpoint() == checkpoint,
          "Graph recovery changed camera clocks or exact nested checkpoint data");
    for (const auto* field : {"animation_profile", "sequence_profile"}) {
        auto invalid = checkpoint;
        invalid[field] = field == std::string_view("animation_profile") ? 7u : 3u;
        rejects([&] { restored.animation->restore(invalid); });
        check(restored.animation->checkpoint() == checkpoint,
              "Rejected graph/camera profile changed an admitted checkpoint");
    }
    const auto sibling = authoring_command(scene, "entity.duplicate", {{"entity", actor.str()}})
                             .at("selected")
                             .get<std::string>();
    auto track = scene.entity(sibling).get<Animator>();
    track.clip = {primary_clip};
    track.graph = {};
    track.states = {};
    track.blend_space = {};
    track.blend_clip = {};
    track.blend_weight = 0;
    track.blend_mode = 0;
    track.blend_root = {};
    track.blend_mask = {};
    track.root_motion = 0;
    track.sequence = group;
    track.sequence_next_clip = track.clip;
    track.sequence_next_start_seconds = 1;
    scene.entity(sibling).set(track);
    const auto compound_source = scene.document();
    World compound(root, compound_source, actor);
    AnimationPlaybackAccess::control(*compound.animation, group,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    const auto graph_pose = compound.pose(.25);
    const auto sibling_pose =
        compound.animation->presentation(compound.scene.entity(sibling).id(), 1);
    const auto compound_checkpoint = compound.animation->checkpoint();
    check(compound_checkpoint.at("version") == 10 &&
              compound_checkpoint.at("skeletal_sequence_profile") == 9 &&
              compound_checkpoint.at("animation_profile") == 8 &&
              compound_checkpoint.at("sequence_profile") == 4,
          "Skeletal recovery wrapper lost graph/camera sequence profiles");
    World compound_restored(root, compound_source, actor);
    compound_restored.animation->restore(compound_checkpoint);
    same_pose(compound_restored.current_pose(), graph_pose, "Graph under skeletal recovery");
    same_pose(
        compound_restored.animation->presentation(compound_restored.scene.entity(sibling).id(), 1),
        sibling_pose, "Skeletal sibling recovery beside graph");
    check(compound_restored.animation->checkpoint() == compound_checkpoint,
          "Combined graph/camera/skeletal recovery changed the exact checkpoint");
    auto invalid_compound = compound_checkpoint;
    invalid_compound["skeletal_sequence_profile"] = 8;
    rejects([&] { compound_restored.animation->restore(invalid_compound); });
    check(compound_restored.animation->checkpoint() == compound_checkpoint,
          "Rejected combined wrapper changed an admitted graph/skeletal world");
    const auto child_id = create("animation.sequence", "Scaled skeletal child");
    AnimationSequence child;
    child.duration = 4;
    child.sequence = group;
    child.sequence_start_seconds = .25;
    child.sequence_rate = 2;
    scene.entity(child_id).set(child);
    track.sequence = scene.reference(child_id);
    scene.entity(sibling).set(track);
    const auto rated_source = scene.document();
    World rated(root, rated_source, actor);
    AnimationPlaybackAccess::control(*rated.animation, group,
                                     {{"operation", "seek"}, {"position_seconds", 1.}});
    const auto rated_graph = rated.pose(.25);
    const auto rated_sibling = rated.animation->presentation(rated.scene.entity(sibling).id(), 1);
    same_pose(rated_sibling, sibling_pose, "Scaled skeletal track beside graph/camera");
    const auto rated_checkpoint = rated.animation->checkpoint();
    check(rated_checkpoint.at("version") == 11 &&
              rated_checkpoint.at("sequence_rate_profile") == 10 &&
              rated_checkpoint.at("skeletal_sequence_profile") == 9 &&
              rated_checkpoint.at("animation_profile") == 8 &&
              rated_checkpoint.at("sequence_profile") == 7,
          "Placement rates lost composed skeletal/graph/camera/nested recovery profiles");
    World rated_cold(root, rated_source, actor);
    rated_cold.animation->restore(rated_checkpoint);
    same_pose(rated_cold.current_pose(), rated_graph, "Graph under placement-rate recovery");
    same_pose(rated_cold.animation->presentation(rated_cold.scene.entity(sibling).id(), 1),
              rated_sibling, "Scaled skeletal sibling recovery");
    check(rated_cold.animation->checkpoint() == rated_checkpoint,
          "Placement-rate compound recovery changed clocks or admitted poses");
    for (const char* field :
         {"sequence_rate_profile", "skeletal_sequence_profile", "animation_profile"}) {
        auto invalid = rated_checkpoint;
        invalid[field] = 1u;
        rejects([&] { rated_cold.animation->restore(invalid); });
        check(rated_cold.animation->checkpoint() == rated_checkpoint,
              "Rejected rate wrapper changed a graph/skeletal/camera world");
    }
    const double clip_duration =
        AnimationPlaybackAccess::status(*rated.animation, rated.scene.reference(sibling))
            .at("sequence_clips")
            .at("duration_seconds");
    const auto spacing = clip_duration * .5;
    child.duration = 4.5 * clip_duration + 1;
    scene.entity(child_id).set(child);
    track.sequence_next_start_seconds = spacing;
    for (std::size_t i = 2; i < sequence_clip_limit; ++i)
        track.sequence_extra_clips.push_back({track.clip, i * spacing});
    scene.entity(sibling).set(track);
    const auto collection_source = scene.document();
    World collection(root, collection_source, actor);
    AnimationPlaybackAccess::control(*collection.animation, group,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    const auto collection_graph = collection.pose(.25);
    const auto collection_sibling =
        collection.animation->presentation(collection.scene.entity(sibling).id(), 1);
    const auto collection_checkpoint = collection.animation->checkpoint();
    check(collection_checkpoint.at("version") == 12 &&
              collection_checkpoint.at("sequence_arrangement_profile") == 11 &&
              collection_checkpoint.at("sequence_rate_profile") == 10 &&
              collection_checkpoint.at("skeletal_sequence_profile") == 9 &&
              collection_checkpoint.at("animation_profile") == 8 &&
              collection_checkpoint.at("sequence_profile") == 7,
          "Eight-clip recovery lost a graph/camera/nested/rate profile");
    World collection_cold(root, collection_source, actor, false);
    collection_cold.animation->restore(collection_checkpoint);
    same_pose(collection_cold.current_pose(), collection_graph, "Graph under collection recovery");
    same_pose(
        collection_cold.animation->presentation(collection_cold.scene.entity(sibling).id(), 1),
        collection_sibling, "Eight-clip sibling recovery");
    check(collection_cold.animation->checkpoint() == collection_checkpoint,
          "Cold composed collection recovery changed its exact checkpoint");
    for (const char* field :
         {"sequence_arrangement_profile", "sequence_rate_profile", "skeletal_sequence_profile",
          "animation_profile", "sequence_profile"}) {
        auto invalid = collection_checkpoint;
        invalid[field] = 1u;
        rejects([&] { collection_cold.animation->restore(invalid); });
        check(collection_cold.animation->checkpoint() == collection_checkpoint,
              "Rejected collection wrapper changed an admitted world");
    }
}
void work_budget_cases(const std::filesystem::path& root,
                       const std::shared_ptr<ProjectLease>& lease, AssetImportService& service,
                       const asset_detail::ModelSelection& selected) {
    auto document =
        AnimationGraphDocument::create(lease, "Assets/Work.animgraph.json", [&](AssetId id) {
            auto source = AnimationGraphSource::create(id);
            source.document["skeleton"] = selected.bindings.at("/rig/skeleton");
            const auto leaf = AssetId::generate();
            source.document["nodes"].push_back({{"id", leaf},
                                                {"name", "Shared clip"},
                                                {"kind", "clip"},
                                                {"clip", selected.bindings.at("/animations/0")}});
            auto previous = leaf;
            for (unsigned i = 1; i < animation_graph_node_limit; ++i) {
                const auto node = AssetId::generate();
                source.document["nodes"].push_back({{"id", node},
                                                    {"name", "Layer " + std::to_string(i)},
                                                    {"kind", "blend"},
                                                    {"first", previous},
                                                    {"second", leaf},
                                                    {"mask", nullptr},
                                                    {"weight", .5}});
                previous = node;
            }
            source.document["output"] = previous;
            return source;
        });
    const auto graph = document->source().asset();
    publish(
        service, document->locator(),
        [](auto& c, const auto& p, const auto&) { prepare_animation_graph_publication(c, p); },
        graph);
    const auto catalog = AssetCatalog::open_project(root);
    const auto joints =
        asset_detail::model_skeleton_joints(selected, selected.bindings.at("/rig/skeleton"))
            .assets.size();
    check(joints > 0, "Work-budget fixture has no admitted skeleton joints");
    const auto work_per_player = animation_graph_node_limit * joints;
    const auto count = 8192 / work_per_player;
    check(count > 0 && count < 256 && count * work_per_player <= 8192 &&
              (count + 1) * work_per_player > 8192,
          "Work fixture does not isolate the evaluated-joint budget from the player budget");
    EngineContext author;
    Scene scene(author.world());
    EntityId first;
    const auto place = [&] {
        asset_detail::ModelPlacementOptions options;
        options.clip = {selected.bindings.at("/animations/0")};
        const auto actor = asset_detail::instantiate_model(
            scene, catalog,
            asset_detail::prepare_model_placement(selected, scene.asset_id(), scene.revision(),
                                                  options));
        authoring_command(scene, "property.set_many",
                          {{"entity", actor.str()},
                           {"component", "forge.animator"},
                           {"values", {{"clip", nullptr}, {"graph", graph}}}});
        if (!first)
            first = actor;
    };
    for (std::size_t i = 0; i < count; ++i)
        place();
    World accepted(root, scene.document(), first);
    check(accepted.animation->checkpoint_ready(), "Graph work below the limit was rejected");
    place();
    rejects([&] { World excessive(root, scene.document(), first); });
    check(accepted.animation->checkpoint_ready(),
          "Rejected work changed another World's admitted graphs");
}
void catalog_replacement_cases(const std::filesystem::path& root, const AssetCatalog& catalog,
                               const Json& source, EntityId actor, AssetId graph) {
    World live(root, source, actor);
    const auto before = live.pose(.5);
    const auto clock = live.time();
    auto replacement = catalog;
    std::vector<AssetRecord> records;
    for (const auto& [id, original] : catalog.records()) {
        auto record = original;
        if (id == graph) {
            record.metadata["forge.import"]["key"] = std::string(64, '0');
            record.metadata["forge.import"]["generation"] =
                record.metadata["forge.import"]["generation"].get<std::uint64_t>() + 1;
        }
        records.push_back(std::move(record));
    }
    replacement.replace_all(std::move(records));
    live.animation->catalog(std::make_shared<const AssetCatalog>(std::move(replacement)));
    // Last-good playback stays available while a replacement is preparing.
    // Pump until its asynchronous failed selection reaches the world owner.
    bool rejected = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!rejected && std::chrono::steady_clock::now() < deadline) {
        try {
            (void)live.animation->prepare_initial_pose();
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected)
            std::this_thread::sleep_for(1ms);
    }
    check(rejected, "Missing graph replacement did not report its failed selection");
    check(live.time() == clock && !live.animation->checkpoint_ready(),
          "Failed graph replacement changed its clock or admitted incompatible recovery");
    same_pose(live.current_pose(), before, "Failed graph replacement last good pose");
    live.animation->catalog(std::make_shared<const AssetCatalog>(catalog));
    rejects([&] { live.ready(); }); // The old publication generation cannot replace a newer one.
    check(live.time() == clock, "Stale graph publication moved its retained clock");
    auto republished = catalog;
    records.clear();
    for (const auto& [id, original] : catalog.records()) {
        auto record = original;
        if (id == graph)
            record.metadata["forge.import"]["generation"] =
                record.metadata["forge.import"]["generation"].get<std::uint64_t>() + 2;
        records.push_back(std::move(record));
    }
    republished.replace_all(std::move(records));
    live.animation->catalog(std::make_shared<const AssetCatalog>(std::move(republished)));
    constexpr float dt = 1.f / 60;
    std::size_t steps = 0;
    const auto recovery_deadline = std::chrono::steady_clock::now() + 5s;
    while (!live.animation->checkpoint_ready() &&
           std::chrono::steady_clock::now() < recovery_deadline) {
        live.step(dt); // Replacements commit at the existing fixed-step boundary.
        ++steps;
        if (!live.animation->checkpoint_ready())
            std::this_thread::sleep_for(1ms);
    }
    check(live.animation->checkpoint_ready(), "Newer graph republication did not finish");
    const auto duration = live.animation->playback(live.ref())->duration;
    const auto expected = std::fmod(clock + steps * double(dt), duration);
    check(steps > 0 && std::abs(live.time() - expected) < 1e-6,
          "Graph replacement did not preserve phase across fixed steps");
    World reference(root, source, actor);
    same_pose(live.current_pose(), reference.pose(expected / duration), "Restored graph selection");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 5 || (argc == 6 && std::string_view(argv[5]) == "--prepare"),
              "Need scratch, model worker, converter and original model source");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        std::filesystem::copy_file(argv[4], root / "Assets/Rig.gltf");
        const auto lease = std::make_shared<ProjectLease>(root);
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(asset_detail::model_importer(std::filesystem::absolute(argv[2]),
                                                   std::filesystem::absolute(argv[3])));
        registry->add(animation_mask_import_registry()->find("forge.animation.mask"));
        registry->add(animation_graph_import_registry()->find("forge.animation.graph"));
        registry->seal();
        AssetImportService service(lease, registry, {"linux", "none", "cpu"}, 1);
        const auto model =
            publish(service, "Assets/Rig.gltf", [](auto& c, const auto& p, const auto& catalog) {
                prepare_model_publication(c, p, catalog);
            });
        auto catalog = AssetCatalog::open_project(root);
        const auto selected = asset_detail::load_model_selection(root, catalog, model);
        const auto skeleton = selected.bindings.at("/rig/skeleton");
        auto mask = AnimationMaskDocument::create(lease, "Assets/Upper.mask.json", [&](auto id) {
            auto source = AnimationMaskSource::create(id);
            source.document["skeleton"] = skeleton;
            source.document["default_weight"] = .75;
            return source;
        });
        const auto mask_id = mask->source().asset();
        publish(
            service, mask->locator(),
            [](auto& c, const auto& p, const auto&) { prepare_animation_mask_publication(c, p); },
            mask_id);
        auto document =
            AnimationGraphDocument::create(lease, "Assets/Layered.animgraph.json", [&](auto id) {
                auto source = AnimationGraphSource::create(id);
                const auto first = AssetId::generate(), second = AssetId::generate(),
                           output = AssetId::generate();
                source.document["skeleton"] = skeleton;
                source.document["inputs"] = {
                    {{"slot", 1}, {"name", "Upper body"}, {"initial", .5}}};
                source.document["nodes"] = {{{"id", output},
                                             {"name", "Upper body"},
                                             {"kind", "additive"},
                                             {"first", first},
                                             {"second", second},
                                             {"mask", mask_id},
                                             {"weight", {{"input", 1}, {"scale", 1}, {"bias", 0}}}},
                                            {{"id", first},
                                             {"name", "Base"},
                                             {"kind", "clip"},
                                             {"clip", selected.bindings.at("/animations/0")}},
                                            {{"id", second},
                                             {"name", "Overlay"},
                                             {"kind", "clip"},
                                             {"clip", selected.bindings.at("/animations/1")}}};
                source.document["output"] = output;
                return source;
            });
        source_bounds(*document);
        const auto graph_id = document->source().asset();
        document->edit(document->revision(), "Set cycle", [](auto& d) {
            d["cycle_seconds"] = 2.5;
            d["opaque"] = {{"wide", UINT64_MAX - 7}};
        });
        check(document->dirty() && document->can_undo(),
              "Graph source did not retain ordinary history");
        document->undo();
        check(!document->dirty() && document->can_redo(),
              "Graph source Undo did not restore baseline");
        document->redo();
        document->save();
        AnimationGraphDocument reopened(lease, document->locator());
        check(reopened.source().document == document->source().document && !reopened.dirty(),
              "Saved graph did not reopen exactly");
        const auto graph_prepare = [](auto& c, const auto& p, const auto&) {
            prepare_animation_graph_publication(c, p);
        };
        publish(service, document->locator(), graph_prepare, graph_id);
        catalog = AssetCatalog::open_project(root);
        const auto record = catalog.records().at(graph_id);
        check(record.type == AnimationGraphAsset::type && !record.subasset &&
                  record.dependency_edges.size() == 4,
              "Graph publication changed root ownership or dependency count");
        const auto key = record.metadata.at("forge.import").at("key").get<std::string>();
        auto cache = DerivedDataCache::open_existing(root);
        const auto artifact = cache.load_selected(key, [&](const auto& value) {
            check(value.files.size() == 1 && value.files.front().name == "graph.json",
                  "Wrong graph artifact files");
            const auto graph = decode_animation_graph(value.files.front().bytes, graph_id);
            check(graph.skeleton == skeleton && graph.cycle_seconds == 2.5 &&
                      graph.nodes.size() == 3 && graph.nodes.back().mask.id == mask_id,
                  "Graph selected artifact lost its bindings, timing or mask");
            const auto admitted = admit_animation_graph(value, record);
            check(admitted.skeleton_revision == selected.revision && admitted.clips.size() == 2 &&
                      admitted.masks.size() == 1 && admitted.masks.contains(mask_id),
                  "Graph resource admission lost exact dependency revisions");
        });
        auto wrong_record = record;
        wrong_record.metadata["forge.import"]["generation"] = -1;
        rejects([&] { (void)admit_animation_graph(artifact, wrong_record); });
        wrong_record = record;
        wrong_record.metadata["forge.animation_graph"]["sha256"] = std::string(64, '0');
        rejects([&] { (void)admit_animation_graph(artifact, wrong_record); });
        wrong_record = record;
        wrong_record.dependency_edges.pop_back();
        rejects([&] { (void)admit_animation_graph(artifact, wrong_record); });
        // Exercise the graph format owner's role validation independently of the
        // cache's earlier immutable-manifest digest check.
        auto wrong_dependency = artifact;
        wrong_record = record;
        wrong_record.dependency_edges.front().role = "unknown";
        wrong_dependency.manifest["inputs"]["dependencies"] = wrong_record.dependency_edges;
        rejects([&] { (void)admit_animation_graph(wrong_dependency, wrong_record); });
        const auto mask_record = catalog.records().at(mask_id);
        const auto mask_key = mask_record.metadata.at("forge.import").at("key").get<std::string>();
        const auto mask_artifact = cache.load_selected(mask_key, [&](const auto& value) {
            const auto admitted = admit_animation_mask(value, mask_record);
            check(admitted.mask.skeleton == skeleton &&
                      admitted.skeleton_revision == selected.revision,
                  "Shared mask admission changed its skeleton binding");
        });
        if (argc == 6) {
            std::cout << path_utf8(root) << '\n';
            return 0;
        }
        auto corrupt = mask_record;
        corrupt.metadata["forge.import"]["importer_revision"] = "unknown";
        rejects([&] { (void)admit_animation_mask(mask_artifact, corrupt); });
        auto wrong_key = mask_artifact;
        wrong_key.key.assign(64, '0');
        rejects([&] { (void)admit_animation_mask(wrong_key, mask_record); });
        // The importer must reject a changed dependency selection before cooking,
        // rather than silently compiling the source against a different rig.
        auto request = service.prepare(document->locator());
        const auto plan = request.importer->discover(request.request, {});
        auto wrong_plan = plan;
        wrong_plan.input.dependencies.front().revision.assign(64, '0');
        rejects(
            [&] { (void)request.importer->import_and_cook(request.request, wrong_plan, {}, {}); });
        auto cancelled = std::stop_source{};
        cancelled.request_stop();
        rejects([&] { (void)request.importer->discover(request.request, cancelled.get_token()); });
        const auto [runtime_source, actor] =
            runtime_cases(root, catalog, selected, mask_id, graph_id);
        camera_profile_recovery(root, runtime_source, actor, selected.bindings.at("/animations/0"));
        catalog_replacement_cases(root, catalog, runtime_source, actor, graph_id);
        work_budget_cases(root, lease, service, selected);
        const auto exported = root.parent_path() / (root.filename().string() + "-export");
        const std::array roots{graph_id, model};
        package_runtime_content(root, exported, roots, {"linux", "none"});
        const auto moved = root.parent_path() / (root.filename().string() + "-moved");
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"linux", "none"});
        check(shipped.records().contains(graph_id) && shipped.records().contains(mask_id) &&
                  !std::filesystem::exists(moved / document->locator()) &&
                  !std::filesystem::exists(moved / "Assets/Rig.gltf"),
              "Graph content closure lost runtime dependencies or included authoring sources");
        World relocated(moved, runtime_source, actor), original(root, runtime_source, actor);
        same_pose(relocated.pose(.5), original.pose(.5));
        std::filesystem::remove(root / "Assets/Rig.gltf");
        std::filesystem::remove(root / document->locator());
        std::filesystem::remove(root / mask->locator());
        World cold(root, runtime_source, actor);
        check(!cold.pose(.5).at("local").empty(), "Cold graph playback lost its source-free pose");
        const auto retained = decode_animation_graph(artifact.files.front().bytes, graph_id);
        check(retained.skeleton == skeleton && retained.nodes.size() == 3,
              "Owned cooked graph depends on authoring source lifetime");
        ResourcePool<AnimationGraphAsset> graphs({1, 8, 16, 1024 * 1024});
        const auto ticket = request_animation_graph(graphs, root, record);
        check(graphs.wait(ticket, 5s), "Cold selected graph request failed after source removal");
        const auto held = graphs.acquire(ticket);
        ResourcePool<AnimationGraphAsset> tight({1, 1, 2, 1});
        const auto over_budget = request_animation_graph(tight, root, record);
        check(!tight.wait(over_budget, 5s) && !tight.acquire(over_budget) && held,
              "An over-budget graph became resident or invalidated another pool's live lease");
        check(held && held->graph.skeleton == skeleton && held->graph.nodes.size() == 3 &&
                  graphs.statistics().memory.animation == held->resident_bytes(),
              "Typed graph resource lost its values or charged the wrong memory owner");
        const auto missing_root = root / "missing-cache";
        std::filesystem::create_directory(missing_root);
        ResourcePool<AnimationGraphAsset> missing({1, 8, 16, 1024 * 1024});
        const auto unavailable = request_animation_graph(missing, missing_root, record);
        check(!missing.wait(unavailable, 5s) && !std::filesystem::exists(missing_root / ".forge"),
              "A selected graph read created a missing cache or admitted unavailable bytes");
        std::cout << "Animation Graph source history/bounds/import/mask/dependencies/cancellation "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
