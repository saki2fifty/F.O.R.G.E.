#pragma once
#include <cstdint>
#include <filesystem>
#include <forge/prefab.hpp>
#include <forge/scene_identity.hpp>
#include <forge/world.hpp>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace forge {
using Json = nlohmann::json;
class Scene {
  public:
    explicit Scene(WorldContext& context);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    flecs::world& world() const { return context_.world(); }
    flecs::entity entity(const std::string& id) const;
    AssetId asset_id() const { return context_.content_.at(membership_).asset; }
    flecs::entity_t membership() const { return membership_; }
    std::string canonical_id(const std::string& id) const { return resolve_legacy_id(opaque_, id); }
    EntityRef reference(const std::string& id) const;
    Json effective_document() const;
    // Detached command/gesture projection; never a mutable live representation.
    Json preview_document(const Json& intended) const;
    static void validate_document(const Json& document);
    Json document() const;
    std::uint64_t revision() const;
    void reset(const Json& document);
    std::size_t entity_count() const;
    Json schema() const;
    // Owner-thread authoring-world activation of copied worker metadata. Native
    // values/templates are staged before handoff; authored bytes and scene history
    // are preserved. This does not migrate schema-incompatible values.
    void publish_component_schemas(const Json& copied,
                                   const std::function<void()>& durable_write = {});
    const PrefabSources& prefab_sources() const { return prefab_sources_; }
    void set_prefab_sources(const PrefabSources& sources);
    // Owner-thread, single-asset publication. Durable write runs only after the
    // replacement hierarchy has been realized and validated. A throwing writer
    // leaves live instances, revision and history untouched.
    void publish_prefab_sources(const PrefabSources& sources,
                                const std::function<void()>& durable_write);
    Json snapshot() const;
    void restore_snapshot(const Json& snapshot);
    void replace(const Json& document);
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void edit(const Json& document);
    void rename_entity(const std::string& id, const std::string& name);
    void reparent_entity(const std::string& id, const std::string& parent,
                         ReparentMode mode = ReparentMode::PreserveWorld);
    std::string duplicate_subtree(const std::string& id);
    void delete_subtree(const std::string& id);
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    void translate(float x, float y, float z);

  private:
    WorldContext& context_;
    flecs::entity_t membership_;
    std::map<std::string, flecs::entity_t>& entities_;
    mutable std::uint64_t revision_ = 0, observed_serial_ = 0, observed_order_ = 0;
    Json opaque_;
    Json serialize(bool effective) const;
    void committed();
    std::uint64_t order_signature() const;
    void restore_child_order(const Json& document);
    void replace_prefab_sources(const PrefabSources&, const Json&, const std::function<void()>&,
                                bool, bool refresh_native_types = false);
    std::vector<Json> undo_, redo_;
    PrefabSources prefab_sources_;
    PrefabTemplates prefab_templates_;
};
void atomic_write(const std::filesystem::path& path, const std::string& contents);
} // namespace forge
