#include "application_state_authoring.hpp"
#include "asset_bytes.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace forge;
using namespace std::chrono_literals;
constexpr auto scene_path = "Scenes/135-state-activity.scene.json";
constexpr auto states_path = "Assets/UI/StateActivity/Activity.appstates.json";
constexpr auto clip_path = "Assets/UI/StateActivity/Spin.animation.json";
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
AssetId asset_at(const std::filesystem::path& root, std::string_view source) {
    const auto catalog = AssetCatalog::open_project(root);
    for (const auto& [id, record] : catalog.records())
        if (record.source == source)
            return id;
    throw std::runtime_error("Missing Gallery dependency: " + std::string(source));
}
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId asset, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font);
        return presenter.inspect_static({asset});
    };
}
void publish(AssetImportService& imports, const std::filesystem::path& path, AssetId id,
             bool states) {
    imports.submit(
        imports.prepare(path, {}, id),
        [states](auto& candidate, const auto& plan, const auto&) {
            if (states)
                prepare_application_states_publication(candidate, plan);
            else
                prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(10s), "Activity asset publication timed out");
    const auto results = imports.poll();
    check(results.size() == 1 && results.front().published,
          results.empty() ? "Missing Activity publication" : results.front().diagnostic);
}
void prepare(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    const auto directory = root / "Assets/UI/StateActivity";
    std::filesystem::create_directories(directory);
    atomic_write(directory / "hud.rml",
                 R"rml(<rml><head><link type="text/rcss" href="hud.rcss"/></head><body>
<div id="panel"><p class="eyebrow">FORGE / STATE CONDITIONS</p><h1>Activity Lab</h1>
<p>One structural group. Independent runtime systems.</p>
<p class="mode">Mode: {{forge_state_app}}</p>
<button id="run" data-event-click="command('forge.state', 'app:playing:' + forge_rev_app)">Run</button>
<button id="rest" data-event-click="command('forge.state', 'app:rest:' + forge_rev_app)">Rest</button>
<button id="hide" data-event-click="command('forge.state', 'app:menu:' + forge_rev_app)">Hide all</button>
<div class="rule"></div><p><b>Run</b> activates the spinning cube, falling body, particles, sound and badge.</p>
<p><b>Rest</b> keeps the group marker. <b>Hide all</b> disables the complete group.</p>
<p class="small">Select an object in the editor to inspect its State Condition. The authored enable fields stay unchanged.</p>
<p class="small">Game paused: {{paused}} / Fixed tick: {{tick}}</p>
<button id="pause" data-event-click="command('Pause')">Pause</button>
<button id="resume" data-event-click="command('Resume')">Resume</button>
<button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml");
    atomic_write(directory / "badge.rml",
                 R"rml(<rml><head><link type="text/rcss" href="hud.rcss"/></head><body>
<div id="activity-badge"><p class="eyebrow">COMPUTED CONDITION</p><h2>Activity enabled</h2>
<p>This document follows <b>badge:visible</b>.</p></div>
</body></rml>)rml");
    atomic_write(directory / "hud.rcss",
                 R"rcss(body,div,p,h1,h2 { display: block; }
body { width: 100%; height: 100%; margin: 0; font-family: Lato; font-size: 17px; color: #e7f0f4; pointer-events: none; }
#panel { position: absolute; top: 22px; left: 22px; width: 88%; max-width: 330px; max-height: 60%; box-sizing: border-box; overflow: auto; padding: 20px; background-color: #122a35ec; border-radius: 12px; pointer-events: auto; }
h1 { font-size: 34px; margin: 5px 0 12px; color: #ffffff; } h2 { font-size: 23px; margin: 7px 0; }
p { margin: 10px 0; line-height: 1.35; } .eyebrow { color: #78d9df; font-size: 12px; letter-spacing: 1px; }
.mode { color: #9ef0c3; font-size: 20px; } .small { font-size: 14px; color: #b5c9d3; }
button { color: #ffffff; background-color: #285366; padding: 9px 12px; margin: 3px 3px 3px 0; border-radius: 5px; }
button:hover { background-color: #35798c; } #run { background-color: #247759; }
.rule { height: 1px; background-color: #46707d; margin: 14px 0; }
#activity-badge { position: absolute; bottom: 22px; right: 22px; width: 80%; max-width: 276px; box-sizing: border-box; padding: 18px; background-color: #174c46ee; border-radius: 10px; }
scrollbarvertical { width: 12px; background-color: #17333f; }
scrollbarvertical sliderbar { background-color: #4b7d89; }
)rcss");
    const auto lease = std::make_shared<ProjectLease>(root);
    AssetId states, spin;
    if (std::filesystem::exists(root / states_path))
        states = ApplicationStatesDocument(lease, states_path).source().asset();
    else {
        auto document = ApplicationStatesDocument::create(lease, states_path, [](AssetId id) {
            auto source = ApplicationStatesSource::create(id);
            source.document["domains"] = Json::array(
                {{{"key", "app"},
                  {"label", "Activity mode"},
                  {"initial", "menu"},
                  {"states",
                   Json::array({{{"key", "menu"}, {"label", "Hidden"}},
                                {{"key", "game"}, {"label", "Group visible"}},
                                {{"key", "playing"}, {"label", "Running"}, {"parent", "game"}},
                                {{"key", "rest"}, {"label", "Resting"}, {"parent", "game"}}})}},
                 {{"key", "badge"},
                  {"label", "Activity badge"},
                  {"computed", true},
                  {"initial", "hidden"},
                  {"states", Json::array({{{"key", "hidden"}}, {{"key", "visible"}}})},
                  {"rules", Json::array({{{"state", "visible"},
                                          {"all", Json::array({{{"domain", "app"},
                                                                {"state", "playing"}}})}}})}}});
            return source;
        });
        states = document->source().asset();
        document->save();
        AssetImportService imports(lease, application_states_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        publish(imports, document->locator(), states, true);
    }
    if (std::filesystem::exists(root / clip_path))
        spin = TransformAnimationDocument(lease, clip_path).source().asset();
    else {
        auto document = TransformAnimationDocument::create(lease, clip_path);
        document->edit(document->revision(), "Configure spin", [](Json& json) {
            TransformAnimationSource source{json};
            source.retime_duration(2 * animation_timebase);
            source.document["channels"] = {"forge.local_rotation"};
            source.set_key(animation_timebase, {{}, rotation_from_euler({0, 180, 0}), {1, 1, 1}});
            source.set_key(2 * animation_timebase,
                           {{}, rotation_from_euler({0, 360, 0}), {1, 1, 1}});
            json = source.document;
        });
        document->save();
        spin = document->source().asset();
        AssetImportService imports(lease, transform_animation_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        publish(imports, document->locator(), spin, false);
    }
    const auto hud = register_ui_document(root, "Assets/UI/StateActivity/hud.rml");
    const auto badge = register_ui_document(root, "Assets/UI/StateActivity/badge.rml");
    WorldContext world;
    Scene scene(world);
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        const auto create = [&](const char* name, bool cube = false) {
            auto entity = scene.entity(authoring_command(scene, "entity.create", {{"name", name}})
                                           .at("selected")
                                           .get<std::string>());
            entity.set<Primitive>({cube ? 0u : no_primitive});
            return entity;
        };
        create("Activity controls - always available").set<UiDocument>({{hud.id}, true, true, 0});
        create("Activity modes - inspect Application States").set<ApplicationStates>({{states}});
        auto camera = create("Game camera");
        camera.set<Camera>({})
            .set<LocalTranslation>({0, 5, -14})
            .set<LocalRotation>(rotation_from_euler({15, 0, 0}));
        auto light = create("Sun");
        light.set<Light>({}).set<LocalRotation>(rotation_from_euler({45, -25, 0}));
        create("Floor", true)
            .set<LocalTranslation>({0, -.25f, 0})
            .set<LocalScale>({14, .5f, 8})
            .set<Tint>({.16f, .23f, .29f})
            .set<PhysicsBody>({})
            .set<BoxCollider>({});
        auto group = create("Activity group - structural condition");
        group.set<LocalTranslation>({}).set<ApplicationStateGate>({true, "app", "game"});
        create("Group marker - visible during Rest", true)
            .child_of(group)
            .set<LocalTranslation>({2, .15f, 0})
            .set<LocalScale>({5, .3f, 2})
            .set<Tint>({.15f, .65f, .64f});
        create("Spinning cube - inspect State Condition", true)
            .child_of(group)
            .set<LocalTranslation>({1, 1.5f, 0})
            .set<Tint>({.2f, .65f, 1})
            .set<TransformAnimator>({{spin}})
            .set<ApplicationStateGate>({true, "app", "playing"});
        PhysicsBody body;
        body.motion = 2;
        body.restitution = .65f;
        create("Falling body - World space follows structural condition", true)
            .child_of(group)
            .set<SpatialBinding>({SpatialMode::World, {}})
            .set<LocalTranslation>({3.5f, 4, 0})
            .set<Tint>({1, .5f, .18f})
            .set<PhysicsBody>(body)
            .set<BoxCollider>({})
            .set<ApplicationStateGate>({true, "app", "playing"});
        create("Fountain - starts again on activation")
            .child_of(group)
            .set<LocalTranslation>({5, .35f, 0})
            .set<ParticleEmitter>(
                {{asset_at(root, "Assets/Particles/BlueFountain.particles.json")}})
            .set<ApplicationStateGate>({true, "app", "playing"});
        AudioSource sound;
        sound.clip.id = asset_at(root, "Assets/FalloffTone.wav");
        sound.loop = sound.play_on_start = true;
        sound.spatialized = false;
        sound.gain = .08f;
        create("Activity tone")
            .child_of(group)
            .set<AudioSource>(sound)
            .set<ApplicationStateGate>({true, "app", "playing"});
        create("Computed activity badge")
            .child_of(group)
            .set<UiDocument>({{badge.id}, true, true, 1})
            .set<ApplicationStateGate>({true, "badge", "visible"});
    }
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, inspector(root, font), scene.schema());
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font,
              const Json& source) {
    GameSessionConfig config;
    config.content_root = root;
    config.ui = true;
    config.audio = AudioConfig{root, AudioOutput::Offline, true};
    GameSession game(config);
    const auto ticket = game.prepare(source);
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!game.poll_preparation(ticket).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Activity scene preparation timed out");
        std::this_thread::sleep_for(1ms);
    }
    game.activate(ticket, {}, false);
    auto& world = game.active();
    const auto original = world.scene.snapshot();
    auto ui = std::static_pointer_cast<UiRuntime>(world.engine.services().ui());
    auto audio = std::static_pointer_cast<AudioRuntime>(world.engine.services().audio());
    UiTestRenderer renderer;
    UiPresenter presenter(renderer, root, font);
    const auto snapshot = [&] {
        return ui->snapshot(world.scene, "state-activity", ticket,
                            game.status().at("clock").at("tick"), true);
    };
    const auto update = [&] {
        auto state = snapshot();
        check(presenter.accept(state), presenter.diagnostic());
        presenter.update(.01, 1180, 760);
        return state;
    };
    presenter.reset("state-activity", ticket);
    check(update().at("documents").size() == 1 && world.physics()->status().at("bodies") == 1,
          "Hidden Activity scene retained gated documents or bodies");
    ui_protocol::CommandGate commands;
    commands.reset("state-activity", ticket);
    const auto click = [&](const char* id) {
        auto* document = Rml::GetContext(0)->GetDocument(0);
        check(document && document->GetElementById(id), "Missing Activity control");
        document->GetElementById(id)->DispatchEvent("click", Rml::Dictionary{});
        check(presenter.pending_command().has_value(), "Activity control did not queue an action");
        const auto ack = commands.dispatch(*presenter.pending_command(), [&](const auto& request) {
            ui->command(world.scene, request, [&](const auto& action) {
                if (action == "Pause")
                    game.pause({});
                else if (action == "Resume")
                    game.resume({});
                else if (action == "Step")
                    game.step();
            });
        });
        check(ack.at("ok").get<bool>(), "Activity control failed");
        presenter.acknowledge(ack);
        return update();
    };
    check(click("run").at("documents").size() == 2, "Computed Activity badge did not activate");
    deadline = std::chrono::steady_clock::now() + 10s;
    while (!animation_runtime(world.engine.world())->prepare_initial_pose() ||
           !particle_runtime(world.engine.world())->prepare()) {
        world.simulation.presentation(1);
        check(std::chrono::steady_clock::now() < deadline, "Activity resources timed out");
        std::this_thread::sleep_for(1ms);
    }
    check(audio->wait_for_sources(5s), "Activity tone preparation failed");
    for (unsigned i = 0; i < 30; ++i)
        game.step();
    const auto active = extract_render_scene(world.simulation.presentation(1));
    check(active.cameras.size() == 1 && active.meshes.size() == 4 && active.particles.size() == 1 &&
              world.physics()->status().at("bodies") == 2 &&
              audio->status().at("source_voices") == 1,
          "Running Activity scene did not activate its stock consumers");
    const auto tick = game.status().at("clock").at("tick");
    check(click("rest").at("documents").size() == 1 &&
              world.physics()->status().at("bodies") == 1 &&
              audio->status().at("source_voices") == 0 &&
              extract_render_scene(world.simulation.presentation(1)).meshes.size() == 2 &&
              game.status().at("clock").at("tick") == tick,
          "Rest lost ancestor membership, retained inactive consumers or advanced time");
    click("hide");
    check(extract_render_scene(world.simulation.presentation(1)).meshes.size() == 1,
          "Parent condition failed to hide the structural marker");
    if (std::filesystem::exists(root / scene_path))
        check(read(root / scene_path) == source, "Activity playback rewrote saved Scene");
    presenter.render();
    check(renderer.draws > 0 && original.at("asset_id").get<AssetId>() == world.scene.asset_id(),
          "Activity native UI produced no geometry or replaced the Scene");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5, "Need Gallery, scratch, font and optional --prepare");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        if (argc == 5) {
            check(std::string_view(argv[4]) == "--prepare", "Unknown Activity preparation option");
            prepare(root, font);
            exercise(root, font, read(root / scene_path));
            std::cout << path_utf8(root) << '\n';
            return 0;
        }
        const auto source = read(root / scene_path);
        exercise(root, font, source);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        const auto package = scratch / "Export";
        package_runtime_content(root, package, roots, {"windows", "d3d12"}, {}, {}, {},
                                inspector(root, font));
        const auto moved = scratch / "Relocated game";
        std::filesystem::rename(package, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(!std::filesystem::exists(moved / states_path) &&
                  !std::filesystem::exists(moved / clip_path),
              "Activity export included editable state or animation source");
        std::filesystem::rename(root, scratch / "Sources offline");
        exercise(moved, font, read(moved / shipped.records().at(roots[0]).source));
        std::filesystem::remove_all(scratch);
        std::cout << "Activity Gallery: native UI actions, hierarchy/computed conditions, stock "
                     "playback/physics/audio/particles and relocated source-free export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
