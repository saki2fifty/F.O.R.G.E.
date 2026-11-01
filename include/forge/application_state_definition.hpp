#pragma once
#include <cstddef>
#include <forge/application_state_key.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
// Semantic identifiers, scoped to a World. No persistent entity identity.
struct ApplicationStateNode {
    std::string key, label, parent;
    bool operator==(const ApplicationStateNode&) const = default;
};
struct ApplicationStateCondition {
    std::string domain, state;
    bool operator==(const ApplicationStateCondition&) const = default;
};
struct ApplicationStateRule {
    std::string state;
    std::vector<ApplicationStateCondition> all;
    bool operator==(const ApplicationStateRule&) const = default;
};
struct ApplicationStateDomainDefinition {
    std::string key, label, initial;
    std::vector<ApplicationStateNode> states;
    bool computed = false;
    std::optional<ApplicationStateCondition> enabled_when;
    std::vector<ApplicationStateRule> rules;
    bool operator==(const ApplicationStateDomainDefinition&) const = default;
};
struct ApplicationStatesDefinition {
    std::vector<ApplicationStateDomainDefinition> domains;
    // Rejects invalid keys, references, bounds, hierarchy and dependency cycles.
    void validate() const;
    std::size_t resident_bytes() const;
    bool operator==(const ApplicationStatesDefinition&) const = default;
};

} // namespace forge
