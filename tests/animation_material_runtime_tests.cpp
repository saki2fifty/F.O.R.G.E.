#include "animation_material_inputs.hpp"
#include "animation_playback_access.hpp"
#include "engine_render_resource.hpp"
#include "transform_animation_authoring.hpp"
#include <chrono>
#include <cmath>
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <iostream>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double value, double expected) {
    if (std::abs(value - expected) > 1e-5)
        throw std::runtime_error("Animated material runtime oracle: " + std::to_string(value) +
                                 " vs " + std::to_string(expected));
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid material animation runtime operation accepted");
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& root, const Json& source, bool resources = true)
        : engine(WorldRole::Runtime, false,
                 resources ? std::vector{runtime_resources_module(root), animation_module(root)}
                           : std::vector{animation_module(root)}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
    }
    void ready(const char* stage = "Material preparation") {
        const auto end = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            if (engine.services().available(Capability::Resources))
                engine.services().resources()->synchronize();
            try {
                if (animation->prepare_initial_pose())
                    break;
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string(stage) + ": " + error.what() + "\n" +
                                         Json(engine.services().diagnostics()).dump(2));
            }
            check(std::chrono::steady_clock::now() < end, "Material animation admission stalled");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
double roughness(const MeshRenderer& renderer) {
    for (const auto& value : renderer.material_parameters)
        if (value.key == "roughnessFactor")
            return value.parameter.value[0];
    throw std::runtime_error("Runtime failed to write material parameter");
}
void subscriptions(const std::filesystem::path& root) {
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
    const auto service = engine.services().resources();
    AnimationMaterialInputs inputs;
    MeshRenderer renderer{engine_primitive(0)};
    bool ready = false;
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (!ready) {
        service->synchronize();
        inputs.begin(service);
        ready = true;
        for (unsigned actor = 0; actor < 256; ++actor)
            try {
                const auto selected = inputs.select(renderer);
                check(selected.mesh && selected.materials.size() == 1,
                      "Expected the builtin Cube's one exact material selection");
            } catch (const AnimationMaterialPending&) {
                ready = false;
            }
        inputs.finish();
        check(std::chrono::steady_clock::now() < end, "Shared animation subscriptions stalled");
        if (!ready)
            std::this_thread::sleep_for(1ms);
    }
    std::vector<std::uint64_t> external;
    for (unsigned n = 0; n < 254; ++n)
        external.push_back(service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id));
    rejects([&] { service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id); });
    inputs.begin(service);
    bool replacement_pending = false;
    try {
        (void)inputs.select(MeshRenderer{engine_primitive(3)});
    } catch (const AnimationMaterialPending&) {
        replacement_pending = true;
    }
    check(replacement_pending, "A new animation mesh bypassed resource admission");
    inputs.finish(); // Retire the old Cube/material before requesting the Plane at capacity.
    for (const auto token : external)
        check(service->release(token), "External subscription was lost");
    // Clearing this consumer releases its two shared subscriptions.
    inputs.clear();
    external.clear();
    for (unsigned n = 0; n < 256; ++n)
        external.push_back(service->request(RuntimeResourceKind::Mesh, engine_primitive(0).id));
    for (const auto token : external)
        check(service->release(token), "Animation retained an unused shared subscription");
}
void playback(const std::filesystem::path& root) {
    const auto began = std::chrono::steady_clock::now();
    const auto stage = [&](const char* name) {
        std::cout << name << ": "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count()
                  << " seconds\n";
    };
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Material.animation.json");
    auto source = document->source();
    const auto slot = asset_detail::engine_mesh_resource(engine_primitive(0)).materials.front().key;
    source.add_material_parameter({slot, "roughnessFactor", MaterialParameterType::Scalar}, {.2});
    source.add_property(AnimationProperty::LightIntensity, 2);
    source.document["channels"] = {"forge.local_translation"};
    LocalTransform pose;
    pose.translation.x = 4;
    const auto last = source.set_key(2 * animation_timebase, pose);
    source.set_material_value(last, slot, "roughnessFactor", {.8});
    auto properties = source.clip().keys.back().properties;
    properties[unsigned(AnimationProperty::LightIntensity)] = 6;
    source.set_key(2 * animation_timebase, pose, false, last, {}, properties);
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    auto publish = [&](const TransformAnimationSource& value) {
        document->edit(document->revision(), "Material runtime fixture",
                       [&](auto& json) { json = value.document; });
        document->save();
        importer.submit(
            importer.prepare(document->locator(), {}, document->source().asset()),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto& artifact) {
                check(artifact.files.size() == 1, "Material clip cook missing");
            });
        check(importer.wait_idle(10s), "Material clip publication stalled");
        const auto results = importer.poll();
        check(results.size() == 1, "Material clip publication returned no receipt");
        if (!results[0].published)
            throw std::runtime_error("Material clip publication failed: " + results[0].diagnostic);
        return std::make_shared<const AssetCatalog>(results[0].publication->catalog);
    };
    auto catalog = publish(source);
    EngineContext authored;
    Scene scene(authored.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Material cube"}})
                           .at("selected")
                           .get<std::string>();
    auto entity = scene.entity(actor);
    entity.set<MeshRenderer>({engine_primitive(0)});
    entity.set<Light>({});
    TransformAnimator config;
    config.clip = {document->source().asset()};
    entity.set<TransformAnimator>(config);
    auto saved = scene.document();
    auto& renderer = saved["entities"][0]["components"]["forge.mesh_renderer"];
    renderer["material_parameters"] =
        Json::array({{{"slot", slot},
                      {"key", "roughnessFactor"},
                      {"parameter", {{"type", 0}, {"value", {.2, 0, 0, 0}}, {"future", 83}}},
                      {"future", "keep"}}});
    const auto original = saved;
    const EntityRef ref{scene.asset_id(), EntityId::parse(actor)};
    World live(root, saved);
    live.animation->catalog(catalog);
    live.ready("Initial material playback");
    stage("Initial mixed-family admission");
    near(roughness(live.scene.entity(actor).get<MeshRenderer>()), .2);
    live.simulation.tick(.5f);
    near(roughness(live.scene.entity(actor).get<MeshRenderer>()), .35);
    near(live.scene.entity(actor).get<Light>().intensity, 3);
    near(live.scene.entity(actor).get<LocalTranslation>().x, 1);
    const auto native = live.scene.entity(actor).get<MeshRenderer>();
    const auto presentation = live.simulation.presentation(.5);
    const auto& value =
        presentation["entities"][0]["components"]["forge.mesh_renderer"]["material_parameters"][0];
    near(value["parameter"]["value"][0].get<double>(), .275);
    check(value.at("future") == "keep" && value.at("parameter").at("future") == 83 &&
              live.scene.entity(actor).get<MeshRenderer>() == native && saved == original,
          "Interpolated material presentation lost extensions or mutated its source/native value");
    const auto checkpoint = live.animation->checkpoint();
    stage("Interpolated presentation");
    World recovered(root, live.scene.snapshot());
    recovered.animation->catalog(catalog);
    recovered.animation->restore(checkpoint);
    stage("Recovered exact runtime");
    near(roughness(recovered.scene.entity(actor).get<MeshRenderer>()), .35);
    recovered.simulation.tick(.25f);
    near(roughness(recovered.scene.entity(actor).get<MeshRenderer>()), .425);
    AnimationPlaybackAccess::control(*live.animation, ref, {{"operation", "pause"}});
    AnimationPlaybackAccess::control(*live.animation, ref,
                                     {{"operation", "seek"}, {"position_seconds", 1}});
    near(roughness(live.scene.entity(actor).get<MeshRenderer>()), .5);
    near(live.scene.entity(actor).get<Light>().intensity, 4);
    auto missing_component = saved;
    missing_component["entities"][0]["components"].erase("forge.mesh_renderer");
    World missing(root, missing_component);
    missing.animation->catalog(catalog);
    const auto before = missing.scene.document();
    rejects([&] { missing.ready(); });
    check(missing.scene.document() == before,
          "Missing material target partially wrote a transform or another property");
    World no_resources(root, saved, false);
    no_resources.animation->catalog(catalog);
    rejects([&] { no_resources.ready(); });
    stage("Missing component/provider rejection");
    // A cook that is source-valid but incompatible with this target retains all
    // last-good native values, including the same clip's other property family.
    auto bad = source;
    const auto first = source.clip().keys.front().id;
    bad.set_material_value(first, slot, "roughnessFactor", {.5});
    bad.set_material_value(last, slot, "roughnessFactor", {.5});
    bad.set_material_curve(first, slot, "roughnessFactor", 0, {true, 0, 4});
    bad.set_material_curve(last, slot, "roughnessFactor", 0, {true, -4, 0});
    catalog = publish(bad);
    live.animation->catalog(catalog);
    const auto retained = live.scene.document();
    rejects([&] { live.ready(); });
    stage("Invalid replacement rejection");
    check(live.scene.document() == retained, "Rejected material replacement lost last-good state");
    live.animation->catalog(publish(source));
    live.ready("Restored valid material selection");
    stage("Corrected replacement admission");
    near(roughness(live.scene.entity(actor).get<MeshRenderer>()), .5);
    auto two = saved;
    const auto other = EntityId::generate().str();
    auto second = two["entities"][0];
    second["id"] = other;
    second["name"] = "Independent material cube";
    two["entities"].push_back(std::move(second));
    World pair(root, two);
    pair.animation->catalog(publish(source));
    pair.ready("Two material players");
    const auto valid = pair.scene.entity(actor).get<MeshRenderer>();
    auto invalid = valid;
    invalid.materials.push_back({"missing-slot", engine_material(EngineMaterial::Default)});
    pair.scene.entity(actor).set<MeshRenderer>(invalid);
    pair.simulation.tick(.25f);
    near(pair.scene.entity(actor).get<Light>().intensity, 2);
    near(pair.scene.entity(other).get<Light>().intensity, 2);
    pair.scene.entity(actor).set<MeshRenderer>(valid);
    pair.ready("Corrected material target without ticking");
    near(roughness(pair.scene.entity(actor).get<MeshRenderer>()), .275);
    near(roughness(pair.scene.entity(other).get<MeshRenderer>()), .275);
    stage("Whole-candidate target rejection and paused repair");
    subscriptions(root);
    stage("Shared subscription saturation and retirement");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need material animation scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        playback(root);
        std::filesystem::remove_all(root);
        std::cout << "Actual material clip publication, runtime, presentation, recovery, failure "
                     "atomicity and shared subscriptions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
