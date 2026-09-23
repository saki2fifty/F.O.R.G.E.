#include "collision_validation.hpp"
#include "cooked_envelope.hpp"
#include <bit>
#include <cmath>
#include <forge/collision_asset.hpp>
#include <functional>
#include <limits>
#include <numeric>
#include <set>

namespace forge {
namespace {
constexpr std::array<std::byte, 8> magic{std::byte{'F'}, std::byte{'R'}, std::byte{'G'},
                                         std::byte{'C'}, std::byte{'O'}, std::byte{'L'},
                                         std::byte{'L'}, std::byte{0}};
constexpr std::size_t metadata_limit = 1024 * 1024;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void add(std::size_t& total, std::size_t n, std::size_t maximum) {
    require(n <= maximum && total <= maximum - n, "Collision aggregate budget exceeded");
    total += n;
}
std::size_t count(const nlohmann::json& j, std::size_t maximum) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Collision count must be a nonnegative integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= maximum, "Collision count exceeds budget");
    return static_cast<std::size_t>(n);
}
template <std::size_t N> std::array<float, N> floats(const nlohmann::json& j) {
    require(j.is_array() && j.size() == N, "Invalid collision vector");
    std::array<float, N> result;
    for (std::size_t i = 0; i < N; ++i) {
        require(j[i].is_number(), "Collision vector must be numeric");
        const auto value = j[i].get<double>();
        require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
                "Collision scalar exceeds finite float range");
        result[i] = static_cast<float>(value);
        require(value == 0 || result[i] != 0, "Collision scalar underflows float");
    }
    return result;
}
std::array<double, 3> subtract(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {double(a[0]) - b[0], double(a[1]) - b[1], double(a[2]) - b[2]};
}
std::array<double, 3> cross(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
} // namespace
void collision_detail::validate(const CollisionData& data, CollisionLimits limits,
                                bool require_geometry) {
    require(!data.nodes.empty() && data.nodes.size() <= limits.nodes &&
                data.root < data.nodes.size(),
            "Invalid collision node count/root");
    std::size_t vertices = 0, triangles = 0, bytes = 0, edges = 0;
    std::set<CollisionMemberId> identities;
    for (const auto& node : data.nodes) {
        require(bool(node.id) && identities.insert(node.id).second,
                "Missing/duplicate collision member identity");
        for (auto v : node.translation)
            require(std::isfinite(v) && std::abs(v) <= 1000000,
                    "Collision translation outside supported meter range");
        for (auto v : node.scale)
            require(std::isfinite(v) && v != 0 && std::abs(v) <= 10000,
                    "Collision scale must be finite, nonzero and within visual scale range");
        double norm = 0;
        for (auto v : node.rotation) {
            require(std::isfinite(v), "Non-finite collision quaternion");
            norm += double(v) * v;
        }
        require(std::abs(norm - 1) <= .00001, "Collision quaternion must be normalized");
        for (auto v : node.dimensions)
            require(std::isfinite(v) && v >= 0 && v <= 10000,
                    "Collision dimension outside supported meter range");
        add(vertices, node.vertices.size(), limits.vertices);
        require(node.vertices.size() <= limits.bytes / 12 &&
                    node.indices.size() <= limits.bytes / 4,
                "Collision geometry exceeds byte budget");
        add(bytes, node.vertices.size() * 12, limits.bytes);
        add(bytes, node.indices.size() * 4, limits.bytes);
        add(edges, node.children.size(), data.nodes.size() - 1);
        for (const auto& v : node.vertices)
            for (float x : v)
                require(std::isfinite(x) && std::abs(x) <= 1000000,
                        "Collision vertex outside supported finite meter range");
        const bool hull = node.kind == CollisionKind::ConvexHull;
        const bool mesh = node.kind == CollisionKind::TriangleMesh;
        const bool compound = node.kind == CollisionKind::Compound;
        require(hull || mesh || node.vertices.empty(), "Primitive/compound has stray geometry");
        require(mesh || node.indices.empty(), "Only triangle collision accepts indices");
        require(compound || node.children.empty(), "Only compound collision accepts children");
        if (hull || mesh || compound)
            require(node.dimensions == std::array<float, 3>{},
                    "Geometry/compound has stray primitive dimensions");
        switch (node.kind) {
        case CollisionKind::Box:
            for (float v : node.dimensions)
                require(v >= .001f, "Box collision dimension below one millimeter");
            break;
        case CollisionKind::Sphere:
            require(node.dimensions[0] >= .001f && node.dimensions[1] == 0 &&
                        node.dimensions[2] == 0,
                    "Invalid sphere collision dimensions");
            break;
        case CollisionKind::Capsule:
        case CollisionKind::Cylinder:
            require(node.dimensions[0] >= .001f && node.dimensions[2] == 0 &&
                        (node.kind == CollisionKind::Capsule || node.dimensions[1] >= .001f),
                    "Invalid capsule/cylinder collision dimensions");
            break;
        case CollisionKind::ConvexHull:
            require((!require_geometry || node.vertices.size() >= 4) &&
                        node.vertices.size() <= limits.hull_points,
                    "Invalid convex hull point count");
            break;
        case CollisionKind::TriangleMesh:
            require((!require_geometry || (node.vertices.size() >= 3 && !node.indices.empty())) &&
                        node.indices.size() % 3 == 0,
                    "Triangle collision requires indexed triangles");
            add(triangles, node.indices.size() / 3, limits.triangles);
            for (auto i : node.indices)
                require(i < node.vertices.size(), "Collision triangle index out of bounds");
            for (std::size_t i = 0; i < node.indices.size(); i += 3) {
                const auto& a = node.vertices[node.indices[i]];
                const auto normal = cross(subtract(node.vertices[node.indices[i + 1]], a),
                                          subtract(node.vertices[node.indices[i + 2]], a));
                require(dot(normal, normal) > 0, "Degenerate collision triangle");
            }
            break;
        case CollisionKind::Compound:
            require(!node.children.empty(), "Empty compound collision");
            break;
        default:
            throw std::runtime_error("Unknown collision shape family");
        }
    }
    std::vector<bool> visited(data.nodes.size());
    std::function<void(std::size_t, std::size_t)> visit = [&](std::size_t i, std::size_t depth) {
        require(i < data.nodes.size() && depth <= limits.depth, "Collision child/depth invalid");
        require(!visited[i], "Collision tree has a cycle or shared child");
        visited[i] = true;
        for (auto child : data.nodes[i].children)
            visit(child, depth + 1);
    };
    visit(data.root, 1);
    require(std::all_of(visited.begin(), visited.end(), [](bool v) { return v; }),
            "Collision tree has unreachable members");
}
void validate_collision(const CollisionData& data, CollisionLimits limits) {
    collision_detail::validate(data, limits, true);
}
bool collision_contains_triangle_mesh(const CollisionData& data) {
    return std::any_of(data.nodes.begin(), data.nodes.end(),
                       [](const auto& n) { return n.kind == CollisionKind::TriangleMesh; });
}
std::vector<std::byte> encode_collision(const CollisionData& data, CollisionLimits limits) {
    validate_collision(data, limits);
    std::vector<std::uint32_t> order(data.nodes.size()), remap(data.nodes.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(),
              [&](auto a, auto b) { return data.nodes[a].id < data.nodes[b].id; });
    for (std::uint32_t i = 0; i < order.size(); ++i)
        remap[order[i]] = i;
    nlohmann::json meta{{"root", remap[data.root]}, {"nodes", nlohmann::json::array()}};
    std::vector<std::byte> payload;
    for (auto index : order) {
        const auto& n = data.nodes[index];
        std::vector<std::uint32_t> children;
        for (auto child : n.children)
            children.push_back(remap[child]);
        std::sort(children.begin(), children.end());
        meta["nodes"].push_back({{"id", n.id},
                                 {"kind", unsigned(n.kind)},
                                 {"translation", n.translation},
                                 {"rotation", n.rotation},
                                 {"scale", n.scale},
                                 {"dimensions", n.dimensions},
                                 {"vertices", n.vertices.size()},
                                 {"indices", n.indices.size()},
                                 {"children", children}});
        for (const auto& v : n.vertices)
            for (float x : v)
                asset_detail::cooked_write32(payload, std::bit_cast<std::uint32_t>(x));
        for (auto i : n.indices)
            asset_detail::cooked_write32(payload, i);
    }
    auto out = asset_detail::encode_envelope(meta, payload, magic, metadata_limit);
    require(out.size() <= limits.bytes, "Collision envelope exceeds file budget");
    return out;
}
CollisionData decode_collision(std::span<const std::byte> bytes, CollisionLimits limits) {
    require(bytes.size() <= limits.bytes, "Collision file exceeds byte budget");
    auto envelope = asset_detail::decode_envelope(bytes, magic, metadata_limit, limits.bytes);
    const auto& nodes = envelope.metadata.at("nodes");
    require(nodes.is_array() && !nodes.empty() && nodes.size() <= limits.nodes,
            "Invalid collision node metadata");
    CollisionData data;
    data.root = static_cast<std::uint32_t>(count(envelope.metadata.at("root"), nodes.size() - 1));
    std::size_t at = 0, total_vertices = 0, total_indices = 0, edges = 0;
    for (const auto& j : nodes) {
        CollisionNode n;
        n.id = j.at("id").get<CollisionMemberId>();
        n.kind = static_cast<CollisionKind>(count(j.at("kind"), unsigned(CollisionKind::Compound)));
        n.translation = floats<3>(j.at("translation"));
        n.rotation = floats<4>(j.at("rotation"));
        n.scale = floats<3>(j.at("scale"));
        n.dimensions = floats<3>(j.at("dimensions"));
        const auto nv = count(j.at("vertices"), limits.vertices);
        const auto ni = count(j.at("indices"), limits.bytes / 4);
        add(total_vertices, nv, limits.vertices);
        add(total_indices, ni, limits.bytes / 4);
        const auto& children = j.at("children");
        require(children.is_array(), "Collision children must be an array");
        add(edges, children.size(), nodes.size() - 1);
        for (const auto& child : children)
            n.children.push_back(static_cast<std::uint32_t>(count(child, nodes.size() - 1)));
        // Check remaining bytes before allocating from untrusted counts.
        require(nv <= (envelope.payload.size() - at) / 12, "Truncated collision vertex payload");
        n.vertices.resize(nv);
        for (auto& v : n.vertices)
            for (auto& x : v)
                x = std::bit_cast<float>(asset_detail::cooked_read32(envelope.payload, at));
        require(ni <= (envelope.payload.size() - at) / 4, "Truncated collision index payload");
        n.indices.resize(ni);
        for (auto& i : n.indices)
            i = asset_detail::cooked_read32(envelope.payload, at);
        data.nodes.push_back(std::move(n));
    }
    require(at == envelope.payload.size(), "Trailing collision payload");
    validate_collision(data, limits);
    return data;
}
} // namespace forge
