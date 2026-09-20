#include "builtins.hpp"
#include "relationship_graph.hpp"
#include "spatial_document.hpp"
#include <algorithm>
#include <cmath>
#include <forge/prefab.hpp>
#include <functional>
#include <set>
namespace forge {
namespace {
const Json& member(const Json& source, const std::string& id) {
    for (const auto& item : source.at("members"))
        if (item.at("id") == id)
            return item;
    throw std::runtime_error("Missing prefab member: " + id);
}
void remap_member_fields(Json& item, const std::map<std::string, std::string>& ids) {
    if (item.contains("parent"))
        item["parent"] = ids.at(item.at("parent"));
    if (item.contains("spatial") && item["spatial"].value("mode", "") == "explicit")
        item["spatial"]["member"] = ids.at(item["spatial"].at("member"));
}
} // namespace
PrefabDocument::PrefabDocument(Json value) : source(std::move(value)) {
    validate(source);
    detail::promote_scale_format(source, "members", 2);
}
void PrefabDocument::validate(const Json& doc) {
    if (!doc.is_object() || doc.value("format", "") != "forge.prefab" ||
        !doc.at("version").is_number_integer() ||
        (doc.at("version") != 1 && doc.at("version") != 2))
        throw std::runtime_error("Expected FORGE prefab schema 1 or 2");
    (void)doc.at("asset_id").get<AssetId>();
    const auto& revision = doc.at("revision");
    if (!revision.is_number_integer() || revision.get<double>() < 1)
        throw std::runtime_error("Prefab revision must be a positive unsigned integer");
    const auto root = doc.at("root").get<PrefabMemberId>().str();
    if (!doc.at("members").is_array() || doc.at("members").empty() ||
        doc.at("members").size() > 4096)
        throw std::runtime_error("Prefab requires 1..4096 members");
    std::set<std::string> ids;
    for (const auto& item : doc.at("members")) {
        const auto id = item.at("id").get<PrefabMemberId>().str();
        if (!ids.insert(id).second)
            throw std::runtime_error("Duplicate prefab member identity");
        if (!item.at("name").is_string() || !item.at("components").is_object())
            throw std::runtime_error("Invalid prefab member name/components");
        if (item.contains("base") || item.contains("prefab_instance"))
            throw std::runtime_error(
                "Nested/legacy prefab definitions are not supported inside prefab assets");
        if (item.contains("world_affine") || item["components"].contains("forge.world_transform"))
            throw std::runtime_error(
                "WorldTransform is derived and cannot be authored in a prefab");
        detail::validate_components(item.at("components"));
        if (item.at("components").contains("forge.local_rotation")) {
            const auto q = detail::read_local(item.at("components")).rotation;
            if (std::abs(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z +
                         double(q.w) * q.w - 1) > 2e-6)
                throw std::runtime_error("Prefab LocalRotation must be a normalized quaternion");
        }
        if (id == root && item.contains("parent"))
            throw std::runtime_error("Prefab root cannot have a member parent");
        if (id != root && !item.contains("parent"))
            throw std::runtime_error("Prefab member must belong to its root");
    }
    if (!ids.contains(root))
        throw std::runtime_error("Prefab root member is missing");
    detail::RelationshipGraph hierarchy;
    for (const auto& item : doc.at("members")) {
        auto& edges = hierarchy[item.at("id").get<std::string>()];
        if (item.contains("parent"))
            edges.push_back({item.at("parent").get<std::string>()});
    }
    detail::validate_relationship_graph(hierarchy, "Prefab member");
    std::map<std::uint64_t, TransformNode> nodes;
    std::map<std::string, std::uint64_t> handles;
    for (const auto& id : ids)
        handles[id] = handles.size() + 1;
    for (const auto& item : doc.at("members")) {
        const std::string id = item.at("id");
        TransformNode n{detail::read_local(item.at("components"))};
        const auto spatial = item.value("spatial", Json{{"mode", "follow_structure"}});
        const auto mode = spatial.at("mode").get<std::string>();
        if (id == root && mode != "follow_structure")
            throw std::runtime_error("Choose spatial attachment on each instance root; the source "
                                     "root must follow structure");
        if (mode == "follow_structure")
            n.parent = item.contains("parent") ? handles.at(item.at("parent")) : 0;
        else if (mode == "explicit")
            n.parent = handles.at(spatial.at("member"));
        else if (mode != "world")
            throw std::runtime_error("Invalid prefab spatial mode");
        nodes[handles.at(id)] = n;
    }
    (void)evaluate_transforms(nodes);
    for (const auto& id : doc.value("dependencies", Json::array()))
        (void)id.get<AssetId>();
}
PrefabDocument PrefabDocument::duplicate() const {
    auto copy = source;
    copy["asset_id"] = AssetId::generate();
    copy["revision"] = std::uint64_t{1};
    std::map<std::string, std::string> ids;
    for (const auto& item : copy["members"])
        ids[item.at("id")] = PrefabMemberId::generate().str();
    copy["root"] = ids.at(copy.at("root"));
    for (auto& item : copy["members"]) {
        item["id"] = ids.at(item.at("id"));
        remap_member_fields(item, ids);
    }
    return PrefabDocument(std::move(copy));
}
PrefabDocument PrefabDocument::reorder_member(PrefabMemberId id, PrefabMemberId before) const {
    if (id == before)
        return *this;
    const auto& moved = member(source, id.str());
    const auto& target = member(source, before.str());
    if (!moved.contains("parent") || moved.value("parent", "") != target.value("parent", ""))
        throw std::runtime_error("Prefab ordering requires two members with the same parent");
    auto copy = source;
    auto rows = Json::array();
    for (const auto& row : source.at("members")) {
        if (row.at("id") == before.str())
            rows.push_back(moved);
        if (row.at("id") != id.str())
            rows.push_back(row);
    }
    copy["members"] = std::move(rows);
    return PrefabDocument(std::move(copy));
}
CompiledPrefab::CompiledPrefab(WorldContext& context, PrefabDocument source)
    : document(std::move(source)), context_(context) {
    auto& world = context.world();
    try {
        std::function<flecs::entity(const std::string&)> compile = [&](const std::string& id) {
            const auto key = PrefabMemberId::parse(id);
            if (members_.contains(key))
                return members_.at(key);
            const auto& item = member(document.source, id);
            auto e =
                item.contains("parent")
                    ? world.entity(flecs::Parent{compile(item.at("parent"))}).add(flecs::Prefab)
                    : world.prefab();
            e.set<TemplateMember>({key});
            members_.emplace(key, e);
            for (const auto& type : detail::builtins())
                if (item["components"].contains(type.name))
                    type.apply(e, type.decode(item["components"].at(type.name)));
            return e;
        };
        for (const auto& item : document.source.at("members"))
            compile(item.at("id"));
    } catch (...) {
        for (auto& [id, e] : members_) {
            (void)id;
            if (e.is_alive())
                e.destruct();
        }
        throw;
    }
}
CompiledPrefab::~CompiledPrefab() {
    // Pinned Flecs4.1.6 guards child deletion while cached spawner tables survive.
    // The owner retains this revision until all its instances are retired.
    for (auto& [id, e] : members_) {
        (void)id;
        if (e.is_alive())
            ecs_remove(context_.world().c_ptr(), e.id(), EcsTreeSpawner);
    }
    for (auto& [id, e] : members_) {
        (void)id;
        if (e.is_alive())
            e.destruct();
    }
}
Json prefab_override_value(const Json& definition, const Json& instance) {
    auto values = definition.at("components");
    for (const auto& [key, value] : instance.at("components").items())
        values[key] = value;
    const auto properties = instance.value("property_overrides", Json::object());
    for (const auto& [component, fields] : properties.items()) {
        const auto type = std::find_if(detail::builtins().begin(), detail::builtins().end(),
                                       [&](const auto& t) { return component == t.name; });
        if (type == detail::builtins().end())
            continue; // Opaque plugin intent is preserved, never interpreted.
        if (component.starts_with("forge.local_"))
            throw std::runtime_error("Transform overrides require a complete local channel");
        if (instance["components"].contains(component))
            throw std::runtime_error("Conflicting component/property override intent");
        if (!fields.is_object())
            throw std::runtime_error("Property overrides must be an object");
        if (!values.contains(component))
            values[component] = type->defaults;
        for (const auto& [key, value] : fields.items()) {
            if (!type->defaults.contains(key))
                continue;
            values[component][key] = value;
        }
    }
    detail::validate_components(values);
    return values;
}
void validate_prefab_instances(const Json& scene) {
    std::map<std::string, const Json*> rows;
    for (const auto& e : scene.at("entities"))
        rows[e.at("id")] = &e;
    std::set<std::string> mapped;
    for (const auto& [id, e] : rows) {
        (void)id;
        if ((e->contains("prefab_member") || e->contains("prefab_instance") ||
             e->contains("property_overrides")) &&
            (scene.at("version") != 4 && scene.at("version") != 5))
            throw std::runtime_error("Reserved structured prefab fields require scene4/5");
        if (e->contains("property_overrides") &&
            (!e->at("property_overrides").is_object() ||
             (!e->contains("prefab_member") && !e->contains("prefab_instance"))))
            throw std::runtime_error(
                "Property override intent belongs to a prefab instance/member");
    }
    for (const auto& [id, e] : rows)
        if (e->contains("prefab_instance")) {
            if ((scene.at("version") != 4 && scene.at("version") != 5))
                throw std::runtime_error("Structured prefabs require scene4/5");
            if (e->contains("base") || e->value("prefab", false))
                throw std::runtime_error(
                    "An instance cannot also be a legacy prefab or derive from another base");
            const auto& p = e->at("prefab_instance");
            (void)p.at("asset").get<AssetId>();
            if (!p.at("revision").is_number_integer() || p.at("revision").get<double>() < 1)
                throw std::runtime_error("Invalid prefab revision reference");
            if (!p.at("members").is_object())
                throw std::runtime_error("Invalid instance-member map");
            if (p.at("members").empty() || p.at("members").size() > 16384)
                throw std::runtime_error("Invalid prefab member mapping count");
            unsigned root_count = 0;
            for (const auto& [m, target] : p.at("members").items()) {
                root_count += target == id;
                (void)PrefabMemberId::parse(m);
                const auto t = target.get<EntityId>().str();
                if (!mapped.insert(t).second)
                    throw std::runtime_error("Duplicate instance-member EntityId");
                if (rows.contains(t) && t != id) {
                    const auto& child = *rows.at(t);
                    if (!child.contains("prefab_member") ||
                        child.at("prefab_member") != Json{{"root", id}, {"member", m}})
                        throw std::runtime_error(
                            "Instance mapping does not match member provenance");
                }
            }
            if (root_count != 1)
                throw std::runtime_error("Instance root must have exactly one member mapping");
        }
    for (const auto& [id, e] : rows)
        if (e->contains("prefab_member")) {
            const auto& p = e->at("prefab_member");
            const std::string root = p.at("root"), m = p.at("member");
            if (!rows.contains(root) || !rows.at(root)->contains("prefab_instance") ||
                rows.at(root)->at("prefab_instance").at("members").value(m, "") != id)
                throw std::runtime_error("Orphan prefab member provenance");
            if (e->contains("base") || e->contains("prefab_instance") || e->value("prefab", false))
                throw std::runtime_error("Structured member cannot redefine inheritance");
        }
}
Json reconcile_prefab_intent(const Json& source, const PrefabSources& sources) {
    auto doc = source;
    std::map<std::string, Json> rows;
    for (const auto& e : source.at("entities"))
        rows[e.at("id")] = e;
    for (const auto& root_row : source.at("entities"))
        if (root_row.contains("prefab_instance")) {
            const std::string root_id = root_row.at("id");
            auto& root = rows.at(root_id);
            auto& p = root["prefab_instance"];
            const auto asset = p.at("asset").get<AssetId>();
            if (!sources.contains(asset)) {
                p["status"] = "missing asset";
                continue;
            }
            const PrefabDocument prefab(sources.at(asset));
            p["revision"] = prefab.revision();
            p["status"] = "current";
            auto& mapping = p["members"];
            const auto root_member = prefab.root().str();
            if (mapping.contains(root_member) && mapping.at(root_member) != root_id)
                throw std::runtime_error("Prefab root identity changed incompatibly");
            mapping[root_member] = root_id;
            std::set<std::string> alive;
            for (const auto& item : prefab.source.at("members")) {
                const std::string m = item.at("id");
                alive.insert(m);
                if (!mapping.contains(m))
                    mapping[m] = EntityId::generate();
            }
            for (const auto& item : prefab.source.at("members")) {
                const std::string m = item.at("id"), id = mapping.at(m);
                if (!rows.contains(id))
                    rows[id] = {{"id", id}, {"components", Json::object()}};
                auto& row = rows.at(id);
                if (m == root_member && !row.value("name_override", false))
                    row["name"] = item.at("name");
                if (m != root_member) {
                    row["name"] = item.at("name");
                    row["parent"] = mapping.at(item.at("parent").get<std::string>());
                    row["prefab_member"] = {{"root", root_id}, {"member", m}};
                    row.erase("missing_member");
                    row["spatial"] = item.value("spatial", Json{{"mode", "follow_structure"}});
                    if (row["spatial"].value("mode", "") == "explicit") {
                        const auto target = row["spatial"].at("member").get<std::string>();
                        row["spatial"].erase("member");
                        row["spatial"]["target"] = EntityRef{doc.at("asset_id").get<AssetId>(),
                                                             mapping.at(target).get<EntityId>()};
                    }
                }
                (void)prefab_override_value(item, row);
            }
            for (const auto& [m, id] : mapping.items())
                if (!alive.contains(m) && rows.contains(id.get<std::string>()))
                    rows.at(id.get<std::string>())["missing_member"] = true;
        }
    auto out = Json::array();
    std::set<std::string> done;
    for (const auto& row : source.at("entities")) {
        out.push_back(rows.at(row.at("id")));
        done.insert(row.at("id"));
    }
    for (const auto& [id, row] : rows)
        if (!done.contains(id))
            out.push_back(row);
    // Structured member order follows the published source. Retain ordinary
    // scene row slots and dynamic attachment order; no per-instance order override.
    for (const auto& [root_id, root] : rows) {
        if (!root.contains("prefab_instance"))
            continue;
        const auto& instance = root.at("prefab_instance");
        const auto asset = instance.at("asset").get<AssetId>();
        if (!sources.contains(asset))
            continue;
        std::map<std::string, std::vector<Json>> siblings;
        for (const auto& m : sources.at(asset).at("members")) {
            if (!m.contains("parent"))
                continue;
            const auto id =
                instance.at("members").at(m.at("id").get<std::string>()).get<std::string>();
            const auto& row = rows.at(id);
            siblings[row.at("parent")].push_back(row);
        }
        std::map<std::string, std::size_t> cursor;
        for (auto& row : out) {
            if (!row.contains("prefab_member") || row.at("prefab_member").at("root") != root_id ||
                row.value("missing_member", false))
                continue;
            const auto parent = row.at("parent").get<std::string>();
            row = siblings.at(parent).at(cursor[parent]++);
        }
    }
    doc["entities"] = std::move(out);
    validate_prefab_instances(doc);
    return doc;
}
Json project_prefab_intent(const Json& scene, const PrefabSources& sources) {
    auto doc = scene;
    std::map<std::string, const Json*> roots;
    for (const auto& row : scene.at("entities"))
        if (row.contains("prefab_instance"))
            roots[row.at("id")] = &row;
    for (auto& row : doc["entities"]) {
        const Json* root = nullptr;
        std::string m;
        if (row.contains("prefab_instance"))
            root = &row;
        else if (row.contains("prefab_member")) {
            root = roots.at(row["prefab_member"].at("root"));
            m = row["prefab_member"].at("member");
        }
        if (!root)
            continue;
        const auto asset = root->at("prefab_instance").at("asset").get<AssetId>();
        if (!sources.contains(asset) || row.value("missing_member", false)) {
            if (row.contains("prefab_member"))
                row["missing_member"] = true;
            continue;
        }
        const auto& definition = sources.at(asset);
        if (m.empty())
            m = definition.at("root");
        row["components"] = prefab_override_value(member(definition, m), row);
    }
    return doc;
}
std::optional<EntityRef> prefab_member_reference(const Json& scene, EntityId instance,
                                                 PrefabMemberRef reference) {
    for (const auto& row : scene.at("entities"))
        if (row.at("id") == instance.str() && row.contains("prefab_instance")) {
            const auto& p = row.at("prefab_instance");
            if (p.at("asset").get<AssetId>() != reference.prefab ||
                !p.at("members").contains(reference.member.str()))
                return {};
            return EntityRef{scene.at("asset_id").get<AssetId>(),
                             p.at("members").at(reference.member.str()).get<EntityId>()};
        }
    return {};
}
PrefabMemberRef
remap_prefab_member_reference(PrefabMemberRef reference, AssetId old_asset, AssetId new_asset,
                              const std::map<PrefabMemberId, PrefabMemberId>& members) {
    if (reference.prefab == old_asset) {
        reference.prefab = new_asset;
        if (const auto found = members.find(reference.member); found != members.end())
            reference.member = found->second;
    }
    return reference;
}
void remap_prefab_instances(Json& scene, const std::map<EntityId, EntityId>& remap) {
    for (auto& e : scene["entities"]) {
        if (e.contains("prefab_instance"))
            for (auto& id : e["prefab_instance"]["members"])
                if (auto it = remap.find(id.get<EntityId>()); it != remap.end())
                    id = it->second;
        if (e.contains("prefab_member")) {
            auto& id = e["prefab_member"]["root"];
            if (auto it = remap.find(id.get<EntityId>()); it != remap.end())
                id = it->second;
        }
    }
}
} // namespace forge
