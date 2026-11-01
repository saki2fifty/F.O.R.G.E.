#pragma once
#include <cstdint>
#include <flecs.h>
#include <forge/application_state_key.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forge {
// Runtime-owned relationships. Query them; submit changes through the host API.
struct ApplicationStateDomainTag {};
struct ApplicationStateActive {};    // Exclusive; runtime-owned active leaf.
struct ApplicationStateMember {};    // Derived active leaf and its ancestors.
struct ApplicationStateRequested {}; // Exclusive; last merged request wins.
// Explicit lifetime of a runtime-created root; target is a semantic state entity.
// The runtime creation owner validates bindings. Scene/Flecs own destruction.
struct ApplicationStateLifetime {};
struct ApplicationStateEntered {
    std::string domain, state, previous, current;
    std::uint64_t revision = 0;
};
struct ApplicationStateExited {
    std::string domain, state, previous, current;
    std::uint64_t revision = 0;
};
// Schema setup may create semantic placeholders before source admission.
inline flecs::entity application_state_domain(flecs::world& world, std::string_view domain) {
    if (!application_state_key(domain))
        throw std::runtime_error("Invalid application state domain key");
    return world.entity(("::forge::application::domains::" + std::string(domain)).c_str());
}
inline flecs::entity application_state_entity(flecs::world& world, std::string_view domain,
                                              std::string_view state) {
    if (!application_state_key(domain) || !application_state_key(state))
        throw std::runtime_error("Invalid application state domain/state key");
    return world.entity(("::forge::application::domains::" + std::string(domain) +
                         "::states::" + std::string(state))
                            .c_str());
}
// Runtime reads do not create entities or registrations, including in empty worlds.
inline bool application_state_is(flecs::world& world, std::string_view domain,
                                 std::string_view state) {
    if (!application_state_key(domain) || !application_state_key(state))
        return false;
    // C++ type-registration caches can differ across the exact SDK module
    // boundary. Resolve the existing native relation without registering a type.
    const auto relation =
        ecs_lookup_path_w_sep(world.c_ptr(), 0, "::forge.application.Member", "::", "::", false);
    if (!relation)
        return false;
    const auto root = "::forge::application::domains::" + std::string(domain);
    const auto owner = world.lookup(root.c_str());
    const auto target = world.lookup((root + "::states::" + std::string(state)).c_str());
    return owner && target && ecs_has_pair(world.c_ptr(), owner.id(), relation, target.id());
}
inline std::string application_state_current(flecs::world& world, std::string_view domain) {
    if (!application_state_key(domain))
        return {};
    const auto relation =
        ecs_lookup_path_w_sep(world.c_ptr(), 0, "::forge.application.Active", "::", "::", false);
    if (!relation)
        return {};
    const auto owner =
        world.lookup(("::forge::application::domains::" + std::string(domain)).c_str());
    if (!owner)
        return {};
    const auto target = ecs_get_target(world.c_ptr(), owner.id(), relation, 0);
    return target ? std::string(world.entity(target).name().c_str()) : std::string{};
}
} // namespace forge
