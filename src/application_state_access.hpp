#pragma once
#include <cstdint>
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
        return {{"domain", value.at("domain")},
                {"state", value.at("state")},
                {"expected_revision", value.at("expected_revision")}};
    }
};
} // namespace forge
