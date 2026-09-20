#include "bounded_json.hpp"
#include <algorithm>
#include <forge/subasset_identity.hpp>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t max_entries = 100000;
void text_field(std::string_view value, std::size_t limit, bool empty = false) {
    if ((!empty && value.empty()) || value.size() > limit ||
        value.find('\0') != std::string_view::npos)
        throw std::runtime_error("Invalid subasset identity text field");
}
void evidence_fields(const SubassetEvidence& evidence) {
    text_field(evidence.exporter_key, 1024, true);
    for (const auto* digest : {&evidence.content_digest, &evidence.semantic_digest})
        if (!digest->empty() && !valid_content_digest(*digest))
            throw std::runtime_error("Invalid subasset identity evidence digest");
}
void unknown_fields(const Json& value, std::size_t& total) {
    if (!value.is_object())
        throw std::runtime_error("Subasset unknown fields must be an object");
    (void)asset_build_digest(value);
    const auto size = value.dump().size();
    total += size;
    if (size > 65536 || total > 16 * 1024 * 1024)
        throw std::runtime_error("Subasset opaque metadata exceeds limits");
}
constexpr const char* root_fields[]{"format",        "version",         "owner",  "source",
                                    "source_digest", "evidence_schema", "entries"};
constexpr const char* entry_fields[]{
    "id",     "key", "type", "display_name", "exporter_key", "content_digest", "semantic_digest",
    "removed"};
template <std::size_t N> void no_reserved(const Json& value, const char* const (&fields)[N]) {
    for (const auto* field : fields)
        if (value.contains(field))
            throw std::runtime_error("Opaque subasset data shadows a reserved field");
}
std::string entry_key(AssetId id) { return "member/" + id.str(); }
using GroupKey = std::pair<std::string, std::string>;
} // namespace
void SubassetIdentityDocument::validate() const {
    if (!owner || !valid_content_digest(source_digest) || entries.size() > max_entries)
        throw std::runtime_error("Invalid subasset identity document or entry limit");
    (void)ProjectPaths::normalize(source);
    text_field(path_utf8(source), 4096);
    text_field(evidence_schema, 256);
    std::size_t metadata = 0;
    unknown_fields(unknown, metadata);
    no_reserved(unknown, root_fields);
    std::set<AssetId> ids;
    std::set<std::string> keys;
    std::set<GroupKey> exporter_keys;
    for (const auto& entry : entries) {
        if (!entry.id || entry.id == owner || !ids.insert(entry.id).second ||
            !keys.insert(entry.key).second)
            throw std::runtime_error("Duplicate/empty subasset identity or mapping key");
        text_field(entry.key, 200);
        text_field(entry.type, 256);
        if (entry.type == "legacy-untyped")
            throw std::runtime_error("Subasset identity requires an actual asset type");
        text_field(entry.display_name, 4096, true);
        evidence_fields(entry.evidence);
        unknown_fields(entry.unknown, metadata);
        no_reserved(entry.unknown, entry_fields);
        if (!entry.removed && !entry.evidence.exporter_key.empty() &&
            !exporter_keys.emplace(entry.type, entry.evidence.exporter_key).second)
            throw std::runtime_error("Duplicate active subasset exporter identity");
    }
}
Json SubassetIdentityDocument::document() const {
    validate();
    Json value = unknown;
    value.update({{"format", "forge.subasset-identity"},
                  {"version", 1},
                  {"owner", owner},
                  {"source", path_utf8(ProjectPaths::normalize(source))},
                  {"source_digest", source_digest},
                  {"evidence_schema", evidence_schema}});
    value["entries"] = Json::array();
    std::map<std::string, const SubassetIdentityEntry*> ordered;
    for (const auto& entry : entries)
        ordered.emplace(entry.key, &entry);
    for (const auto& [key, entry] : ordered) {
        Json row = entry->unknown;
        row.update({{"id", entry->id},
                    {"key", key},
                    {"type", entry->type},
                    {"display_name", entry->display_name},
                    {"exporter_key", entry->evidence.exporter_key},
                    {"content_digest", entry->evidence.content_digest},
                    {"semantic_digest", entry->evidence.semantic_digest},
                    {"removed", entry->removed}});
        value["entries"].push_back(std::move(row));
    }
    if (value.dump().size() > max_asset_index_bytes)
        throw std::runtime_error("Subasset identity document exceeds byte limit");
    return value;
}
SubassetIdentityDocument SubassetIdentityDocument::parse(std::string_view bytes) {
    if (bytes.size() > max_asset_index_bytes)
        throw std::runtime_error("Subasset identity document exceeds byte limit");
    auto value =
        asset_detail::parse_bounded_json(std::as_bytes(std::span(bytes)), max_asset_index_bytes);
    if (value.at("format") != "forge.subasset-identity" || value.at("version") != 1 ||
        !value.at("entries").is_array() || value.at("entries").size() > max_entries)
        throw std::runtime_error("Unsupported subasset identity document");
    SubassetIdentityDocument result;
    result.owner = value.at("owner").get<AssetId>();
    result.source =
        ProjectPaths::normalize(std::filesystem::u8path(value.at("source").get<std::string>()));
    result.source_digest = value.at("source_digest").get<std::string>();
    result.evidence_schema = value.at("evidence_schema").get<std::string>();
    for (auto row : value.at("entries")) {
        SubassetIdentityEntry entry;
        entry.id = row.at("id").get<AssetId>();
        entry.key = row.at("key").get<std::string>();
        entry.type = row.at("type").get<std::string>();
        entry.display_name = row.at("display_name").get<std::string>();
        entry.evidence = {row.at("exporter_key").get<std::string>(),
                          row.at("content_digest").get<std::string>(),
                          row.at("semantic_digest").get<std::string>()};
        entry.removed = row.at("removed").get<bool>();
        for (const auto* field : entry_fields)
            row.erase(field);
        entry.unknown = std::move(row);
        result.entries.push_back(std::move(entry));
    }
    for (const auto* field : root_fields)
        value.erase(field);
    result.unknown = std::move(value);
    result.validate();
    return result;
}
SubassetIdentityCandidate reconcile_subassets(const SubassetIdentityDocument& previous,
                                              std::string source_digest,
                                              std::string_view evidence_schema,
                                              std::span<const SubassetObservation> observations,
                                              std::span<const SubassetIdentityDecision> decisions) {
    previous.validate();
    if (!valid_content_digest(source_digest) || observations.size() > max_entries ||
        decisions.size() > observations.size())
        throw std::runtime_error("Invalid subasset reconciliation input or limit");
    // Changing how evidence is computed cannot silently reinterpret the old map.
    if (evidence_schema != previous.evidence_schema)
        throw std::runtime_error("Subasset evidence schema changed; explicit migration required");
    std::map<std::string, std::size_t> observed;
    std::map<AssetId, std::size_t> old;
    std::set<GroupKey> exporter_keys;
    for (std::size_t i = 0; i < observations.size(); ++i) {
        const auto& item = observations[i];
        text_field(item.address, 4096);
        text_field(item.type, 256);
        text_field(item.display_name, 4096, true);
        evidence_fields(item.evidence);
        if (!observed.emplace(item.address, i).second)
            throw std::runtime_error("Duplicate candidate-local subasset address");
        if (!item.evidence.exporter_key.empty() &&
            !exporter_keys.emplace(item.type, item.evidence.exporter_key).second)
            throw std::runtime_error("Duplicate observed subasset exporter identity");
    }
    for (std::size_t i = 0; i < previous.entries.size(); ++i)
        old.emplace(previous.entries[i].id, i);
    std::vector<std::optional<std::size_t>> matches(observations.size());
    std::set<std::size_t> used, explicit_new, decided;
    for (const auto& decision : decisions) {
        const auto found = observed.find(decision.address);
        if (found == observed.end() || !decided.insert(found->second).second)
            throw std::runtime_error("Unknown/duplicate subasset mapping decision");
        if (!decision.previous) {
            explicit_new.insert(found->second);
            continue;
        }
        const auto target = old.find(*decision.previous);
        if (target == old.end() || !used.insert(target->second).second ||
            previous.entries[target->second].type != observations[found->second].type)
            throw std::runtime_error("Subasset mapping decision has unknown/reused/wrong-type ID");
        matches[found->second] = target->second;
    }
    // Match unique groups, never first-by-array-order. Explicit exporter IDs are
    // authoritative: unlike unkeyed evidence, different keys never fall through.
    auto pass = [&](auto key_of, bool stable) {
        std::map<GroupKey, std::vector<std::size_t>> before, after;
        for (std::size_t i = 0; i < previous.entries.size(); ++i) {
            const auto& entry = previous.entries[i];
            if (used.contains(i) || entry.removed ||
                (!stable && !entry.evidence.exporter_key.empty()))
                continue;
            auto key = key_of(entry.evidence);
            if (!key.empty())
                before[{entry.type, std::move(key)}].push_back(i);
        }
        for (std::size_t i = 0; i < observations.size(); ++i) {
            const auto& item = observations[i];
            if (matches[i] || explicit_new.contains(i) ||
                (!stable && !item.evidence.exporter_key.empty()))
                continue;
            auto key = key_of(item.evidence);
            if (!key.empty())
                after[{item.type, std::move(key)}].push_back(i);
        }
        for (const auto& [key, current] : after) {
            const auto found = before.find(key);
            if (current.size() == 1 && found != before.end() && found->second.size() == 1) {
                matches[current.front()] = found->second.front();
                used.insert(found->second.front());
            }
        }
    };
    pass([](const auto& e) { return e.exporter_key; }, true);
    pass(
        [](const auto& e) {
            return e.content_digest.empty() || e.semantic_digest.empty()
                       ? std::string{}
                       : e.content_digest + e.semantic_digest;
        },
        false);
    SubassetIdentityCandidate result;
    // If independently unique content and role evidence disagree, selecting one
    // by heuristic priority would silently retarget a reference. Report it.
    std::vector<std::set<std::size_t>> proposals(observations.size());
    auto propose = [&](auto key_of) {
        std::map<GroupKey, std::vector<std::size_t>> before, after;
        for (std::size_t i = 0; i < previous.entries.size(); ++i) {
            const auto& entry = previous.entries[i];
            if (used.contains(i) || entry.removed || !entry.evidence.exporter_key.empty())
                continue;
            auto key = key_of(entry.evidence);
            if (!key.empty())
                before[{entry.type, std::move(key)}].push_back(i);
        }
        for (std::size_t i = 0; i < observations.size(); ++i) {
            const auto& item = observations[i];
            if (matches[i] || explicit_new.contains(i) || !item.evidence.exporter_key.empty())
                continue;
            auto key = key_of(item.evidence);
            if (!key.empty())
                after[{item.type, std::move(key)}].push_back(i);
        }
        for (const auto& [key, current] : after) {
            const auto found = before.find(key);
            if (current.size() == 1 && found != before.end() && found->second.size() == 1)
                proposals[current.front()].insert(found->second.front());
        }
    };
    propose([](const auto& e) { return e.semantic_digest; });
    propose([](const auto& e) { return e.content_digest; });
    std::map<std::size_t, std::size_t> claims;
    for (const auto& proposed : proposals)
        for (auto target : proposed)
            ++claims[target];
    for (std::size_t i = 0; i < proposals.size(); ++i)
        if (proposals[i].size() == 1 && claims.at(*proposals[i].begin()) == 1) {
            matches[i] = *proposals[i].begin();
            used.insert(*matches[i]);
        }
    // Remaining objects can be new or ambiguously related to removed/changed
    // entries. Group the diagnostic to avoid a quadratic all-pairs comparison.
    std::map<std::string, std::vector<AssetId>> uncertain_old, all_old;
    std::map<GroupKey, std::vector<AssetId>> tombstone_exporters;
    for (std::size_t i = 0; i < previous.entries.size(); ++i) {
        const auto& entry = previous.entries[i];
        if (used.contains(i))
            continue;
        all_old[entry.type].push_back(entry.id);
        if (entry.evidence.exporter_key.empty())
            uncertain_old[entry.type].push_back(entry.id);
        else if (entry.removed)
            tombstone_exporters[{entry.type, entry.evidence.exporter_key}].push_back(entry.id);
    }
    std::map<std::string, SubassetIdentityConflict> conflicts;
    for (std::size_t i = 0; i < observations.size(); ++i) {
        if (matches[i] || explicit_new.contains(i))
            continue;
        const auto& item = observations[i];
        const auto& candidates =
            item.evidence.exporter_key.empty() ? all_old[item.type] : uncertain_old[item.type];
        const auto tombstone = tombstone_exporters.find({item.type, item.evidence.exporter_key});
        if (candidates.empty() && tombstone == tombstone_exporters.end())
            continue;
        auto& conflict = conflicts[item.type];
        conflict.code = "subasset.identity-ambiguous";
        conflict.type = item.type;
        conflict.observations.push_back(item.address);
        // Per-type union is bounded by the source entry count.
        if (conflict.previous.empty())
            conflict.previous = all_old[item.type];
        conflict.diagnostic = "Cannot establish subasset correspondence. Choose an existing "
                              "same-type member or explicitly create a new logical asset; "
                              "previous outputs remain selected.";
    }
    for (auto& [type, conflict] : conflicts) {
        (void)type;
        result.conflicts.push_back(std::move(conflict));
    }
    if (!result.conflicts.empty())
        return result;
    auto candidate = previous;
    candidate.source_digest = std::move(source_digest);
    for (auto& entry : candidate.entries)
        entry.removed = true;
    // Sort ephemeral addresses only to make allocation traversal reproducible.
    // Random IDs are allocated once and persisted; they are not content hashes.
    for (const auto& [address, index] : observed) {
        const auto& item = observations[index];
        std::size_t target;
        if (matches[index]) {
            target = *matches[index];
        } else {
            if (candidate.entries.size() >= max_entries)
                throw std::runtime_error("Subasset identity retained-entry limit exceeded");
            target = candidate.entries.size();
            SubassetIdentityEntry entry;
            entry.id = AssetId::generate();
            entry.key = entry_key(entry.id);
            entry.type = item.type;
            candidate.entries.push_back(std::move(entry));
        }
        auto& entry = candidate.entries[target];
        entry.display_name = item.display_name;
        entry.evidence = item.evidence;
        entry.removed = false;
        result.assignments.emplace(address, entry.id);
    }
    candidate.validate();
    result.document = std::move(candidate);
    return result;
}
SubassetIdentityDuplicate duplicate_subasset_identity(const SubassetIdentityDocument& source,
                                                      std::filesystem::path destination) {
    source.validate();
    SubassetIdentityDuplicate result{source, {}};
    result.document.source = ProjectPaths::normalize(destination);
    result.document.owner = AssetId::generate();
    result.old_to_new.emplace(source.owner, result.document.owner);
    for (auto& entry : result.document.entries) {
        const auto previous = entry.id;
        entry.id = AssetId::generate();
        entry.key = entry_key(entry.id);
        result.old_to_new.emplace(previous, entry.id);
    }
    result.document.validate();
    return result;
}
} // namespace forge
