#include "navigation_mesh.hpp"
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
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
void validate_tile(std::span<const std::byte> bytes) {
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
};
Mesh::Mesh(std::span<const std::byte> bytes) {
    validate_tile(bytes);
    impl_ = std::make_unique<Impl>();
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
Mesh::~Mesh() = default;
std::vector<Double3> Mesh::triangles() const {
    const auto* tile = static_cast<const dtNavMesh*>(impl_->mesh.get())->getTile(0);
    std::vector<Double3> out;
    for (int i = 0; i < tile->header->polyCount; ++i) {
        const auto& p = tile->polys[i];
        for (unsigned j = 1; j + 1 < p.vertCount; ++j)
            for (auto v : {p.verts[0], p.verts[j], p.verts[j + 1]}) {
                auto* xyz = &tile->verts[v * 3];
                out.push_back({xyz[0], xyz[1], xyz[2]});
            }
    }
    return out;
}
struct Query::Impl {
    std::shared_ptr<const Mesh> owner;
    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{dtAllocNavMeshQuery(),
                                                                         dtFreeNavMeshQuery};
    dtQueryFilter filter;
    explicit Impl(std::shared_ptr<const Mesh> value) : owner(std::move(value)) {
        require(owner && query && dtStatusSucceed(query->init(owner->impl_->mesh.get(), 2048)),
                "Cannot allocate nav query");
    }
    dtPolyRef nearest(Double3 p, float* projected) {
        float v[3]{float(p[0]), float(p[1]), float(p[2])}, ext[3]{.5f, 2, .5f};
        dtPolyRef ref = 0;
        auto s = query->findNearestPoly(v, ext, &filter, &ref, projected);
        return dtStatusSucceed(s) ? ref : 0;
    }
};
Query::Query(std::shared_ptr<const Mesh> value) : impl_(std::make_unique<Impl>(std::move(value))) {}
Query::~Query() = default;
NavResult Query::project(Double3 p) {
    if (!point(p))
        return result(NavStatus::Invalid);
    float pos[3];
    if (!impl_->nearest(p, pos))
        return result(NavStatus::StartOutside);
    return {NavStatus::Success, {{pos[0], pos[1], pos[2]}}, {}};
}
NavResult Query::path(Double3 a, Double3 b) {
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
        out.points.push_back(p);
    }
    return out;
}
NavResult Query::move(Double3 a, Double3 b) {
    if (!point(a) || !point(b))
        return result(NavStatus::Invalid);
    float start[3], target[3]{float(b[0]), float(b[1]), float(b[2])}, end[3]{};
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
    return {NavStatus::Success, {p}, {}};
}
} // namespace navigation_detail
} // namespace forge
