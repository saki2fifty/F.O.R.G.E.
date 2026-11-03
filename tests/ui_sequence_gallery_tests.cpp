#include "animation_component_values.hpp"
#include "animation_playback_access.hpp"
#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include "transform_animation_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/151-sequence-ui.scene.json";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    if (std::abs(actual - expected) > 1e-4)
        throw std::runtime_error("Sequence UI oracle: " + std::to_string(actual) + " vs " +
                                 std::to_string(expected));
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId id, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font);
        return presenter.inspect_static({id});
    };
}
void author(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    check(!std::filesystem::exists(root / scene_path), "Sequence UI Gallery already exists");
    const auto directory = root / "Assets/UI/Sequence";
    std::filesystem::create_directories(directory);
    atomic_write(directory / "hud.rml",
                 R"rml(<rml><head><link type="text/rcss" href="hud.rcss" /></head>
<body><div id="panel"><p id="eyebrow">FORGE / SCENE TIMELINE</p>
<h1>Sequence-driven UI</h1><p>One scene clock drives named UI Values.</p>
<div class="track"><div id="fill" data-style-width="width + 'px'" data-style-opacity="amount" data-class-bright="amount >= 0.6"></div></div>
<p id="amount">Opacity: {{amount}}</p><p>Width: {{width}} px</p>
<p id="state" data-class-paused="paused">Pause holds the bar. The small pulse keeps moving.</p>
<div id="pulse"></div><div class="buttons"><button id="pause" data-event-click="command('Pause')">Pause</button>
<button id="resume" data-event-click="command('Resume')">Resume</button>
<button id="step" data-event-click="command('Step')">Step</button></div>
<p class="hint">The two-second child starts at 1s of a four-second loop. Inspect the HUD's UI Values and Transform Animator. No C++ compiler required.</p>
</div></body></rml>)rml");
    atomic_write(
        directory / "hud.rcss",
        R"rcss(body {width:100%;height:100%;font-family:Lato;font-size:18px;color:#edf3fa;pointer-events:none;}
body,div,h1,p {display:block;}
#panel {position:absolute;left:5%;top:5%;width:90%;max-width:680px;max-height:90%;box-sizing:border-box;padding:20px;overflow:auto;background-color:#182b3af2;pointer-events:auto;}
h1 {font-size:28px;margin:0 0 14px;} p {margin:12px 0;}
#eyebrow {font-size:14px;color:#65c9dd;} .track {height:32px;background-color:#101d29;overflow:hidden;}
#fill {width:80px;height:32px;background-color:#48b6d5;opacity:0.2;} #fill.bright {background-color:#e9b75b;}
#state {padding:10px;background-color:#285a74;} #state.paused {background-color:#986026;}
#pulse {width:16px;height:16px;background-color:#65c9dd;animation:1s linear-in infinite alternate pulse-motion;}
@keyframes pulse-motion {from {opacity:0.2;} to {opacity:1;}}
button {display:inline-block;padding:9px 12px;margin:12px 6px 4px 0;background-color:#34597b;}
button:hover,button:focus {background-color:#5997c7;} .hint {font-size:15px;color:#b4c4d4;}
scrollbarvertical {width:12px;background-color:#101d29;} scrollbarvertical sliderbar {background-color:#5997c7;}
)rcss");
    const auto ui = register_ui_document(root, "Assets/UI/Sequence/hud.rml");
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read(root / "Scenes/100-ui-animation.scene.json"),
                                        AssetId::generate(), scene.schema());
    for (auto& row : source["entities"])
        if (row["components"].contains("forge.ui_document")) {
            row["name"] = "HUD - keyed Timeline values";
            row["components"]["forge.ui_document"]["document"] = ui.id;
        }
    scene.reset(source);
    std::string hud;
    const auto initial = scene.snapshot();
    for (const auto& row : initial.at("entities"))
        if (row.at("components").contains("forge.ui_document"))
            hud = row.at("id");
    check(!hud.empty(), "Gallery HUD missing");
    scene.entity(hud).set<UiValues>({{{"amount", .2}, {"width", 80}}});
    const auto schema = scene.schema();
    const auto& types = schema.at("components");
    const auto declaration = std::find_if(types.begin(), types.end(), [](const auto& type) {
        return type.at("id") == "forge.ui_values";
    });
    check(declaration != types.end(), "UI Values schema missing");
    const Json values = {{"values", Json::array({{{"name", "amount"}, {"value", .2}},
                                                 {{"name", "width"}, {"value", 80}}})}};
    const auto fields = animation_component_fields(*declaration, &values);
    const auto lease = std::make_shared<ProjectLease>(root);
    auto document =
        TransformAnimationDocument::create(lease, "Assets/Sequences/UiValues.animation.json");
    document->edit(document->revision(), "Sequence UI values", [&](Json& json) {
        TransformAnimationSource clip{json};
        clip.add_component_field(fields.at(0), .2);
        clip.add_component_field(fields.at(1), 80);
        clip.document["channels"] = Json::array();
        const auto middle = clip.set_key(animation_timebase, {});
        clip.set_component_value(middle, fields.at(0), 1.);
        clip.set_component_value(middle, fields.at(1), 360.);
        const auto last = clip.set_key(2 * animation_timebase, {});
        clip.set_component_value(last, fields.at(0), .2);
        clip.set_component_value(last, fields.at(1), 80.);
        json = clip.document;
    });
    document->save();
    const auto clip = document->source().asset();
    AssetImportService imports(lease, transform_animation_import_registry(),
                               {"windows", "d3d12", "desktop"});
    imports.submit(
        imports.prepare("Assets/Sequences/UiValues.animation.json", {}, clip),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(20)), "UI clip publication timed out");
    const auto receipts = imports.poll();
    check(receipts.size() == 1 && receipts[0].published, "UI clip publication failed");
    const auto parent =
        authoring_command(scene, "entity.create",
                          {{"recipe", "animation.sequence"}, {"name", "Root - four-second loop"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence outer;
    outer.duration = 4;
    outer.loop = true;
    scene.entity(parent).set(outer);
    const auto child = authoring_command(scene, "entity.create",
                                         {{"recipe", "animation.sequence"},
                                          {"name", "Child - two seconds placed at 1s"}})
                           .at("selected")
                           .get<std::string>();
    AnimationSequence inner;
    inner.duration = 2;
    inner.sequence = {scene.asset_id(), EntityId::parse(parent)};
    inner.sequence_start_seconds = 1;
    scene.entity(child).set(inner);
    TransformAnimator track{{clip}};
    track.sequence = {scene.asset_id(), EntityId::parse(child)};
    scene.entity(hud).set(track);
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, inspector(root, font), scene.schema());
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    std::shared_ptr<UiRuntime> ui;
    World(const std::filesystem::path& root, const Json& source, bool admit = true)
        : engine(WorldRole::Runtime, false, {animation_module(root), ui_module(root)}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())),
          ui(std::static_pointer_cast<UiRuntime>(engine.services().ui())) {
        scene.restore_snapshot(source);
        if (!admit)
            return;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline, "UI sequence admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
    }
};
void exercise(const std::filesystem::path& root, const Json& source,
              const std::vector<std::byte>& font) {
    World live(root, source);
    EntityRef controller;
    std::string hud;
    for (const auto& row : source.at("entities")) {
        if (row.at("components").contains("forge.ui_values"))
            hud = row.at("id");
        if (row.at("name") == "Root - four-second loop")
            controller = {live.scene.asset_id(), row.at("id").get<EntityId>()};
    }
    check(bool(controller.entity) && !hud.empty(), "Sequence UI owners missing");
    auto seek = [&](double seconds) {
        AnimationPlaybackAccess::control(*live.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
    };
    const auto amount = [&] { return live.scene.entity(hud).get<UiValues>().values.at(0).value; };
    for (const auto& [time, expected] : std::array<std::pair<double, double>, 7>{
             {{0, .2}, {1, .2}, {1.5, .6}, {2, 1}, {2.5, .6}, {3, .2}, {3.5, .2}}}) {
        seek(time);
        near(amount(), expected);
    }
    seek(1.5);
    const auto checkpoint = live.animation->checkpoint();
    World cold(root, source, false);
    cold.animation->restore(checkpoint);
    near(cold.scene.entity(hud).get<UiValues>().values.at(0).value, .6);
    near(cold.animation->playback(controller)->time, 1.5);
    auto config = live.scene.entity(controller.entity.str()).get<AnimationSequence>();
    config.playback_speed = 2;
    live.scene.entity(controller.entity.str()).set(config);
    live.simulation.tick(.25f);
    near(amount(), 1);
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("ui-sequence", 1);
        const auto snapshot = [&] {
            return live.ui->snapshot(live.scene, "ui-sequence", 1, 0, true);
        };
        check(presenter.accept(snapshot()), presenter.diagnostic().c_str());
        presenter.update(0, 640, 480);
        presenter.render();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* fill = document->GetElementById("fill");
        auto* pulse = document->GetElementById("pulse");
        near(fill->GetProperty<float>("width"), 360);
        near(fill->GetProperty<float>("opacity"), 1);
        check(fill->IsClassSet("bright"), "Sequence bound class did not change");
        document->GetElementById("resume")->Focus();
        const auto revision = presenter.presentation_revision();
        seek(1.5);
        check(presenter.accept(snapshot()), "Seek UI snapshot rejected");
        presenter.update(.05, 640, 480);
        near(fill->GetProperty<float>("width"), 220);
        near(fill->GetProperty<float>("opacity"), .6);
        const auto first = pulse->GetProperty<float>("opacity");
        for (int i = 2; i <= 10; ++i) {
            check(presenter.accept(snapshot()), "Held UI snapshot rejected");
            presenter.update(i * .05, 640, 480);
            presenter.render();
        }
        near(fill->GetProperty<float>("width"), 220);
        check(pulse->GetProperty<float>("opacity") > first &&
                  document->GetFocusLeafNode()->GetId() == "resume" &&
                  presenter.presentation_revision() == revision &&
                  Rml::GetContext(0)->GetDocument(0) == document,
              "Held sequence stopped native CSS or value updates replaced document/focus");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Sequence UI native resources survived retirement");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch root and font");
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2], font);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(scratch);
        const auto root = scratch / "source";
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read(root / scene_path);
        exercise(root, source, font);
        check(read(root / scene_path) == source, "Playback changed the authored Scene file");
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, inspector(root, font));
        }
        const auto destination = scratch / "export";
        package_runtime_content(root, destination, roots, {"windows", "d3d12"}, {}, {}, {},
                                inspector(root, font));
        const auto moved = scratch / "relocated";
        std::filesystem::rename(destination, moved);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(catalog.records().size() == 4, "Sequence UI package lost/duplicated closure");
        std::filesystem::rename(root, scratch / "source-offline");
        exercise(moved, read(moved / catalog.records().at(roots[0]).source), font);
        std::cout << "Sequence-driven UI native width/opacity/class/focus, nested "
                     "seek/rate/recovery, independent CSS and relocated content passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
