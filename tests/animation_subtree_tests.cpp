#include "animation_playback_access.hpp"
#include "animation_preview.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_placement.hpp"
#include <chrono>
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
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .003,
            "Subtree pose mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    World(const std::filesystem::path& project, const Json& source, EntityId id)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())),
          actor(id) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline, "Subtree admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
    flecs::entity entity() { return scene.entity(actor.str()); }
    EntityRef ref() { return scene.reference(actor.str()); }
    Json pose(double alpha = 1) { return animation->presentation(entity().id(), alpha); }
};
std::size_t joint(const Json& pose, AssetId id) {
    const auto& ids = pose.at("joint_assets");
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == Json(id))
            return i;
    throw std::runtime_error("Durable joint absent from pose");
}
Json with_mask(Json source, EntityId actor, AssetId mask, float weight = 1) {
    for (auto& row : source.at("entities"))
        if (row.at("id") == Json(actor)) {
            row["components"]["forge.animator"]["blend_root"] = mask ? Json(mask) : Json();
            row["components"]["forge.animator"]["blend_weight"] = weight;
        }
    return source;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 5 || argc == 6,
                "Need scratch, model worker, converter and original source");
        const auto project = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(project / "Assets");
        std::filesystem::copy_file(argv[4], project / "Assets/MaskColumn.gltf");
        auto registry = std::make_shared<AssetImporterRegistry>();
        const auto converter = std::filesystem::absolute(argv[3]);
        auto importer = model_importer(std::filesystem::absolute(argv[2]), converter);
        if (std::string_view(argv[2]) == "--direct")
            importer = std::make_shared<forge::test::DirectModelImporter>(importer, converter);
        registry->add(importer);
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(project);
        AssetImportService service(lease, registry, {"linux", "none", "cpu"}, 1);
        const auto publish = [&] {
            auto draft = service.prepare("Assets/MaskColumn.gltf");
            service.submit(
                draft,
                [](auto& c, const auto& p, const auto& catalog) {
                    prepare_model_publication(c, p, catalog);
                },
                [](const auto&, const auto&) {});
            require(service.wait_idle(60s), "Subtree import timeout");
            const auto receipts = service.poll();
            require(receipts.size() == 1 && receipts[0].published,
                    receipts.empty() ? "Missing subtree receipt" : receipts[0].diagnostic);
        };
        publish();
        auto catalog = AssetCatalog::open_project(project);
        AssetId model;
        for (const auto& [id, record] : catalog.records())
            if (record.type == ModelAsset::type)
                model = id;
        const auto selected = load_model_selection(project, catalog, model);
        const auto rig = selected.bindings.at("/nodes/0"), upper = selected.bindings.at("/nodes/1"),
                   mesh = selected.bindings.at("/nodes/2"), side = selected.bindings.at("/nodes/3"),
                   tip = selected.bindings.at("/nodes/4");
        EngineContext author;
        Scene scene(author.world());
        ModelPlacementOptions options;
        options.clip = {selected.bindings.at("/animations/0")};
        const auto actor = instantiate_model(
            scene, catalog,
            prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
        auto config = scene.entity(actor.str()).get<Animator>();
        require(!config.blend_root.id, "New Animator did not default to whole skeleton");
        config.blend_clip = {selected.bindings.at("/animations/1")};
        config.blend_weight = 1;
        config.transition_seconds = 0;
        scene.entity(actor.str()).set(config);
        const auto before = scene.document();
        authoring_command(scene, "property.set",
                          {{"entity", actor.str()},
                           {"component", "forge.animator"},
                           {"field", "blend_root"},
                           {"value", upper}});
        const auto source = scene.document();
        require(scene.entity(actor.str()).get<Animator>().blend_root.id == upper,
                "Typed subtree reference lost");
        if (argc == 6) {
            require(std::string_view(argv[5]) == "--prepare", "Unknown subtree fixture mode");
            atomic_write(project / "main.scene.json", source.dump(2));
            std::cout << project.string() << '\n';
            return 0;
        }
        require(scene.undo() && scene.document() == before, "Subtree Undo failed");
        require(scene.redo() && scene.document() == source, "Subtree Redo failed");
        // Four joints expose a descendant and a sibling independently of the visible skin.
        World base(project, with_mask(source, actor, {}, 0), actor),
            full(project, with_mask(source, actor, {}), actor), masked(project, source, actor),
            root(project, with_mask(source, actor, rig), actor);
        for (auto* world : {&base, &full, &masked, &root})
            world->simulation.tick(1);
        const auto bp = base.pose(), fp = full.pose(), mp = masked.pose(), rp = root.pose();
        require(mp.at("local").size() == 4, "Original subtree fixture lost joints");
        for (const auto id : {rig, side}) {
            const auto i = joint(mp, id);
            require(mp.at("local")[i] == bp.at("local")[i], "Subtree modified base/sibling joint");
        }
        for (const auto id : {upper, tip}) {
            const auto i = joint(mp, id);
            require(mp.at("local")[i] == fp.at("local")[i],
                    "Subtree excluded its joint/descendant");
        }
        require(rp.at("local") == fp.at("local"), "Selecting rig root differed from full blend");
        near(fp["local"][joint(fp, rig)]["translation"][0], 1);
        near(fp["local"][joint(fp, rig)]["rotation"][1], std::sin(3.141592653589793 / 12));
        near(mp["local"][joint(mp, upper)]["translation"][0], .35);
        near(mp["local"][joint(mp, tip)]["translation"][1], .75);
        near(mp["local"][joint(mp, side)]["translation"][0], -1);
        World half(project, with_mask(source, actor, upper, .5f), actor);
        half.simulation.tick(1);
        near(half.pose()["local"][joint(mp, upper)]["translation"][0], .175);
        require(half.pose()["local"][joint(mp, rig)] == bp["local"][joint(mp, rig)],
                "Half mix changed excluded root");
        // Paths absent from both clips retain authored values. The explicit channel union
        // still owns secondary-only paths, sampling primary rest values outside the mask.
        auto authored = source;
        for (auto& row : authored["entities"])
            if (row["components"].contains("forge.model_source") &&
                row["components"]["forge.model_source"]["node"] == Json(side))
                row["components"]["forge.local_scale"]["x"] = 2.;
        World intent(project, authored, actor);
        intent.simulation.tick(1);
        const auto intent_scene = intent.scene.snapshot();
        for (const auto& row : intent_scene["entities"])
            if (row["components"].contains("forge.model_source") &&
                row["components"]["forge.model_source"]["node"] == Json(side))
                near(row["components"]["forge.local_scale"]["x"], 2);
        SkeletalAnimationPreview preview;
        preview.start(project, source, scene.reference(actor.str()));
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!preview.prepare()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Masked preview admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        preview.seek(1);
        require(preview.pose()["local"] == mp["local"], "Preview did not share masked pose");
        preview.stop();
        require(scene.document() == source, "Preview edited saved source");
        const auto checkpoint = masked.animation->checkpoint();
        World restored(project, masked.scene.snapshot(), actor);
        restored.animation->restore(checkpoint);
        require(restored.pose()["local"] == mp["local"], "Subtree recovery changed pose");
        AnimationPlaybackAccess::control(*masked.animation, masked.ref(), {{"operation", "pause"}});
        AnimationPlaybackAccess::control(*masked.animation, masked.ref(),
                                         {{"operation", "seek"}, {"position_seconds", 1.5}});
        near(masked.pose()["time"], 1.5);
        near(masked.pose()["local"][joint(mp, rig)]["translation"][0], 0);
        require(!masked.pose()["playing"].get<bool>(), "Masked seek lost pause");
        AnimationPlaybackAccess::control(*masked.animation, masked.ref(), {{"operation", "play"}});
        masked.simulation.tick(.25f);
        near(masked.pose()["time"], 1.75);
        // Runtime mask changes preserve clocks, reject foreign/non-joints before writing a pose.
        const auto time = masked.pose()["time"].get<double>();
        config = masked.entity().get<Animator>();
        config.blend_root = {rig};
        masked.entity().set(config);
        masked.animation->synchronize();
        near(masked.pose()["time"], time);
        masked.simulation.tick(.1f);
        require(masked.pose()["local"][joint(mp, rig)]["translation"][0].get<double>() > 1,
                "Runtime root mask did not adopt");
        for (const auto bad : {mesh, AssetId::generate()}) {
            const auto prior = masked.scene.snapshot();
            config.blend_root = {bad};
            masked.entity().set(config);
            masked.animation->synchronize();
            require(!masked.animation->checkpoint_ready(),
                    "Invalid subtree became checkpoint-ready");
            // Rejection itself writes no pose. The existing last-good player may
            // continue advancing on later ticks, as with other invalid clip edits.
            const auto after = masked.scene.snapshot();
            for (std::size_t i = 0; i < prior["entities"].size(); ++i)
                if (prior["entities"][i].at("id") != Json(actor))
                    require(after["entities"][i] == prior["entities"][i],
                            "Invalid mask changed model node");
            config.blend_root = {upper};
            masked.entity().set(config);
            masked.animation->synchronize();
            require(masked.animation->checkpoint_ready(),
                    "Valid subtree did not recover after rejection");
        }
        masked.simulation.tick(.1f);
        const auto written = masked.scene.snapshot();
        for (const auto& row : written["entities"])
            if (row["components"].contains("forge.model_source") &&
                row["components"]["forge.model_source"]["node"] == Json(rig))
                near(row["components"]["forge.local_translation"]["x"], 0);
        // Reimport durable membership: node names and physical array positions are not bindings.
        Json gltf;
        std::ifstream(project / "Assets/MaskColumn.gltf") >> gltf;
        gltf["nodes"][1]["name"] = "Renamed upper";
        atomic_write(project / "Assets/MaskColumn.gltf", gltf.dump(2));
        publish();
        catalog = AssetCatalog::open_project(project);
        const auto renamed = load_model_selection(project, catalog, model);
        require(renamed.bindings.at("/nodes/1") == upper, "Reimport lost durable renamed joint");
        World renamed_world(project, source, actor);
        renamed_world.simulation.tick(1);
        require(renamed_world.pose()["local"] == mp["local"],
                "Renamed subtree changed sampled pose");
        const auto old_nodes = gltf.at("nodes");
        const std::array<std::size_t, 5> order{1, 0, 2, 3, 4};
        std::array<std::size_t, 5> remap{};
        for (std::size_t i = 0; i < order.size(); ++i)
            remap[order[i]] = i;
        for (std::size_t i = 0; i < order.size(); ++i) {
            gltf["nodes"][i] = old_nodes[order[i]];
            if (gltf["nodes"][i].contains("children"))
                for (auto& child : gltf["nodes"][i]["children"])
                    child = remap[child.get<std::size_t>()];
        }
        for (auto& skin : gltf["skins"]) {
            skin["skeleton"] = remap[skin["skeleton"].get<std::size_t>()];
            for (auto& j : skin["joints"])
                j = remap[j.get<std::size_t>()];
        }
        for (auto& clip : gltf["animations"])
            for (auto& channel : clip["channels"])
                channel["target"]["node"] = remap[channel["target"]["node"].get<std::size_t>()];
        for (auto& gltf_scene : gltf["scenes"])
            for (auto& n : gltf_scene["nodes"])
                n = remap[n.get<std::size_t>()];
        atomic_write(project / "Assets/MaskColumn.gltf", gltf.dump(2));
        publish();
        catalog = AssetCatalog::open_project(project);
        const auto reordered = load_model_selection(project, catalog, model);
        require(reordered.bindings.at("/nodes/0") == upper &&
                    reordered.bindings.at("/nodes/1") == rig,
                "Reimport lost durable reordered subtree");
        World reordered_world(project, source, actor);
        reordered_world.simulation.tick(1);
        require(reordered_world.pose()["local"] == mp["local"], "Reordered subtree changed pose");
        std::cout
            << "Subtree root/child/descendant/sibling, typed history, authored intent, "
               "preview, seek, recovery, runtime rejection and renamed/reordered reimport passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
