#include "animation_preview.hpp"
#include "animation_states_authoring.hpp"
#include "animation_states_resource.hpp"
#include "asset_reference_impact.hpp"
#include "model_authoring.hpp"
#include "model_import_test.hpp"
#include "model_placement.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <fstream>
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
void near(double a, double b, const char* why) {
    require(std::abs(a - b) < 2e-4,
            std::string(why) + ": " + std::to_string(a) + " vs " + std::to_string(b));
}
void same_pose(const Json& a, const Json& b, const char* why) {
    require(a.size() == b.size(), why);
    for (std::size_t j = 0; j < a.size(); ++j)
        for (const auto* field : {"translation", "rotation", "scale"})
            for (std::size_t c = 0; c < a[j].at(field).size(); ++c)
                require(std::abs(a[j].at(field)[c].get<double>() -
                                 b[j].at(field)[c].get<double>()) < 2e-6,
                        why);
}
template <class F> void rejects(F f, const char* why) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    require(failed, why);
}
struct Fixture {
    EngineContext engine;
    Scene scene;
    std::shared_ptr<AnimationRuntime> animation;
    EntityId actor;
    Fixture(const std::filesystem::path& project, const Json& source, EntityId id)
        : engine(WorldRole::Runtime, false, {animation_module(project)}), scene(engine.world()),
          animation(animation_runtime(engine.world())), actor(id) {
        scene.restore_snapshot(source);
        const auto end = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < end, "State admission timeout");
            std::this_thread::sleep_for(1ms);
        }
    }
    Json pose() { return animation->presentation(scene.entity(actor.str()).id(), 1); }
    void state(std::uint32_t value) {
        auto e = scene.entity(actor.str());
        auto c = e.get<Animator>();
        c.state = value;
        e.set(c);
    }
};
#include "animation_exit_tests.hpp"
void control_tests(const std::filesystem::path& root, const Json& doc, EntityId actor,
                   const AnimationStatesSource& source) {
    auto held = doc;
    held["entities"][0]["components"]["forge.animator"]["play_on_start"] = false;
    Fixture f(root, held, actor);
    const auto entity = f.scene.entity(actor.str());
    const auto reference = *f.engine.world().reference(entity.id());
    auto status = [&] { return *f.animation->playback(reference); };
    auto command = [&](AnimationControl op, std::uint64_t tick) {
        return f.animation->control(reference, op, tick);
    };
    require(!status().playing && status().time == 0, "Initially held player advanced");
    require(command(AnimationControl::Play, 1), "Play held player rejected");
    f.animation->tick(.5f, 1);
    near(status().time, .5, "Play did not advance");
    const auto before_late = f.animation->checkpoint();
    require(!command(AnimationControl::Pause, 1) && before_late == f.animation->checkpoint(),
            "Late control mutated completed tick");
    require(command(AnimationControl::Pause, 2), "Pause rejected");
    f.animation->tick(.5f, 2);
    near(status().time, .5, "Pause advanced clip");
    require(!status().playing && f.animation->events().empty(), "Paused player emitted");
    const auto paused = f.animation->checkpoint();
    Fixture recovery(root, held, actor);
    recovery.animation->restore(paused);
    require(!recovery.animation->playback(reference)->playing, "Recovery lost paused intent");
    recovery.animation->tick(.5f, 3);
    near(recovery.animation->playback(reference)->time, .5, "Recovered pause advanced");
    require(command(AnimationControl::Play, 3), "Resume rejected");
    f.animation->tick(.5f, 3);
    near(status().time, 1, "Resume restarted instead of continuing");
    const auto world_before = f.scene.document();
    require(command(AnimationControl::Restart, 4), "Restart rejected");
    near(status().time, 0, "Restart did not reset clock");
    require(world_before == f.scene.document() && f.animation->events().empty(),
            "Restart wrote callback pose/components or emitted immediately");
    const auto restarted = f.animation->checkpoint();
    require(restarted.at("entries")[0].at("states").at("entry_pending") == true,
            "Restart lost entry marker intent");
    Fixture replay_recovery(root, held, actor);
    replay_recovery.animation->restore(restarted);
    replay_recovery.animation->tick(.5f, 4);
    f.animation->tick(.5f, 4);
    require(f.animation->events().size() == 2 && f.animation->events()[0].marker == 1 &&
                f.animation->events()[1].marker == 2 &&
                replay_recovery.animation->events().size() == 2,
            "Restart/recovery entry crossing differs");
    auto config = entity.get<Animator>();
    config.enabled = false;
    config.playback_speed = 0;
    entity.set(config);
    f.animation->tick(.2f, 5);
    require(command(AnimationControl::Restart, 6), "Disabled restart rejected");
    f.animation->tick(.2f, 6);
    near(status().time, 0, "Disabled restart advanced");
    require(f.animation->events().empty(), "Disabled restart emitted");
    config.enabled = true;
    config.playback_speed = 1;
    entity.set(config);
    f.animation->tick(.1f, 7);
    require(f.animation->events().size() == 1 && f.animation->events()[0].marker == 1,
            "Enabled restart lost pending entry");
    f.state(2);
    f.animation->tick(.1f, 8);
    require(status().transitioning, "Fixture did not enter fade");
    require(command(AnimationControl::Restart, 9), "Fade restart rejected");
    require(!status().transitioning && status().transition_weight == 1,
            "Restart retained named outgoing fade");
    f.animation->tick(.1f, 9);
    near(status().time, .1, "Fade restart clock");
    auto resumed_scene = f.scene.document();
    Fixture fade_recovery(root, resumed_scene, actor);
    fade_recovery.animation->restore(f.animation->checkpoint());
    require(!fade_recovery.animation->playback(reference)->transitioning,
            "Recovery recreated cleared fade");
    f.state(3);
    f.animation->tick(.1f, 10);
    require(command(AnimationControl::Play, 11), "Play one-shot rejected");
    f.animation->tick(.6f, 11);
    require(status().finished, "Fixture nonloop did not finish");
    require(!command(AnimationControl::Play, 12), "Completed Play implicitly restarted");
    require(command(AnimationControl::Restart, 12), "Completed restart rejected");
    f.animation->tick(.05f, 12);
    require(!status().finished && f.animation->events().size() == 1 &&
                f.animation->events()[0].marker == 8,
            "Completed restart did not reenter once");
    const auto safe = f.animation->checkpoint();
    rejects([&] { command(static_cast<AnimationControl>(99), 13); }, "Invalid command accepted");
    require(safe == f.animation->checkpoint(), "Invalid command mutated clock");
    Fixture interrupted(root, doc, actor);
    interrupted.animation->tick(.3f, 1);
    interrupted.state(2);
    interrupted.animation->tick(.1f, 2);
    interrupted.state(3);
    interrupted.animation->tick(.05f, 3);
    require(!interrupted.animation->checkpoint().at("entries")[0].at("states").at("frozen").empty(),
            "Fixture did not freeze interrupted fade");
    require(interrupted.animation->control(reference, AnimationControl::Pause, 4),
            "Interrupted fade pause rejected");
    const auto frozen_time = interrupted.animation->playback(reference)->time;
    const auto frozen_weight = interrupted.animation->playback(reference)->transition_weight;
    interrupted.animation->tick(.2f, 4);
    near(interrupted.animation->playback(reference)->time, frozen_time,
         "Paused fade clip advanced");
    near(interrupted.animation->playback(reference)->transition_weight, frozen_weight,
         "Paused fade weight advanced");
    require(interrupted.animation->control(reference, AnimationControl::Restart, 5),
            "Interrupted fade restart rejected");
    require(interrupted.animation->checkpoint().at("entries")[0].at("states").at("frozen").empty(),
            "Restart retained frozen outgoing pose");
    interrupted.animation->tick(.05f, 5);
    require(!interrupted.animation->playback(reference)->transitioning,
            "Restart recreated interrupted fade");

    auto direct = doc;
    auto& direct_config = direct["entities"][0]["components"]["forge.animator"];
    direct_config["states"] = nullptr;
    direct_config["state"] = 0;
    direct_config["skeleton"] = source.document.at("skeleton");
    direct_config["clip"] = source.document.at("states")[0].at("clip");
    direct_config["blend_clip"] = source.document.at("states")[1].at("clip");
    direct_config["blend_weight"] = .6;
    Fixture mixed(root, direct, actor);
    mixed.animation->tick(.5f, 1);
    require(mixed.animation->control(reference, AnimationControl::Restart, 2),
            "Direct mix restart rejected");
    const auto mix_recovery = mixed.animation->checkpoint();
    near(mixed.animation->playback(reference)->blend_weight, .6, "Restart lost direct mix");
    Fixture mixed_restored(root, direct, actor);
    mixed_restored.animation->restore(mix_recovery);
    mixed_restored.animation->tick(.25f, 2);
    mixed.animation->tick(.25f, 2);
    near(mixed.animation->playback(reference)->time, .25, "Direct mix replay time");
    same_pose(mixed.pose().at("local"), mixed_restored.pose().at("local"),
              "Direct mix restart recovery pose differs");
    entity.remove<Animator>();
    require(!command(AnimationControl::Restart, 13), "Removed player controlled");
}
void playback_tests(const std::filesystem::path& root, const Json& doc, EntityId actor,
                    const AnimationStatesSource& source) {
    Fixture f(root, doc, actor);
    const auto entity = f.scene.entity(actor.str());
    const auto reference = *f.engine.world().reference(entity.id());
    auto status = [&] {
        const auto value = f.animation->playback(reference);
        require(bool(value), "Realized playback unavailable");
        return *value;
    };
    const auto initial = status();
    require(initial.state == 1 && initial.state_name == "Idle" && initial.loop && initial.enabled &&
                initial.playing && !initial.finished && !initial.transitioning,
            "Initial named playback flags");
    near(initial.time, 0, "Initial time");
    near(initial.duration, 2, "Fixture duration");
    f.animation->tick(.5f, 1);
    near(status().time, .5, "Authoritative clip time");
    near(status().progress, .25, "Normalized progress");
    const auto checkpoint = f.animation->checkpoint();
    for (unsigned i = 0; i < 10; ++i) {
        (void)status();
        (void)f.animation->presentation(entity.id(), .2);
    }
    require(checkpoint == f.animation->checkpoint(), "Status/presentation mutated fixed clocks");
    Fixture restored(root, doc, actor);
    restored.animation->restore(checkpoint);
    near(restored.animation->playback(reference)->progress, .25, "Restored progress");
    f.animation->tick(2.25f, 2);
    near(status().time, .75, "Wrapped loop time");
    require(!status().finished, "Loop reported complete");
    auto request = entity.get<Animator>();
    request.enabled = false;
    entity.set(request);
    f.animation->tick(.2f, 3);
    near(status().time, .75, "Disabled clock advanced");
    require(!status().enabled && !status().playing, "Disabled playback flags");
    request.enabled = true;
    request.playback_speed = 0;
    entity.set(request);
    f.animation->tick(.2f, 4);
    near(status().time, .75, "Zero-speed clock advanced");
    require(status().enabled && !status().playing, "Zero-speed playback flags");
    request.state = 999;
    entity.set(request);
    f.animation->tick(.2f, 5);
    require(status().state == 1, "Invalid request lost last-good realization");
    request.state = 3;
    request.playback_speed = 4;
    entity.set(request);
    f.animation->tick(.15f, 6);
    require(status().finished && !status().loop && status().transitioning && status().playing,
            "Nonloop completion conflated with longer fade");
    near(status().progress, 1, "Nonloop endpoint progress");
    near(status().transition_weight, .15 / .4, "Named incoming fade weight");
    f.animation->tick(.3f, 7);
    require(status().finished && !status().transitioning && !status().playing,
            "Completed fade remains active");
    entity.remove<Animator>();
    require(!f.animation->playback(reference), "Removed Animator accepted");
    require(!f.animation->playback({AssetId::generate(), EntityId::generate()}),
            "Missing entity accepted");
    f.animation->shutdown();
    require(!f.animation->playback(reference), "Shutdown retained playback");

    auto direct = doc;
    auto& config = direct["entities"][0]["components"]["forge.animator"];
    config["states"] = nullptr;
    config["state"] = 0;
    config["skeleton"] = source.document.at("skeleton");
    config["clip"] = source.document.at("states")[0].at("clip");
    config["blend_clip"] = source.document.at("states")[1].at("clip");
    config["blend_weight"] = .6;
    Fixture mixed(root, direct, actor);
    mixed.animation->tick(.25f, 1);
    const auto mix = mixed.animation->playback(reference);
    require(mix && !mix->states && !mix->state && mix->state_name.empty() && mix->blend_clip &&
                !mix->transitioning && !mix->finished,
            "Direct blend mislabeled as named-state transition");
    near(mix->blend_weight, .6, "Direct mix weight");
    near(mix->transition_weight, 1, "Direct named fade default");
    near(mix->progress, .125, "Direct incoming progress");
}
void marker_tests(const std::filesystem::path& root, const Json& doc, EntityId actor) {
    const auto ids = [&](Fixture& f) {
        std::vector<std::uint32_t> result;
        double previous = -1;
        for (const auto& event : f.animation->events()) {
            require(event.offset >= previous, "Marker occurrences not chronological");
            previous = event.offset;
            require(event.entity == *f.engine.world().reference(f.scene.entity(actor.str()).id()),
                    "Marker entity identity changed");
            require(!event.name.empty() && bool(event.clip) && bool(event.states),
                    "Marker fields missing");
            result.push_back(event.marker);
        }
        return result;
    };
    Fixture loop(root, doc, actor);
    require(loop.animation->events().empty(), "Preparation emitted a marker");
    loop.animation->tick(.5f, 1);
    require(ids(loop) == std::vector<std::uint32_t>{1, 2}, "Entry and exact quarter marker");
    near(loop.animation->events()[1].offset, .5, "Marker within-tick offset");
    require(loop.animation->events()[1].tick == 1, "Marker tick identity");
    loop.animation->tick(1.5f, 2);
    require(ids(loop) == std::vector<std::uint32_t>{3, 1, 4}, "Endpoint and zero seam markers");
    loop.animation->tick(4.f, 3);
    require(ids(loop) == std::vector<std::uint32_t>{2, 3, 1, 4, 2, 3, 1, 4},
            "Multiple loops lost markers");
    const auto checkpoint = loop.animation->checkpoint();
    Fixture restored(root, doc, actor);
    restored.animation->restore(checkpoint);
    require(restored.animation->events().empty(), "Restore replayed previous marker events");
    restored.animation->tick(.5f, 4);
    require(ids(restored) == std::vector<std::uint32_t>{2},
            "Recovery root=" + root.filename().string() +
                " marker IDs=" + Json(ids(restored)).dump() + " checkpoint pending=" +
                checkpoint.at("entries")[0].at("states").at("entry_pending").dump() +
                " saved_time=" + checkpoint.at("entries")[0].at("time").dump() +
                " time=" + restored.pose().at("time").dump() + " pose=" + restored.pose().dump());
    Fixture pending(root, doc, actor);
    Fixture pending_restored(root, doc, actor);
    pending_restored.animation->restore(pending.animation->checkpoint());
    pending_restored.animation->tick(.5f, 1);
    require(ids(pending_restored) == std::vector<std::uint32_t>{1, 2},
            "Recovery lost pending entry");
    Fixture transitions(root, doc, actor);
    transitions.animation->tick(.4f, 1);
    transitions.state(2);
    transitions.animation->tick(.2f, 2);
    require(ids(transitions) == std::vector<std::uint32_t>{5},
            "Outgoing transition emitted duplicate markers");
    transitions.state(3);
    transitions.animation->tick(.5f, 3);
    require(ids(transitions) == std::vector<std::uint32_t>{8, 9, 10},
            "Interrupted nonloop markers");
    near(transitions.animation->events()[1].offset, .125, "Scaled marker offset");
    transitions.animation->tick(.5f, 4);
    require(transitions.animation->events().empty(), "Completed nonloop replayed markers");
    Fixture stopped(root, doc, actor);
    auto entity = stopped.scene.entity(actor.str());
    auto config = entity.get<Animator>();
    config.playback_speed = 0;
    entity.set(config);
    stopped.animation->tick(2.f, 1);
    require(stopped.animation->events().empty(), "Zero speed emitted markers");
    config.playback_speed = 1;
    config.enabled = false;
    entity.set(config);
    stopped.animation->tick(2.f, 2);
    require(stopped.animation->events().empty(), "Disabled player emitted markers");
    config.enabled = true;
    entity.set(config);
    stopped.animation->tick(.5f, 3);
    require(ids(stopped) == std::vector<std::uint32_t>{1, 2}, "Pause lost pending entry markers");
    Fixture muted(root, doc, actor);
    muted.animation->event_delivery(false);
    muted.animation->tick(4.f, 1);
    require(muted.animation->events().empty(), "Muted preview emitted gameplay markers");
    auto multiple = doc;
    Json marker_config;
    for (const auto& row : doc.at("entities"))
        if (row.at("components").contains("forge.animator"))
            marker_config = row.at("components").at("forge.animator");
    const std::array<std::string, 3> entity_ids{"00000000-0000-4000-8000-000000000001",
                                                "00000000-0000-4000-8000-000000000002",
                                                "00000000-0000-4000-8000-000000000003"};
    multiple["entities"] = Json::array();
    for (auto i = entity_ids.rbegin(); i != entity_ids.rend(); ++i)
        multiple["entities"].push_back({{"id", *i},
                                        {"name", "Marker player"},
                                        {"spatial", {{"mode", "world"}}},
                                        {"components", {{"forge.animator", marker_config}}}});
    Fixture together(root, multiple, Json(entity_ids[0]).get<EntityId>());
    together.animation->tick(.5f, 1);
    const auto events = together.animation->events();
    require(events.size() == 6, "Multiple marker players did not broadcast");
    for (std::size_t i = 0; i < events.size(); ++i)
        require(events[i].entity.entity.str() == entity_ids[i % 3] &&
                    events[i].marker == (i < 3 ? 1 : 2),
                "Same-time markers did not use stable entity ordering");
    rejects([&] { together.animation->tick(1000.f, 2); },
            "Global marker budget only checked each player");
    require(together.animation->events().empty(),
            "Global overflow delivered earlier players' partial events");
    Fixture overflow(root, doc, actor);
    rejects([&] { overflow.animation->tick(4096.f, 1); }, "Marker overflow silently truncated");
    require(overflow.animation->events().empty(), "Marker overflow published partial events");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 5 || argc == 6,
                "Need scratch root, model worker, converter and BlendColumn source");
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
        AssetImportService models(lease, registry, {"linux", "none", "cpu"}, 1);
        models.submit(
            models.prepare("Assets/BlendColumn.gltf"),
            [](auto& c, const auto& p, const auto& cat) { prepare_model_publication(c, p, cat); },
            [](const auto&, const auto&) {});
        require(models.wait_idle(60s), "Model publication timeout");
        auto results = models.poll();
        require(results.size() == 1 && results.front().published,
                results.empty() ? "No model result" : results.front().diagnostic);
        auto catalog = AssetCatalog::open_project(root);
        AssetId model;
        for (const auto& [id, r] : catalog.records())
            if (r.type == ModelAsset::type)
                model = id;
        const auto selection = load_model_selection(root, catalog, model);
        const auto first = selection.bindings.at("/animations/0"),
                   second = selection.bindings.at("/animations/1");
        AssetId skeleton;
        for (const auto& [id, r] : catalog.records())
            if (r.type == SkeletonAsset::type)
                skeleton = id;
        const auto graph = AssetId::generate();
        auto source = AnimationStatesSource::create(graph);
        source.document["skeleton"] = skeleton;
        source.document["entry"] = 1;
        source.document["next_id"] = 4;
        source.document["states"] = Json::array();
        for (unsigned id = 1; id <= 3; ++id)
            source.document["states"].push_back({{"id", id},
                                                 {"name", id == 1   ? "Idle"
                                                          : id == 2 ? "Reach"
                                                                    : "Return"},
                                                 {"clip", id == 2 ? second : first},
                                                 {"loop", id != 3},
                                                 {"speed", id == 3 ? 4 : 1},
                                                 {"transition_seconds", .4}});
        source.validate();
        exit_source_tests(source, lease, root);
        auto decoded = decode_animation_states(source.cook(), graph);
        require(decoded.state("Reach").id == 2 && decoded.state(0).id == 1,
                "Named/default state lookup");
        auto invalid = source;
        invalid.document["states"][1]["id"] = 1;
        rejects([&] { invalid.validate(); }, "Duplicate IDs admitted");
        invalid = source;
        invalid.document["states"][1]["speed"] = -1;
        rejects([&] { invalid.validate(); }, "Negative speed admitted");
        invalid = source;
        invalid.document["entry"] = 99;
        rejects([&] { invalid.validate(); }, "Missing entry admitted");
        atomic_write(root / "Assets/Test.states.json", source.document.dump(2) + '\n');
        AnimationStatesDocument history(lease, "Assets/Test.states.json");
        history.edit(history.revision(), "Rename/reorder", [](Json& d) {
            d["states"][1]["name"] = "Raised";
            std::swap(d["states"][0], d["states"][1]);
        });
        require(history.source().compiled().state(2).name == "Raised", "Rename/reorder lost ID");
        history.undo();
        require(history.source().document == source.document, "State source Undo");
        history.redo();
        history.undo();
        auto identity_history =
            AnimationStatesDocument::create(lease, "Assets/History.states.json");
        identity_history->edit(identity_history->revision(), "Allocate state", [&](Json& d) {
            d["states"] = Json::array({{{"id", 1},
                                        {"name", "First"},
                                        {"clip", first},
                                        {"loop", true},
                                        {"speed", 1},
                                        {"transition_seconds", 0}}});
            d["entry"] = 1;
            d["next_id"] = 2;
        });
        identity_history->save();
        identity_history->undo();
        require(identity_history->source().document.at("next_id") == 2,
                "Undo reused a saved state ID");
        rejects(
            [&] {
                identity_history->edit(identity_history->revision(), "Reuse state", [&](Json& d) {
                    d["states"] = Json::array({{{"id", 1},
                                                {"name", "Different"},
                                                {"clip", second},
                                                {"loop", true},
                                                {"speed", 1},
                                                {"transition_seconds", 0}}});
                    d["entry"] = 1;
                });
            },
            "Previously allocated state ID reused after Undo");
        identity_history->redo();
        require(identity_history->source().document.at("states")[0].at("id") == 1,
                "Redo changed the restored state identity");
        identity_history.reset();
        AssetImportService states(lease, animation_states_import_registry(),
                                  {"linux", "none", "cpu"}, 1);
        states.submit(
            states.prepare("Assets/Test.states.json", {}, graph),
            [](auto& c, const auto& p, const auto&) { prepare_animation_states_publication(c, p); },
            [](const auto&, const auto&) {});
        require(states.wait_idle(10s), "State publication timeout");
        results = states.poll();
        require(results.size() == 1 && results.front().published,
                results.empty() ? "No states result" : results.front().diagnostic);
        catalog = AssetCatalog::open_project(root);
        require(catalog.dependency_graph().dependencies(graph).size() == 4,
                "State dependency closure");
        const auto published_key =
            catalog.records().at(graph).metadata.at("forge.import").at("key");
        std::filesystem::copy_file(root / "Assets/BlendColumn.gltf", root / "Assets/Other.gltf");
        models.submit(
            models.prepare("Assets/Other.gltf"),
            [](auto& c, const auto& p, const auto& cat) { prepare_model_publication(c, p, cat); },
            [](const auto&, const auto&) {});
        require(models.wait_idle(60s), "Second rig publication timeout");
        results = models.poll();
        require(results.size() == 1 && results.front().published,
                "Second rig fixture publication failed");
        catalog = AssetCatalog::open_project(root);
        AssetId foreign;
        for (const auto& [id, record] : catalog.records())
            if (record.type == AnimationClipAsset::type && record.source == "Assets/Other.gltf")
                foreign = id;
        require(bool(foreign), "Second rig clip missing");
        invalid = source;
        invalid.document["states"][1]["clip"] = foreign;
        atomic_write(root / "Assets/Test.states.json", invalid.document.dump());
        states.submit(
            states.prepare("Assets/Test.states.json", {}, graph),
            [](auto& c, const auto& p, const auto&) { prepare_animation_states_publication(c, p); },
            [](const auto&, const auto&) {});
        require(states.wait_idle(10s), "Invalid state publication did not finish");
        results = states.poll();
        require(results.size() == 1 && !results.front().published &&
                    results.front().diagnostic.find("same exact imported model revision") !=
                        std::string::npos,
                "A clip from another imported model was admitted");
        require(AssetCatalog::open_project(root)
                        .records()
                        .at(graph)
                        .metadata.at("forge.import")
                        .at("key") == published_key,
                "Failed state publication replaced last-good selection");
        atomic_write(root / "Assets/Test.states.json", source.document.dump(2) + '\n');
        EngineContext author;
        Scene authored(author.world());
        ModelPlacementOptions options;
        options.clip = {first};
        auto actor = instantiate_model(
            authored, catalog,
            prepare_model_placement(selection, authored.asset_id(), authored.revision(), options));
        apply_authoring(authored,
                        Json::array({{{"operation", "property.set"},
                                      {"arguments",
                                       {{"entity", actor.str()},
                                        {"component", "forge.animator"},
                                        {"field", "states"},
                                        {"value", graph}}}}}),
                        authored.revision());
        const auto doc = authored.document();
        auto marker_source = source;
        const auto marker_graph = AssetId::generate();
        marker_source.document["asset_id"] = marker_graph;
        marker_source.document["next_marker_id"] = 11;
        for (auto& row : marker_source.document["states"]) {
            const auto state = row.at("id").get<unsigned>();
            auto& markers = row["markers"] = Json::array();
            if (state == 1) {
                for (auto [id, phase] : {std::pair{1, 0.}, {2, .25}, {3, .75}, {4, 1.}})
                    markers.push_back(
                        {{"id", id},
                         {"name", id == 2 || id == 4 ? "Pulse" : "Idle" + std::to_string(id)},
                         {"phase", phase},
                         {"value", id == 2   ? 1.
                                   : id == 4 ? 0.
                                             : double(id)}});
            } else {
                const auto id = state == 2 ? 5 : 8;
                for (auto [offset, phase] : {std::pair{0, 0.}, {1, .25}, {2, state == 2 ? .5 : 1.}})
                    markers.push_back({{"id", id + offset},
                                       {"name", "Action" + std::to_string(id + offset)},
                                       {"phase", phase},
                                       {"value", double(id + offset)}});
            }
        }
        marker_source.validate();
        for (const auto* field : {"phase", "value", "id"}) {
            auto bad = marker_source;
            bad.document["states"][0]["markers"][0][field] =
                field == std::string_view("phase")   ? Json(1.1)
                : field == std::string_view("value") ? Json(1e7)
                                                     : Json(1.5);
            rejects([&] { bad.validate(); }, "Invalid marker numeric field admitted");
        }
        auto duplicate = marker_source;
        duplicate.document["states"][1]["markers"][0]["id"] = 1;
        rejects([&] { duplicate.validate(); }, "Cross-state marker ID duplicated");
        auto too_many = marker_source;
        too_many.document["states"][0]["markers"] = Json::array();
        too_many.document["next_marker_id"] = 100;
        for (unsigned id = 11; id < 76; ++id)
            too_many.document["states"][0]["markers"].push_back(
                {{"id", id}, {"name", "Hit"}, {"phase", .5}, {"value", 0}});
        rejects([&] { too_many.validate(); }, "Marker per-state limit ignored");
        auto set_limit = marker_source;
        set_limit.document["states"] = Json::array();
        set_limit.document["next_id"] = 10;
        set_limit.document["next_marker_id"] = 577;
        for (unsigned i = 0; i < 9; ++i) {
            auto row = source.document.at("states")[0];
            row["id"] = i + 1;
            row["name"] = "State" + std::to_string(i);
            row["markers"] = Json::array();
            for (unsigned j = 0; j < 64; ++j)
                row["markers"].push_back(
                    {{"id", i * 64 + j + 1}, {"name", "Hit"}, {"phase", .5}, {"value", 0}});
            set_limit.document["states"].push_back(row);
        }
        rejects([&] { set_limit.validate(); }, "Marker set limit ignored");
        auto old = source;
        old.document.erase("next_marker_id");
        old.validate();
        auto marker_history_source = source;
        marker_history_source.document["asset_id"] = AssetId::generate();
        atomic_write(root / "Assets/MarkerHistory.states.json",
                     marker_history_source.document.dump(2));
        AnimationStatesDocument marker_history(lease, "Assets/MarkerHistory.states.json");
        marker_history.edit(marker_history.revision(), "Allocate marker", [](Json& d) {
            d["next_marker_id"] = 2;
            d["states"][0]["markers"] =
                Json::array({{{"id", 1}, {"name", "Hit"}, {"phase", .5}, {"value", 0}}});
        });
        marker_history.save();
        marker_history.undo();
        require(marker_history.source().document.at("next_marker_id") == 2,
                "Saved marker ID reused by Undo");
        rejects(
            [&] {
                marker_history.edit(marker_history.revision(), "Reuse marker", [](Json& d) {
                    d["states"][0]["markers"] = Json::array(
                        {{{"id", 1}, {"name", "Different"}, {"phase", .5}, {"value", 0}}});
                });
            },
            "Previously allocated marker ID reused");
        marker_history.redo();
        require(marker_history.source().document.at("states")[0].at("markers")[0].at("id") == 1,
                "Redo lost stable marker ID");
        atomic_write(root / "Assets/Markers.states.json", marker_source.document.dump(2));
        states.submit(
            states.prepare("Assets/Markers.states.json", {}, marker_graph),
            [](auto& c, const auto& p, const auto&) { prepare_animation_states_publication(c, p); },
            [](const auto&, const auto&) {});
        require(states.wait_idle(10s), "Marker publication timeout");
        results = states.poll();
        require(results.size() == 1 && results.front().published,
                results.empty() ? "No marker publication" : results.front().diagnostic);
        auto marker_doc = doc;
        for (auto& row : marker_doc["entities"])
            if (row.at("components").contains("forge.animator"))
                row["components"]["forge.animator"]["states"] = marker_graph;
        if (argc != 6)
            control_tests(root, marker_doc, actor, marker_source);
        playback_tests(root, marker_doc, actor, marker_source);
        marker_tests(root, marker_doc, actor);
        if (argc != 6 || std::string_view(argv[5]) == "--exits-prepare") {
            const auto exits =
                exit_runtime_tests(root, lease, marker_doc, actor, marker_source, argc == 6);
            if (argc == 6) {
                atomic_write(root / "main.scene.json", exits.dump(2));
                std::cout << root.string() << '\n';
                return 0;
            }
        }
        if (argc == 6) {
            const bool markers = std::string_view(argv[5]) == "--markers-prepare";
            require(markers || std::string_view(argv[5]) == "--prepare",
                    "Unknown preparation mode");
            atomic_write(root / "main.scene.json", (markers ? marker_doc : doc).dump(2));
            std::cout << root.string() << '\n';
            return 0;
        }
        auto legacy = doc;
        for (auto& row : legacy.at("entities"))
            if (row.at("components").contains("forge.animator")) {
                auto& value = row["components"]["forge.animator"];
                value.erase("states");
                value.erase("state");
            }
        Fixture legacy_player(root, legacy, actor);
        legacy_player.animation->tick(.2f);
        near(legacy_player.pose().at("time"), .2,
             "Existing direct-clip scene did not retain additive compatibility");
        Fixture runtime(root, doc, actor);
        require(runtime.pose().at("state") == 1, "Entry state did not play");
        runtime.animation->tick(.5f);
        runtime.state(2);
        auto before = runtime.pose().at("local");
        runtime.animation->synchronize();
        require(runtime.pose().at("state") == 1, "Pause adopted a state switch");
        runtime.animation->tick(.1f);
        auto pose = runtime.pose();
        require(pose.at("state") == 2, "C++ state selection failed");
        near(pose.at("time"), .1, "Incoming clip clock");
        near(pose.at("blend_weight"), .25, "Transition weight");
        // Ozz samples native quaternion keys with normalized linear interpolation.
        // Idle is .6 through its 0 -> 10 degree segment; Reach is .2 through 0 -> 75.
        // Then the two normalized local rotations mix at incoming weight .25.
        auto quaternion = [](double angle, double phase) {
            const double half = angle * 3.141592653589793 / 360;
            const double z = phase * std::sin(half), w = 1 - phase + phase * std::cos(half);
            const double length = std::sqrt(z * z + w * w);
            return std::array{z / length, w / length};
        };
        const auto idle = quaternion(10, .6), reach = quaternion(75, .2);
        const double z = .75 * idle[0] + .25 * reach[0];
        const double w = .75 * idle[1] + .25 * reach[1];
        const double norm = std::sqrt(z * z + w * w);
        bool checked = false;
        for (const auto& local : pose.at("local"))
            if (std::abs(local.at("rotation")[2].get<double>()) > .001) {
                near(local.at("rotation")[2], z / norm, "Independent-phase crossfade quaternion");
                near(local.at("rotation")[3], w / norm, "Independent-phase normalized quaternion");
                near(local.at("translation")[0], .0175,
                     "Unanimated outgoing channel uses rest pose");
                checked = true;
            }
        require(checked, "No animated joint pose evaluated");
        auto checkpoint = runtime.animation->checkpoint();
        require(!checkpoint.is_null(), "State checkpoint not ready");
        Fixture recovered(root, runtime.scene.document(), actor);
        recovered.animation->restore(checkpoint);
        require(recovered.pose().at("local") == pose.at("local"),
                "Live outgoing transition recovery pose changed");
        auto bad = checkpoint;
        bad["entries"][0]["states"]["revision"] = std::string(64, '0');
        rejects([&] { recovered.animation->restore(bad); }, "Wrong state revision restored");
        require(recovered.animation->checkpoint() == checkpoint,
                "Failed state recovery mutated clocks");
        runtime.state(3);
        runtime.animation->prepare_preview_state();
        same_pose(runtime.pose().at("local"), pose.at("local"),
                  "Interrupted transition jumped before its next tick");
        runtime.animation->tick(.1f);
        auto interrupted = runtime.animation->checkpoint();
        Fixture interrupted_copy(root, runtime.scene.document(), actor);
        interrupted_copy.animation->restore(interrupted);
        same_pose(interrupted_copy.pose().at("local"), runtime.pose().at("local"),
                  "Frozen interruption recovery changed pose");
        bad = interrupted;
        bad["entries"][0]["states"]["frozen"][0]["rotation"] = {0, 0, 0, 0};
        rejects([&] { interrupted_copy.animation->restore(bad); },
                "Invalid frozen quaternion restored");
        require(interrupted_copy.animation->checkpoint() == interrupted,
                "Frozen recovery failure mutated state");
        auto config = runtime.scene.entity(actor.str()).get<Animator>();
        config.playback_speed = 0;
        runtime.scene.entity(actor.str()).set(config);
        runtime.animation->prepare_preview_state();
        const auto held = runtime.animation->checkpoint();
        runtime.animation->tick(.2f);
        require(runtime.animation->checkpoint() == held, "Speed zero advanced a state transition");
        config.playback_speed = 1;
        runtime.scene.entity(actor.str()).set(config);
        runtime.animation->tick(.4f);
        near(runtime.pose().at("blend_weight"), 1, "Transition did not complete");
        runtime.state(99);
        runtime.animation->tick(.1f);
        require(runtime.pose().at("state") == 3 && !runtime.animation->checkpoint_ready(),
                "Missing state replaced last-good playback");
        rejects([&] { (void)runtime.animation->checkpoint(); },
                "Invalid desired state checkpointed");
        runtime.state(1);
        runtime.animation->tick(.1f);
        require(runtime.pose().at("state") == 1, "Valid state did not recover from a bad request");
        Fixture stepped(root, doc, actor);
        RuntimeSimulation simulation(stepped.engine.world(), stepped.scene);
        RuntimeClock clock;
        clock.pause(RuntimeClock::Time{});
        stepped.state(2);
        const auto paused = stepped.pose();
        clock.advance(RuntimeClock::Time{} + 1s, [&](float dt) { simulation.tick(dt); });
        require(stepped.pose() == paused, "Paused user clock changed animation states");
        clock.step([&](float dt) { simulation.tick(dt); });
        require(stepped.pose().at("state") == 2, "Fixed Step did not adopt requested state");
        near(stepped.pose().at("time"), 1. / 60, "State Step did not use one fixed tick");
        auto disabled = stepped.scene.entity(actor.str()).get<Animator>();
        disabled.enabled = false;
        stepped.scene.entity(actor.str()).set(disabled);
        stepped.animation->prepare_preview_state();
        const auto disabled_checkpoint = stepped.animation->checkpoint();
        simulation.tick(.1f);
        require(stepped.animation->checkpoint() == disabled_checkpoint,
                "Disabled Animator advanced a state transition");
        Fixture endpoint(root, doc, actor);
        endpoint.state(3);
        auto fast = endpoint.scene.entity(actor.str()).get<Animator>();
        fast.playback_speed = 4;
        endpoint.scene.entity(actor.str()).set(fast);
        endpoint.animation->tick(.15f);
        near(endpoint.pose().at("time"), 2, "Nonlooping state did not stop at clip endpoint");
        require(endpoint.pose().at("playing").get<bool>(),
                "Short clip stopped before its fade finished");
        endpoint.animation->tick(.3f);
        near(endpoint.pose().at("blend_weight"), 1, "Short nonlooping state fade did not finish");
        require(!endpoint.pose().at("playing").get<bool>(),
                "Finished nonlooping state kept playing");
        // Recovery may begin before the asynchronous state-set/catalog/model admission finishes.
        EngineContext cold(WorldRole::Runtime, false, {animation_module(root)});
        Scene cold_scene(cold.world());
        cold_scene.restore_snapshot(recovered.scene.document());
        auto cold_animation = animation_runtime(cold.world());
        cold_animation->restore(checkpoint);
        require(cold_animation->checkpoint() == checkpoint,
                "Cold state recovery did not admit dependencies atomically");
        SkeletalAnimationPreview preview;
        preview.start(root, doc, {authored.asset_id(), actor});
        while (!preview.prepare())
            std::this_thread::sleep_for(1ms);
        preview.select_state(2);
        near(preview.pose().at("time"), 0, "Preview state command advanced time");
        preview.tick(.2f);
        require(preview.pose().at("state_name") == "Reach", "Preview state selection differs");
        preview.stop();
        require(authored.document() == doc, "State preview mutated authored scene");
        atomic_write(root / "main.scene.json", doc.dump(2));
        auto marker_export_doc = marker_doc;
        const auto marker_scene = AssetId::generate();
        marker_export_doc["asset_id"] = marker_scene;
        atomic_write(root / "markers.scene.json", marker_export_doc.dump(2));
        const std::array roots{authored.asset_id(), marker_scene};
        prepare_runtime_content_catalog(*lease, roots, {}, authored.schema());
        const auto package = root.parent_path() / ("states-package-" + AssetId::generate().str());
        package_runtime_content(root, package, roots, {"linux", "none"}, {}, {}, authored.schema());
        const auto shipped = open_runtime_content(package, {"linux", "none"});
        require(shipped.records().contains(graph) && shipped.records().contains(first) &&
                    shipped.records().contains(second),
                "Export omitted state-set dependencies");
        require(!std::filesystem::exists(package / "Assets/Test.states.json") &&
                    !std::filesystem::exists(package / "Assets/BlendColumn.gltf"),
                "Authored sources shipped in runtime");
        std::filesystem::rename(root / "Assets", root / "Assets-offline");
        const auto relocated =
            root.parent_path() / ("states-relocated-" + AssetId::generate().str());
        std::filesystem::rename(package, relocated);
        Fixture exported(relocated, doc, actor);
        exported.state(2);
        exported.animation->tick(.2f);
        require(exported.pose().at("state") == 2, "Relocated source-free state transition failed");
        near(exported.pose().at("blend_weight"), .5, "Exported state transition differs");
        require(shipped.records().contains(marker_graph), "Export omitted marker state set");
        require(!std::filesystem::exists(relocated / "Assets/Markers.states.json"),
                "Marker authoring source shipped");
        marker_tests(relocated, marker_export_doc, actor);

        std::cout << "State schema/history/publication, C++ selection, live/interrupted "
                     "transitions, pause/speed, recovery and preview passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
