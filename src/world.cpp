#include "builtins.hpp"
#include <forge/world.hpp>
#include <stdexcept>
#include <vector>
namespace forge {
WorldContext::WorldContext(WorldRole role) : role_(role) {
    schema_ = detail::register_builtins(world_);
    world_.component<WorldTransform>("forge.world_transform")
        .add(flecs::OnInstantiate, flecs::DontInherit);
    world_.component<SpatialBinding>("forge.spatial_binding")
        .add(flecs::OnInstantiate, flecs::Override);
    world_.component<PersistentEntityId>("forge.entity_id")
        .add(flecs::OnInstantiate, flecs::DontInherit);
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
            if (evaluating_transforms_)
                return;
            ++transform_epoch_;
            auto it = content_.find(owner_of(e));
            if (it != content_.end())
                ++it->second.serial;
        });
}
WorldContext::~WorldContext() = default;
LocalTransform WorldContext::get_local_transform(flecs::entity e) const {
    LocalTransform result;
    if (e.has<LocalTranslation>())
        result.translation = e.get<LocalTranslation>();
    if (e.has<LocalRotation>())
        result.rotation = e.get<LocalRotation>();
    if (e.has<LocalScale>())
        result.scale = e.get<LocalScale>();
    return result;
}
std::map<std::uint64_t, TransformNode> WorldContext::collect_transforms() const {
    std::map<std::uint64_t, TransformNode> nodes;
    auto query = world_.query_builder<const LocalTranslation>()
                     .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                     .build();
    query.each([&](flecs::entity e, const LocalTranslation&) {
        TransformNode n{get_local_transform(e)};
        const auto b = e.has<SpatialBinding>() ? e.get<SpatialBinding>() : SpatialBinding{};
        auto p = e.target(flecs::ChildOf);
        std::uint64_t structural = p && p.has<LocalTranslation>() ? p.id() : 0, explicit_target = 0;
        if (b.mode == SpatialMode::Explicit) {
            const auto resolved = resolve(b.target, owner_of(e));
            if (resolved.state == ResolveState::Available &&
                world_.entity(resolved.entity).has<LocalTranslation>())
                explicit_target = resolved.entity;
        }
        const auto parent = effective_spatial_parent(b.mode, structural, explicit_target);
        n.parent = parent.entity;
        n.parent_resolved = parent.resolved;
        nodes.emplace(e.id(), n);
    });
    return nodes;
}
void WorldContext::evaluate_world_transforms() {
    if (evaluated_epoch_ == transform_epoch_)
        return;
    const auto nodes = collect_transforms();
    const auto& evaluated = transform_evaluator_.evaluate(nodes);
    evaluating_transforms_ = true;
    try {
        for (const auto& [id, v] : evaluated) {
            auto e = world_.entity(id);
            if (!e.owns<WorldTransform>())
                e.set<WorldTransform>({v.affine, v.resolved, 1});
            else {
                const auto previous = e.get<WorldTransform>();
                if (previous.affine != v.affine || previous.resolved != v.resolved)
                    e.set<WorldTransform>({v.affine, v.resolved, previous.revision + 1});
            }
        }
        // Retire a derived value if its entity loses its effective translation.
        std::vector<flecs::entity> stale;
        auto derived = world_.query_builder<const WorldTransform>()
                           .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                           .build();
        derived.each([&](flecs::entity e, const WorldTransform&) {
            if (!nodes.contains(e.id()))
                stale.push_back(e);
        });
        for (auto e : stale)
            e.remove<WorldTransform>();
        evaluated_epoch_ = transform_epoch_;
        evaluating_transforms_ = false;
    } catch (...) {
        evaluating_transforms_ = false;
        throw;
    }
}

void WorldContext::translate_content(flecs::entity_t membership, Double3 delta) {
    evaluate_world_transforms();
    const auto nodes = collect_transforms();
    std::map<flecs::entity_t, LocalTranslation> pending;
    // Include generated prefab interiors, which deliberately have no authored JSON row.
    auto moves = [&](flecs::entity_t id) {
        auto e = world_.entity(id);
        return owner_of(e) == membership && !e.has(flecs::Prefab);
    };
    for (const auto& [id, node] : nodes) {
        if (!moves(id))
            continue;
        auto e = world_.entity(id);
        const auto value = e.get<WorldTransform>();
        if (!value.resolved)
            throw std::runtime_error("Runtime translation has an unresolved spatial parent");
        AffineTransform parent;
        if (node.parent) {
            parent = world_.entity(node.parent).get<WorldTransform>().affine;
            if (moves(node.parent))
                for (unsigned i = 0; i < 3; ++i)
                    parent.m[4 * i + 3] += delta[i];
        }
        const auto point =
            inverse(parent).point({value.affine.m[3] + delta[0], value.affine.m[7] + delta[1],
                                   value.affine.m[11] + delta[2]});
        auto local = node.local;
        local.translation = {point[0], point[1], point[2]};
        (void)affine_transform(local); // Validate every result before mutating any entity.
        if (!equivalent(node.local.translation, local.translation))
            pending.emplace(id, local.translation);
    }
    for (const auto& [id, translation] : pending)
        world_.entity(id).set<LocalTranslation>(translation);
}

WorldContext::Resolution WorldContext::resolve(EntityRef ref, flecs::entity_t membership) const {
    Resolution result{ResolveState::Unresolved};
    unsigned matches = 0;
    for (const auto& [root, content] : content_) {
        if (membership && root != membership)
            continue;
        if (content.asset != ref.scene)
            continue;
        if (++matches > 1)
            return {ResolveState::Ambiguous};
        result = {ResolveState::Missing};
        const auto it = content.persistent.find(ref.entity);
        if (it != content.persistent.end() && world_.is_alive(it->second)) {
            auto e = world_.entity(it->second);
            if (e.owns<PersistentEntityId>() && e.get<PersistentEntityId>().value == ref.entity)
                result = {ResolveState::Available, it->second};
        }
    }
    return result;
}
std::optional<EntityRef> WorldContext::reference(flecs::entity_t handle) const {
    if (!world_.is_alive(handle))
        return {};
    auto e = world_.entity(handle);
    if (!e.owns<PersistentEntityId>())
        return {};
    auto it = content_.find(owner_of(e));
    if (it == content_.end() || !it->second.asset)
        return {};
    return EntityRef{it->second.asset, e.get<PersistentEntityId>().value};
}
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
