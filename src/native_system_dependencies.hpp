#pragma once
#include <forge/world.hpp>
#include <span>
namespace forge::detail {
inline constexpr std::size_t native_dependency_node_capacity = 512;
inline constexpr std::size_t native_dependency_link_capacity = 512;
inline constexpr unsigned native_dependency_target_capacity = 8;
// Detached native lookup data. No order inference, relation edits or graph owner.
Json native_system_dependencies(flecs::world& world, std::span<const ecs_entity_t> roots);
void validate_native_system_dependencies(std::span<const ecs_entity_t> roots, const Json& snapshot);
} // namespace forge::detail
