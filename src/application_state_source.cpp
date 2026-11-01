#include "bounded_json.hpp"
#include <forge/application_state_source.hpp>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
ApplicationStateCondition condition(const Json& value) {
    require(value.is_object(), "Application state condition must be an object");
    return {value.at("domain").get<std::string>(), value.at("state").get<std::string>()};
}
ApplicationStatesDefinition checked_definition(const Json& document) {
    const auto encoded = document.dump();
    require(asset_detail::parse_bounded_json(std::as_bytes(std::span(encoded)),
                                             application_states_byte_limit, 24000, 10) == document,
            "Application states must survive save and reload without value changes");
    require(document.is_object() && document.at("format") == "forge.application-states" &&
                document.at("version").is_number_integer() && document.at("version") == 1 &&
                bool(document.at("asset_id").get<AssetId>()),
            "Unsupported application states source or missing identity");
    const auto& rows = document.at("domains");
    require(rows.is_array() && !rows.empty() && rows.size() <= 16,
            "Application states require 1..16 domains");
    ApplicationStatesDefinition definition;
    for (const auto& row : rows) {
        require(row.is_object(), "Application state domain must be an object");
        ApplicationStateDomainDefinition domain;
        domain.key = row.at("key").get<std::string>();
        domain.label = row.value("label", domain.key);
        domain.initial = row.at("initial").get<std::string>();
        if (row.contains("computed")) {
            require(row.at("computed").is_boolean(), "Computed state flag must be boolean");
            domain.computed = row.at("computed");
        }
        if (row.contains("enabled_when"))
            domain.enabled_when = condition(row.at("enabled_when"));
        const auto& states = row.at("states");
        require(states.is_array() && !states.empty() && states.size() <= 64,
                "Application state domains require 1..64 states");
        for (const auto& state : states) {
            require(state.is_object(), "Application state must be an object");
            ApplicationStateNode node;
            node.key = state.at("key").get<std::string>();
            node.label = state.value("label", node.key);
            node.parent = state.value("parent", std::string{});
            domain.states.push_back(std::move(node));
        }
        if (row.contains("rules")) {
            const auto& rules = row.at("rules");
            require(rules.is_array() && rules.size() <= 64,
                    "Application state rule count exceeds 64");
            for (const auto& rule : rules) {
                require(rule.is_object(), "Application state rule must be an object");
                ApplicationStateRule output;
                output.state = rule.at("state").get<std::string>();
                const auto& all = rule.at("all");
                require(all.is_array() && !all.empty() && all.size() <= 8,
                        "Application state rules require 1..8 conditions");
                for (const auto& value : all)
                    output.all.push_back(condition(value));
                domain.rules.push_back(std::move(output));
            }
        }
        definition.domains.push_back(std::move(domain));
    }
    definition.validate();
    return definition;
}
} // namespace
ApplicationStatesSource ApplicationStatesSource::create(AssetId id) {
    require(bool(id), "Application states require an asset identity");
    return {{{"format", "forge.application-states"},
             {"version", 1},
             {"asset_id", id},
             {"domains",
              Json::array(
                  {{{"key", "app"},
                    {"label", "Game mode"},
                    {"initial", "menu"},
                    {"states", Json::array({{{"key", "menu"}, {"label", "Menu"}},
                                            {{"key", "playing"}, {"label", "Playing"}},
                                            {{"key", "game_over"}, {"label", "Game over"}}})}}})}}};
}
ApplicationStatesSource ApplicationStatesSource::parse(std::span<const std::byte> bytes) {
    ApplicationStatesSource result{
        asset_detail::parse_bounded_json(bytes, application_states_byte_limit, 24000, 10)};
    result.validate();
    return result;
}
AssetId ApplicationStatesSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void ApplicationStatesSource::validate() const { (void)checked_definition(document); }
ApplicationStatesDefinition ApplicationStatesSource::compiled() const {
    return checked_definition(document);
}
std::vector<std::byte> ApplicationStatesSource::cook() const {
    validate();
    const auto text = document.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
} // namespace forge
