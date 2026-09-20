#include <algorithm>
#include <forge/subasset_identity.hpp>
#include <iostream>

using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid identity operation accepted");
}
std::string digest(std::string_view value) { return asset_build_digest(value); }
SubassetObservation observation(std::string address, std::string type, std::string content,
                                std::string role = {}, std::string exporter = {}) {
    return {std::move(address),
            std::move(type),
            "Duplicate display name",
            {std::move(exporter), content.empty() ? "" : digest(content),
             role.empty() ? "" : digest(role)}};
}
SubassetIdentityDocument blank() {
    return {AssetId::generate(), "Assets/model.gltf", digest("initial"), "fixture.evidence/1", {}};
}
SubassetIdentityCandidate reconcile(const SubassetIdentityDocument& previous,
                                    const std::vector<SubassetObservation>& items,
                                    const std::vector<SubassetIdentityDecision>& decisions = {}) {
    return reconcile_subassets(previous, digest("next source"), "fixture.evidence/1", items,
                               decisions);
}
const SubassetIdentityEntry& entry(const SubassetIdentityDocument& doc, AssetId id) {
    const auto it = std::find_if(doc.entries.begin(), doc.entries.end(),
                                 [&](const auto& row) { return row.id == id; });
    if (it == doc.entries.end())
        throw std::runtime_error("Missing identity entry");
    return *it;
}
} // namespace
int main() {
    try {
        auto empty = blank();
        std::vector<SubassetObservation> items{
            observation("/meshes/0", "mesh", "left vertices", "left-hand"),
            observation("/meshes/1", "mesh", "right vertices", "right-hand"),
            observation("/materials/0", "material", "red"),
            observation("/materials/1", "material", "blue"),
            observation("/animations/0", "animation-clip", "walk keys"),
            observation("/animations/1", "animation-clip", "run keys"),
            observation("/skins/0", "skeleton", "bind pose", "humanoid", "exporter.rig")};
        auto first = reconcile(empty, items);
        require(first.document.has_value() && first.assignments.size() == items.size(),
                "Initial mapping failed");
        auto saved = *first.document;
        saved.unknown = {{"plugin", {{"opaque-id", saved.entries.front().id.str()}}}};
        saved.entries.front().unknown = {{"extra", {{"external-id", saved.owner.str()}}}};
        const auto serialized = saved.document().dump();
        auto reopened = SubassetIdentityDocument::parse(serialized);
        require(reopened.document().dump() == serialized, "Sidecar round trip changed data");

        // Rename every item and move every candidate-local address. Display names
        // deliberately duplicate throughout; mesh/material/clip order all change.
        auto reordered = items;
        std::reverse(reordered.begin(), reordered.end());
        for (auto& item : reordered) {
            item.display_name = "Renamed";
            item.address = "new" + item.address;
        }
        std::swap(reordered[5].address, reordered[6].address);
        auto next = reconcile(saved, reordered);
        require(next.document.has_value(), "Reorder/rename was treated as new identity");
        for (const auto& item : reordered) {
            const auto original = std::find_if(items.begin(), items.end(), [&](const auto& old) {
                return old.evidence == item.evidence && old.type == item.type;
            });
            require(next.assignments.at(item.address) == first.assignments.at(original->address),
                    "Reorder silently retargeted a reference");
        }
        require(next.document->unknown == saved.unknown &&
                    entry(*next.document, saved.entries.front().id).unknown ==
                        saved.entries.front().unknown,
                "Reimport rewrote unknown plugin payload");
        // Geometry changes with a stable semantic role; skeleton changes under
        // an authoritative exporter key preserve logical ID, not compatibility.
        auto changed = items;
        changed[0].evidence.content_digest = digest("modified vertices");
        changed[6].evidence.content_digest = digest("new skeleton layout");
        changed[6].evidence.semantic_digest = digest("modified hierarchy");
        auto edited = reconcile(saved, changed);
        require(edited.document.has_value() && edited.assignments == first.assignments,
                "Identifiable source edit changed AssetIds");

        // Independent content and semantic evidence pointing at different old
        // objects is a conflict, not an implicit heuristic-priority decision.
        auto contradictory = items;
        std::swap(contradictory[0].evidence.semantic_digest,
                  contradictory[1].evidence.semantic_digest);
        auto conflict = reconcile(saved, contradictory);
        require(!conflict.document && conflict.assignments.empty() && !conflict.conflicts.empty(),
                "Contradictory identity evidence silently chose a target");
        require(saved.document().dump() == serialized, "Rejected match modified old state");
        auto explicit_match =
            reconcile(saved, contradictory,
                      {{items[0].address, first.assignments.at(items[0].address)},
                       {items[1].address, first.assignments.at(items[1].address)}});
        require(explicit_match.document.has_value() &&
                    explicit_match.assignments == first.assignments,
                "Explicit reviewed correspondence not accepted");

        auto less = items;
        less.erase(less.begin());
        auto removed = reconcile(saved, less);
        const auto removed_id = first.assignments.at(items[0].address);
        require(removed.document.has_value() && entry(*removed.document, removed_id).removed,
                "Removed member lost its durable tombstone");
        auto returned = reconcile(*removed.document, items);
        require(!returned.document && !returned.conflicts.empty(),
                "Removed member resurrected without an explicit choice");
        auto restored = reconcile(*removed.document, items, {{items[0].address, removed_id}});
        require(restored.document.has_value() && !entry(*restored.document, removed_id).removed,
                "Explicit tombstone restoration failed");
        auto create_new = reconcile(*removed.document, items, {{items[0].address, std::nullopt}});
        require(create_new.document.has_value() &&
                    create_new.assignments.at(items[0].address) != removed_id &&
                    entry(*create_new.document, removed_id).removed,
                "Explicit new identity reused a removed member");

        // Distinct exporter IDs prove removal/addition even with identical bytes.
        auto keyed = reconcile(blank(), {observation("a", "mesh", "identical", "", "A")});
        auto replace_key =
            reconcile(*keyed.document, {observation("a", "mesh", "identical", "", "B")});
        require(replace_key.document.has_value() &&
                    replace_key.assignments.at("a") != keyed.assignments.at("a"),
                "Different exporter IDs fell back to content identity");
        // Duplicate primitives are legal, but their later correspondence needs
        // semantic evidence or explicit mapping rather than array positions.
        std::vector<SubassetObservation> twins{observation("0", "mesh", "same"),
                                               observation("1", "mesh", "same")};
        auto initial_twins = reconcile(blank(), twins);
        auto ambiguous = reconcile(*initial_twins.document, twins);
        require(!ambiguous.document && ambiguous.conflicts.size() == 1 &&
                    ambiguous.conflicts[0].observations.size() == 2 &&
                    ambiguous.conflicts[0].previous.size() == 2,
                "Duplicate primitive correspondence guessed by index");
        auto resolved = reconcile(
            *initial_twins.document, twins,
            {{"0", initial_twins.assignments.at("1")}, {"1", initial_twins.assignments.at("0")}});
        require(resolved.document.has_value(), "Explicit primitive mapping failed");
        auto no_evidence = reconcile(blank(), {observation("0", "mesh", "")});
        auto same_name = reconcile(*no_evidence.document, {observation("0", "mesh", "")});
        require(!same_name.document, "Name/index alone established persistent identity");

        auto extra = items;
        extra.push_back(observation("/meshes/9", "mesh", "new geometry", "new role"));
        auto added = reconcile(saved, extra);
        require(added.document.has_value() && added.document->entries.size() == items.size() + 1,
                "Adding an identifiable member failed");
        const auto duplicate = duplicate_subasset_identity(saved, "Assets/model-copy.gltf");
        require(duplicate.document.owner != saved.owner &&
                    duplicate.old_to_new.size() == saved.entries.size() + 1 &&
                    duplicate.document.unknown == saved.unknown,
                "Container duplication changed opaque data or reused IDs");
        for (const auto& old : saved.entries) {
            const auto& copy = entry(duplicate.document, duplicate.old_to_new.at(old.id));
            require(copy.id != old.id && copy.key != old.key && copy.unknown == old.unknown,
                    "Member duplication lost unknown data or reused identity");
        }

        rejects([&] { (void)reconcile_subassets(saved, digest("x"), "changed-schema", items); });
        rejects([&] { (void)reconcile(saved, items, {{"absent", std::nullopt}}); });
        rejects([&] { (void)reconcile(saved, items, {{items[0].address, AssetId::generate()}}); });
        rejects([&] {
            (void)reconcile(saved, items,
                            {{items[0].address, first.assignments.at(items[2].address)}});
        });
        rejects([&] {
            (void)reconcile(saved, items,
                            {{items[0].address, removed_id}, {items[1].address, removed_id}});
        });
        auto bad = items;
        bad[1].address = bad[0].address;
        rejects([&] { (void)reconcile(saved, bad); });
        bad = items;
        bad[0].evidence.content_digest = "invalid";
        rejects([&] { (void)reconcile(saved, bad); });
        auto invalid_doc = saved.document();
        invalid_doc["version"] = 2;
        rejects([&] { (void)SubassetIdentityDocument::parse(invalid_doc.dump()); });
        invalid_doc = saved.document();
        invalid_doc["entries"].push_back(invalid_doc["entries"][0]);
        rejects([&] { (void)SubassetIdentityDocument::parse(invalid_doc.dump()); });
        auto reserved = saved;
        reserved.unknown["owner"] = "shadow";
        rejects([&] { (void)reserved.document(); });
        auto deep = saved;
        nlohmann::json nested = 0;
        for (int i = 0; i < 70; ++i)
            nested = {{"next", std::move(nested)}};
        deep.unknown["nested"] = std::move(nested);
        rejects([&] { (void)deep.document(); });
        // Indexed matching and conflict aggregation at a meaningful catalog size.
        std::vector<SubassetObservation> many;
        for (unsigned i = 0; i < 10000; ++i)
            many.push_back(observation(std::to_string(i), "mesh", "content" + std::to_string(i)));
        auto large = reconcile(blank(), many);
        std::reverse(many.begin(), many.end());
        auto large_reimport = reconcile(*large.document, many);
        require(large_reimport.document.has_value() &&
                    large_reimport.assignments == large.assignments,
                "10k source reorder changed identity");
        std::cout
            << "Subasset identity matching, ambiguity, tombstones, duplication and limits passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
