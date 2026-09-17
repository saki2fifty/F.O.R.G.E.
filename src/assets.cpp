#include <algorithm>
#include <cctype>
#include <forge/assets.hpp>
#include <forge/audio_components.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>
#include <forge/schema.hpp>
#include <fstream>
namespace forge {
std::filesystem::path AssetCatalog::project_index(const std::filesystem::path& root) {
    return ProjectPaths(root).resolve("forge.assets.json");
}
AssetCatalog AssetCatalog::open_project(const std::filesystem::path& root) {
    AssetCatalog result(root);
    const auto index = project_index(root);
    if (std::filesystem::exists(index)) {
        if (std::filesystem::file_size(index) > 4 * 1024 * 1024)
            throw std::runtime_error("Asset index exceeds 4 MiB");
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
        if (std::filesystem::file_size(p) > 4 * 1024 * 1024)
            throw std::runtime_error("Asset index exceeds 4 MiB");
        std::ifstream in(p, std::ios::binary);
        if (!in)
            throw std::runtime_error("Cannot read asset index");
        return std::string(std::istreambuf_iterator<char>(in), {});
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
    if (!record.id || record.type.empty() || !record.schema_version ||
        !record.metadata.is_object() || record.metadata.dump().size() > 65536)
        throw std::runtime_error("Invalid asset metadata");
    for (auto dependency : record.dependencies)
        if (!dependency)
            throw std::runtime_error("Empty asset dependency identity");
    record.source = ProjectPaths::normalize(record.source);
    (void)locate(record.source);
    if (records_.contains(record.id))
        throw std::runtime_error("Duplicate asset identity: " + record.id.str());
    for (const auto& [id, old] : records_) {
        (void)id;
        if (ProjectPaths(project_).same_locator(old.source, record.source))
            throw std::runtime_error("Asset source already has a different identity");
    }
    records_.emplace(record.id, std::move(record));
}
void AssetCatalog::replace(AssetRecord record) {
    auto candidate = *this;
    auto it = candidate.records_.find(record.id);
    if (it == candidate.records_.end() || it->second.type != record.type)
        throw std::runtime_error("Asset replacement must preserve identity and type");
    candidate.records_.erase(it);
    candidate.add(std::move(record));
    records_.swap(candidate.records_);
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
    auto record = it->second;
    record.source = source;
    candidate.records_.erase(it);
    candidate.add(record);
    const auto resolved = candidate.resolve(id, record.type);
    if (resolved.state != AssetState::Available)
        throw std::runtime_error(resolved.diagnostic);
    records_.swap(candidate.records_);
}
void AssetCatalog::save(const std::filesystem::path& index) const {
    auto records = Json::array();
    for (const auto& [id, record] : records_) {
        const auto text = record.source.generic_u8string();
        records.push_back({{"id", id},
                           {"type", record.type},
                           {"source", std::string(text.begin(), text.end())},
                           {"schema_version", record.schema_version},
                           {"dependencies", record.dependencies},
                           {"metadata", record.metadata}});
    }
    const auto document = Json{{"version", 1}, {"assets", records}}.dump(2);
    if (document.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Asset index exceeds 4 MiB");
    atomic_write(index, document);
}
void AssetCatalog::load(const std::filesystem::path& index) {
    std::ifstream stream(index);
    const auto doc = core_document_schemas().prepare("asset_index", Json::parse(stream));
    AssetCatalog candidate(project_);
    for (const auto& record : doc.at("assets")) {
        const auto& schema = record.at("schema_version");
        if (!schema.is_number_integer() || schema.get<double>() < 1 ||
            schema.get<double>() > 4294967295.0)
            throw std::runtime_error("Invalid asset schema version");
        candidate.add({record.at("id").get<AssetId>(), record.at("type").get<std::string>(),
                       std::filesystem::u8path(record.at("source").get<std::string>()),
                       record.at("schema_version").get<unsigned>(),
                       record.at("dependencies").get<std::vector<AssetId>>(),
                       record.value("metadata", Json::object())});
    }
    records_.swap(candidate.records_);
}
} // namespace forge
