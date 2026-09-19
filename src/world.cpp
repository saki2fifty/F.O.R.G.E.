#include "builtins.hpp"
#include <forge/world.hpp>
#include <stdexcept>
#include <vector>
namespace forge {
namespace {
struct RegistrationScope {
    ecs_world_t* world;
    ecs_entity_t previous;
    explicit RegistrationScope(flecs::world& w)
        : world(w.c_ptr()), previous(ecs_set_scope(world, 0)) {}
    ~RegistrationScope() { ecs_set_scope(world, previous); }
};
struct CoreRegistration {
    Json schema;
    explicit CoreRegistration(flecs::world& world) {
        world.module<CoreRegistration>();
        RegistrationScope scope(world);
        schema = detail::register_builtins(world);
        world.component<PersistentEntityId>("forge.entity_id")
            .add(flecs::OnInstantiate, flecs::DontInherit);
        world.component<StableId>("forge.stable_id").add(flecs::OnInstantiate, flecs::DontInherit);
        world.component<AuthoredName>("forge.authored_name")
            .add(flecs::OnInstantiate, flecs::DontInherit);
        world.component<SceneMember>("forge.scene_member")
            .add(flecs::Exclusive)
            .add(flecs::OnInstantiate, flecs::DontInherit)
            .add(flecs::OnDeleteTarget, flecs::Delete);
    }
};
struct TransformsRegistration {
    explicit TransformsRegistration(flecs::world& world) {
        world.module<TransformsRegistration>();
        RegistrationScope scope(world);
        world.component<WorldTransform>("forge.world_transform")
            .add(flecs::OnInstantiate, flecs::DontInherit);
        world.component<SpatialBinding>("forge.spatial_binding")
            .add(flecs::OnInstantiate, flecs::Override);
        world.component<MissingStructuralParent>().add(flecs::OnInstantiate, flecs::DontInherit);
    }
};
struct PrefabsRegistration {
    explicit PrefabsRegistration(flecs::world& world) {
        world.module<PrefabsRegistration>();
        RegistrationScope scope(world);
        world.component<TemplateMember>("forge.prefab_member_definition")
            .add(flecs::OnInstantiate, flecs::Inherit);
        world.component<AuthoredPrefab>("forge.authored_prefab")
            .add(flecs::OnInstantiate, flecs::DontInherit);
    }
};
struct PhysicsRegistration {
    Json schema;
    explicit PhysicsRegistration(flecs::world& world) {
        world.module<PhysicsRegistration>();
        RegistrationScope scope(world);
        schema = detail::register_builtins(world, true);
    }
};
struct AudioRegistration {
    Json schema;
    explicit AudioRegistration(flecs::world& world) {
        world.module<AudioRegistration>();
        RegistrationScope scope(world);
        schema = detail::register_builtins(world, 2);
    }
};
struct AnimationRegistration {
    Json schema;
    explicit AnimationRegistration(flecs::world& world) {
        world.module<AnimationRegistration>();
        RegistrationScope scope(world);
        schema = detail::register_builtins(world, 3);
    }
};
struct NavigationRegistration {
    Json schema;
    explicit NavigationRegistration(flecs::world& world) {
        world.module<NavigationRegistration>();
        RegistrationScope scope(world);
        schema = detail::register_builtins(world, 4);
    }
};
struct UiRegistration {
    Json schema;
    explicit UiRegistration(flecs::world& world) {
        world.module<UiRegistration>();
        RegistrationScope scope(world);
        schema = detail::register_builtins(world, 5);
    }
};
struct InputRegistration {
    explicit InputRegistration(flecs::world& world) {
        world.module<InputRegistration>();
        RegistrationScope scope(world);
        world.component<FixedSimulation>("forge.runtime.FixedSimulation");
    }
};
std::vector<EngineModule> built_in_modules() {
    std::vector<EngineModule> result;
    auto add = [&](const char* id, std::vector<std::string> dependencies, auto schemas) {
        EngineModule m;
        m.id = id;
        m.dependencies = std::move(dependencies);
        m.schemas = schemas;
        result.push_back(std::move(m));
    };
    add("forge.core", {}, [](ModuleContext& c) { c.world.import<CoreRegistration>(); });
    add("forge.transforms", {"forge.core"},
        [](ModuleContext& c) { c.world.import<TransformsRegistration>(); });
    add("forge.prefabs", {"forge.transforms"},
        [](ModuleContext& c) { c.world.import<PrefabsRegistration>(); });
    add("forge.input", {"forge.core"},
        [](ModuleContext& c) { c.world.import<InputRegistration>(); });
    return result;
}
} // namespace
EngineModule physics_schema_module() {
    EngineModule result;
    result.id = "forge.physics";
    result.dependencies = {"forge.core", "forge.transforms"};
    result.schemas = [](ModuleContext& c) { c.world.import<PhysicsRegistration>(); };
    return result;
}
EngineModule audio_schema_module() {
    EngineModule result;
    result.id = "forge.audio";
    result.dependencies = {"forge.core", "forge.transforms"};
    result.schemas = [](ModuleContext& c) { c.world.import<AudioRegistration>(); };
    return result;
}
EngineModule animation_schema_module() {
    EngineModule result;
    result.id = "forge.animation";
    result.dependencies = {"forge.core", "forge.transforms"};
    result.schemas = [](ModuleContext& c) { c.world.import<AnimationRegistration>(); };
    return result;
}
EngineModule navigation_schema_module() {
    EngineModule result;
    result.id = "forge.navigation";
    result.dependencies = {"forge.core", "forge.transforms"};
    result.schemas = [](ModuleContext& c) { c.world.import<NavigationRegistration>(); };
    return result;
}
EngineModule ui_schema_module() {
    EngineModule result;
    result.id = "forge.ui";
    result.dependencies = {"forge.core"};
    result.schemas = [](ModuleContext& c) { c.world.import<UiRegistration>(); };
    return result;
}
WorldContext::WorldContext(WorldRole role, ServiceAccess services,
                           std::vector<EngineModule> modules)
    : services_(services.world_scope()), role_(role) {
    // C addon tags retain process-global IDs. Register these in a consistent
    // order before FORGE/content allocations in every host world; late imports
    // can collide with entities already allocated in a different world.
    world_.import<flecs::stats>();
    world_.import<flecs::metrics>();
    world_.import<flecs::alerts>();
    auto composition = built_in_modules();
    if (std::none_of(modules.begin(), modules.end(),
                     [](const auto& m) { return m.id == "forge.physics"; }))
        composition.push_back(physics_schema_module());
    if (std::none_of(modules.begin(), modules.end(),
                     [](const auto& m) { return m.id == "forge.audio"; }))
        composition.push_back(audio_schema_module());
    if (std::none_of(modules.begin(), modules.end(),
                     [](const auto& m) { return m.id == "forge.animation"; }))
        composition.push_back(animation_schema_module());
    if (std::none_of(modules.begin(), modules.end(),
                     [](const auto& m) { return m.id == "forge.navigation"; }))
        composition.push_back(navigation_schema_module());
    if (std::none_of(modules.begin(), modules.end(),
                     [](const auto& m) { return m.id == "forge.ui"; }))
        composition.push_back(ui_schema_module());
    for (auto& module : modules)
        composition.push_back(std::move(module));
    modules_.bootstrap(world_, role_, services_, std::move(composition), this);
    try {
        schema_ = world_.import<CoreRegistration>().get<CoreRegistration>().schema;
        for (const auto& c :
             world_.import<PhysicsRegistration>().get<PhysicsRegistration>().schema.at(
                 "components"))
            schema_["components"].push_back(c);
        for (const auto& c :
             world_.import<AudioRegistration>().get<AudioRegistration>().schema.at("components"))
            schema_["components"].push_back(c);
        for (const auto& c :
             world_.import<AnimationRegistration>().get<AnimationRegistration>().schema.at(
                 "components"))
            schema_["components"].push_back(c);
        for (const auto& c :
             world_.import<NavigationRegistration>().get<NavigationRegistration>().schema.at(
                 "components"))
            schema_["components"].push_back(c);
        for (const auto& c :
             world_.import<UiRegistration>().get<UiRegistration>().schema.at("components"))
            schema_["components"].push_back(c);
        local_transforms_ = world_.query_builder<const LocalTranslation>()
                                .cache_kind(flecs::QueryCacheNone)
                                .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                                .build();
        derived_transforms_ = world_.query_builder<const WorldTransform>()
                                  .cache_kind(flecs::QueryCacheAuto)
                                  .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                                  .build();
        // Revision invalidation includes direct native writes, removal and relation edits.
        // Internal observation only: application notifications remain post-commit.
        world_.observer()
            .with(flecs::Wildcard)
            .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
            .event(flecs::OnAdd)
            .event(flecs::OnSet)
            .event(flecs::OnRemove)
            .each([this](flecs::iter& event, size_t row) {
                if (evaluating_transforms_)
                    return;
                const auto e = event.entity(row);
                const ecs_id_t changed = event.event_id();
                const bool relation = ecs_id_is_pair(changed) &&
                                      (ecs_pair_first(world_, changed) == flecs::ChildOf ||
                                       ecs_pair_first(world_, changed) == flecs::IsA ||
                                       ecs_pair_first(world_, changed) == world_.id<SceneMember>());
                if (relation || changed == ecs_id(EcsParent) ||
                    changed == world_.id<PersistentEntityId>() ||
                    changed == world_.id<LocalTranslation>() ||
                    changed == world_.id<LocalRotation>() || changed == world_.id<LocalScale>() ||
                    changed == world_.id<SpatialBinding>() ||
                    changed == world_.id<MissingStructuralParent>())
                    ++transform_epoch_;
                auto it = content_.find(owner_of(e));
                if (it != content_.end())
                    ++it->second.serial;
            });
    } catch (...) {
        modules_.stop();
        throw;
    }
}
WorldContext::~WorldContext() { modules_.stop(); }
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
    local_transforms_.each([&](flecs::entity e, const LocalTranslation&) {
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
        n.parent_resolved = parent.resolved && !(b.mode == SpatialMode::FollowStructure &&
                                                 e.has<MissingStructuralParent>());
        nodes.emplace(e.id(), n);
    });
    return nodes;
}
void WorldContext::evaluate_world_transforms() {
    if (evaluated_epoch_ == transform_epoch_)
        return;
    auto profile = services_.profile("ecs", "TransformEvaluation");
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
        derived_transforms_.each([&](flecs::entity e, const WorldTransform&) {
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
    auto root = world_.entity().add(flecs::OrderedChildren);
    content_.emplace(root.id(), Content{});
    return root.id();
}
void WorldContext::detach(flecs::entity_t root) {
    world_.entity(root).destruct(); // SceneMember target cleanup deletes only owned content.
    content_.erase(root);
}
} // namespace forge
