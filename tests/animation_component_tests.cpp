#include "animation_component_values.hpp"
#include "animation_playback_access.hpp"
#include "authored_schema.hpp"
#include "transform_animation_authoring.hpp"
#include "transform_animation_preview.hpp"
#include <chrono>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <source_location>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected,
          std::source_location location = std::source_location::current()) {
    if (std::abs(actual - expected) >= 1e-5)
        throw std::runtime_error("Component animation numeric oracle at line " +
                                 std::to_string(location.line()) + ": " + std::to_string(actual) +
                                 " vs " + std::to_string(expected));
}
template <class F>
void rejects(F action, std::source_location location = std::source_location::current()) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid component animation accepted at line " +
                             std::to_string(location.line()));
}
struct Motion {
    float speed = 20;
    double weight = .5;
    std::int32_t ticks = 0;
    std::string label = "retained";
};
EngineModule motion_module() {
    EngineModule module;
    module.id = "project.motion";
    module.dependencies = {"forge.transforms"};
    module.schemas = [](ModuleContext& context) {
        const auto type = context.world.component<Motion>();
        // Match the native layout explicitly, including its owned C++ string.
        ecs_struct_desc_t descriptor{};
        descriptor.entity = type;
        const std::array names{"speed", "weight", "ticks", "label"};
        const std::array ids{context.world.id<float>(), context.world.id<double>(),
                             context.world.id<std::int32_t>(), context.world.id<std::string>()};
        const std::array offsets{offsetof(Motion, speed), offsetof(Motion, weight),
                                 offsetof(Motion, ticks), offsetof(Motion, label)};
        for (std::size_t i = 0; i < names.size(); ++i) {
            auto& member = descriptor.members[i];
            member.name = names[i];
            member.type = ids[i];
            member.offset = std::int32_t(offsets[i]);
            member.use_offset = true;
        }
        descriptor.members[0].range = {-200, 200};
        check(ecs_struct_init(context.world, &descriptor) != 0,
              "Motion native Meta registration failed");
        detail::opt_in_authoring(
            context.world, type, "project.motion", "project.motion", 1,
            {{"speed", 20}, {"weight", .5}, {"ticks", 0}, {"label", "retained"}}, "Gameplay");
    };
    return module;
}
std::vector<double> sample(const TransformAnimationSource& source, double time) {
    const auto clip = source.clip();
    std::vector<double> result(clip.component_bindings.size());
    clip.sample_components(time, result);
    return result;
}
void source_checks(const AnimationComponentBinding& binding) {
    auto source = TransformAnimationSource::create(AssetId::generate());
    const auto first = source.clip().keys.front().id;
    source.add_component_field(binding, 20);
    source.document["channels"] = Json::array();
    const auto last = source.set_key(2 * animation_timebase, {});
    source.set_component_value(last, binding.component, binding.field, 100);
    near(sample(source, animation_timebase)[0], 60);
    source.document["keys"][0]["component_values"][0]["future"] = "retained";
    source.set_component_curve(first, binding.component, binding.field, {true, 0, 120});
    near(sample(source, animation_timebase)[0], 90);
    source.document["keys"][0]["component_values"][0]["curve"]["future"] = 7;
    source.set_component_curve(first, binding.component, binding.field, {true, 0, 120});
    check(source.document["keys"][0]["component_values"][0]["curve"]["future"] == 7,
          "Component curve editing lost unknown settings");
    auto retimed = source;
    retimed.retime_duration(4 * animation_timebase);
    near(sample(retimed, 2 * animation_timebase)[0], 90);
    auto held = source;
    held.document["keys"][0]["interpolation"] = "held";
    near(sample(held, animation_timebase)[0], 20);
    auto both = source;
    both.add_material_parameter({"default", "roughnessFactor", MaterialParameterType::Scalar},
                                {.5});
    both.add_property(AnimationProperty::LightIntensity, 1);
    both.set_property_curve(first, AnimationProperty::LightIntensity, {true, 0, 0});
    check(both.document.at("version") == 6 && both.clip().component_bindings.size() == 1,
          "Other track editing downgraded component tracks");
    const auto decoded = decode_transform_animation(source.cook(), source.asset());
    check(decoded.component_bindings == source.clip().component_bindings,
          "Component bindings changed through cooked decoding");
    for (int version = 1; version <= 5; ++version) {
        auto old = TransformAnimationSource::create(AssetId::generate());
        old.document["version"] = version;
        old.document["component_bindings"] = "unknown retained extension";
        const auto unchanged = old.document;
        rejects([&] { old.add_component_field(binding, 1); });
        check(old.document == unchanged, "Source promotion collision changed legacy data");
        old.document.erase("component_bindings");
        old.add_component_field(binding, 1);
        check(old.document.at("version") == 6, "Legacy source failed explicit promotion");
    }
    auto invalid = source;
    invalid.document["keys"][0]["component_values"].push_back(
        invalid.document["keys"][0]["component_values"][0]);
    rejects([&] { invalid.validate(); });
    invalid = source;
    invalid.document["component_bindings"].push_back(invalid.document["component_bindings"][0]);
    rejects([&] { invalid.validate(); });
    const auto before = source.document;
    rejects([&] { source.set_component_value(last, binding.component, binding.field, 1e10); });
    rejects([&] {
        source.set_component_curve(first, binding.component, binding.field, {true, 0, 1e10});
    });
    auto overshoot = source;
    overshoot.retime_duration(10 * animation_timebase);
    rejects([&] {
        overshoot.set_component_curve(first, binding.component, binding.field, {true, 0, 1e9});
    });
    check(source.document == before, "Rejected component edit changed source");
    source.set_key(animation_timebase, {});
    near(sample(source, animation_timebase)[0], 90);
    check(source.document["keys"][0]["component_values"][0]["future"] == "retained",
          "Key editing lost unknown metadata");
}
struct PaddedOwnedState {
    std::string label = "default-owned";
    float speed = 90;
    bool enabled = true;
    std::uint16_t counter = 11; // Runtime state intentionally outside reflected members.
};
void padding_updates() {
    EngineModule module;
    module.id = "project.padding";
    module.dependencies = {"forge.transforms"};
    module.schemas = [](ModuleContext& context) {
        const auto type = context.world.component<PaddedOwnedState>()
                              .member<std::string>("label")
                              .member<float>("speed")
                              .member<bool>("enabled");
        detail::opt_in_authoring(context.world, type, "project.padding", "project.padding", 1,
                                 {{"label", "default-owned"}, {"speed", 90}, {"enabled", true}},
                                 "Gameplay");
    };
    EngineContext engine(WorldRole::Runtime, false, {module});
    Scene scene(engine.world());
    const auto& codec = engine.world().authored_codecs().front();
    const auto actor = EntityId::generate();
    scene.reset(
        {{"version", 3},
         {"asset_id", AssetId::generate()},
         {"entities", Json::array({{{"id", actor},
                                    {"name", "Runtime state"},
                                    {"components", {{"project.padding", codec.defaults()}}}}})}});
    auto entity = scene.entity(actor.str());
    auto state = entity.get<PaddedOwnedState>();
    state.counter = 73;
    state.label = std::string(96, 'x');
    entity.set(state);
    scene.tune_runtime_property(actor.str(), codec.key(), "speed", 120);
    check(entity.get<PaddedOwnedState>().counter == 73 &&
              entity.get<PaddedOwnedState>().label == state.label,
          "Live property tuning reset unreflected or owned native state");
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.add_component_field({codec.key(), "speed", codec.stamp()}, 120);
    source.document["channels"] = Json::array();
    const auto last = source.set_key(animation_timebase, {});
    source.set_component_value(last, codec.key(), "speed", 360);
    auto prepared = prepare_animation_components(engine.world(), entity, source.clip(),
                                                 animation_timebase / 2, true);
    check(entity.get<PaddedOwnedState>().speed == 120 &&
              entity.get<PaddedOwnedState>().counter == 73,
          "Detached native update mutated its source");
    for (const auto& candidate : prepared)
        candidate.codec->apply(entity, candidate.value);
    near(entity.get<PaddedOwnedState>().speed, 240);
    check(entity.get<PaddedOwnedState>().counter == 73 &&
              entity.get<PaddedOwnedState>().label == state.label,
          "Animated field update reset unreflected or owned native state");
}
struct Runtime {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    Runtime(const std::filesystem::path& root, const Json& source,
            std::shared_ptr<const AssetCatalog> catalog)
        : engine(WorldRole::Runtime, false, {animation_module(root), motion_module()}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        animation->catalog(std::move(catalog));
    }
    void ready() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Component animation admission stalled");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
void playback(const std::filesystem::path& root) {
    padding_updates();
    EngineContext authored(WorldRole::Authoring, false, {motion_module()});
    Scene scene(authored.world());
    const auto& codec = authored.world().authored_codecs().front();
    const AnimationComponentBinding speed{"project.motion", "speed", codec.stamp()};
    source_checks(speed);
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Motion.animation.json");
    auto source = document->source();
    source.add_component_field(speed, 20);
    source.add_component_field({speed.component, "weight", speed.admission}, .5);
    source.document["channels"] = {"forge.local_translation"};
    source.add_property(AnimationProperty::LightIntensity, 2);
    LocalTransform endpoint;
    endpoint.translation.x = 4;
    const auto last = source.set_key(2 * animation_timebase, endpoint);
    source.set_component_value(last, speed.component, speed.field, 100);
    source.set_component_value(last, speed.component, "weight", .9);
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    document->edit(document->revision(), "Animated fields",
                   [&](auto& value) { value = source.document; });
    document->undo();
    check(document->source().clip().component_bindings.empty(), "Field binding Undo failed");
    document->redo();
    document->save();
    auto publish = [&] {
        importer.submit(
            importer.prepare(document->locator(), {}, source.asset()),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(importer.wait_idle(10s), "Component clip publication stalled");
        const auto results = importer.poll();
        check(results.size() == 1 && results[0].published, "Component clip publication failed");
        return std::make_shared<const AssetCatalog>(results[0].publication->catalog);
    };
    const auto catalog = publish();
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Animated motion"}})
                           .at("selected")
                           .get<std::string>();
    auto entity = scene.entity(actor);
    codec.apply(entity, codec.prepare(scene.world(), codec.defaults()));
    entity.set<Light>({});
    TransformAnimator animator;
    animator.clip = {source.asset()};
    entity.set(animator);
    const auto saved = scene.snapshot();
    auto preview = scene.effective_document();
    const auto unmodified = preview;
    const auto schema = scene.schema();
    const EntityRef reference{scene.asset_id(), EntityId::parse(actor)};
    check(apply_transform_animation_preview(preview, reference, source.clip(), animation_timebase,
                                            &schema),
          "Component preview failed");
    near(preview["entities"][0]["components"]["project.motion"]["speed"].get<double>(), 60);
    check(scene.effective_document() == unmodified && entity.get<Motion>().speed == 20,
          "Copied component preview mutated native or authored data");
    auto bad_schema = schema;
    for (auto& type : bad_schema["components"])
        if (type.at("id") == speed.component)
            for (auto& field : type["fields"])
                if (field.at("id") == speed.field)
                    field["read_only"] = true;
    auto bad_preview = unmodified;
    rejects([&] {
        apply_transform_animation_preview(bad_preview, reference, source.clip(), 0, &bad_schema);
    });
    check(bad_preview == unmodified, "Rejected preview changed its copied document");
    Runtime live(root, saved, catalog);
    live.ready();
    auto runtime_entity = live.scene.entity(actor);
    auto value = runtime_entity.get<Motion>();
    value.ticks = 73;
    value.label = "runtime-owned";
    runtime_entity.set(value);
    double observed = 0;
    auto behavior = live.scene.world()
                        .system<Motion>()
                        .kind(live.scene.world().lookup("forge.runtime.Gameplay"))
                        .each([&](Motion& state) {
                            observed = state.speed;
                            ++state.ticks;
                        });
    behavior.add<FixedSimulation>();
    live.simulation.tick(.5f);
    near(observed, 20); // Gameplay precedes Animation in the established fixed pipeline.
    near(runtime_entity.get<Motion>().speed, 40);
    near(runtime_entity.get<Motion>().weight, .6);
    check(runtime_entity.get<Motion>().ticks == 74 &&
              runtime_entity.get<Motion>().label == "runtime-owned",
          "Animation reset another native field or owned string");
    live.simulation.tick(.25f);
    near(observed, 40); // The actual C++ system consumes the committed animated value.
    near(runtime_entity.get<Motion>().speed, 50);
    check(runtime_entity.get<Motion>().ticks == 75 &&
              runtime_entity.get<Motion>().label == "runtime-owned",
          "A later animated update reset another native field");
    const auto native_before = live.scene.document();
    const auto patch = live.animation->presentation_properties(runtime_entity.id(), .5);
    near(patch["project.motion"]["speed"].get<double>(), 45);
    check(live.scene.document() == native_before, "Presentation sampling mutated native state");
    const auto checkpoint = live.animation->checkpoint();
    Runtime recovered(root, live.scene.snapshot(), catalog);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    near(recovered.scene.entity(actor).get<Motion>().speed, 50);
    recovered.simulation.tick(.25f);
    near(recovered.scene.entity(actor).get<Motion>().speed, 60);
    check(recovered.scene.entity(actor).get<Motion>().label == "runtime-owned",
          "Recovery lost owned state");
    AnimationPlaybackAccess::control(*live.animation, reference, {{"operation", "pause"}});
    live.simulation.tick(.25f);
    near(runtime_entity.get<Motion>().speed, 50);
    AnimationPlaybackAccess::control(*live.animation, reference,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    near(runtime_entity.get<Motion>().speed, 80);
    auto out_of_range = source;
    out_of_range.set_component_value(last, speed.component, speed.field, 300);
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), runtime_entity, out_of_range.clip(),
                                           0, true);
    });
    check(runtime_entity.get<Motion>().speed == 80, "Rejected whole-curve admission mutated state");
    auto overshoot = source;
    overshoot.set_component_curve(overshoot.clip().keys.front().id, speed.component, speed.field,
                                  {true, 0, 800});
    check(overshoot.clip().component_range(0)[1] > 200,
          "Cubic target-range fixture did not overshoot");
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), runtime_entity, overshoot.clip(), 0,
                                           true);
    });
    auto wrong_stamp = source;
    wrong_stamp.document["component_bindings"][0]["admission"]["digest"] = std::string(64, '1');
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), runtime_entity, wrong_stamp.clip(),
                                           0, true);
    });
    auto integer = source;
    integer.add_component_field({speed.component, "ticks", speed.admission}, 0);
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), runtime_entity, integer.clip(), 0,
                                           true);
    });
    check(runtime_entity.get<Motion>().speed == 80 &&
              runtime_entity.get<Motion>().label == "runtime-owned",
          "Rejected component candidate changed native state");
    auto reordered = source;
    std::reverse(reordered.document["component_bindings"].begin(),
                 reordered.document["component_bindings"].end());
    for (auto& key : reordered.document["keys"])
        std::reverse(key["component_values"].begin(), key["component_values"].end());
    const auto reordered_values = animation_component_patch(prepare_animation_components(
        live.engine.world(), runtime_entity, reordered.clip(), animation_timebase, true));
    near(reordered_values["project.motion"]["speed"].get<double>(), 60);
    near(reordered_values["project.motion"]["weight"].get<double>(), .7);
    auto missing = saved;
    missing["entities"][0]["components"].erase("project.motion");
    Runtime absent(root, missing, catalog);
    rejects([&] { absent.ready(); });
    near(absent.scene.entity(actor).get<LocalTranslation>().x, 0);
    check(scene.snapshot() == saved, "Playback changed authored data");
    auto component_only = source;
    component_only.document["channels"] = Json::array();
    document->edit(document->revision(), "Component-only looping",
                   [&](auto& value) { value = component_only.document; });
    document->save();
    Runtime looping(root, saved, publish());
    looping.ready();
    for (unsigned i = 0; i < 3; ++i)
        looping.simulation.tick(.5f);
    looping.simulation.tick(.25f);
    looping.simulation.tick(.5f);
    const auto loop_actor = looping.scene.entity(actor);
    near(loop_actor.get<Motion>().speed, 30);
    near(looping.animation->presentation_properties(loop_actor.id(), 0)["project.motion"]["speed"]
             .get<double>(),
         30);
    // Engine-owned reconstructed values are sufficient for preview; no project systems are
    // installed.
    EngineContext copied(
        WorldRole::Runtime, false,
        {detail::copied_authoring_module(scene.authored_types(), role_mask(WorldRole::Runtime))});
    Scene copied_scene(copied.world());
    copied_scene.restore_snapshot(saved);
    const auto candidates = prepare_animation_components(copied.world(), copied_scene.entity(actor),
                                                         source.clip(), animation_timebase, true);
    near(animation_component_patch(candidates)["project.motion"]["speed"].get<double>(), 60);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need component animation scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        playback(root);
        std::filesystem::remove_all(root);
        std::cout << "Component clip source, native behavior, copied preview, admission and "
                     "recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
