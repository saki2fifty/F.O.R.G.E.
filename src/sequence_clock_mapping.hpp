#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::detail {
// Authored inputs share the root's input unit. Its explicit timebase converts
// spans/samples to the caller's clock unit once, preserving authored boundaries.
// The sequence provider owns admission and the sole root clock.
class SequenceClockMapping {
  public:
    static constexpr std::size_t maximum_spans = 256;
    static constexpr std::size_t maximum_placements = 8;
    struct Placement {
        double start = 0, rate = 1;
        bool loop = false;
    };
    struct Span {
        double begin = 0, end = 0, local_begin = 0, local_end = 0;
        // The containing parent cycle can reset before this child becomes active.
        double reset_begin = 0;
        bool active = true;
        double rate = 1;
        bool loop = false;
        std::size_t placement = 0;
        bool operator==(const Span&) const = default;
        double sample(double root) const {
            if (root <= begin)
                return local_begin;
            if (root >= end)
                return local_end;
            return std::clamp(local_begin + (root - begin) * rate, local_begin, local_end);
        }
        double root_point(double local) const {
            // Stored endpoints keep adjacent entry/end events coincident,
            // including fractional rates whose inverse cannot be represented.
            if (local <= local_begin)
                return begin;
            if (local >= local_end)
                return end;
            return std::clamp(begin + (local - local_begin) / rate, begin, end);
        }
    };
    static SequenceClockMapping root(double duration, double timebase = 1) {
        validate_duration(duration);
        validate_duration(timebase);
        validate_duration(duration * timebase);
        SequenceClockMapping result;
        result.duration_ = duration;
        result.timebase_ = timebase;
        result.append({0, duration * timebase, 0, duration * timebase});
        return result;
    }
    static SequenceClockMapping child(const SequenceClockMapping& parent, double start,
                                      double duration, double rate, bool loop) {
        const Placement placement{start, rate, loop};
        return child(parent, duration, std::span(&placement, 1));
    }
    static SequenceClockMapping child(const SequenceClockMapping& parent, double duration,
                                      std::span<const Placement> placements) {
        validate_duration(duration);
        if (placements.empty() || placements.size() > maximum_placements)
            throw std::runtime_error("Child sequence requires one to eight placements");
        for (std::size_t i = 0; i < placements.size(); ++i) {
            const auto& placement = placements[i];
            if (!std::isfinite(placement.start) || placement.start < 0 ||
                placement.start > parent.duration_ || !std::isfinite(placement.rate) ||
                placement.rate <= 0 || (i && placement.start <= placements[i - 1].start))
                throw std::runtime_error("Invalid or unordered child sequence placement");
            if (i + 1 < placements.size() && !placement.loop &&
                placement.start + duration / placement.rate > placements[i + 1].start)
                throw std::runtime_error("Child sequence placements must not overlap");
        }
        SequenceClockMapping result;
        result.duration_ = duration;
        result.timebase_ = parent.timebase_;
        const double local_duration = duration * parent.timebase_;
        validate_duration(local_duration);
        struct Plan {
            double begin, end;
            long double first_cycle;
            std::size_t count;
        };
        const auto coordinate = [&](std::size_t index, long double cycle) {
            const auto& placement = placements[index];
            // Match the authored arithmetic before converting units. Adjacent
            // finite placements/cycles then share exactly the same boundary.
            return (placement.start + double(cycle) * duration / placement.rate) * parent.timebase_;
        };
        const auto plan = [&](const Span& span, std::size_t index) -> Plan {
            const auto& placement = placements[index];
            const auto rate = span.rate * placement.rate;
            if (!std::isfinite(rate) || rate <= 0)
                throw std::runtime_error("Invalid cumulative child sequence rate");
            const double start = coordinate(index, 0);
            const double begin = std::max(span.local_begin, start);
            const double limit = index + 1 == placements.size()
                                     ? span.local_end
                                     : std::min(span.local_end, coordinate(index + 1, 0));
            const double end = placement.loop ? limit : std::min(limit, coordinate(index, 1));
            if (!span.active || end < begin || start > span.local_end)
                return {0, 0, 0, 0};
            if (!placement.loop)
                return {begin, end, 0, 1};
            const long double first = std::floor((static_cast<long double>(begin) - start) *
                                                 placement.rate / local_duration);
            long double count = std::max(1.L, std::ceil((static_cast<long double>(end) - start) *
                                                        placement.rate / local_duration) -
                                                  first);
            // One rounded final boundary can make ceil include an empty cycle.
            // Reconcile it against the same authored coordinate, before casting.
            if (!std::isfinite(count) || count > maximum_spans + 1)
                throw std::runtime_error("Child sequence exceeds 256 derived clock spans");
            if (count > 1 && coordinate(index, first + count - 1) >= end)
                --count;
            if (count > maximum_spans)
                throw std::runtime_error("Child sequence exceeds 256 derived clock spans");
            return {begin, end, first, static_cast<std::size_t>(count)};
        };
        std::size_t count = 0;
        for (const auto& span : parent.spans()) {
            std::size_t in_parent = 0;
            for (std::size_t p = 0; p < placements.size(); ++p) {
                const auto planned = plan(span, p);
                if (planned.count > maximum_spans - count - in_parent)
                    throw std::runtime_error("Child sequence exceeds 256 derived clock spans");
                in_parent += planned.count;
            }
            if (!in_parent && count == maximum_spans)
                throw std::runtime_error("Child sequence exceeds 256 derived clock spans");
            count += std::max(std::size_t(1), in_parent);
        }
        for (const auto& span : parent.spans()) {
            bool first = true;
            for (std::size_t p = 0; p < placements.size(); ++p) {
                const auto planned = plan(span, p);
                for (std::size_t i = 0; i < planned.count; ++i) {
                    const long double cycle = planned.first_cycle + i;
                    const double cycle_begin = coordinate(p, cycle),
                                 cycle_end = coordinate(p, cycle + 1);
                    const double begin = std::max(planned.begin, cycle_begin),
                                 end = std::min(planned.end, cycle_end);
                    const double root_begin = span.root_point(begin),
                                 root_end = span.root_point(end);
                    result.append({root_begin, root_end,
                                   begin == cycle_begin
                                       ? 0
                                       : std::clamp((begin - cycle_begin) * placements[p].rate, 0.,
                                                    local_duration),
                                   end == cycle_end
                                       ? local_duration
                                       : std::clamp((end - cycle_begin) * placements[p].rate, 0.,
                                                    local_duration),
                                   first ? span.reset_begin : root_begin, true,
                                   span.rate * placements[p].rate, placements[p].loop, p});
                    first = false;
                }
            }
            if (first)
                result.append({span.end, span.end, 0, 0, span.reset_begin, false, span.rate});
        }
        return result;
    }
    std::span<const Span> spans() const {
        return count_ > 1 ? std::span<const Span>(many_) : std::span(&first_, count_);
    }
    double rate(double root) const {
        const auto span = at(root);
        return span ? span->rate : 1;
    }
    bool loop(double root) const {
        const auto span = at(root);
        return span && span->loop;
    }
    bool ended(double root) const { return !spans().empty() && root >= spans().back().end; }
    double duration() const { return duration_ * timebase_; }
    const Span* at(double root) const {
        const auto values = spans();
        if (values.empty())
            return nullptr;
        const auto next =
            std::upper_bound(values.begin(), values.end(), root,
                             [](double time, const Span& span) { return time < span.reset_begin; });
        return next == values.begin() ? &values.front() : &*(next - 1);
    }
    double sample(double root) const {
        const auto span = at(root);
        return span ? span->sample(root) : 0;
    }
    bool active(double root, bool include_end = false) const {
        const auto span = at(root);
        return span && span->active && root >= span->begin &&
               (root < span->end || (include_end && root == span->end));
    }
    bool reset_crossed(double begin, double end) const {
        const auto values = spans();
        for (std::size_t i = 1; i < values.size(); ++i)
            if (values[i].reset_begin > begin && values[i].reset_begin <= end)
                return true;
        return false;
    }
    bool operator==(const SequenceClockMapping&) const = default;

  private:
    static void validate_duration(double duration) {
        if (!std::isfinite(duration) || duration <= 0)
            throw std::runtime_error("Sequence clock duration must be finite and positive");
    }
    void append(Span span) {
        if (count_ == maximum_spans || span.end < span.begin || span.reset_begin > span.begin ||
            (count_ && span.reset_begin < spans().back().end))
            throw std::runtime_error("Invalid or excessive derived sequence clock spans");
        if (!count_)
            first_ = span;
        else {
            if (count_ == 1)
                many_.push_back(first_);
            many_.push_back(span);
        }
        ++count_;
    }
    double duration_ = 0, timebase_ = 1;
    std::size_t count_ = 0;
    Span first_;
    std::vector<Span> many_;
};
} // namespace forge::detail
