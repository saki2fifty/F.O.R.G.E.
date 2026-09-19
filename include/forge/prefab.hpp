#pragma once
#include <forge/world.hpp>
#include <memory>
namespace forge {
struct PrefabDocument {
    Json source;
    explicit PrefabDocument(Json document);
    AssetId asset() const { return source.at("asset_id").get<AssetId>(); }
    std::uint64_t revision() const { return source.at("revision").get<std::uint64_t>(); }
    PrefabMemberId root() const { return source.at("root").get<PrefabMemberId>(); }
    static void validate(const Json& document);
    PrefabDocument duplicate() const;
    PrefabDocument reorder_member(PrefabMemberId member, PrefabMemberId before) const;
};
// World-local immutable realization. Instances must die before their templates.
class CompiledPrefab {
  public:
    CompiledPrefab(WorldContext& world, PrefabDocument document);
    ~CompiledPrefab();
    CompiledPrefab(const CompiledPrefab&) = delete;
    CompiledPrefab& operator=(const CompiledPrefab&) = delete;
    const PrefabDocument document;
    flecs::entity root() const { return members_.at(document.root()); }
    const std::map<PrefabMemberId, flecs::entity>& members() const { return members_; }

  private:
    WorldContext& context_;
    std::map<PrefabMemberId, flecs::entity> members_;
};
using PrefabSources = std::map<AssetId, Json>;
using PrefabTemplates = std::map<AssetId, std::shared_ptr<CompiledPrefab>>;
// Detached authoring preparation only. Live effective values remain in Flecs.
Json reconcile_prefab_intent(const Json& scene, const PrefabSources& sources);
Json project_prefab_intent(const Json& scene, const PrefabSources& sources);
void validate_prefab_instances(const Json& scene);
// Resolve a known typed definition reference through one explicit instance.
// The resulting EntityRef may remain unresolved when its member was removed.
std::optional<EntityRef> prefab_member_reference(const Json& scene, EntityId instance,
                                                 PrefabMemberRef reference);
PrefabMemberRef
remap_prefab_member_reference(PrefabMemberRef reference, AssetId old_asset, AssetId new_asset,
                              const std::map<PrefabMemberId, PrefabMemberId>& members);
void remap_prefab_instances(Json& scene, const std::map<EntityId, EntityId>& remap);
Json prefab_override_value(const Json& member, const Json& instance);
} // namespace forge
