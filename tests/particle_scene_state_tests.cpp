#include "particle_scene_state.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Action> void reject(Action&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Scene particle state or capture was admitted");
}
ParticleEffectData effect() {
    ParticleEffectData value;
    value.version = 38;
    value.mesh = engine_primitive(0);
    value.capacity = 3;
    value.spawn_rate = 4;
    value.lifetime = .75;
    value.direction = {1, 0, 0};
    value.cone_angle = 0;
    value.speed_min = value.speed_max = 2;
    value.acceleration = {};
    value.scene_collision = ParticleSceneCollision{};
    validate_particle_effect(value);
    return value;
}
const ParticleSceneQuery clear = [](const auto&, const auto&, double) -> std::optional<PhysicsHit> {
    return {};
};
void timed_query_adapter() {
    const auto data = effect();
    std::vector<ParticleSceneSweep> queries;
    const ParticleSceneMotionQuery record = [&](const ParticleSceneSweep& query) {
        queries.push_back(query);
        return ParticleSceneQueryResult{{}, 2};
    };
    ParticleSceneWork work;
    const auto first = advance_particle_scene_history({}, data, 17, 0, 1, {}, clear, work, record);
    check(queries.size() == 4 && work.queries == 8,
          "Timed Scene adapter lost births or native operation counts");
    for (std::size_t i = 0; i < queries.size(); ++i)
        check(queries[i].start_seconds == .25 * (i + 1) && queries[i].end_seconds == 1 &&
                  queries[i].query_budget == particle_scene_birth_query_limit &&
                  first.traces[i].step.queries == 2,
              "Birth-local query times were not mapped into the containing fixed interval");
    ParticleSceneWork legacy_work;
    const auto legacy = advance_particle_scene_history({}, data, 17, 0, 1, {}, clear, legacy_work);
    check(sample_particle_scene_history(first, data, 0, 1, 1) ==
              sample_particle_scene_history(legacy, data, 0, 1, 1),
          "A clear timed query changed the existing emission/motion owner");
    queries.clear();
    work = {};
    const auto second =
        advance_particle_scene_history(first, data, 17, 1, 1.25, {}, clear, work, record);
    check(queries.size() == 4 && work.queries == 8 && second.initialized,
          "Timed query adapter lost the next interval's live/new membership");
    for (std::size_t i = 0; i < queries.size(); ++i)
        check(queries[i].start_seconds == (i == 3 ? .25 : 0) && queries[i].end_seconds == .25,
              "Timed query adapter replayed absolute age or omitted a zero-duration birth");
    auto burst = data;
    burst.emission = ParticleEmission::Burst;
    burst.capacity = burst.burst_count = 1;
    const ParticleSceneMotionQuery last = [](const ParticleSceneSweep& query) {
        check(query.query_budget == 1, "World remainder was not passed to the native query");
        return ParticleSceneQueryResult{{}, 1};
    };
    work = {particle_scene_query_limit - 1, 0};
    const auto admitted =
        advance_particle_scene_history({}, burst, 17, 0, .1, {}, clear, work, last);
    check(work.queries == particle_scene_query_limit && admitted.live.size() == 1,
          "Last available native operation was dropped or incorrectly charged");
    for (unsigned invalid : {0u, 2u}) {
        work = {particle_scene_query_limit - 1, 0};
        reject([&] {
            (void)advance_particle_scene_history({}, burst, 17, 0, .1, {}, clear, work,
                                                 [invalid](const ParticleSceneSweep&) {
                                                     return ParticleSceneQueryResult{{}, invalid};
                                                 });
        });
        check(work.queries == particle_scene_query_limit - 1,
              "Invalid native operation counts partially changed the World ledger");
    }
}
void intervals_and_frames() {
    const auto data = effect();
    ParticleSceneWork work;
    const auto first = advance_particle_scene_history({}, data, 17, 0, 1, {}, clear, work);
    check(first.traces.size() == 4 && first.live.size() == 3 && work.births == 4 &&
              work.queries == 4,
          "Births that expire inside one interval lost their bounded contact trace");
    check(sample_particle_scene_history(first, data, 0, 1, .249).empty(),
          "A continuous Scene birth appeared before its emission phase");
    const auto born = sample_particle_scene_history(first, data, 0, 1, .25);
    const auto before_expiry = sample_particle_scene_history(first, data, 0, 1, .999);
    const auto expired = sample_particle_scene_history(first, data, 0, 1, 1);
    check(born.size() == 1 && born[0].sample.birth == 0 && born[0].sample.age == 0 &&
              before_expiry.size() == 3 && before_expiry[0].sample.birth == 0 &&
              expired.size() == 3 && expired[0].sample.birth == 1 &&
              expired[0].sample.position[0] == 1,
          "Scene history changed exact birth/lifetime phase membership or motion");
    AffineTransform moved;
    moved.m = {-2, .25, 0, 10, 0, 3, 0, 0, 0, 0, .5, 0};
    work = {};
    const auto second =
        advance_particle_scene_history(first, data, 17, 1, 1.25, moved, clear, work);
    const auto values = sample_particle_scene_history(second, data, 1, 1.25, 1.25);
    check(values.size() == 3 && values[0].sample.birth == 2 && values[0].sample.position[0] == 1 &&
              values[0].basis[0] == 1 && values.back().sample.birth == 4 &&
              values.back().sample.position[0] == 10 && values.back().sample.velocity[0] == -4 &&
              values.back().basis[0] == -2 && values.back().basis[1] == .25 &&
              values.back().basis[4] == 3,
          "Emitter movement/reflection/shear dragged old births or changed the new birth basis");
    work = {};
    const auto frozen =
        advance_particle_scene_history(second, data, 17, 1.25, 1.25, {}, clear, work);
    check(work.queries == 0 &&
              sample_particle_scene_history(frozen, data, 1.25, 1.25, 1.25) == values,
          "A frozen interval queried physics, replayed births or changed a world sample");
    reject([&] { (void)sample_particle_scene_history(second, data, 1, 1.25, .99); });
}
void contact_death_and_failure() {
    auto data = effect();
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 2;
    data.lifetime = 2;
    data.scene_collision->bounce_limit = 0;
    const ParticleSceneQuery wall = [](const Double3& origin, const Double3& delta,
                                       double radius) -> std::optional<PhysicsHit> {
        const auto distance = 2 - radius - origin[0];
        if (delta[0] <= 0 || distance < 0 || distance > delta[0])
            return {};
        return PhysicsHit{{}, {2, 0, 0}, {-1, 0, 0}, distance / delta[0]};
    };
    ParticleSceneWork work;
    const auto killed = advance_particle_scene_history({}, data, 17, 0, 1, {}, wall, work);
    check(killed.live.empty() &&
              sample_particle_scene_history(killed, data, 0, 1, .97).size() == 2 &&
              sample_particle_scene_history(killed, data, 0, 1, .98).empty(),
          "A killed burst vanished before its impact or remained after it");
    work = {};
    const auto later = advance_particle_scene_history(killed, data, 17, 1, 1.5, {}, wall, work);
    check(later.live.empty() && later.traces.empty() && work.queries == 0,
          "A killed burst replayed after its initial interval");
    work = {};
    const auto initial = advance_particle_scene_history({}, data, 17, 0, .5, {}, clear, work);
    const auto retained = sample_particle_scene_history(initial, data, 0, .5, .5);
    unsigned calls = 0;
    work = {};
    reject([&] {
        (void)advance_particle_scene_history(
            initial, data, 17, .5, .75, {},
            [&](const auto& origin, const auto& delta, double radius) {
                if (++calls == 2)
                    throw std::runtime_error("Injected second-birth native query failure");
                return wall(origin, delta, radius);
            },
            work);
    });
    check(sample_particle_scene_history(initial, data, 0, .5, .5) == retained,
          "A later-birth failure modified the admitted original history");
    auto continuous = effect();
    work = {};
    const ParticleSceneQuery penetrating = [](const auto&, const auto&,
                                              double) -> std::optional<PhysicsHit> {
        return PhysicsHit{{}, {.025, 0, 0}, {-1, 0, 0}, 0};
    };
    const auto at_boundary =
        advance_particle_scene_history({}, continuous, 17, 0, .25, {}, penetrating, work);
    check(work.queries == 1 && at_boundary.live.empty() &&
              sample_particle_scene_history(at_boundary, continuous, 0, .25, .25).empty(),
          "A penetrating birth at the exact interval endpoint escaped its initial native probe");
}
void large_clock_and_budgets() {
    auto data = effect();
    data.spawn_rate = data.capacity = 32;
    data.lifetime = 1;
    constexpr double previous = 999999999.75, current = 1000000000;
    ParticleSceneHistory original;
    original.initialized = true;
    const auto ordinal = std::uint64_t(previous * data.spawn_rate) - 1;
    const auto born = particle_birth_state(data, particle_spawn_frame(data), 17, ordinal);
    original.live.emplace(
        ordinal, ParticleSceneBirth{
                     born, {{born.origin, born.velocity}}, {1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}});
    ParticleSceneWork work;
    const auto next =
        advance_particle_scene_history(original, data, 17, previous, current, {}, clear, work);
    const auto sampled = sample_particle_scene_history(next, data, previous, current, current);
    check(sampled.size() == 9 && sampled[0].sample.birth == ordinal &&
              sampled[0].sample.age == .25 && sampled[0].sample.position[0] == .5 &&
              sampled.back().sample.birth == 31999999999ULL && sampled.back().sample.age == 0,
          "Large emission phases lost a birth, short relative age or fixed-update motion");
    data = effect();
    work = {particle_scene_query_limit, 0};
    reject([&] { (void)advance_particle_scene_history({}, data, 17, 0, .25, {}, clear, work); });
    data.spawn_rate = 256;
    data.lifetime = .01;
    work = {};
    reject([&] { (void)advance_particle_scene_history({}, data, 17, 0, 11, {}, clear, work); });
    check(work.queries == 0, "Overflowing interval queried geometry before birth-budget admission");
    reject([&] { (void)advance_particle_scene_history({}, data, 17, 1, 2, {}, clear, work); });
}
void capture_codec() {
    ParticleWorldSample value;
    value.sample.birth = 17;
    value.sample.size = .25;
    value.sample.color = {.2f, .3f, .4f};
    value.sample.position = {1e8, 2, -3};
    value.basis = {-2, .25, 0, 0, 3, .5, 0, 0, 0};
    const std::array original{value};
    const auto encoded = encode_particle_world_samples(original);
    check(decode_particle_world_samples(nlohmann::json::parse(encoded.dump())) ==
              std::vector<ParticleWorldSample>{value},
          "Particle World capture changed exact copied values/basis");
    for (unsigned kind = 0; kind < 9; ++kind) {
        auto bad = encoded;
        if (kind == 0)
            bad[0]["birth"] = -1;
        if (kind == 1)
            bad[0]["position"] = {0, 0};
        if (kind == 2)
            bad[0]["position"][0] = 1e11;
        if (kind == 3)
            bad[0]["size"] = true;
        if (kind == 4)
            bad[0]["opacity"] = 1.1;
        if (kind == 5)
            bad[0]["color"][2] = -1;
        if (kind == 6)
            bad[0]["basis"][1] = "1";
        if (kind == 7)
            bad.push_back(bad[0]);
        if (kind == 8)
            bad[0]["extra"] = 0;
        reject([&] { (void)decode_particle_world_samples(bad); });
    }
    std::vector<ParticleWorldSample> oversized(particle_scene_collision_capacity + 1, value);
    reject([&] { (void)encode_particle_world_samples(oversized); });
    value.basis[0] = std::numeric_limits<double>::quiet_NaN();
    reject([&] { validate_particle_world_samples(std::span(&value, 1)); });
}
void world_trails() {
    for (unsigned profile : {54u, 55u}) {
        auto data = effect();
        data.version = profile;
        data.mesh = engine_primitive(8);
        data.emission = ParticleEmission::Burst;
        data.capacity = data.burst_count = 1;
        data.lifetime = 2;
        data.trail = ParticleTrailSettings{.5, 4};
        validate_particle_effect(data);
        ParticleSceneWork work;
        const auto first = advance_particle_scene_history({}, data, 17, 0, .25, {}, clear, work);
        AffineTransform moved;
        moved.m[0] = 3;
        moved.m[3] = 100;
        work = {};
        const auto second =
            advance_particle_scene_history(first, data, 17, .25, .75, moved, clear, work);
        for (double time : {.25, .5, .75}) {
            const auto path = sample_particle_scene_paths(second, data, .25, .75, time);
            validate_particle_world_paths(data, time, path);
            check(decode_particle_world_paths(encode_particle_world_paths(path)) == path,
                  "World trail capture changed widths, point fields or birth ranges");
            check(path.paths.paths.size() == 1 && path.width_scales == std::vector<double>{1},
                  "World trail changed a retained birth width or lost a presentation interval");
            const auto& points = path.paths.points;
            check(points.size() >= 2 &&
                      points.front().age == std::max(0., time - data.trail->duration) &&
                      points.back().age == time,
                  "World trail lost its clipped age interval");
            for (const auto& point : points)
                check(std::abs(point.position[0] - 2 * point.age) < 1e-12,
                      "A moved emitter dragged historical World trail positions");
        }
        work = {};
        const auto frozen =
            advance_particle_scene_history(first, data, 17, .25, .25, moved, clear, work);
        check(work.queries == 0 &&
                  sample_particle_scene_paths(frozen, data, .25, .25, .25).paths.points.size() == 3,
              "Frozen World trail playback queried physics or lost its earlier tail");

        auto expired_data = data;
        expired_data.lifetime = .5;
        work = {};
        const auto expired =
            advance_particle_scene_history({}, expired_data, 17, 0, .75, {}, clear, work);
        check(expired.live.empty() &&
                  sample_particle_scene_history(expired, expired_data, 0, .75, .75).empty() &&
                  sample_particle_scene_paths(expired, expired_data, 0, .75, .75)
                          .paths.paths.size() == 1,
              "An expired head either remained visible or erased its surviving World tail");
        work = {};
        const auto gone =
            advance_particle_scene_history(expired, expired_data, 17, .75, 1, moved, clear, work);
        const auto shrinking = sample_particle_scene_paths(gone, expired_data, .75, 1, .9);
        check(
            work.queries == 0 && shrinking.paths.points.size() == 2 &&
                shrinking.paths.points.back().position[0] == 1 &&
                sample_particle_scene_paths(gone, expired_data, .75, 1, 1).paths.points.empty(),
            "A shrinking World tail queried physics, moved its endpoint or survived its boundary");

        const ParticleSceneQuery wall = [](const Double3& origin, const Double3& delta,
                                           double radius) -> std::optional<PhysicsHit> {
            const auto distance = .45 - radius - origin[0];
            if (delta[0] <= 0 || distance < 0 || distance > delta[0])
                return {};
            return PhysicsHit{{}, {.45, 0, 0}, {-1, 0, 0}, distance / delta[0]};
        };
        data.scene_collision->bounce_limit = 0;
        work = {};
        const auto killed = advance_particle_scene_history({}, data, 17, 0, .25, {}, wall, work);
        const auto contact_path = sample_particle_scene_paths(killed, data, 0, .25, .25);
        check(killed.live.empty() && contact_path.paths.paths.size() == 1 &&
                  std::abs(contact_path.paths.points.back().age - .2) < 1e-12 &&
                  std::abs(contact_path.paths.points.back().position[0] - .4) < 1e-12 &&
                  sample_particle_scene_history(killed, data, 0, .25, .25).empty(),
              "Contact death lost its exact terminal trail point or revived the head");
        work = {};
        const auto aftermath =
            advance_particle_scene_history(killed, data, 17, .25, .7, moved, clear, work);
        check(work.queries == 0 &&
                  !sample_particle_scene_paths(aftermath, data, .25, .7, .6).paths.points.empty() &&
                  sample_particle_scene_paths(aftermath, data, .25, .7, .7).paths.points.empty(),
              "A contact tail vanished early, survived its exact endpoint or queried physics");
        auto bouncing = data;
        bouncing.scene_collision->bounce_limit = 4;
        bouncing.scene_collision->restitution = 1;
        work = {};
        const auto rebound =
            advance_particle_scene_history({}, bouncing, 17, 0, .5, {}, wall, work);
        const auto turned = sample_particle_scene_paths(rebound, bouncing, 0, .5, .5);
        check(std::any_of(turned.paths.points.begin(), turned.paths.points.end(),
                          [](const auto& point) {
                              return std::abs(point.age - .2) < 1e-12 && point.position[0] > .39;
                          }),
              "World trail sampling cut across a sharp native response instead of retaining its "
              "contact corner");
        work = {};
        const auto continued =
            advance_particle_scene_history(rebound, bouncing, 17, .5, .6, moved, clear, work);
        const auto retained_turn = sample_particle_scene_paths(continued, bouncing, .5, .6, .6);
        check(std::any_of(retained_turn.paths.points.begin(), retained_turn.paths.points.end(),
                          [](const auto& point) { return std::abs(point.age - .2) < 1e-12; }),
              "A retained World trail smoothed away an earlier contact corner");
        auto insufficient = bouncing;
        insufficient.trail->segments = 1;
        work = {};
        reject([&] {
            (void)advance_particle_scene_history({}, insufficient, 17, 0, .5, {}, wall, work);
        });
        // The same corner must be admitted at every intermediate presentation
        // alpha even when its complete tail has expired at the new clock.
        insufficient.lifetime = .25;
        work = {};
        reject([&] {
            (void)advance_particle_scene_history({}, insufficient, 17, 0, 1, {}, wall, work);
        });
        work = {};
        const auto recovered =
            advance_particle_scene_history({}, insufficient, 17, 0, .1, {}, wall, work);
        check(sample_particle_scene_paths(recovered, insufficient, 0, .1, .1).paths.points.size() ==
                  2,
              "Rejected World contact geometry prevented a smaller valid update");

        auto curved = bouncing;
        curved.acceleration = {0, 1, .5};
        curved.drag = .25;
        curved.size_start = .2;
        curved.size_end = .6;
        curved.trail->segments = 8;
        ParticleSceneHistory history;
        for (unsigned step = 0; step < 6; ++step) {
            work = {};
            history = advance_particle_scene_history(history, curved, 17, step * .1,
                                                     (step + 1) * .1, {}, clear, work);
        }
        const auto curve_path = sample_particle_scene_paths(history, curved, .5, .6, .55);
        for (const auto& point : curve_path.paths.points) {
            const auto expected = evaluate_particle_motion({{}, {2, 0, 0}}, curved.acceleration,
                                                           curved.drag, point.age);
            for (unsigned axis = 0; axis < 3; ++axis)
                check(std::abs(point.position[axis] - expected.position[axis]) < .002,
                      "Retained World trail force/drag motion exceeded its sampled approximation");
            check(std::abs(point.size - std::lerp(.2, .6, point.age / curved.lifetime)) < 1e-12,
                  "World trail appearance used extended tail life instead of original birth age");
        }

        auto continuous = data;
        continuous.emission = ParticleEmission::Continuous;
        continuous.capacity = 8;
        continuous.spawn_rate = 2;
        work = {};
        const auto old =
            advance_particle_scene_history({}, continuous, 17, 0, .75, {}, clear, work);
        work = {};
        const auto combined =
            advance_particle_scene_history(old, continuous, 17, .75, 1.25, moved, clear, work);
        const auto paths = sample_particle_scene_paths(combined, continuous, .75, 1.25, 1.25);
        check(paths.paths.paths.size() == 2 && paths.width_scales == std::vector<double>({1, 3}) &&
                  paths.paths.points[paths.paths.paths[0].first].position[0] < 2 &&
                  paths.paths.points[paths.paths.paths[1].first].position[0] == 100,
              "World trail births lost their independent adopted position or width");

        auto long_step = data;
        long_step.lifetime = 30;
        long_step.trail = ParticleTrailSettings{.001, 16};
        work = {};
        const auto bounded =
            advance_particle_scene_history({}, long_step, 17, 0, 30, {}, clear, work);
        check(work.queries == 1 && bounded.trail_current->points.size() <= 19,
              "A long fixed update retained every crossed history sample or queried extra physics");
        const auto middle = sample_particle_scene_paths(bounded, long_step, 0, 30, 15);
        check(middle.paths.paths.size() == 1 && middle.paths.points.size() == 17 &&
                  middle.paths.points.back().position[0] == 30,
              "A bounded long-step snapshot erased intermediate presentation motion");
        auto invalid = second;
        auto late = continuous;
        late.trail = ParticleTrailSettings{.001, 8};
        const double clock = 1e8 + .25;
        ParticleSceneHistory ended;
        ended.initialized = true;
        ended.trail_current = ParticleSceneTrailSnapshot{clock, {}, {}};
        work = {};
        const auto born_late =
            advance_particle_scene_history(ended, late, 17, clock, clock + .5, {}, clear, work);
        const auto late_paths =
            sample_particle_scene_paths(born_late, late, clock, clock + .5, clock + .375);
        check(late_paths.paths.paths.size() == 1 && late_paths.paths.points.size() == 9 &&
                  late_paths.paths.paths.front().birth == std::uint64_t(clock * late.spawn_rate) &&
                  std::abs(late_paths.paths.points.back().position[0] - .25) < 1e-12,
              "Large-clock World trail sampling changed emission phase or rebuilt expired births");
        invalid.trail_previous->points.front().age = std::numeric_limits<double>::quiet_NaN();
        reject([&] { (void)sample_particle_scene_paths(invalid, data, .25, .75, .5); });
        const auto captured = sample_particle_scene_paths(second, data, .25, .75, .75);
        reject([&] { validate_particle_world_paths(data, .25, captured); });
        const auto wire = encode_particle_world_paths(captured);
        for (unsigned kind = 0; kind < 10; ++kind) {
            auto bad = wire;
            if (kind == 0)
                bad["extra"] = 0;
            if (kind == 1)
                bad["paths"][0][1] = 1;
            if (kind == 2)
                bad["paths"][0][2] = 1;
            if (kind == 3)
                bad["paths"][0][1] = std::numeric_limits<std::uint64_t>::max();
            if (kind == 4)
                bad["paths"][0][3] = -1;
            if (kind == 5)
                bad["points"][0][0] = true;
            if (kind == 6)
                bad["points"][0][4] = 0;
            if (kind == 7)
                bad["points"][0][5] = 1.000000001;
            if (kind == 8)
                bad["points"][0][8] = 2;
            if (kind == 9)
                bad["paths"].push_back(bad["paths"][0]);
            reject([&] { (void)decode_particle_world_paths(bad); });
        }
    }
}
} // namespace
int main() {
    try {
        intervals_and_frames();
        timed_query_adapter();
        contact_death_and_failure();
        large_clock_and_budgets();
        capture_codec();
        world_trails();
        std::cout << "Scene particle interval state and capture passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
