#include <algorithm>
#include <forge/collision_generation.hpp>
#include <numeric>
#include <set>

namespace forge {
namespace {
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
bool degenerate(const std::array<float, 3>& a, const std::array<float, 3>& b,
                const std::array<float, 3>& c) {
    std::array<double, 3> ab{}, ac{};
    for (unsigned i = 0; i < 3; ++i) {
        ab[i] = double(b[i]) - a[i];
        ac[i] = double(c[i]) - a[i];
    }
    return ab[1] * ac[2] == ab[2] * ac[1] && ab[2] * ac[0] == ab[0] * ac[2] &&
           ab[0] * ac[1] == ab[1] * ac[0];
}
} // namespace
CollisionGeneration generate_collision(const MeshData& mesh,
                                       const CollisionMeshSelection& selection,
                                       CollisionMemberId member, CollisionLimits limits) {
    validate_mesh(mesh);
    require(bool(member), "Collision generation requires stable member identity");
    require(selection.kind == CollisionKind::ConvexHull ||
                selection.kind == CollisionKind::TriangleMesh,
            "Mesh generation supports convex hull or triangle collision");
    require(selection.degenerate == CollisionDegeneratePolicy::Reject ||
                selection.degenerate == CollisionDegeneratePolicy::Remove,
            "Unsupported degenerate triangle policy");
    require(selection.lod < mesh.lods.size() && (selection.all_parts || !selection.parts.empty()),
            "Select an existing mesh LOD and at least one part");
    const auto& lod = mesh.lods[selection.lod];
    require(!selection.all_parts || selection.parts.empty(),
            "Whole Mesh selection cannot also specify part indices");
    require(selection.parts.size() <= lod.parts.size(), "Too many collision source parts");
    auto parts = selection.parts;
    if (selection.all_parts) {
        parts.resize(lod.parts.size());
        std::iota(parts.begin(), parts.end(), 0u);
    }
    std::sort(parts.begin(), parts.end());
    require(std::adjacent_find(parts.begin(), parts.end()) == parts.end(),
            "Repeated collision source part");
    CollisionGeneration out;
    CollisionNode node;
    node.id = member;
    node.kind = selection.kind;
    node.dimensions = {};
    std::size_t source_triangles = 0;
    for (auto index : parts) {
        require(index < lod.parts.size(), "Collision source part no longer exists");
        const auto& part = lod.parts[index];
        require(part.topology == MeshTopology::Triangles,
                "Collision generation requires triangle topology");
        const auto available_indices = part.indices.empty() ? part.vertices : part.indices.size();
        require(available_indices <= limits.bytes / 4 &&
                    available_indices / 3 <= limits.triangles &&
                    source_triangles <= limits.triangles - available_indices / 3,
                "Collision source exceeds triangle budget");
        source_triangles += available_indices / 3;
        const auto* position = part.find("POSITION");
        const auto& values = std::get<std::vector<float>>(position->values);
        auto used = part.indices;
        if (used.empty()) {
            used.resize(part.vertices);
            std::iota(used.begin(), used.end(), 0u);
        }
        std::sort(used.begin(), used.end());
        used.erase(std::unique(used.begin(), used.end()), used.end());
        const auto vertex_count = used.size();
        const auto base = node.vertices.size();
        require(base <= limits.vertices && vertex_count <= limits.vertices - base &&
                    base <= UINT32_MAX && vertex_count <= UINT32_MAX - base,
                "Generated collision exceeds vertex budget");
        if (selection.kind == CollisionKind::ConvexHull)
            require(base <= limits.hull_points && vertex_count <= limits.hull_points - base,
                    "Generated convex collision exceeds point budget");
        require(base + vertex_count <= limits.bytes / 12,
                "Generated collision exceeds byte budget");
        for (std::size_t i : used)
            node.vertices.push_back({values[i * 3], values[i * 3 + 1], values[i * 3 + 2]});
        if (selection.kind == CollisionKind::ConvexHull)
            continue;
        for (std::size_t i = 0; i < available_indices; i += 3) {
            std::array<std::uint32_t, 3> tri;
            for (unsigned k = 0; k < 3; ++k) {
                const auto source_index =
                    part.indices.empty() ? static_cast<std::uint32_t>(i + k) : part.indices[i + k];
                const auto at =
                    std::lower_bound(used.begin(), used.end(), source_index) - used.begin();
                tri[k] = static_cast<std::uint32_t>(base + at);
            }
            if (degenerate(node.vertices[tri[0]], node.vertices[tri[1]], node.vertices[tri[2]])) {
                require(
                    selection.degenerate == CollisionDegeneratePolicy::Remove,
                    "Source contains a degenerate collision triangle; choose Remove explicitly");
                ++out.removed_triangles;
                continue;
            }
            require(node.indices.size() <= limits.bytes / 4 &&
                        3 <= limits.bytes / 4 - node.indices.size(),
                    "Generated collision exceeds index byte budget");
            node.indices.insert(node.indices.end(), tri.begin(), tri.end());
        }
    }
    out.data.nodes.push_back(std::move(node));
    validate_collision(out.data, limits);
    return out;
}
} // namespace forge
