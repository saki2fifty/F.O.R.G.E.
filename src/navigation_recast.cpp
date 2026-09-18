#include "navigation_geometry.hpp"
#include "navigation_mesh.hpp"
#include <DetourAlloc.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <cmath>
#include <cstring>
#include <numeric>
namespace forge::navigation_detail {
namespace {
void require(bool ok) {
    if (!ok)
        throw std::runtime_error(
            "Navigation build failed or exceeded supported grid/polygon/detail limits");
}
} // namespace
std::vector<std::byte> build_tile(const Geometry& g, NavigationSettings settings) {
    validate_navigation_settings(settings);
    const auto& verts = g.vertices;
    const auto& indices = g.indices;
    require(!indices.empty() && indices.size() % 3 == 0 && indices.size() <= 16384 * 3 &&
            verts.size() == indices.size() * 3);
    for (std::size_t i = 0; i < indices.size(); ++i)
        require(indices[i] == int(i));
    for (float v : verts)
        require(std::isfinite(v) && std::abs(v) <= 4090);
    rcContext ctx;
    rcConfig cfg{};
    cfg.cs = settings.cell_size;
    cfg.ch = settings.cell_height;
    cfg.walkableSlopeAngle = settings.slope;
    cfg.walkableHeight = int(std::ceil(settings.height / cfg.ch));
    cfg.walkableClimb = int(std::floor(settings.climb / cfg.ch));
    cfg.walkableRadius = int(std::ceil(settings.radius / cfg.cs));
    cfg.maxEdgeLen = 60;
    cfg.maxSimplificationError = 1.3f;
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = 1.2f;
    cfg.detailSampleMaxError = .1f;
    rcCalcBounds(verts.data(), int(verts.size() / 3), cfg.bmin, cfg.bmax);
    cfg.bmin[1] -= 2 * cfg.ch;
    cfg.bmax[1] += settings.height + cfg.ch;
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
    require(cfg.width > 0 && cfg.height > 0 && cfg.width <= 512 && cfg.height <= 512);
    require((cfg.bmax[1] - cfg.bmin[1]) / cfg.ch < 8191);
    std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(),
                                                                       rcFreeHeightField);
    require(bool(solid));
    require(rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs,
                                cfg.ch));
    std::vector<unsigned char> areas(indices.size() / 3);
    rcMarkWalkableTriangles(&ctx, settings.slope, verts.data(), int(verts.size() / 3),
                            indices.data(), int(areas.size()), areas.data());
    require(rcRasterizeTriangles(&ctx, verts.data(), int(verts.size() / 3), indices.data(),
                                 areas.data(), int(areas.size()), *solid, cfg.walkableClimb));
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);
    std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(
        rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
    require(bool(compact));
    require(
        rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *compact));
    require(rcErodeWalkableArea(&ctx, cfg.walkableRadius, *compact));
    require(rcBuildDistanceField(&ctx, *compact));
    require(rcBuildRegions(&ctx, *compact, 0, 0, 0));
    std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(),
                                                                        rcFreeContourSet);
    require(bool(contours));
    require(rcBuildContours(&ctx, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours));
    std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> polys(rcAllocPolyMesh(), rcFreePolyMesh);
    require(bool(polys));
    require(rcBuildPolyMesh(&ctx, *contours, 6, *polys));
    require(polys->npolys > 0 && polys->npolys <= 4096 && polys->nverts >= 3 &&
            polys->nverts <= 16384);
    std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail(
        rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
    require(bool(detail));
    require(rcBuildPolyMeshDetail(&ctx, *polys, *compact, cfg.cs * 6, cfg.ch, *detail));
    require(detail->nmeshes == polys->npolys && detail->nverts >= 0 && detail->nverts < 65536 &&
            detail->ntris > 0 && detail->ntris <= 65536);
    unsigned detail_vertices = 0, detail_triangles = 0;
    for (int i = 0; i < polys->npolys; ++i) {
        const auto* polygon = polys->polys + i * 12;
        unsigned count = 0;
        while (count < 6 && polygon[count] != RC_MESH_NULL_IDX) {
            require(polygon[count] < polys->nverts);
            ++count;
        }
        require(count >= 3);
        const auto* sub = detail->meshes + i * 4;
        require(sub[0] == detail_vertices && sub[2] == detail_triangles && sub[1] >= count &&
                sub[1] <= 255 && sub[3] >= count - 2 && sub[3] <= 255);
        require(sub[1] <= unsigned(detail->nverts) - detail_vertices &&
                sub[3] <= unsigned(detail->ntris) - detail_triangles);
        for (unsigned t = 0; t < sub[3]; ++t)
            for (unsigned k = 0; k < 3; ++k)
                require(detail->tris[(sub[2] + t) * 4 + k] < sub[1]);
        detail_vertices += sub[1];
        detail_triangles += sub[3];
    }
    require(detail_vertices == unsigned(detail->nverts) &&
            detail_triangles == unsigned(detail->ntris));
    for (float v : std::span(detail->verts, std::size_t(detail->nverts) * 3))
        require(std::isfinite(v) && std::abs(v) <= 4096);
    for (int i = 0; i < polys->npolys; ++i) {
        polys->flags[i] = 1;
        polys->areas[i] = 0;
    }
    dtNavMeshCreateParams p{};
    p.verts = polys->verts;
    p.vertCount = polys->nverts;
    p.polys = polys->polys;
    p.polyAreas = polys->areas;
    p.polyFlags = polys->flags;
    p.polyCount = polys->npolys;
    p.nvp = 6;
    p.detailMeshes = detail->meshes;
    p.detailVerts = detail->verts;
    p.detailVertsCount = detail->nverts;
    p.detailTris = detail->tris;
    p.detailTriCount = detail->ntris;
    p.walkableHeight = settings.height;
    p.walkableRadius = settings.radius;
    p.walkableClimb = settings.climb;
    p.cs = cfg.cs;
    p.ch = cfg.ch;
    p.buildBvTree = false;
    std::copy(polys->bmin, polys->bmin + 3, p.bmin);
    std::copy(polys->bmax, polys->bmax + 3, p.bmax);
    unsigned char* data = nullptr;
    int bytes = 0;
    require(dtCreateNavMeshData(&p, &data, &bytes));
    std::unique_ptr<unsigned char, decltype(&dtFree)> owned(data, dtFree);
    require(bytes > 0 && bytes <= int(max_nav_bytes));
    std::vector<std::byte> out(bytes);
    std::memcpy(out.data(), data, bytes);
    validate_tile(out);
    return out;
}
} // namespace forge::navigation_detail
