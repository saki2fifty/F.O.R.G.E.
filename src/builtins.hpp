#pragma once
#include <array>
#include <forge/animation_components.hpp>
#include <forge/audio_components.hpp>
#include <forge/navigation_components.hpp>
#include <forge/physics_components.hpp>
#include <forge/ui_components.hpp>
#include <forge/world.hpp>
#include <optional>
#include <variant>
namespace forge::detail {
inline constexpr std::size_t builtin_count = 15;
using Value = std::variant<LocalTranslation, LocalRotation, LocalScale, Tint, Primitive,
                           PhysicsBody, BoxCollider, SphereCollider, CapsuleCollider, AudioSource,
                           AudioListener, Animator, NavigationSurface, NavigationAgent, UiDocument>;
struct Builtin {
    const char* name;
    const char* description;
    const char* unit;
    std::optional<double> minimum, maximum;
    Json defaults;
    flecs::entity (*register_type)(flecs::world&);
    Value (*decode)(const Json&);
    Json (*read)(flecs::entity, bool);
    flecs::entity (*owner)(flecs::entity);
    void (*apply)(flecs::entity, const std::optional<Value>&);
};
const std::array<Builtin, builtin_count>& builtins();
Json register_builtins(flecs::world& world, unsigned family = 0);
void validate_components(const Json& components);
// Checks registered scalar storage/ranges only; domain validation remains with each subsystem.
void validate_reflected_value(flecs::world world, ecs_entity_t type, const void* value);
template <class T> void validate_reflected_value(flecs::entity entity, const T& value) {
    validate_reflected_value(entity.world(), entity.world().id<T>(), &value);
}
} // namespace forge::detail
