#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include "import_cache_limits.hpp"
#include <algorithm>
#include <forge/asset_publication.hpp>
#include <forge/engine_assets.hpp>
#include <fstream>
#include <set>

namespace forge {
#ifdef FORGE_ASSET_PUBLICATION_TESTING
void asset_publication_test_checkpoint(unsigned stage);
#endif
namespace {
using Json = nlohmann::json;
constexpr auto journal_name = ".forge/asset-publication.json";
constexpr std::size_t journal_limit = 256 * 1024 * 1024;
using asset_storage::erase_file;
using asset_storage::ordinary;
using asset_storage::read;
using asset_storage::replace;
Json parse(std::string_view bytes, std::size_t limit = max_asset_index_bytes) {
    return asset_detail::parse_bounded_json(std::as_bytes(std::span(bytes)), limit);
}
Json optional_bytes(const std::optional<std::string>& value) {
    return value ? Json(*value) : Json();
}
std::optional<std::string> optional_bytes(const Json& value) {
    if (value.is_null())
        return {};
    if (!value.is_string() || value.get_ref<const std::string&>().size() > max_asset_index_bytes)
        throw std::runtime_error("Invalid publication recovery bytes");
    return value.get<std::string>();
}
void cancel_if_requested(std::stop_token cancel) {
    if (cancel.stop_requested())
        throw std::runtime_error("Asset publication cancelled");
}
void check_source_path(const ProjectPaths& paths, const std::filesystem::path& source) {
    auto reserved = [](const std::filesystem::path& path) {
        auto locator = path_utf8(path);
        // Control names remain reserved when a project moves between platforms.
        std::transform(locator.begin(), locator.end(), locator.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : char(c);
        });
        return locator == "forge.assets.json" || locator.starts_with("forge.assets.json.") ||
               locator == ".forge" || locator.starts_with(".forge/") ||
               locator.ends_with(".forge-import.json");
    };
    if (reserved(source) || reserved(paths.relative(paths.resolve(source))) ||
        paths.same_locator(source, "forge.assets.json"))
        throw std::runtime_error("Import source cannot be FORGE publication/control metadata");
}
void check_ticket(const ProjectPaths& paths, const AssetPublicationTicket& ticket) {
    if (!ticket.owner || ProjectPaths::normalize(ticket.source) != ticket.source)
        throw std::runtime_error("Invalid asset publication ticket");
    check_source_path(paths, ticket.source);
    if (read(AssetCatalog::project_index(paths.root())) != ticket.catalog_bytes ||
        read(paths.resolve(AssetPublisher::sidecar_path(ticket.source))) != ticket.sidecar_bytes)
        throw std::runtime_error("Asset catalog or import sidecar changed; discard stale import");
}
std::string selected_key(const AssetRecord& record) {
    if (!record.metadata.contains("forge.import"))
        return {};
    const auto& selected = record.metadata.at("forge.import");
    if (selected.at("version") != 1 || !valid_content_digest(selected.at("key").get<std::string>()))
        throw std::runtime_error("Incompatible selected artifact metadata");
    return selected.at("key");
}
void check_inputs(const ProjectPaths& paths, const AssetPublicationCandidate& candidate,
                  const AssetCatalog& catalog, std::stop_token cancel) {
    std::size_t total = 0;
    auto check_source = [&](const std::filesystem::path& source, const std::string& expected) {
        cancel_if_requested(cancel);
        const auto bytes = asset_detail::read_bytes(paths.resolve(source), 512 * 1024 * 1024);
        total += bytes.size();
        if (total > 1024ull * 1024 * 1024)
            throw std::runtime_error("Publication source recheck exceeds 1 GiB");
        if (asset_detail::content_digest(bytes) != expected)
            throw std::runtime_error("Asset source changed during import: " + path_utf8(source));
    };
    check_source(candidate.ticket.source, candidate.input.source_digest);
    if (candidate.input.source_dependencies.size() > 4096)
        throw std::runtime_error("Publication source file count exceeds limit");
    for (const auto& [source, digest] : candidate.input.source_dependencies)
        check_source(std::filesystem::u8path(source), digest);
    for (const auto& dependency : candidate.input.dependencies) {
        if (dependency.kind == AssetDependencyKind::Optional && dependency.revision.empty())
            continue; // Declared optional fallback; importer compatibility must validate it.
        if (const auto* builtin = engine_asset(dependency.target)) {
            if (dependency.expected_type != builtin->type ||
                dependency.revision != engine_asset_revision(dependency.target))
                throw std::runtime_error("Engine dependency type/revision mismatch: " +
                                         dependency.target.str());
            continue;
        }
        const auto found = catalog.records().find(dependency.target);
        if (found == catalog.records().end() || found->second.type != dependency.expected_type ||
            (found->second.subasset && found->second.subasset->removed) ||
            selected_key(found->second) != dependency.revision || dependency.revision.empty())
            throw std::runtime_error("Required asset dependency revision changed or is missing: " +
                                     dependency.target.str());
    }
}
} // namespace

Json AssetImportSidecar::document() const {
    identity.validate();
    if (!unknown.is_object())
        throw std::runtime_error("Invalid import sidecar unknown fields");
    Json value = unknown;
    for (auto field : {"format", "version", "settings", "identity", "build_inputs"})
        if (value.contains(field))
            throw std::runtime_error("Opaque sidecar shadows reserved field");
    value.update({{"format", "forge.asset-import"},
                  {"version", 1},
                  {"settings", settings},
                  {"identity", identity.document()},
                  {"build_inputs", build_inputs}});
    (void)asset_build_digest(unknown);
    (void)asset_build_digest(build_inputs);
    if (value.dump().size() > max_asset_index_bytes)
        throw std::runtime_error("Import sidecar exceeds 64 MiB");
    return value;
}
AssetImportSidecar AssetImportSidecar::parse(std::string_view bytes) {
    auto value = forge::parse(bytes);
    if (value.at("format") != "forge.asset-import" || value.at("version") != 1)
        throw std::runtime_error("Unsupported import sidecar");
    AssetImportSidecar result{value.at("settings").get<ImportSettingsDocument>(),
                              SubassetIdentityDocument::parse(value.at("identity").dump()),
                              value.at("build_inputs")};
    for (auto field : {"format", "version", "settings", "identity", "build_inputs"})
        value.erase(field);
    result.unknown = std::move(value);
    (void)result.document();
    return result;
}
AssetPublisher::AssetPublisher(const ProjectLease& lease)
    : lease_(lease), paths_(lease.root()), thread_(std::this_thread::get_id()) {
    check_owner();
}
void AssetPublisher::check_owner() const {
    if (std::this_thread::get_id() != thread_)
        throw std::runtime_error("Asset publication must run on its owning writer thread");
    lease_.check();
}
std::filesystem::path AssetPublisher::sidecar_path(const std::filesystem::path& source) {
    auto result = ProjectPaths::normalize(source);
    result += ".forge-import.json";
    return result;
}
AssetPublicationTicket AssetPublisher::capture(AssetId owner,
                                               const std::filesystem::path& source) const {
    check_owner();
    if (std::filesystem::exists(paths_.resolve(journal_name)) ||
        std::filesystem::exists(paths_.resolve(".forge/asset-file-operation.json")))
        throw std::runtime_error("Recover interrupted asset publication before importing");
    AssetPublicationTicket result{owner, ProjectPaths::normalize(source),
                                  read(AssetCatalog::project_index(paths_.root())),
                                  read(paths_.resolve(sidecar_path(source)))};
    check_ticket(paths_, result);
    return result;
}
bool AssetPublisher::recover() {
    check_owner();
    const auto journal_path = paths_.resolve(journal_name);
    const auto bytes = read(journal_path, journal_limit);
    if (!bytes)
        return false;
    const auto journal = parse(*bytes, journal_limit);
    if (journal.at("format") != "forge.asset-publication" || journal.at("version") != 1)
        throw std::runtime_error("Unsupported asset publication recovery record");
    const auto source =
        ProjectPaths::normalize(std::filesystem::u8path(journal.at("source").get<std::string>()));
    check_source_path(paths_, source);
    const auto sidecar = paths_.resolve(sidecar_path(source));
    const auto index = AssetCatalog::project_index(paths_.root());
    const auto old_catalog = optional_bytes(journal.at("old_catalog"));
    const auto new_catalog = optional_bytes(journal.at("new_catalog"));
    const auto old_sidecar = optional_bytes(journal.at("old_sidecar"));
    const auto new_sidecar = optional_bytes(journal.at("new_sidecar"));
    if (!new_catalog || !new_sidecar)
        throw std::runtime_error("Incomplete publication recovery record");
    AssetCatalog validation(paths_.root());
    validation.restore(parse(*new_catalog));
    const auto imported = AssetImportSidecar::parse(*new_sidecar);
    if (imported.identity.source != source ||
        !validation.records().contains(imported.identity.owner) ||
        validation.records().at(imported.identity.owner).source != source ||
        selected_key(validation.records().at(imported.identity.owner)) !=
            asset_build_digest(imported.build_inputs))
        throw std::runtime_error("Publication recovery identity mismatch");
    if (old_catalog) {
        AssetCatalog previous(paths_.root());
        previous.restore(parse(*old_catalog));
    }
    if (old_sidecar) {
        const auto previous = AssetImportSidecar::parse(*old_sidecar);
        if (previous.identity.source != source ||
            previous.identity.owner != imported.identity.owner)
            throw std::runtime_error("Prior import recovery identity mismatch");
    }
    const auto current_catalog = read(index), current_sidecar = read(sidecar);
    if (current_catalog == new_catalog && current_sidecar == new_sidecar) {
        erase_file(journal_path); // Catalog was the commit point; finish cleanup only.
        return true;
    }
    if (current_catalog != old_catalog ||
        (current_sidecar != old_sidecar && current_sidecar != new_sidecar))
        throw std::runtime_error("Asset recovery conflicts with external edits; files preserved");
    if (current_sidecar != old_sidecar) {
        if (old_sidecar)
            replace(sidecar, *old_sidecar);
        else
            erase_file(sidecar);
    }
    erase_file(journal_path);
    return true;
}
AssetPublicationResult AssetPublisher::publish(AssetPublicationCandidate candidate,
                                               const AssetImporter& importer,
                                               const Compatibility& compatibility,
                                               std::stop_token cancel) {
    check_owner();
    cancel_if_requested(cancel);
    if (!compatibility)
        throw std::runtime_error("Runtime compatibility preflight is required");
    if (std::filesystem::exists(paths_.resolve(journal_name)))
        throw std::runtime_error("Recover interrupted asset publication before importing");
    check_ticket(paths_, candidate.ticket);
    const auto& descriptor = importer.descriptor();
    if (candidate.input.importer != descriptor.id ||
        candidate.input.importer_revision != descriptor.revision ||
        candidate.input.output_format != descriptor.output_format ||
        candidate.input.output_version != descriptor.output_version ||
        candidate.input.settings_version != importer.settings().version() ||
        candidate.input.settings != importer.settings().effective(candidate.sidecar.settings) ||
        candidate.sidecar.build_inputs != candidate.input.document() ||
        candidate.sidecar.identity.owner != candidate.ticket.owner ||
        candidate.sidecar.identity.source != candidate.ticket.source ||
        candidate.sidecar.identity.source_digest != candidate.input.source_digest ||
        !std::any_of(descriptor.targets.begin(), descriptor.targets.end(), [&](const auto& target) {
            return (target.platform == "*" || target.platform == candidate.input.platform) &&
                   (target.backend == "*" || target.backend == candidate.input.backend) &&
                   (target.profile == "*" || target.profile == candidate.input.profile);
        }))
        throw std::runtime_error("Import candidate identity/settings/profile mismatch");
    if (candidate.ticket.sidecar_bytes) {
        const auto previous = AssetImportSidecar::parse(*candidate.ticket.sidecar_bytes);
        if (previous.identity.owner != candidate.ticket.owner ||
            previous.identity.source != candidate.ticket.source)
            throw std::runtime_error("Existing import sidecar belongs to another logical asset");
        auto preserve = [](Json& current, const Json& old) {
            auto merged = old;
            merged.update(current);
            current = std::move(merged);
        };
        preserve(candidate.sidecar.unknown, previous.unknown);
        preserve(candidate.sidecar.settings.unknown, previous.settings.unknown);
        preserve(candidate.sidecar.identity.unknown, previous.identity.unknown);
        std::map<AssetId, SubassetIdentityEntry*> next;
        for (auto& entry : candidate.sidecar.identity.entries)
            next.emplace(entry.id, &entry);
        for (const auto& entry : previous.identity.entries) {
            const auto found = next.find(entry.id);
            if (found == next.end() || found->second->type != entry.type ||
                found->second->key != entry.key)
                throw std::runtime_error(
                    "Reimport must preserve subasset identity or tombstone it");
            preserve(found->second->unknown, entry.unknown);
        }
    }
    const auto sidecar_bytes = candidate.sidecar.document().dump(2);
    AssetCatalog catalog(paths_.root());
    if (candidate.ticket.catalog_bytes)
        catalog.restore(parse(*candidate.ticket.catalog_bytes));
    std::uint64_t publication_generation = 1;
    if (const auto prior = catalog.records().find(candidate.ticket.owner);
        prior != catalog.records().end() && prior->second.metadata.contains("forge.import")) {
        (void)selected_key(prior->second);
        const auto generation =
            prior->second.metadata.at("forge.import").value("generation", Json(0u));
        if (!generation.is_number_integer() || generation.get<double>() < 0 ||
            generation.get<std::uint64_t>() == UINT64_MAX)
            throw std::runtime_error("Invalid/exhausted asset publication generation");
        publication_generation = generation.get<std::uint64_t>() + 1;
    }
    check_inputs(paths_, candidate, catalog, cancel);
    std::map<AssetId, AssetRecord> next;
    for (const auto& record : candidate.records) {
        if (!next.emplace(record.id, record).second || record.source != candidate.ticket.source ||
            std::find(descriptor.output_types.begin(), descriptor.output_types.end(),
                      record.type) == descriptor.output_types.end())
            throw std::runtime_error("Invalid imported record set or unsupported output type");
        if (record.id == candidate.ticket.owner
                ? record.subasset.has_value()
                : (!record.subasset || record.subasset->owner != candidate.ticket.owner))
            throw std::runtime_error("Import candidate may only replace its own root/subassets");
    }
    if (!next.contains(candidate.ticket.owner) ||
        next.size() != candidate.sidecar.identity.entries.size() + 1)
        throw std::runtime_error("Import record set does not cover its durable member mapping");
    for (const auto& entry : candidate.sidecar.identity.entries) {
        const auto found = next.find(entry.id);
        if (found == next.end() || found->second.type != entry.type ||
            found->second.subasset !=
                std::optional(AssetSubasset{candidate.ticket.owner, entry.key, entry.removed}))
            throw std::runtime_error("Import record disagrees with durable member mapping");
    }
    for (const auto id : catalog.members(candidate.ticket.owner, true))
        if (!next.contains(id))
            throw std::runtime_error("Removed import member needs a tombstone");
    auto& owner = next.at(candidate.ticket.owner);
    owner.source_dependencies.clear();
    for (const auto& [source, digest] : candidate.input.source_dependencies)
        owner.source_dependencies.push_back(
            {std::filesystem::u8path(source), "import.source", digest});
    for (const auto& dependency : candidate.input.dependencies) {
        if (next.contains(dependency.target))
            throw std::runtime_error("Build inputs must not depend on their own candidate outputs");
        if (std::find(owner.dependency_edges.begin(), owner.dependency_edges.end(), dependency) ==
            owner.dependency_edges.end())
            owner.dependency_edges.push_back(dependency);
    }
    std::set<AssetId> owner_targets;
    for (const auto& edge : owner.dependency_edges)
        owner_targets.insert(edge.target);
    owner.dependencies.assign(owner_targets.begin(), owner_targets.end());
    for (const auto& [id, record] : next) {
        (void)id;
        for (const auto& edge : record.dependency_edges)
            if (!next.contains(edge.target) &&
                std::find(candidate.input.dependencies.begin(), candidate.input.dependencies.end(),
                          edge) == candidate.input.dependencies.end())
                throw std::runtime_error(
                    "External output dependency is absent from captured build inputs");
    }
    DerivedDataCache cache(paths_.root(), asset_detail::import_cache_limits(descriptor));
    auto artifact = cache.publish(candidate.input, std::move(candidate.files),
                                  [&](const CachedArtifact& value) { importer.validate(value); });
    const auto artifact_digest = asset_build_digest(artifact.manifest.at("files"));
    std::vector<AssetRecord> records;
    for (const auto& [id, record] : catalog.records())
        if (!next.contains(id))
            records.push_back(record);
    for (auto& [id, record] : next) {
        if (catalog.records().contains(id)) {
            auto metadata = catalog.records().at(id).metadata;
            metadata.update(record.metadata);
            record.metadata = std::move(metadata);
        }
        if (!record.subasset || !record.subasset->removed)
            record.metadata["forge.import"] = {
                {"version", 1},
                // Keep the commit point distinct for sidecar-only intent edits.
                // This sequence is not part of content identity or cache keys.
                {"generation", publication_generation},
                {"key", artifact.key},
                {"artifact_digest", artifact_digest},
                {"importer", descriptor.id},
                {"importer_revision", descriptor.revision},
                {"settings_version", candidate.input.settings_version},
                {"source_digest", candidate.input.source_digest},
                {"sidecar_digest",
                 asset_detail::content_digest(std::as_bytes(std::span(sidecar_bytes)))},
                {"dependency_digest",
                 asset_build_digest(Json{{"assets", candidate.input.document().at("dependencies")},
                                         {"sources", candidate.input.source_dependencies}})},
                {"settings_digest", asset_build_digest(candidate.input.settings)},
                {"output_format", candidate.input.output_format},
                {"output_version", candidate.input.output_version},
                {"platform", candidate.input.platform},
                {"backend", candidate.input.backend},
                {"profile", candidate.input.profile}};
        records.push_back(std::move(record));
    }
    auto updated = catalog;
    updated.replace_all(std::move(records));
    compatibility(updated, artifact);
    check_owner();
    check_inputs(paths_, candidate, catalog, cancel);
    check_ticket(paths_, candidate.ticket);
    cancel_if_requested(cancel);
    const auto catalog_bytes = updated.document().dump(2);
    if (catalog_bytes.size() > max_asset_index_bytes)
        throw std::runtime_error("Asset catalog exceeds 64 MiB");
    const Json journal = {{"format", "forge.asset-publication"},
                          {"version", 1},
                          {"source", path_utf8(candidate.ticket.source)},
                          {"old_catalog", optional_bytes(candidate.ticket.catalog_bytes)},
                          {"new_catalog", catalog_bytes},
                          {"old_sidecar", optional_bytes(candidate.ticket.sidecar_bytes)},
                          {"new_sidecar", sidecar_bytes}};
    const auto journal_bytes = journal.dump();
    if (journal_bytes.size() > journal_limit)
        throw std::runtime_error("Asset recovery record exceeds 256 MiB");
    const auto journal_path = paths_.resolve(journal_name);
    const auto index = AssetCatalog::project_index(paths_.root());
    const auto sidecar = paths_.resolve(sidecar_path(candidate.ticket.source));
    std::map<std::filesystem::path, std::string, ProjectLocatorLess> written_sources{
        {index.lexically_relative(paths_.root()),
         asset_detail::content_digest(std::as_bytes(std::span(catalog_bytes)))},
        {sidecar_path(candidate.ticket.source),
         asset_detail::content_digest(std::as_bytes(std::span(sidecar_bytes)))}};
    if (candidate.ticket.catalog_bytes &&
        parse(*candidate.ticket.catalog_bytes).at("version") == 1) {
        auto backup = index;
        backup += ".v1.backup";
        const auto previous = read(backup);
        if (previous && previous != candidate.ticket.catalog_bytes)
            throw std::runtime_error(
                "Asset index migration backup conflicts with current v1 source");
        if (!previous)
            replace(backup, *candidate.ticket.catalog_bytes);
    }
    replace(journal_path, journal_bytes);
#ifdef FORGE_ASSET_PUBLICATION_TESTING
    asset_publication_test_checkpoint(1);
#endif
    std::string cleanup;
    try {
        replace(sidecar, sidecar_bytes);
#ifdef FORGE_ASSET_PUBLICATION_TESTING
        asset_publication_test_checkpoint(2);
#endif
        cancel_if_requested(cancel);
        replace(index, catalog_bytes); // Commit point. No cancellation after this boundary.
#ifdef FORGE_ASSET_PUBLICATION_TESTING
        asset_publication_test_checkpoint(3);
#endif
    } catch (...) {
        // A post-rename directory flush can fail after catalog selection succeeded.
        // Classify by exact disk state, not by whether the last syscall returned.
        if (read(index) != std::optional(catalog_bytes) ||
            read(sidecar) != std::optional(sidecar_bytes)) {
            recover();
            throw;
        }
        cleanup = "Asset selected, but final durability flush failed; recovery record retained";
    }
    if (cleanup.empty()) {
        try {
            erase_file(journal_path);
        } catch (const std::exception& error) {
            cleanup = error.what();
        }
    }
    return {std::move(updated), std::move(artifact), std::move(cleanup),
            std::move(written_sources)};
}
} // namespace forge
