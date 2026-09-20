#include "asset_bytes.hpp"
#include <algorithm>
#include <cctype>
#include <forge/assets.hpp>
#include <forge/audio_components.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>
#include <forge/schema.hpp>
#include <fstream>
namespace forge {
namespace {
Json parse_index(std::span<const std::byte> bytes) {
    std::size_t events = 0;
    return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                       reinterpret_cast<const char*>(bytes.data() + bytes.size()),
                       [&](int depth, Json::parse_event_t, Json&) {
                           if (depth > 64 || ++events > 4000000)
                               throw std::runtime_error(
                                   "Asset index exceeds JSON nesting/element limits");
                           return true;
                       });
}
void normalize_record(AssetRecord& record, const ProjectPaths& paths) {
    if (!record.id || record.type.empty() || record.type == "legacy-untyped" ||
        record.type.size() > 256 || !record.schema_version || !record.metadata.is_object())
        throw std::runtime_error("Invalid asset metadata");
    (void)asset_build_digest(
        record.metadata); // Bounded depth/strings and finite values before dump.
    if (record.metadata.dump().size() > 65536)
        throw std::runtime_error("Asset metadata exceeds 64 KiB");
    for (auto dependency : record.dependencies)
        if (!dependency)
            throw std::runtime_error("Empty asset dependency identity");
    record.source = ProjectPaths::normalize(record.source);
    (void)paths.resolve(record.source);
    if (!record.dependency_edges.empty()) {
        std::set<AssetId> targets;
        for (const auto& edge : record.dependency_edges) {
            if (edge.expected_type == "legacy-untyped")
                throw std::runtime_error("Typed dependencies cannot use the legacy untyped marker");
            targets.insert(edge.target);
            if (edge.target == record.id && edge.expected_type != record.type)
                throw std::runtime_error("Self reference has an incompatible asset type");
        }
        if (targets != std::set<AssetId>(record.dependencies.begin(), record.dependencies.end()))
            throw std::runtime_error(
                "Typed dependency targets disagree with compatibility projection");
    }
    for (auto& source : record.source_dependencies) {
        if (source.role == "forge.primary")
            throw std::runtime_error("Primary source dependency is supplied by the asset catalog");
        source.source = ProjectPaths::normalize(source.source);
        (void)paths.resolve(source.source);
    }
    std::sort(record.dependency_edges.begin(), record.dependency_edges.end());
    std::sort(record.source_dependencies.begin(), record.source_dependencies.end());
    if (record.subasset && (!record.subasset->owner || record.subasset->owner == record.id ||
                            record.subasset->key.empty() || record.subasset->key.size() > 200 ||
                            record.subasset->key.find('\0') != std::string::npos))
        throw std::runtime_error("Invalid subasset owner/key for " + record.id.str());
}
void preserve_identity(const AssetRecord& previous, const AssetRecord& candidate) {
    if (previous.type != candidate.type ||
        previous.subasset.has_value() != candidate.subasset.has_value() ||
        (previous.subasset && (previous.subasset->owner != candidate.subasset->owner ||
                               previous.subasset->key != candidate.subasset->key)))
        throw std::runtime_error(
            "Asset replacement must preserve identity, type and subasset ownership");
}
std::vector<AssetDependency> logical_edges(const AssetRecord& record) {
    if (!record.dependency_edges.empty())
        return record.dependency_edges;
    std::vector<AssetDependency> result;
    for (auto target : record.dependencies)
        result.push_back({target, "legacy-untyped", AssetDependencyKind::Runtime, "legacy", {}});
    return result;
}
std::vector<AssetSourceDependency> source_edges(const AssetRecord& record) {
    auto result = record.source_dependencies;
    result.push_back({record.source, "forge.primary", {}});
    return result;
}
} // namespace
std::filesystem::path AssetCatalog::project_index(const std::filesystem::path& root) {
    return ProjectPaths(root).resolve("forge.assets.json");
}
AssetCatalog AssetCatalog::open_project(const std::filesystem::path& root) {
    AssetCatalog result(root);
    const auto index = project_index(root);
    if (std::filesystem::exists(index)) {
        if (std::filesystem::file_size(index) > max_asset_index_bytes)
            throw std::runtime_error("Asset index exceeds 64 MiB");
        result.load(index);
    }
    return result;
}
AssetRecord AssetCatalog::register_audio_clip(const std::filesystem::path& root,
                                              const std::filesystem::path& source) {
    ProjectPaths paths(root);
    const auto locator = ProjectPaths::normalize(source);
    const auto file = paths.resolve(locator);
    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".wav" || !std::filesystem::is_regular_file(file))
        throw std::runtime_error("Select an existing project-relative WAV file");
    const auto index = project_index(root);
    auto read = [](const auto& p) {
        if (!std::filesystem::exists(p))
            return std::string{};
        const auto bytes = asset_detail::read_bytes(p, max_asset_index_bytes);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto baseline = read(index);
    auto catalog = open_project(root);
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        if (paths.same_locator(record.source, locator)) {
            if (record.type != AudioClipAsset::type)
                throw std::runtime_error("Source already registered with another asset type");
            return record;
        }
    }
    AssetRecord record{AssetId::generate(), AudioClipAsset::type, locator, 1, {}};
    catalog.add(record);
    if (read(index) != baseline)
        throw std::runtime_error("Asset index changed externally; refresh and retry");
    catalog.save(index);
    return record;
}
AssetCatalog::AssetCatalog(std::filesystem::path project)
    : project_(std::filesystem::weakly_canonical(project)) {}
std::filesystem::path AssetCatalog::locate(const std::filesystem::path& source) const {
    return ProjectPaths(project_).resolve(source);
}
void AssetCatalog::add(AssetRecord record) {
    ProjectPaths paths(project_);
    normalize_record(record, paths);
    if (records_.contains(record.id))
        throw std::runtime_error("Duplicate asset identity: " + record.id.str());
    if (record.subasset || !members(record.id, true).empty()) {
        std::vector<AssetRecord> candidate;
        for (const auto& [id, existing] : records_) {
            (void)id;
            candidate.push_back(existing);
        }
        candidate.push_back(std::move(record));
        replace_all(std::move(candidate));
        return;
    }
    for (const auto& [id, old] : records_) {
        (void)id;
        if (paths.same_locator(old.source, record.source))
            throw std::runtime_error("Asset source already has a different identity");
    }
    for (const auto& edge : record.dependency_edges) {
        const auto found = records_.find(edge.target);
        if (found != records_.end() && found->second.type != edge.expected_type)
            throw std::runtime_error("Asset dependency type does not match registered target");
    }
    // Check incoming edges too: previously missing targets cannot be realized with
    // an incompatible type merely because their consumers were registered first.
    for (auto consumer : graph_.referrers(record.id))
        for (const auto& edge : graph_.dependencies(consumer))
            if (edge.target == record.id && edge.expected_type != "legacy-untyped" &&
                edge.expected_type != record.type)
                throw std::runtime_error(
                    "Asset type conflicts with existing dependency expectations");
    auto candidate_graph = graph_;
    candidate_graph.replace(record.id, logical_edges(record));
    candidate_graph.replace_sources(record.id, source_edges(record));
    records_.emplace(record.id, std::move(record));
    graph_ = std::move(candidate_graph);
}
void AssetCatalog::replace(AssetRecord record) {
    auto candidate = *this;
    auto it = candidate.records_.find(record.id);
    if (it == candidate.records_.end() || it->second.type != record.type)
        throw std::runtime_error("Asset replacement must preserve identity and type");
    preserve_identity(it->second, record);
    candidate.records_.erase(it);
    candidate.add(std::move(record));
    records_.swap(candidate.records_);
    std::swap(graph_, candidate.graph_);
}
void AssetCatalog::replace_all(std::vector<AssetRecord> records) {
    if (records.size() > 100000)
        throw std::runtime_error("Asset catalog exceeds 100000 records");
    AssetCatalog candidate(project_);
    ProjectPaths paths(project_);
    std::map<std::filesystem::path, AssetId, ProjectLocatorLess> locators;
    std::map<std::string, AssetId> file_identities;
    std::map<AssetId, std::filesystem::path> resolved;
    std::map<std::pair<AssetId, std::string>, AssetId> member_keys;
    auto graph_records = Json::array();
    for (auto& record : records) {
        normalize_record(record, paths);
        if (const auto previous = records_.find(record.id); previous != records_.end())
            preserve_identity(previous->second, record);
        const auto absolute = paths.resolve(record.source);
        const auto family = record.subasset ? record.subasset->owner : record.id;
        const auto [location, fresh] = locators.emplace(absolute, family);
        if (!fresh && location->second != family)
            throw std::runtime_error("Asset source already has a different identity");
        if (fresh && std::filesystem::exists(absolute)) {
            const auto [file, unique] =
                file_identities.emplace(paths.file_identity(record.source), family);
            if (!unique && file->second != family)
                throw std::runtime_error("Asset sources alias the same filesystem object");
        }
        if (record.subasset &&
            !member_keys.emplace(std::make_pair(family, record.subasset->key), record.id).second)
            throw std::runtime_error(
                "Subasset mapping key already has an identity (including tombstones)");
        resolved.emplace(record.id, absolute);
        if (!candidate.records_.emplace(record.id, std::move(record)).second)
            throw std::runtime_error("Duplicate asset identity");
    }
    std::map<AssetId, std::vector<AssetDependency>> member_edges;
    const ProjectLocatorLess less;
    for (const auto& [id, record] : candidate.records_) {
        if (!record.subasset)
            continue;
        const auto& member = *record.subasset;
        const auto parent = candidate.records_.find(member.owner);
        if (parent == candidate.records_.end() || parent->second.subasset)
            throw std::runtime_error("Subasset owner must be a registered root asset container: " +
                                     id.str() + " -> " + member.owner.str());
        const auto& source = resolved.at(id);
        const auto& parent_source = resolved.at(member.owner);
        if (less(source, parent_source) || less(parent_source, source))
            throw std::runtime_error(
                "Subasset must share its container's canonical source locator: " + id.str());
        if (!member.removed)
            member_edges[member.owner].push_back({id,
                                                  record.type,
                                                  AssetDependencyKind::Subasset,
                                                  "forge.subasset:" + member.key,
                                                  {}});
    }
    for (const auto& [id, record] : candidate.records_) {
        auto edges = logical_edges(record);
        const auto& children = member_edges[id];
        edges.insert(edges.end(), children.begin(), children.end());
        graph_records.push_back(
            {{"consumer", id}, {"edges", edges}, {"sources", source_edges(record)}});
    }
    candidate.graph_.restore({{"version", 2}, {"records", std::move(graph_records)}});
    for (const auto& [consumer, record] : candidate.records_) {
        (void)record;
        for (const auto& edge : candidate.graph_.dependencies(consumer)) {
            const auto target = candidate.records_.find(edge.target);
            if (target != candidate.records_.end() && edge.expected_type != "legacy-untyped" &&
                target->second.type != edge.expected_type)
                throw std::runtime_error("Asset dependency type does not match registered target");
        }
    }
    records_.swap(candidate.records_);
    std::swap(graph_, candidate.graph_);
}
std::vector<AssetId> AssetCatalog::members(AssetId owner, bool include_removed) const {
    std::vector<AssetId> result;
    for (const auto& [id, record] : records_)
        if (record.subasset && record.subasset->owner == owner &&
            (include_removed || !record.subasset->removed))
            result.push_back(id);
    return result;
}
void AssetCatalog::set_dependencies(AssetId consumer, std::vector<AssetDependency> edges) {
    const auto it = records_.find(consumer);
    if (it == records_.end())
        throw std::runtime_error("Unknown asset dependency consumer");
    auto record = it->second;
    std::set<AssetId> targets;
    for (const auto& edge : edges)
        targets.insert(edge.target);
    record.dependencies.assign(targets.begin(), targets.end());
    record.dependency_edges = std::move(edges);
    replace(std::move(record));
}
void AssetCatalog::set_source_dependencies(AssetId consumer,
                                           std::vector<AssetSourceDependency> sources) {
    const auto it = records_.find(consumer);
    if (it == records_.end())
        throw std::runtime_error("Unknown source dependency consumer");
    auto record = it->second;
    record.source_dependencies = std::move(sources);
    replace(std::move(record));
}
AssetRecord AssetCatalog::add_scene(const std::filesystem::path& source) {
    std::ifstream stream(locate(source));
    const auto doc = Json::parse(stream);
    Scene::validate_document(doc);
    if (doc.at("version") != 2 && doc.at("version") != 3 && doc.at("version") != 4)
        throw std::runtime_error("Scene must be migrated before catalog registration");
    AssetRecord record{doc.at("asset_id").get<AssetId>(),
                       SceneAsset::type,
                       ProjectPaths::normalize(source),
                       doc.at("version").get<unsigned>(),
                       {}};
    add(record);
    return record;
}
AssetResolution AssetCatalog::resolve(AssetId id, const std::string& expected_type) const {
    const auto it = records_.find(id);
    if (it == records_.end())
        return {AssetState::Unresolved, {}, "Asset identity is not registered"};
    const auto& record = it->second;
    if (record.type != expected_type)
        return {AssetState::Incompatible, record,
                "Asset type mismatch: expected " + expected_type + ", found " + record.type};
    if (record.subasset && record.subasset->removed)
        return {AssetState::Removed, record,
                "Subasset was removed from its source; identity is retained as a tombstone"};
    try {
        const auto path = locate(record.source);
        if (!std::filesystem::is_regular_file(path))
            return {AssetState::Missing, record, "Asset source is missing"};
        if (record.type == SceneAsset::type) {
            std::ifstream stream(path);
            const auto doc = Json::parse(stream);
            Scene::validate_document(doc);
            if (doc.at("version") != record.schema_version ||
                doc.at("asset_id").get<AssetId>() != id)
                return {AssetState::Incompatible, record,
                        "Scene identity or schema does not match metadata"};
        } else if (record.type == PrefabAsset::type) {
            std::ifstream stream(path);
            const PrefabDocument doc(Json::parse(stream));
            if (record.schema_version != 1 || doc.asset() != id)
                return {AssetState::Incompatible, record,
                        "Prefab identity/schema does not match metadata"};
        } else if (record.schema_version != 1) {
            return {AssetState::Incompatible, record, "Unsupported asset metadata schema"};
        }
        return {AssetState::Available, record, {}};
    } catch (const std::exception& e) {
        return {AssetState::Incompatible, record, e.what()};
    }
}
void AssetCatalog::relocate(AssetId id, const std::filesystem::path& source) {
    auto candidate = *this;
    const auto it = candidate.records_.find(id);
    if (it == candidate.records_.end())
        throw std::runtime_error("Unknown asset identity");
    if (it->second.subasset)
        throw std::runtime_error(
            "Move the source container rather than an individual imported subasset");
    const auto type = it->second.type;
    std::vector<AssetRecord> records;
    for (const auto& [key, existing] : records_) {
        auto record = existing;
        if (key == id || (record.subasset && record.subasset->owner == id))
            record.source = source;
        records.push_back(std::move(record));
    }
    candidate.replace_all(std::move(records));
    const auto resolved = candidate.resolve(id, type);
    if (resolved.state != AssetState::Available)
        throw std::runtime_error(resolved.diagnostic);
    records_.swap(candidate.records_);
    std::swap(graph_, candidate.graph_);
}
void AssetCatalog::save(const std::filesystem::path& index) const {
    auto records = Json::array();
    for (const auto& [id, record] : records_) {
        records.push_back({{"id", id},
                           {"type", record.type},
                           {"source", path_utf8(record.source)},
                           {"schema_version", record.schema_version},
                           {"dependencies", record.dependencies},
                           {"metadata", record.metadata},
                           {"dependency_edges", record.dependency_edges},
                           {"source_dependencies", record.source_dependencies}});
        if (record.subasset)
            records.back()["subasset"] = {{"owner", record.subasset->owner},
                                          {"key", record.subasset->key},
                                          {"removed", record.subasset->removed}};
    }
    const auto document = Json{{"version", 2}, {"assets", records}}.dump(2);
    if (document.size() > max_asset_index_bytes)
        throw std::runtime_error("Asset index exceeds 64 MiB");
    if (std::filesystem::exists(index)) {
        const auto bytes = asset_detail::read_bytes(index, max_asset_index_bytes);
        const std::string original(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto previous = parse_index(bytes);
        if (previous.at("version") == 1) {
            auto backup = index;
            backup += ".v1.backup";
            if (std::filesystem::exists(backup)) {
                if (asset_detail::read_bytes(backup, max_asset_index_bytes) != bytes)
                    throw std::runtime_error(
                        "Asset index migration backup conflicts with the current v1 source");
            } else {
                atomic_write(backup, original);
            }
        } else if (previous.at("version") != 2) {
            throw std::runtime_error("Cannot overwrite an unsupported asset index version");
        }
    }
    atomic_write(index, document);
}
void AssetCatalog::load(const std::filesystem::path& index) {
    const auto bytes = asset_detail::read_bytes(index, max_asset_index_bytes);
    const auto parsed = parse_index(bytes);
    const auto doc = core_document_schemas().prepare("asset_index", parsed);
    std::vector<AssetRecord> records;
    for (const auto& record : doc.at("assets")) {
        const auto& schema = record.at("schema_version");
        if (!schema.is_number_integer() || schema.get<double>() < 1 ||
            schema.get<double>() > 4294967295.0)
            throw std::runtime_error("Invalid asset schema version");
        auto edges = record.value("dependency_edges", std::vector<AssetDependency>{});
        auto sources = record.value("source_dependencies", std::vector<AssetSourceDependency>{});
        std::optional<AssetSubasset> subasset;
        if (record.contains("subasset")) {
            const auto& value = record.at("subasset");
            subasset =
                AssetSubasset{value.at("owner").get<AssetId>(), value.at("key").get<std::string>(),
                              value.value("removed", false)};
        }
        records.push_back({record.at("id").get<AssetId>(), record.at("type").get<std::string>(),
                           std::filesystem::u8path(record.at("source").get<std::string>()),
                           record.at("schema_version").get<unsigned>(),
                           record.at("dependencies").get<std::vector<AssetId>>(),
                           record.value("metadata", Json::object()), std::move(edges),
                           std::move(sources), std::move(subasset)});
    }
    replace_all(std::move(records));
}
} // namespace forge
