#include "particle_scene_state.hpp"
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
} // namespace
int main() {
    try {
        intervals_and_frames();
        contact_death_and_failure();
        large_clock_and_budgets();
        capture_codec();
        std::cout << "Scene particle interval state and capture passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
