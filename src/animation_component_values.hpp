#pragma once
#include "authored_component.hpp"
#include <forge/transform_animation.hpp>
#include <forge/world.hpp>

namespace forge {
// Shared detached-value admission for native playback and copied editor preview.
std::optional<AnimationComponentValueType>
animation_component_field_type(const nlohmann::json& field);
bool animation_component_field_supported(const nlohmann::json& field);
nlohmann::json sample_animation_component(const nlohmann::json& declaration,
                                          const nlohmann::json& current,
                                          const TransformAnimationClip&, double time,
                                          bool whole_clip);
struct AnimationComponentCandidate {
    const detail::AuthoredCodec* codec;
    std::optional<detail::ReflectedCandidate> value;
    nlohmann::json patch;
};
std::vector<AnimationComponentCandidate> prepare_animation_components(WorldContext&, flecs::entity,
                                                                      const TransformAnimationClip&,
                                                                      double time,
                                                                      bool whole_clip = false);
nlohmann::json animation_component_patch(const std::vector<AnimationComponentCandidate>&);
} // namespace forge
