#include "model_authoring.hpp"
#include "asset_bytes.hpp"
#include "model_bundle.hpp"
#include "model_importer.hpp"
#include "model_selection.hpp"
#include <algorithm>
#include <forge/model_asset.hpp>
#include <set>
namespace forge {
namespace {
using SelectedBindings = std::map<std::string, AssetId>;
std::optional<SelectedBindings>
selected_correspondence(const AssetPublicationCandidate& c,
                        const asset_detail::ModelBundleIndex& bundle, const AssetCatalog& catalog) {
    const auto root = catalog.records().find(c.ticket.owner);
    if (root == catalog.records().end() || !root->second.metadata.contains("forge.import") ||
        root->second.metadata.at("forge.import").at("key") != c.input.key())
        return {};
    const auto& owner = root->second;
    if (owner.type != ModelAsset::type || owner.subasset || owner.source != c.ticket.source ||
        !owner.metadata.contains("forge.model"))
        return {};
    const auto file = std::find_if(c.files.begin(), c.files.end(),
                                   [](const auto& f) { return f.name == "model.json"; });
    if (file == c.files.end() ||
        owner.metadata.at("forge.model").at("sha256") !=
            asset_detail::content_digest(file->bytes) ||
        owner.metadata.at("forge.model").at("bytes") != file->bytes.size())
        return {};
    // Same complete input key AND same compiled index authenticating every member.
    // Addresses now refer to this exact existing revision, not source-order guesses
    // across different imports. The catalog remains the sole selected binding graph.
    auto require = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Selected model correspondence disagrees with "
                                     "catalog/sidecar; restore consistent import metadata");
    };
    require(std::count_if(owner.dependency_edges.begin(), owner.dependency_edges.end(),
                          [](const auto& edge) { return !is_declared_runtime_dependency(edge); }) ==
            bundle.members.size());
    std::map<std::string, AssetId> selected;
    for (const auto& edge : owner.dependency_edges) {
        if (is_declared_runtime_dependency(edge))
            continue;
        require(edge.kind == AssetDependencyKind::Runtime &&
                edge.role.starts_with("model.member:") && edge.revision == c.input.key() &&
                selected.emplace(edge.role.substr(13), edge.target).second);
        const auto found = catalog.records().find(edge.target);
        require(found != catalog.records().end() && found->second.type == edge.expected_type);
    }
    std::map<AssetId, const SubassetIdentityEntry*> previous;
    for (const auto& entry : c.sidecar.identity.entries)
        previous.emplace(entry.id, &entry);
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
    }
    return selected;
}
bool same_source_structure(const asset_detail::ModelBundleIndex& before,
                           const asset_detail::ModelBundleIndex& after, bool migrating) {
    if (before.members.size() != after.members.size())
        return false;
    const auto& a = before.hierarchy;
    const auto& b = after.hierarchy;
    auto old_nodes = a.at("nodes"), new_nodes = b.at("nodes");
    for (auto* nodes : {&old_nodes, &new_nodes})
        for (auto& node : *nodes) {
            node.erase("local");
            node.erase("trs");
        }
    if (old_nodes != new_nodes ||
        a.value("material_variants", nlohmann::json::array()) !=
            b.value("material_variants", nlohmann::json::array()) ||
        a.contains("animation") != b.contains("animation"))
        return false;
    if (a.contains("animation")) {
        const auto& old_plan = a.at("animation").at("plan");
        const auto& new_plan = b.at("animation").at("plan");
        for (const auto* key : {"nodes", "joint_nodes", "joint_parents", "node_skins", "skins"})
            if (old_plan.value(key, nlohmann::json{}) != new_plan.value(key, nlohmann::json{}))
                return false;
        const auto& old_clips = old_plan.at("clips");
        const auto& new_clips = new_plan.at("clips");
        if (old_clips.size() != new_clips.size())
            return false;
        for (std::size_t i = 0; i < old_clips.size(); ++i)
            for (const auto* key : {"source_index", "transform_channels", "morph_tracks"})
                if (old_clips[i].value(key, nlohmann::json{}) !=
                    new_clips[i].value(key, nlohmann::json{}))
                    return false;
    }
    std::map<std::string, const asset_detail::ModelImportMember*> old_members;
    for (const auto& member : before.members)
        old_members.emplace(member.identity.address, &member);
    for (const auto& member : after.members) {
        const auto found = old_members.find(member.identity.address);
        if (found == old_members.end())
            return false;
        const auto& old = *found->second;
        if (old.identity.type != member.identity.type || old.node != member.node ||
            old.material_variant != member.material_variant || old.bindings != member.bindings)
            return false;
        if (old.identity.evidence.exporter_key != member.identity.evidence.exporter_key)
            return false;
        // Migration changes transform/context evidence only. Raw geometry,
        // material/image/variant content and clip curves must still agree.
        if ((!migrating && old.identity.evidence != member.identity.evidence) ||
            (migrating && member.identity.type != "model_node" &&
             member.identity.type != "skeleton" &&
             old.identity.evidence.content_digest != member.identity.evidence.content_digest))
            return false;
    }
    return true;
}
std::optional<SelectedBindings> source_correspondence(const AssetPublicationCandidate& c,
                                                      const asset_detail::ModelBundleIndex& bundle,
                                                      const AssetCatalog& catalog, bool migrating) {
    if (!c.ticket.sidecar_bytes || !catalog.records().contains(c.ticket.owner))
        return {};
    const auto sidecar = AssetImportSidecar::parse(*c.ticket.sidecar_bytes);
    auto old_inputs = sidecar.build_inputs;
    auto new_inputs = c.input.document();
    auto known_key = [](const auto& input) {
        return input.is_object() && input.contains("key_version") &&
               (input.at("key_version") == 1 || input.at("key_version") == 2);
    };
    if (!known_key(old_inputs) || !known_key(new_inputs))
        return {};
    // Toolchain/recipe revisions may change; source bytes, includes, settings,
    // build dependencies and target must all be exactly the same revision.
    // Key1 omits tool revisions; key2 adds them. Both have the same remaining
    // source/settings contract, so adding a producer must not change identity.
    for (const auto* key :
         {"importer_revision", "tool_revisions", "output_version", "key_version"}) {
        old_inputs.erase(key);
        new_inputs.erase(key);
    }
    if (sidecar.identity.source_digest != c.input.source_digest || old_inputs != new_inputs)
        return {};
    asset_detail::ModelSelection selected;
    try {
        selected =
            asset_detail::load_model_selection(catalog.project_root(), catalog, c.ticket.owner);
    } catch (const std::runtime_error&) {
        // Missing/corrupt disposable cooks cannot prove correspondence. The
        // ordinary resolver below still permits complete explicit review.
        return {};
    } catch (const nlohmann::json::exception&) {
        return {}; // Malformed selected cache metadata also requires explicit review.
    }
    if (selected.artifact->manifest.at("inputs") != sidecar.build_inputs ||
        selected.index.evidence_schema != sidecar.identity.evidence_schema ||
        (migrating && (selected.index.version < 3 || selected.index.version > 4)) ||
        !same_source_structure(selected.index, bundle, migrating))
        return {};
    std::map<AssetId, const SubassetIdentityEntry*> entries;
    for (const auto& entry : sidecar.identity.entries)
        if (!entry.removed)
            entries.emplace(entry.id, &entry);
    if (entries.size() != selected.index.members.size())
        throw std::runtime_error("Model sidecar active members differ from selected catalog");
    for (const auto& member : selected.index.members) {
        const auto id = selected.bindings.at(member.identity.address);
        const auto found = entries.find(id);
        const auto& record = catalog.records().at(id);
        if (found == entries.end() || found->second->type != member.identity.type ||
            found->second->evidence != member.identity.evidence ||
            found->second->key != record.subasset->key)
            throw std::runtime_error(
                "Model sidecar evidence differs from selected cooked revision");
    }
    return selected.bindings;
}
std::vector<SubassetIdentityDecision>
reviewed_correspondence(std::span<const SubassetIdentityDecision> explicit_decisions,
                        const std::optional<SelectedBindings>& selected) {
    std::vector<SubassetIdentityDecision> result(explicit_decisions.begin(),
                                                 explicit_decisions.end());
    std::set<std::string> decided;
    std::set<AssetId> claimed;
    for (const auto& decision : explicit_decisions) {
        decided.insert(decision.address);
        if (decision.previous)
            claimed.insert(*decision.previous);
    }
    if (selected)
        for (const auto& [address, id] : *selected)
            if (!decided.contains(address) && !claimed.contains(id))
                result.push_back({address, id});
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
    if (bundle.version != 5 || bundle.evidence_schema != "forge.gltf-model.v2")
        throw std::runtime_error("New model publications require source identity evidence v2");
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
        const auto existing = previous_catalog.records().find(c.ticket.owner);
        if (existing != previous_catalog.records().end() &&
            existing->second.metadata.contains("forge.import"))
            throw std::runtime_error("Published model identity sidecar is missing; restore it "
                                     "before reimporting to preserve existing member IDs");
        previous.owner = c.ticket.owner;
        previous.source = c.ticket.source;
        previous.source_digest = c.input.source_digest;
        previous.evidence_schema = bundle.evidence_schema;
    }
    if (previous.owner != c.ticket.owner || previous.source != c.ticket.source)
        throw std::runtime_error("Model sidecar belongs to another asset/source");
    std::vector<SubassetObservation> observed;
    for (const auto& member : bundle.members)
        observed.push_back(member.identity);
    const bool migrating = previous.evidence_schema == "forge.gltf-model.v1";
    if (!migrating && previous.evidence_schema != bundle.evidence_schema)
        throw std::runtime_error("Unsupported model evidence migration");
    auto selected = selected_correspondence(c, bundle, previous_catalog);
    if (!selected)
        selected = source_correspondence(c, bundle, previous_catalog, migrating);
    if (migrating) {
        previous.evidence_schema = bundle.evidence_schema;
        // Never reinterpret legacy hashes, including tombstone evidence. IDs,
        // durable keys, removed state and opaque entry/root data remain intact.
        for (auto& entry : previous.entries) {
            entry.evidence.content_digest.clear();
            entry.evidence.semantic_digest.clear();
        }
    }
    const auto mapped = reviewed_correspondence(decisions, selected);
    auto reconciled = reconcile_subassets(previous, c.input.source_digest, bundle.evidence_schema,
                                          observed, mapped);
    if (!reconciled.document) {
        if (migrating)
            for (auto& conflict : reconciled.conflicts)
                conflict.diagnostic = "Legacy model identity evidence needs migration. Restore "
                                      "the original inputs and selected import, or review each "
                                      "member explicitly. " +
                                      conflict.diagnostic;
        throw SubassetIdentityFailure(std::move(reconciled.conflicts));
    }
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
            if (member.node)
                record.metadata["forge.model"] = {
                    {"version", 2}, {"node", *member.node}, {"name", member.identity.display_name}};
            if (member.material_variant)
                record.metadata["forge.model"] = {{"version", 3},
                                                  {"material_variant", *member.material_variant},
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
