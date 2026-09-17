#include "builtins.hpp"
#include "scene_draft.hpp"
#include <array>
#include <cmath>
#include <forge/scene.hpp>
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
        (doc.value("version", 0) != 1 && doc.value("version", 0) != 2) ||
        !doc.contains("entities") || !doc.at("entities").is_array())
        throw std::runtime_error("Expected scene version 1 or 2 and an entities array");
    if (doc.at("version") == 2) {
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
    std::map<std::string, const Json*> entities;
    for (const auto& e : doc.at("entities")) {
        const auto id = e.at("id").get<std::string>();
        if (doc.at("version") == 2)
            (void)EntityId::parse(id);
        if (id.empty() || !entities.emplace(id, &e).second)
            throw std::runtime_error("Empty or duplicate entity ID");
        if (!e.at("name").is_string() || !e.at("components").is_object())
            throw std::runtime_error("Invalid entity name/components");
        if (e.contains("prefab") && !e.at("prefab").is_boolean())
            throw std::runtime_error("Invalid prefab flag");
        detail::validate_components(e.at("components"));
    }
    if (doc.at("version") == 2 && doc.contains("legacy_ids"))
        for (const auto& [alias, target] : doc.at("legacy_ids").items())
            if (entities.contains(alias) && target != alias)
                throw std::runtime_error("Legacy alias shadows a persistent entity ID");
    for (const char* relation : {"parent", "base"}) {
        std::set<std::string> visiting, done;
        std::function<void(const std::string&)> visit = [&](const std::string& id) {
            if (done.contains(id))
                return;
            if (!visiting.insert(id).second)
                throw std::runtime_error("Cyclic scene relationship");
            const auto& e = *entities.at(id);
            if (e.contains(relation)) {
                const auto target = e.at(relation).get<std::string>();
                if (!entities.contains(target))
                    throw std::runtime_error("Missing relationship target");
                if (std::string(relation) == "base" && !entities.at(target)->value("prefab", false))
                    throw std::runtime_error("Base must be a prefab");
                visit(target);
            }
            visiting.erase(id);
            done.insert(id);
        };
        for (const auto& [id, value] : entities) {
            (void)value;
            visit(id);
        }
    }
    // Prefab expansion follows base links and then child links. Validate their
    // combined graph before Flecs can attempt recursive child instantiation.
    std::map<std::string, std::vector<std::string>> expansion;
    for (const auto& [id, e] : entities) {
        if (e->contains("base"))
            expansion[id].push_back(e->at("base").get<std::string>());
        if (e->contains("parent"))
            expansion[e->at("parent").get<std::string>()].push_back(id);
    }
    std::set<std::string> visiting, done;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (done.contains(id))
            return;
        if (!visiting.insert(id).second)
            throw std::runtime_error(
                "Parent and prefab links would recursively instantiate the hierarchy");
        for (const auto& next : expansion[id])
            visit(next);
        visiting.erase(id);
        done.insert(id);
    };
    for (const auto& [id, e] : entities) {
        (void)e;
        visit(id);
    }
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
std::uint64_t Scene::revision() const {
    const auto serial = context_.content_.at(membership_).serial;
    if (serial != observed_serial_) {
        observed_serial_ = serial;
        ++revision_;
    }
    return revision_;
}
void Scene::committed() {
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
    const auto doc = source.at("version") == 1 ? migrate_scene(source, &opaque_) : source;
    struct Intended {
        std::string id, name, parent, base;
        bool prefab;
        std::array<std::optional<detail::Value>, 5> values;
    };
    // All parsing/type conversion/opaque copies happen before the first world write.
    auto opaque = doc;
    std::vector<Intended> intended;
    std::set<std::string> retained;
    for (auto& item : opaque["entities"]) {
        Intended next{item.at("id"),
                      item.at("name"),
                      item.value("parent", std::string{}),
                      item.value("base", std::string{}),
                      item.value("prefab", false),
                      {}};
        retained.insert(next.id);
        auto& components = item["components"];
        for (std::size_t i = 0; i < detail::builtins().size(); ++i) {
            const auto& type = detail::builtins()[i];
            if (!components.contains(type.name))
                continue;
            next.values[i] = type.decode(components.at(type.name));
            for (const auto& [field, value] : type.defaults.items()) {
                (void)value;
                components[type.name].erase(field);
            }
            if (components[type.name].empty())
                components.erase(type.name);
        }
        item.erase("name");
        item.erase("parent");
        item.erase("base");
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
        if (parent && parent != entity(next.parent))
            e.remove(flecs::ChildOf, parent);
        const auto base = e.target(flecs::IsA);
        if (base && (base != entity(next.base) || refresh.contains(next.id))) {
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
    for (auto it = entities_.begin(); it != entities_.end();) {
        if (!retained.contains(it->first)) {
            auto e = world().entity(it->second);
            if (e.is_alive())
                e.destruct();
            it = entities_.erase(it);
        } else
            ++it;
    }
    for (const auto& next : intended) {
        auto e = entity(next.id);
        if (!e || !e.is_alive()) {
            e = world().entity().add<SceneMember>(membership_).set<StableId>({next.id});
            entities_[next.id] = e.id();
        }
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
        if (!next.parent.empty() && e.target(flecs::ChildOf) != entity(next.parent))
            e.child_of(entity(next.parent));
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
    opaque_ = std::move(opaque);
    committed();
}
void Scene::reset(const Json& doc) {
    replace(doc);
    undo_.clear();
    redo_.clear();
}
Json Scene::serialize(bool effective) const {
    auto doc = opaque_;
    auto output = Json::array();
    std::map<std::string, const Json*> fragments;
    for (const auto& item : opaque_.at("entities"))
        fragments[item.at("id")] = &item;
    for (auto item : doc.at("entities")) {
        auto e = entity(item.at("id"));
        if (!e || !e.is_alive())
            continue;
        item["name"] = e.get<AuthoredName>().value;
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
            auto& data = item["components"][type.name];
            if (!data.is_object())
                data = Json::object();
            data.update(value);
        }
        output.push_back(std::move(item));
    }
    doc["entities"] = std::move(output);
    return doc;
}
Json Scene::document() const { return serialize(false); }
Json Scene::effective_document() const { return serialize(true); }
Json Scene::schema() const { return context_.schema(); }
detail::SceneDraft::SceneDraft(const Scene& source)
    : document_(source.document()), schema_(source.schema()) {}
void detail::SceneDraft::edit(const Json& document) {
    Scene::validate_document(document);
    auto normalized = document;
    for (auto& item : normalized["entities"])
        for (const auto& type : builtins()) {
            if (!item["components"].contains(type.name))
                continue;
            auto& data = item["components"][type.name];
            for (const auto& [field, initial] : type.defaults.items())
                if (initial.is_number_unsigned())
                    data[field] = data.at(field).get<std::uint32_t>();
                else
                    data[field] = data.at(field).get<float>();
        }
    document_ = std::move(normalized);
}
Json detail::SceneDraft::effective_document() const {
    // Only detached transaction/gesture intent is evaluated here. Live views use
    // Scene::effective_document and Flecs get/has. No world or callbacks are created.
    auto result = document_;
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
    return result;
}
Json Scene::preview_document(const Json& intended) const {
    detail::SceneDraft draft(*this);
    draft.edit(intended);
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
    if (before == (doc.at("version") == 1 ? migrate_scene(doc, &before) : doc))
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
    find_entity(doc, resolve_legacy_id(doc, id))["name"] = name;
    edit(doc);
}
void detail::SceneDraft::reparent_entity(const std::string& id, const std::string& parent) {
    auto doc = document();
    auto& e = find_entity(doc, resolve_legacy_id(doc, id));
    if (parent.empty())
        e.erase("parent");
    else
        e["parent"] = resolve_legacy_id(doc, parent);
    edit(doc); // Existing relationship validation rejects missing parents and cycles.
}
std::string detail::SceneDraft::duplicate_subtree(const std::string& id) {
    auto doc = document();
    const auto canonical = resolve_legacy_id(doc, id);
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
    auto copies = Json::array();
    for (auto e : doc["entities"]) {
        const auto original = e.at("id").get<std::string>();
        if (!ids.contains(original))
            continue;
        e["id"] = remap.at(original);
        if (original == canonical)
            e["name"] = e.at("name").get<std::string>() + " Copy";
        for (const char* relation : {"parent", "base"})
            if (e.contains(relation) && remap.contains(e.at(relation).get<std::string>()))
                e[relation] = remap.at(e.at(relation).get<std::string>());
        copies.push_back(std::move(e));
    }
    for (auto& e : copies)
        doc["entities"].push_back(std::move(e));
    edit(doc);
    return remap.at(canonical);
}
void detail::SceneDraft::delete_subtree(const std::string& id) {
    auto doc = document();
    const auto canonical = resolve_legacy_id(doc, id);
    const auto ids = subtree(doc, canonical);
    auto remaining = Json::array();
    for (const auto& e : doc["entities"]) {
        if (ids.contains(e.at("id").get<std::string>()))
            continue;
        if (ids.contains(e.value("base", std::string{})))
            throw std::runtime_error("Cannot delete a prefab used outside this subtree");
        remaining.push_back(e);
    }
    doc["entities"] = std::move(remaining);
    edit(doc);
}
void Scene::rename_entity(const std::string& id, const std::string& name) {
    detail::SceneDraft draft(*this);
    draft.rename_entity(id, name);
    edit(draft.document());
}
void Scene::reparent_entity(const std::string& id, const std::string& parent) {
    detail::SceneDraft draft(*this);
    draft.reparent_entity(id, parent);
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
    world().defer_begin();
    world().each([&](flecs::entity e, const Position& p) {
        if (context_.owner_of(e) == membership_)
            e.set<Position>({p.x + x, p.y + y, p.z + z});
    });
    world().defer_end();
    committed();
}
} // namespace forge
