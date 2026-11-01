#pragma once
#include "runtime_entity_creation.hpp"
#include <cstdint>
#include <forge/application_states.hpp>
#include <forge/world.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace forge {
// Both Play transports preserve their normal session/sequence gates. This adapter
// adds the same World-generation and domain-revision requirement to state controls.
struct ApplicationStateAccess {
    static nlohmann::json request(const nlohmann::json& value, std::uint64_t generation) {
        if (!generation || !value.contains("generation") ||
            !value.at("generation").is_number_unsigned() ||
            value.at("generation").get<std::uint64_t>() != generation)
            throw std::runtime_error("Application state request targets a stale world generation");
        if (!value.contains("expected_revision") ||
            !value.at("expected_revision").is_number_unsigned())
            throw std::runtime_error(
                "Application state request requires the current domain revision");
        nlohmann::json result{{"domain", value.at("domain")},
                              {"state", value.at("state")},
                              {"expected_revision", value.at("expected_revision")}};
        if (value.contains("entity"))
            result["entity"] = value.at("entity");
        return result;
    }
    static nlohmann::json control(WorldContext& context, const nlohmann::json& value,
                                  std::uint64_t generation) {
        auto command = request(value, generation);
        if (command.contains("entity"))
            return detail::control_runtime_entity_scope(context, command);
        return control_application_states(context.world(), command);
    }
};
} // namespace forge
