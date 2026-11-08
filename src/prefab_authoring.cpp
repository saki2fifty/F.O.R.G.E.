#include "builtins.hpp"
#include "json_document.hpp"
#include "json_value_equal.hpp"
#include "prefab_component_intent.hpp"
#include "prefab_references.hpp"
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
#include <set>
namespace forge {
namespace {
Json read_prefab(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > PrefabLibrary::max_source_bytes)
        throw std::runtime_error("Prefab source exceeds 8 MiB");
    std::ifstream input(path, std::ios::binary);
    auto source = detail::parse_json_document(input);
    return PrefabDocument(std::move(source)).source;
}
AssetRecord prefab_record(const PrefabDocument& document, const std::filesystem::path& path) {
    AssetRecord record{document.asset(), PrefabAsset::type, path,
                       document.source.at("version").get<unsigned>(), document.dependencies()};
    for (const auto id : record.dependencies) {
        const bool base =
            PrefabDocument::is_variant(document.source) && document.source.at("base") == Json(id);
        record.dependency_edges.push_back({id,
                                           PrefabAsset::type,
                                           AssetDependencyKind::Runtime,
                                           base ? "document:base" : "document:dependencies",
                                           {}});
    }
    return record;
}
void check_base_sources(const PrefabSources& current, const PrefabSources& expected) {
    if (current.size() == expected.size() &&
        std::all_of(current.begin(), current.end(), [&](const auto& item) {
            const auto found = expected.find(item.first);
            return found != expected.end() && detail::json_value_equal(item.second, found->second);
        }))
        return;
    throw std::runtime_error("Prefab base changed; review its source again before publishing");
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
    const auto asset = AssetId::generate();
    bool portable_references = false;
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
        portable_references |=
            !detail::transform_prefab_component_references(
                 item["components"], scene.schema(),
                 [&](const Json& value) {
                     return detail::capture_prefab_reference(value, scene.asset_id(), asset, ids);
                 })
                 .empty();
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
                           {"version", portable_references ? 3 : 1},
                           {"asset_id", asset},
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
        auto record = prefab_record(p, it->path().lexically_relative(project_));
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
PrefabSources PrefabLibrary::base_sources(const Scene& scene, const Json& document) const {
    PrefabSources result;
    const Json* selected = &document;
    while (PrefabDocument::is_variant(*selected)) {
        const auto base = selected->at("base").get<AssetRef<PrefabAsset>>().id;
        if (result.size() >= 16 || result.contains(base))
            throw std::runtime_error("Invalid prefab base chain; refresh sources");
        const auto loaded = scene.prefab_sources().find(base);
        if (loaded == scene.prefab_sources().end() ||
            !detail::json_value_equal(source(base), loaded->second))
            throw std::runtime_error("Prefab base changed on disk; refresh sources before editing");
        selected = &result.emplace(base, loaded->second).first->second;
    }
    return result;
}
AssetId PrefabLibrary::create(Scene& scene, PrefabDocument document,
                              const std::filesystem::path& relative) {
    const auto path = locate(relative);
    if (std::filesystem::exists(path))
        throw std::runtime_error(
            "Choose a new prefab filename; existing assets are never overwritten by Create");
    if (document.revision() != 1)
        throw std::runtime_error("New prefab assets start at revision 1");
    const auto bytes = document.source.dump(2);
    if (bytes.size() > max_source_bytes)
        throw std::runtime_error("Prefab source exceeds 8 MiB");
    auto candidate = catalog_;
    auto records = records_;
    auto record = prefab_record(document, relative);
    candidate.add(record);
    records.emplace(record.id, record);
    auto sources = scene.prefab_sources();
    if (!sources.emplace(document.asset(), document.source).second)
        throw std::runtime_error("Prefab AssetId already loaded");
    const auto bases = base_sources(scene, document.source);
    scene.publish_prefab_sources(sources, [&] {
        check_base_sources(base_sources(scene, document.source), bases);
        if (std::filesystem::exists(path))
            throw std::runtime_error("Prefab destination appeared during creation");
        atomic_write(path, bytes);
    });
    catalog_ = std::move(candidate);
    records_.swap(records);
    return document.asset();
}
AssetId PrefabLibrary::create_variant(Scene& scene, AssetId base,
                                      const std::filesystem::path& relative) {
    const auto loaded = scene.prefab_sources().find(base);
    if (loaded == scene.prefab_sources().end() ||
        !detail::json_value_equal(source(base), loaded->second))
        throw std::runtime_error("Prefab base changed; refresh sources before creating a variant");
    return create(scene,
                  PrefabDocument({{"format", "forge.prefab"},
                                  {"version", 4},
                                  {"asset_id", AssetId::generate()},
                                  {"revision", 1},
                                  {"root", loaded->second.at("root")},
                                  {"base", base},
                                  {"overrides", Json::array()}}),
                  relative);
}
AssetId PrefabLibrary::duplicate(Scene& scene, AssetId asset,
                                 const std::filesystem::path& relative) {
    return create(scene, PrefabDocument(source(asset)).duplicate(scene.schema()), relative);
}
void PrefabLibrary::publish(Scene& scene, const Json& expected, Json candidate,
                            const PrefabSources& expected_bases) {
    const PrefabDocument old(expected);
    if (candidate.at("asset_id").get<AssetId>() != old.asset() ||
        candidate.at("root") != expected.at("root"))
        throw std::runtime_error("Editing cannot replace a prefab's AssetId/root identity");
    if ((PrefabDocument::is_variant(candidate) || PrefabDocument::is_variant(expected)) &&
        candidate.value("base", Json()) != expected.value("base", Json()))
        throw std::runtime_error("Editing cannot replace a prefab's base identity");
    if (old.revision() == UINT64_MAX)
        throw std::runtime_error("Prefab revision exhausted");
    candidate["revision"] = old.revision() + 1;
    const PrefabDocument parsed(candidate);
    candidate = parsed.source;
    const auto bytes = candidate.dump(2);
    if (bytes.size() > max_source_bytes)
        throw std::runtime_error("Prefab source exceeds 8 MiB");
    const auto path = locate(records_.at(old.asset()).source);
    if (!detail::json_value_equal(source(old.asset()), expected))
        throw std::runtime_error("Prefab changed on disk; reopen its source before publishing");
    check_base_sources(base_sources(scene, expected), expected_bases);
    auto sources = scene.prefab_sources();
    sources[old.asset()] = candidate;
    auto records = records_;
    records.at(old.asset()) = prefab_record(parsed, records.at(old.asset()).source);
    AssetCatalog catalog(project_);
    for (const auto& [id, record] : records) {
        (void)id;
        catalog.add(record);
    }
    scene.publish_prefab_sources(sources, [&] {
        check_base_sources(base_sources(scene, expected), expected_bases);
        if (!detail::json_value_equal(read_prefab(path), expected))
            throw std::runtime_error("Prefab changed while preparing the candidate");
        atomic_write(path, bytes);
    });
    catalog_ = std::move(catalog);
    records_.swap(records);
}

std::filesystem::path PrefabLibrary::source_path(AssetId id) const {
    return records_.at(id).source;
}
PrefabApplyCandidate PrefabLibrary::prepare_apply(const Scene& scene,
                                                  const std::string& selected) const {
    PrefabApplyCandidate result;
    result.scene = scene.document();
    const auto effective = scene.effective_document();
    const Json* root = nullptr;
    for (const auto& row : result.scene.at("entities"))
        if (row.at("id") == selected)
            root = &row;
    if (!root || !root->contains("prefab_instance"))
        throw std::runtime_error("Apply requires the prefab instance root");
    const auto asset = root->at("prefab_instance").at("asset").get<AssetId>();
    result.expected = source(asset);
    if (!scene.prefab_sources().contains(asset) ||
        scene.prefab_sources().at(asset) != result.expected)
        throw std::runtime_error("Prefab changed; refresh sources before reviewing Apply");
    result.expected_bases = base_sources(scene, result.expected);
    result.source = result.expected;
    std::map<std::string, Json> shown;
    for (const auto& row : effective.at("entities"))
        shown[row.at("id")] = row;
    const auto schema = scene.schema();
    std::map<std::string, Json> types;
    for (const auto& type : schema.at("components"))
        types[type.at("id")] = type;
    const auto root_id = root->at("id").get<std::string>();
    const auto mapping = root->at("prefab_instance").at("members");
    const bool variant = PrefabDocument::is_variant(result.source);
    const bool addressed_references = result.source.at("version").get<unsigned>() >= 4;
    const auto* definition = scene.prefab_definition(asset);
    if (!definition)
        throw std::runtime_error("Refresh prefab sources before reviewing Apply");
    std::map<std::string, std::string> inverse;
    for (const auto& [member, entity] : mapping.items())
        inverse.emplace(entity.get<std::string>(), member);
    auto transfer_reference = [&](const Json& value) {
        return addressed_references
                   ? detail::capture_prefab_reference(value, scene.asset_id(), asset, inverse)
                   : value;
    };
    bool portable_references = false;
    auto& authored = result.source[variant ? "overrides" : "members"];
    for (const auto& inherited : definition->at("members")) {
        const auto key = inherited.at("id").get<std::string>();
        if (!mapping.contains(key))
            continue;
        const auto entity = mapping.at(key).get<std::string>();
        auto row = std::find_if(result.scene["entities"].begin(), result.scene["entities"].end(),
                                [&](const Json& value) { return value.at("id") == entity; });
        if (row == result.scene["entities"].end() || row->value("missing_member", false))
            continue;
        if (entity != root_id &&
            row->value("prefab_member", Json::object()).value("root", "") != root_id)
            throw std::runtime_error("Prefab member mapping changed during Apply review");
        auto destination = std::find_if(authored.begin(), authored.end(),
                                        [&](const Json& value) { return value.at("id") == key; });
        if (destination == authored.end()) {
            if (!variant)
                throw std::runtime_error("Prefab definition changed during Apply review");
            authored.push_back({{"id", key}, {"components", Json::object()}});
            destination = std::prev(authored.end());
        }
        auto& member = *destination;
        if (row->value("name_override", false)) {
            member["name"] = row->at("name");
            row->erase("name_override");
            ++result.overrides;
        }
        auto& components = (*row)["components"];
        for (auto it = components.begin(); it != components.end();) {
            const auto type = types.find(it.key());
            if (type == types.end() ||
                (type->second.value("custom", false) &&
                 (!it.value().is_object() ||
                  it.value().value("$forge", Json()) != type->second.at("admission")))) {
                ++it;
                continue; // Unadmitted/opaque authored values remain exactly where they are.
            }
            auto transferred = Json{{it.key(), it.value()}};
            portable_references |= !detail::transform_prefab_component_references(
                                        transferred, schema, transfer_reference)
                                        .empty();
            member["components"][it.key()] = transferred.at(it.key());
            if (result.source.at("version") == 5)
                detail::clear_prefab_component_suppression(member, it.key());
            if (variant && member.contains("property_overrides"))
                member["property_overrides"].erase(it.key());
            it = components.erase(it);
            ++result.overrides;
        }
        auto masks = row->value("property_overrides", Json::object());
        for (auto it = masks.begin(); it != masks.end();) {
            const auto type = types.find(it.key());
            if (type == types.end() || !shown.at(entity).at("components").contains(it.key()) ||
                (type->second.value("custom", false) &&
                 it.value().value("$forge", Json()) != type->second.at("admission"))) {
                ++it;
                continue;
            }
            const auto& value = shown.at(entity).at("components").at(it.key());
            if (!variant && !member["components"].contains(it.key()))
                member["components"][it.key()] = value;
            for (auto field = it.value().begin(); field != it.value().end();) {
                const auto known =
                    std::find_if(type->second.at("fields").begin(), type->second.at("fields").end(),
                                 [&](const Json& f) { return f.at("id") == field.key(); });
                if (known == type->second.at("fields").end()) {
                    ++field;
                    continue;
                }
                auto transferred = value.at(field.key());
                bool reference = false;
                detail::transform_prefab_reference_field(*known, transferred, transfer_reference,
                                                         reference);
                portable_references |= reference;
                if (variant && !member["components"].contains(it.key())) {
                    if (result.source.at("version") == 5)
                        detail::clear_prefab_component_suppression(member, it.key());
                    auto& intent = member["property_overrides"][it.key()];
                    if (it.value().contains("$forge"))
                        intent["$forge"] = it.value().at("$forge");
                    intent[field.key()] = std::move(transferred);
                } else
                    member["components"][it.key()][field.key()] = std::move(transferred);
                field = it.value().erase(field);
                ++result.overrides;
            }
            auto remaining = it.value();
            remaining.erase("$forge");
            if (remaining.empty())
                it = masks.erase(it);
            else
                ++it;
        }
        if (masks.empty())
            row->erase("property_overrides");
        else
            (*row)["property_overrides"] = std::move(masks);
    }
    if (!result.overrides)
        throw std::runtime_error("This instance has no admitted overrides to Apply");
    if (variant) {
        for (auto& member : authored)
            if (member.contains("property_overrides") && member.at("property_overrides").empty())
                member.erase("property_overrides");
        authored.erase(std::remove_if(authored.begin(), authored.end(),
                                      [](const Json& member) {
                                          return member.size() == 2 && member.contains("id") &&
                                                 member.at("components").empty();
                                      }),
                       authored.end());
    }
    if (portable_references && !addressed_references) {
        // Only an explicitly transferred non-null reference activates migration.
        // Unrelated edits retain legacy absolute Scene references and source version.
        for (auto& member : result.source["members"])
            detail::transform_prefab_component_references(
                member["components"], schema, [&](const Json& value) {
                    if (value.contains("prefab")) {
                        (void)detail::checked_prefab_reference(value, asset);
                        return value;
                    }
                    return detail::capture_prefab_reference(value, scene.asset_id(), asset,
                                                            inverse);
                });
        result.source["version"] = 3;
    }
    if (PrefabDocument(result.expected).revision() == UINT64_MAX)
        throw std::runtime_error("Prefab revision exhausted");
    result.source["revision"] = PrefabDocument(result.expected).revision() + 1;
    result.source = PrefabDocument(result.source).source;
    if (result.source.dump(2).size() > max_source_bytes)
        throw std::runtime_error("Prefab source exceeds 8 MiB");
    auto sources = scene.prefab_sources();
    sources[asset] = result.source;
    result.scene = reconcile_prefab_intent(result.scene, sources, schema);
    return result;
}
void PrefabLibrary::publish_apply(Scene& scene, const PrefabApplyCandidate& change,
                                  const std::function<void()>& writer,
                                  std::shared_ptr<Scene::PrefabReplay> replay) {
    const PrefabDocument old(change.expected), next(change.source);
    if (next.source.dump(2).size() > max_source_bytes)
        throw std::runtime_error("Prefab source exceeds 8 MiB");
    if (old.asset() != next.asset() || old.root() != next.root() ||
        next.revision() != old.revision() + 1)
        throw std::runtime_error("Invalid Apply revision/identity");
    if (source(old.asset()) != change.expected)
        throw std::runtime_error("Prefab changed after Apply review; source was not overwritten");
    if ((PrefabDocument::is_variant(change.source) ||
         PrefabDocument::is_variant(change.expected)) &&
        change.source.value("base", Json()) != change.expected.value("base", Json()))
        throw std::runtime_error("Apply cannot replace a prefab's base identity");
    check_base_sources(base_sources(scene, change.expected), change.expected_bases);
    auto sources = scene.prefab_sources();
    sources[old.asset()] = next.source;
    auto records = records_;
    records.at(old.asset()) = prefab_record(next, records.at(old.asset()).source);
    AssetCatalog catalog(project_);
    for (const auto& [id, record] : records) {
        (void)id;
        catalog.add(record);
    }
    auto durable = [&] {
        check_base_sources(base_sources(scene, change.expected), change.expected_bases);
        if (source(old.asset()) != change.expected)
            throw std::runtime_error("Prefab changed during Apply preparation");
        writer();
    };
    if (replay)
        scene.edit_prefab(sources, change.scene, durable, std::move(replay));
    else
        scene.replay_prefab(sources, change.scene, durable);
    catalog_ = std::move(catalog);
    records_.swap(records);
}
} // namespace forge
