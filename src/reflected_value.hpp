#pragma once
#include <flecs.h>
#include <nlohmann/json.hpp>
#include <span>
namespace forge::detail {
// Optional engine authoring semantics attached to a native Meta member entity.
// This identifies entries within that collection, not globally identifiable objects.
struct ReflectedSequenceKey {
    std::string member;
};
inline constexpr const char* reflected_sequence_key_type = "forge.meta.sequence_key";
// Explicit engine-owned reference/container adapters only. No arbitrary project opaque types.
struct ReflectedAdapter {
    ecs_entity_t type;
    const char* kind; // asset_ref, entity_ref, vector, or string (native EcsOpaque adapter).
    const char* asset_type = nullptr;
    nlohmann::json (*read)(const void*) = nullptr;
    void (*assign)(void*, const nlohmann::json&) = nullptr;
};
// A bounded copied projection of native Meta/Doc/Units; no process-local IDs,
// offsets, hooks or object bytes cross this interface. Not an SDK opt-in by itself.
nlohmann::json reflected_type_schema(flecs::world world, ecs_entity_t type,
                                     std::span<const ReflectedAdapter> references = {});
// Validates detached values. Unknown fields remain intact, subject to the same
// bounded JSON envelope. Native ranges do not veto mutation; callers commit later.
void validate_reflected_json(const nlohmann::json& schema, const nlohmann::json& value);
// Process-local detached native storage. The registering world and any module
// callbacks must outlive this value. Construction never mutates an entity.
class ReflectedCandidate {
  public:
    ReflectedCandidate(flecs::world world, ecs_entity_t type, const nlohmann::json& value,
                       std::span<const ReflectedAdapter> references = {});
    ~ReflectedCandidate();
    ReflectedCandidate(const ReflectedCandidate&) = delete;
    ReflectedCandidate& operator=(const ReflectedCandidate&) = delete;
    ReflectedCandidate(ReflectedCandidate&& other) noexcept;
    ReflectedCandidate& operator=(ReflectedCandidate&& other) noexcept;
    const void* data() const { return value_; }

  private:
    ecs_world_t* world_ = nullptr;
    ecs_entity_t type_ = 0;
    void* value_ = nullptr;
};
// Reads named native Meta fields into the existing FORGE value representation.
// It never writes to/shrinks native vectors and never copies native layouts to JSON.
nlohmann::json read_reflected_native(flecs::world world, ecs_entity_t type, const void* value,
                                     std::span<const ReflectedAdapter> references = {});
} // namespace forge::detail
