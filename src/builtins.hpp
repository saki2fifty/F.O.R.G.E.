#pragma once
#include <array>
#include <forge/world.hpp>
#include <optional>
#include <variant>
namespace forge::detail {
using Value = std::variant<Position, Rotation, Scale, Tint, Primitive>;
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
const std::array<Builtin, 5>& builtins();
Json register_builtins(flecs::world& world);
void validate_components(const Json& components);
} // namespace forge::detail
