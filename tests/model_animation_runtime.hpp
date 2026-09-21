#pragma once
#include "animation_resource.hpp"
#include <forge/animation.hpp>
#include <forge/runtime.hpp>
#include <thread>
// Exercises the real cooked model publication through CPU resources and the
// existing animation module. Geometry rendering is tested by the render provider.
void model_animation_runtime(const std::filesystem::path& project, const AssetCatalog& catalog,
                             const std::map<std::string, AssetId>& members) {
    using namespace forge::animation_detail;
    const AssetRef<SkeletonAsset> skeleton{members.at("/rig/skeleton")};
    const AssetRef<AnimationClipAsset> clip{members.at("/animations/0")};
    const auto snapshot = std::make_shared<const AssetCatalog>(catalog);
    ModelAnimationResources resources(project);
    auto request = resources.request(snapshot, skeleton, clip);
    auto coalesced = resources.request(snapshot, skeleton, clip);
    require(request.skeleton_ticket.inspect().identity ==
                    coalesced.skeleton_ticket.inspect().identity &&
                request.clip_ticket.inspect().identity == coalesced.clip_ticket.inspect().identity,
            "Model animation requests did not coalesce");
    require(!resources.acquire(request), "Background resource adopted outside owner pump");
    require(resources.prepare_recovery(request, 10s), "Model animation resources timed out");
    auto held = resources.acquire(request);
    require(bool(held) && held.skeleton->joint_nodes == std::vector<std::size_t>{0, 1, 2},
            "Native model joint mapping lost");
    require(held.clip->has_transform_channels && held.clip->transform_channels.size() == 1 &&
                held.clip->transform_channels[0].node == 1 &&
                held.clip->transform_channels[0].path == AnimatedTransformPath::Translation,
            "Clip resource lost independent animated translation intent");
    require(held.skeleton->joint_assets == std::vector<AssetId>{members.at("/nodes/0"),
                                                                members.at("/nodes/1"),
                                                                members.at("/nodes/2")},
            "Skeleton resource lost durable node mapping");
    require(resources.skeleton_statistics().memory.animation > 0 &&
                resources.clip_statistics().memory.animation > 0,
            "Animation resident allocations not accounted");
    {
        Sampler sampler(held.skeleton->native, held.clip->native);
        sampler.sample(.5f);
        const auto local = sampler.local_pose();
        require(local.size() == 3 && std::abs(local[1].translation[0] - .5f) < .002f &&
                    std::abs(local[1].translation[1] - 1.f) < .002f,
                "Cooked model clip local pose differs");
        sampler.reset();
        rejects([&] { sampler.local_pose(); });
    }
    auto wrong = catalog;
    auto bad = wrong.records().at(clip.id);
    bad.dependency_edges[0].target = AssetId::generate();
    bad.dependencies = {bad.dependency_edges[0].target};
    wrong.replace(bad);
    rejects(
        [&] { resources.request(std::make_shared<const AssetCatalog>(wrong), skeleton, clip); });
    // A new publication generation may reuse the same cooked bytes. Both typed
    // tickets must change together; old consumer leases keep their own objects.
    auto publication = [&](std::uint64_t generation) {
        auto updated = catalog;
        for (const auto& [id, value] : catalog.records()) {
            if (id != request.model && (!value.subasset || value.subasset->owner != request.model))
                continue;
            auto record = value;
            record.metadata["forge.import"]["generation"] = generation;
            updated.replace(std::move(record));
        }
        return std::make_shared<const AssetCatalog>(std::move(updated));
    };
    auto replacement = resources.request(publication(request.generation + 1), skeleton, clip);
    require(resources.prepare_recovery(replacement, 10s), "Model replacement did not load");
    auto newer = resources.acquire(replacement);
    require(bool(newer) && newer.skeleton.identity() != held.skeleton.identity() &&
                newer.clip.identity() != held.clip.identity() &&
                held.clip->native->info().duration == 1,
            "Replacement retargeted an old lease or mixed generations");
    auto weak = newer.clip.weak();
    const auto selected = load_model_selection(project, catalog, request.model);
    const auto mesh_resource = model_mesh_resource(selected, {members.at("/meshes/0")});
    require(mesh_resource.model && mesh_resource.model->model == request.model &&
                mesh_resource.model->revision == request.revision &&
                mesh_resource.model->nodes.size() == 1 &&
                mesh_resource.model->nodes[0].node == members.at("/nodes/0") &&
                mesh_resource.model->nodes[0].skin == 0 &&
                mesh_resource.model->nodes[0].morph_weights == std::vector<float>{0} &&
                mesh_resource.model->skins.size() == 1 &&
                mesh_resource.model->skins[0].joints ==
                    std::vector<AssetId>{members.at("/nodes/1"), members.at("/nodes/2")} &&
                mesh_resource.model->skins[0].inverse_bind == std::vector<AffineTransform>(2),
            "Model mesh resource lost skin joint order, inverse binds or node default weights");
    const auto artifact = project / ".forge/cache/derived" / request.revision /
                          selected.member(clip.id).artifact.file;
    const auto good_bytes = read_bytes(artifact, 16 * 1024 * 1024);
    auto corrupt = good_bytes;
    corrupt[0] ^= std::byte{1};
    write(artifact, corrupt);
    auto failed = resources.request(publication(request.generation + 2), skeleton, clip);
    require(!resources.prepare_recovery(failed, 10s), "Corrupt model replacement was adopted");
    rejects([&] { resources.acquire(failed); });
    require(newer.clip->native->info().duration == 1 && bool(weak.lock()),
            "Failed replacement destroyed previous good animation");
    write(artifact, good_bytes);
    resources.unload(failed);
    require(held.skeleton->native->info().tracks == 3 &&
                resources.skeleton_statistics().retired >= 1,
            "Unload invalidated a held model animation revision");
    resources.close();
    require(!held.skeleton && !held.clip && !newer.clip && !weak.lock(),
            "Closed animation owner left live leases");
    rejects([&] { held.clip.get(); });
    // Scope cancellation with actual queued native loads must join safely.
    {
        ModelAnimationResources cancelled(project);
        auto pending = cancelled.request(snapshot, skeleton, clip);
        cancelled.unload(pending);
        cancelled.close();
    }
    struct Runtime {
        Module module;
        EngineContext engine;
        Scene scene;
        RuntimeSimulation simulation;
        std::shared_ptr<AnimationRuntime> animation;
        Runtime(const std::filesystem::path& project, bool physics = false)
            : engine(WorldRole::Runtime, false,
                     [&] {
                         std::vector<EngineModule> modules{animation_module(project)};
                         if (physics) {
                             PhysicsConfig config;
                             config.gravity = {0, 0, 0};
                             modules.push_back(physics_module(config));
                         }
                         return modules;
                     }()),
              scene(engine.world()), simulation(engine.world(), scene, module),
              animation(animation_runtime(engine.world())) {}
        Json pose() { return animation->presentation(scene.entity("actor").id(), 1); }
    };
    Json config = {{"skeleton", skeleton.id}, {"clip", clip.id}, {"enabled", true},
                   {"play_on_start", true},   {"loop", false},   {"playback_speed", 1}};
    Json document = {
        {"version", 1},
        {"entities", Json::array({{{"id", "actor"},
                                   {"name", "Actor"},
                                   {"components",
                                    {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                     {"forge.animator", config}}}}})}};
    const auto source_path = project / catalog.records().at(request.model).source;
    auto moved_source = source_path;
    moved_source += ".temporarily-absent";
    std::filesystem::rename(source_path, moved_source);
    auto second_actor = document["entities"][0];
    second_actor["id"] = "actor2";
    second_actor["name"] = "Second actor";
    document["entities"].push_back(std::move(second_actor));
    Runtime runtime(project);
    runtime.scene.restore_snapshot(document);
    runtime.simulation.reset_presentation();
    require(runtime.animation->checkpoint().is_null(), "Pending bindings fabricated recovery");
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    // The paused presentation owner boundary admits resources, without any tick.
    while (runtime.pose().is_null() && std::chrono::steady_clock::now() < deadline) {
        runtime.simulation.presentation(0);
        std::this_thread::sleep_for(1ms);
    }
    require(!runtime.pose().is_null() && runtime.pose().at("time") == 0,
            "Paused model resource loading failed or advanced the clock");
    const auto authored = runtime.scene.snapshot();
    const auto before = runtime.animation->checkpoint();
    for (unsigned i = 0; i < 4; ++i)
        runtime.simulation.presentation(.5);
    require(runtime.animation->checkpoint() == before, "Model presentation advanced playback");
    runtime.simulation.tick(.5f);
    const auto pose = runtime.pose();
    require(std::abs(pose.at("local")[1].at("translation")[0].get<double>() - .5) < .002 &&
                pose.at("joint_nodes") == Json({0, 1, 2}) && pose.contains("model_revision") &&
                runtime.scene.snapshot() == authored,
            "Model playback or authored isolation failed");
    require(pose.at("morphs").size() == 1 && pose.at("morphs")[0].at("node") == 0 &&
                std::abs(pose.at("morphs")[0].at("weights")[0].get<double>() - .5) < .002,
            "Model morphs did not use the same runtime sample time");
    const auto recovery = runtime.animation->checkpoint();
    Runtime recovered(project);
    recovered.scene.restore_snapshot(authored);
    recovered.animation->restore(recovery);
    require(recovered.pose() == runtime.pose(), "Model recovery changed sampled pose");
    auto invalid = recovery;
    invalid["entries"][0]["time"] = .2;
    invalid["entries"][1]["clip_revision"] = "different-family";
    rejects([&] { recovered.animation->restore(invalid); });
    require(recovered.animation->checkpoint() == recovery,
            "Failed multi-player restore changed an earlier player");
    auto empty = authored;
    empty["entities"] = Json::array();
    recovered.scene.restore_snapshot(empty);
    recovered.animation->synchronize();
    require(recovered.animation->checkpoint().at("entries").empty(),
            "Removed model players retained playback state");
    // Real scene-node application uses one root scope per model instance. A
    // nested instance, Parent storage and inherited ModelSource/TRS are included.
    auto bound_document = document;
    for (auto& actor : bound_document["entities"])
        actor["components"]["forge.model_source"] = {{"model", request.model}, {"node", nullptr}};
    bound_document["entities"][1]["parent"] = "actor";
    bound_document["entities"][1]["components"]["forge.animator"]["playback_speed"] = .5;
    for (const auto& [name, parent] :
         std::vector<std::pair<std::string, std::string>>{{"joint", "actor"}, {"joint2", "actor2"}})
        bound_document["entities"].push_back(
            {{"id", name},
             {"name", name},
             {"parent", parent},
             {"components",
              {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 0}}},
               {"forge.model_source",
                {{"model", request.model}, {"node", members.at("/nodes/1")}}}}}});
    Runtime bound(project);
    bound.scene.restore_snapshot(bound_document);
    auto joint = bound.scene.entity("joint"), joint2 = bound.scene.entity("joint2");
    auto base = bound.engine.world()
                    .world()
                    .prefab()
                    .set<LocalRotation>(rotation_from_euler({0, 0, 20}))
                    .set<LocalScale>({2, 3, 4})
                    .set<ModelSource>({{request.model}, {members.at("/nodes/1")}});
    joint.remove<LocalRotation>().remove<LocalScale>().remove<ModelSource>().is_a(base);
    joint2.remove(flecs::ChildOf, flecs::Wildcard)
        .set<flecs::Parent>({bound.scene.entity("actor2").id()});
    bound.simulation.reset_presentation();
    const auto binding_deadline = std::chrono::steady_clock::now() + 10s;
    while (bound.pose().is_null() && std::chrono::steady_clock::now() < binding_deadline) {
        bound.simulation.presentation(0);
        std::this_thread::sleep_for(1ms);
    }
    require(!bound.pose().is_null(), "Model binding resources did not load");
    double pre_physics_x = -1;
    auto monitor = bound.engine.world()
                       .world()
                       .system()
                       .kind(bound.engine.world().world().entity("forge.runtime.PrePhysics"))
                       .immediate()
                       .run([&](flecs::iter&) { pre_physics_x = joint.get<LocalTranslation>().x; });
    monitor.add<FixedSimulation>();
    bound.simulation.tick(.5f);
    require(std::abs(joint.get<LocalTranslation>().x - .5) < .002 &&
                std::abs(joint2.get<LocalTranslation>().x - .25) < .002 &&
                std::abs(pre_physics_x - .5) < .002,
            "Animation did not precede physics or crossed a nested model instance boundary");
    require(joint.owns<LocalTranslation>() && !joint.owns<LocalRotation>() &&
                !joint.owns<LocalScale>() && !joint.owns<ModelSource>() &&
                joint.get<LocalScale>() == LocalScale{2, 3, 4},
            "Translation-only animation materialized unrelated prefab overrides");
    base.set<LocalScale>({3, 4, 5});
    bound.simulation.tick(.1f);
    require(joint.get<LocalScale>() == LocalScale{3, 4, 5},
            "Animated node stopped inheriting scale changes");
    // Moving a node outside its source root must never target the other instance.
    const auto detached_position = joint.get<LocalTranslation>();
    joint.remove(flecs::ChildOf, flecs::Wildcard);
    bound.simulation.tick(.1f);
    require(joint.get<LocalTranslation>() == detached_position &&
                std::abs(joint2.get<LocalTranslation>().x - .35) < .002,
            "Detached node retargeted another model instance");
    joint.child_of(bound.scene.entity("actor"));
    const auto good_binding_checkpoint = bound.animation->checkpoint();
    // Ambiguous provenance rejects the complete instance update and can recover
    // when the conflicting node is removed; no arbitrary first-match binding.
    joint2.remove<flecs::Parent>().child_of(bound.scene.entity("actor"));
    const auto before_bad_binding = joint.get<LocalTranslation>();
    bound.simulation.tick(.1f);
    require(joint.get<LocalTranslation>() == before_bad_binding &&
                !bound.animation->checkpoint_ready(),
            "Ambiguous model binding changed live channels or advertised recovery");
    rejects([&] { bound.animation->restore(good_binding_checkpoint); });
    require(joint.get<LocalTranslation>() == before_bad_binding,
            "Rejected binding recovery wrote an animated channel");
    joint2.child_of(bound.scene.entity("actor2"));
    bound.simulation.tick(.1f);
    require(bound.animation->checkpoint_ready() &&
                std::abs(joint.get<LocalTranslation>().x - .9) < .002,
            "Repaired model binding did not recover");
    const auto repaired_checkpoint = bound.animation->checkpoint();
    joint2.child_of(bound.scene.entity("actor"));
    bound.simulation.tick(.05f);
    require(!bound.animation->checkpoint_ready(), "Repeated invalid binding was not rejected");
    joint2.child_of(bound.scene.entity("actor2"));
    bound.animation->restore(repaired_checkpoint);
    require(bound.animation->checkpoint_ready() &&
                bound.animation->checkpoint() == repaired_checkpoint,
            "Valid binding recovery retained a stale error or changed checkpoint values");
    monitor.destruct();
    auto physics_document = document;
    physics_document["entities"].erase(physics_document["entities"].begin() + 1,
                                       physics_document["entities"].end());
    physics_document["entities"][0]["components"]["forge.model_source"] = {{"model", request.model},
                                                                           {"node", nullptr}};
    physics_document["entities"].push_back(
        {{"id", "joint"},
         {"name", "Joint"},
         {"parent", "actor"},
         {"components",
          {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 0}}},
           {"forge.model_source", {{"model", request.model}, {"node", members.at("/nodes/1")}}}}}});
    auto wait_for_model = [&](Runtime& runtime) {
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (runtime.pose().is_null() && std::chrono::steady_clock::now() < until) {
            runtime.simulation.presentation(0);
            std::this_thread::sleep_for(1ms);
        }
        require(!runtime.pose().is_null(), "Animated physics resources did not load");
    };
    for (bool realize_first : {false, true})
        for (unsigned motion : {1u, 2u}) {
            Runtime physical(project, true);
            physical.scene.restore_snapshot(physics_document);
            auto node = physical.scene.entity("joint");
            node.set<PhysicsBody>({motion}).set<BoxCollider>({}).set<SpatialBinding>(
                {SpatialMode::World, {}});
            auto physics = std::static_pointer_cast<PhysicsRuntime>(
                physical.engine.world().services().physics());
            if (realize_first)
                physics->synchronize(0);
            wait_for_model(physical);
            const auto before = physical.engine.world().get_local_transform(node);
            physical.simulation.tick(.25f);
            if (motion == 2) {
                require(physical.engine.world().get_local_transform(node) == before &&
                            !physical.animation->checkpoint_ready(),
                        "Animation wrote a solver-owned Dynamic pose before rejection");
                node.set<PhysicsBody>({1});
                physical.simulation.tick(.25f);
                require(physical.animation->checkpoint_ready() &&
                            std::abs(node.get<LocalTranslation>().x - .5) < .002,
                        "Repairing animated body ownership did not resume playback");
            } else {
                require(physical.animation->checkpoint_ready() &&
                            std::abs(node.get<LocalTranslation>().x - .25) < .002,
                        "Valid animated Kinematic body was rejected");
                const auto checkpoint = physical.animation->checkpoint();
                {
                    Runtime restored(project, true);
                    restored.scene.restore_snapshot(physical.scene.snapshot());
                    auto restored_physics = std::static_pointer_cast<PhysicsRuntime>(
                        restored.engine.world().services().physics());
                    restored_physics->restore(physics->checkpoint());
                    restored.animation->restore(checkpoint);
                    restored.simulation.reset_presentation();
                    restored.simulation.tick(.1f);
                    require(restored.animation->checkpoint_ready() &&
                                std::abs(restored.scene.entity("joint").get<LocalTranslation>().x -
                                         .35) < .002,
                            "Valid physics-aware animation reconstruction did not resume");
                }
                node.set<PhysicsBody>({2});
                physics->synchronize(0);
                auto invalid_recovery = checkpoint;
                invalid_recovery["entries"][0]["time"] = .75;
                const auto pose = physical.engine.world().get_local_transform(node);
                rejects([&] { physical.animation->restore(invalid_recovery); });
                require(physical.animation->checkpoint() == checkpoint &&
                            physical.engine.world().get_local_transform(node) == pose,
                        "Physics-rejected animation recovery changed playback or transforms");
            }
        }
    {
        Runtime scaled(project, true);
        auto input = physics_document;
        input["entities"][0]["components"]["forge.animator"]["clip"] = members.at("/animations/1");
        input["entities"][1]["components"]["forge.model_source"]["node"] = members.at("/nodes/2");
        scaled.scene.restore_snapshot(input);
        auto node = scaled.scene.entity("joint");
        node.set<PhysicsBody>({1}).set<SphereCollider>({}).set<SpatialBinding>(
            {SpatialMode::World, {}});
        auto physics =
            std::static_pointer_cast<PhysicsRuntime>(scaled.engine.world().services().physics());
        physics->synchronize(0);
        wait_for_model(scaled);
        const auto before = scaled.engine.world().get_local_transform(node);
        scaled.simulation.tick(.5f); // Scale channel requests(.5,1,1.5): not a sphere scale.
        require(scaled.engine.world().get_local_transform(node) == before &&
                    !scaled.animation->checkpoint_ready() && physics->status().at("bodies") == 1,
                "Invalid animated sphere scale mutated ECS or destroyed the retained collider");
        node.remove<PhysicsBody>().remove<SphereCollider>();
        scaled.simulation.tick(.1f);
        const auto scale = node.get<LocalScale>();
        require(scaled.animation->checkpoint_ready() && std::abs(scale.x - .6f) < .002f &&
                    std::abs(scale.y - 1.2f) < .002f && std::abs(scale.z - 1.8f) < .002f &&
                    node.get<LocalTranslation>() == before.translation,
                "Removing incompatible physics did not restore visual-only scale animation");
    }
    std::filesystem::rename(moved_source, source_path);
}
