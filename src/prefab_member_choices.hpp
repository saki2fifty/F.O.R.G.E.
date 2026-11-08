#pragma once
#include "prefab_composition.hpp"

namespace forge::detail {
inline std::string prefab_member_choice_name(const Json& member) {
    const auto& name = member.at("name").get_ref<const std::string&>();
    auto size = std::min<std::size_t>(name.size(), 120);
    while (size && size < name.size() && (static_cast<unsigned char>(name[size]) & 0xc0) == 0x80)
        --size;
    return name.substr(0, size) + (size < name.size() ? "…" : "");
}
inline std::string prefab_member_choice_label(const Json& member) {
    return prefab_member_choice_name(member) + " [" +
           member.at("id").get<std::string>().substr(0, 8) + "]";
}
struct PrefabMemberChoice {
    PrefabMemberPath reference;
    std::string label;
};
// Disposable authoring projection, shared by source controls and discovery.
// Inputs are admitted complete definitions; raw sources retain all authority.
inline std::vector<PrefabMemberChoice> prefab_member_choices(const Json& source,
                                                             const PrefabSources& definitions) {
    const auto asset = source.at("asset_id").get<AssetId>();
    std::vector<PrefabMemberChoice> choices;
    std::vector<PrefabInstanceStep> path;
    std::set<AssetId> visiting;
    std::function<void(const Json&, const std::string&)> visit = [&](const Json& definition,
                                                                     const std::string& prefix) {
        const auto current = definition.at("asset_id").get<AssetId>();
        if (PrefabDocument::is_variant(definition) || !visiting.insert(current).second ||
            path.size() > 16)
            throw std::runtime_error("Resolve prefab composition before choosing members");
        for (const auto& member : definition.at("members")) {
            if (choices.size() >= 4096)
                throw std::runtime_error("Prefab member choices exceed4096 expanded rows");
            const auto id = member.at("id").get<PrefabMemberId>();
            const auto name = prefix + prefab_member_choice_label(member);
            choices.push_back({{asset, path, id}, name});
            if (const auto child = nested_prefab_asset(member, definition.at("version"))) {
                const auto found = definitions.find(*child);
                if (found == definitions.end())
                    throw std::runtime_error("Missing nested prefab member choices");
                path.push_back({id, *child});
                visit(found->second, name + " / ");
                path.pop_back();
            }
        }
        visiting.erase(current);
    };
    visit(source, "");
    return choices;
}
inline std::string prefab_member_preview(const Json& source, const PrefabSources& definitions,
                                         const PrefabMemberPath& path) {
    const Json* selected = &source;
    std::string prefix;
    for (const auto& step : path.instances) {
        const Json* mount = nullptr;
        for (const auto& member : selected->at("members"))
            if (member.at("id") == Json(step.mount)) {
                mount = &member;
                break;
            }
        if (!mount || nested_prefab_asset(*mount, selected->at("version")) != step.asset ||
            !definitions.contains(step.asset))
            return prefix + "Missing nested occurrence [" + step.mount.str().substr(0, 8) +
                   "] / member [" + path.member.str().substr(0, 8) + "]";
        prefix += prefab_member_choice_label(*mount) + " / ";
        selected = &definitions.at(step.asset);
    }
    for (const auto& member : selected->at("members"))
        if (member.at("id") == Json(path.member))
            return prefix + prefab_member_choice_label(member);
    return prefix + "Missing member [" + path.member.str().substr(0, 8) + "]";
}
} // namespace forge::detail
