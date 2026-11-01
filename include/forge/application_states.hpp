#pragma once
#include <cstdint>
#include <forge/application_state_definition.hpp>
#include <forge/application_state_ecs.hpp>
#include <forge/identity.hpp>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
struct ApplicationStateObservation {
    std::string domain, label, active, requested;
    std::vector<std::string> membership;
    std::uint64_t revision = 0;
    bool computed = false;
};

// Registration and semantic entities may be obtained during project schema setup,
// before configuration. Queries remain valid when the definition is installed.
void register_application_states(flecs::world&);
// Once per World, outside iteration/defer. Invalid definitions do not install.
void configure_application_states(flecs::world&, const ApplicationStatesDefinition&,
                                  AssetId source = {});
// Stage-safe after installation. Reads immutable definitions, writes a deferred
// native relationship only. Computed/inactive/unknown domains reject requests.
void request_application_state(flecs::world&, std::string_view domain, std::string_view state,
                               std::optional<std::uint64_t> expected_revision = {});
// Runtime creation owner only: bind an admitted root to an active state/ancestor.
// Immutable once bound; same binding is idempotent. Does not advance state/time.
void bind_application_state_lifetime(flecs::world&, flecs::entity_t root, std::string_view domain,
                                     std::string_view state,
                                     std::optional<std::uint64_t> expected_revision = {});
// Copied semantic binding, or null. Never exports a native handle.
nlohmann::json application_state_lifetime(const flecs::world&, flecs::entity_t root);
// Host composition only. Called outside native event/defer after structural
// retirement and before user lifecycle notifications. Returns the prior binding
// so RuntimeSimulation follows its existing active-pipeline lifetime.
std::function<void()> exchange_application_state_retirement_handler(flecs::world&,
                                                                    std::function<void()>);
// Copied host observations. Choices are optional to keep routine status small.
nlohmann::json application_states_status(const flecs::world&, bool choices = false);
// Owner-thread command, shared by host/Play/UI adapters. No clock advancement.
nlohmann::json control_application_states(flecs::world&, const nlohmann::json&);
// Owner boundary only. Publishes all changes before synchronous custom events.
// Observer requests are retained for the next boundary; recursive advance rejects.
bool advance_application_states(flecs::world&);
// Idempotent startup boundary. Does not consume handler requests after initialization.
bool initialize_application_states(flecs::world&);
// Private, exact-definition recovery. Captures semantic keys/revisions/pending
// requests, never native IDs. Restore is before initialization and emits no events.
nlohmann::json checkpoint_application_states(const flecs::world&);
void restore_application_states(flecs::world&, const nlohmann::json&);
// Owner-thread observation; all strings are independent copies.
std::vector<ApplicationStateObservation> observe_application_states(const flecs::world&);
} // namespace forge
