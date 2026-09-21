#include "builtins.hpp"
#include "relationship_graph.hpp"
#include "scene_draft.hpp"
#include "spatial_document.hpp"
#include <array>
#include <cmath>
#include <forge/primitive_catalog.hpp>
#include <forge/scene.hpp>
#include <forge/scene_render_settings.hpp>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace forge {
namespace {
void validate(const Json& doc) {
    if (!doc.is_object() || !doc.contains("version") || !doc.at("version").is_number_integer() ||
        (doc.at("version") != 1 && doc.at("version") != 2 && doc.at("version") != 3 &&
         doc.at("version") != 4 && doc.at("version") != 5) ||
        !doc.contains("entities") || !doc.at("entities").is_array())
        throw std::runtime_error("Expected scene version 1, 2, 3, 4 or 5 and an entities array");
    if (doc.at("version") != 1) {
        (void)doc.at("asset_id").get<AssetId>();
        if (doc.contains("legacy_ids")) {
            if (!doc.at("legacy_ids").is_object())
                throw std::runtime_error("Invalid legacy alias map");
            for (const auto& [alias, target] : doc.at("legacy_ids").items()) {
                if (alias.empty())
                    throw std::runtime_error("Empty legacy alias");
                (void)target.get<EntityId>();
            }
        }
    }
    (void)scene_render_settings(doc);
    validate_prefab_instances(doc);
    std::map<std::string, const Json*> entities;
    for (const auto& e : doc.at("entities")) {
        const auto id = e.at("id").get<std::string>();
        if (doc.at("version") != 1)
            (void)EntityId::parse(id);
        if (id.empty() || !entities.emplace(id, &e).second)
            throw std::runtime_error("Empty or duplicate entity ID");
        if (!e.at("name").is_string() || !e.at("components").is_object())
            throw std::runtime_error("Invalid entity name/components");
        if (e.contains("prefab") && !e.at("prefab").is_boolean())
            throw std::runtime_error("Invalid prefab flag");
        if (doc.at("version") >= 3)
            detail::validate_components(e.at("components"));
        else {
            const auto& c = e.at("components");
            for (auto name : {"forge.position", "forge.rotation", "forge.scale", "forge.tint",
                              "forge.primitive"}) {
                if (!c.contains(name))
                    continue;
                for (auto field : std::string(name) == "forge.primitive"
                                      ? std::vector<const char*>{"kind"}
                                  : std::string(name) == "forge.tint"
                                      ? std::vector<const char*>{"r", "g", "b"}
                                      : std::vector<const char*>{"x", "y", "z"}) {
                    const auto& v = c.at(name).at(field);
                    if (!v.is_number() || !std::isfinite(v.get<float>()))
                        throw std::runtime_error("Invalid legacy component value");
                    const float n = v.get<float>();
                    if ((std::string(name) == "forge.rotation" && std::abs(n) > 360000) ||
                        (std::string(name) == "forge.scale" && (n < .001f || n > 10000)) ||
                        (std::string(name) == "forge.tint" && (n < 0 || n > 1)) ||
                        (std::string(name) == "forge.primitive" &&
                         (!v.is_number_integer() || n < 0 || n >= primitive_count)))
                        throw std::runtime_error("Legacy component outside range");
                }
            }
        }
    }
    if (doc.at("version") != 1 && doc.contains("legacy_ids"))
        for (const auto& [alias, target] : doc.at("legacy_ids").items())
            if (entities.contains(alias) && target != alias)
                throw std::runtime_error("Legacy alias shadows a persistent entity ID");
    for (const char* relation : {"parent", "base"}) {
        detail::RelationshipGraph graph;
        for (const auto& [id, row] : entities) {
            auto& edges = graph[id];
            if (!row->contains(relation))
                continue;
            const auto target = row->at(relation).get<std::string>();
            if (!entities.contains(target))
                throw std::runtime_error("Missing relationship target");
            if (std::string(relation) == "base" && !entities.at(target)->value("prefab", false))
                throw std::runtime_error("Base must be a prefab");
            edges.push_back({target});
        }
        // Scene ownership adds one native structural level. OrderedChildren
        // allocates a child record even for leaves, so that reserved level must
        // count before realizing or reparenting the deepest authored entity.
        detail::validate_relationship_graph(graph, relation, std::string(relation) == "parent");
    }
    for (const auto& [id, row] : entities)
        if (row->contains("prefab_instance")) {
            auto parent = row->value("parent", std::string{});
            while (!parent.empty()) {
                const auto& ancestor = *entities.at(parent);
                if (ancestor.value("prefab", false))
                    throw std::runtime_error(
                        "Structured instances inside legacy prefab definitions are unsupported");
                parent = ancestor.value("parent", std::string{});
            }
        }
    // Prefab expansion follows base links and then child links. Validate their
    // combined graph before Flecs can attempt recursive child instantiation.
    detail::RelationshipGraph expansion;
    for (const auto& [id, e] : entities) {
        expansion.try_emplace(id);
        if (e->contains("base"))
            expansion[id].push_back({e->at("base").get<std::string>(), false});
        if (e->contains("parent"))
            expansion[e->at("parent").get<std::string>()].push_back({id});
    }
    detail::validate_relationship_graph(expansion, "Prefab expansion", 1);
    if (doc.at("version") >= 3)
        detail::validate_spatial(doc);
}
Json& find_entity(Json& doc, const std::string& id) {
    for (auto& entity : doc["entities"])
        if (entity.at("id") == id)
            return entity;
    throw std::runtime_error("Entity no longer exists: " + id);
}
std::set<std::string> subtree(Json& doc, const std::string& id) {
    find_entity(doc, id);
    std::set<std::string> ids{id};
    bool changed;
    do {
        changed = false;
        for (const auto& e : doc["entities"])
            if (ids.contains(e.value("parent", std::string{})))
                changed |= ids.insert(e.at("id").get<std::string>()).second;
    } while (changed);
    return ids;
}
} // namespace
Scene::Scene(WorldContext& context)
    : context_(context), membership_(context.attach()),
      entities_(context.content_.at(membership_).entities), opaque_(empty_scene()) {
    context_.content_.at(membership_).asset = opaque_.at("asset_id").get<AssetId>();
    committed();
}
Scene::~Scene() { context_.detach(membership_); }
void Scene::validate_document(const Json& doc) { validate(doc); }
std::uint64_t Scene::order_signature() const {
    // Native set_child_order is immediate and does not emit OnSet in this pin.
    // This derived fingerprint invalidates UI caches without owning another order.
    std::uint64_t result = 14695981039346656037ull;
    auto mix = [&](ecs_entity_t id) { result = (result ^ id) * 1099511628211ull; };
    auto append = [&](ecs_entity_t parent) {
        auto e = world().entity(parent);
        if (!e.is_alive() || !e.has(flecs::OrderedChildren))
            return;
        mix(parent);
        const auto children = ecs_get_ordered_children(world(), parent);
        for (int32_t i = 0; i < children.count; ++i)
            mix(children.ids[i]);
    };
    append(membership_);
    for (const auto& [id, handle] : entities_) {
        (void)id;
        append(handle);
    }
    return result;
}
std::uint64_t Scene::revision() const {
    const auto serial = context_.content_.at(membership_).serial;
    const auto order = order_signature();
    if (serial != observed_serial_ || order != observed_order_) {
        observed_serial_ = serial;
        observed_order_ = order;
        ++revision_;
    }
    return revision_;
}
void Scene::committed() {
    observed_order_ = order_signature();
    observed_serial_ = context_.content_.at(membership_).serial;
    ++revision_;
}
flecs::entity Scene::entity(const std::string& id) const {
    const auto it = entities_.find(canonical_id(id));
    return world().entity(it == entities_.end() || !world().is_alive(it->second) ? 0 : it->second);
}
EntityRef Scene::reference(const std::string& id) const {
    const auto target = entity(id);
    if (!target)
        throw std::runtime_error("Entity no longer exists: " + id);
    return {asset_id(), target.get<PersistentEntityId>().value};
}
std::size_t Scene::entity_count() const {
    std::size_t count = 0;
    for (const auto& [id, handle] : entities_) {
        (void)id;
        count += world().is_alive(handle);
    }
    return count;
}
void Scene::replace(const Json& source) {
    validate(source);
    auto doc = source.at("version") < 3 ? migrate_scene(source, &opaque_) : source;
    detail::promote_scale_format(doc, "entities", 5);
    struct Intended {
        std::string id, name, parent, base;
        bool prefab;
        SpatialBinding spatial;
        std::array<std::optional<detail::Value>, detail::builtin_count> values;
    };
    // All parsing/type conversion/opaque copies happen before the first world write.
    auto opaque = doc;
    auto reconciled = reconcile_prefab_intent(doc, prefab_sources_);
    if (reconciled.at("entities").size() != doc.at("entities").size())
        throw std::runtime_error(
            "Instance member rows cannot be removed independently; edit the prefab source");
    for (const auto& item : doc.at("entities"))
        if (item.contains("prefab_member")) {
            const auto& expected = find_entity(reconciled, item.at("id"));
            for (auto key : {"name", "parent", "spatial", "missing_member"})
                if (item.value(key, Json()) != expected.value(key, Json()))
                    throw std::runtime_error(
                        "Edit structured member names/hierarchy/bindings in the prefab source");
        }
    const auto projected = project_prefab_intent(doc, prefab_sources_);
    detail::validate_spatial(projected);
    std::map<std::string, flecs::entity> structured_bases;
    std::set<std::string> inactive_members;
    for (const auto& item : doc.at("entities")) {
        const Json* root = nullptr;
        std::string member;
        if (item.contains("prefab_instance"))
            root = &item;
        else if (item.contains("prefab_member")) {
            for (const auto& r : doc.at("entities"))
                if (r.at("id") == item.at("prefab_member").at("root"))
                    root = &r;
            member = item.at("prefab_member").at("member");
        }
        if (!root)
            continue;
        const auto asset = root->at("prefab_instance").at("asset").get<AssetId>();
        if (!prefab_templates_.contains(asset) || item.value("missing_member", false)) {
            if (item.contains("prefab_member"))
                inactive_members.insert(item.at("id"));
            continue;
        }
        const auto& compiled = *prefab_templates_.at(asset);
        if (member.empty())
            member = compiled.document.root().str();
        auto found = compiled.members().find(PrefabMemberId::parse(member));
        if (found == compiled.members().end()) {
            inactive_members.insert(item.at("id"));
            continue;
        }
        structured_bases.emplace(item.at("id"), found->second);
    }
    std::vector<Intended> intended;
    std::set<std::string> retained;
    for (auto& item : opaque["entities"]) {
        if (inactive_members.contains(item.at("id")))
            continue;
        Intended next{item.at("id"),
                      item.at("name"),
                      item.value("parent", std::string{}),
                      item.value("base", std::string{}),
                      item.value("prefab", false),
                      detail::read_binding(item),
                      {}};
        retained.insert(next.id);
        auto& components = item["components"];
        auto materialized = components;
        if (item.contains("property_overrides")) {
            for (const auto& projected_row : projected.at("entities"))
                if (projected_row.at("id") == item.at("id"))
                    for (const auto& [component, fields] : item.at("property_overrides").items()) {
                        (void)fields;
                        if (projected_row.at("components").contains(component))
                            materialized[component] = projected_row.at("components").at(component);
                    }
        }
        for (std::size_t i = 0; i < detail::builtins().size(); ++i) {
            const auto& type = detail::builtins()[i];
            if (!materialized.contains(type.name))
                continue;
            next.values[i] = type.decode(materialized.at(type.name));
            if (!components.contains(type.name))
                continue;
            auto extensions = detail::builtin_extensions(type, components.at(type.name));
            if (extensions.is_null())
                components.erase(type.name);
            else
                components[type.name] = std::move(extensions);
        }
        item.erase("name");
        if (!inactive_members.contains(next.parent))
            item.erase("parent");
        item.erase("base");
        if (item.contains("spatial")) {
            item["spatial"].erase("mode");
            item["spatial"].erase("target");
        }
        // Keep explicit false presence, never its live truth.
        if (item.value("prefab", false))
            item.erase("prefab");
        intended.push_back(std::move(next));
    }
    // Existing v1 ChildOf prefab children are copied by Flecs at instantiation.
    // Reconcile affected generated interiors when their source changes; preserve
    // every surviving authored handle. This is not a new prefab asset workflow.
    const auto previous = document();
    std::map<std::string, const Json*> old_items, new_items;
    for (const auto& item : previous.at("entities"))
        old_items[item.at("id")] = &item;
    for (const auto& item : doc.at("entities"))
        new_items[item.at("id")] = &item;
    std::set<std::string> dirty_templates;
    auto dirty_ancestors = [&](const auto& items, std::string id) {
        while (!id.empty()) {
            auto it = items.find(id);
            if (it == items.end())
                break;
            if (it->second->value("prefab", false))
                dirty_templates.insert(id);
            id = it->second->value("parent", std::string{});
        }
    };
    for (const auto& [id, item] : old_items)
        if (!new_items.contains(id) || *item != *new_items.at(id)) {
            dirty_ancestors(old_items, id);
            dirty_ancestors(new_items, id);
        }
    for (const auto& [id, item] : new_items)
        if (!old_items.contains(id)) {
            (void)item;
            dirty_ancestors(new_items, id);
        }
    std::set<std::string> refresh;
    for (const auto& next : intended) {
        auto base = next.base;
        while (!base.empty()) {
            if (dirty_templates.contains(base)) {
                refresh.insert(next.id);
                break;
            }
            base = new_items.at(base)->value("base", std::string{});
        }
    }
    // Detach surviving entities before deleting former parents. Also remove obsolete
    // IsA references before a removed base can trigger Flecs target cleanup.
    for (const auto& next : intended) {
        auto e = entity(next.id);
        if (!e || !e.is_alive())
            continue;
        const auto parent = e.target(flecs::ChildOf);
        const auto desired_parent =
            next.parent.empty() ? world().entity(membership_) : entity(next.parent);
        if (parent && parent != desired_parent && !new_items.at(next.id)->contains("prefab_member"))
            e.remove(flecs::ChildOf, parent);
        const auto base = e.target(flecs::IsA);
        const auto desired_base =
            structured_bases.contains(next.id) ? structured_bases.at(next.id) : entity(next.base);
        if (base && (base != desired_base || refresh.contains(next.id))) {
            std::vector<flecs::entity> generated;
            e.children([&](flecs::entity child) {
                if (!child.target<SceneMember>())
                    generated.push_back(child);
            });
            for (auto child : generated)
                child.destruct();
            e.remove(flecs::IsA, base);
        }
    }
    std::set<std::string> rebuild_roots;
    for (const auto& item : doc.at("entities"))
        if (item.contains("prefab_instance") && structured_bases.contains(item.at("id"))) {
            auto e = entity(item.at("id"));
            if (!e || e.target(flecs::IsA) != structured_bases.at(item.at("id")))
                rebuild_roots.insert(item.at("id"));
        }
    for (const auto& next : intended) {
        auto e = entity(next.id);
        if (e && e.is_alive() && !new_items.at(next.id)->contains("prefab_member")) {
            auto p = e.parent();
            if (p && p.has<TemplateMember>())
                e.remove(flecs::ChildOf, p);
        }
    }
    for (const auto& item : previous.at("entities"))
        if (item.contains("prefab_member") &&
            rebuild_roots.contains(item.at("prefab_member").at("root"))) {
            auto e = entity(item.at("id"));
            if (e && e.is_alive())
                e.destruct();
            entities_.erase(item.at("id").get<std::string>());
        }
    for (auto it = entities_.begin(); it != entities_.end();) {
        if (!retained.contains(it->first)) {
            auto e = world().entity(it->second);
            if (e.is_alive())
                e.destruct();
            it = entities_.erase(it);
        } else
            ++it;
    }
    for (const auto& root_id : rebuild_roots) {
        auto e = entity(root_id);
        if (!e || !e.is_alive()) {
            e = world()
                    .entity()
                    .add(flecs::OrderedChildren)
                    .add<SceneMember>(membership_)
                    .set<StableId>({root_id});
            entities_[root_id] = e.id();
        }
        e.is_a(structured_bases.at(root_id));
        const auto& mapping = new_items.at(root_id)->at("prefab_instance").at("members");
        std::function<void(flecs::entity)> adopt = [&](flecs::entity parent) {
            parent.children([&](flecs::entity child) {
                if (!child.has<TemplateMember>())
                    return;
                const auto key = child.get<TemplateMember>().id.str();
                if (!mapping.contains(key))
                    throw std::runtime_error(
                        "Compiled prefab member missing from instance mapping");
                const std::string id = mapping.at(key);
                child.add<SceneMember>(membership_).set<StableId>({id});
                entities_[id] = child.id();
                adopt(child);
            });
        };
        adopt(e);
    }
    for (const auto& next : intended) {
        auto e = entity(next.id);
        if (!e || !e.is_alive()) {
            e = world()
                    .entity()
                    .add(flecs::OrderedChildren)
                    .add<SceneMember>(membership_)
                    .set<StableId>({next.id});
            entities_[next.id] = e.id();
        }
        if (inactive_members.contains(next.parent))
            e.add<MissingStructuralParent>();
        else
            e.remove<MissingStructuralParent>();
        if (!e.owns<SpatialBinding>() || e.get<SpatialBinding>() != next.spatial)
            e.set<SpatialBinding>(next.spatial);
        if (!e.owns<AuthoredName>() || e.get<AuthoredName>().value != next.name)
            e.set<AuthoredName>({next.name});
        if (next.prefab != e.has<AuthoredPrefab>()) {
            if (next.prefab)
                e.add<AuthoredPrefab>();
            else
                e.remove<AuthoredPrefab>();
        }
        bool prefab = next.prefab;
        auto parent = next.parent;
        while (!parent.empty()) {
            const auto& ancestor = *new_items.at(parent);
            prefab |= ancestor.value("prefab", false);
            parent = ancestor.value("parent", std::string{});
        }
        if (prefab != e.has(flecs::Prefab)) {
            if (prefab)
                e.add(flecs::Prefab);
            else
                e.remove(flecs::Prefab);
        }
        for (std::size_t i = 0; i < detail::builtins().size(); ++i)
            detail::builtins()[i].apply(e, next.values[i]);
    }
    for (const auto& next : intended) {
        auto e = entity(next.id);
        const auto parent = next.parent.empty() ? world().entity(membership_) : entity(next.parent);
        if (parent && e.target(flecs::ChildOf) != parent)
            e.child_of(parent);
    }
    // Build source child/base dependencies first, independent of file row order.
    std::set<std::string> linked;
    std::function<void(const std::string&)> link_base = [&](const std::string& id) {
        if (!linked.insert(id).second)
            return;
        for (const auto& next : intended)
            if (next.parent == id)
                link_base(next.id);
        const auto base = new_items.at(id)->value("base", std::string{});
        if (!base.empty()) {
            link_base(base);
            auto e = entity(id);
            if (e.target(flecs::IsA) != entity(base))
                e.is_a(entity(base));
        }
    };
    for (const auto& next : intended)
        link_base(next.id);
    auto& content = context_.content_.at(membership_);
    content.asset = doc.at("asset_id").get<AssetId>();
    content.persistent.clear();
    for (const auto& [id, handle] : entities_) {
        const auto persistent = EntityId::parse(id);
        auto e = world().entity(handle);
        if (!e.owns<PersistentEntityId>() || e.get<PersistentEntityId>().value != persistent)
            e.set<PersistentEntityId>({persistent});
        content.persistent.emplace(persistent, handle);
    }
    restore_child_order(doc);
    opaque_ = std::move(opaque);
    committed();
}
void Scene::set_prefab_sources(const PrefabSources& sources) {
    publish_prefab_sources(sources, [] {});
}
void Scene::publish_prefab_sources(const PrefabSources& sources,
                                   const std::function<void()>& durable_write) {
    if (sources == prefab_sources_) {
        durable_write();
        return;
    }
    replace_prefab_sources(sources, document(), durable_write, false);
}
void Scene::replace_prefab_sources(const PrefabSources& sources, const Json& source_document,
                                   const std::function<void()>& durable_write, bool all) {
    auto profile = context_.services().profile("prefab", "PrefabReconciliation");
    std::set<AssetId> visiting, done;
    std::function<void(AssetId)> dependencies = [&](AssetId asset) {
        if (done.contains(asset))
            return;
        if (!visiting.insert(asset).second)
            throw std::runtime_error("Cyclic prefab dependencies");
        if (!sources.contains(asset))
            throw std::runtime_error("Missing prefab dependency: " + asset.str());
        PrefabDocument::validate(sources.at(asset));
        for (const auto& dependency : sources.at(asset).value("dependencies", Json::array()))
            dependencies(dependency.get<AssetId>());
        visiting.erase(asset);
        done.insert(asset);
    };
    for (const auto& [asset, source] : sources) {
        (void)source;
        dependencies(asset);
    }
    PrefabTemplates candidate;
    for (const auto& [asset, source] : sources) {
        PrefabDocument parsed(source);
        if (parsed.asset() != asset)
            throw std::runtime_error("Prefab source AssetId mismatch");
        const auto old = prefab_templates_.find(asset);
        if (old != prefab_templates_.end() && old->second->document.source == source)
            candidate[asset] = old->second;
        else {
            if (old != prefab_templates_.end() &&
                parsed.revision() <= old->second->document.revision())
                throw std::runtime_error(
                    "Prefab revisions must advance; an existing revision is immutable");
            candidate[asset] = std::make_shared<CompiledPrefab>(context_, std::move(parsed));
        }
    }
    const auto before = document();
    const auto intended = reconcile_prefab_intent(source_document, sources);
    validate(intended);
    detail::validate_spatial(project_prefab_intent(intended, sources));
    // Temporary candidate content in the existing WorldContext. It is never
    // advanced, presented, notified or exposed as an authored scene. No new world.
    Scene staged(context_);
    staged.prefab_templates_ = candidate;
    staged.prefab_sources_ = sources;
    staged.replace(intended);
    (void)staged.effective_document();
    std::set<std::string> roots, affected;
    for (const auto& item : intended.at("entities"))
        if (item.contains("prefab_instance")) {
            const auto asset = item.at("prefab_instance").at("asset").get<AssetId>();
            if (prefab_sources_.contains(asset) != sources.contains(asset) ||
                (prefab_sources_.contains(asset) && sources.contains(asset) &&
                 prefab_sources_.at(asset) != sources.at(asset)))
                roots.insert(item.at("id"));
        }
    auto collect = [&](const Json& doc) {
        for (const auto& item : doc.at("entities"))
            if (all || roots.contains(item.at("id")) ||
                (item.contains("prefab_member") &&
                 roots.contains(item.at("prefab_member").at("root"))))
                affected.insert(item.at("id"));
    };
    collect(before);
    collect(intended);
    auto next_entities = entities_;
    for (const auto& id : affected) {
        next_entities.erase(id);
        if (staged.entities_.contains(id))
            next_entities[id] = staged.entities_.at(id);
    }
    std::map<EntityId, flecs::entity_t> next_persistent;
    for (const auto& [id, handle] : next_entities)
        next_persistent.emplace(EntityId::parse(id), handle);
    // Allocate/resolve every commit action before publishing bytes. There are no
    // user-supplied writers, parsing or container growth in the handoff. Flecs
    // lifecycle hooks remain trusted native code, as elsewhere in the host.
    std::vector<flecs::entity> detach, retire, adopt;
    std::vector<std::pair<flecs::entity, bool>> availability;
    std::vector<std::pair<flecs::entity, flecs::entity>> attach;
    for (const auto& [id, handle] : entities_) {
        auto e = world().entity(handle);
        if (affected.contains(id))
            retire.push_back(e);
        else if (e.parent() && e.parent().owns<StableId>() &&
                 affected.contains(e.parent().get<StableId>().value))
            detach.push_back(e);
    }
    for (const auto& id : affected)
        if (next_entities.contains(id))
            adopt.push_back(world().entity(next_entities.at(id)));
    for (const auto& item : intended.at("entities")) {
        const std::string id = item.at("id"), parent = item.value("parent", "");
        if (item.contains("prefab_member") || !next_entities.contains(id))
            continue;
        auto e = world().entity(next_entities.at(id));
        auto p = world().entity(parent.empty()                   ? membership_
                                : next_entities.contains(parent) ? next_entities.at(parent)
                                                                 : 0);
        if (e.parent() != p)
            attach.emplace_back(e, p);
        availability.emplace_back(e, !parent.empty() && !p);
    }
    const auto next_asset = intended.at("asset_id").get<AssetId>();
    auto next_sources = sources;
    auto next_opaque = staged.opaque_;
    // Prepare complete native sibling permutations before the durable boundary.
    std::map<ecs_entity_t, ecs_entity_t> final_parents;
    std::map<ecs_entity_t, std::vector<ecs_entity_t>> final_order;
    std::set<ecs_entity_t> retiring;
    for (auto e : retire)
        retiring.insert(e.id());
    for (const auto& [id, handle] : next_entities)
        final_parents.emplace(handle, world().entity(handle).parent().id());
    for (const auto& [e, parent] : attach)
        final_parents.at(e.id()) = parent.id();
    for (const auto& item : intended.at("entities")) {
        const auto found = next_entities.find(item.at("id").get<std::string>());
        if (found == next_entities.end())
            continue;
        const auto parent = final_parents.at(found->second);
        if (parent)
            final_order[parent].push_back(found->second);
    }
    for (auto& [parent, children] : final_order) {
        std::set<ecs_entity_t> seen(children.begin(), children.end());
        world().entity(parent).children([&](flecs::entity child) {
            if (retiring.contains(child.id()))
                return;
            const auto planned = final_parents.find(child.id());
            if (planned != final_parents.end() && planned->second != parent)
                return;
            if (seen.insert(child.id()).second)
                children.push_back(child.id());
        });
    }
    durable_write(); // A failed atomic replacement discards candidates only.
    for (auto e : detach)
        e.remove(flecs::ChildOf, flecs::Wildcard);
    for (auto e : adopt)
        e.add<SceneMember>(membership_);
    for (auto [e, p] : attach) {
        e.remove(flecs::ChildOf, flecs::Wildcard);
        if (p)
            e.child_of(p);
    }
    for (auto [e, missing] : availability) {
        if (missing)
            e.add<MissingStructuralParent>();
        else
            e.remove<MissingStructuralParent>();
    }
    for (auto e : retire)
        if (e.is_alive())
            e.destruct();
    for (const auto& id : affected)
        staged.entities_.erase(id);
    entities_.swap(next_entities);
    context_.content_.at(membership_).persistent.swap(next_persistent);
    context_.content_.at(membership_).asset = next_asset;
    opaque_.swap(next_opaque);
    prefab_sources_.swap(next_sources);
    prefab_templates_.swap(candidate);
    for (const auto& [parent, children] : final_order)
        ecs_set_child_order(world(), parent, children.data(),
                            static_cast<int32_t>(children.size()));
    if (!affected.empty()) {
        undo_.clear();
        redo_.clear();
    }
    committed();
}
Json Scene::snapshot() const {
    auto result = document();
    std::set<AssetId> referenced;
    for (const auto& item : result.at("entities"))
        if (item.contains("prefab_instance"))
            referenced.insert(item.at("prefab_instance").at("asset").get<AssetId>());
    std::function<void(AssetId)> append = [&](AssetId id) {
        if (!prefab_sources_.contains(id))
            return;
        const auto& source = prefab_sources_.at(id);
        if (!result.contains("_prefab_sources"))
            result["_prefab_sources"] = Json::array();
        result["_prefab_sources"].push_back(source);
        for (const auto& dependency : source.value("dependencies", Json::array())) {
            const auto target = dependency.get<AssetId>();
            if (referenced.insert(target).second)
                append(target);
        }
    };
    const auto initial = referenced;
    for (auto id : initial)
        append(id);
    return result;
}
void Scene::restore_snapshot(const Json& source) {
    auto doc = source;
    PrefabSources definitions;
    for (const auto& item : doc.value("_prefab_sources", Json::array())) {
        PrefabDocument parsed(item);
        if (!definitions.emplace(parsed.asset(), item).second)
            throw std::runtime_error("Duplicate prefab dependency");
    }
    doc.erase("_prefab_sources");
    validate(doc);
    if (definitions == prefab_sources_)
        replace(reconcile_prefab_intent(doc, definitions));
    else
        replace_prefab_sources(definitions, doc, [] {}, true);
}
void Scene::reset(const Json& doc) {
    replace(doc);
    undo_.clear();
    redo_.clear();
}
void Scene::restore_child_order(const Json& document) {
    std::map<ecs_entity_t, std::vector<ecs_entity_t>> desired;
    for (const auto& item : document.at("entities")) {
        auto child = entity(item.at("id"));
        if (!child)
            continue;
        auto parent = child.parent();
        if (parent)
            desired[parent.id()].push_back(child.id());
    }
    for (auto& [parent_id, children] : desired) {
        auto parent = world().entity(parent_id);
        parent.add(flecs::OrderedChildren);
        std::set<ecs_entity_t> seen(children.begin(), children.end());
        // Include generated/non-authored children: native API requires a complete
        // permutation, even in release builds where its debug checks are absent.
        parent.children([&](flecs::entity child) {
            if (seen.insert(child.id()).second)
                children.push_back(child.id());
        });
        ecs_set_child_order(world().c_ptr(), parent_id, children.data(),
                            static_cast<int32_t>(children.size()));
    }
}
Json Scene::serialize(bool effective) const {
    if (effective)
        context_.evaluate_world_transforms();
    auto doc = opaque_;
    auto output = Json::array();
    std::map<std::string, const Json*> fragments;
    for (const auto& item : opaque_.at("entities"))
        fragments[item.at("id")] = &item;
    for (auto item : doc.at("entities")) {
        auto e = entity(item.at("id"));
        if (!e || !e.is_alive()) {
            if (item.contains("prefab_member")) {
                item["missing_member"] = true;
                output.push_back(std::move(item));
            }
            continue;
        }
        item["name"] = e.get<AuthoredName>().value;
        const auto binding = e.has<SpatialBinding>() ? e.get<SpatialBinding>() : SpatialBinding{};
        if (item.contains("spatial") || binding.mode != SpatialMode::FollowStructure)
            item["spatial"]["mode"] = binding.mode == SpatialMode::World      ? "world"
                                      : binding.mode == SpatialMode::Explicit ? "explicit"
                                                                              : "follow_structure";
        if (binding.mode == SpatialMode::Explicit)
            item["spatial"]["target"] = binding.target;
        bool implicit_prefab = false;
        for (auto parent = e.target(flecs::ChildOf); parent; parent = parent.target(flecs::ChildOf))
            implicit_prefab |= parent.has(flecs::Prefab);
        if (e.has(flecs::Prefab) && (e.has<AuthoredPrefab>() || !implicit_prefab))
            item["prefab"] = true;
        for (const auto relation : {flecs::ChildOf, flecs::IsA}) {
            auto target = e.target(relation);
            if (target && target.owns<StableId>())
                item[relation == flecs::ChildOf ? "parent" : "base"] = target.get<StableId>().value;
        }
        for (const auto& type : detail::builtins()) {
            const auto value = type.read(e, effective);
            if (value.is_null()) {
                item["components"].erase(type.name);
                continue;
            }
            if (effective) {
                auto owner = type.owner(e);
                if (owner && owner != e && owner.owns<StableId>()) {
                    auto fragment = fragments.find(owner.get<StableId>().value);
                    if (fragment != fragments.end() &&
                        fragment->second->at("components").contains(type.name))
                        item["components"][type.name] =
                            fragment->second->at("components").at(type.name);
                }
            }
            if (!effective &&
                item.value("property_overrides", Json::object()).contains(type.name)) {
                auto& fields = item["property_overrides"][type.name];
                for (auto& [field, v] : fields.items())
                    if (value.contains(field))
                        v = detail::merge_builtin_property_extensions(type, field, value.at(field),
                                                                      v);
                continue;
            }
            auto& data = item["components"][type.name];
            if (!data.is_object())
                data = Json::object();
            data = detail::merge_builtin_extensions(type, value, data);
        }
        if (effective && e.has<LocalTranslation>()) {
            const auto t = context_.get_local_transform(e);
            auto& c = item["components"];
            c["forge.position"] = c.at("forge.local_translation");
            c["forge.scale"] = c.value("forge.local_scale", detail::encode(t.scale));
            auto angles = rotation_to_euler(t.rotation);
            c["forge.rotation"] = {{"x", angles[0]}, {"y", angles[1]}, {"z", angles[2]}};
            const auto& w = e.get<WorldTransform>();
            item["world_affine"] = w.affine.m;
            item["spatial_resolved"] = w.resolved;
        }
        output.push_back(std::move(item));
    }
    // Keep inter-parent row slots stable for backwards-compatible round trips;
    // each sibling subsequence is projected from Flecs OrderedChildren, not names.
    std::map<ecs_entity_t, std::vector<std::size_t>> slots;
    std::map<ecs_entity_t, Json> rows;
    for (std::size_t i = 0; i < output.size(); ++i) {
        auto e = entity(output[i].at("id"));
        if (e && e.parent() && e.parent().has(flecs::OrderedChildren)) {
            slots[e.parent().id()].push_back(i);
            rows.emplace(e.id(), output[i]);
        }
    }
    for (const auto& [parent, indices] : slots) {
        const auto children = ecs_get_ordered_children(world().c_ptr(), parent);
        std::size_t next = 0;
        for (int32_t i = 0; i < children.count; ++i)
            if (rows.contains(children.ids[i]))
                output[indices.at(next++)] = rows.at(children.ids[i]);
    }
    doc["entities"] = std::move(output);
    detail::promote_scale_format(doc, "entities", 5);
    return doc;
}
Json Scene::document() const { return serialize(false); }
Json Scene::effective_document() const { return serialize(true); }
Json Scene::schema() const { return context_.schema(); }
detail::SceneDraft::SceneDraft(const Scene& source)
    : document_(source.document()), schema_(source.schema()), prefabs_(source.prefab_sources()) {}
void detail::SceneDraft::edit(const Json& document) {
    Scene::validate_document(document);
    auto normalized = document;
    for (auto& item : normalized["entities"])
        for (const auto& type : schema_.at("components")) {
            const auto name = type.at("id").get<std::string>();
            if (!item["components"].contains(name))
                continue;
            auto& data = item["components"][name];
            for (const auto& field : type.at("fields")) {
                const auto key = field.at("id").get<std::string>();
                if (!data.contains(key))
                    continue;
                const auto storage = field.at("type").get<std::string>();
                if (storage == "uint32")
                    data[key] = data.at(key).get<std::uint32_t>();
                else if (storage == "float32")
                    data[key] = double(data.at(key).get<float>());
                else if (storage == "float64")
                    data[key] = data.at(key).get<double>();
            }
        }
    detail::promote_scale_format(normalized, "entities", 5);
    document_ = std::move(normalized);
}
std::string detail::SceneDraft::instantiate_prefab(AssetId asset) {
    if (!prefabs_.contains(asset))
        throw std::runtime_error("Prefab source is not available in this project");
    const PrefabDocument source(prefabs_.at(asset));
    auto doc = document_;
    doc["version"] = std::max(doc.at("version").get<unsigned>(), 4u);
    const auto id = EntityId::generate().str();
    std::string name = "Prefab";
    for (const auto& m : source.source.at("members"))
        if (m.at("id") == source.root().str())
            name = m.at("name");
    doc["entities"].push_back({{"id", id},
                               {"name", name},
                               {"components", Json::object()},
                               {"prefab_instance",
                                {{"asset", asset},
                                 {"revision", source.revision()},
                                 {"members", {{source.root().str(), id}}}}}});
    edit(reconcile_prefab_intent(doc, prefabs_));
    return id;
}
void detail::SceneDraft::revert_prefab_name(const std::string& id) {
    auto doc = document_;
    auto& root = find_entity(doc, id);
    if (!root.contains("prefab_instance"))
        throw std::runtime_error("Select a prefab instance root");
    root.erase("name_override");
    edit(reconcile_prefab_intent(doc, prefabs_));
}
Json detail::SceneDraft::effective_document() const {
    // Only detached transaction/gesture intent is evaluated here. Live views use
    // Scene::effective_document and Flecs get/has. No world or callbacks are created.
    auto result = project_prefab_intent(document_, prefabs_);
    std::map<std::string, const Json*> source;
    for (const auto& e : document_.at("entities"))
        source[e.at("id")] = &e;
    for (auto& e : result["entities"]) {
        const Json* current = source.at(e.at("id"));
        for (std::size_t depth = 0; depth < source.size() && current->contains("base"); ++depth) {
            current = source.at(current->at("base"));
            for (const auto& type : builtins())
                if (!e["components"].contains(type.name) &&
                    current->at("components").contains(type.name))
                    e["components"][type.name] = current->at("components").at(type.name);
        }
    }
    return project_spatial(std::move(result));
}
Json Scene::preview_document(const Json& intended) const {
    detail::SceneDraft draft(*this);
    auto canonical = intended;
    for (auto& e : canonical["entities"])
        for (auto name : {"forge.position", "forge.rotation", "forge.scale"})
            if (e["components"].contains(name)) {
                detail::write_channel(e, name, e["components"].at(name));
                e["components"].erase(name);
            }
    draft.edit(canonical);
    return draft.effective_document();
}

void atomic_write(const std::filesystem::path& path, const std::string& contents) {
    if (path.empty())
        throw std::runtime_error("Empty save path");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".pending";
    try {
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.exceptions(std::ios::badbit | std::ios::failbit);
            out << contents;
            out.flush();
        }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::filesystem::filesystem_error(
                "Atomic save replacement failed", temp, path,
                std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
        std::filesystem::rename(temp, path);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
void Scene::save(const std::filesystem::path& path) const { write_scene_file(path, document()); }
void Scene::load(const std::filesystem::path& path) {
    replace(read_scene_file(path));
    undo_.clear();
    redo_.clear();
}
void Scene::edit(const Json& doc) {
    auto before = document();
    if (before == (doc.at("version") < 3 ? migrate_scene(doc, &before) : doc))
        return;
    replace(doc);
    undo_.push_back(std::move(before));
    redo_.clear();
    if (undo_.size() > 100)
        undo_.erase(undo_.begin());
}
void detail::SceneDraft::rename_entity(const std::string& id, const std::string& name) {
    if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("Entity name must not be blank");
    auto doc = document();
    auto& target = find_entity(doc, resolve_legacy_id(doc, id));
    target["name"] = name;
    if (target.contains("prefab_instance"))
        target["name_override"] = true;
    edit(doc);
}
void detail::SceneDraft::reparent_entity(const std::string& id, const std::string& parent,
                                         ReparentMode mode) {
    auto doc = document();
    const auto canonical_parent = resolve_legacy_id(doc, parent);
    detail::rebind(doc, effective_document(), resolve_legacy_id(doc, id),
                   {{"mode", "follow_structure"}}, &canonical_parent, mode);
    edit(doc);
}
std::string detail::SceneDraft::duplicate_subtree(const std::string& id) {
    auto doc = document();
    const auto canonical = resolve_legacy_id(doc, id);
    if (find_entity(doc, canonical).contains("prefab_member"))
        throw std::runtime_error("Operate on the whole prefab instance or edit its source");
    const auto ids = subtree(doc, canonical);
    std::set<std::string> occupied;
    for (const auto& e : doc["entities"])
        occupied.insert(e.at("id").get<std::string>());
    std::map<std::string, std::string> remap;
    for (const auto& original : ids) {
        std::string copy;
        do {
            copy = EntityId::generate().str();
        } while (!occupied.insert(copy).second);
        remap[original] = copy;
    }
    for (const auto& e : doc["entities"])
        if (ids.contains(e.at("id")) && e.contains("prefab_instance"))
            for (const auto& value : e.at("prefab_instance").at("members")) {
                const std::string old = value;
                if (!remap.contains(old))
                    remap[old] = EntityId::generate().str();
            }
    std::map<EntityId, EntityId> typed_remap;
    for (const auto& [a, b] : remap)
        typed_remap.emplace(EntityId::parse(a), EntityId::parse(b));
    auto copies = Json::array();
    for (auto e : doc["entities"]) {
        const auto original = e.at("id").get<std::string>();
        if (!ids.contains(original))
            continue;
        e["id"] = remap.at(original);
        if (original == canonical) {
            e["name"] = e.at("name").get<std::string>() + " Copy";
            if (e.contains("prefab_instance"))
                e["name_override"] = true;
        }
        for (const char* relation : {"parent", "base"})
            if (e.contains(relation) && remap.contains(e.at(relation).get<std::string>()))
                e[relation] = remap.at(e.at(relation).get<std::string>());
        detail::remap_spatial(e, doc.at("asset_id").get<AssetId>(),
                              doc.at("asset_id").get<AssetId>(), typed_remap);
        copies.push_back(std::move(e));
    }
    Json copied_document = {{"entities", copies}};
    remap_prefab_instances(copied_document, typed_remap);
    for (auto& e : copied_document["entities"])
        doc["entities"].push_back(std::move(e));
    edit(doc);
    return remap.at(canonical);
}
void detail::SceneDraft::delete_subtree(const std::string& id) {
    auto doc = document();
    const auto canonical = resolve_legacy_id(doc, id);
    if (find_entity(doc, canonical).contains("prefab_member"))
        throw std::runtime_error("Operate on the whole prefab instance or edit its source");
    const auto ids = subtree(doc, canonical);
    const auto effective = effective_document();
    auto remaining = Json::array();
    for (const auto& e : doc["entities"]) {
        if (ids.contains(e.at("id").get<std::string>()))
            continue;
        if (ids.contains(e.value("base", std::string{})))
            throw std::runtime_error("Cannot delete a prefab used outside this subtree");
        auto survivor = e;
        const auto binding = detail::read_binding(e);
        if (binding.mode == SpatialMode::Explicit &&
            binding.target.scene == doc.at("asset_id").get<AssetId>() &&
            ids.contains(binding.target.entity.str())) {
            auto detached = doc;
            detail::rebind(detached, effective, e.at("id"), {{"mode", "world"}}, nullptr,
                           ReparentMode::PreserveWorld);
            survivor = find_entity(detached, e.at("id"));
        }
        remaining.push_back(survivor);
    }
    doc["entities"] = std::move(remaining);
    edit(doc);
}
void Scene::rename_entity(const std::string& id, const std::string& name) {
    detail::SceneDraft draft(*this);
    draft.rename_entity(id, name);
    edit(draft.document());
}
void Scene::reparent_entity(const std::string& id, const std::string& parent, ReparentMode mode) {
    detail::SceneDraft draft(*this);
    draft.reparent_entity(id, parent, mode);
    edit(draft.document());
}
std::string Scene::duplicate_subtree(const std::string& id) {
    detail::SceneDraft draft(*this);
    auto result = draft.duplicate_subtree(id);
    edit(draft.document());
    return result;
}
void Scene::delete_subtree(const std::string& id) {
    detail::SceneDraft draft(*this);
    draft.delete_subtree(id);
    edit(draft.document());
}
bool Scene::undo() {
    if (undo_.empty())
        return false;
    auto current = document();
    replace(undo_.back());
    undo_.pop_back();
    redo_.push_back(std::move(current));
    return true;
}
bool Scene::redo() {
    if (redo_.empty())
        return false;
    auto current = document();
    replace(redo_.back());
    redo_.pop_back();
    undo_.push_back(std::move(current));
    return true;
}
void Scene::translate(float x, float y, float z) {
    context_.translate_content(membership_, {x, y, z});
    committed();
}
} // namespace forge
