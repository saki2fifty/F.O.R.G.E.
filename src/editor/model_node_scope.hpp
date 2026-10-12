#pragma once
#include <forge/model_asset.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
namespace forge {
// A disposable picker policy for built-in references whose runtime contract
// requires this holder's model. Custom C++ references retain their own semantics.
inline std::optional<AssetId> model_node_scope(const nlohmann::json& components,
                                               std::string_view component,
                                               const nlohmann::json& field) {
    if ((component != "forge.animator" && component != "forge.two_bone_ik" &&
         component != "forge.aim_ik") ||
        field.value("asset_type", std::string{}) != ModelNodeAsset::type)
        return {};
    if (!components.contains("forge.model_source") ||
        !components.at("forge.model_source").contains("model") ||
        components.at("forge.model_source").at("model").is_null())
        return AssetId{};
    return components.at("forge.model_source").at("model").get<AssetId>();
}
} // namespace forge
