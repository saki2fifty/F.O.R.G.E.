#pragma once
#include <flecs.h>
#include <nlohmann/json.hpp>
#include <span>
namespace forge::detail {
// Explicit, engine-owned adapters only. An arbitrary EcsOpaque is not an AssetRef.
struct ReflectedReference {
    ecs_entity_t type;
    const char* kind;
    const char* asset_type = nullptr;
};
// A bounded copied projection of native Meta/Doc/Units; no process-local IDs,
// offsets, hooks or object bytes cross this interface. Not an SDK opt-in by itself.
nlohmann::json reflected_type_schema(flecs::world world, ecs_entity_t type,
                                     std::span<const ReflectedReference> references = {});
// Validates detached values. Unknown fields remain intact, subject to the same
// bounded JSON envelope. Native ranges do not veto mutation; callers commit later.
void validate_reflected_json(const nlohmann::json& schema, const nlohmann::json& value);
} // namespace forge::detail
