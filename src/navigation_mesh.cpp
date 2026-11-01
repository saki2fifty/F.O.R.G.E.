#include "navigation_mesh.hpp"
#include <DetourNavMesh.h>
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
              sizeof(dtLink) == 12 && sizeof(dtPolyDetail) == 12);
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
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
void validate_tile(std::span<const std::byte> bytes, MeshSpace space) {
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
                h.detailMeshCount == h.polyCount,
            "Invalid navmesh link/detail count");
    require(h.detailVertCount >= 0 && h.detailVertCount <= 65536 &&
                h.detailTriCount >= h.polyCount && h.detailTriCount <= 65536,
            "Navmesh detail limit");
    require(h.bvNodeCount == 0 && h.offMeshConCount == 0 && h.offMeshBase == h.polyCount,
            "Unsupported BV tree or off-mesh data");
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
    for (int i = 0; i < h.polyCount; ++i) {
        const auto& p = polys[i];
        require(p.vertCount >= 3 && p.vertCount <= 6 && p.firstLink == 0 && p.flags == 1 &&
                    p.areaAndtype == 0,
                "Unsupported or invalid navmesh polygon");
        edges += p.vertCount;
        std::set<unsigned> unique;
        double sign = 0, area = 0;
        for (unsigned j = 0; j < p.vertCount; ++j) {
            require(p.verts[j] < h.vertCount && unique.insert(p.verts[j]).second,
                    "Invalid/repeated polygon vertex index");
            require(p.neis[j] <= h.polyCount && p.neis[j] != i + 1, "Invalid polygon neighbour");
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
    require(edges == unsigned(h.maxLinkCount) && nextVert == unsigned(h.detailVertCount) &&
                nextTri == unsigned(h.detailTriCount),
            "Navmesh section counts do not agree");
}
nlohmann::json tile_properties(std::span<const std::byte> bytes) {
    validate_tile(bytes);
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
    MeshSpace space = MeshSpace::Spatial3D;
    float altitude = 0; // Derived flat native Y; never a second authored position.
    const dtNavMesh* native() const { return base_tiles ? base_tiles->mesh() : mesh.get(); }
};
Mesh::Mesh(std::span<const std::byte> bytes, MeshSpace space, bool dynamic) {
    impl_ = std::make_unique<Impl>();
    impl_->space = space;
    if (dynamic) {
        impl_->layers = decode_layers(bytes);
        require(impl_->layers->settings.plane == space, "Navigation layer/query plane mismatch");
        impl_->base_tiles = std::make_unique<TileCache>(*impl_->layers);
    } else {
        validate_tile(bytes, space);
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
            tiles = std::make_unique<TileCache>(*owner->impl_->layers);
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
            s.tiles = std::make_unique<TileCache>(*s.owner->impl_->layers);
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
NavResult Query::path(Double3 a, Double3 b) {
    if (impl_->owner->dynamic_obstacles() && !impl_->tiles)
        return result(NavStatus::Unavailable);
    if (!point(a) || !point(b))
        return result(NavStatus::Invalid);
    float start[3], end[3];
    auto ra = impl_->nearest(a, start), rb = impl_->nearest(b, end);
    if (!ra)
        return result(NavStatus::StartOutside);
    if (!rb)
        return result(NavStatus::EndOutside);
    dtPolyRef polys[128]{};
    int count = 0;
    auto status = impl_->query->findPath(ra, rb, start, end, &impl_->filter, polys, &count, 128);
    if (status & (DT_OUT_OF_NODES | DT_BUFFER_TOO_SMALL))
        return result(NavStatus::Limit);
    if (dtStatusFailed(status) || count == 0)
        return result(NavStatus::NoPath);
    if ((status & DT_PARTIAL_RESULT) || polys[count - 1] != rb)
        return result(NavStatus::Partial);
    float points[max_waypoints * 3]{};
    int n = 0;
    status = impl_->query->findStraightPath(start, end, polys, count, points, nullptr, nullptr, &n,
                                            max_waypoints);
    if (status & (DT_OUT_OF_NODES | DT_BUFFER_TOO_SMALL))
        return result(NavStatus::Limit);
    if (dtStatusFailed(status))
        return result(NavStatus::NoPath);
    NavResult out;
    out.status = NavStatus::Success;
    for (int i = 0; i < n; ++i) {
        Double3 p{points[i * 3], points[i * 3 + 1], points[i * 3 + 2]};
        if (!point(p))
            return result(NavStatus::Invalid);
        out.points.push_back(world_coordinates(impl_->owner->space(), p, a[2]));
    }
    return out;
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
        if (!point(neighbour.position) || !velocity_valid(neighbour.velocity))
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
        solver.addCircle(p, radius, v, v);
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
