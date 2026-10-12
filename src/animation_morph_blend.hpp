#pragma once
#include "morph_animation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
namespace forge::animation_detail {
using MorphPose = std::vector<asset_detail::MorphWeightsSample>;
// Explicit-node union and immutable defaults are prepared at resource adoption.
// Missing curves never inherit a previous frame's deformation.
inline MorphPose merge_morph_defaults(std::span<const asset_detail::MorphWeightsSample> first,
                                      std::span<const asset_detail::MorphWeightsSample> second) {
    std::map<std::size_t, std::vector<float>> union_nodes;
    for (const auto samples : {first, second})
        for (const auto& value : samples) {
            const auto [entry, inserted] = union_nodes.emplace(value.node, value.weights);
            if (!inserted && entry->second != value.weights)
                throw std::runtime_error("Morph clips disagree on exact model defaults");
        }
    MorphPose result;
    if (union_nodes.size() > 1024)
        throw std::runtime_error("Morph node union exceeds the admitted track profile");
    for (auto& [node, weights] : union_nodes)
        result.push_back({node, std::move(weights)});
    return result;
}
inline MorphPose blend_morph_samples(std::span<const asset_detail::MorphWeightsSample> defaults,
                                     std::span<const asset_detail::MorphWeightsSample> first,
                                     std::span<const asset_detail::MorphWeightsSample> second,
                                     double weight, bool additive = false,
                                     std::span<const float> mask = {}) {
    if (!std::isfinite(weight) || weight < 0 || weight > 1 ||
        (!mask.empty() && mask.size() != defaults.size()))
        throw std::runtime_error("Invalid morph blend weight or mask size");
    for (const auto samples : {defaults, first, second})
        for (std::size_t i = 1; i < samples.size(); ++i)
            if (samples[i - 1].node >= samples[i].node)
                throw std::runtime_error("Morph blend nodes must be unique and ordered");
    std::size_t a = 0, b = 0;
    const auto next = [](auto samples, std::size_t& at,
                         const auto& reference) -> const std::vector<float>& {
        if (at == samples.size() || samples[at].node > reference.node)
            return reference.weights;
        if (samples[at].node < reference.node ||
            samples[at].weights.size() != reference.weights.size())
            throw std::runtime_error("Morph blend node or target count differs");
        return samples[at++].weights;
    };
    MorphPose result;
    result.reserve(defaults.size());
    for (std::size_t i = 0; i < defaults.size(); ++i) {
        const auto& reference = defaults[i];
        const auto& left = next(first, a, reference);
        const auto& right = next(second, b, reference);
        const double selected = mask.empty() ? 1 : mask[i];
        if (!std::isfinite(selected) || selected < 0 || selected > 1)
            throw std::runtime_error("Morph mask outside finite [0,1]");
        const auto w = weight * selected;
        asset_detail::MorphWeightsSample value{reference.node, {}};
        value.weights.reserve(reference.weights.size());
        for (std::size_t c = 0; c < reference.weights.size(); ++c) {
            if (!std::isfinite(left[c]) || !std::isfinite(right[c]) ||
                !std::isfinite(reference.weights[c]))
                throw std::runtime_error("Morph blend source is nonfinite");
            const double v = additive
                                 ? double(left[c]) + w * (double(right[c]) - reference.weights[c])
                                 : (1 - w) * left[c] + w * right[c];
            if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
                throw std::runtime_error("Morph blend exceeds finite float storage");
            value.weights.push_back(float(v));
        }
        result.push_back(std::move(value));
    }
    if (a != first.size() || b != second.size())
        throw std::runtime_error("Morph blend contains an unadmitted node");
    return result;
}
} // namespace forge::animation_detail
