#pragma once
#include <forge/prefab.hpp>
#include <stdexcept>
#include <string_view>

namespace forge::detail {
template <class Document>
auto owned_prefab_member(Document& source, const std::string& id) -> decltype(&source) {
    for (const auto* rows : PrefabDocument::authored_member_arrays(source)) {
        if (std::string_view(rows) == "overrides")
            continue;
        for (auto& row : source.at(rows))
            if (row.at("id") == id)
                return &row;
    }
    return nullptr;
}
inline Json& prefab_member_intent(Json& source, const std::string& id) {
    if (auto* local = owned_prefab_member(source, id))
        return *local;
    if (!PrefabDocument::is_variant(source))
        throw std::runtime_error("Choose an existing prefab source member");
    for (auto& row : source["overrides"])
        if (row.at("id") == id)
            return row;
    source["overrides"].push_back({{"id", id}, {"components", Json::object()}});
    return source["overrides"].back();
}
inline void prune_prefab_member_intent(Json& source, const std::string& id) {
    if (!PrefabDocument::is_variant(source))
        return;
    auto& rows = source["overrides"];
    for (auto at = rows.begin(); at != rows.end(); ++at)
        if (at->at("id") == id) {
            if (at->contains("property_overrides") && at->at("property_overrides").empty())
                at->erase("property_overrides");
            if (source.at("version").get<unsigned>() >= 5 &&
                at->contains("suppressed_components") && at->at("suppressed_components").empty())
                at->erase("suppressed_components");
            if (at->size() == 2 && at->at("components").empty())
                rows.erase(at);
            return;
        }
}
} // namespace forge::detail
