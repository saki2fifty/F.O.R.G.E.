#pragma once
#include <forge/application_state_key.hpp>
#include <forge/asset_ref.hpp>
#include <stdexcept>
#include <string>

namespace forge {
struct ApplicationStatesAsset {
    static constexpr const char* type = "application_states";
};
// One configured, non-prefab controller per active runtime World.
// Active values are runtime relationships, never authored component fields.
struct ApplicationStates {
    AssetRef<ApplicationStatesAsset> definition;
    bool operator==(const ApplicationStates&) const = default;
};
// Optional execution condition. Subsystems retain their own enabled values and
// lifecycle; this does not pause the clock or destroy the authored entity.
struct ApplicationStateGate {
    bool enabled = false;
    std::string domain = "app", state = "playing";
    void validate() const {
        if (!application_state_key(domain) || !application_state_key(state))
            throw std::runtime_error("State Condition requires valid domain and state keys");
    }
    bool operator==(const ApplicationStateGate&) const = default;
};
} // namespace forge
