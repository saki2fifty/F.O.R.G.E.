#include "asset_bytes.hpp"
#include <algorithm>
#include <cmath>
#include <forge/asset_build.hpp>
#include <stdexcept>

namespace forge {
namespace {
using Json = nlohmann::json;
bool builds(AssetDependencyKind kind) {
    return kind == AssetDependencyKind::Source || kind == AssetDependencyKind::Build ||
           kind == AssetDependencyKind::Subasset;
}
std::string kind_name(AssetDependencyKind kind) {
    switch (kind) {
    case AssetDependencyKind::Source:
        return "source";
    case AssetDependencyKind::Build:
        return "build";
    case AssetDependencyKind::Runtime:
        return "runtime";
    case AssetDependencyKind::Optional:
        return "optional";
    case AssetDependencyKind::Subasset:
        return "subasset";
    }
    throw std::runtime_error("Invalid asset dependency kind");
}
AssetDependencyKind parse_kind(const std::string& name) {
    for (auto kind :
         {AssetDependencyKind::Source, AssetDependencyKind::Build, AssetDependencyKind::Runtime,
          AssetDependencyKind::Optional, AssetDependencyKind::Subasset})
        if (kind_name(kind) == name)
            return kind;
    throw std::runtime_error("Unknown asset dependency kind: " + name);
}
void validate_edge(const AssetDependency& edge) {
    if (!edge.target || edge.expected_type.empty() || edge.expected_type.size() > 256 ||
        edge.role.empty() || edge.role.size() > 256 ||
        (!edge.revision.empty() && !valid_content_digest(edge.revision)))
        throw std::runtime_error("Invalid asset dependency identity/type/role/revision");
    (void)kind_name(edge.kind);
}
Json edge_document(const AssetDependency& edge) {
    validate_edge(edge);
    return {{"target", edge.target},
            {"type", edge.expected_type},
            {"kind", kind_name(edge.kind)},
            {"role", edge.role},
            {"revision", edge.revision}};
}
void validate_sources(std::vector<AssetSourceDependency>& sources) {
    if (sources.size() > 65536)
        throw std::runtime_error("Asset source dependency limit exceeded");
    std::map<std::filesystem::path, std::set<std::string>, ProjectLocatorLess> roles;
    for (auto& source : sources) {
        source.source = ProjectPaths::normalize(source.source);
        if (path_utf8(source.source).size() > 4096 || source.role.empty() ||
            source.role.size() > 256 ||
            (!source.revision.empty() && !valid_content_digest(source.revision)))
            throw std::runtime_error("Invalid source dependency locator/role/revision");
        if (!roles[source.source].insert(source.role).second)
            throw std::runtime_error("Duplicate source dependency role");
    }
    std::sort(sources.begin(), sources.end());
}
void validate_json(const Json& value, unsigned depth, std::size_t& count, std::size_t& bytes) {
    if (++count > 65536 || depth > 64)
        throw std::runtime_error("Asset build data exceeds nesting/element limits");
    if (value.is_discarded() || value.is_binary() ||
        (value.is_number_float() && !std::isfinite(value.get<double>())))
        throw std::runtime_error("Asset build data contains an unsupported value");
    if (value.is_string() && value.get_ref<const std::string&>().size() > 65536)
        throw std::runtime_error("Asset build string exceeds 64 KiB");
    if (value.is_string())
        bytes += value.get_ref<const std::string&>().size();
    if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            (void)child;
            if (key.size() > 65536)
                throw std::runtime_error("Asset build key exceeds 64 KiB");
            bytes += key.size();
            if (bytes > 4 * 1024 * 1024)
                throw std::runtime_error("Asset build strings exceed 4 MiB");
        }
    }
    if (bytes > 4 * 1024 * 1024)
        throw std::runtime_error("Asset build strings exceed 4 MiB");
    if (value.is_structured())
        for (const auto& child : value)
            validate_json(child, depth + 1, count, bytes);
}
} // namespace

void to_json(Json& value, const AssetDependency& edge) { value = edge_document(edge); }
void from_json(const Json& value, AssetDependency& edge) {
    AssetDependency candidate{value.at("target").get<AssetId>(), value.at("type"),
                              parse_kind(value.at("kind")), value.at("role"), value.at("revision")};
    validate_edge(candidate);
    edge = std::move(candidate);
}
void to_json(Json& value, const AssetSourceDependency& source) {
    value = {
        {"source", path_utf8(source.source)}, {"role", source.role}, {"revision", source.revision}};
}
void from_json(const Json& value, AssetSourceDependency& source) {
    std::vector<AssetSourceDependency> candidate{
        {std::filesystem::u8path(value.at("source").get<std::string>()), value.at("role"),
         value.at("revision")}};
    validate_sources(candidate);
    source = std::move(candidate[0]);
}
std::size_t AssetDependencyGraph::edge_count() const {
    std::size_t result = 0;
    for (const auto& [id, edges] : forward_) {
        (void)id;
        result += edges.size();
    }
    for (const auto& [id, sources] : sources_) {
        (void)id;
        result += sources.size();
    }
    return result;
}

bool valid_content_digest(std::string_view digest) {
    return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string asset_build_digest(const Json& value) {
    std::size_t count = 0, bytes = 0;
    validate_json(value, 0, count, bytes);
    const auto text = value.dump();
    if (text.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Asset build key data exceeds 4 MiB");
    return asset_detail::content_digest(std::as_bytes(std::span(text)));
}
const std::vector<AssetDependency>& AssetDependencyGraph::dependencies(AssetId consumer) const {
    static const std::vector<AssetDependency> empty;
    auto it = forward_.find(consumer);
    return it == forward_.end() ? empty : it->second;
}
const std::vector<AssetSourceDependency>&
AssetDependencyGraph::source_dependencies(AssetId consumer) const {
    static const std::vector<AssetSourceDependency> empty;
    const auto it = sources_.find(consumer);
    return it == sources_.end() ? empty : it->second;
}
std::vector<AssetId>
AssetDependencyGraph::source_referrers(const std::filesystem::path& source) const {
    const auto it = source_reverse_.find(ProjectPaths::normalize(source));
    return it == source_reverse_.end() ? std::vector<AssetId>{}
                                       : std::vector<AssetId>(it->second.begin(), it->second.end());
}
std::vector<AssetId>
AssetDependencyGraph::invalidated_by_source(const std::filesystem::path& source) const {
    auto pending = source_referrers(source);
    std::set<AssetId> seen(pending.begin(), pending.end());
    for (std::size_t at = 0; at < pending.size(); ++at)
        for (auto referrer : referrers(pending[at]))
            if (seen.insert(referrer).second)
                pending.push_back(referrer);
    return {seen.begin(), seen.end()};
}
std::vector<AssetId> AssetDependencyGraph::referrers(AssetId target) const {
    auto it = reverse_.find(target);
    return it == reverse_.end() ? std::vector<AssetId>{}
                                : std::vector<AssetId>(it->second.begin(), it->second.end());
}
std::vector<AssetId> AssetDependencyGraph::invalidated_by(AssetId target) const {
    std::set<AssetId> seen{target};
    std::vector<AssetId> pending{target};
    for (std::size_t at = 0; at < pending.size(); ++at)
        for (auto referrer : referrers(pending[at]))
            if (seen.insert(referrer).second)
                pending.push_back(referrer);
    seen.erase(target);
    return {seen.begin(), seen.end()};
}
std::vector<AssetId> AssetDependencyGraph::build_order(std::span<const AssetId> roots) const {
    if (roots.size() > 100000)
        throw std::runtime_error("Asset build root list exceeds 100000 entries");
    // Iterative DFS keeps adversarially deep imported graphs off the C++ call stack.
    struct Frame {
        AssetId id;
        std::size_t edge = 0;
    };
    std::map<AssetId, unsigned> colors;
    std::vector<AssetId> result;
    std::vector<Frame> stack;
    for (auto root : roots) {
        if (!root)
            throw std::runtime_error("Empty build root identity");
        if (colors[root] == 2)
            continue;
        stack.push_back({root});
        colors[root] = 1;
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& edges = dependencies(frame.id);
            while (frame.edge < edges.size() && !builds(edges[frame.edge].kind))
                ++frame.edge;
            if (frame.edge == edges.size()) {
                colors[frame.id] = 2;
                result.push_back(frame.id);
                stack.pop_back();
                continue;
            }
            const auto target = edges[frame.edge++].target;
            if (colors[target] == 2)
                continue;
            if (colors[target] == 1) {
                std::string path;
                bool started = false;
                for (const auto& item : stack) {
                    started |= item.id == target;
                    if (started)
                        path += item.id.str() + " -> ";
                }
                throw std::runtime_error("Asset build dependency cycle: " + path + target.str());
            }
            if (colors.size() > 100000)
                throw std::runtime_error("Asset dependency traversal exceeds 100000 nodes");
            colors[target] = 1;
            stack.push_back({target});
        }
    }
    return result;
}
void AssetDependencyGraph::replace(AssetId consumer, std::vector<AssetDependency> edges) {
    if (!consumer || edges.size() > 65536)
        throw std::runtime_error("Invalid dependency consumer or edge limit");
    if (!forward_.contains(consumer) && forward_.size() >= 100000)
        throw std::runtime_error("Asset graph exceeds 100000 consumers");
    if (edge_count() - dependencies(consumer).size() + edges.size() > 1000000)
        throw std::runtime_error("Asset graph exceeds edge limit");
    for (const auto& edge : edges)
        validate_edge(edge);
    std::sort(edges.begin(), edges.end());
    std::set<std::tuple<AssetId, AssetDependencyKind, std::string>> unique;
    for (const auto& edge : edges)
        if (!unique.emplace(edge.target, edge.kind, edge.role).second)
            throw std::runtime_error("Duplicate asset dependency edge");
    auto candidate = *this;
    for (const auto& edge : candidate.dependencies(consumer)) {
        auto it = candidate.reverse_.find(edge.target);
        if (it != candidate.reverse_.end()) {
            it->second.erase(consumer);
            if (it->second.empty())
                candidate.reverse_.erase(it);
        }
    }
    candidate.forward_[consumer] = std::move(edges);
    for (const auto& edge : candidate.dependencies(consumer))
        candidate.reverse_[edge.target].insert(consumer);
    const AssetId roots[]{consumer};
    (void)candidate.build_order(roots);
    forward_.swap(candidate.forward_);
    reverse_.swap(candidate.reverse_);
}
void AssetDependencyGraph::replace_sources(AssetId consumer,
                                           std::vector<AssetSourceDependency> sources) {
    if (!consumer || (!forward_.contains(consumer) && forward_.size() >= 100000))
        throw std::runtime_error("Invalid source dependency consumer or graph limit");
    validate_sources(sources);
    if (edge_count() - source_dependencies(consumer).size() + sources.size() > 1000000)
        throw std::runtime_error("Asset graph exceeds edge limit");
    auto candidate = *this;
    for (const auto& source : candidate.source_dependencies(consumer)) {
        auto it = candidate.source_reverse_.find(source.source);
        if (it != candidate.source_reverse_.end()) {
            it->second.erase(consumer);
            if (it->second.empty())
                candidate.source_reverse_.erase(it);
        }
    }
    candidate.forward_.try_emplace(consumer);
    candidate.sources_[consumer] = std::move(sources);
    for (const auto& source : candidate.source_dependencies(consumer))
        candidate.source_reverse_[source.source].insert(consumer);
    forward_.swap(candidate.forward_);
    sources_.swap(candidate.sources_);
    source_reverse_.swap(candidate.source_reverse_);
}
Json AssetDependencyGraph::document() const {
    auto records = Json::array();
    for (const auto& [consumer, edges] : forward_) {
        auto values = Json::array();
        for (const auto& edge : edges)
            values.push_back(edge_document(edge));
        auto sources = Json::array();
        for (const auto& source : source_dependencies(consumer))
            sources.push_back({{"source", path_utf8(source.source)},
                               {"role", source.role},
                               {"revision", source.revision}});
        records.push_back({{"consumer", consumer},
                           {"edges", std::move(values)},
                           {"sources", std::move(sources)}});
    }
    return {{"version", 2}, {"records", std::move(records)}};
}
void AssetDependencyGraph::restore(const Json& document) {
    if ((document.at("version") != 1 && document.at("version") != 2) ||
        !document.at("records").is_array() || document.at("records").size() > 100000)
        throw std::runtime_error("Unsupported asset dependency graph");
    AssetDependencyGraph candidate;
    std::vector<AssetId> roots;
    std::size_t edge_count = 0;
    for (const auto& record : document.at("records")) {
        const auto consumer = record.at("consumer").get<AssetId>();
        if (!consumer || candidate.forward_.contains(consumer) || !record.at("edges").is_array() ||
            record.at("edges").size() > 65536)
            throw std::runtime_error("Duplicate dependency consumer or invalid edge list");
        auto& edges = candidate.forward_[consumer];
        for (const auto& value : record.at("edges")) {
            if (++edge_count > 1000000)
                throw std::runtime_error("Asset graph exceeds edge limit");
            AssetDependency edge{value.at("target").get<AssetId>(), value.at("type"),
                                 parse_kind(value.at("kind")), value.at("role"),
                                 value.at("revision")};
            validate_edge(edge);
            edges.push_back(std::move(edge));
        }
        std::sort(edges.begin(), edges.end());
        std::set<std::tuple<AssetId, AssetDependencyKind, std::string>> unique;
        for (const auto& edge : edges) {
            if (!unique.emplace(edge.target, edge.kind, edge.role).second)
                throw std::runtime_error("Duplicate asset dependency edge");
            candidate.reverse_[edge.target].insert(consumer);
        }
        if (record.contains("sources")) {
            const auto& values = record.at("sources");
            if (!values.is_array() || values.size() > 65536)
                throw std::runtime_error("Invalid source dependency list");
            auto& sources = candidate.sources_[consumer];
            for (const auto& value : values) {
                if (++edge_count > 1000000)
                    throw std::runtime_error("Asset graph exceeds edge limit");
                sources.push_back({std::filesystem::u8path(value.at("source").get<std::string>()),
                                   value.at("role"), value.at("revision")});
            }
            validate_sources(sources);
            for (const auto& source : sources)
                candidate.source_reverse_[source.source].insert(consumer);
        }
        roots.push_back(consumer);
    }
    (void)candidate.build_order(roots);
    forward_.swap(candidate.forward_);
    reverse_.swap(candidate.reverse_);
    sources_.swap(candidate.sources_);
    source_reverse_.swap(candidate.source_reverse_);
}
Json AssetBuildInput::document() const {
    if (!valid_content_digest(source_digest) || importer.empty() || importer_revision.empty() ||
        !settings_version || !settings.is_object() || output_format.empty() || !output_version ||
        platform.empty() || backend.empty() || profile.empty())
        throw std::runtime_error("Incomplete asset build inputs");
    for (const auto& [name, digest] : source_dependencies)
        if (name.empty() || !valid_content_digest(digest))
            throw std::runtime_error("Invalid source dependency digest");
    if (tool_revisions.size() > 64)
        throw std::runtime_error("Too many asset build tool revisions");
    for (const auto& [name, revision] : tool_revisions) {
        if (name.empty() || name.size() > 128 || !valid_content_digest(revision) ||
            !std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '.' || c == '_' || c == '-';
            }))
            throw std::runtime_error("Invalid asset build tool revision");
    }
    auto ordered = dependencies;
    std::sort(ordered.begin(), ordered.end());
    Json revisions = Json::array();
    for (const auto& edge : ordered)
        revisions.push_back(edge_document(edge));
    Json result{{"key_version", tool_revisions.empty() ? 1 : 2},
                {"source", source_digest},
                {"importer", importer},
                {"importer_revision", importer_revision},
                {"settings_version", settings_version},
                {"settings", settings},
                {"source_dependencies", source_dependencies},
                {"dependencies", std::move(revisions)},
                {"output_format", output_format},
                {"output_version", output_version},
                {"platform", platform},
                {"backend", backend},
                {"profile", profile}};
    if (!tool_revisions.empty())
        result["tool_revisions"] = tool_revisions;
    return result;
}
std::string AssetBuildInput::key() const { return asset_build_digest(document()); }
} // namespace forge
