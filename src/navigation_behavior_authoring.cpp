#include "navigation_behavior_authoring.hpp"
#include "asset_single_source_importer.hpp"
#include "state_graph_layout.hpp"
#include <algorithm>

namespace forge {
std::uint32_t add_navigation_behavior_state(nlohmann::json& source) {
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (id > UINT32_MAX || source.at("states").size() >= 32)
        throw std::runtime_error("Behavior state or identity capacity reached");
    source["states"].push_back({{"id", id},
                                {"name", "State " + std::to_string(id)},
                                {"action", {{"kind", "wait"}}},
                                {"transitions", nlohmann::json::array()}});
    source["next_id"] = id + 1;
    if (source.at("entry") == 0)
        source["entry"] = id;
    return std::uint32_t(id);
}
std::uint32_t connect_navigation_behavior_states(nlohmann::json& source, std::uint32_t from,
                                                 std::uint32_t to) {
    auto& states = source.at("states");
    auto at = std::find_if(states.begin(), states.end(),
                           [&](const auto& s) { return s.at("id") == from; });
    const auto target =
        std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.at("id") == to; });
    std::size_t total = 0;
    for (const auto& s : states)
        total += s.at("transitions").size();
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (at == states.end() || target == states.end())
        throw std::runtime_error("Behavior transition state is missing");
    if (id > UINT32_MAX || at->at("transitions").size() >= 8 || total >= 128)
        throw std::runtime_error("Behavior transition or identity capacity reached");
    (*at)["transitions"].push_back(
        {{"id", id}, {"to", to}, {"condition", {{"kind", "elapsed"}, {"seconds", 1}}}});
    source["next_id"] = id + 1;
    return std::uint32_t(id);
}
void remove_navigation_behavior_state(nlohmann::json& source, std::uint32_t id) {
    auto& states = source.at("states");
    const auto at =
        std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.at("id") == id; });
    if (at == states.end())
        throw std::runtime_error("Behavior state is missing");
    states.erase(at);
    for (auto& s : states) {
        auto& edges = s.at("transitions");
        for (auto e = edges.begin(); e != edges.end();)
            e = e->at("to") == id ? edges.erase(e) : std::next(e);
    }
    asset_detail::remove_state_graph_position(source, id, navigation_behavior_layout_format);
    if (source.at("entry") == id)
        source["entry"] = states.empty() ? nlohmann::json(0) : states.front().at("id");
}
namespace {
const SingleSourceImportProfile<NavigationBehaviorSource> profile{
    .importer = "forge.navigation.behavior",
    .revision = "forge.navigation.behavior.v1",
    .label = "Navigation Behavior",
    .description = "Cook bounded patrol, wait and target-chase state graphs.",
    .suffix = ".behavior.json",
    .format = "forge.navigation-behavior",
    .asset_type = NavigationBehaviorAsset::type,
    .file = "behavior.json",
    .evidence_schema = "forge.navigation-behavior.single.v1",
    .metadata_key = "forge.navigation_behavior",
    .source_limit = navigation_behavior_byte_limit,
    .cooked_limit = navigation_behavior_cooked_byte_limit,
    .parse_cooked = NavigationBehaviorSource::parse_cooked};
} // namespace
std::shared_ptr<const AssetImporterRegistry> navigation_behavior_import_registry() {
    return single_source_import_registry(profile);
}
void prepare_navigation_behavior_publication(AssetPublicationCandidate& candidate,
                                             const AssetImportPlan& plan) {
    prepare_single_source_publication(candidate, plan, profile);
}
} // namespace forge
