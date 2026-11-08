#pragma once
#include <forge/world.hpp>
#include <memory>
#include <vector>
namespace forge {
struct PrefabDocument {
    static constexpr unsigned current_version = 6;
    static constexpr bool supports_version(unsigned version) {
        return version >= 1 && version <= current_version;
    }
    Json source;
    explicit PrefabDocument(Json document);
    AssetId asset() const { return source.at("asset_id").get<AssetId>(); }
    std::uint64_t revision() const { return source.at("revision").get<std::uint64_t>(); }
    PrefabMemberId root() const { return source.at("root").get<PrefabMemberId>(); }
    std::vector<AssetId> dependencies() const;
    static void validate(const Json& document);
    static bool is_variant(const Json& document);
    // Only arrays whose meaning belongs to the selected source version.
    static std::vector<const char*> authored_member_arrays(const Json& document);
    // Explicit source5 opt-in; old opaque fields are never silently interpreted.
    PrefabDocument with_component_suppression(PrefabMemberId member, const std::string& component,
                                              bool suppressed,
                                              const Json& schema = Json::object()) const;
    PrefabDocument with_added_member(Json member) const;
    PrefabDocument with_member_parent(PrefabMemberId member, PrefabMemberId parent) const;
    PrefabDocument with_member_spatial(PrefabMemberId member, Json binding) const;
    PrefabDocument revert_member_structure(PrefabMemberId member, const std::string& field) const;
    PrefabDocument with_member_removed(PrefabMemberId member, bool removed) const;
    PrefabDocument with_member_order(PrefabMemberId parent,
                                     const std::vector<PrefabMemberId>& prefix) const;
    PrefabDocument duplicate(const Json& schema = Json::object()) const;
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
// Detached source preparation. Raw documents retain authority; variant values
// are admitted before the existing immutable template compiler consumes them.
PrefabSources resolve_prefab_sources(const PrefabSources&, const Json& schema = Json::object());
// Detached authoring preparation only. Live effective values remain in Flecs.
Json reconcile_prefab_intent(const Json& scene, const PrefabSources& sources,
                             const Json& schema = Json::object());
Json reconcile_prefab_intent(const Json& scene, const PrefabTemplates& templates,
                             const Json& schema = Json::object());
Json project_prefab_intent(const Json& scene, const PrefabSources& sources,
                           const Json& schema = Json::object());
Json project_prefab_intent(const Json& scene, const PrefabTemplates& templates,
                           const Json& schema = Json::object());
void validate_prefab_instances(const Json& scene);
// Resolve a known typed definition reference through one explicit instance.
// The resulting EntityRef may remain unresolved when its member was removed.
std::optional<EntityRef> prefab_member_reference(const Json& scene, EntityId instance,
                                                 PrefabMemberRef reference);
PrefabMemberRef
remap_prefab_member_reference(PrefabMemberRef reference, AssetId old_asset, AssetId new_asset,
                              const std::map<PrefabMemberId, PrefabMemberId>& members);
void remap_prefab_instances(Json& scene, const std::map<EntityId, EntityId>& remap);
Json prefab_override_value(const Json& member, const Json& instance,
                           const Json& schema = Json::object());
} // namespace forge
