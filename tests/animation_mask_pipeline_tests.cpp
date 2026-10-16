#include "animation_mask_authoring.hpp"
#include "animation_mask_binding.hpp"
#include "animation_mask_resource.hpp"
#include "animation_pose_compare.hpp"
#include "animation_preview.hpp"
#include "asset_reference_impact.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_placement.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .003,
            "Mask numeric mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
}
template <class F> void rejects(F run, const char* message) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}
struct World {
    EngineContext engine;
    Scene scene;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    std::uint64_t tick_id = 0;
    World(const std::filesystem::path& project, const Json& source, EntityId id,
          bool prepare = true)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          animation(animation_runtime(engine.world())), actor(id) {
        scene.restore_snapshot(source);
        if (prepare) {
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (!animation->prepare_initial_pose()) {
                require(std::chrono::steady_clock::now() < deadline, "Mask admission timeout");
                std::this_thread::sleep_for(1ms);
            }
        }
    }
    flecs::entity entity() { return scene.entity(actor.str()); }
    EntityRef ref() { return scene.reference(actor.str()); }
    void tick(float dt) { animation->tick(dt, ++tick_id); }
    Json pose() { return animation->presentation(entity().id(), 1); }
};
std::size_t joint(const Json& pose, AssetId id) {
    for (std::size_t i = 0; i < pose.at("joint_assets").size(); ++i)
        if (pose["joint_assets"][i] == Json(id))
            return i;
    throw std::runtime_error("Mask durable joint absent");
}
Json config(Json source, EntityId actor, AssetId mask, float weight = 1, unsigned mode = 0,
            AssetId subtree = {}) {
    for (auto& row : source["entities"])
        if (row["id"] == Json(actor)) {
            auto& c = row["components"]["forge.animator"];
            c["blend_mask"] = mask ? Json(mask) : Json();
            c["blend_weight"] = weight;
            c["blend_mode"] = mode;
            c["blend_root"] = subtree ? Json(subtree) : Json();
        }
    return source;
}
void same(const Json& a, const Json& b) {
    require(a.at("local") == b.at("local") && a.at("morphs") == b.at("morphs"),
            "Mask recovery pose differs");
}
void written_root(World& world, AssetId id) {
    const auto pose = world.pose();
    const auto index = joint(pose, id);
    const auto snapshot = world.scene.effective_document();
    for (const auto& row : snapshot.at("entities")) {
        const auto& components = row.at("components");
        if (components.contains("forge.model_source") &&
            components.at("forge.model_source").at("node") == Json(id)) {
            const auto& value = components.at("forge.local_translation");
            near(value.at("x"), pose.at("local")[index].at("translation")[0]);
            near(value.at("z"), pose.at("local")[index].at("translation")[2]);
            return;
        }
    }
    throw std::runtime_error("Mask root's written transform is absent");
}
void weighted(const Json& a, const Json& b, const Json& actual, AssetId id, float w) {
    const auto index = joint(actual, id);
    for (const auto* field : {"translation", "scale"})
        for (unsigned c = 0; c < 3; ++c)
            near(actual["local"][index][field][c],
                 a["local"][index][field][c].get<double>() * (1 - w) +
                     b["local"][index][field][c].get<double>() * w);
    double length = 0, dot = 0;
    std::array<double, 4> q{};
    for (unsigned c = 0; c < 4; ++c)
        dot += a["local"][index]["rotation"][c].get<double>() *
               b["local"][index]["rotation"][c].get<double>();
    for (unsigned c = 0; c < 4; ++c) {
        q[c] = a["local"][index]["rotation"][c].get<double>() * (1 - w) +
               (dot < 0 ? -1 : 1) * b["local"][index]["rotation"][c].get<double>() * w;
        length += q[c] * q[c];
    }
    for (unsigned c = 0; c < 4; ++c)
        near(actual["local"][index]["rotation"][c], q[c] / std::sqrt(length));
}
} // namespace
int main(int argc, char** argv) {
    const char* phase = "model import";
    try {
        require(argc == 5 || argc == 6,
                "Need scratch, model worker, converter and original source");
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
            [](auto& c, const auto& p, const auto& cat) { prepare_model_publication(c, p, cat); },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(60s), "Mask model cook timeout");
        const auto receipts = imports.poll();
        require(receipts.size() == 1 && receipts[0].published,
                receipts.empty() ? "Missing model receipt" : receipts[0].diagnostic);
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type)
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        const auto root = selected.bindings.at("/nodes/0"),
                   upper = selected.bindings.at("/nodes/1"),
                   mesh = selected.bindings.at("/nodes/2"), side = selected.bindings.at("/nodes/3"),
                   tip = selected.bindings.at("/nodes/4"),
                   skeleton = selected.bindings.at("/rig/skeleton");
        phase = "mask publication";
        auto document =
            AnimationMaskDocument::create(lease, "Assets/Weighted.mask.json", [&](AssetId id) {
                auto mask = AnimationMaskSource::create(id);
                mask.document["skeleton"] = skeleton;
                mask.document["entries"] = {
                    {{"joint", root}, {"weight", .25}, {"descendants", false}},
                    {{"joint", upper}, {"weight", .5}, {"descendants", true}},
                    {{"joint", mesh}, {"weight", .25}, {"descendants", true}},
                    {{"joint", tip}, {"weight", .75}, {"descendants", false}}};
                return mask;
            });
        const auto mask = document->source().asset();
        const auto baseline = document->source().document;
        document->edit(document->revision(), "Change default",
                       [](Json& d) { d["default_weight"] = .1; });
        require(document->dirty(), "Mask source edit lost dirty state");
        document->undo();
        require(document->source().document == baseline, "Mask Undo differs");
        document->redo();
        require(document->source().document.at("default_weight") == .1, "Mask Redo differs");
        document->undo();
        rejects(
            [&] {
                document->edit(document->revision(), "Invalid weight",
                               [](Json& d) { d["entries"][0]["weight"] = 2; });
            },
            "Invalid mask edit entered history");
        require(document->source().document == baseline, "Rejected mask edit changed source");
        AssetImportService masks(lease, animation_mask_import_registry(), {"linux", "none", "cpu"},
                                 1);
        const auto publish = [&] {
            document->save();
            masks.submit(
                masks.prepare(document->locator(), {}, mask),
                [](auto& c, const auto& p, const auto&) {
                    prepare_animation_mask_publication(c, p);
                },
                [](const auto&, const auto&) {});
            require(masks.wait_idle(10s), "Mask publication timeout");
            const auto done = masks.poll();
            require(done.size() == 1 && done[0].published,
                    done.empty() ? "Missing mask receipt" : done[0].diagnostic);
        };
        publish();
        catalog = AssetCatalog::open_project(project);
        const auto mask_record = catalog.records().at(mask);
        require(mask_record.dependency_edges.size() == 5,
                "Mask closure omitted skeleton or explicit bones");
        ResourcePool<AnimationMaskAsset> pool;
        const auto request = request_animation_mask(pool, project, mask_record);
        require(pool.wait(request, 10s), request.inspect().diagnostic);
        const auto admitted = pool.acquire(request);
        require(admitted && admitted->skeleton_revision == selected.revision,
                "Mask resource lost skeleton revision");
        const auto durable = animation_mask_joints(selected, skeleton);
        require(admitted->mask.weights(skeleton, durable.assets, durable.parents).size() == 5,
                "Mask resource lost native joints");
        phase = "typed scene authoring";
        EngineContext author;
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.clip = {selected.bindings.at("/animations/0")};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
        auto c = scene.entity(actor.str()).get<Animator>();
        require(!c.blend_mask.id, "Older model placement did not default to a null mask");
        c.blend_clip = {selected.bindings.at("/animations/1")};
        c.blend_weight = 1;
        c.transition_seconds = 0;
        scene.entity(actor.str()).set(c);
        const auto before = scene.document();
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.animator"},
                           {"field", "blend_mask"},
                           {"value", mask}});
        const auto source = scene.document();
        require(scene.entity(actor.str()).get<Animator>().blend_mask.id == mask && scene.undo() &&
                    scene.document() == before && scene.redo() && scene.document() == source,
                "Mask typed history differs");
        if (argc == 6) {
            require(std::string_view(argv[5]) == "--prepare", "Unknown mask fixture mode");
            atomic_write(project / "main.scene.json", source.dump(2));
            std::cout << project.string() << '\n';
            return 0;
        }
        const std::array refs{
            AssetReferenceDocument{document->locator(), document->source().document}};
        const auto impact = inspect_asset_references(scene.schema(), refs, {skeleton, upper});
        require(impact.references.size() == 2 && impact.uninspected.empty(),
                "Mask typed reference review lost coverage");
        phase = "weighted playback";
        World base(project, config(source, actor, {}, 0), actor),
            full(project, config(source, actor, {}), actor), weighted_world(project, source, actor),
            combined(project, config(source, actor, mask, 1, 0, upper), actor),
            half(project, config(source, actor, mask, .5), actor);
        for (auto* w : {&base, &full, &weighted_world, &combined, &half})
            w->tick(1);
        const auto bp = base.pose(), fp = full.pose(), wp = weighted_world.pose();
        // Existing null-mask recovery records have no mask fields.
        auto legacy_checkpoint = base.animation->checkpoint();
        auto& legacy_blend = legacy_checkpoint["entries"][0]["blend"];
        legacy_blend.erase("mask");
        legacy_blend.erase("mask_revision");
        World legacy(project, base.scene.document(), actor, false);
        legacy.animation->restore(legacy_checkpoint);
        same(bp, legacy.pose());
        for (const auto [id, weight] :
             std::array{std::pair{root, .25f}, std::pair{upper, .5f}, std::pair{tip, .75f},
                        std::pair{mesh, .25f}, std::pair{side, 0.f}}) {
            weighted(bp, fp, wp, id, weight);
            weighted(bp, fp, half.pose(), id, weight * .5f);
            weighted(bp, fp, combined.pose(), id, id == upper || id == tip ? weight : 0);
        }
        near(wp["morphs"][0]["weights"][0], .75);
        near(wp["morphs"][0]["weights"][1], -.3125);
        phase = "additive playback";
        World additive(project, config(source, actor, mask, 1, 1), actor),
            quarter(project, config(source, actor, {}, .25f, 1), actor),
            ahalf(project, config(source, actor, {}, .5f, 1), actor),
            athree(project, config(source, actor, {}, .75f, 1), actor);
        for (auto* w : {&additive, &quarter, &ahalf, &athree})
            w->tick(1);
        const auto ap = additive.pose();
        for (const auto& [id, comparison] :
             std::array{std::pair{root, quarter.pose()}, std::pair{mesh, quarter.pose()},
                        std::pair{upper, ahalf.pose()}, std::pair{tip, athree.pose()},
                        std::pair{side, bp}})
            require(test::same_blended_local_pose(ap["local"][joint(ap, id)],
                                                  comparison["local"][joint(comparison, id)]),
                    "Additive joint influence differs from scalar overlay: " +
                        Json{{"joint", id},
                             {"actual", ap["local"][joint(ap, id)]},
                             {"expected", comparison["local"][joint(comparison, id)]}}
                            .dump());
        near(ap["morphs"][0]["weights"][0], .8125);
        near(ap["morphs"][0]["weights"][1], -.4375);
        auto motion = config(source, actor, mask, 1, 1);
        for (auto& row : motion["entities"])
            if (row["id"] == Json(actor))
                row["components"]["forge.animator"]["root_motion"] = 2;
        World extracted(project, motion, actor);
        extracted.tick(1);
        const auto delta = extracted.animation->root_motion(extracted.ref());
        require(bool(delta), "Weighted root extraction absent");
        near(delta->translation[0], .25);
        near(delta->translation[2], 1);
        SkeletalAnimationPreview preview;
        preview.start(project, source, scene.reference(actor.str()));
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!preview.prepare()) {
            require(std::chrono::steady_clock::now() < deadline, "Mask preview admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        preview.seek(1);
        same(wp, preview.pose());
        require(scene.document() == source, "Mask preview changed authored source");
        preview.stop();
        const auto checkpoint = weighted_world.animation->checkpoint();
        World recovered(project, weighted_world.scene.document(), actor, false);
        recovered.animation->restore(checkpoint);
        same(wp, recovered.pose());
        auto bad = checkpoint;
        bad["entries"][0]["blend"]["mask_revision"] = "foreign";
        const auto safe = recovered.animation->checkpoint();
        rejects([&] { recovered.animation->restore(bad); }, "Mask revision mismatch restored");
        require(recovered.animation->checkpoint() == safe, "Rejected mask recovery mutated clocks");
        weighted_world.tick(.1f);
        recovered.tick(.1f);
        same(weighted_world.pose(), recovered.pose());
        const auto time = weighted_world.pose().at("time");
        require(weighted_world.animation->control(weighted_world.ref(), AnimationControl::Pause,
                                                  ++weighted_world.tick_id),
                "Mask pause rejected");
        auto selected_config = weighted_world.entity().get<Animator>();
        selected_config.blend_mask = {};
        weighted_world.entity().set(selected_config);
        weighted_world.animation->synchronize();
        require(weighted_world.animation->checkpoint_ready() &&
                    weighted_world.pose().at("time") == time &&
                    !weighted_world.pose().at("playing").get<bool>(),
                "Clearing mask lost paused phase");
        written_root(weighted_world, root);
        selected_config.blend_mask = {mask};
        weighted_world.entity().set(selected_config);
        weighted_world.animation->synchronize();
        require(weighted_world.animation->checkpoint_ready() &&
                    weighted_world.pose().at("time") == time,
                "Reassigning mask lost phase");
        written_root(weighted_world, root);
        const auto held = weighted_world.pose();
        selected_config.blend_mask = {model};
        weighted_world.entity().set(selected_config);
        weighted_world.animation->synchronize();
        same(held, weighted_world.pose());
        require(!weighted_world.animation->checkpoint_ready(), "Wrong-type mask checkpointed");
        selected_config.blend_mask = {mask};
        weighted_world.entity().set(selected_config);
        weighted_world.animation->synchronize();
        selected_config.blend_mask = {};
        weighted_world.entity().set(selected_config);
        weighted_world.animation->pose_validator(
            [](const auto&) { throw std::runtime_error("Weighted mask physics veto"); });
        weighted_world.animation->synchronize();
        same(held, weighted_world.pose());
        written_root(weighted_world, root);
        require(!weighted_world.animation->checkpoint_ready(), "Physics-vetoed mask adopted");
        weighted_world.animation->pose_validator({});
        selected_config.blend_mask = {mask};
        weighted_world.entity().set(selected_config);
        weighted_world.animation->synchronize();
        require(weighted_world.animation->checkpoint_ready(),
                "Held mask failed to recover after veto");
        auto conflict = selected_config;
        conflict.states.id = AssetId::generate();
        conflict.blend_clip = {};
        conflict.blend_weight = 0;
        conflict.blend_root = {};
        conflict.blend_mode = 0;
        weighted_world.entity().set(conflict);
        weighted_world.animation->synchronize();
        same(held, weighted_world.pose());
        require(
            !weighted_world.animation->checkpoint_ready() &&
                Json(weighted_world.engine.world().services().diagnostics())
                        .dump()
                        .find("Named animation states cannot also use manual two-clip blending") !=
                    std::string::npos,
            "A mask-only named-state conflict was not rejected before resource loading");
        weighted_world.entity().set(selected_config);
        weighted_world.animation->synchronize();
        require(weighted_world.animation->checkpoint_ready(),
                "Direct mask configuration did not recover after a named-state conflict");
        // A failed source publication does not replace the selected cooked mask.
        document->edit(document->revision(), "Foreign bone",
                       [](Json& d) { d["entries"][0]["joint"] = AssetId::generate(); });
        document->save();
        masks.submit(
            masks.prepare(document->locator(), {}, mask),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_animation_mask_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        require(masks.wait_idle(10s), "Invalid mask cook timeout");
        const auto rejected = masks.poll();
        require(rejected.size() == 1 && !rejected[0].published, "Missing bone published");
        require(AssetCatalog::open_project(project).records().at(mask).metadata ==
                    mask_record.metadata,
                "Rejected mask displaced last-good selection");
        document->undo();
        document->save();
        // A new immutable mask selection is bound once, with clocks preserved.
        document->edit(document->revision(), "Raise root influence", [&](Json& d) {
            for (auto& entry : d["entries"])
                if (entry.at("joint") == Json(root))
                    entry["weight"] = .5;
        });
        publish();
        const auto updated_catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project));
        const auto updated_revision =
            updated_catalog->records().at(mask).metadata.at("forge.import").at("key");
        require(updated_revision != admitted.identity().revision,
                "Changed mask reused its previous immutable key");
        const auto previous_time = recovered.pose().at("time").get<double>();
        recovered.animation->catalog(updated_catalog);
        const auto reload_deadline = std::chrono::steady_clock::now() + 10s;
        unsigned steps = 0;
        do {
            recovered.tick(.01f);
            ++steps;
            require(std::chrono::steady_clock::now() < reload_deadline,
                    "Mask replacement admission timeout");
            std::this_thread::sleep_for(1ms);
        } while (recovered.animation->checkpoint().at("entries")[0].at("blend").at(
                     "mask_revision") != updated_revision);
        near(recovered.pose().at("time"), std::fmod(previous_time + steps * double(.01f), 2.));
        const auto replaced = recovered.animation->checkpoint();
        rejects([&] { recovered.animation->restore(checkpoint); },
                "Old mask revision restored over new selection");
        require(recovered.animation->checkpoint() == replaced,
                "Rejected old mask recovery changed the new playback");
        // Return to the original authored mask before proving source-free export.
        document->undo();
        publish();
        const auto destination = project.parent_path() / (project.filename().string() + "-export");
        const std::array package_roots{mask, model};
        package_runtime_content(project, destination, package_roots, {"linux", "none"});
        const auto moved = project.parent_path() / (project.filename().string() + "-relocated");
        std::filesystem::rename(destination, moved);
        std::filesystem::remove(project / "Assets/OverlayColumn.gltf");
        std::filesystem::remove(project / "Assets/Weighted.mask.json");
        require(!std::filesystem::exists(moved / "Assets/Weighted.mask.json") &&
                    !std::filesystem::exists(moved / "Assets/OverlayColumn.gltf"),
                "Source mask/model shipped in runtime content");
        (void)open_runtime_content(moved, {"linux", "none"});
        World exported(moved, source, actor);
        exported.tick(1);
        same(wp, exported.pose());
        auto record = AssetCatalog::open_project(moved).records().at(mask);
        record.dependency_edges.pop_back();
        ResourcePool<AnimationMaskAsset> invalid_pool;
        const auto invalid = request_animation_mask(invalid_pool, moved, record);
        require(!invalid_pool.wait(invalid, 10s), "Incomplete cooked mask closure admitted");
        std::cout << "Mask source/history/publication, fractional blend/additive/root/morph, "
                     "subtree composition, paused phase, rollback, cold recovery and relocated "
                     "source-free export passed\n";
    } catch (const std::exception& e) {
        std::cerr << phase << ": " << e.what() << '\n';
        return 1;
    }
}
