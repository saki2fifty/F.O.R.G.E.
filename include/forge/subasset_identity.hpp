#pragma once
#include <forge/assets.hpp>
#include <span>

namespace forge {
// Evidence is produced by an admitted importer. Neither source array positions
// nor display names are identity evidence. Digests describe canonical content
// and semantic roles; their interpretation is versioned by evidence_schema.
struct SubassetEvidence {
    std::string exporter_key;
    std::string content_digest;
    std::string semantic_digest;
    bool operator==(const SubassetEvidence&) const = default;
};
struct SubassetIdentityEntry {
    AssetId id;
    std::string key;
    std::string type;
    std::string display_name;
    SubassetEvidence evidence;
    bool removed = false;
    nlohmann::json unknown = nlohmann::json::object();
};
struct SubassetIdentityDocument {
    AssetId owner;
    std::filesystem::path source;
    std::string source_digest;
    std::string evidence_schema;
    std::vector<SubassetIdentityEntry> entries;
    nlohmann::json unknown = nlohmann::json::object();
    nlohmann::json document() const;
    static SubassetIdentityDocument parse(std::string_view bytes);
    void validate() const;
};
struct SubassetObservation {
    // Candidate-local address used to bind importer outputs. Never persisted as
    // the durable key; it may change on every import (for example /meshes/3).
    std::string address;
    std::string type;
    std::string display_name;
    SubassetEvidence evidence;
};
struct SubassetIdentityDecision {
    std::string address;
    // Empty means explicitly create a NEW logical asset. A nonempty ID selects
    // a previous same-type entry, including a deliberately restored tombstone.
    std::optional<AssetId> previous;
};
struct SubassetIdentityConflict {
    std::string code;
    std::string type;
    std::vector<std::string> observations;
    std::vector<AssetId> previous;
    std::string diagnostic;
};
struct SubassetIdentityCandidate {
    // Absent on ANY ambiguity. Neither the input nor the catalog is mutated.
    std::optional<SubassetIdentityDocument> document;
    std::map<std::string, AssetId> assignments;
    std::vector<SubassetIdentityConflict> conflicts;
};
SubassetIdentityCandidate
reconcile_subassets(const SubassetIdentityDocument& previous, std::string source_digest,
                    std::string_view evidence_schema,
                    std::span<const SubassetObservation> observations,
                    std::span<const SubassetIdentityDecision> decisions = {});
// Duplicate identity only. Importer-owned, understood reference fields require
// an explicit remap using old_to_new; opaque/unknown payloads are never scanned.
struct SubassetIdentityDuplicate {
    SubassetIdentityDocument document;
    std::map<AssetId, AssetId> old_to_new;
};
SubassetIdentityDuplicate duplicate_subasset_identity(const SubassetIdentityDocument& source,
                                                      std::filesystem::path destination);
} // namespace forge
