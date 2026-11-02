#pragma once
#include <flecs.h>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>
namespace forge::detail {
// Optional engine authoring semantics attached to a native Meta member entity.
// This identifies entries within that collection, not globally identifiable objects.
struct ReflectedSequenceKey {
    std::string member;
    std::vector<std::string> additional_members;
};
// Empty means positional. Composite keys use an unambiguous internal JSON tuple;
// this is collection-local identity, not a new authored ID or asset reference.
std::string reflected_sequence_identity(const nlohmann::json& field, const nlohmann::json& element);
inline constexpr const char* reflected_sequence_key_type = "forge.meta.sequence_key";
// A native member's semantic limit can narrow the common bounded vector envelope.
struct ReflectedSequenceLimit {
    std::size_t maximum;
    // Zero retains the common value envelope; layout/schema bounds do not expand.
    std::size_t value_bytes = 0, value_elements = 0;
};
inline constexpr const char* reflected_sequence_limit_type = "forge.meta.sequence_limit";
// Explicit additive native member evolution. Ordinary reflected members remain
// required; a declared missing default is validated and copied with the schema.
struct ReflectedMemberDefault {
    nlohmann::json value;
};
inline constexpr const char* reflected_member_default_type = "forge.meta.member_default";
// Static authored inputs can narrow the common numeric animation eligibility.
// Presence belongs to the native member; copied schemas only project this policy.
struct ReflectedNoAnimation {};
inline constexpr const char* reflected_no_animation_type = "forge.meta.no_animation";
struct ReflectedValueLimits {
    std::size_t bytes = 65536, elements = 4096;
};
// Copied hints must fit fixed ceilings. Only explicitly annotated native
// sequences expand the otherwise unchanged common payload envelope.
ReflectedValueLimits reflected_value_limits(const nlohmann::json& schema);
// The value must already have passed schema admission. Missing required members
// still throw; only explicit native evolution annotations supply a default.
const nlohmann::json& reflected_member_value(const nlohmann::json& field,
                                             const nlohmann::json& object);
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
    // A baseline stages an update through native copy hooks and changed root members.
    ReflectedCandidate(flecs::world world, ecs_entity_t type, const nlohmann::json& value,
                       std::span<const ReflectedAdapter> references = {},
                       const void* baseline = nullptr);
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
