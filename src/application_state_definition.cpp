#include <algorithm>
#include <forge/application_state_definition.hpp>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace forge {
std::size_t ApplicationStatesDefinition::resident_bytes() const {
    std::size_t result =
        sizeof(*this) + domains.capacity() * sizeof(ApplicationStateDomainDefinition);
    const auto condition_bytes = [](const ApplicationStateCondition& condition) {
        return condition.domain.capacity() + condition.state.capacity();
    };
    for (const auto& domain : domains) {
        result += domain.key.capacity() + domain.label.capacity() + domain.initial.capacity() +
                  domain.states.capacity() * sizeof(ApplicationStateNode) +
                  domain.rules.capacity() * sizeof(ApplicationStateRule);
        if (domain.enabled_when)
            result += condition_bytes(*domain.enabled_when);
        for (const auto& state : domain.states)
            result += state.key.capacity() + state.label.capacity() + state.parent.capacity();
        for (const auto& rule : domain.rules) {
            result +=
                rule.state.capacity() + rule.all.capacity() * sizeof(ApplicationStateCondition);
            for (const auto& condition : rule.all)
                result += condition_bytes(condition);
        }
    }
    return result;
}
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void label(const std::string& value) {
    require(value.size() <= 96 && value.find('\0') == std::string::npos,
            "Application state label exceeds bounds or contains NUL");
    (void)nlohmann::json(value).dump();
}
} // namespace
void ApplicationStatesDefinition::validate() const {
    require(!domains.empty() && domains.size() <= 16, "Application states require 1..16 domains");
    std::map<std::string, std::size_t, std::less<>> indices;
    std::vector<std::set<std::string, std::less<>>> states(domains.size());
    std::size_t total_states = 0, total_rules = 0;
    for (std::size_t i = 0; i < domains.size(); ++i) {
        const auto& domain = domains[i];
        require(application_state_key(domain.key) && indices.emplace(domain.key, i).second,
                "Application state domain keys must be unique bounded identifiers");
        label(domain.label);
        require(!domain.states.empty() && domain.states.size() <= 64 &&
                    (total_states += domain.states.size()) <= 512,
                "Application state count exceeds bounds");
        std::map<std::string, std::string, std::less<>> parents;
        for (const auto& state : domain.states) {
            require(application_state_key(state.key) && states[i].insert(state.key).second,
                    "Application state keys must be unique bounded identifiers");
            label(state.label);
            parents.emplace(state.key, state.parent);
        }
        require(states[i].contains(domain.initial), "Application state initial value is unknown");
        for (const auto& state : domain.states) {
            std::set<std::string> seen;
            for (auto current = state.key; !current.empty(); current = parents.at(current)) {
                require(states[i].contains(current), "Application state parent is unknown");
                require(seen.insert(current).second && seen.size() <= 16,
                        "Application state hierarchy is cyclic or exceeds depth 16");
            }
        }
        require(domain.rules.size() <= 64 && (total_rules += domain.rules.size()) <= 256,
                "Application state rule count exceeds bounds");
        require(domain.computed || domain.rules.empty(),
                "Direct application state domains cannot have computation rules");
        for (const auto& rule : domain.rules) {
            require(states[i].contains(rule.state), "Computed application state output is unknown");
            require(!rule.all.empty() && rule.all.size() <= 8,
                    "Application state rules require 1..8 conditions");
        }
    }
    std::vector<std::vector<std::size_t>> dependencies(domains.size());
    for (std::size_t i = 0; i < domains.size(); ++i) {
        const auto condition = [&](const ApplicationStateCondition& value) {
            const auto domain = indices.find(value.domain);
            require(domain != indices.end() && states[domain->second].contains(value.state),
                    "Application state condition references an unknown domain/state");
            dependencies[i].push_back(domain->second);
        };
        if (domains[i].enabled_when)
            condition(*domains[i].enabled_when);
        for (const auto& rule : domains[i].rules)
            for (const auto& value : rule.all)
                condition(value);
    }
    std::vector<unsigned> marks(domains.size());
    const std::function<void(std::size_t)> visit = [&](std::size_t i) {
        require(marks[i] != 1, "Application state dependency cycle");
        if (marks[i] == 2)
            return;
        marks[i] = 1;
        for (const auto dependency : dependencies[i])
            visit(dependency);
        marks[i] = 2;
    };
    for (std::size_t i = 0; i < domains.size(); ++i)
        visit(i);
}
} // namespace forge
