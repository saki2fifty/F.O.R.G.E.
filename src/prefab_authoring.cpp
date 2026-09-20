#include "builtins.hpp"
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
#include <set>
namespace forge {
namespace {
Json read_prefab(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 8 * 1024 * 1024)
        throw std::runtime_error("Prefab source exceeds 8 MiB");
    std::ifstream input(path);
    auto source = Json::parse(input);
    return PrefabDocument(std::move(source)).source;
}
} // namespace
PrefabDocument create_prefab_source(const Scene& scene, const std::string& selected) {
    const auto doc = scene.document(), effective = scene.effective_document();
    const auto root = resolve_legacy_id(doc, selected);
    std::map<std::string, Json> rows, values;
    for (const auto& item : doc.at("entities"))
        rows[item.at("id")] = item;
    for (const auto& item : effective.at("entities"))
        values[item.at("id")] = item;
    if (!rows.contains(root))
        throw std::runtime_error("Select a scene subtree first");
    std::set<std::string> included{root};
    bool changed;
    do {
        changed = false;
        for (const auto& [id, item] : rows)
            if (included.contains(item.value("parent", "")))
                changed |= included.insert(id).second;
    } while (changed);
    std::map<std::string, std::string> ids;
    for (const auto& id : included)
        ids[id] = PrefabMemberId::generate().str();
    Json members = Json::array();
    for (const auto& id : included) {
        auto item = rows.at(id);
        if (item.contains("prefab_instance") || item.contains("prefab_member") ||
            item.contains("base") || item.value("prefab", false))
            throw std::runtime_error(
                "Create Prefab requires an ordinary authored subtree; nested prefabs and legacy "
                "conversion are deferred. Use Duplicate asset for an independent prefab.");
        if (item.value("missing_member", false))
            throw std::runtime_error("Resolve missing prefab members before creating a source");
        item["id"] = ids.at(id);
        for (const auto& type : detail::builtins())
            if (values.at(id)["components"].contains(type.name))
                item["components"][type.name] = values.at(id)["components"].at(type.name);
        for (auto key : {"base", "prefab", "prefab_instance", "prefab_member", "property_overrides",
                         "missing_member", "name_override"})
            item.erase(key);
        if (id == root) {
            item.erase("parent");
            item["spatial"] = {{"mode", "follow_structure"}};
        } else {
            item["parent"] = ids.at(item.at("parent"));
            if (item.value("spatial", Json::object()).value("mode", "") == "explicit") {
                const auto ref = item.at("spatial").at("target").get<EntityRef>();
                if (ref.scene != scene.asset_id() || !ids.contains(ref.entity.str()))
                    throw std::runtime_error("Prefab source cannot capture a spatial target "
                                             "outside the selected subtree");
                item["spatial"].erase("target");
                item["spatial"]["member"] = ids.at(ref.entity.str());
            }
        }
        members.push_back(std::move(item));
    }
    return PrefabDocument({{"format", "forge.prefab"},
                           {"version", 1},
                           {"asset_id", AssetId::generate()},
                           {"revision", std::uint64_t{1}},
                           {"root", ids.at(root)},
                           {"members", std::move(members)}});
}
std::string instantiate_prefab(Scene& scene, AssetId asset) {
    return authoring_command(scene, "prefab.instantiate", {{"asset", asset}}).at("selected");
}
PrefabLibrary::PrefabLibrary(std::filesystem::path project)
    : project_(std::filesystem::weakly_canonical(project)), catalog_(project_) {}
std::filesystem::path PrefabLibrary::locate(const std::filesystem::path& relative) const {
    const auto normalized = ProjectPaths::normalize(relative);
    if (!normalized.filename().string().ends_with(".prefab.json"))
        throw std::runtime_error("Choose a project-relative .prefab.json filename");
    for (const auto& part : normalized)
        if (part == ".forge" || part == ".git")
            throw std::runtime_error("Invalid prefab asset path");
    return ProjectPaths(project_).resolve(normalized);
}
PrefabSources PrefabLibrary::scan(AssetCatalog& candidate,
                                  std::map<AssetId, AssetRecord>& records) const {
    PrefabSources sources;
    std::size_t count = 0, bytes = 0;
    for (std::filesystem::recursive_directory_iterator it(project_), end; it != end; ++it) {
        if (++count > 10000)
            throw std::runtime_error("Prefab scan exceeds 10000 entries");
        if (it->is_symlink()) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) {
            if (it->path().filename() == ".forge" || it->path().filename() == ".git" ||
                it.depth() >= 16)
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || !it->path().filename().string().ends_with(".prefab.json"))
            continue;
        bytes += it->file_size();
        if (bytes > 64 * 1024 * 1024)
            throw std::runtime_error("Prefab scan exceeds 64 MiB");
        auto source = read_prefab(it->path());
        const PrefabDocument p(source);
        AssetRecord record{p.asset(), PrefabAsset::type, it->path().lexically_relative(project_),
                           source.at("version").get<unsigned>(),
                           source.value("dependencies", std::vector<AssetId>{})};
        candidate.add(record);
        records.emplace(p.asset(), record);
        sources.emplace(p.asset(), std::move(source));
    }
    for (const auto& [id, record] : records) {
        (void)id;
        for (auto dependency : record.dependencies)
            if (candidate.resolve(dependency, PrefabAsset::type).state != AssetState::Available)
                throw std::runtime_error("Missing/incompatible declared prefab dependency: " +
                                         dependency.str());
    }
    return sources;
}
void PrefabLibrary::refresh(Scene& scene) {
    AssetCatalog candidate(project_);
    std::map<AssetId, AssetRecord> records;
    const auto sources = scan(candidate, records);
    scene.set_prefab_sources(sources);
    catalog_ = std::move(candidate);
    records_.swap(records);
}
void PrefabLibrary::load_scene(Scene& scene, const Json& document) {
    AssetCatalog candidate(project_);
    std::map<AssetId, AssetRecord> records;
    const auto sources = scan(candidate, records);
    auto snapshot = document;
    snapshot["_prefab_sources"] = Json::array();
    for (const auto& [id, source] : sources) {
        (void)id;
        snapshot["_prefab_sources"].push_back(source);
    }
    scene.restore_snapshot(snapshot);
    scene.reset(scene.document());
    catalog_ = std::move(candidate);
    records_.swap(records);
}
Json PrefabLibrary::source(AssetId asset) const {
    const auto resolved = catalog_.resolve(AssetRef<PrefabAsset>{asset});
    if (resolved.state != AssetState::Available)
        throw std::runtime_error(resolved.diagnostic);
    return read_prefab(locate(resolved.record->source));
}
AssetId PrefabLibrary::create(Scene& scene, PrefabDocument document,
                              const std::filesystem::path& relative) {
    const auto path = locate(relative);
    if (std::filesystem::exists(path))
        throw std::runtime_error(
            "Choose a new prefab filename; existing assets are never overwritten by Create");
    if (document.revision() != 1)
        throw std::runtime_error("New prefab assets start at revision 1");
    auto candidate = catalog_;
    auto records = records_;
    AssetRecord record{document.asset(), PrefabAsset::type, relative,
                       document.source.at("version").get<unsigned>(),
                       document.source.value("dependencies", std::vector<AssetId>{})};
    candidate.add(record);
    records.emplace(record.id, record);
    auto sources = scene.prefab_sources();
    if (!sources.emplace(document.asset(), document.source).second)
        throw std::runtime_error("Prefab AssetId already loaded");
    scene.publish_prefab_sources(sources, [&] {
        if (std::filesystem::exists(path))
            throw std::runtime_error("Prefab destination appeared during creation");
        atomic_write(path, document.source.dump(2));
    });
    catalog_ = std::move(candidate);
    records_.swap(records);
    return document.asset();
}
AssetId PrefabLibrary::duplicate(Scene& scene, AssetId asset,
                                 const std::filesystem::path& relative) {
    return create(scene, PrefabDocument(source(asset)).duplicate(), relative);
}
void PrefabLibrary::publish(Scene& scene, const Json& expected, Json candidate) {
    const PrefabDocument old(expected);
    if (candidate.at("asset_id").get<AssetId>() != old.asset() ||
        candidate.at("root") != expected.at("root"))
        throw std::runtime_error("Editing cannot replace a prefab's AssetId/root identity");
    if (old.revision() == UINT64_MAX)
        throw std::runtime_error("Prefab revision exhausted");
    candidate["revision"] = old.revision() + 1;
    const PrefabDocument parsed(candidate);
    candidate = parsed.source;
    const auto path = locate(records_.at(old.asset()).source);
    if (source(old.asset()) != expected)
        throw std::runtime_error("Prefab changed on disk; reopen its source before publishing");
    auto sources = scene.prefab_sources();
    sources[old.asset()] = candidate;
    auto records = records_;
    records.at(old.asset()).schema_version = candidate.at("version").get<unsigned>();
    records.at(old.asset()).dependencies = candidate.value("dependencies", std::vector<AssetId>{});
    AssetCatalog catalog(project_);
    for (const auto& [id, record] : records) {
        (void)id;
        catalog.add(record);
    }
    scene.publish_prefab_sources(sources, [&] {
        if (read_prefab(path) != expected)
            throw std::runtime_error("Prefab changed while preparing the candidate");
        atomic_write(path, candidate.dump(2));
    });
    catalog_ = std::move(catalog);
    records_.swap(records);
}
} // namespace forge
