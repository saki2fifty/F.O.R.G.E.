#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::detail {
// Derived timing in the caller's common time unit. The sequence provider owns
// admission and the root clock; this value only maps its finite active spans.
class SequenceClockMapping {
  public:
    static constexpr std::size_t maximum_spans = 256;
    struct Span {
        double begin = 0, end = 0, local_begin = 0, local_end = 0;
        // The containing parent cycle can reset before this child becomes active.
        double reset_begin = 0;
        bool active = true;
        bool operator==(const Span&) const = default;
        double sample(double root, double rate) const {
            if (root <= begin)
                return local_begin;
            if (root >= end)
                return local_end;
            return std::clamp(local_begin + (root - begin) * rate, local_begin, local_end);
        }
        double root_point(double local, double rate) const {
            // Stored endpoints keep adjacent entry/end events coincident,
            // including fractional rates whose inverse cannot be represented.
            if (local <= local_begin)
                return begin;
            if (local >= local_end)
                return end;
            return std::clamp(begin + (local - local_begin) / rate, begin, end);
        }
    };
    static SequenceClockMapping root(double duration) {
        validate_duration(duration);
        SequenceClockMapping result;
        result.duration_ = duration;
        result.append({0, duration, 0, duration});
        return result;
    }
    static SequenceClockMapping child(const SequenceClockMapping& parent, double start,
                                      double duration, double rate, bool loop) {
        validate_duration(duration);
        if (!std::isfinite(start) || start < 0 || start > parent.duration_ ||
            !std::isfinite(rate) || rate <= 0 || !std::isfinite(parent.rate_ * rate) ||
            parent.rate_ * rate <= 0)
            throw std::runtime_error("Invalid child sequence clock placement");
        SequenceClockMapping result;
        result.duration_ = duration;
        result.rate_ = parent.rate_ * rate;
        struct Plan {
            long double begin, end, origin, first_cycle;
            std::size_t count;
            bool active;
        };
        const auto plan = [&](const Span& span) -> Plan {
            const long double origin =
                start >= span.local_begin && start <= span.local_end
                    ? span.root_point(start, parent.rate_)
                    : span.begin +
                          (static_cast<long double>(start) - span.local_begin) / parent.rate_;
            const long double begin = std::max(static_cast<long double>(span.begin), origin);
            const long double end = loop ? span.end
                                         : std::min(static_cast<long double>(span.end),
                                                    origin + duration / result.rate_);
            if (!span.active || end < begin)
                return {span.end, span.end, 0, 0, 1, false};
            if (!loop)
                return {begin, end, origin, 0, 1, true};
            const long double first = std::floor((begin - origin) * result.rate_ / duration);
            const long double count =
                std::max(1.L, std::ceil((end - origin) * result.rate_ / duration) - first);
            // Check the floating count before any integer cast or iteration.
            if (!std::isfinite(count) || count > maximum_spans)
                throw std::runtime_error("Child sequence exceeds 256 derived clock spans");
            return {begin, end, origin, first, static_cast<std::size_t>(count), true};
        };
        std::size_t count = 0;
        for (const auto& span : parent.spans()) {
            const auto planned = plan(span);
            if (planned.count > maximum_spans - count)
                throw std::runtime_error("Child sequence exceeds 256 derived clock spans");
            count += planned.count;
        }
        for (const auto& span : parent.spans()) {
            const auto planned = plan(span);
            if (!planned.active) {
                result.append({span.end, span.end, 0, 0, span.reset_begin, false});
                continue;
            }
            for (std::size_t i = 0; i < planned.count; ++i) {
                const long double cycle = planned.first_cycle + i;
                const long double begin =
                    loop ? std::max(planned.begin, planned.origin + cycle * duration / result.rate_)
                         : planned.begin;
                const long double end =
                    loop ? std::min(planned.end,
                                    planned.origin + (cycle + 1) * duration / result.rate_)
                         : planned.end;
                result.append(
                    {double(begin), double(end),
                     double(std::clamp((begin - planned.origin) * result.rate_ - cycle * duration,
                                       0.L, static_cast<long double>(duration))),
                     double(std::clamp((end - planned.origin) * result.rate_ - cycle * duration,
                                       0.L, static_cast<long double>(duration))),
                     i ? double(begin) : span.reset_begin});
            }
        }
        return result;
    }
    std::span<const Span> spans() const {
        return count_ > 1 ? std::span<const Span>(many_) : std::span(&first_, count_);
    }
    double rate() const { return rate_; }
    double duration() const { return duration_; }
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
        return span ? span->sample(root, rate_) : 0;
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
    double duration_ = 0, rate_ = 1;
    std::size_t count_ = 0;
    Span first_;
    std::vector<Span> many_;
};
} // namespace forge::detail
