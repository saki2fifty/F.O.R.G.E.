#include "bounded_json.hpp"
#include "collision_validation.hpp"
#include <algorithm>
#include <cmath>
#include <forge/collision_source.hpp>
#include <limits>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t source_limit = 1024 * 1024;
constexpr std::array<std::string_view, 7> names{
    "box", "sphere", "capsule", "cylinder", "convex_hull", "triangle_mesh", "compound"};
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
std::uint32_t integer(const Json& j, std::uint32_t maximum) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Collision source requires a nonnegative integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= maximum, "Collision source integer exceeds limit");
    return static_cast<std::uint32_t>(n);
}
template <std::size_t N> std::array<float, N> floats(const Json& j) {
    require(j.is_array() && j.size() == N, "Invalid collision source vector");
    std::array<float, N> out;
    for (std::size_t i = 0; i < N; ++i) {
        require(j[i].is_number(), "Collision source vector must be numeric");
        const auto n = j[i].get<double>();
        require(std::isfinite(n) && std::abs(n) <= std::numeric_limits<float>::max(),
                "Collision source has invalid float");
        out[i] = static_cast<float>(n);
        require(n == 0 || out[i] != 0, "Collision source float underflow");
    }
    return out;
}
CollisionKind kind(const Json& value) {
    const auto name = value.get<std::string>();
    const auto at = std::find(names.begin(), names.end(), name);
    require(at != names.end(), "Unknown collision source shape family");
    return static_cast<CollisionKind>(at - names.begin());
}
struct Recipe {
    CollisionData data;
    std::map<std::size_t, std::pair<AssetRef<MeshAsset>, CollisionMeshSelection>> sources;
};
Recipe recipe(const CollisionSource& source) {
    const auto& doc = source.document;
    require(doc.is_object() && doc.at("kind") == "forge.collision" &&
                doc.at("version").is_number_integer() && doc.at("version") == 1 &&
                bool(source.asset()),
            "Unsupported collision source identity/kind/version");
    const auto& nodes = doc.at("nodes");
    require(nodes.is_array() && !nodes.empty() && nodes.size() <= CollisionLimits{}.nodes,
            "Invalid collision source node count");
    Recipe out;
    std::map<CollisionMemberId, std::uint32_t> indices;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        require(
            indices
                .emplace(nodes[i].at("id").get<CollisionMemberId>(), static_cast<std::uint32_t>(i))
                .second,
            "Repeated collision source member identity");
    }
    const auto root = doc.at("root").get<CollisionMemberId>();
    require(indices.contains(root), "Collision source root is missing");
    out.data.root = indices.at(root);
    std::size_t edges = 0;
    for (const auto& j : nodes) {
        CollisionNode n;
        n.id = j.at("id").get<CollisionMemberId>();
        n.kind = kind(j.at("kind"));
        n.translation = floats<3>(j.at("translation"));
        n.rotation = floats<4>(j.at("rotation"));
        n.scale = floats<3>(j.at("scale"));
        n.dimensions = floats<3>(j.at("dimensions"));
        const auto& children = j.at("children");
        require(children.is_array() && children.size() <= nodes.size() - 1 - edges,
                "Collision source has too many child edges");
        edges += children.size();
        for (const auto& child : children) {
            const auto id = child.get<CollisionMemberId>();
            require(indices.contains(id), "Collision source child is missing");
            n.children.push_back(indices.at(id));
        }
        const bool generated =
            n.kind == CollisionKind::ConvexHull || n.kind == CollisionKind::TriangleMesh;
        require(generated == j.contains("source"),
                "Collision geometry needs an explicit Mesh source; primitives do not");
        if (generated) {
            const auto& input = j.at("source");
            const auto mesh = input.at("mesh").get<AssetRef<MeshAsset>>();
            require(bool(mesh.id), "Collision Mesh source identity is empty");
            CollisionMeshSelection selection;
            selection.kind = n.kind;
            selection.lod = integer(input.at("lod"), 15);
            const auto& parts = input.at("parts");
            selection.all_parts = parts == "all";
            if (!selection.all_parts) {
                require(parts.is_array() && !parts.empty() && parts.size() <= 65536,
                        "Collision source requires all parts or a bounded explicit selection");
                const auto revision = input.at("revision").get<std::string>();
                require(revision.size() == 64 && std::all_of(revision.begin(), revision.end(),
                                                             [](char c) {
                                                                 return (c >= '0' && c <= '9') ||
                                                                        (c >= 'a' && c <= 'f');
                                                             }),
                        "Collision part selection must identify its reviewed Mesh revision");
                std::set<std::uint32_t> unique;
                for (const auto& p : parts) {
                    const auto value = integer(p, 65535);
                    require(unique.insert(value).second, "Repeated collision source part");
                    selection.parts.push_back(value);
                }
            }
            const auto policy = input.at("degenerate").get<std::string>();
            require(policy == "reject" || policy == "remove", "Unknown degenerate triangle policy");
            selection.degenerate = policy == "reject" ? CollisionDegeneratePolicy::Reject
                                                      : CollisionDegeneratePolicy::Remove;
            out.sources.emplace(out.data.nodes.size(), std::make_pair(mesh, std::move(selection)));
        }
        out.data.nodes.push_back(std::move(n));
    }
    collision_detail::validate(out.data, {}, false);
    return out;
}
} // namespace
AssetId CollisionSource::asset() const { return document.at("asset_id").get<AssetId>(); }
CollisionSource CollisionSource::create(AssetId asset, CollisionKind type) {
    require(unsigned(type) <= unsigned(CollisionKind::Cylinder),
            "Create an initial primitive; generated/compound recipes need explicit inputs");
    const auto member = CollisionMemberId::generate();
    std::array<float, 3> dimensions{1, 1, 1};
    if (type == CollisionKind::Sphere)
        dimensions = {.5f, 0, 0};
    if (type == CollisionKind::Capsule || type == CollisionKind::Cylinder)
        dimensions = {.5f, 1, 0};
    CollisionSource result{{{"kind", "forge.collision"},
                            {"version", 1},
                            {"asset_id", asset},
                            {"root", member},
                            {"nodes", Json::array({{{"id", member},
                                                    {"kind", names[unsigned(type)]},
                                                    {"translation", {0, 0, 0}},
                                                    {"rotation", {0, 0, 0, 1}},
                                                    {"scale", {1, 1, 1}},
                                                    {"dimensions", dimensions},
                                                    {"children", Json::array()}}})}}};
    result.validate();
    return result;
}
CollisionSource CollisionSource::from_mesh(AssetId asset, CollisionKind type,
                                           AssetRef<MeshAsset> mesh) {
    require(type == CollisionKind::ConvexHull || type == CollisionKind::TriangleMesh,
            "Mesh collision requires convex hull or triangle mesh geometry");
    require(bool(mesh.id), "Mesh collision requires a source Mesh identity");
    auto result = create(asset, CollisionKind::Box);
    auto& node = result.document["nodes"][0];
    node["kind"] = names[unsigned(type)];
    node["dimensions"] = {0, 0, 0};
    node["source"] = {{"mesh", mesh.id}, {"lod", 0}, {"parts", "all"}, {"degenerate", "reject"}};
    result.validate();
    return result;
}
CollisionSource CollisionSource::parse(std::span<const std::byte> bytes) {
    CollisionSource source{asset_detail::parse_bounded_json(bytes, source_limit, 262144, 32)};
    source.validate();
    return source;
}
void CollisionSource::validate() const {
    // Reject nonfinite numbers even in unknown extension fields; retain their
    // structure verbatim rather than searching/remapping arbitrary UUID text.
    std::size_t events = 0;
    const auto walk = [&](auto&& self, const Json& j, unsigned depth) -> void {
        require(depth <= 32 && ++events <= 262144, "Collision source exceeds structural budget");
        if (j.is_number_float())
            require(std::isfinite(j.get<double>()), "Nonfinite collision source number");
        if (j.is_structured())
            for (const auto& value : j)
                self(self, value, depth + 1);
    };
    walk(walk, document, 0);
    require(document.dump().size() <= source_limit, "Collision source exceeds byte budget");
    (void)recipe(*this);
}
std::vector<AssetRef<MeshAsset>> CollisionSource::mesh_sources() const {
    validate();
    std::set<AssetRef<MeshAsset>> ids;
    for (const auto& [index, input] : recipe(*this).sources) {
        (void)index;
        ids.insert(input.first);
    }
    return {ids.begin(), ids.end()};
}
ResolvedCollisionSource
resolve_collision_source(const CollisionSource& source,
                         const std::function<MeshData(AssetRef<MeshAsset>)>& resolve_mesh,
                         CollisionLimits limits) {
    source.validate();
    auto resolved = recipe(source);
    ResolvedCollisionSource out;
    auto remaining = limits;
    for (const auto& [index, input] : resolved.sources) {
        require(bool(resolve_mesh), "Collision generation has no Mesh resolver");
        auto& node = resolved.data.nodes[index];
        auto generated =
            generate_collision(resolve_mesh(input.first), input.second, node.id, remaining);
        node.vertices = std::move(generated.data.nodes[0].vertices);
        node.indices = std::move(generated.data.nodes[0].indices);
        remaining.vertices -= node.vertices.size();
        remaining.triangles -= node.indices.size() / 3;
        remaining.bytes -= node.vertices.size() * 12 + node.indices.size() * 4;
        if (generated.removed_triangles)
            out.removed_triangles.emplace(node.id, generated.removed_triangles);
    }
    validate_collision(resolved.data, limits);
    out.data = std::move(resolved.data);
    return out;
}
} // namespace forge
