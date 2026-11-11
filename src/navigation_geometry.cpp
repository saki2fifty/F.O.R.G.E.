#include "navigation_geometry.hpp"
#include "asset_bytes.hpp"
#include "builtins.hpp"
#include "navigation_collision_collection.hpp"
#include "navigation_link_value.hpp"
#include "navigation_region_value.hpp"
#include "navigation_space.hpp"
#include <algorithm>
#include <forge/geometry.hpp>
#include <set>
namespace forge {
namespace navigation_detail {
Sources capture(const nlohmann::json& doc, NavigationPlane plane, std::string_view profile) {
    NavigationSettings settings;
    settings.plane = plane;
    settings.profile = profile;
    return capture(doc, settings);
}
Sources capture(const nlohmann::json& doc, const NavigationSettings& settings) {
    const auto plane = settings.plane;
    const std::string_view profile = settings.profile;
    if (!profile.empty())
        validate_profile_name(profile);
    if (plane != NavigationPlane::Spatial3D && plane != NavigationPlane::PlanarXY)
        throw std::runtime_error("Unsupported navigation source plane");
    const auto* marker = plane == NavigationPlane::PlanarXY ? "forge.navigation_region_2d"
                                                            : "forge.navigation_surface";
    const auto has_surface = [&](const Json& components) {
        return components.contains(marker) && components.at(marker).at("enabled").get<bool>();
    };
    const auto has_link = [&](const Json& components) {
        if (profile.empty() || !components.contains("forge.navigation_link"))
            return false;
        const auto& value = components.at("forge.navigation_link");
        return value.at("plane") == unsigned(plane) && value.at("profile") == Json(profile) &&
               value.at("enabled").get<bool>();
    };
    Sources g;
    g.plane = plane;
    g.scene = doc.at("asset_id").get<AssetId>();
    std::map<EntityId, const Json*> included;
    std::map<std::string, const Json*> entities;
    for (const auto& e : doc.at("entities"))
        entities.emplace(e.at("id").get<std::string>(), &e);
    for (const auto& e : doc.at("entities")) {
        const auto& c = e.at("components");
        if (e.value("prefab", false) ||
            (!has_surface(c) && !has_link(c) && !collect_static_collider(c, settings)))
            continue;
        if (included.size() >= 512)
            throw std::runtime_error("Navigation geometry exceeds 512 entities");
        if (!included.emplace(e.at("id").get<EntityId>(), &e).second)
            throw std::runtime_error("Duplicate navigation geometry identity");
    }
    for (const auto& [id, ep] : included) {
        const auto& e = *ep;
        const auto& c = e.at("components");
        const bool automatic = collect_static_collider(c, settings);
        if (has_surface(c) && plane == NavigationPlane::Spatial3D &&
            !c.contains("forge.primitive") && !c.contains("forge.mesh_renderer"))
            throw std::runtime_error("NavigationSurface requires mesh geometry, not a group: " +
                                     id.str());
        for (const char* body : {"forge.physics_body", "forge.physics_body_2d"})
            if (c.contains(body) && c.at(body).at("motion").get<unsigned>() != 0)
                throw std::runtime_error("Navigation source must be static: " + id.str());
        if (!e.value("spatial_resolved", false) || !e.contains("world_affine"))
            throw std::runtime_error("Navigation requires resolved WorldTransform: " + id.str());
        std::set<std::string> ancestry;
        const Json* node = &e;
        while (node) {
            const std::string node_id = node->at("id");
            if (!ancestry.insert(node_id).second)
                throw std::runtime_error("Navigation source spatial cycle");
            const auto& components = node->at("components");
            if (plane == NavigationPlane::PlanarXY &&
                (automatic || (has_surface(c) && c.at(marker).value("source", 0u) == 1)) &&
                components.contains("forge.local_rotation")) {
                const auto& q = components.at("forge.local_rotation");
                physics_detail::validate_planar_rotation_2d(
                    {q.at("x"), q.at("y"), q.at("z"), q.at("w")});
            }
            for (const char* body : {"forge.physics_body", "forge.physics_body_2d"})
                if (components.contains(body) &&
                    components.at(body).at("motion").get<unsigned>() != 0)
                    throw std::runtime_error("Navigation source follows moving physics body: " +
                                             node_id);
            for (const char* controller :
                 {"forge.character_controller", "forge.character_controller_2d"})
                if (components.contains(controller) &&
                    components.at(controller).at("enabled").get<bool>())
                    throw std::runtime_error(
                        "Navigation source follows an enabled character controller: " + node_id);
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
        g.identities.push_back(id);
        if (has_link(c)) {
            const auto value = link_from_value(c.at("forge.navigation_link"));
            g.connections.push_back(derive_link(id, value, transform));
            if (g.connections.size() > navigation_link_limit)
                throw std::runtime_error("Navigation exceeds 32 authored links in this profile");
        }
        if (!has_surface(c) && !automatic)
            continue;
        Surface source{id, transform};
        if (plane == NavigationPlane::PlanarXY) {
            if (automatic) {
                source.region = NavigationRegion2D{};
                source.region->source = 1;
                source.region->obstacle = true;
            } else
                source.region = region_from_value(c.at(marker));
            if (source.region->source == 0)
                (void)region_outline(*source.region, transform);
            else {
                source.collider = collider_source(c, transform);
                if (source.collider->map) {
                    g.maps.insert(source.collider->map);
                    if (g.maps.size() > max_tile_inputs)
                        throw std::runtime_error("Navigation exceeds32 Tile Map inputs");
                }
            }
        } else if (automatic) {
            constexpr const char* shapes[]{"forge.box_collider", "forge.sphere_collider",
                                           "forge.capsule_collider", "forge.cylinder_collider",
                                           "forge.asset_collider"};
            source.collision = Json::object();
            for (const auto* shape : shapes)
                if (c.contains(shape))
                    for (const auto& type : detail::builtins())
                        if (std::string_view(type.name) == shape)
                            source.collision[shape] =
                                detail::canonical_builtin_value(type, c.at(shape));
            if (source.collision.size() != 1)
                throw std::runtime_error(
                    "Collected navigation body requires exactly one Collider 3D: " + id.str());
            if (source.collision.contains("forge.asset_collider")) {
                const auto asset = source.collision.at("forge.asset_collider")
                                       .at("asset")
                                       .get<AssetRef<CollisionAsset>>();
                if (!asset.id)
                    throw std::runtime_error("Collected navigation body needs a Collision asset");
                g.collisions.insert(asset.id);
                if (g.collisions.size() > max_mesh_inputs)
                    throw std::runtime_error("Navigation exceeds32 selected Collision inputs");
            }
        } else {
            source.primitive = primitive_kind(e);
        }
        if (plane == NavigationPlane::Spatial3D && !source.collision.is_null()) {
            // Physical geometry is prepared by the existing shape/resource adapter.
        } else if (plane == NavigationPlane::Spatial3D && source.primitive == no_primitive) {
            if (!c.contains("forge.mesh_renderer") ||
                !c.at("forge.mesh_renderer").value("enabled", true) ||
                c.at("forge.mesh_renderer").at("mesh").is_null())
                throw std::runtime_error("NavigationSurface requires enabled mesh geometry: " +
                                         id.str());
            source.mesh = c.at("forge.mesh_renderer").at("mesh").get<AssetId>();
            if (engine_asset(source.mesh))
                throw std::runtime_error("Unsupported navigation engine mesh");
            g.meshes.insert(source.mesh);
            if (g.meshes.size() > max_mesh_inputs)
                throw std::runtime_error("Navigation exceeds 32 selected Mesh inputs");
        } else if (plane == NavigationPlane::Spatial3D &&
                   source.primitive >= primitive_meshes().size())
            throw std::runtime_error("Unsupported navigation primitive");
        g.surfaces.push_back(source);
    }
    validate_navigation_links(g.connections, plane);
    if (g.surfaces.empty())
        throw std::runtime_error(plane == NavigationPlane::PlanarXY
                                     ? "No enabled NavigationRegion2D geometry"
                                     : "No enabled NavigationSurface geometry");
    return g;
}
nlohmann::json mesh_inputs(const Sources& source,
                           const std::function<std::string(AssetId)>& revision) {
    Json result = Json::array();
    for (auto id : source.meshes)
        result.push_back({{"asset_id", id}, {"revision", revision(id)}});
    return result;
}
Json tile_inputs(const Sources& source, const std::function<std::string(AssetId)>& revision) {
    Json values = Json::array();
    if (source.maps.size() > max_tile_inputs ||
        (source.plane != NavigationPlane::PlanarXY && !source.maps.empty()))
        throw std::runtime_error("Invalid navigation Tile Map input profile");
    for (auto id : source.maps)
        values.push_back({{"asset_id", id}, {"revision", revision(id)}});
    return values;
}
Json collision_inputs(const Sources& source, const std::function<std::string(AssetId)>& revision) {
    if (source.collisions.size() > max_mesh_inputs ||
        (source.plane != NavigationPlane::Spatial3D && !source.collisions.empty()))
        throw std::runtime_error("Invalid navigation Collision input profile");
    Json values = Json::array();
    for (auto id : source.collisions)
        values.push_back({{"asset_id", id}, {"revision", revision(id)}});
    return values;
}
std::string source_digest(const Sources& source, const Json& inputs, const Json& collisions) {
    if (!collisions.is_array() || collisions.size() != source.collisions.size())
        throw std::runtime_error("Navigation requires selected Collision input provenance");
    if (source.plane == NavigationPlane::PlanarXY) {
        if (!inputs.is_array() || (!collider_sources(source) && !inputs.empty()))
            throw std::runtime_error("XY regions do not use external Mesh provenance");
        Json regions = Json::array();
        for (const auto& surface : source.surfaces) {
            if (!surface.region)
                throw std::runtime_error("XY navigation needs region geometry");
            const auto& matrix = surface.transform.m;
            const auto& region = *surface.region;
            regions.push_back(
                {{"entity", surface.entity},
                 {"xy", {matrix[0], matrix[1], matrix[3], matrix[4], matrix[5], matrix[7]}},
                 {"width", region.width},
                 {"height", region.height},
                 {"obstacle", region.obstacle}});
            if (surface.collider) {
                regions.back().erase("width");
                regions.back().erase("height");
                regions.back()["collider"] = collider_source_value(*surface.collider);
            } else if (!region.points.empty()) {
                regions.back().erase("width");
                regions.back().erase("height");
                regions.back()["points"] = region_value(region).at("points");
            }
        }
        auto value = Json{{"bridge", collider_sources(source) ? 5 : 3},
                          {"plane", 1},
                          {"scene", source.scene},
                          {"regions", regions}};
        if (collider_sources(source))
            value["tiles"] = inputs;
        if (!source.connections.empty())
            value["links"] = navigation_link_values(source.connections, source.plane);
        const auto text = value.dump();
        return asset_detail::content_digest(std::as_bytes(std::span(text)));
    }
    Json surfaces = Json::array();
    for (const auto& s : source.surfaces) {
        Json value = {{"entity", s.entity}, {"affine", s.transform.m}};
        if (!s.collision.is_null())
            value["collision"] = s.collision;
        else if (s.mesh)
            value["mesh"] = s.mesh;
        else
            value["primitive"] = s.primitive;
        surfaces.push_back(std::move(value));
    }
    auto value =
        Json{{"bridge", 2}, {"scene", source.scene}, {"surfaces", surfaces}, {"meshes", inputs}};
    if (!collisions.empty())
        value["collisions"] = collisions;
    if (!source.connections.empty())
        value["links"] = navigation_link_values(source.connections, source.plane);
    const auto text = value.dump();
    return asset_detail::content_digest(std::as_bytes(std::span(text)));
}
namespace {
Geometry planar_geometry(const Sources& source, const Json& inputs = Json::array(),
                         const std::function<RegionContours(const Surface&)>& maps = {}) {
    Geometry result;
    result.scene = source.scene;
    result.sources = source.identities;
    result.connections = source.connections;
    for (const auto& surface : source.surfaces) {
        if (!surface.region)
            throw std::runtime_error("XY navigation needs region geometry");
        if (surface.collider) {
            RegionContours outlines;
            if (surface.collider->kind == 3) {
                if (!maps)
                    throw std::runtime_error(
                        "Navigation Tile Map collision geometry is not prepared");
                outlines = maps(surface);
            } else
                outlines.push_back(
                    collider_outline_2d(surface.collider->kind, surface.collider->dimensions,
                                        surface.transform, surface.region->obstacle));
            for (const auto& outline : outlines) {
                std::vector<geometry_detail::PolygonPoint> xy;
                for (const auto& point : outline)
                    xy.push_back({point[0], point[1]});
                if (surface.region->obstacle) {
                    if (result.obstacles.size() >= 512)
                        throw std::runtime_error("Navigation exceeds512 convex obstacle masks");
                    std::vector<float> mask;
                    for (const auto& point : xy)
                        mask.insert(mask.end(), {float(point[0]), 0, float(point[1])});
                    result.obstacles.push_back(std::move(mask));
                } else {
                    for (const auto& triangle : geometry_detail::polygon_triangles(xy)) {
                        if (result.indices.size() / 3 >= 16384)
                            throw std::runtime_error("Navigation geometry exceeds16384 triangles");
                        for (auto corner : {triangle[0], triangle[2], triangle[1]}) {
                            result.indices.push_back(int(result.indices.size()));
                            result.vertices.insert(result.vertices.end(),
                                                   {float(xy[corner][0]), 0, float(xy[corner][1])});
                        }
                    }
                }
            }
            continue;
        }
        if (!surface.region->points.empty()) {
            const auto outline = region_outline(*surface.region, surface.transform);
            std::vector<std::array<float, 3>> native;
            std::vector<geometry_detail::PolygonPoint> rounded;
            for (const auto& value : outline) {
                const auto point = native_coordinates(MeshSpace::PlanarXY, value);
                native.push_back({float(point[0]), float(point[1]), float(point[2])});
                rounded.push_back({native.back()[0], native.back()[2]});
            }
            // Decompose the actual float coordinates handed to the solver. A
            // source contour must not silently collapse during native conversion.
            const auto triangles = geometry_detail::polygon_triangles(rounded);
            for (const auto& triangle : triangles) {
                if (surface.region->obstacle) {
                    if (result.obstacles.size() == 512)
                        throw std::runtime_error("Navigation exceeds512 convex obstacle masks");
                    std::vector<float> mask;
                    for (auto corner : triangle)
                        mask.insert(mask.end(), native[corner].begin(), native[corner].end());
                    result.obstacles.push_back(std::move(mask));
                } else {
                    if (result.indices.size() / 3 == 16384)
                        throw std::runtime_error("Navigation geometry exceeds16384 triangles");
                    for (auto corner : {triangle[0], triangle[2], triangle[1]}) {
                        result.indices.push_back(int(result.indices.size()));
                        result.vertices.insert(result.vertices.end(), native[corner].begin(),
                                               native[corner].end());
                    }
                }
            }
            continue;
        }
        const auto quad = region_quad(*surface.region, surface.transform);
        std::array<float, 12> native{};
        for (unsigned i = 0; i < 4; ++i) {
            const auto point = native_coordinates(MeshSpace::PlanarXY, quad[i]);
            for (unsigned axis = 0; axis < 3; ++axis)
                native[i * 3 + axis] = float(point[axis]);
        }
        if (surface.region->obstacle) {
            if (result.obstacles.size() == 512)
                throw std::runtime_error("Navigation exceeds512 convex obstacle masks");
            result.obstacles.emplace_back(native.begin(), native.end());
            continue;
        }
        if (result.indices.size() / 3 > 16384 - 2)
            throw std::runtime_error("Navigation geometry exceeds16384 triangles");
        // Positive world-XY winding maps to downward native Y. Reverse each
        // triangle so the existing slope classifier sees a walkable floor.
        for (unsigned corner : {0u, 2u, 1u, 0u, 3u, 2u}) {
            result.indices.push_back(int(result.indices.size()));
            result.vertices.insert(result.vertices.end(), native.begin() + corner * 3,
                                   native.begin() + corner * 3 + 3);
        }
    }
    if (result.vertices.empty())
        throw std::runtime_error("XY navigation needs at least one walkable region");
    result.digest = source_digest(source, inputs);
    return result;
}
} // namespace
namespace {
void triangle(Geometry& g, AffineTransform transform, Float3 a, Float3 b, Float3 d) {
    if (transform_parity(transform) == TransformParity::Negative)
        std::swap(b, d);
    std::array<Float3, 3> points;
    unsigned next = 0;
    for (auto p : {a, b, d}) {
        const auto world = transform.point({p[0], p[1], p[2]});
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(world[axis]) || std::abs(world[axis]) > 4090)
                throw std::runtime_error("Navigation geometry must lie within +/-4090 metres");
            points[next][axis] = float(world[axis]);
        }
        ++next;
    }
    const auto area = geom_cross(geom_sub(points[1], points[0]), geom_sub(points[2], points[0]));
    if (geom_dot(area, area) < 1e-14f)
        return;
    if (g.indices.size() / 3 >= 16384)
        throw std::runtime_error("Navigation geometry exceeds 16384 triangles");
    for (auto p : points) {
        g.indices.push_back(int(g.indices.size()));
        g.vertices.insert(g.vertices.end(), p.begin(), p.end());
    }
}
Geometry triangles(const Sources& source,
                   const std::function<const MeshResourceData&(AssetId)>& resolve,
                   const std::function<PhysicsDebugCollision(AssetId)>& collisions = {}) {
    Geometry g;
    g.scene = source.scene;
    g.sources = source.identities;
    g.connections = source.connections;
    for (const auto& s : source.surfaces) {
        const auto transform = s.transform;
        if (!s.collision.is_null()) {
            PhysicsDebugCollision selected;
            if (s.collision.contains("forge.asset_collider")) {
                if (!collisions)
                    throw std::runtime_error("Navigation Collision asset is not prepared");
                const auto asset = s.collision.at("forge.asset_collider")
                                       .at("asset")
                                       .get<AssetRef<CollisionAsset>>();
                selected = collisions(asset.id);
            }
            const auto remaining = 16384 - g.indices.size() / 3;
            if (!remaining)
                throw std::runtime_error("Navigation geometry exceeds 16384 triangles");
            const auto physical =
                collision_geometry(s.collision, transform, std::move(selected), remaining);
            for (const auto& points : physical.shape.triangles)
                triangle(g, physical.rigid, points[0], points[1], points[2]);
            continue;
        }
        if (s.mesh) {
            if (!resolve)
                throw std::runtime_error("Legacy navigation requires built-in mesh geometry: " +
                                         s.entity.str());
            const auto& data = resolve(s.mesh);
            const auto& mesh = data.mesh;
            validate_mesh(mesh, mesh_limits);
            if (!mesh.morph_names.empty() || !mesh.morph_defaults.empty())
                throw std::runtime_error("Navigation does not bake morphing Meshes");
            if (data.model)
                for (const auto& node : data.model->nodes)
                    if (node.skin || !node.morph_weights.empty())
                        throw std::runtime_error(
                            "Navigation does not bake skinned or morphing Mesh nodes");
            for (const auto& part : mesh.lods.at(0).parts) {
                if (part.topology != MeshTopology::Triangles || !part.joint_palette.empty() ||
                    !part.morph_targets.empty())
                    throw std::runtime_error("Navigation requires static triangle Mesh parts");
                for (const auto& stream : part.streams)
                    if (stream.semantic.starts_with("JOINTS_") ||
                        stream.semantic.starts_with("WEIGHTS_"))
                        throw std::runtime_error("Navigation does not bake skinned Meshes");
                const auto* stream = part.find("POSITION");
                if (!stream || stream->components != 3 ||
                    !std::holds_alternative<std::vector<float>>(stream->values))
                    throw std::runtime_error("Navigation Mesh requires float3 POSITION");
                const auto& positions = std::get<std::vector<float>>(stream->values);
                auto point = [&](std::size_t corner) {
                    const auto index = part.indices.empty() ? corner : part.indices.at(corner);
                    return Float3{positions.at(index * 3), positions.at(index * 3 + 1),
                                  positions.at(index * 3 + 2)};
                };
                const auto count = part.indices.empty() ? part.vertices : part.indices.size();
                for (std::size_t t = 0; t < count; t += 3)
                    triangle(g, transform, point(t), point(t + 1), point(t + 2));
            }
            continue;
        }
        const auto kind = s.primitive;
        const auto& mesh = primitive_meshes()[kind];
        for (std::size_t t = 0; t < mesh.size(); t += 3) {
            auto a = mesh[t].position, b = mesh[t + 1].position, d = mesh[t + 2].position;
            auto cross = geom_cross(geom_sub(b, a), geom_sub(d, a));
            if (geom_dot(cross, cross) < 1e-14f)
                continue;
            if (geom_dot(cross, mesh[t].normal) < 0)
                std::swap(b, d);
            triangle(g, transform, a, b, d);
        }
    }
    if (g.indices.empty())
        throw std::runtime_error("No enabled NavigationSurface geometry");
    return g;
}
} // namespace
Geometry geometry(const nlohmann::json& doc) { return geometry(capture(doc)); }
Geometry geometry(const Sources& source) {
    if (source.plane == NavigationPlane::PlanarXY)
        return planar_geometry(source);
    auto g = triangles(source, {});
    Json canonical = {
        {"bridge", 1}, {"scene", g.scene}, {"sources", g.sources}, {"vertices", g.vertices}};
    if (!source.connections.empty())
        canonical["links"] = navigation_link_values(source.connections, source.plane);
    auto text = canonical.dump();
    g.digest = asset_detail::content_digest(std::as_bytes(std::span(text)));
    return g;
}
Geometry geometry(const Sources& source, const Json& inputs,
                  const std::function<const MeshResourceData&(AssetId)>& mesh,
                  const std::function<RegionContours(const Surface&)>& maps,
                  const std::function<PhysicsDebugCollision(AssetId)>& collisions,
                  const Json& collision_provenance) {
    if (source.plane == NavigationPlane::PlanarXY) {
        if (!inputs.is_array() || (!collider_sources(source) && !inputs.empty()))
            throw std::runtime_error("XY regions do not use external Mesh provenance");
        return planar_geometry(source, inputs, maps);
    }
    auto g = triangles(source, mesh, collisions);
    g.digest = source_digest(source, inputs, collision_provenance);
    return g;
}
} // namespace navigation_detail
std::string navigation_geometry_digest(const nlohmann::json& doc) {
    return navigation_detail::geometry(doc).digest;
}
} // namespace forge
