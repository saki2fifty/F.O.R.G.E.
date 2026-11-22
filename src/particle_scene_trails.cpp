#include "particle_scene_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void finite(double value, double low, double high) {
    require(std::isfinite(value) && value >= low && value <= high,
            "Particle World trail history exceeds its finite range");
}
void validate_snapshot(const ParticleSceneTrailSnapshot& snapshot, const ParticleEffectData& data,
                       double time) {
    require(snapshot.time == time && snapshot.ranges.size() <= data.capacity,
            "Particle World trail snapshot has a stale clock or too many births");
    const auto limit = 2 * std::size_t(data.trail->segments) + 4;
    require(snapshot.points.size() <= data.capacity * limit,
            "Particle World trail history exceeds its point budget");
    std::size_t consumed = 0;
    std::optional<std::uint64_t> previous;
    for (const auto& range : snapshot.ranges) {
        require(range.birth < 256000000000ULL && (!previous || range.birth > *previous) &&
                    range.first == consumed && range.count && range.count <= limit &&
                    range.first <= snapshot.points.size() &&
                    range.count <= snapshot.points.size() - range.first,
                "Particle World trail history has an invalid birth or point range");
        previous = range.birth;
        finite(range.width_scale, 0, 2e10);
        finite(range.end_age, 0, data.lifetime);
        double last = -1;
        for (const auto& point : std::span(snapshot.points).subspan(range.first, range.count)) {
            finite(point.age, 0, range.end_age);
            require(point.age > last, "Particle World trail history ages are not ordered");
            last = point.age;
            for (auto value : point.position)
                finite(value, -1e10, 1e10);
        }
        consumed += range.count;
    }
    require(consumed == snapshot.points.size(),
            "Particle World trail history has unowned point storage");
}
struct TrailBirth {
    std::uint64_t ordinal;
    const ParticleSceneTrailRange* previous;
    const ParticleSceneTrace* trace;
    double start_age, end_age, width_scale;
};
template <class Visit>
void visit_births(const ParticleSceneHistory& history, const ParticleEffectData& data,
                  double previous, double current, double seconds, Visit&& visit,
                  bool include_expired = false) {
    validate_particle_effect(data);
    require(particle_uses_scene_trails(data), "World trail sampling needs a Scene trail profile");
    finite(previous, 0, 1e9);
    finite(current, previous, 1e9);
    finite(seconds, previous, current);
    if (!history.initialized) {
        require(previous == 0 && current == 0,
                "Particle World trails cannot reconstruct an elapsed World");
        return;
    }
    require(history.trail_previous.has_value(), "Particle World trail snapshot is missing");
    const auto& snapshot = *history.trail_previous;
    validate_snapshot(snapshot, data, previous);
    require(history.traces.size() <= particle_scene_trace_limit,
            "Particle World trail interval exceeds its trace budget");
    const auto window = particle_birth_window(data, seconds, data.trail->duration);
    std::size_t old = 0, trace = 0;
    std::optional<std::uint64_t> last;
    while (old < snapshot.ranges.size() || trace < history.traces.size()) {
        const auto* a = old < snapshot.ranges.size() ? &snapshot.ranges[old] : nullptr;
        const auto* b = trace < history.traces.size() ? &history.traces[trace] : nullptr;
        const auto ordinal = a && (!b || a->birth < b->birth.ordinal) ? a->birth : b->birth.ordinal;
        require(!last || ordinal > *last,
                "Particle World trail interval has unordered birth traces");
        last = ordinal;
        const auto* retained = a && a->birth == ordinal ? a : nullptr;
        const auto* active = b && b->birth.ordinal == ordinal ? b : nullptr;
        old += retained != nullptr;
        trace += active != nullptr;
        if (!include_expired && (ordinal < window.first || ordinal >= window.last))
            continue;
        const double rate = data.emission == ParticleEmission::Burst ? 1 : data.spawn_rate;
        const double birth_phase =
            data.emission == ParticleEmission::Burst ? 0 : double(ordinal + 1);
        const auto phase = seconds * rate - birth_phase;
        const auto age = include_expired ? std::optional<double>{phase / rate}
                                         : particle_birth_age(data, window, seconds, ordinal);
        if (!age)
            continue;
        TrailBirth birth{ordinal,
                         retained,
                         active,
                         std::max(0., (previous * rate - birth_phase) / rate),
                         retained ? retained->end_age : data.lifetime,
                         retained ? retained->width_scale : 1};
        if (active) {
            birth.start_age = particle_scene_trace_start_age(data, *active, previous);
            birth.width_scale = std::hypot(active->basis[0], active->basis[3], active->basis[6]);
            if (retained)
                require(birth.width_scale == retained->width_scale,
                        "Particle World trail birth width changed between snapshots");
            if (active->step.death)
                birth.end_age = std::min(birth.end_age, birth.start_age + *active->step.death);
        }
        finite(birth.width_scale, 0, 2e10);
        finite(birth.end_age, 0, data.lifetime);
        if (!include_expired && phase >= (birth.end_age + data.trail->duration) * rate)
            continue;
        if (include_expired ||
            std::min(*age, birth.end_age) > std::max(0., *age - data.trail->duration))
            visit(birth, *age, snapshot);
    }
}
Double3 position_at(const TrailBirth& birth, const ParticleSceneTrailSnapshot& snapshot,
                    double age) {
    finite(age, 0, birth.end_age);
    if (birth.trace && age >= birth.start_age) {
        const auto& step = birth.trace->step;
        const auto endpoint = std::min(step.duration, step.death.value_or(step.duration));
        const auto motion = sample_particle_scene_motion(
            step, std::clamp(age - birth.start_age, 0., endpoint), true);
        require(bool(motion), "Particle World trail requested motion after contact death");
        return motion->position;
    }
    require(birth.previous, "Particle World trail has no retained earlier motion");
    const auto points =
        std::span(snapshot.points).subspan(birth.previous->first, birth.previous->count);
    require(age >= points.front().age && age <= points.back().age,
            "Particle World trail requested motion outside its retained window");
    const auto next = std::upper_bound(points.begin(), points.end(), age,
                                       [](double at, const auto& point) { return at < point.age; });
    if (next == points.end())
        return points.back().position;
    const auto& prior = *(next - 1);
    const auto weight = (age - prior.age) / (next->age - prior.age);
    Double3 result;
    for (unsigned axis = 0; axis < 3; ++axis)
        result[axis] = std::lerp(prior.position[axis], next->position[axis], weight);
    return result;
}
std::vector<ParticleSceneTrailPoint> contact_points(const TrailBirth& birth,
                                                    const ParticleSceneTrailSnapshot& snapshot) {
    std::vector<ParticleSceneTrailPoint> result;
    if (birth.previous)
        for (const auto& point :
             std::span(snapshot.points).subspan(birth.previous->first, birth.previous->count))
            if (point.contact && point.age < birth.end_age)
                result.push_back(point);
    if (birth.trace) {
        const auto& knots = birth.trace->step.knots;
        require(knots.size() <= 2 * particle_scene_birth_query_limit + 1,
                "Particle World trail interval exceeds its knot budget");
        // Equal-time motion knots bracket the existing contact response. Keep
        // its adopted departure side; terminal deaths use the path endpoint.
        for (std::size_t i = 1; i < knots.size(); ++i)
            if (knots[i].time == knots[i - 1].time) {
                const auto age = birth.start_age + knots[i].time;
                if (age < birth.end_age)
                    result.push_back({age, knots[i].motion.position, true});
            }
    }
    require(result.size() <= particle_scene_birth_query_limit + 2 * particle_path_segment_limit + 4,
            "Particle World trail contact storage exceeds its budget");
    std::stable_sort(result.begin(), result.end(),
                     [](const auto& a, const auto& b) { return a.age < b.age; });
    std::size_t count = 0;
    for (const auto point : result) {
        if (count && result[count - 1].age == point.age)
            result[count - 1] = point;
        else
            result[count++] = point;
    }
    result.resize(count);
    return result;
}
} // namespace

ParticleScenePathSamples sample_particle_scene_paths(const ParticleSceneHistory& history,
                                                     const ParticleEffectData& data,
                                                     double previous, double current,
                                                     double seconds) {
    ParticleScenePathSamples result;
    visit_births(history, data, previous, current, seconds,
                 [&](const TrailBirth& birth, double age, const auto& snapshot) {
                     const auto first = result.paths.points.size();
                     const auto tail = std::max(0., age - data.trail->duration);
                     const auto head = std::min(age, birth.end_age);
                     std::vector<double> ages{tail};
                     for (const auto& contact : contact_points(birth, snapshot))
                         if (contact.age > tail && contact.age < head)
                             ages.push_back(contact.age);
                     ages.push_back(head);
                     require(ages.size() <= data.trail->segments + 1,
                             "Particle World trail contacts exceed the segment budget");
                     const auto target = std::max(
                         ages.size(),
                         1 + std::size_t(std::clamp(std::ceil((head - tail) * data.trail->segments /
                                                              data.trail->duration),
                                                    1., double(data.trail->segments))));
                     while (ages.size() < target) {
                         std::size_t gap = 1;
                         for (std::size_t i = 2; i < ages.size(); ++i)
                             if (ages[i] - ages[i - 1] > ages[gap] - ages[gap - 1])
                                 gap = i;
                         const auto middle = std::midpoint(ages[gap - 1], ages[gap]);
                         require(middle > ages[gap - 1] && middle < ages[gap],
                                 "Particle World trail cannot subdivide at this age precision");
                         ages.insert(ages.begin() + gap, middle);
                     }
                     for (const auto at : ages) {
                         ParticleMotionState motion{position_at(birth, snapshot, at), {}};
                         // Ribbon geometry consumes positions/appearance, not a
                         // fabricated historical instantaneous velocity.
                         result.paths.points.push_back(sample_particle_birth(
                             data, ParticleBirthState{birth.ordinal}, at, &motion));
                     }
                     const auto count = result.paths.points.size() - first;
                     require(count >= 2 && result.paths.paths.size() < data.capacity &&
                                 result.paths.segment_count() - 1 <= particle_scene_capacity,
                             "Particle World paths exceed their birth or segment budget");
                     result.paths.paths.push_back({birth.ordinal, first, count});
                     result.width_scales.push_back(birth.width_scale);
                 });
    return result;
}

void retain_particle_scene_trails(ParticleSceneHistory& next, const ParticleSceneHistory& original,
                                  const ParticleEffectData& data, double previous, double current) {
    if (original.initialized) {
        require(original.trail_current.has_value(),
                "Particle World trail source changed without restarting history");
        validate_snapshot(*original.trail_current, data, previous);
        next.trail_previous = original.trail_current;
    } else
        next.trail_previous = ParticleSceneTrailSnapshot{previous, {}, {}};
    // Validate every presentation window in this interval, including births
    // whose entire tail expires before the fixed update ends. Reject the whole
    // candidate rather than silently dropping a corner at a later render alpha.
    visit_births(
        next, data, previous, current, current,
        [&](const TrailBirth& birth, double age, const auto& snapshot) {
            const auto contacts = contact_points(birth, snapshot);
            std::size_t first = 0;
            for (std::size_t i = 0; i < contacts.size(); ++i) {
                const auto at = std::max(
                    birth.start_age,
                    std::nextafter(contacts[i].age, std::numeric_limits<double>::infinity()));
                if (at > age || contacts[i].age >= birth.end_age)
                    continue;
                const auto tail = std::max(0., at - data.trail->duration);
                while (first <= i && contacts[first].age <= tail)
                    ++first;
                require(i + 1 - first < data.trail->segments,
                        "Particle World trail contacts exceed the segment budget");
            }
        },
        true);
    ParticleSceneTrailSnapshot retained{current, {}, {}};
    visit_births(next, data, previous, current, current,
                 [&](const TrailBirth& birth, double age, const auto& snapshot) {
                     const auto first = retained.points.size();
                     const auto head = std::min(age, birth.end_age);
                     const auto spacing = data.trail->duration / data.trail->segments;
                     const auto tail = std::max(0., age - data.trail->duration);
                     const auto begin = std::floor(tail / spacing);
                     const auto end = std::floor(head / spacing);
                     require(end >= begin && end - begin <= data.trail->segments + 1,
                             "Particle World trail age grid exceeds its bounded window");
                     for (double i = begin; i <= end; ++i) {
                         const auto at = std::clamp(i * spacing, 0., head);
                         if (retained.points.size() > first && at <= retained.points.back().age)
                             continue;
                         retained.points.push_back({at, position_at(birth, snapshot, at)});
                     }
                     if (retained.points.size() == first || retained.points.back().age < head)
                         retained.points.push_back({head, position_at(birth, snapshot, head)});
                     for (const auto& contact : contact_points(birth, snapshot)) {
                         if (contact.age < begin * spacing || contact.age > head)
                             continue;
                         const auto same = std::find_if(
                             retained.points.begin() + first, retained.points.end(),
                             [&](const auto& point) { return point.age == contact.age; });
                         if (same == retained.points.end())
                             retained.points.push_back(contact);
                         else
                             *same = contact;
                     }
                     std::sort(retained.points.begin() + first, retained.points.end(),
                               [](const auto& a, const auto& b) { return a.age < b.age; });
                     retained.ranges.push_back({birth.ordinal, birth.width_scale, birth.end_age,
                                                first, retained.points.size() - first});
                 });
    validate_snapshot(retained, data, current);
    next.trail_current = std::move(retained);
}
} // namespace forge
