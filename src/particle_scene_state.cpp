#include "particle_scene_state.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void interval(double previous, double current) {
    require(std::isfinite(previous) && std::isfinite(current) && previous >= 0 &&
                previous <= current && current <= 1e9,
            "Particle Scene interval is outside its ordered finite range");
}
std::optional<double> live_age(const ParticleEffectData& effect, const ParticleBirthWindow& window,
                               double seconds, std::uint64_t ordinal) {
    if (effect.emission == ParticleEmission::Burst && !window.last)
        return {};
    return particle_birth_age(effect, window, seconds, ordinal);
}
ParticleSceneBirth birth(const ParticleEffectData& effect, const ParticleSpawnFrame& spawn,
                         std::uint32_t seed, std::uint64_t ordinal, const AffineTransform& frame) {
    ParticleSceneBirth result;
    result.birth = particle_birth_state(effect, spawn, seed, ordinal);
    result.state.motion = {frame.point(result.birth.origin), frame.vector(result.birth.velocity)};
    result.acceleration = frame.vector(effect.acceleration);
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 3; ++column)
            result.basis[row * 3 + column] = frame.m[row * 4 + column];
    return result;
}
} // namespace
ParticleSceneHistory advance_particle_scene_history(const ParticleSceneHistory& original,
                                                    const ParticleEffectData& effect,
                                                    std::uint32_t seed, double previous,
                                                    double current, const AffineTransform& frame,
                                                    const ParticleSceneQuery& query,
                                                    ParticleSceneWork& work) {
    validate_particle_effect(effect);
    require(particle_uses_scene_collision(effect), "Particle Scene history needs Scene values");
    interval(previous, current);
    require(original.initialized || previous == 0,
            "Particle Scene history cannot reconstruct an elapsed physics World");
    require(work.births <= particle_scene_trace_limit && work.queries <= particle_scene_query_limit,
            "Particle Scene work counters exceed the fixed-update profile");
    require(original.live.size() <= effect.capacity,
            "Particle Scene history exceeds the admitted effect capacity");
    for (auto value : frame.m)
        require(std::isfinite(value) && std::abs(value) <= 1e10,
                "Particle Scene birth frame exceeds its finite World range");
    const bool burst = effect.emission == ParticleEmission::Burst;
    const double rate = burst ? 1 : effect.spawn_rate;
    const auto phase_before = previous * rate, phase_after = current * rate;
    const auto lifetime = effect.lifetime * rate;
    const auto window_before = particle_birth_window(effect, previous);
    const auto window_after = particle_birth_window(effect, current);
    ParticleSceneHistory next;
    next.initialized = true;
    const auto advance = [&](ParticleSceneBirth value, bool born) {
        const auto ordinal = value.birth.ordinal;
        require(born || bool(live_age(effect, window_before, previous, ordinal)),
                "Particle Scene history contains a birth outside its emission phase");
        require(work.births < particle_scene_trace_limit,
                "Particle Scene response exceeds 2048 birth traces per fixed update");
        ++work.births;
        const double birth_phase = burst ? 0 : double(ordinal + 1);
        const auto start_phase = std::max(phase_before, birth_phase);
        const double elapsed_phase = start_phase - birth_phase;
        const auto duration =
            std::max(0., std::min(phase_after - start_phase, lifetime - elapsed_phase)) / rate;
        ParticleSceneTrace trace{value.birth, value.basis, (start_phase - phase_before) / rate, {}};
        trace.step = advance_particle_scene_motion(
            value.state, value.acceleration, effect.drag, duration, *effect.scene_collision,
            [&](const Double3& origin, const Double3& delta, double radius) {
                require(
                    work.queries < particle_scene_query_limit,
                    particle_uses_scene_collision_2d(effect)
                        ? "Particle Scene response exceeds 8192 circle queries per fixed update"
                        : "Particle Scene response exceeds 8192 sphere queries per fixed update");
                ++work.queries;
                return query(origin, delta, radius);
            },
            born, particle_uses_scene_collision_2d(effect));
        value.state = trace.step.state;
        if (value.state.alive && live_age(effect, window_after, current, ordinal)) {
            require(next.live.size() < effect.capacity,
                    "Particle Scene live births exceed effect capacity");
            next.live.emplace(ordinal, std::move(value));
        }
        // Old live records must correspond to the prior phase. New births can
        // both start and expire inside this interval and still need its trace.
        next.traces.push_back(std::move(trace));
    };
    for (const auto& [ordinal, value] : original.live) {
        require(ordinal == value.birth.ordinal && value.state.alive,
                "Particle Scene live history contains an invalid birth");
        advance(value, false);
    }
    const auto first = burst ? 0 : std::uint64_t(std::floor(phase_before));
    const auto last = burst ? (original.initialized ? 0 : effect.burst_count)
                            : std::uint64_t(std::floor(phase_after));
    require(last >= first && last - first <= particle_scene_trace_limit - work.births,
            "Particle Scene interval creates more than 2048 bounded birth traces");
    const auto spawn = particle_spawn_frame(effect);
    for (auto ordinal = first; ordinal < last; ++ordinal)
        advance(birth(effect, spawn, seed, ordinal, frame), true);
    if (particle_uses_scene_trails(effect))
        retain_particle_scene_trails(next, original, effect, previous, current);
    return next;
}
std::vector<ParticleWorldSample> sample_particle_scene_history(const ParticleSceneHistory& history,
                                                               const ParticleEffectData& effect,
                                                               double previous, double current,
                                                               double seconds) {
    interval(previous, current);
    require(std::isfinite(seconds) && seconds >= previous && seconds <= current,
            "Particle Scene sample is outside its fixed interval");
    require(particle_uses_scene_collision(effect), "Particle Scene capture needs Scene values");
    const double rate = effect.emission == ParticleEmission::Burst ? 1 : effect.spawn_rate;
    const auto offset = (seconds * rate - previous * rate) / rate;
    const auto window = particle_birth_window(effect, seconds);
    std::vector<ParticleWorldSample> result;
    for (const auto& trace : history.traces) {
        const auto age = live_age(effect, window, seconds, trace.birth.ordinal);
        if (!age || offset < trace.begin)
            continue;
        const auto relative = std::clamp(offset - trace.begin, 0., trace.step.duration);
        const auto motion = sample_particle_scene_motion(trace.step, relative);
        if (!motion)
            continue;
        require(result.size() < effect.capacity &&
                    result.size() < particle_scene_collision_capacity,
                "Particle Scene sample exceeds its reserved birth capacity");
        result.push_back({sample_particle_birth(effect, trace.birth, *age, &*motion), trace.basis});
    }
    validate_particle_world_samples(result);
    return result;
}
} // namespace forge
