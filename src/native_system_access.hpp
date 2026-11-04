#pragma once
#include <cstddef>
#include <forge/world.hpp>
#include <string>
namespace forge::detail {
inline constexpr std::size_t native_system_plan_capacity = 256;
inline constexpr std::size_t native_access_term_capacity = 512;
inline constexpr std::size_t native_access_row_capacity = 32;
inline constexpr std::size_t native_access_overlap_capacity = 128;
inline constexpr std::size_t native_access_comparison_capacity = 8192;
inline constexpr std::size_t native_worker_write_capacity = 128;
// Copied observations only. The runtime owner supplies its actual native plan.
std::string native_system_display_name(flecs::world& world, ecs_entity_t id);
Json native_system_access(flecs::world& world, const ecs_query_t* query, std::size_t& remaining);
Json native_system_access_overlaps(const Json& entries);
Json native_system_worker_access(const Json& entries, unsigned workers);
// Validate optional copied access data before native UI tables are opened.
void validate_native_system_access(const Json& entries, const Json* overlaps);
void validate_native_system_worker_access(const Json& entries, const Json& observation,
                                          unsigned workers);
} // namespace forge::detail
