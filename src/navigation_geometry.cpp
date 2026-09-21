#include "navigation_geometry.hpp"
#include "asset_bytes.hpp"
#include <forge/geometry.hpp>
#include <set>
namespace forge {
void validate_navigation_settings(const NavigationSettings& s) {
    auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    if (!range(s.radius, .05f, 5) || !range(s.height, .2f, 10) || !range(s.climb, 0, 2) ||
        !range(s.slope, 0, 60) || !range(s.cell_size, .05f, 1) ||
        !range(s.cell_height, .025f, .5f) || s.climb >= s.height || s.cell_height > s.height / 2)
        throw std::runtime_error("Invalid navigation settings: radius .05-5 m, height .2-10 m, "
                                 "climb 0-2 m below height, slope 0-60 degrees, cells .05-1 m / "
                                 ".025-.5 m; at least two cells of clearance required");
}
void to_json(nlohmann::json& j, const NavigationSettings& s) {
    j = {{"radius", s.radius}, {"height", s.height},       {"climb", s.climb},
         {"slope", s.slope},   {"cell_size", s.cell_size}, {"cell_height", s.cell_height}};
}
void from_json(const nlohmann::json& j, NavigationSettings& s) {
    s = {j.at("radius"), j.at("height"),    j.at("climb"),
         j.at("slope"),  j.at("cell_size"), j.at("cell_height")};
    validate_navigation_settings(s);
}
namespace navigation_detail {
Geometry geometry(const nlohmann::json& doc) {
    Geometry g;
    g.scene = doc.at("asset_id").get<AssetId>();
    std::map<EntityId, const Json*> included;
    std::map<std::string, const Json*> entities;
    for (const auto& e : doc.at("entities"))
        entities.emplace(e.at("id").get<std::string>(), &e);
    for (const auto& e : doc.at("entities")) {
        const auto& c = e.at("components");
        if (e.value("prefab", false) || !c.contains("forge.navigation_surface") ||
            !c.at("forge.navigation_surface").at("enabled").get<bool>())
            continue;
        if (included.size() >= 512)
            throw std::runtime_error("Navigation geometry exceeds 512 entities");
        if (!included.emplace(e.at("id").get<EntityId>(), &e).second)
            throw std::runtime_error("Duplicate navigation geometry identity");
    }
    for (const auto& [id, ep] : included) {
        const auto& e = *ep;
        const auto& c = e.at("components");
        if ((!c.contains("forge.primitive") && !c.contains("forge.mesh_renderer")) ||
            primitive_kind(e) == no_primitive)
            throw std::runtime_error("NavigationSurface requires built-in mesh geometry: " +
                                     id.str());
        if (c.contains("forge.physics_body") &&
            c.at("forge.physics_body").at("motion").get<unsigned>() != 0)
            throw std::runtime_error("NavigationSurface must be static: " + id.str());
        if (!e.value("spatial_resolved", false) || !e.contains("world_affine"))
            throw std::runtime_error("Navigation requires resolved WorldTransform: " + id.str());
        std::set<std::string> ancestry;
        const Json* node = &e;
        while (node) {
            const std::string node_id = node->at("id");
            if (!ancestry.insert(node_id).second)
                throw std::runtime_error("Navigation source spatial cycle");
            const auto& components = node->at("components");
            if (components.contains("forge.physics_body") &&
                components.at("forge.physics_body").at("motion").get<unsigned>() != 0)
                throw std::runtime_error("Navigation source follows moving physics body: " +
                                         node_id);
            if (components.contains("forge.navigation_agent") &&
                components.at("forge.navigation_agent").at("enabled").get<bool>())
                throw std::runtime_error("Navigation source follows an enabled NavigationAgent: " +
                                         node_id);
            auto spatial = node->value("spatial", Json::object());
            auto mode = spatial.value("mode", std::string("follow_structure"));
            if (mode == "world")
                break;
            std::string parent;
            if (mode == "explicit") {
                auto ref = spatial.at("target").get<EntityRef>();
                if (ref.scene != g.scene)
                    throw std::runtime_error(
                        "Navigation source has unsupported external spatial parent");
                parent = ref.entity.str();
            } else
                parent = node->value("parent", std::string{});
            if (parent.empty())
                break;
            if (!entities.contains(parent))
                throw std::runtime_error("Navigation source spatial parent missing");
            node = entities.at(parent);
        }
        AffineTransform transform{e.at("world_affine").get<std::array<double, 12>>()};
        // Keep TRS representability validation; signed/zero visual scales are valid.
        // Forward geometry handles their winding and skips collapsed triangles.
        (void)decompose(transform);
        auto kind = primitive_kind(e);
        if (kind >= primitive_meshes().size())
            throw std::runtime_error("Unsupported navigation primitive");
        g.sources.push_back(id);
        const auto& mesh = primitive_meshes()[kind];
        for (std::size_t t = 0; t < mesh.size(); t += 3) {
            auto a = mesh[t].position, b = mesh[t + 1].position, d = mesh[t + 2].position;
            auto cross = geom_cross(geom_sub(b, a), geom_sub(d, a));
            if (geom_dot(cross, cross) < 1e-14f)
                continue;
            if (geom_dot(cross, mesh[t].normal) < 0)
                std::swap(b, d);
            if (transform_parity(transform) == TransformParity::Negative)
                std::swap(b, d);
            std::array<Float3, 3> points;
            unsigned next = 0;
            for (auto p : {a, b, d}) {
                const auto world = transform.point({p[0], p[1], p[2]});
                for (unsigned axis = 0; axis < 3; ++axis) {
                    if (!std::isfinite(world[axis]) || std::abs(world[axis]) > 4090)
                        throw std::runtime_error(
                            "Navigation geometry must lie within +/-4090 metres");
                    points[next][axis] = float(world[axis]);
                }
                ++next;
            }
            const auto area =
                geom_cross(geom_sub(points[1], points[0]), geom_sub(points[2], points[0]));
            if (geom_dot(area, area) < 1e-14f)
                continue;
            if (g.indices.size() / 3 >= 16384)
                throw std::runtime_error("Navigation geometry exceeds 16384 triangles");
            for (auto p : points) {
                g.indices.push_back(int(g.indices.size()));
                g.vertices.insert(g.vertices.end(), p.begin(), p.end());
            }
        }
    }
    if (g.indices.empty())
        throw std::runtime_error("No enabled NavigationSurface geometry");
    Json canonical = {
        {"bridge", 1}, {"scene", g.scene}, {"sources", g.sources}, {"vertices", g.vertices}};
    auto text = canonical.dump();
    g.digest = asset_detail::content_digest(std::as_bytes(std::span(text)));
    return g;
}
} // namespace navigation_detail
std::string navigation_geometry_digest(const nlohmann::json& doc) {
    return navigation_detail::geometry(doc).digest;
}
} // namespace forge
