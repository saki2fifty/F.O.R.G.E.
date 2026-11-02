#include "navigation_mesh.hpp"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourObstacleAvoidance.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
namespace forge {
const char* nav_status_name(NavStatus status) {
    static const char* names[]{"success",
                               "partial path",
                               "missing navmesh",
                               "stale navmesh",
                               "start outside navmesh",
                               "end outside navmesh",
                               "no path",
                               "navigation limit exceeded",
                               "invalid navigation data",
                               "navigation unavailable"};
    auto i = unsigned(status);
    return i < std::size(names) ? names[i] : "invalid navigation status";
}
namespace navigation_detail {
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(dtPolyRef) == 4 && sizeof(dtMeshHeader) == 100 && sizeof(dtPoly) == 32 &&
              sizeof(dtLink) == 12 && sizeof(dtPolyDetail) == 12 &&
              sizeof(dtOffMeshConnection) == 36);
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
bool near(Double3 left, Double3 right) {
    for (unsigned axis = 0; axis < 3; ++axis)
        if (std::abs(left[axis] - right[axis]) > .00001)
            return false;
    return true;
}
std::array<dtPolyRef, 2> ground_endpoints(const dtNavMesh& mesh, const dtMeshTile& tile,
                                          const dtOffMeshConnection& connection) {
    require(tile.header && connection.poly < tile.header->polyCount,
            "Invalid native navigation link polygon");
    std::array<dtPolyRef, 2> endpoints{};
    auto index = tile.polys[connection.poly].firstLink;
    unsigned visits = 0;
    while (index != DT_NULL_LINK) {
        require(index < unsigned(tile.header->maxLinkCount) &&
                    ++visits <= unsigned(tile.header->maxLinkCount),
                "Native navigation link adjacency exceeds bounds");
        const auto& edge = tile.links[index];
        const dtMeshTile* neighbour_tile = nullptr;
        const dtPoly* neighbour = nullptr;
        if (edge.edge < 2 && edge.ref &&
            dtStatusSucceed(mesh.getTileAndPolyByRef(edge.ref, &neighbour_tile, &neighbour)) &&
            neighbour->getType() == DT_POLYTYPE_GROUND)
            endpoints[edge.edge] = edge.ref;
        index = edge.next;
    }
    return endpoints;
}
class Reader {
    std::span<const std::byte> bytes_;
    std::size_t at_ = 0;

  public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {
        require(bytes.size() <= max_nav_bytes, "Navmesh exceeds 4 MiB");
    }
    template <class T> std::vector<T> array(std::size_t count) {
        require(count <= (bytes_.size() - at_) / sizeof(T), "Truncated navmesh array");
        std::vector<T> out(count);
        if (count)
            std::memcpy(out.data(), bytes_.data() + at_, count * sizeof(T));
        at_ += count * sizeof(T);
        return out;
    }
    void end() { require(at_ == bytes_.size(), "Unexpected trailing navmesh data"); }
};
bool finite(float v) { return std::isfinite(v) && std::abs(v) <= 4096; }
bool point(Double3 v) {
    return std::all_of(v.begin(), v.end(),
                       [](double x) { return std::isfinite(x) && std::abs(x) <= 4096; });
}
NavResult result(NavStatus s) { return {s, {}, nav_status_name(s)}; }
} // namespace
void validate_tile(std::span<const std::byte> bytes, MeshSpace space,
                   std::span<const NavigationLinkData> connections) {
    validate_navigation_links(connections, space);
    require(space == MeshSpace::Spatial3D || space == MeshSpace::PlanarXY,
            "Unsupported navigation coordinate space");
    Reader r(bytes);
    auto h = r.array<dtMeshHeader>(1)[0];
    require(h.magic == DT_NAVMESH_MAGIC && h.version == DT_NAVMESH_VERSION,
            "Unsupported Detour tile version");
    require(h.x == 0 && h.y == 0 && h.layer == 0 && h.userId == 0,
            "Only world-anchored single-tile navmeshes supported");
    require(h.polyCount > 0 && h.polyCount <= int(max_polygons) && h.vertCount >= 3 &&
                h.vertCount <= 16384,
            "Navmesh polygon/vertex limit");
    require(h.maxLinkCount >= 3 && h.maxLinkCount <= h.polyCount * 6 &&
                h.offMeshConCount == int(connections.size()) && h.offMeshBase > 0 &&
                h.offMeshBase == h.polyCount - h.offMeshConCount &&
                h.detailMeshCount == h.offMeshBase && h.vertCount >= 3 + h.offMeshConCount * 2,
            "Invalid navmesh link/detail count");
    require(h.detailVertCount >= 0 && h.detailVertCount <= 65536 &&
                h.detailTriCount >= h.offMeshBase && h.detailTriCount <= 65536,
            "Navmesh detail limit");
    require(h.bvNodeCount == 0, "Unsupported navmesh BV tree");
    require(std::isfinite(h.walkableHeight) && h.walkableHeight >= .2f && h.walkableHeight <= 10 &&
                std::isfinite(h.walkableRadius) && h.walkableRadius >= .05f &&
                h.walkableRadius <= 5 && std::isfinite(h.walkableClimb) && h.walkableClimb >= 0 &&
                h.walkableClimb <= 2 && std::isfinite(h.bvQuantFactor) && h.bvQuantFactor >= 1 &&
                h.bvQuantFactor <= 20,
            "Invalid navmesh agent/numeric settings");
    for (unsigned i = 0; i < 3; ++i)
        require(finite(h.bmin[i]) && finite(h.bmax[i]) && h.bmax[i] > h.bmin[i],
                "Invalid navmesh bounds");
    auto verts = r.array<float>(std::size_t(h.vertCount) * 3);
    auto polys = r.array<dtPoly>(h.polyCount);
    auto links = r.array<std::byte>(std::size_t(h.maxLinkCount) * sizeof(dtLink));
    require(std::all_of(links.begin(), links.end(), [](auto b) { return b == std::byte{}; }),
            "Navmesh file contains runtime link state");
    auto details = r.array<dtPolyDetail>(h.detailMeshCount);
    auto detailVerts = r.array<float>(std::size_t(h.detailVertCount) * 3);
    auto tris = r.array<unsigned char>(std::size_t(h.detailTriCount) * 4);
    auto off_mesh = r.array<dtOffMeshConnection>(std::size_t(h.offMeshConCount));
    r.end();
    auto checkVerts = [&](const auto& data) {
        for (std::size_t i = 0; i < data.size(); ++i)
            require(finite(data[i]) && data[i] >= h.bmin[i % 3] - .001f &&
                        data[i] <= h.bmax[i % 3] + .001f,
                    "Invalid navmesh vertex");
    };
    checkVerts(verts);
    checkVerts(detailVerts);
    if (space == MeshSpace::PlanarXY) {
        const auto flat = [&](const auto& data) {
            for (std::size_t i = 1; i < data.size(); i += 3)
                require(std::abs(data[i] - verts[1]) <= .0001f,
                        "XY navigation requires a flat native tile");
        };
        flat(verts);
        flat(detailVerts);
    }
    unsigned edges = 0, nextVert = 0, nextTri = 0;
    const auto ground_vertices = unsigned(h.vertCount - h.offMeshConCount * 2);
    for (int i = 0; i < h.offMeshBase; ++i) {
        const auto& p = polys[i];
        require(p.vertCount >= 3 && p.vertCount <= 6 && p.firstLink == 0 && p.flags == 1 &&
                    p.areaAndtype == 0,
                "Unsupported or invalid navmesh polygon");
        edges += p.vertCount;
        std::set<unsigned> unique;
        double sign = 0, area = 0;
        for (unsigned j = 0; j < p.vertCount; ++j) {
            require(p.verts[j] < ground_vertices && unique.insert(p.verts[j]).second,
                    "Invalid/repeated polygon vertex index");
            require(p.neis[j] <= h.offMeshBase && p.neis[j] != i + 1, "Invalid polygon neighbour");
        }
        for (unsigned j = 0; j < p.vertCount; ++j) {
            const auto *a = &verts[p.verts[j] * 3], *b = &verts[p.verts[(j + 1) % p.vertCount] * 3],
                       *c = &verts[p.verts[(j + 2) % p.vertCount] * 3];
            const double turn = (double(b[0]) - a[0]) * (double(c[2]) - b[2]) -
                                (double(b[2]) - a[2]) * (double(c[0]) - b[0]);
            if (std::abs(turn) > 1e-8) {
                require(sign == 0 || sign * turn > 0, "Non-convex navmesh polygon");
                sign = turn;
            }
            area += double(a[0]) * b[2] - double(b[0]) * a[2];
            if (p.neis[j]) {
                const auto& n = polys[p.neis[j] - 1];
                require(n.vertCount >= 3 && n.vertCount <= 6, "Invalid neighbor vertex count");
                bool reciprocal = false;
                for (unsigned k = 0; k < n.vertCount; ++k)
                    if (n.verts[k] == p.verts[(j + 1) % p.vertCount] &&
                        n.verts[(k + 1) % n.vertCount] == p.verts[j] && n.neis[k] == i + 1)
                        reciprocal = true;
                require(reciprocal, "Non-reciprocal navmesh edge");
            }
        }
        require(area < -1e-7 && sign < 0, "Degenerate navmesh polygon");
        for (unsigned j = p.vertCount; j < 6; ++j)
            require(p.verts[j] == 0 && p.neis[j] == 0, "Unexpected unused polygon data");
        const auto& d = details[i];
        require(d.vertBase == nextVert && d.triBase == nextTri && d.triCount >= p.vertCount - 2 &&
                    unsigned(d.vertCount) + p.vertCount <= 255,
                "Invalid navmesh detail ranges");
        require(d.vertCount <= unsigned(h.detailVertCount) - nextVert &&
                    d.triCount <= unsigned(h.detailTriCount) - nextTri,
                "Navmesh detail range exceeds array");
        bool boundary = false;
        for (unsigned t = 0; t < d.triCount; ++t) {
            const auto* tri = &tris[(std::size_t(d.triBase) + t) * 4];
            for (unsigned k = 0; k < 3; ++k)
                require(tri[k] < unsigned(d.vertCount) + p.vertCount,
                        "Invalid detail triangle index");
            require(tri[0] != tri[1] && tri[0] != tri[2] && tri[1] != tri[2] &&
                        (tri[3] & ~0x15) == 0,
                    "Invalid detail triangle");
            boundary |= tri[3] != 0;
        }
        require(boundary, "Navmesh detail has no boundary edges");
        nextVert += d.vertCount;
        nextTri += d.triCount;
    }
    NativeNavigationLinks expected(connections, space, verts[1]);
    dtNavMeshCreateParams parameters{};
    expected.apply(parameters);
    for (std::size_t i = 0; i < connections.size(); ++i) {
        const auto& connection = off_mesh[i];
        const auto& polygon = polys[std::size_t(h.offMeshBase) + i];
        require(connection.poly == unsigned(h.offMeshBase) + i && connection.side == 0xff &&
                    connection.flags == parameters.offMeshConDir[i] &&
                    connection.userId == parameters.offMeshConUserID[i] &&
                    connection.rad == parameters.offMeshConRad[i],
                "Navigation link identity, direction, radius or tile side mismatch");
        require(polygon.vertCount == 2 && polygon.firstLink == 0 && polygon.flags == 1 &&
                    polygon.areaAndtype == (DT_POLYTYPE_OFFMESH_CONNECTION << 6) &&
                    polygon.verts[0] == ground_vertices + i * 2 &&
                    polygon.verts[1] == ground_vertices + i * 2 + 1,
                "Invalid navigation link polygon");
        for (unsigned j = 0; j < 6; ++j) {
            require(polygon.neis[j] == 0 && (j < 2 || polygon.verts[j] == 0),
                    "Navigation link contains unexpected polygon adjacency");
            require(connection.pos[j] == parameters.offMeshConVerts[i * 6 + j] &&
                        connection.pos[j] == verts[(ground_vertices + i * 2) * 3 + j],
                    "Navigation link endpoint metadata/native mismatch");
        }
    }
    require(edges + unsigned(connections.size()) * 4 == unsigned(h.maxLinkCount) &&
                nextVert == unsigned(h.detailVertCount) && nextTri == unsigned(h.detailTriCount),
            "Navmesh section counts do not agree");
}
nlohmann::json tile_properties(std::span<const std::byte> bytes, MeshSpace space,
                               std::span<const NavigationLinkData> connections) {
    validate_tile(bytes, space, connections);
    dtMeshHeader h;
    std::memcpy(&h, bytes.data(), sizeof h);
    return {{"minimum", {h.bmin[0], h.bmin[1], h.bmin[2]}},
            {"maximum", {h.bmax[0], h.bmax[1], h.bmax[2]}},
            {"height", h.walkableHeight},
            {"radius", h.walkableRadius},
            {"climb", h.walkableClimb},
            {"cell_inverse", h.bvQuantFactor}};
}
struct Mesh::Impl {
    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{dtAllocNavMesh(), dtFreeNavMesh};
    std::optional<HeightfieldLayers> layers;
    std::unique_ptr<TileCache> base_tiles;
    std::vector<NavigationLinkData> connections;
    MeshSpace space = MeshSpace::Spatial3D;
    float altitude = 0; // Derived flat native Y; never a second authored position.
    const dtNavMesh* native() const { return base_tiles ? base_tiles->mesh() : mesh.get(); }
};
Mesh::Mesh(std::span<const std::byte> bytes, MeshSpace space, bool dynamic,
           std::span<const NavigationLinkData> connections) {
    validate_navigation_links(connections, space);
    impl_ = std::make_unique<Impl>();
    impl_->space = space;
    impl_->connections.assign(connections.begin(), connections.end());
    if (dynamic) {
        impl_->layers = decode_layers(bytes);
        require(impl_->layers->settings.plane == space, "Navigation layer/query plane mismatch");
        impl_->base_tiles = std::make_unique<TileCache>(*impl_->layers, connections);
    } else {
        validate_tile(bytes, space, connections);
        require(bool(impl_->mesh), "Cannot allocate navmesh");
        auto* data = static_cast<unsigned char*>(dtAlloc(bytes.size(), DT_ALLOC_PERM));
        require(data, "Cannot allocate navmesh tile");
        std::memcpy(data, bytes.data(), bytes.size());
        const auto status = impl_->mesh->init(data, int(bytes.size()), DT_TILE_FREE_DATA);
        if (dtStatusFailed(status)) {
            dtFree(data);
            throw std::runtime_error("Detour rejected admitted navmesh");
        }
    }
    const auto* native = impl_->native();
    bool occupied = false;
    for (int i = 0; i < native->getMaxTiles(); ++i) {
        const auto* tile = native->getTile(i);
        if (tile->header && tile->header->vertCount) {
            impl_->altitude = tile->verts[1];
            occupied = true;
            break;
        }
    }
    require(occupied, "Navigation contains no walkable polygons");
    std::vector<bool> connected(connections.size());
    for (int i = 0; i < native->getMaxTiles() && !connections.empty(); ++i) {
        const auto* tile = native->getTile(i);
        if (!tile->header)
            continue;
        for (int j = 0; j < tile->header->offMeshConCount; ++j) {
            const auto& link = tile->offMeshCons[j];
            require(link.userId > 0 && link.userId <= connections.size(),
                    "Native navigation link has no admitted source identity");
            const auto endpoints = ground_endpoints(*native, *tile, link);
            connected[link.userId - 1] =
                connected[link.userId - 1] || (endpoints[0] && endpoints[1]);
        }
    }
    require(std::ranges::all_of(connected, [](bool value) { return value; }),
            "Navigation link endpoints must both attach to walkable ground");
}
Mesh::~Mesh() = default;
MeshSpace Mesh::space() const { return impl_->space; }
bool Mesh::dynamic_obstacles() const { return impl_->layers.has_value(); }
std::vector<Double3> Mesh::triangles() const {
    std::vector<Double3> out;
    const auto* native = impl_->native();
    for (int index = 0; index < native->getMaxTiles(); ++index) {
        const auto* tile = native->getTile(index);
        if (!tile->header)
            continue;
        for (int i = 0; i < tile->header->polyCount; ++i) {
            const auto& p = tile->polys[i];
            for (unsigned j = 1; j + 1 < p.vertCount; ++j)
                for (auto v : {p.verts[0], p.verts[j], p.verts[j + 1]}) {
                    auto* xyz = &tile->verts[v * 3];
                    out.push_back(world_coordinates(impl_->space, {xyz[0], xyz[1], xyz[2]}));
                }
        }
    }
    return out;
}
struct Query::Impl {
    std::shared_ptr<const Mesh> owner;
    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{dtAllocNavMeshQuery(),
                                                                         dtFreeNavMeshQuery};
    dtQueryFilter filter;
    std::unique_ptr<TileCache> tiles;
    std::uint64_t generation = 0;
    std::unique_ptr<dtObstacleAvoidanceQuery, decltype(&dtFreeObstacleAvoidanceQuery)> avoidance{
        nullptr, dtFreeObstacleAvoidanceQuery};
    explicit Impl(std::shared_ptr<const Mesh> value) : owner(std::move(value)) {
        require(owner && query, "Cannot allocate nav query");
        if (owner->impl_->layers)
            tiles = std::make_unique<TileCache>(*owner->impl_->layers, owner->impl_->connections);
        require(
            dtStatusSucceed(query->init(tiles ? tiles->mesh() : owner->impl_->mesh.get(), 2048)),
            "Cannot initialize nav query");
    }
    dtPolyRef nearest(Double3 p, float* projected) {
        p = native_coordinates(owner->space(), p);
        if (owner->space() == MeshSpace::PlanarXY)
            p[1] = owner->impl_->altitude;
        float v[3]{float(p[0]), float(p[1]), float(p[2])}, ext[3]{.5f, 2, .5f};
        dtPolyRef ref = 0;
        auto s = query->findNearestPoly(v, ext, &filter, &ref, projected);
        return dtStatusSucceed(s) ? ref : 0;
    }
};
Query::Query(std::shared_ptr<const Mesh> value) : impl_(std::make_unique<Impl>(std::move(value))) {}
Query::~Query() = default;
bool Query::obstacles(std::span<const CylinderObstacle> desired) {
    auto& s = *impl_;
    require(s.owner->dynamic_obstacles() || desired.empty(),
            "Enabled navigation obstacles require an explicit dynamic-profile rebuild");
    if (!s.owner->dynamic_obstacles())
        return false;
    try {
        bool reconstructed = false;
        if (!s.tiles) {
            s.tiles =
                std::make_unique<TileCache>(*s.owner->impl_->layers, s.owner->impl_->connections);
            require(dtStatusSucceed(s.query->init(s.tiles->mesh(), 2048)),
                    "Cannot reconstruct navigation query");
            reconstructed = true;
        }
        const bool changed = s.tiles->update(desired) || reconstructed;
        if (changed)
            ++s.generation;
        return changed;
    } catch (...) {
        s.tiles.reset(); // A partial native derivative is never queryable.
        throw;
    }
}
std::uint64_t Query::generation() const { return impl_->generation; }
std::uint64_t Query::tile_rebuilds() const {
    return impl_->tiles ? impl_->tiles->rebuild_count() : 0;
}
NavResult Query::project(Double3 p) {
    if (impl_->owner->dynamic_obstacles() && !impl_->tiles)
        return result(NavStatus::Unavailable);
    if (!point(p))
        return result(NavStatus::Invalid);
    float pos[3];
    if (!impl_->nearest(p, pos))
        return result(NavStatus::StartOutside);
    return {NavStatus::Success,
            {world_coordinates(impl_->owner->space(), {pos[0], pos[1], pos[2]}, p[2])},
            {}};
}
NavResult Query::path(Double3 a, Double3 b) { return route(a, b).path; }
Query::Route Query::route(Double3 a, Double3 b) {
    const auto failure = [](NavStatus status) { return Route{result(status), {}}; };
    if (impl_->owner->dynamic_obstacles() && !impl_->tiles)
        return failure(NavStatus::Unavailable);
    if (!point(a) || !point(b))
        return failure(NavStatus::Invalid);
    float start[3], end[3];
    auto ra = impl_->nearest(a, start), rb = impl_->nearest(b, end);
    if (!ra)
        return failure(NavStatus::StartOutside);
    if (!rb)
        return failure(NavStatus::EndOutside);
    dtPolyRef polys[128]{};
    int count = 0;
    auto status = impl_->query->findPath(ra, rb, start, end, &impl_->filter, polys, &count, 128);
    if (status & (DT_OUT_OF_NODES | DT_BUFFER_TOO_SMALL))
        return failure(NavStatus::Limit);
    if (dtStatusFailed(status) || count == 0)
        return failure(NavStatus::NoPath);
    if ((status & DT_PARTIAL_RESULT) || polys[count - 1] != rb)
        return failure(NavStatus::Partial);
    float points[max_waypoints * 3]{};
    unsigned char flags[max_waypoints]{};
    dtPolyRef references[max_waypoints]{};
    int n = 0;
    status = impl_->query->findStraightPath(start, end, polys, count, points, flags, references, &n,
                                            max_waypoints);
    if (status & (DT_OUT_OF_NODES | DT_BUFFER_TOO_SMALL))
        return failure(NavStatus::Limit);
    if (dtStatusFailed(status))
        return failure(NavStatus::NoPath);
    Route out;
    out.path.status = NavStatus::Success;
    const auto append = [&](Double3 value) {
        if (out.path.points.size() >= max_waypoints)
            return false;
        out.path.points.push_back(value);
        return true;
    };
    for (int i = 0; i < n; ++i) {
        Double3 native_point{points[i * 3], points[i * 3 + 1], points[i * 3 + 2]};
        if (!point(native_point))
            return failure(NavStatus::Invalid);
        const auto value = world_coordinates(impl_->owner->space(), native_point, a[2]);
        const bool existing_exit = !out.links.empty() &&
                                   out.links.back().exit + 1 == out.path.points.size() &&
                                   near(value, out.path.points.back());
        if (!existing_exit && !append(value))
            return failure(NavStatus::Limit);
        const auto* native_mesh = impl_->query->getAttachedNavMesh();
        dtPolyRef link_reference =
            flags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION ? references[i] : 0;
        // A route beginning exactly at an entry can retain the native START flag
        // without an off-mesh corner. The same admitted corridor still owns its
        // first connection; recover that boundary rather than losing traversal.
        if (i == 0 && !link_reference) {
            for (int corridor = 1; corridor < count; ++corridor) {
                if (!native_mesh->getOffMeshConnectionByRef(polys[corridor]))
                    continue;
                float entry[3]{}, exit[3]{};
                if (dtStatusFailed(native_mesh->getOffMeshConnectionPolyEndPoints(
                        polys[corridor - 1], polys[corridor], entry, exit)))
                    return failure(NavStatus::Invalid);
                if (near(native_point, {entry[0], entry[1], entry[2]}))
                    link_reference = polys[corridor];
                break;
            }
        }
        if (!link_reference)
            continue;
        const auto* connection = native_mesh->getOffMeshConnectionByRef(link_reference);
        if (!connection || !connection->userId ||
            connection->userId > impl_->owner->impl_->connections.size())
            return failure(NavStatus::Invalid);
        const auto at = std::find(polys, polys + count, link_reference);
        if (at == polys || at == polys + count)
            return failure(NavStatus::Invalid);
        float entry[3]{}, exit[3]{};
        if (dtStatusFailed(
                native_mesh->getOffMeshConnectionPolyEndPoints(at[-1], *at, entry, exit)))
            return failure(NavStatus::Invalid);
        const Double3 native_entry{entry[0], entry[1], entry[2]},
            native_exit{exit[0], exit[1], exit[2]};
        if (!point(native_entry) || !point(native_exit) || !near(native_point, native_entry))
            return failure(NavStatus::Invalid);
        const auto begin = out.path.points.size() - 1;
        if (!append(world_coordinates(impl_->owner->space(), native_exit, a[2])))
            return failure(NavStatus::Limit);
        out.links.push_back({impl_->owner->impl_->connections[connection->userId - 1].entity, begin,
                             out.path.points.size() - 1});
    }
    return out;
}
bool Query::link_available(EntityId source, Double3 entry, Double3 exit) const {
    if (!point(entry) || !point(exit) || (impl_->owner->dynamic_obstacles() && !impl_->tiles))
        return false;
    const auto& admitted = impl_->owner->impl_->connections;
    const auto selected = std::find_if(admitted.begin(), admitted.end(),
                                       [&](const auto& link) { return link.entity == source; });
    if (selected == admitted.end())
        return false;
    const auto ordinal = unsigned(selected - admitted.begin()) + 1;
    const auto* native = impl_->query->getAttachedNavMesh();
    const auto space = impl_->owner->space();
    for (int i = 0; i < native->getMaxTiles(); ++i) {
        const auto* tile = native->getTile(i);
        if (!tile->header)
            continue;
        for (int j = 0; j < tile->header->offMeshConCount; ++j) {
            const auto& link = tile->offMeshCons[j];
            if (link.userId != ordinal)
                continue;
            const auto endpoints = ground_endpoints(*native, *tile, link);
            if (!endpoints[0] || !endpoints[1])
                continue;
            float from[3]{}, to[3]{};
            const auto ref = native->getPolyRefBase(tile) | dtPolyRef(link.poly);
            if (dtStatusFailed(
                    native->getOffMeshConnectionPolyEndPoints(endpoints[0], ref, from, to)))
                continue;
            const auto a = world_coordinates(space, {from[0], from[1], from[2]}, entry[2]);
            const auto b = world_coordinates(space, {to[0], to[1], to[2]}, entry[2]);
            if (near(entry, a) && near(exit, b))
                return true;
            if (selected->bidirectional && (link.flags & DT_OFFMESH_CON_BIDIR) && near(entry, b) &&
                near(exit, a))
                return true;
        }
    }
    return false;
}
NavResult Query::move(Double3 a, Double3 b) {
    if (impl_->owner->dynamic_obstacles() && !impl_->tiles)
        return result(NavStatus::Unavailable);
    if (!point(a) || !point(b))
        return result(NavStatus::Invalid);
    auto native_target = native_coordinates(impl_->owner->space(), b);
    if (impl_->owner->space() == MeshSpace::PlanarXY)
        native_target[1] = impl_->owner->impl_->altitude;
    float start[3],
        target[3]{float(native_target[0]), float(native_target[1]), float(native_target[2])},
        end[3]{};
    auto ref = impl_->nearest(a, start);
    if (!ref)
        return result(NavStatus::StartOutside);
    dtPolyRef visited[32]{};
    int n = 0;
    auto s =
        impl_->query->moveAlongSurface(ref, start, target, &impl_->filter, end, visited, &n, 32);
    if (s & DT_BUFFER_TOO_SMALL)
        return result(NavStatus::Limit);
    if (dtStatusFailed(s) || !n)
        return result(NavStatus::NoPath);
    float on_surface[3]{};
    if (dtStatusFailed(impl_->query->closestPointOnPoly(visited[n - 1], end, on_surface, nullptr)))
        return result(NavStatus::Invalid);
    if (std::hypot(double(on_surface[0]) - end[0], double(on_surface[2]) - end[2]) > .005)
        return result(NavStatus::Invalid);
    Double3 p{on_surface[0], on_surface[1], on_surface[2]};
    if (!point(p))
        return result(NavStatus::Invalid);
    return {NavStatus::Success, {world_coordinates(impl_->owner->space(), p, a[2])}, {}};
}
NavResult Query::avoid_velocity(Double3 position, Double3 desired, Double3 previous, float speed,
                                float radius, float horizon,
                                std::span<const Neighbour> neighbours) {
    if (impl_->owner->dynamic_obstacles() && !impl_->tiles)
        return result(NavStatus::Unavailable);
    if (!point(position) || !std::isfinite(speed) || speed < 0 || speed > 20 ||
        !std::isfinite(radius) || radius < .05f || radius > 5 || !std::isfinite(horizon) ||
        horizon < .25f || horizon > 5)
        return result(NavStatus::Invalid);
    if (neighbours.size() > 63)
        return result(NavStatus::Limit);
    auto velocity_valid = [](Double3 v) {
        return std::ranges::all_of(
            v, [](double x) { return std::isfinite(x) && std::abs(x) <= 20.01; });
    };
    if (!velocity_valid(desired) || !velocity_valid(previous))
        return result(NavStatus::Invalid);
    for (const auto& neighbour : neighbours)
        if (!point(neighbour.position) || !velocity_valid(neighbour.velocity) ||
            !std::isfinite(neighbour.radius) || neighbour.radius < .05f || neighbour.radius > 5)
            return result(NavStatus::Invalid);
    if (speed == 0)
        return {NavStatus::Success, {Double3{}}, {}};
    float center[3];
    auto start = impl_->nearest(position, center);
    if (!start)
        return result(NavStatus::StartOutside);
    constexpr int max_nearby = 32, max_walls = max_nearby * DT_VERTS_PER_POLYGON;
    if (!impl_->avoidance) {
        decltype(impl_->avoidance) candidate{dtAllocObstacleAvoidanceQuery(),
                                             dtFreeObstacleAvoidanceQuery};
        if (!candidate || !candidate->init(63, max_walls))
            return result(NavStatus::Limit);
        impl_->avoidance = std::move(candidate);
    }
    auto& solver = *impl_->avoidance;
    solver.reset();
    for (const auto& neighbour : neighbours) {
        auto position = native_coordinates(impl_->owner->space(), neighbour.position);
        if (impl_->owner->space() == MeshSpace::PlanarXY)
            position[1] = impl_->owner->impl_->altitude;
        const auto velocity = native_coordinates(impl_->owner->space(), neighbour.velocity);
        float p[3]{float(position[0]), float(position[1]), float(position[2])};
        float v[3]{float(velocity[0]), 0, float(velocity[2])};
        solver.addCircle(p, neighbour.radius, v, v);
    }
    dtPolyRef nearby[max_nearby]{};
    int count = 0;
    auto status =
        impl_->query->findLocalNeighbourhood(start, center, speed * horizon + 2 * radius,
                                             &impl_->filter, nearby, nullptr, &count, max_nearby);
    if (status & (DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES))
        return result(NavStatus::Limit);
    if (dtStatusFailed(status))
        return result(NavStatus::Invalid);
    for (int i = 0; i < count; ++i) {
        float walls[DT_VERTS_PER_POLYGON * 6]{};
        int wall_count = 0;
        status = impl_->query->getPolyWallSegments(nearby[i], &impl_->filter, walls, nullptr,
                                                   &wall_count, DT_VERTS_PER_POLYGON);
        if (status & DT_BUFFER_TOO_SMALL)
            return result(NavStatus::Limit);
        if (dtStatusFailed(status))
            return result(NavStatus::Invalid);
        for (int wall = 0; wall < wall_count; ++wall)
            solver.addSegment(walls + wall * 6, walls + wall * 6 + 3);
    }
    const dtObstacleAvoidanceParams parameters{.45f, 3.f, .8f, 1.f, 4.f, horizon, 0, 7, 3, 3};
    const auto native_desired = native_coordinates(impl_->owner->space(), desired);
    const auto native_previous = native_coordinates(impl_->owner->space(), previous);
    float wanted[3]{float(native_desired[0]), 0, float(native_desired[2])};
    float old[3]{float(native_previous[0]), 0, float(native_previous[2])}, output[3]{};
    if (solver.sampleVelocityAdaptive(center, radius, speed, old, wanted, output, &parameters) <= 0)
        return result(NavStatus::Invalid);
    Double3 velocity{output[0], 0, output[2]};
    if (!velocity_valid(velocity))
        return result(NavStatus::Invalid);
    const auto length = std::hypot(velocity[0], velocity[2]);
    if (length > speed)
        for (auto& v : velocity)
            v *= speed / length;
    return {NavStatus::Success, {world_coordinates(impl_->owner->space(), velocity)}, {}};
}
} // namespace navigation_detail
} // namespace forge
