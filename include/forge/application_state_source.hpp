#pragma once
#include <cstddef>
#include <forge/application_state_components.hpp>
#include <forge/application_state_definition.hpp>
#include <forge/identity.hpp>
#include <nlohmann/json.hpp>
#include <span>

namespace forge {
inline constexpr std::size_t application_states_byte_limit = 512 * 1024;
struct ApplicationStatesSource {
    nlohmann::json document;
    static ApplicationStatesSource create(AssetId);
    static ApplicationStatesSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    ApplicationStatesDefinition compiled() const;
    std::vector<std::byte> cook() const;
};
} // namespace forge
