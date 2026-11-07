#include "sequence_clip_span.hpp"
#include <array>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <limits>
#include <vector>

using namespace forge;
using namespace forge::detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    check(std::isfinite(actual) && std::abs(actual - expected) < 1e-9,
          "Arrangement sample differs from its independent reference");
}
template <class F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid arrangement was accepted");
}
double value(const SequenceClipArrangement& arrangement, double seconds) {
    const auto sample = arrangement.sample(seconds * animation_timebase);
    const auto a = 10 * double(sample.primary + 1) + sample.primary_us / animation_timebase;
    const auto b = 10 * double(sample.next + 1) + sample.next_us / animation_timebase;
    return std::lerp(a, b, sample.weight);
}
void multiway_influences() {
    using Clip = SequenceClipArrangement::Clip;
    const double unit = animation_timebase;
    const std::array triple{Clip{0, 4 * unit}, Clip{1, 4 * unit}, Clip{2, 4 * unit}};
    rejects([&] { return SequenceClipArrangement::make(triple); });
    const auto layered = SequenceClipArrangement::make(triple, true);
    const auto middle = layered.influences(3 * unit);
    near(middle.weights[0], 2. / 9);
    near(middle.weights[1], 4. / 9);
    near(middle.weights[2], 1. / 3);
    check(middle.strongest() == 1, "Dominant contribution is not the largest influence");
    const auto summary = layered.summary(3 * unit);
    check(summary.summary_only && summary.primary == 1 && summary.next == 2,
          "Two-slot summary pretended to describe the complete three-clip pose");
    near(summary.primary_us, 2 * unit);
    near(summary.next_us, unit);
    near(summary.weight, 3. / 7);
    check(!layered.summary(2 * unit).summary_only && !layered.summary(4 * unit).summary_only,
          "Ordinary adjacent pair acquired a multiway summary label");
    check(layered.influences(2.5 * unit).strongest() == 1,
          "Later equal-influence clip did not win the held-value tie");
    const auto mean = layered.average_influences(2 * unit, 4 * unit);
    near(mean.weights[0], 7. / 27);
    near(mean.weights[1], 11. / 27);
    near(mean.weights[2], 1. / 3);
    const std::array gap{Clip{0, unit}, Clip{3, unit}};
    const auto cut = SequenceClipArrangement::make(gap, true);
    near(cut.clip_time_us(0, -unit), 0);
    near(cut.clip_time_us(0, 20 * unit), unit);
    near(cut.clip_time_us(1, 3.5 * unit), .5 * unit);
    rejects([&] { return cut.clip_time_us(2, 0); });
    rejects([&] { return cut.clip_time_us(0, std::numeric_limits<double>::infinity()); });
    near(cut.influences(3 * unit).weights[1], 1);
    near(cut.average_influences(2 * unit, 3 * unit).weights[0], 1);
    near(cut.average_influences(3 * unit, 4 * unit).weights[1], 1);
    near(cut.average_influences(2 * unit, 4 * unit).weights[0], .5);
    near(cut.average_influences(2 * unit, 4 * unit).weights[1], .5);
    std::array<Clip, sequence_clip_limit> clips{};
    for (std::size_t i = 0; i < clips.size(); ++i)
        clips[i] = {.25 * i, 4 * unit};
    const auto all = SequenceClipArrangement::make(clips, true);
    const auto reference = [&](std::size_t index, double seconds) {
        // Closed-form suffix product, independent of the forward overlay sampler.
        const auto alpha = [&](std::size_t i) {
            return std::clamp((seconds - .25 * i) / 3.75, 0., 1.);
        };
        double weight = index ? alpha(index) : 1;
        for (std::size_t later = index + 1; later < clips.size(); ++later)
            weight *= 1 - alpha(later);
        return weight;
    };
    for (int step = -16; step <= 112; ++step) {
        const double seconds = step / 16.;
        const auto sampled = all.influences(seconds * unit);
        double sum = 0;
        check(sampled.count == 8, "Layered influences lost a clip slot");
        for (std::size_t i = 0; i < sampled.count; ++i) {
            near(sampled.weights[i], reference(i, seconds));
            check(sampled.weights[i] >= 0 && sampled.weights[i] <= 1,
                  "A layered influence left its normalized range");
            sum += sampled.weights[i];
        }
        near(sum, 1);
        const auto instant = all.average_influences(seconds * unit, seconds * unit);
        check(instant.weights == sampled.weights, "Zero-width influence mean changed its point");
    }
    for (const auto range : {std::array<double, 2>{1.75, 4}, {-1, 7}}) {
        const auto average = all.average_influences(range[0] * unit, range[1] * unit);
        constexpr unsigned steps = 4096;
        double sum = 0;
        for (std::size_t clip = 0; clip < clips.size(); ++clip) {
            // Independent composite Simpson reference for all seven fade factors.
            long double integral = 0;
            for (unsigned i = 0; i <= steps; ++i) {
                const double time = std::lerp(range[0], range[1], double(i) / steps);
                const unsigned coefficient = i == 0 || i == steps ? 1 : i % 2 ? 4 : 2;
                integral += coefficient * reference(clip, time);
            }
            near(average.weights[clip], double(integral / (3 * steps)));
            sum += average.weights[clip];
        }
        near(sum, 1);
    }
    rejects([&] { return all.average_influences(2, 1); });
    rejects([&] { return all.influences(std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { return all.average_influences(0, std::numeric_limits<double>::infinity()); });
    rejects([&] {
        return all.average_influences(-std::numeric_limits<double>::max(),
                                      std::numeric_limits<double>::max());
    });
}
void source_collections(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    PrefabLibrary library(root);
    for (const auto* component : {"forge.transform_animator", "forge.animator"}) {
        EngineContext engine;
        Scene scene(engine.world());
        const auto create = [&](const char* recipe) {
            return authoring_command(scene, "entity.create", {{"recipe", recipe}})
                .at("selected")
                .get<std::string>();
        };
        const auto controller = create("animation.sequence");
        const EntityRef binding{scene.asset_id(), EntityId::parse(controller)};
        const auto actor = create("primitive.0");
        authoring_command(scene, "component.add", {{"entity", actor}, {"component", component}});
        const auto first = AssetId::generate(), next = AssetId::generate(),
                   extra = AssetId::generate();
        auto sequence = scene.entity(controller).get<AnimationSequence>();
        sequence.duration = 10;
        scene.entity(controller).set(sequence);
        check(std::string_view(component) == "forge.animator"
                  ? !scene.entity(actor).get<Animator>().sequence_multiway
                  : !scene.entity(actor).get<TransformAnimator>().sequence_multiway,
              "New players must retain the legacy overlap profile by default");
        const auto before = scene.document();
        auto entries = Json::array();
        for (std::size_t i = 0; i < sequence_clip_limit - 2; ++i)
            entries.push_back(
                {{"clip", extra},
                 {"start_seconds", 2. + i},
                 {"future", {{"note", "retained source extension"}, {"ordinal", i}}}});
        Json fields{{"clip", first},
                    {"sequence", binding},
                    {"sequence_next_clip", next},
                    {"sequence_next_start_seconds", 1.},
                    {"sequence_extra_clips", entries},
                    {"sequence_multiway", true}};
        if (std::string_view(component) == "forge.animator")
            fields["skeleton"] = AssetId::generate();
        const auto set = [&](const Json& values) {
            authoring_command(scene, "property.set_many",
                              {{"entity", actor}, {"component", component}, {"values", values}});
        };
        set(fields);
        const auto saved = scene.document();
        check(std::string_view(component) == "forge.animator"
                  ? scene.entity(actor).get<Animator>().sequence_multiway
                  : scene.entity(actor).get<TransformAnimator>().sequence_multiway,
              "Multiway opt-in did not reach native storage");
        for (const auto& row : saved.at("entities"))
            if (row.at("id") == actor)
                check(row.at("components").at(component).at("sequence_extra_clips") == entries,
                      "Collection publication lost its entry or opaque extension");
        if (std::string_view(component) == "forge.animator")
            check(scene.entity(actor).get<Animator>().sequence_extra_clips.at(0).clip.id == extra,
                  "Skeletal collection did not reach typed native storage");
        else
            check(scene.entity(actor).get<TransformAnimator>().sequence_extra_clips.at(0).clip.id ==
                      extra,
                  "Object collection did not reach typed native storage");
        check(scene.undo() && scene.document() == before, "Collection edit was not one Undo step");
        check(scene.redo() && scene.document() == saved, "Collection Redo changed intent");
        const auto schema = scene.schema();
        bool described = false;
        for (const auto& type : schema.at("components"))
            if (type.at("id") == component)
                for (const auto& field : type.at("fields"))
                    if (field.at("id") == "sequence_extra_clips") {
                        check(field.at("type") == "vector" && field.at("maximum_count") == 6,
                              "Collection discovery lost its shape/count limit");
                        for (const auto& member : field.at("element").at("fields"))
                            if (member.at("id") == "clip") {
                                check(member.at("type") == "asset_ref" &&
                                          member.at("asset_type") ==
                                              (std::string_view(component) == "forge.animator"
                                                   ? "animation_clip"
                                                   : "transform_animation"),
                                      "Collection discovery lost its typed clip reference");
                                described = true;
                            }
                    }
        check(described, "Collection omitted its discoverable clip field");
        for (const auto& bad :
             std::vector<Json>{{{"sequence_multiway", 1}},
                               {{"sequence_multiway", "true"}},
                               {{"sequence_multiway", nullptr}},
                               {{"sequence", nullptr}},
                               {{"sequence_next_clip", nullptr}},
                               {{"sequence_extra_clips", Json::object()}},
                               {{"sequence_extra_clips",
                                 Json::array({{{"clip", nullptr}, {"start_seconds", 2.}}})}},
                               {{"sequence_extra_clips",
                                 Json::array({{{"clip", extra}, {"start_seconds", .5}}})}},
                               {{"sequence_extra_clips", std::vector<Json>(7, entries.at(0))}}}) {
            const auto revision = scene.revision();
            rejects([&] { set(bad); });
            check(scene.revision() == revision && scene.document() == saved,
                  "Rejected collection partially changed values, extensions or history");
        }
        EngineContext restored_engine;
        Scene restored(restored_engine.world());
        restored.restore_snapshot(saved);
        check(restored.document() == saved,
              "Collection source roundtrip changed intent/extensions");
        scene.reparent_entity(actor, controller, ReparentMode::KeepLocal);
        const auto definition = create_prefab_source(scene, controller);
        const auto prefab =
            library.create(scene, definition, std::string(component) + ".prefab.json");
        const auto instance = instantiate_prefab(scene, prefab);
        bool found = false;
        const auto effective = scene.effective_document();
        for (const auto& row : effective.at("entities")) {
            const auto entity = scene.entity(row.at("id").get<std::string>());
            if (entity.target(flecs::ChildOf) != scene.entity(instance) ||
                !row.at("components").contains(component))
                continue;
            const auto& config = row.at("components").at(component);
            check(config.at("sequence").get<EntityRef>() == scene.reference(instance) &&
                      config.at("sequence_extra_clips") == entries &&
                      config.at("sequence_multiway") == true,
                  "Prefab collection lost controller remapping, asset IDs or opaque fields");
            found = true;
        }
        check(found, "Prefab did not materialize the typed clip collection");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need clip arrangement scratch root");
        using Clip = SequenceClipArrangement::Clip;
        constexpr double second = animation_timebase;
        const std::array clips{Clip{5, 2 * second}, Clip{6, 2 * second}, Clip{8, 2 * second}};
        const auto arrangement = SequenceClipArrangement::make(clips);
        check(arrangement.size() == 3, "Arrangement lost a clip");
        near(arrangement.start_us, 5 * second);
        near(arrangement.duration_us, 5 * second);
        near(arrangement.relative_time(4 * second), 0);
        near(arrangement.relative_time(6 * second), second);
        near(arrangement.relative_time(20 * second), 5 * second);
        for (const auto row : std::array<std::array<double, 2>, 10>{{{-1, 10},
                                                                     {0, 10},
                                                                     {.5, 10.5},
                                                                     {1, 11},
                                                                     {1.5, 16},
                                                                     {2, 21},
                                                                     {2.5, 21.5},
                                                                     {3, 30},
                                                                     {4, 31},
                                                                     {6, 32}}})
            near(value(arrangement, row[0]), row[1]);
        near(arrangement.clip_offset_us(0), 0);
        near(arrangement.clip_offset_us(1), second);
        near(arrangement.clip_offset_us(2), 3 * second);
        for (std::size_t i = 0; i < 3; ++i)
            near(arrangement.clip_duration_us(i), 2 * second);
        check(!arrangement.cut_crossed(0, 2.5 * second) &&
                  arrangement.cut_crossed(2.5 * second, 3 * second),
              "Arrangement lost an adjacent cut or invented an overlap cut");
        const auto outgoing_end = arrangement.sample_window(0, 2 * second);
        const auto next_begin = arrangement.sample(2 * second);
        check(outgoing_end.primary == 0 && outgoing_end.next == 1 && outgoing_end.weight == 1 &&
                  next_begin.primary == 1 && next_begin.next == 2 && next_begin.weight == 0,
              "Interval endpoint lost its outgoing pair at the window boundary");
        near(outgoing_end.next_us, next_begin.primary_us);

        std::array<Clip, 8> eight;
        for (std::size_t i = 0; i < eight.size(); ++i)
            eight[i] = {double(i), 2 * second};
        const auto full = SequenceClipArrangement::make(eight);
        near(full.duration_us, 9 * second);
        // The selected clips' linear values have this independent closed form
        // across the seven one-second overlaps, then one final unblended second.
        for (int i = -10; i <= 370; ++i) {
            const double t = i * .027;
            const double expected = t < 0   ? 10
                                    : t < 1 ? 10 + t
                                    : t < 8 ? 10 * std::floor(t) + 1 + 10 * (t - std::floor(t))
                                    : t < 9 ? 73 + t
                                            : 82;
            near(value(full, t), expected);
        }
        const std::array gaps{Clip{0, second}, Clip{3, second}, Clip{6, second}};
        const auto held = SequenceClipArrangement::make(gaps);
        for (const auto row : std::array<std::array<double, 2>, 6>{
                 {{1, 11}, {2, 11}, {3, 20}, {5, 21}, {6, 30}, {8, 31}}})
            near(value(held, row[0]), row[1]);
        check(!held.cut_crossed(second, 2 * second) && held.cut_crossed(2 * second, 3 * second) &&
                  held.cut_crossed(3 * second, 6 * second),
              "A gap must hold until the actual incoming start");

        for (const double incoming : {0., .5, 1., 2., 3.}) {
            const std::array pair{Clip{0, 2 * second}, Clip{incoming, 2 * second}};
            const auto collection = SequenceClipArrangement::make(pair);
            const auto legacy = SequenceClipSpan::make(0, incoming, 2 * second, 2 * second);
            for (int i = -10; i <= 200; ++i) {
                const double time = i * 27001.;
                const auto sample = collection.sample(time);
                check(sample.primary == 0 && sample.next == 1,
                      "Pair collection changed the legacy observed identities");
                near(sample.weight, legacy.next_weight(time));
                near(sample.primary_us, std::clamp(time, 0., 2 * second));
                near(sample.next_us, std::clamp(time - incoming * second, 0., 2 * second));
            }
        }
        const std::array single{Clip{2, second}};
        const auto one = SequenceClipArrangement::make(single);
        near(value(one, .5), 10.5);
        near(value(one, 3), 11);
        check(!one.cut_crossed(0, 10 * second), "Single clip invented a cut");
        rejects([] { (void)SequenceClipArrangement::make({}); });
        rejects([&] { (void)SequenceClipArrangement::make(std::vector<Clip>(9, {0, second})); });
        for (const auto& bad :
             std::vector<std::vector<Clip>>{{{-1, second}},
                                            {{0, 0}},
                                            {{3601, second}},
                                            {{0, std::numeric_limits<double>::infinity()}},
                                            {{std::numeric_limits<double>::quiet_NaN(), second}},
                                            {{1, second}, {0, second}},
                                            {{0, 2 * second}, {.5, .2 * second}},
                                            {{0, 3 * second}, {1, 3 * second}, {2, 3 * second}}})
            rejects([&] { (void)SequenceClipArrangement::make(bad); });
        rejects([&] { (void)arrangement.clip_offset_us(3); });
        rejects([&] { (void)arrangement.clip_duration_us(3); });
        rejects([&] { (void)arrangement.sample_window(2, 0); });
        rejects([&] { (void)arrangement.sample(std::numeric_limits<double>::infinity()); });
        rejects([&] { (void)arrangement.relative_time(std::numeric_limits<double>::quiet_NaN()); });
        multiway_influences();
        source_collections(std::filesystem::absolute(argv[1]) / AssetId::generate().str());
        std::cout << "Eight-clip timing, typed source/history/schema and portable Prefab admission "
                     "pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
