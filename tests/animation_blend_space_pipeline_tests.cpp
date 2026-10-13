#include "animation_blend_space_authoring.hpp"
#include "animation_blend_space_resource.hpp"
#include "animation_preview.hpp"
#include "asset_reference_impact.hpp"
#include "model_placement.hpp"
#include "runtime_package.hpp"
#include <array>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool v, const std::string& why) {
    if (!v)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .003,
            "Blend Space numeric mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
}
template <class F> void rejects(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid Blend Space replacement/recovery admitted");
}
struct World {
    EngineContext engine;
    Scene scene;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    std::uint64_t tick_id = 0;
    World(const std::filesystem::path& p, const Json& source, EntityId id, bool prepare = true)
        : engine(WorldRole::Runtime, false, {animation_module(p)}), scene(engine.world()),
          animation(animation_runtime(engine.world())), actor(id) {
        scene.restore_snapshot(source);
        if (prepare) {
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (!animation->prepare_initial_pose()) {
                require(std::chrono::steady_clock::now() < deadline,
                        "Blend Space admission timeout");
                std::this_thread::sleep_for(1ms);
            }
        }
    }
    flecs::entity entity() { return scene.entity(actor.str()); }
    EntityRef ref() { return scene.reference(actor.str()); }
    void tick(float dt) { animation->tick(dt, ++tick_id); }
    Json pose() { return animation->presentation(entity().id(), 1); }
};
void same(const Json& a, const Json& b) {
    require(a.at("local") == b.at("local") && a.at("morphs") == b.at("morphs"),
            "Blend Space recovery/preview pose differs");
}
void written_pose(World& world) {
    const auto pose = world.pose();
    const auto snapshot = world.scene.effective_document();
    std::size_t checked = 0;
    for (const auto& row : snapshot.at("entities")) {
        const auto& components = row.at("components");
        if (!components.contains("forge.model_source") ||
            components.at("forge.model_source").at("node").is_null())
            continue;
        const auto found = std::find(pose.at("joint_assets").begin(), pose.at("joint_assets").end(),
                                     components.at("forge.model_source").at("node"));
        if (found == pose.at("joint_assets").end())
            continue;
        const auto index = std::size_t(found - pose.at("joint_assets").begin());
        for (const auto& [component, field] :
             std::array{std::pair{"forge.local_translation", "translation"},
                        std::pair{"forge.local_scale", "scale"}})
            for (unsigned c = 0; c < 3; ++c)
                near(components.at(component).at(std::array{"x", "y", "z"}[c]),
                     pose.at("local")[index].at(field)[c]);
        ++checked;
    }
    require(checked == pose.at("joint_assets").size(),
            "Blend Space written-channel check lost joints");
}
void mix(const Json& a, const Json& b, const Json& actual, double w) {
    require(a.at("joint_assets") == actual.at("joint_assets"),
            "Blend Space lost durable joint mapping");
    for (std::size_t j = 0; j < actual.at("local").size(); ++j) {
        for (const auto* field : {"translation", "scale"})
            for (unsigned c = 0; c < 3; ++c)
                near(actual["local"][j][field][c], (1 - w) * a["local"][j][field][c].get<double>() +
                                                       w * b["local"][j][field][c].get<double>());
        double dot = 0, length = 0;
        std::array<double, 4> q{};
        for (unsigned c = 0; c < 4; ++c)
            dot += a["local"][j]["rotation"][c].get<double>() *
                   b["local"][j]["rotation"][c].get<double>();
        for (unsigned c = 0; c < 4; ++c) {
            q[c] = (1 - w) * a["local"][j]["rotation"][c].get<double>() +
                   w * (dot < 0 ? -1 : 1) * b["local"][j]["rotation"][c].get<double>();
            length += q[c] * q[c];
        }
        for (unsigned c = 0; c < 4; ++c)
            near(actual["local"][j]["rotation"][c], q[c] / std::sqrt(length));
    }
}
Json direct(Json source, EntityId actor, AssetId clip) {
    for (auto& row : source["entities"])
        if (row["id"] == Json(actor)) {
            row["components"]["forge.animator"]["blend_space"] = nullptr;
            row["components"]["forge.animator"]["clip"] = clip;
        }
    return source;
}
} // namespace
int main(int argc, char** argv) {
    const char* phase = "source publication";
    try {
        require(argc == 3 || argc == 4, "Need Gallery project and scratch root");
        const auto gallery = std::filesystem::weakly_canonical(argv[1]);
        const auto scratch = std::filesystem::weakly_canonical(argv[2]);
        for (auto at = scratch; !at.empty();) {
            require(at != gallery,
                    "Blend Space scratch root must be outside the Gallery source tree");
            const auto parent = at.parent_path();
            if (parent == at)
                break;
            at = parent;
        }
        const auto project = scratch / AssetId::generate().str();
        std::filesystem::create_directories(project);
        std::filesystem::copy(argv[1], project, std::filesystem::copy_options::recursive);
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type && record.source == "Assets/OverlayColumn.gltf")
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        const auto skeleton = selected.bindings.at("/rig/skeleton");
        const auto rest = selected.bindings.at("/animations/2"),
                   base = selected.bindings.at("/animations/0"),
                   overlay = selected.bindings.at("/animations/1");
        auto lease = std::make_shared<ProjectLease>(project);
        auto document = AnimationBlendSpaceDocument::create(
            lease, "Assets/TestMotion.blendspace.json", [&](AssetId id) {
                auto source = AnimationBlendSpaceSource::create(id);
                source.document["skeleton"] = skeleton;
                source.document["cycle_seconds"] = 2;
                for (const auto& [position, clip] :
                     std::array{std::pair{0, rest}, std::pair{1, base}, std::pair{2, overlay},
                                std::pair{3, base}})
                    source.document["samples"].push_back(
                        {{"id", AssetId::generate()}, {"position", position}, {"clip", clip}});
                return source;
            });
        const auto asset = document->source().asset();
        const auto initial = document->source().document;
        document->edit(document->revision(), "Change cycle",
                       [](Json& d) { d["cycle_seconds"] = 3; });
        document->undo();
        require(document->source().document == initial, "Blend Space source Undo differs");
        document->redo();
        require(document->source().document.at("cycle_seconds") == 3, "Blend Space Redo differs");
        document->undo();
        AssetImportService imports(lease, animation_blend_space_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        auto publish = [&] {
            document->save();
            imports.submit(
                imports.prepare(document->locator(), {}, asset),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_animation_blend_space_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            require(imports.wait_idle(15s), "Blend Space publication timeout");
            const auto receipts = imports.poll();
            require(receipts.size() == 1 && receipts[0].published,
                    receipts.empty() ? "Missing Blend Space receipt" : receipts[0].diagnostic);
            catalog = AssetCatalog::open_project(project);
        };
        publish();
        ResourcePool<AnimationBlendSpaceAsset> pool;
        const auto ticket =
            request_animation_blend_space(pool, project, catalog.records().at(asset));
        require(pool.wait(ticket, 10s) && bool(pool.acquire(ticket)),
                "Typed Blend Space admission failed");
        EngineContext author(WorldRole::Authoring);
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.clip = {base};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
        require(!scene.entity(actor.str()).get<Animator>().blend_space.id &&
                    scene.entity(actor.str()).get<Animator>().blend_position == 0,
                "Older model placement lost Blend Space defaults");
        const auto before = scene.document();
        auto commands = Json::array({{{"operation", "property.set"},
                                      {"arguments",
                                       {{"entity", actor},
                                        {"component", "forge.animator"},
                                        {"field", "blend_space"},
                                        {"value", asset}}}},
                                     {{"operation", "property.set"},
                                      {"arguments",
                                       {{"entity", actor},
                                        {"component", "forge.animator"},
                                        {"field", "blend_position"},
                                        {"value", 1.5}}}}});
        apply_authoring(scene, commands, scene.revision());
        const auto source = scene.document();
        require(scene.undo() && scene.document() == before && scene.redo() &&
                    scene.document() == source,
                "Typed Blend Space Scene history differs");
        if (argc == 4) {
            require(std::string_view(argv[3]) == "--prepare", "Unknown Blend Space fixture mode");
            atomic_write(project / "main.scene.json", source.dump(2));
            std::cout << project.string() << '\n';
            return 0;
        }
        phase = "phase-aligned playback";
        World runtime(project, source, actor),
            rest_world(project, direct(source, actor, rest), actor),
            base_world(project, direct(source, actor, base), actor),
            overlay_world(project, direct(source, actor, overlay), actor);
        runtime.tick(.5f);
        rest_world.tick(.5f);
        base_world.tick(.5f);
        overlay_world.tick(.25f);
        const auto expected = runtime.pose();
        written_pose(runtime);
        near(expected.at("time"), .5);
        near(expected.at("duration"), 2);
        mix(base_world.pose(), overlay_world.pose(), expected, .5);
        near(expected["morphs"][0]["weights"][0], .625);
        near(expected["morphs"][0]["weights"][1], -.0625);
        SkeletalAnimationPreview preview;
        preview.start(project, source, scene.reference(actor.str()));
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!preview.prepare()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Blend Space preview admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        preview.seek(.5);
        same(expected, preview.pose());
        preview.select_blend_position(.5f);
        mix(rest_world.pose(), base_world.pose(), preview.pose(), .5);
        require(scene.document() == source, "Blend Space preview changed authored source");
        preview.stop();
        const auto checkpoint = runtime.animation->checkpoint();
        World recovered(project, runtime.scene.document(), actor, false);
        recovered.animation->restore(checkpoint);
        same(expected, recovered.pose());
        auto invalid = checkpoint;
        invalid["entries"][0]["blend_space"]["revision"] = "foreign";
        rejects([&] { recovered.animation->restore(invalid); });
        require(recovered.animation->checkpoint() == checkpoint,
                "Rejected Blend Space recovery changed clocks");
        phase = "paused pair changes and rollback";
        require(
            runtime.animation->control(runtime.ref(), AnimationControl::Pause, ++runtime.tick_id),
            "Blend Space pause rejected");
        auto c = runtime.entity().get<Animator>();
        for (float position : {2.f, 3.f, -2.f, .5f, 1.5f, 1.25f, 1.75f, 1.5f}) {
            c.blend_position = position;
            runtime.entity().set(c);
            runtime.animation->synchronize();
            require(runtime.animation->checkpoint_ready(),
                    "Explicit paused Blend Space change not admitted");
            near(runtime.pose().at("time"), .5);
            const auto& a = position < 1   ? rest_world.pose()
                            : position < 2 ? base_world.pose()
                            : position < 3 ? overlay_world.pose()
                                           : base_world.pose();
            const auto& b = position < 1   ? base_world.pose()
                            : position < 2 ? overlay_world.pose()
                                           : base_world.pose();
            mix(a, b, runtime.pose(),
                position < 0 || position >= 3 || position == 2 ? 0
                                                               : position - std::floor(position));
            written_pose(runtime);
        }
        const auto held = runtime.pose();
        c.blend_position = 1.25f;
        runtime.entity().set(c);
        runtime.animation->pose_validator(
            [](const auto&) { throw std::runtime_error("Blend Space physics veto"); });
        runtime.animation->synchronize();
        same(held, runtime.pose());
        written_pose(runtime);
        require(!runtime.animation->checkpoint_ready(), "Vetoed Blend Space checkpointed");
        runtime.animation->pose_validator({});
        c.blend_position = 1.5f;
        runtime.entity().set(c);
        runtime.animation->synchronize();
        c.blend_mask.id = AssetId::generate();
        runtime.entity().set(c);
        runtime.animation->synchronize();
        same(held, runtime.pose());
        require(!runtime.animation->checkpoint_ready(), "Space/manual mask conflict admitted");
        c.blend_mask = {};
        runtime.entity().set(c);
        runtime.animation->synchronize();
        phase = "root intervals";
        auto moving = source;
        for (auto& row : moving["entities"])
            if (row["id"] == Json(actor))
                row["components"]["forge.animator"]["root_motion"] = 2;
        World motion(project, moving, actor);
        motion.tick(.5f);
        const auto delta = motion.animation->root_motion(motion.ref());
        require(bool(delta), "Blend Space extraction absent");
        near(delta->translation[0], .25);
        near(delta->translation[2], .25);
        auto mc = motion.entity().get<Animator>();
        mc.blend_position = 0;
        motion.entity().set(mc);
        motion.tick(.25f);
        const auto still = motion.animation->root_motion(motion.ref());
        require(bool(still), "Blend Space endpoint extraction absent");
        near(still->translation[0], 0);
        near(still->translation[2], 0);
        // At a pure endpoint, scaling the interval to that clip must preserve
        // complete-loop turning displacement as well as ordinary pose sampling.
        auto endpoint_source = moving;
        for (auto& row : endpoint_source["entities"])
            if (row["id"] == Json(actor))
                row["components"]["forge.animator"]["blend_position"] = 2;
        World loops(project, endpoint_source, actor),
            native_loops(project, direct(endpoint_source, actor, overlay), actor);
        loops.tick(7);
        native_loops.tick(3.5f);
        const auto ld = loops.animation->root_motion(loops.ref()),
                   nd = native_loops.animation->root_motion(native_loops.ref());
        require(bool(ld) && bool(nd), "Blend Space loop extraction absent");
        for (unsigned axis = 0; axis < 3; ++axis)
            near(ld->translation[axis], nd->translation[axis]);
        for (unsigned axis = 0; axis < 4; ++axis)
            near(ld->rotation[axis], nd->rotation[axis]);
        auto nonloop = endpoint_source;
        for (auto& row : nonloop["entities"])
            if (row["id"] == Json(actor))
                row["components"]["forge.animator"]["loop"] = false;
        World finished(project, nonloop, actor);
        finished.tick(3);
        near(finished.pose().at("time"), 2);
        require(!finished.pose().at("playing").get<bool>() &&
                    finished.animation->playback(finished.ref())->finished,
                "Nonloop Blend Space stopped at a clip duration instead of its cycle");
        phase = "replacement publication";
        const auto last_good = catalog.records().at(asset).metadata.at("forge.import");
        document->edit(document->revision(), "Foreign sample",
                       [](Json& d) { d["samples"][0]["clip"] = AssetId::generate(); });
        document->save();
        imports.submit(
            imports.prepare(document->locator(), {}, asset),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_animation_blend_space_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(10s), "Rejected Blend Space publication timeout");
        const auto rejected = imports.poll();
        require(rejected.size() == 1 && !rejected[0].published &&
                    AssetCatalog::open_project(project).records().at(asset).metadata.at(
                        "forge.import") == last_good,
                "Invalid Blend Space publication replaced last-good selection");
        document->undo();
        document->save();
        document->edit(document->revision(), "Longer cycle",
                       [](Json& d) { d["cycle_seconds"] = 4; });
        publish();
        const auto revised = std::make_shared<const AssetCatalog>(catalog);
        recovered.animation->catalog(revised);
        const auto replacement_deadline = std::chrono::steady_clock::now() + 10s;
        double phase_before = 0;
        do {
            phase_before = recovered.pose().at("time").get<double>() /
                           recovered.pose().at("duration").get<double>();
            recovered.tick(.01f);
            require(std::chrono::steady_clock::now() < replacement_deadline,
                    "Blend Space revision admission timeout");
            std::this_thread::sleep_for(1ms);
        } while (
            recovered.animation->checkpoint().at("entries")[0].at("blend_space").at("revision") ==
            checkpoint.at("entries")[0].at("blend_space").at("revision"));
        near(recovered.pose().at("time"), phase_before * 4 + double(.01f));
        require(recovered.pose().at("duration") == 4 &&
                    recovered.pose().at("time").get<double>() >= 1,
                "Blend Space revision did not preserve normalized cycle phase");
        const auto held_revision = recovered.animation->checkpoint();
        rejects([&] { recovered.animation->restore(checkpoint); });
        require(recovered.animation->checkpoint() == held_revision,
                "Stale space recovery changed new clocks");
        document->undo();
        publish();
        phase = "source-free export";
        const auto package = project.parent_path() / ("space-export-" + AssetId::generate().str());
        const std::array roots{asset, model};
        package_runtime_content(project, package, roots, {"windows", "d3d12"});
        // Only the UUID-named scratch copy is made unavailable, reversibly.
        // The repository Gallery and its authored assets remain readable.
        std::filesystem::rename(project / "Assets", project / "AuthoredSources-offline");
        const auto moved = project.parent_path() / ("space-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, moved);
        (void)open_runtime_content(moved, {"windows", "d3d12"});
        require(!std::filesystem::exists(moved / "Assets/TestMotion.blendspace.json"),
                "Blend Space source shipped");
        World exported(moved, source, actor);
        exported.tick(.5f);
        same(expected, exported.pose());
        auto record = AssetCatalog::open_project(moved).records().at(asset);
        record.dependency_edges.pop_back();
        ResourcePool<AnimationBlendSpaceAsset> invalid_pool;
        require(!invalid_pool.wait(request_animation_blend_space(invalid_pool, moved, record), 10s),
                "Incomplete Blend Space closure admitted");
        std::cout
            << "Blend Space publication/history, phase-aligned adjacent poses, morphs, paused pair "
               "changes, physics rollback, root intervals, cold recovery and source-free export "
               "passed\n";
    } catch (const std::exception& e) {
        std::cerr << phase << ": " << e.what() << '\n';
        return 1;
    }
}
