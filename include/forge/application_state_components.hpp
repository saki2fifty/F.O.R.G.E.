#pragma once
#include <forge/asset_ref.hpp>

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
} // namespace forge
