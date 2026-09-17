#include "spatial_document.hpp"
#include <cmath>
#include <set>
#include <stdexcept>
namespace forge::detail {
namespace {
Json& row(Json& doc, const std::string& id) {
    for (auto& e : doc["entities"])
        if (e.at("id") == id)
            return e;
    throw std::runtime_error("Missing transform entity: " + id);
}
const Json& row(const Json& doc, const std::string& id) {
    for (const auto& e : doc.at("entities"))
        if (e.at("id") == id)
            return e;
    throw std::runtime_error("Missing transform entity: " + id);
}
Json effective_locals(const Json& doc) {
    auto out = doc;
    std::map<std::string, const Json*> source;
    for (const auto& e : doc.at("entities"))
        source[e.at("id")] = &e;
    for (auto& e : out["entities"]) {
        const Json* base = &e;
        for (std::size_t i = 0; i < source.size() && base->contains("base"); ++i) {
            base = source.at(base->at("base"));
            for (auto name :
                 {"forge.local_translation", "forge.local_rotation", "forge.local_scale"})
                if (!e["components"].contains(name) && base->at("components").contains(name))
                    e["components"][name] = base->at("components").at(name);
        }
    }
    return out;
}
AffineTransform matrix(const Json& e) {
    if (!e.value("spatial_resolved", false) || !e.contains("world_affine"))
        throw std::runtime_error("Spatial parent is missing or unresolved");
    return {e.at("world_affine").get<std::array<double, 12>>()};
}
} // namespace
Json encode(LocalTranslation v) { return {{"x", v.x}, {"y", v.y}, {"z", v.z}}; }
Json encode(LocalRotation v) { return {{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}}; }
Json encode(LocalScale v) { return {{"x", v.x}, {"y", v.y}, {"z", v.z}}; }
LocalTransform read_local(const Json& c) {
    LocalTransform t;
    if (c.contains("forge.local_translation")) {
        const auto& p = c.at("forge.local_translation");
        t.translation = {p.at("x"), p.at("y"), p.at("z")};
    }
    if (c.contains("forge.local_rotation")) {
        const auto& p = c.at("forge.local_rotation");
        t.rotation = {p.at("x"), p.at("y"), p.at("z"), p.at("w")};
    }
    if (c.contains("forge.local_scale")) {
        const auto& p = c.at("forge.local_scale");
        t.scale = {p.at("x"), p.at("y"), p.at("z")};
    }
    (void)affine_transform(t);
    return t;
}
SpatialBinding read_binding(const Json& e) {
    SpatialBinding b;
    if (!e.contains("spatial"))
        return b;
    const auto& s = e.at("spatial");
    const auto mode = s.at("mode").get<std::string>();
    if (mode == "world")
        b.mode = SpatialMode::World;
    else if (mode == "follow_structure")
        b.mode = SpatialMode::FollowStructure;
    else if (mode == "explicit") {
        b.mode = SpatialMode::Explicit;
        b.target = s.at("target").get<EntityRef>();
    } else
        throw std::runtime_error("Unknown spatial binding mode");
    if (b.mode != SpatialMode::Explicit && s.contains("target"))
        throw std::runtime_error("Only explicit spatial binding may specify a target");
    return b;
}
Json migrate_transforms(const Json& source) {
    if (source.at("version") == 3)
        return source;
    if (source.at("version") != 2)
        throw std::runtime_error("Transform migration requires scene-v2 identity");
    auto doc = source;
    doc["version"] = 3;
    for (auto& e : doc["entities"]) {
        if (e.contains("spatial") || e.contains("world_affine") || e.contains("spatial_resolved"))
            throw std::runtime_error(
                "Legacy scene uses reserved transform fields; original preserved");
        auto& c = e["components"];
        for (auto name : {"forge.local_translation", "forge.local_rotation", "forge.local_scale",
                          "forge.world_transform"})
            if (c.contains(name))
                throw std::runtime_error(
                    "Legacy component conflicts with reserved transform type; original preserved");
        if (c.contains("forge.position")) {
            c["forge.local_translation"] = c.at("forge.position");
            for (auto axis : {"x", "y", "z"})
                c["forge.local_translation"][axis] =
                    double(c.at("forge.position").at(axis).get<float>());
            c.erase("forge.position");
        }
        if (c.contains("forge.scale")) {
            c["forge.local_scale"] = c.at("forge.scale");
            c.erase("forge.scale");
        }
        if (c.contains("forge.rotation")) {
            const auto old = c.at("forge.rotation");
            auto extras = old;
            for (auto axis : {"x", "y", "z"})
                extras.erase(axis);
            c["forge.local_rotation"] = encode(rotation_from_euler(
                {old.at("x").get<float>(), old.at("y").get<float>(), old.at("z").get<float>()}));
            if (!extras.empty())
                c["forge.local_rotation"]["legacy_euler_fields"] = extras;
            c.erase("forge.rotation");
        }
        e["spatial"] = {{"mode", "world"}};
    }
    return doc;
}
Json project_spatial(Json doc) {
    std::map<std::string, std::uint64_t> ids;
    std::uint64_t n = 0;
    for (const auto& e : doc.at("entities"))
        if (e.at("components").contains("forge.local_translation"))
            ids[e.at("id")] = ++n;
    std::map<std::uint64_t, TransformNode> nodes;
    for (auto& e : doc["entities"]) {
        const auto id = e.at("id").get<std::string>();
        if (!ids.contains(id))
            continue;
        TransformNode node{read_local(e.at("components"))};
        auto b = read_binding(e);
        const auto structural =
            ids.contains(e.value("parent", std::string{})) ? ids.at(e.at("parent")) : 0;
        const auto explicit_target = b.mode == SpatialMode::Explicit &&
                                             b.target.scene == doc.at("asset_id").get<AssetId>() &&
                                             ids.contains(b.target.entity.str())
                                         ? ids.at(b.target.entity.str())
                                         : 0;
        const auto parent = effective_spatial_parent(b.mode, structural, explicit_target);
        node.parent = parent.entity;
        node.parent_resolved = parent.resolved;
        nodes[ids.at(id)] = node;
    }
    const auto evaluated = evaluate_transforms(nodes);
    for (auto& e : doc["entities"]) {
        const auto id = e.at("id").get<std::string>();
        if (!ids.contains(id))
            continue;
        const auto& world = evaluated.at(ids.at(id));
        e["world_affine"] = world.affine.m;
        e["spatial_resolved"] = world.resolved;
        auto local = read_local(e.at("components"));
        auto& c = e["components"];
        // Read-only display adapters. Never accepted as canonical v3 persistence.
        c["forge.position"] = c.at("forge.local_translation");
        auto angles = rotation_to_euler(local.rotation);
        c["forge.rotation"] = {{"x", angles[0]}, {"y", angles[1]}, {"z", angles[2]}};
        c["forge.scale"] = c.value("forge.local_scale", encode(local.scale));
    }
    return doc;
}
void validate_spatial(const Json& doc) {
    for (const auto& e : doc.at("entities")) {
        (void)read_binding(e);
        (void)read_local(e.at("components"));
        if (e.contains("world_affine") || e.contains("spatial_resolved") ||
            e.at("components").contains("forge.world_transform"))
            throw std::runtime_error("WorldTransform is derived and cannot be authored");
        for (auto old : {"forge.position", "forge.rotation", "forge.scale"})
            if (e.at("components").contains(old))
                throw std::runtime_error("Scene-v3 requires canonical local transform components");
        const auto& c = e.at("components");
        if (c.contains("forge.local_rotation")) {
            const auto& q = c.at("forge.local_rotation");
            double length = 0;
            for (auto axis : {"x", "y", "z", "w"}) {
                double v = q.at(axis);
                length += v * v;
            }
            if (!std::isfinite(length) || std::abs(length - 1) > 2e-6)
                throw std::runtime_error("Local rotation must be a normalized quaternion");
        }
    }
    (void)project_spatial(effective_locals(doc));
}
void write_local(Json& e, const LocalTransform& current, const LocalTransform& desired,
                 TransformChannel channels, bool changed_only) {
    (void)affine_transform(desired);
    auto& c = e["components"];
    auto mask = unsigned(channels);
    if (mask == 0 || mask > 7)
        throw std::runtime_error("Explicit transform write channels required");
    if ((mask & 1) && (!changed_only || !equivalent(current.translation, desired.translation)))
        c["forge.local_translation"].update(encode(desired.translation));
    if ((mask & 2) && (!changed_only || !equivalent(current.rotation, desired.rotation)))
        c["forge.local_rotation"].update(encode(normalized(desired.rotation)));
    if ((mask & 4) && (!changed_only || !equivalent(current.scale, desired.scale)))
        c["forge.local_scale"].update(encode(desired.scale));
}
void write_world(Json& authored, const Json& effective, const std::string& id,
                 const AffineTransform& desired, TransformChannel channels, bool changed_only) {
    const auto& e = row(effective, id);
    const auto old_local = read_local(e.at("components"));
    // Parent affine = old_world * inverse(old_local). Shared graph owns parent semantics.
    const auto parent = matrix(e) * inverse(affine_transform(old_local));
    auto local = decompose(inverse(parent) * desired);
    const auto mask = unsigned(channels);
    if (!(mask & 1) && !equivalent(old_local.translation, local.translation))
        throw std::runtime_error("Operation would also require a translation override");
    if (!(mask & 2) && !equivalent(old_local.rotation, local.rotation))
        throw std::runtime_error("Operation would also require a rotation override");
    if (!(mask & 4) && !equivalent(old_local.scale, local.scale))
        throw std::runtime_error("Operation would also require a scale override");
    write_local(row(authored, id), old_local, local, channels, changed_only);
}
void rebind(Json& doc, const Json& effective, const std::string& id, const Json& spatial,
            const std::string* structural, ReparentMode mode) {
    auto candidate = doc;
    auto& e = row(candidate, id);
    const auto& old = row(effective, id);
    if (structural) {
        if (structural->empty())
            e.erase("parent");
        else
            e["parent"] = *structural;
    }
    e["spatial"] = spatial;
    // Validate new graph before compensation; missing explicit target never becomes a root.
    auto projected = project_spatial(effective_locals(candidate));
    if (mode == ReparentMode::PreserveWorld &&
        old.at("components").contains("forge.local_translation"))
        write_world(candidate, projected, id, matrix(old), TransformChannel::All, true);
    doc = std::move(candidate);
}
void write_channel(Json& e, const char* name, const Json& values) {
    std::string old = name;
    auto& c = e["components"];
    if (old == "forge.position")
        c["forge.local_translation"].update(values);
    else if (old == "forge.scale")
        c["forge.local_scale"].update(values);
    else if (old == "forge.rotation")
        c["forge.local_rotation"].update(
            encode(rotation_from_euler({values.at("x"), values.at("y"), values.at("z")})));
    else
        c[name].update(values);
}
void remap_spatial(Json& e, AssetId source, AssetId destination,
                   const std::map<EntityId, EntityId>& remap) {
    auto b = read_binding(e);
    if (b.mode == SpatialMode::Explicit)
        e["spatial"]["target"] = remap_entity_ref(b.target, source, destination, remap);
}
} // namespace forge::detail
