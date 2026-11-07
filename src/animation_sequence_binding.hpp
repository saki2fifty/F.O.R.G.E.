#pragma once
#include <cmath>
#include <forge/animation_components.hpp>
#include <optional>
#include <stdexcept>
namespace forge::animation_detail {
inline void validate_sequence_rate(double rate) {
    if (!std::isfinite(rate) || rate < .25 || rate > 4)
        throw std::runtime_error("Sequence placement rate must be within 0.25–4");
}
// Configuration admission shared by native C++ values and authored decoding.
// Controller resolution and resource compatibility stay with AnimationRuntime.
inline void validate_sequence_binding(const Animator& value,
                                      std::optional<bool> authored_binding = {}) {
    const bool bound = authored_binding.value_or(bool(value.sequence.entity));
    if (bool(value.sequence.scene) != bool(value.sequence.entity))
        throw std::runtime_error("Animator sequence needs a complete Scene/entity reference");
    if (!std::isfinite(value.sequence_start_seconds) || value.sequence_start_seconds < 0 ||
        value.sequence_start_seconds > 3600 || !std::isfinite(value.sequence_next_start_seconds) ||
        value.sequence_next_start_seconds < 0 || value.sequence_next_start_seconds > 3600)
        throw std::runtime_error("Animator sequence starts must be within 0–3600 seconds");
    if (bound &&
        (value.states.id || value.blend_space.id || value.graph.id || value.blend_clip.id ||
         value.blend_root.id || value.blend_mask.id || value.blend_mode != 0))
        throw std::runtime_error(
            "Skeletal sequence tracks use direct clips without another clip-selection mode");
    if (value.sequence_next_clip.id &&
        (!bound || !value.clip.id ||
         value.sequence_next_start_seconds < value.sequence_start_seconds))
        throw std::runtime_error(
            "Next skeletal clip needs a sequence, primary clip and ordered starts");
}
} // namespace forge::animation_detail
