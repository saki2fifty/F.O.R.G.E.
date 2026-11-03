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
#include <limits>
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
enum class MotionMode : std::int32_t { Forward = 1, Reverse = -1 };
struct Motion {
    float speed = 20;
    double weight = .5;
    std::int32_t ticks = 0;
    std::string label = "retained";
    bool enabled = true;
    std::int64_t marker = 0;
    std::uint64_t score = 0;
    MotionMode mode = MotionMode::Forward;
    std::uint32_t flags = 0;
};
EngineModule motion_module() {
    EngineModule module;
    module.id = "project.motion";
    module.dependencies = {"forge.transforms"};
    module.schemas = [](ModuleContext& context) {
        context.world.component<MotionMode>()
            .constant("Forward", MotionMode::Forward)
            .constant("Reverse", MotionMode::Reverse);
        ecs_bitmask_desc_t mask{};
        mask.entity = context.world.entity("project.MotionFlags");
        mask.constants[0].name = "Left";
        mask.constants[0].value = 1;
        mask.constants[1].name = "Right";
        mask.constants[1].value = 2;
        const auto mask_type = ecs_bitmask_init(context.world, &mask);
        check(mask_type != 0, "Motion flag metadata registration failed");
        const auto type = context.world.component<Motion>();
        // Match the native layout explicitly, including its owned C++ string.
        ecs_struct_desc_t descriptor{};
        descriptor.entity = type;
        const std::array names{"speed",  "weight", "ticks", "label", "enabled",
                               "marker", "score",  "mode",  "flags"};
        const std::array<ecs_entity_t, 9> ids{context.world.id<float>(),
                                              context.world.id<double>(),
                                              context.world.id<std::int32_t>(),
                                              context.world.id<std::string>(),
                                              context.world.id<bool>(),
                                              context.world.id<std::int64_t>(),
                                              context.world.id<std::uint64_t>(),
                                              context.world.id<MotionMode>(),
                                              mask_type};
        const std::array offsets{
            offsetof(Motion, speed), offsetof(Motion, weight),  offsetof(Motion, ticks),
            offsetof(Motion, label), offsetof(Motion, enabled), offsetof(Motion, marker),
            offsetof(Motion, score), offsetof(Motion, mode),    offsetof(Motion, flags)};
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
        detail::opt_in_authoring(context.world, type, "project.motion", "project.motion", 1,
                                 {{"speed", 20},
                                  {"weight", .5},
                                  {"ticks", 0},
                                  {"label", "retained"},
                                  {"enabled", true},
                                  {"marker", 0},
                                  {"score", 0},
                                  {"mode", 1},
                                  {"flags", 0}},
                                 "Gameplay");
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
        candidate.apply(entity);
    near(entity.get<PaddedOwnedState>().speed, 240);
    check(entity.get<PaddedOwnedState>().counter == 73 &&
              entity.get<PaddedOwnedState>().label == state.label,
          "Animated field update reset unreflected or owned native state");
}
struct NestedOwnedState {
    PaddedOwnedState motion;
    double gain = .5;
};
void nested_updates() {
    EngineModule module;
    module.id = "project.nested";
    module.dependencies = {"forge.transforms"};
    module.schemas = [](ModuleContext& context) {
        context.world.component<PaddedOwnedState>()
            .member<std::string>("label")
            .member<float>("speed")
            .member<bool>("enabled");
        const auto type = context.world.component<NestedOwnedState>()
                              .member<PaddedOwnedState>("motion")
                              .member<double>("gain");
        detail::opt_in_authoring(
            context.world, type, "project.nested", "project.nested", 1,
            {{"motion", {{"label", "default-owned"}, {"speed", 90}, {"enabled", true}}},
             {"gain", .5}},
            "Gameplay");
    };
    EngineContext engine(WorldRole::Runtime, false, {module});
    Scene scene(engine.world());
    const auto& codec = engine.world().authored_codecs().front();
    const auto actor = EntityId::generate();
    scene.reset({{"version", 3},
                 {"asset_id", AssetId::generate()},
                 {"entities", Json::array({{{"id", actor},
                                            {"name", "Nested native state"},
                                            {"components", {{codec.key(), codec.defaults()}}}}})}});
    auto entity = scene.entity(actor.str());
    auto state = entity.get<NestedOwnedState>();
    state.motion.counter = 73;
    state.motion.label = std::string(96, 'n');
    state.gain = .75;
    entity.set(state);
    auto value = codec.read(entity, true);
    value["motion"]["speed"] = 240;
    auto candidate = codec.prepare_update(entity, value);
    check(candidate.has_value(), "Nested native update was not admitted");
    check(entity.get<NestedOwnedState>().motion.speed == 90,
          "Detached nested candidate mutated its source");
    codec.apply(entity, candidate);
    const auto& result = entity.get<NestedOwnedState>();
    check(result.motion.speed == 240 && result.motion.enabled && result.motion.counter == 73 &&
              result.motion.label == state.motion.label && result.gain == .75,
          "Nested update reset owned, hidden or unrelated native state");
}
struct Runtime {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    Runtime(const std::filesystem::path& root, const Json& source,
            std::shared_ptr<const AssetCatalog> catalog, EngineModule module = motion_module())
        : engine(WorldRole::Runtime, false, {animation_module(root), std::move(module)}),
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
#include "animation_component_collection_tests.hpp"
struct NestedAnimation {
    Motion settings;
    Motion other;
    float speed = 3;
};
EngineModule nested_animation_module() {
    auto module = motion_module();
    const auto base = module.schemas;
    module.schemas = [base](ModuleContext& context) {
        base(context);
        const auto type = context.world.component<NestedAnimation>()
                              .member<Motion>("settings")
                              .member<Motion>("other")
                              .member<float>("speed");
        const auto defaults = detail::export_authored_types(context.world).at(0).at("defaults");
        detail::opt_in_authoring(context.world, type, "project.nested_animation", "project.motion",
                                 1, {{"settings", defaults}, {"other", defaults}, {"speed", 3}},
                                 "Gameplay");
    };
    return module;
}
void nested_playback(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    EngineContext authored(WorldRole::Authoring, false, {nested_animation_module()});
    Scene scene(authored.world());
    const auto& codecs = authored.world().authored_codecs();
    const auto& codec = *std::find_if(codecs.begin(), codecs.end(), [](const auto& c) {
        return c.key() == "project.nested_animation";
    });
    using Kind = AnimationComponentValueType;
    const auto binding = [&](const char* name, Kind kind) {
        return AnimationComponentBinding{codec.key(), "settings", codec.stamp(), kind, {name}};
    };
    const auto speed = binding("speed", Kind::Scalar), enabled = binding("enabled", Kind::Boolean),
               marker = binding("marker", Kind::SignedInteger),
               score = binding("score", Kind::UnsignedInteger),
               mode = binding("mode", Kind::SignedInteger),
               flags = binding("flags", Kind::UnsignedInteger);
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Nested.animation.json");
    auto source = document->source();
    source.add_component_field(speed, 20);
    source.add_component_field(enabled, true);
    source.add_component_field(marker, INT64_MIN);
    source.add_component_field(score, UINT64_MAX);
    source.add_component_field(mode, 1);
    source.add_component_field(flags, 1u);
    source.add_component_field({codec.key(), "speed", codec.stamp()}, 3);
    source.add_component_field({codec.key(), "other", codec.stamp(), Kind::Scalar, {"speed"}}, 11);
    source.document["channels"] = Json::array();
    const auto first = source.clip().keys.front().id;
    const auto last = source.set_key(2 * animation_timebase, {});
    source.set_component_value(last, codec.key(), "settings", 100, speed.member_path);
    source.set_component_value(last, codec.key(), "settings", false, enabled.member_path);
    source.set_component_value(last, codec.key(), "settings", INT64_MAX, marker.member_path);
    source.set_component_value(last, codec.key(), "settings", (std::uint64_t{1} << 53) + 1,
                               score.member_path);
    source.set_component_value(last, codec.key(), "settings", -1, mode.member_path);
    source.set_component_value(last, codec.key(), "settings", 2u, flags.member_path);
    source.set_component_curve(first, codec.key(), "settings", {true, 0, 120}, speed.member_path);
    check(source.document.at("version") == 8, "Nested source did not explicitly promote");
    const auto decoded = decode_transform_animation(source.cook(), source.asset());
    check(decoded.component_bindings == source.clip().component_bindings,
          "Nested binding identity changed through cooking");
    auto duplicate = source;
    const std::array selected{first};
    duplicate.duplicate_keys(selected, animation_timebase);
    duplicate.retime_duration(4 * animation_timebase);
    check(duplicate.clip().keys.at(1).component_values.at(2).value == INT64_MIN,
          "Nested duplication/retiming lost exact held values");
    const auto snapshot = source.document;
    for (const Json& bad : {Json::array({""}), Json::array({0}), Json("speed"),
                            Json(std::vector<std::string>(17, "speed"))}) {
        auto invalid = source;
        invalid.document["component_bindings"][0]["member_path"] = bad;
        rejects([&] { invalid.validate(); });
    }
    auto collision = TransformAnimationSource::create(AssetId::generate());
    collision.add_component_field({codec.key(), "speed", codec.stamp()}, 3);
    collision.document["keys"][0]["component_values"][0]["member_path"] = "opaque";
    const auto retained = collision.document;
    rejects([&] { collision.add_component_field(speed, 20); });
    check(collision.document == retained, "Nested promotion changed opaque legacy data");
    collision.document["keys"][0]["component_values"][0].erase("member_path");
    collision.document["component_bindings"][0]["member_path"] = "opaque";
    rejects([&] { collision.add_component_field(speed, 20); });
    document->edit(document->revision(), "Nested component keys",
                   [&](auto& json) { json = source.document; });
    document->undo();
    check(document->source().clip().component_bindings.empty(), "Nested Undo failed");
    document->redo();
    check(document->source().document == source.document, "Nested Redo changed metadata");
    document->save();
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    importer.submit(
        importer.prepare(document->locator(), {}, source.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(importer.wait_idle(10s), "Nested publication stalled");
    const auto results = importer.poll();
    check(results.size() == 1 && results[0].published, "Nested publication failed");
    const auto catalog = std::make_shared<const AssetCatalog>(results[0].publication->catalog);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Nested fields"}})
                           .at("selected")
                           .get<std::string>();
    auto authored_entity = scene.entity(actor);
    codec.apply(authored_entity, codec.prepare(scene.world(), codec.defaults()));
    TransformAnimator animator;
    animator.clip = {source.asset()};
    animator.loop = false;
    authored_entity.set(animator);
    auto saved = scene.snapshot();
    saved["entities"][0]["components"][codec.key()]["settings"]["future"] = "retained extension";
    scene.restore_snapshot(saved);
    auto preview = scene.effective_document();
    const auto schema = scene.schema();
    check(apply_transform_animation_preview(preview, {scene.asset_id(), EntityId::parse(actor)},
                                            decoded, animation_timebase, &schema),
          "Nested preview failed");
    const auto& sampled = preview["entities"][0]["components"][codec.key()];
    near(sampled["settings"]["speed"], 90);
    check(sampled["settings"]["enabled"] == true && sampled["settings"]["score"] == UINT64_MAX &&
              sampled["settings"]["label"] == "retained" && sampled["other"]["speed"] == 11 &&
              sampled["speed"] == 3,
          "Nested preview lost sibling or exact held state");
    for (const auto& [label, mutate] :
         std::vector<std::pair<std::string, std::function<void(Json&)>>>{
             {"read-only ancestor", [](auto& s) { s["fields"][0]["read_only"] = true; }},
             {"read-only leaf", [](auto& s) { s["fields"][0]["fields"][0]["read_only"] = true; }},
             {"missing leaf", [](auto& s) { s["fields"][0]["fields"][0]["id"] = "renamed"; }}}) {
        auto altered = codec.editor_schema();
        mutate(altered);
        rejects([&] { sample_animation_component(altered, codec.defaults(), decoded, 0, true); });
    }
    auto invalid = source;
    invalid.set_component_value(last, codec.key(), "settings", 99, mode.member_path);
    rejects([&] {
        sample_animation_component(codec.editor_schema(), codec.defaults(), invalid.clip(), 0,
                                   true);
    });
    check(source.document == snapshot, "Invalid nested operations changed source");
    Runtime runtime(root, saved, catalog, nested_animation_module());
    runtime.ready();
    auto entity = runtime.scene.entity(actor);
    auto native = entity.get<NestedAnimation>();
    native.settings.ticks = 73;
    native.settings.label = std::string(96, 'q');
    entity.set(native);
    runtime.simulation.tick(1.f);
    near(entity.get<NestedAnimation>().settings.speed, 90);
    check(entity.get<NestedAnimation>().settings.ticks == 73 &&
              entity.get<NestedAnimation>().settings.label == native.settings.label,
          "Nested runtime animation changed unrelated native fields");
    const auto patch = runtime.animation->presentation_properties(entity.id(), .5);
    check(patch.at(codec.key()).at("settings").at("label") == native.settings.label &&
              patch.at(codec.key()).at("settings").at("ticks") == 73,
          "Nested presentation produced an incomplete root struct");
    const auto presented = runtime.simulation.presentation(.5);
    check(
        presented.at("entities")[0].at("components").at(codec.key()).at("settings").at("future") ==
            "retained extension",
        "Nested runtime projection lost an opaque sibling");
    const auto recovery = runtime.animation->checkpoint();
    Runtime recovered(root, runtime.scene.snapshot(), catalog, nested_animation_module());
    recovered.ready();
    recovered.animation->restore(recovery);
    runtime.simulation.tick(1.f);
    recovered.simulation.tick(1.f);
    check(entity.get<NestedAnimation>().settings.mode == MotionMode::Reverse &&
              !entity.get<NestedAnimation>().settings.enabled &&
              entity.get<NestedAnimation>().settings.marker == INT64_MAX &&
              recovered.scene.entity(actor).get<NestedAnimation>().settings.score ==
                  ((std::uint64_t{1} << 53) + 1),
          "Nested endpoint/recovery lost exact discrete values");
    check(scene.snapshot() == saved, "Nested preview/runtime changed authored values");
    const EntityRef reference{runtime.scene.asset_id(), EntityId::parse(actor)};
    AnimationPlaybackAccess::control(*runtime.animation, reference, {{"operation", "pause"}});
    runtime.simulation.tick(.25f);
    check(entity.get<NestedAnimation>().settings.marker == INT64_MAX,
          "Paused nested values advanced");
    AnimationPlaybackAccess::control(*runtime.animation, reference,
                                     {{"operation", "seek"}, {"position_seconds", 1}});
    near(entity.get<NestedAnimation>().settings.speed, 90);
    check(entity.get<NestedAnimation>().settings.marker == INT64_MIN,
          "Seeking nested fields rounded or retained the wrong held key");
    auto overshoot = source;
    overshoot.set_component_curve(first, codec.key(), "settings", {true, 0, 1200},
                                  speed.member_path);
    const auto unchanged_native = codec.read(entity, true);
    rejects([&] {
        prepare_animation_components(runtime.engine.world(), entity, overshoot.clip(), 0, true);
    });
    check(codec.read(entity, true) == unchanged_native,
          "Rejected whole nested curve admission mutated native fields");
    auto deep_schema = codec.editor_schema();
    auto deep_current = codec.defaults();
    auto deep_clip = decoded;
    auto& root_field = deep_schema["fields"][0];
    const auto original_fields = root_field["fields"];
    root_field["fields"] =
        Json::array({{{"id", "inner"}, {"type", "struct"}, {"fields", original_fields}}});
    deep_current["settings"] = {{"inner", deep_current.at("settings")}};
    for (auto& item : deep_clip.component_bindings)
        if (item.field == "settings")
            item.member_path.insert(item.member_path.begin(), "inner");
    const auto deep_sample =
        sample_animation_component(deep_schema, deep_current, deep_clip, animation_timebase, true);
    near(deep_sample.at("settings").at("inner").at("speed"), 90);
    check(deep_sample.at("settings").at("inner").at("score") == UINT64_MAX,
          "Multiple struct ancestors lost an exact held leaf");
    const AnimationComponentBinding escaped{
        codec.key(), "root/~", codec.stamp(), Kind::Scalar, {"leaf/~"}};
    Json escaped_value{{"root/~", {{"leaf/~", 17}}}};
    check(escaped_value.at(animation_component_pointer(escaped)) == 17,
          "Member paths were split or used as unescaped JSON pointer text");
    auto removed = source;
    removed.erase_component_field(codec.key(), "settings", enabled.member_path);
    check(removed.clip().component_bindings.size() == 7 &&
              removed.clip().component_bindings.front() == speed,
          "Removing a nested field removed its sibling");
}

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
void discrete_playback(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    EngineContext authored(WorldRole::Authoring, false, {motion_module()});
    Scene scene(authored.world());
    const auto& codec = authored.world().authored_codecs().front();
    const auto admission_before = codec.stamp();
    const auto structure_before = codec.declaration.at("structure");
    const auto current_schema = codec.editor_schema();
    for (const auto& field : current_schema.at("fields"))
        check(field.at("animatable") ==
                  animation_component_field_type(field.at("type").get_ref<const std::string&>(),
                                                 field.value("read_only", false))
                      .has_value(),
              "Current discovery disagrees with component animation support");
    bool retained_legacy_annotation = false;
    for (const auto& field : structure_before.at("fields"))
        if (field.at("id") == "enabled")
            retained_legacy_annotation = field.at("animatable") == false;
    check(codec.stamp() == admission_before &&
              codec.declaration.at("structure") == structure_before && retained_legacy_annotation,
          "Derived capability metadata changed the stamped native structure");
    const auto binding = [&](const char* name, AnimationComponentValueType kind) {
        return AnimationComponentBinding{codec.key(), name, codec.stamp(), kind};
    };
    using Kind = AnimationComponentValueType;
    const auto speed = binding("speed", Kind::Scalar), flag = binding("enabled", Kind::Boolean),
               marker = binding("marker", Kind::SignedInteger),
               score = binding("score", Kind::UnsignedInteger),
               mode = binding("mode", Kind::SignedInteger),
               bits = binding("flags", Kind::UnsignedInteger);
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr auto precise = (std::uint64_t{1} << 53) + 1;
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Discrete.animation.json");
    auto source = document->source();
    source.add_component_field(speed, 20);
    auto collision = source;
    collision.document["component_bindings"][0]["value_type"] = "opaque legacy extension";
    const auto collision_before = collision.document;
    rejects([&] { collision.add_component_field(flag, true); });
    check(collision.document == collision_before, "Typed promotion changed an opaque extension");
    source.add_component_field(flag, true);
    source.add_component_field(marker, low);
    source.add_component_field(score, maximum);
    source.add_component_field(mode, 1);
    source.add_component_field(bits, 3);
    source.document["channels"] = Json::array();
    source.document["duration_us"] = 2 * animation_timebase;
    const auto first = source.clip().keys.front().id;
    const auto last = source.set_key(2 * animation_timebase, {});
    source.set_component_value(last, speed.component, speed.field, 100);
    source.set_component_value(last, flag.component, flag.field, false);
    source.set_component_value(last, marker.component, marker.field, high);
    source.set_component_value(last, score.component, score.field, precise);
    source.set_component_value(last, mode.component, mode.field, -1);
    source.set_component_value(last, bits.component, bits.field, 2);
    check(source.document.at("version") == 7, "Discrete fields need an explicit typed source");
    const auto source_before = source.document;
    rejects([&] { source.set_component_value(last, flag.component, flag.field, 1); });
    rejects([&] { source.set_component_value(last, marker.component, marker.field, maximum); });
    rejects([&] { source.set_component_value(last, marker.component, marker.field, 1.25); });
    rejects([&] { source.set_component_value(last, score.component, score.field, -1); });
    rejects([&] { source.set_component_curve(first, flag.component, flag.field, {}); });
    check(source.document == source_before, "Rejected discrete edit changed source");
    std::array<Json, 6> values;
    const auto clip = source.clip();
    clip.sample_component_values(animation_timebase, values);
    near(values[0].get<double>(), 60);
    check(values[1] == true && values[2].get<std::int64_t>() == low &&
              values[3].get<std::uint64_t>() == maximum && values[4] == 1 && values[5] == 3,
          "Discrete values interpolated or lost wide native storage");
    clip.sample_component_values(2 * animation_timebase, values);
    check(values[1] == false && values[2].get<std::int64_t>() == high &&
              values[3].get<std::uint64_t>() == precise && values[4] == -1 && values[5] == 2,
          "Discrete values did not switch at the exact key");
    std::array<double, 6> numeric;
    rejects([&] { clip.sample_components(0, numeric); });
    const auto decoded = decode_transform_animation(source.cook(), source.asset());
    check(decoded.component_bindings == clip.component_bindings &&
              decoded.keys.back().component_values[3].value.get<std::uint64_t>() == precise,
          "Cooked typed values lost their identity or integer precision");
    auto inserted = source;
    const auto middle = inserted.set_key(animation_timebase, {});
    const auto& row = inserted.document.at("keys")[1].at("component_values");
    check(row[2].at("value").get<std::int64_t>() == low &&
              row[3].at("value").get<std::uint64_t>() == maximum,
          "Key insertion did not retain exact held values");
    const std::array selected{middle};
    inserted.duplicate_keys(selected, animation_timebase / 2);
    inserted.retime_duration(4 * animation_timebase);
    check(inserted.clip().keys[2].component_values[3].value.get<std::uint64_t>() == maximum,
          "Key duplication/retiming rounded a held integer");
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    auto publish = [&] {
        document->edit(document->revision(), "Typed component fields",
                       [&](auto& value) { value = source.document; });
        document->save();
        importer.submit(
            importer.prepare(document->locator(), {}, source.asset()),
            [](auto& catalog, const auto& plan, const auto&) {
                prepare_transform_animation_publication(catalog, plan);
            },
            [](const auto&, const auto&) {});
        check(importer.wait_idle(10s), "Discrete clip publication stalled");
        auto result = importer.poll();
        check(result.size() == 1 && result[0].published, "Discrete clip publication failed");
        return std::make_shared<const AssetCatalog>(result[0].publication->catalog);
    };
    const auto catalog = publish();
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Discrete motion"}})
                           .at("selected")
                           .get<std::string>();
    codec.apply(scene.entity(actor), codec.prepare(scene.world(), codec.defaults()));
    TransformAnimator animator;
    animator.clip = {source.asset()};
    animator.loop = false;
    scene.entity(actor).set(animator);
    const auto saved = scene.snapshot();
    const EntityRef reference{scene.asset_id(), EntityId::parse(actor)};
    auto preview = saved;
    const auto schema = scene.schema();
    apply_transform_animation_preview(preview, reference, clip, animation_timebase, &schema);
    const auto& preview_value = preview["entities"][0]["components"][codec.key()];
    check(preview_value.at("marker").get<std::int64_t>() == low &&
              preview_value.at("score").get<std::uint64_t>() == maximum,
          "Copied preview lost wide integer storage");
    Runtime live(root, saved, catalog);
    live.ready();
    auto entity = live.scene.entity(actor);
    live.simulation.tick(1.f);
    auto state = entity.get<Motion>();
    near(state.speed, 60);
    check(state.enabled && state.marker == low && state.score == maximum &&
              state.mode == MotionMode::Forward && state.flags == 3 && state.label == "retained",
          "Native discrete fields changed between keys");
    const auto patch = live.animation->presentation_properties(entity.id(), .5).at(codec.key());
    check(patch.at("score").get<std::uint64_t>() == maximum,
          "Presentation rounded an unsigned64 field");
    Runtime cold(root, live.scene.snapshot(), catalog);
    cold.ready();
    cold.animation->restore(live.animation->checkpoint());
    for (auto* runtime : {&live, &cold}) {
        runtime->simulation.tick(1.f);
        const auto end = runtime->scene.entity(actor).get<Motion>();
        near(end.speed, 100);
        check(!end.enabled && end.marker == high && end.score == precise &&
                  end.mode == MotionMode::Reverse && end.flags == 2,
              "Native typed endpoint/recovery did not preserve exact values");
    }
    AnimationPlaybackAccess::control(*live.animation, reference, {{"operation", "pause"}});
    AnimationPlaybackAccess::control(*live.animation, reference,
                                     {{"operation", "seek"}, {"position_seconds", 0}});
    check(entity.get<Motion>().score == maximum && entity.get<Motion>().marker == low,
          "Paused seek rounded discrete values");
    auto invalid = source;
    invalid.set_component_value(last, mode.component, mode.field, 77);
    const auto before = live.scene.document();
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), entity, invalid.clip(), 0, true);
    });
    check(live.scene.document() == before, "Rejected future enum key changed native state");
    invalid = source;
    invalid.set_component_value(last, bits.component, bits.field, 8);
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), entity, invalid.clip(), 0, true);
    });
    check(live.scene.document() == before, "Rejected flag bits changed native state");
    invalid = source;
    invalid.add_component_field(binding("ticks", Kind::SignedInteger), high);
    rejects([&] {
        (void)prepare_animation_components(live.engine.world(), entity, invalid.clip(), 0, true);
    });
    check(scene.snapshot() == saved, "Runtime/discrete preview changed authored bytes");
    // Isolate discrete-only loop snapping: the prior presentation value differs
    // after an interior held key; no float/transform endpoint can mask this check.
    source.erase_component_field(speed.component, speed.field);
    const auto interior = source.set_key(animation_timebase, {});
    source.set_component_value(interior, marker.component, marker.field, high);
    const auto loop_catalog = publish();
    Runtime loop(root, saved, loop_catalog);
    auto config = loop.scene.entity(actor).get<TransformAnimator>();
    config.loop = true;
    loop.scene.entity(actor).set(config);
    loop.ready();
    loop.simulation.tick(1.9f);
    check(loop.scene.entity(actor).get<Motion>().marker == high, "Interior held key was skipped");
    loop.simulation.tick(.2f);
    check(loop.animation->presentation_properties(loop.scene.entity(actor).id(), 0)
                  .at(codec.key())
                  .at("marker")
                  .get<std::int64_t>() == low,
          "Discrete-only loop did not snap its presentation endpoint");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need component animation scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        nested_updates();
        nested_playback(root / "nested");
        collection_playback(root / "collections");
        playback(root);
        discrete_playback(root / "discrete");
        std::filesystem::remove_all(root);
        std::cout << "Component clip source, native behavior, copied preview, admission and "
                     "recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
