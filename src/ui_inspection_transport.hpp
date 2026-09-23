#pragma once
#include <forge/ui_assets.hpp>
namespace forge::ui_inspection_detail {
inline nlohmann::json encode(const UiAssetSnapshot& s) {
    nlohmann::json j{{"format", "forge.ui-inspection"},    {"version", 1},
                     {"project", path_utf8(s.project)},    {"documents", nlohmann::json::array()},
                     {"sources", nlohmann::json::array()}, {"automatic", nlohmann::json::array()}};
    for (const auto& [id, path] : s.documents)
        j["documents"].push_back({{"id", id}, {"source", path_utf8(path)}});
    for (const auto& r : s.sources)
        j["sources"].push_back({{"source", path_utf8(r.source)},
                                {"type", r.type},
                                {"digest", r.digest},
                                {"bytes", r.bytes},
                                {"details", r.details}});
    for (const auto& path : s.automatic_sources)
        j["automatic"].push_back(path_utf8(path));
    return j;
}
inline UiAssetSnapshot decode(const nlohmann::json& j) {
    if (j.at("format") != "forge.ui-inspection" || j.at("version") != 1 ||
        !j.at("sources").is_array() || j.at("sources").size() > 32 ||
        !j.at("documents").is_array() || j.at("documents").size() != 1 ||
        !j.at("automatic").is_array() || j.at("automatic").size() > 32)
        throw std::runtime_error("export.ui.inspection: Invalid worker result");
    UiAssetSnapshot s;
    s.project = std::filesystem::u8path(j.at("project").get<std::string>());
    for (const auto& d : j.at("documents"))
        s.documents.emplace(d.at("id").get<AssetId>(),
                            std::filesystem::u8path(d.at("source").get<std::string>()));
    for (const auto& r : j.at("sources"))
        s.sources.push_back({std::filesystem::u8path(r.at("source").get<std::string>()),
                             r.at("type"), r.at("digest"), r.at("bytes"), r.at("details")});
    for (const auto& p : j.at("automatic"))
        s.automatic_sources.insert(std::filesystem::u8path(p.get<std::string>()));
    return s;
}
} // namespace forge::ui_inspection_detail
