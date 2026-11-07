#pragma once
#include "authored_component.hpp"
#include "builtins.hpp"
#include <forge/transform_animation.hpp>
#include <forge/world.hpp>

namespace forge {
// Shared detached-value admission for native playback and copied editor preview.
// Builtins explicitly opt in with a reflected stamp; authored values also carry $forge.
bool animation_component_matches(const nlohmann::json& declaration, const nlohmann::json& current);
std::optional<AnimationComponentValueType>
animation_component_field_type(const nlohmann::json& field);
bool animation_component_field_supported(const nlohmann::json& field);
const nlohmann::json& animation_component_field(const nlohmann::json& declaration,
                                                const AnimationComponentBinding&);
std::vector<AnimationComponentBinding>
animation_component_fields(const nlohmann::json& declaration,
                           const nlohmann::json* current = nullptr);
nlohmann::json::json_pointer animation_component_pointer(const nlohmann::json& declaration,
                                                         const nlohmann::json& current,
                                                         const AnimationComponentBinding&);
nlohmann::json sample_animation_component(const nlohmann::json& declaration,
                                          const nlohmann::json& current,
                                          const TransformAnimationClip&, double time,
                                          bool whole_clip,
                                          std::span<const nlohmann::json> samples = {});
// Apply copied projections through existing material and reflected-extension owners.
void merge_animation_properties(nlohmann::json& components, const nlohmann::json& patch,
                                const WorldContext&);
struct AnimationComponentCandidate {
    const detail::AuthoredCodec* codec;
    std::optional<detail::ReflectedCandidate> value;
    nlohmann::json patch;
    const detail::Builtin* builtin = nullptr;
    std::optional<detail::Value> builtin_value;
    std::string key() const;
    void apply(flecs::entity) const;
};
std::vector<AnimationComponentCandidate>
prepare_animation_components(WorldContext&, flecs::entity, const TransformAnimationClip&,
                             double time, bool whole_clip = false,
                             std::span<const nlohmann::json> samples = {});
nlohmann::json animation_component_patch(const std::vector<AnimationComponentCandidate>&);
} // namespace forge
