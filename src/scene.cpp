#include <cmath>
#include <forge/scene.hpp>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace forge {
namespace {
void validate(const Json& doc) {
    if (!doc.is_object() || doc.value("version", 0) != 1 || !doc.contains("entities") ||
        !doc.at("entities").is_array())
        throw std::runtime_error("Expected scene version 1 and an entities array");
    std::map<std::string, const Json*> entities;
    for (const auto& e : doc.at("entities")) {
        const auto id = e.at("id").get<std::string>();
        if (id.empty() || !entities.emplace(id, &e).second)
            throw std::runtime_error("Empty or duplicate entity ID");
        if (!e.at("name").is_string() || !e.at("components").is_object())
            throw std::runtime_error("Invalid entity name/components");
        if (e.contains("prefab") && !e.at("prefab").is_boolean())
            throw std::runtime_error("Invalid prefab flag");
        if (e.at("components").contains("forge.position")) {
            const auto& p = e.at("components").at("forge.position");
            for (const char* axis : {"x", "y", "z"}) {
                const auto value = p.at(axis).get<float>();
                if (!std::isfinite(value))
                    throw std::runtime_error("Position must be finite");
            }
        }
    }
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
    throw std::runtime_error("Entity no longer exists");
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
Scene::Scene() { replace(Json{{"version", 1}, {"entities", Json::array()}}); }
void Scene::replace(const Json& doc) {
    validate(doc);
    auto next = std::make_unique<flecs::world>();
    next->component<Position>("forge.position")
        .member<float>("x")
        .member<float>("y")
        .member<float>("z");
    for (const char* axis : {"x", "y", "z"}) {
        std::string description =
            std::string("Position along the ") + axis + " axis in world units.";
        next->component<Position>().lookup(axis).set_doc_brief(description.c_str());
    }
    next->component<Position>().add(flecs::OnInstantiate, flecs::Inherit);
    next->component<StableId>("forge.stable_id");
    std::map<std::string, flecs::entity> entities;
    for (const auto& item : doc.at("entities")) {
        const auto id = item.at("id").get<std::string>();
        auto e = next->entity().set<StableId>({id});
        if (item.value("prefab", false))
            e.add(flecs::Prefab);
        const auto& c = item.at("components");
        if (c.contains("forge.position")) {
            const auto& p = c.at("forge.position");
            e.set<Position>(
                {p.at("x").get<float>(), p.at("y").get<float>(), p.at("z").get<float>()});
        }
        entities.emplace(id, e);
    }
    for (const auto& item : doc.at("entities")) {
        auto e = entities.at(item.at("id").get<std::string>());
        if (item.contains("parent"))
            e.child_of(entities.at(item.at("parent").get<std::string>()));
        if (item.contains("base"))
            e.is_a(entities.at(item.at("base").get<std::string>()));
    }
    // Commit only after complete validation and construction. Unknown data remains authored.
    entities_.clear();
    for (const auto& [id, e] : entities)
        entities_[id] = e.id();
    world_ = std::move(next);
    source_ = doc;
    ++revision_;
}
void Scene::reset(const Json& doc) {
    replace(doc);
    undo_.clear();
    redo_.clear();
}
Json Scene::document() const {
    auto doc = source_;
    std::map<std::string, Position> positions;
    for (const auto& [id, handle] : entities_) {
        auto e = world_->entity(handle);
        if (e.owns<Position>())
            positions.emplace(id, e.get<Position>());
    }
    for (auto& e : doc["entities"]) {
        auto it = positions.find(e.at("id").get<std::string>());
        if (it != positions.end()) {
            const auto& p = it->second;
            auto& data = e["components"]["forge.position"];
            data["x"] = p.x;
            data["y"] = p.y;
            data["z"] = p.z;
        }
    }
    return doc;
}
Json Scene::schema() const {
    auto component = world_->component<Position>();
    const auto* structure = ecs_get(world_->c_ptr(), component.id(), EcsStruct);
    if (!structure)
        throw std::runtime_error("Position reflection metadata is missing");
    Json fields = Json::array();
    const auto* members = ecs_vec_first_t(&structure->members, ecs_member_t);
    for (int i = 0; i < ecs_vec_count(&structure->members); ++i) {
        const auto& member = members[i];
        const char* description = ecs_doc_get_brief(world_->c_ptr(), member.member);
        fields.push_back({{"id", member.name},
                          {"type", "float32"},
                          {"description", description ? description : ""}});
    }
    return Json{{"version", 1},
                {"components", Json::array({{{"id", "forge.position"}, {"fields", fields}}})}};
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
            throw std::runtime_error("Atomic save replacement failed");
#else
        std::filesystem::rename(temp, path);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
void Scene::save(const std::filesystem::path& path) const {
    atomic_write(path, document().dump(2));
}
void Scene::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open scene");
    Json doc;
    input >> doc;
    replace(doc);
    undo_.clear();
    redo_.clear();
}
void Scene::edit(const Json& doc) {
    auto before = document();
    if (before == doc)
        return;
    replace(doc);
    undo_.push_back(std::move(before));
    redo_.clear();
    if (undo_.size() > 100)
        undo_.erase(undo_.begin());
}
void Scene::rename_entity(const std::string& id, const std::string& name) {
    if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("Entity name must not be blank");
    auto doc = document();
    find_entity(doc, id)["name"] = name;
    edit(doc);
}
void Scene::reparent_entity(const std::string& id, const std::string& parent) {
    auto doc = document();
    auto& e = find_entity(doc, id);
    if (parent.empty())
        e.erase("parent");
    else
        e["parent"] = parent;
    edit(doc); // Existing relationship validation rejects missing parents and cycles.
}
std::string Scene::duplicate_subtree(const std::string& id) {
    auto doc = document();
    const auto ids = subtree(doc, id);
    std::set<std::string> occupied;
    for (const auto& e : doc["entities"])
        occupied.insert(e.at("id").get<std::string>());
    std::map<std::string, std::string> remap;
    for (const auto& original : ids) {
        unsigned suffix = 1;
        std::string copy;
        do {
            copy = original + "-copy-" + std::to_string(suffix++);
        } while (!occupied.insert(copy).second);
        remap[original] = copy;
    }
    auto copies = Json::array();
    for (auto e : doc["entities"]) {
        const auto original = e.at("id").get<std::string>();
        if (!ids.contains(original))
            continue;
        e["id"] = remap.at(original);
        if (original == id)
            e["name"] = e.at("name").get<std::string>() + " Copy";
        for (const char* relation : {"parent", "base"})
            if (e.contains(relation) && remap.contains(e.at(relation).get<std::string>()))
                e[relation] = remap.at(e.at(relation).get<std::string>());
        copies.push_back(std::move(e));
    }
    for (auto& e : copies)
        doc["entities"].push_back(std::move(e));
    edit(doc);
    return remap.at(id);
}
void Scene::delete_subtree(const std::string& id) {
    auto doc = document();
    const auto ids = subtree(doc, id);
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
    world_->defer_begin();
    world_->each(
        [&](flecs::entity e, const Position& p) { e.set<Position>({p.x + x, p.y + y, p.z + z}); });
    world_->defer_end();
    ++revision_;
}
} // namespace forge
