#pragma once
#include "reflected_value.hpp"
#include <vector>
namespace forge::detail {
// Engine admission annotation on a native component entity. Structure stays in
// Flecs Meta; this carries only authoring identity, ownership and declared defaults.
struct AuthoredTypeAdmission {
    std::string key, module, category;
    std::uint32_t version = 0;
    nlohmann::json defaults;
};
std::vector<ReflectedAdapter> authoring_value_adapters(flecs::world&);
void opt_in_authoring(flecs::world&, ecs_entity_t native_type, std::string key, std::string module,
                      std::uint32_t version, nlohmann::json defaults, std::string category);
// Copied metadata only; all native IDs/callbacks remain inside the borrowed world.
nlohmann::json export_authored_types(flecs::world&);
void validate_authored_types(flecs::world&, const nlohmann::json& copied_types);
// Structural compatibility excludes physical field order and presentation labels.
std::string authored_structure_digest(const nlohmann::json& native_projection);
} // namespace forge::detail
