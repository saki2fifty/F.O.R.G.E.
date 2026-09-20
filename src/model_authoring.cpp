#include "model_authoring.hpp"
#include "asset_bytes.hpp"
#include "model_bundle.hpp"
#include "model_importer.hpp"
#include <forge/model_asset.hpp>
namespace forge {
void prepare_model_publication(AssetPublicationCandidate& c, const AssetImportPlan& plan,
                               std::span<const SubassetIdentityDecision> decisions) {
    if (plan.input.output_format != "forge.model-bundle" ||
        c.input.document() != plan.input.document())
        throw std::runtime_error("Model publication requires its exact prepared build input");
    const auto bundle = asset_detail::validate_model_bundle(c.files);
    if (bundle.source_digest != c.input.source_digest)
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
    auto reconciled = reconcile_subassets(previous, c.input.source_digest, "forge.gltf-model.v1",
                                          observed, decisions);
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
