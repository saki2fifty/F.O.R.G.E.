#include "builtins.hpp"
#include <forge/world.hpp>
namespace forge {
WorldContext::WorldContext(WorldRole role) : role_(role) {
    schema_ = detail::register_builtins(world_);
    world_.component<StableId>("forge.stable_id").add(flecs::OnInstantiate, flecs::DontInherit);
    world_.component<AuthoredName>("forge.authored_name")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world_.component<AuthoredPrefab>("forge.authored_prefab")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world_.component<SceneMember>("forge.scene_member")
        .add(flecs::Exclusive)
        .add(flecs::OnInstantiate, flecs::DontInherit)
        .add(flecs::OnDeleteTarget, flecs::Delete);
    // Revision invalidation includes direct native writes, removal and relation edits.
    // Internal observation only: application notifications remain post-commit.
    world_.observer()
        .with(flecs::Wildcard)
        .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
        .event(flecs::OnAdd)
        .event(flecs::OnSet)
        .event(flecs::OnRemove)
        .each([this](flecs::entity e) {
            auto it = content_.find(owner_of(e));
            if (it != content_.end())
                ++it->second.serial;
        });
}
WorldContext::~WorldContext() = default;
flecs::entity_t WorldContext::owner_of(flecs::entity entity) const {
    for (auto current = entity; current; current = current.target(flecs::ChildOf)) {
        auto owner = current.target<SceneMember>();
        if (owner)
            return owner.id();
    }
    return 0;
}
flecs::entity_t WorldContext::attach() {
    auto root = world_.entity();
    content_.emplace(root.id(), Content{});
    return root.id();
}
void WorldContext::detach(flecs::entity_t root) {
    world_.entity(root).destruct(); // SceneMember target cleanup deletes only owned content.
    content_.erase(root);
}
} // namespace forge
