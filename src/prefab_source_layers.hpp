#pragma once
#include "prefab_references.hpp"
#include <algorithm>
#include <forge/asset_ref.hpp>
#include <forge/prefab.hpp>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace forge::detail {
// Admitted raw source data only. This is a dependency projection, not a source owner.
inline std::vector<AssetId> prefab_inheritance_inputs(const Json& source) {
    std::vector<AssetId> result;
    if (!PrefabDocument::is_variant(source))
        return result;
    result.push_back(source.at("base").get<AssetRef<PrefabAsset>>().id);
    if (source.at("version").get<unsigned>() >= 8)
        for (const auto& value : source.value("mixins", Json::array()))
            result.push_back(value.get<AssetRef<PrefabAsset>>().id);
    return result;
}
// C3 precedence is calculated from high to low priority. The caller applies its
// reverse, so the primary base precedes additional inputs and local intent is last.
// All state is bounded, disposable preparation data for the existing resolver.
inline std::map<AssetId, std::vector<AssetId>>
prefab_inheritance_orders(const PrefabSources& sources) {
    std::map<AssetId, std::vector<AssetId>> orders;
    std::set<AssetId> visiting;
    std::function<const std::vector<AssetId>&(AssetId)> visit =
        [&](AssetId asset) -> const std::vector<AssetId>& {
        if (const auto at = orders.find(asset); at != orders.end())
            return at->second;
        if (!visiting.insert(asset).second || visiting.size() > 17)
            throw std::runtime_error("Invalid Prefab inheritance graph");
        const auto& source = sources.at(asset);
        auto parents = prefab_inheritance_inputs(source);
        std::reverse(parents.begin(), parents.end());
        std::vector<std::vector<AssetId>> pending;
        std::optional<AssetId> family;
        for (const auto parent : parents) {
            const auto& order = visit(parent);
            if ((family && *family != order.back()) ||
                source.at("root") != sources.at(parent).at("root"))
                throw std::runtime_error(
                    "Additional Prefab sources require a common ancestor and root identity");
            family = order.back();
            pending.push_back(order);
        }
        pending.push_back(parents);
        std::vector<AssetId> result{asset};
        while (true) {
            std::optional<AssetId> selected;
            bool remaining = false;
            for (const auto& sequence : pending) {
                if (sequence.empty())
                    continue;
                remaining = true;
                const auto head = sequence.front();
                const bool in_tail =
                    std::any_of(pending.begin(), pending.end(), [&](const auto& other) {
                        return !other.empty() &&
                               std::find(other.begin() + 1, other.end(), head) != other.end();
                    });
                if (!in_tail) {
                    selected = head;
                    break;
                }
            }
            if (!remaining)
                break;
            if (!selected)
                throw std::runtime_error("Conflicting Prefab source precedence");
            if (result.size() == 64)
                throw std::runtime_error("Prefab inheritance exceeds64 source contributors");
            result.push_back(*selected);
            for (auto& sequence : pending)
                if (!sequence.empty() && sequence.front() == *selected)
                    sequence.erase(sequence.begin());
        }
        visiting.erase(asset);
        return orders.emplace(asset, std::move(result)).first->second;
    };
    for (const auto& [asset, source] : sources) {
        (void)source;
        visit(asset);
    }
    return orders;
}
// A declaration can be reached through several paths, but a sibling source
// cannot claim another contributor's dormant identity or address by coincidence.
inline void validate_prefab_source_origins(const std::vector<AssetId>& order,
                                           const PrefabSources& sources,
                                           const std::map<AssetId, std::vector<AssetId>>& orders,
                                           const Json& schema) {
    std::map<std::string, AssetId> declarations;
    for (const auto asset : order) {
        const auto& source = sources.at(asset);
        const auto* key = PrefabDocument::is_variant(source) ? "added_members" : "members";
        if (PrefabDocument::is_variant(source) && source.at("version").get<unsigned>() < 6)
            continue;
        if (!source.contains(key))
            continue;
        for (const auto& row : source.at(key))
            if (!declarations.emplace(row.at("id").get<std::string>(), asset).second)
                throw std::runtime_error(
                    "Prefab source contributors declare the same member identity");
    }
    for (const auto asset : order) {
        const auto& source = sources.at(asset);
        const auto version = source.at("version").get<unsigned>();
        const bool variant = PrefabDocument::is_variant(source);
        const auto& ancestors = orders.at(asset);
        const auto address = [&](const std::string& id) {
            const auto declaration = declarations.find(id);
            if (declaration != declarations.end() &&
                std::find(ancestors.begin(), ancestors.end(), declaration->second) ==
                    ancestors.end())
                throw std::runtime_error(
                    "Prefab source address captures a peer contributor's member");
        };
        for (const auto* key : PrefabDocument::authored_member_arrays(source))
            for (const auto& row : source.at(key)) {
                if (variant && std::string_view(key) == "overrides")
                    address(row.at("id").get<std::string>());
                if (!variant || version >= 6) {
                    if (row.contains("parent"))
                        address(row.at("parent").get<std::string>());
                    if (row.contains("spatial") &&
                        row.at("spatial").value("mode", "") == "explicit")
                        address(row.at("spatial").at("member").get<std::string>());
                }
                if (version >= 3)
                    for (const auto* channel : {"components", "property_overrides"}) {
                        if (!row.contains(channel) ||
                            (!variant && std::string_view(channel) != "components"))
                            continue;
                        auto values = row.at(channel);
                        transform_prefab_component_references(
                            values, schema, [&](const Json& value) {
                                if (const auto reference =
                                        source_member_reference(value, version, asset))
                                    address(reference->instances.empty()
                                                ? reference->member.str()
                                                : reference->instances.front().mount.str());
                                return value;
                            });
                    }
            }
        if (variant && version >= 6)
            for (const auto& row : source.value("member_order", Json::array())) {
                address(row.at("parent").get<std::string>());
                for (const auto& id : row.at("members"))
                    address(id.get<std::string>());
            }
    }
}
} // namespace forge::detail
