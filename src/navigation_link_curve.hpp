#pragma once
#include "navigation_space.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace forge::navigation_detail {
// Owning geometry value for one admitted span, without traversal state or clocks.
// Lift is world +Y in either navigation plane; XY retains the caller's depth.
class NavigationLinkCurve {
  public:
    NavigationLinkCurve(Double3 start, Double3 end, double height)
        : start_(start), end_(end), height_(height) {
        for (const auto& point : {start, end})
            for (double coordinate : point)
                if (!std::isfinite(coordinate) || std::abs(coordinate) > 4096)
                    throw std::invalid_argument("Navigation curve endpoint exceeds bounds");
        validate_height(height);
        if (std::max(start[1], end[1]) + height > 4096)
            throw std::invalid_argument("Navigation curve height exceeds bounds");
        for (unsigned axis = 0; axis != 3; ++axis)
            delta_[axis] = end[axis] - start[axis];
        chord_ = std::hypot(delta_[0], delta_[1], delta_[2]);
        horizontal_ = std::hypot(delta_[0], delta_[2]);
        if (chord_ < double(.01f) || (height > 0 && horizontal_ < double(.01f)))
            throw std::invalid_argument("Navigation curve needs a separated horizontal span");
    }

    static void validate_height(double height) {
        if (!std::isfinite(height) || height < 0 || height > 100)
            throw std::invalid_argument("Navigation curve height must be within 0-100 metres");
    }

    Double3 point(double t) const {
        parameter_bounds(t);
        if (t == 0)
            return start_;
        if (t == 1)
            return end_;
        Double3 value;
        for (unsigned axis = 0; axis != 3; ++axis)
            value[axis] = std::lerp(start_[axis], end_[axis], t);
        value[1] += 4 * height_ * t * (1 - t);
        return value;
    }

    // Projection is deliberately not clamped: an owner must reject out-of-span
    // or off-curve poses before using them as held/recovered traversal state.
    double parameter(Double3 position) const {
        for (double coordinate : position)
            if (!std::isfinite(coordinate))
                throw std::invalid_argument("Navigation curve pose must be finite");
        if (position == start_)
            return 0;
        if (position == end_)
            return 1;
        double dot = 0;
        for (unsigned axis = 0; axis != 3; ++axis)
            if (height_ == 0 || axis != 1)
                dot += (position[axis] - start_[axis]) * delta_[axis];
        const auto t = dot / (height_ == 0 ? chord_ * chord_ : horizontal_ * horizontal_);
        if (!std::isfinite(t))
            throw std::invalid_argument("Navigation curve projection exceeds bounds");
        return t;
    }

    double length(double from = 0, double to = 1) const {
        parameter_bounds(from);
        parameter_bounds(to);
        if (to < from)
            throw std::invalid_argument("Navigation curve interval is reversed");
        const auto dt = to - from;
        if (height_ == 0 || dt == 0)
            return dt * chord_;
        const auto slope = delta_[1] + 4 * height_ - 8 * height_ * from;
        const auto change = 8 * height_ * dt;
        const auto last = slope - change;
        const auto first_speed = std::hypot(horizontal_, slope);
        const auto last_speed = std::hypot(horizontal_, last);
        const auto sum = first_speed + last_speed;
        const auto ratio = change / sum;
        // Integrate hypot(horizontal, slope(t)). Factoring the primitive's
        // difference avoids subtracting nearly equal large values for tiny lifts
        // and short advances. asinh(u)-asinh(v) = 2*atanh((u-v)/(ru+rv)).
        const auto hyperbolic = ratio == 0 ? 1 : std::atanh(ratio) / ratio;
        return .5 * dt * (first_speed + last * (slope + last) / sum) +
               horizontal_ * horizontal_ * dt / sum * hyperbolic;
    }

    // Exact maximum distance from the parameter-matched chord: the difference
    // is entirely world Y, with its maximum at the interval midpoint.
    double chord_deviation(double from, double to) const {
        parameter_bounds(from);
        parameter_bounds(to);
        if (to < from)
            throw std::invalid_argument("Navigation curve interval is reversed");
        const auto width = to - from;
        return height_ * width * width;
    }

    double sweep_end(double from, double to, double maximum_deviation) const {
        if (!std::isfinite(maximum_deviation) || maximum_deviation <= 0)
            throw std::invalid_argument(
                "Navigation curve sweep tolerance must be finite and positive");
        for (unsigned split = 0; split != 48; ++split) {
            if (chord_deviation(from, to) <= maximum_deviation)
                return to;
            const auto next = std::midpoint(from, to);
            if (next == from)
                break;
            to = next;
        }
        throw std::invalid_argument("Navigation curve sweep interval exceeds subdivision bounds");
    }

    double advance(double from, double metres) const {
        parameter_bounds(from);
        if (!std::isfinite(metres) || metres < 0)
            throw std::invalid_argument("Navigation curve distance must be finite and nonnegative");
        if (metres == 0)
            return from;
        if (metres >= length(from))
            return 1;
        auto low = from, high = 1.;
        // A fixed bound makes work independent of frame time and curve shape.
        for (unsigned iteration = 0; iteration != 48; ++iteration) {
            const auto middle = std::midpoint(low, high);
            if (length(from, middle) <= metres)
                low = middle;
            else
                high = middle;
        }
        return low;
    }

  private:
    static void parameter_bounds(double t) {
        if (!std::isfinite(t) || t < 0 || t > 1)
            throw std::invalid_argument("Navigation curve parameter must be within 0-1");
    }
    Double3 start_, end_, delta_{};
    double height_, chord_ = 0, horizontal_ = 0;
};
} // namespace forge::navigation_detail
