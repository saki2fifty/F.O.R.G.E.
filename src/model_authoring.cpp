#include "model_authoring.hpp"
#include "asset_bytes.hpp"
#include "model_bundle.hpp"
#include "model_importer.hpp"
#include <algorithm>
#include <forge/model_asset.hpp>
#include <set>
namespace forge {
namespace {
std::vector<SubassetIdentityDecision>
selected_correspondence(const AssetPublicationCandidate& c,
                        const asset_detail::ModelBundleIndex& bundle, const AssetCatalog& catalog,
                        std::span<const SubassetIdentityDecision> explicit_decisions) {
    std::vector<SubassetIdentityDecision> result(explicit_decisions.begin(),
                                                 explicit_decisions.end());
    const auto root = catalog.records().find(c.ticket.owner);
    if (root == catalog.records().end() || !root->second.metadata.contains("forge.import") ||
        root->second.metadata.at("forge.import").at("key") != c.input.key())
        return result;
    const auto& owner = root->second;
    if (owner.type != ModelAsset::type || owner.subasset || owner.source != c.ticket.source ||
        !owner.metadata.contains("forge.model"))
        return result;
    const auto file = std::find_if(c.files.begin(), c.files.end(),
                                   [](const auto& f) { return f.name == "model.json"; });
    if (file == c.files.end() ||
        owner.metadata.at("forge.model").at("sha256") !=
            asset_detail::content_digest(file->bytes) ||
        owner.metadata.at("forge.model").at("bytes") != file->bytes.size())
        return result;
    // Same complete input key AND same compiled index authenticating every member.
    // Addresses now refer to this exact existing revision, not source-order guesses
    // across different imports. The catalog remains the sole selected binding graph.
    auto require = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Selected model correspondence disagrees with "
                                     "catalog/sidecar; restore consistent import metadata");
    };
    require(owner.dependency_edges.size() == bundle.members.size());
    std::map<std::string, AssetId> selected;
    for (const auto& edge : owner.dependency_edges) {
        require(edge.kind == AssetDependencyKind::Runtime &&
                edge.role.starts_with("model.member:") && edge.revision == c.input.key() &&
                selected.emplace(edge.role.substr(13), edge.target).second);
        const auto found = catalog.records().find(edge.target);
        require(found != catalog.records().end() && found->second.type == edge.expected_type);
    }
    std::map<AssetId, const SubassetIdentityEntry*> previous;
    for (const auto& entry : c.sidecar.identity.entries)
        previous.emplace(entry.id, &entry);
    std::set<std::string> decided;
    std::set<AssetId> claimed;
    for (const auto& decision : explicit_decisions) {
        decided.insert(decision.address);
        if (decision.previous)
            claimed.insert(*decision.previous);
    }
    for (const auto& member : bundle.members) {
        const auto selected_member = selected.find(member.identity.address);
        require(selected_member != selected.end());
        const auto id = selected_member->second;
        const auto old = previous.find(id);
        const auto record = catalog.records().find(id);
        require(old != previous.end() && !old->second->removed &&
                old->second->type == member.identity.type &&
                old->second->evidence == member.identity.evidence &&
                record != catalog.records().end() && record->second.subasset &&
                record->second.subasset->owner == c.ticket.owner &&
                !record->second.subasset->removed &&
                record->second.subasset->key == old->second->key &&
                record->second.metadata.at("forge.import") == owner.metadata.at("forge.import"));
        if (!decided.contains(member.identity.address) && !claimed.contains(id))
            result.push_back({member.identity.address, id});
    }
    return result;
}
} // namespace
void prepare_model_publication(AssetPublicationCandidate& c, const AssetImportPlan& plan,
                               const AssetCatalog& previous_catalog,
                               std::span<const SubassetIdentityDecision> decisions) {
    if (plan.input.output_format != "forge.model-bundle" ||
        c.input.document() != plan.input.document())
        throw std::runtime_error("Model publication requires its exact prepared build input");
    const auto bundle = asset_detail::validate_model_bundle(c.files);
    if (bundle.source_digest != c.input.source_digest || bundle.version != c.input.output_version)
        throw std::runtime_error("Model candidate belongs to another source revision");
    if (bundle.hierarchy.contains("animation")) {
        const auto digest =
            bundle.hierarchy.at("animation").at("provenance").at("converter_sha256");
        if (c.input.importer_revision != asset_detail::model_recipe_revision() ||
            !c.input.tool_revisions.contains("gltf2ozz") ||
            digest != c.input.tool_revisions.at("gltf2ozz") ||
            !plan.data.at("animation").get<bool>() || digest != plan.data.at("converter_sha256"))
            throw std::runtime_error("Model animation provenance differs from prepared recipe");
    } else if (plan.data.at("animation").get<bool>())
        throw std::runtime_error("Animated source cannot publish a static-only family");
    auto previous = c.sidecar.identity;
    if (!previous.owner) {
        previous.owner = c.ticket.owner;
        previous.source = c.ticket.source;
        previous.source_digest = c.input.source_digest;
        previous.evidence_schema = "forge.gltf-model.v1";
    }
    if (previous.owner != c.ticket.owner || previous.source != c.ticket.source)
        throw std::runtime_error("Model sidecar belongs to another asset/source");
    std::vector<SubassetObservation> observed;
    for (const auto& member : bundle.members)
        observed.push_back(member.identity);
    const auto mapped = selected_correspondence(c, bundle, previous_catalog, decisions);
    auto reconciled = reconcile_subassets(previous, c.input.source_digest, "forge.gltf-model.v1",
                                          observed, mapped);
    if (!reconciled.document)
        throw SubassetIdentityFailure(std::move(reconciled.conflicts));
    const auto key = c.input.key();
    std::vector<AssetRecord> records;
    AssetRecord owner{c.ticket.owner, ModelAsset::type, c.ticket.source, 1, {}};
    for (const auto& file : c.files)
        if (file.name == "model.json")
            owner.metadata["forge.model"] = {{"version", 1},
                                             {"file", file.name},
                                             {"sha256", asset_detail::content_digest(file.bytes)},
                                             {"bytes", file.bytes.size()}};
    std::map<AssetId, const asset_detail::ModelImportMember*> by_id;
    for (const auto& member : bundle.members) {
        const auto id = reconciled.assignments.at(member.identity.address);
        by_id.emplace(id, &member);
        // The catalog graph is the only binding authority. This is a revision-local
        // model slot, not a durable identity or a parallel mapping service.
        owner.dependency_edges.push_back({id, member.identity.type, AssetDependencyKind::Runtime,
                                          "model.member:" + member.identity.address, key});
    }
    for (const auto& entry : reconciled.document->entries) {
        AssetRecord record{entry.id, entry.type, c.ticket.source, 1, {}};
        record.subasset = AssetSubasset{c.ticket.owner, entry.key, entry.removed};
        if (!entry.removed) {
            const auto& member = *by_id.at(entry.id);
            record.metadata["forge.model"] = {{"version", 1},
                                              {"file", member.artifact.file},
                                              {"sha256", member.artifact.digest},
                                              {"bytes", member.artifact.bytes},
                                              {"name", member.identity.display_name}};
            for (const auto& [role, address] : member.bindings) {
                const auto target = reconciled.assignments.at(address);
                record.dependency_edges.push_back({target, by_id.at(target)->identity.type,
                                                   AssetDependencyKind::Runtime, role, key});
            }
        }
        std::set<AssetId> targets;
        for (const auto& edge : record.dependency_edges)
            targets.insert(edge.target);
        record.dependencies.assign(targets.begin(), targets.end());
        records.push_back(std::move(record));
    }
    records.push_back(std::move(owner));
    // Catalog-wide validation needs the project and unrelated records and belongs
    // to AssetPublisher. This stage validates only the complete typed family above.
    c.sidecar.identity = std::move(*reconciled.document);
    c.records = std::move(records);
}
} // namespace forge
