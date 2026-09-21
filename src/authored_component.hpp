#pragma once
#include "reflected_value.hpp"
#include <forge/engine_module.hpp>
#include <optional>
#include <vector>
namespace forge::detail {
// Private, world-local cache derived from an admitted native Meta component.
// No values or project code are owned here. The registering world outlives users.
struct AuthoredCodec {
    ecs_entity_t native_type = 0;
    nlohmann::json declaration;
    std::vector<ReflectedAdapter> adapters;
    std::string key() const;
    nlohmann::json stamp() const;
    bool matches(const nlohmann::json& value) const;
    nlohmann::json defaults() const;
    nlohmann::json editor_schema() const;
    std::optional<ReflectedCandidate> prepare(flecs::world&, const nlohmann::json&) const;
    nlohmann::json read(flecs::entity, bool effective) const;
    void apply(flecs::entity, const std::optional<ReflectedCandidate>&) const;
    nlohmann::json extensions(const nlohmann::json&) const;
    nlohmann::json merge(const nlohmann::json& known, const nlohmann::json& opaque) const;
};
std::vector<AuthoredCodec> authored_codecs(flecs::world&);
// Registers only engine-owned reconstructed value types. All project code stays
// in the schema worker. Successful registrations live until world finalization.
EngineModule copied_authoring_module(nlohmann::json copied_types);
void validate_custom_values(const nlohmann::json& editor_schema, const nlohmann::json& components);
void validate_runtime_custom_values(const WorldContext&, const nlohmann::json& components);
void project_custom_prefab_intent(nlohmann::json& values, const nlohmann::json& instance,
                                  const nlohmann::json& editor_schema);
} // namespace forge::detail
