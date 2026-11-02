#include "navigation_geometry.hpp"
#include "navigation_layers.hpp"
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
using Compact = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>;
void validate_geometry(const Geometry& g, NavigationSettings settings) {
    validate_navigation_settings(settings);
    const auto& verts = g.vertices;
    const auto& indices = g.indices;
    require(!indices.empty() && indices.size() % 3 == 0 && indices.size() <= 16384 * 3 &&
            verts.size() == indices.size() * 3);
    for (std::size_t i = 0; i < indices.size(); ++i)
        require(indices[i] == int(i));
    for (float v : verts)
        require(std::isfinite(v) && std::abs(v) <= 4090);
    const bool planar = settings.plane == NavigationPlane::PlanarXY;
    require(g.obstacles.size() <= 512 && (planar || g.obstacles.empty()));
    if (planar) {
        for (std::size_t i = 1; i < verts.size(); i += 3)
            require(verts[i] == 0);
        for (const auto& mask : g.obstacles) {
            require(mask.size() >= 9 && mask.size() % 3 == 0 &&
                    mask.size() / 3 <= navigation_region_vertex_limit);
            const auto corners = mask.size() / 3;
            std::vector<geometry_detail::PolygonPoint> points;
            points.reserve(corners);
            for (std::size_t i = 0; i < mask.size(); ++i)
                require(std::isfinite(mask[i]) && std::abs(mask[i]) <= 4090 &&
                        (i % 3 != 1 || mask[i] == 0));
            for (std::size_t i = 0; i < corners; ++i)
                points.push_back({mask[i * 3], mask[i * 3 + 2]});
            (void)geometry_detail::validate_polygon(points);
            for (std::size_t i = 0; i < corners; ++i) {
                const auto a = i * 3, b = ((i + 1) % corners) * 3, c = ((i + 2) % corners) * 3;
                const double turn =
                    (double(mask[b]) - mask[a]) * (double(mask[c + 2]) - mask[b + 2]) -
                    (double(mask[b + 2]) - mask[a + 2]) * (double(mask[c]) - mask[b]);
                require(turn > 1e-10); // Canonical convex winding; reject float-collapsed masks.
            }
        }
    }
}
rcConfig configuration(const Geometry& g, NavigationSettings settings) {
    rcConfig cfg{};
    const auto& verts = g.vertices;
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
    return cfg;
}
Compact rasterize(rcContext& context, const Geometry& g, NavigationSettings settings,
                  const rcConfig& cfg) {
    const auto& verts = g.vertices;
    const auto& indices = g.indices;
    std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(),
                                                                       rcFreeHeightField);
    require(bool(solid));
    require(rcCreateHeightfield(&context, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs,
                                cfg.ch));
    std::vector<unsigned char> areas(indices.size() / 3);
    rcMarkWalkableTriangles(&context, settings.slope, verts.data(), int(verts.size() / 3),
                            indices.data(), int(areas.size()), areas.data());
    require(rcRasterizeTriangles(&context, verts.data(), int(verts.size() / 3), indices.data(),
                                 areas.data(), int(areas.size()), *solid, cfg.walkableClimb));
    rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);
    std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(
        rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
    require(bool(compact));
    require(rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid,
                                      *compact));
    for (const auto& mask : g.obstacles)
        rcMarkConvexPolyArea(&context, mask.data(), int(mask.size() / 3), cfg.bmin[1] - cfg.ch,
                             cfg.bmax[1] + cfg.ch, RC_NULL_AREA, *compact);
    require(rcErodeWalkableArea(&context, cfg.walkableRadius, *compact));
    return compact;
}
} // namespace
std::vector<std::byte> build_tile(const Geometry& g, NavigationSettings settings) {
    if (settings.dynamic_obstacles)
        return encode_layers(build_layers(g, settings));
    validate_geometry(g, settings);
    rcContext ctx;
    const auto cfg = configuration(g, settings);
    auto compact = rasterize(ctx, g, settings, cfg);
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
    validate_tile(out, settings.plane);
    return out;
}
HeightfieldLayers build_layers(const Geometry& g, NavigationSettings settings) {
    validate_geometry(g, settings);
    require(settings.dynamic_obstacles);
    const auto whole = configuration(g, settings);
    const auto nx = (unsigned(whole.width) + layer_width - 1) / layer_width;
    const auto nz = (unsigned(whole.height) + layer_width - 1) / layer_width;
    require(nx * nz <= max_layers);
    HeightfieldLayers out;
    out.settings = settings;
    std::copy(whole.bmin, whole.bmin + 3, out.minimum.begin());
    std::copy(whole.bmax, whole.bmax + 3, out.maximum.begin());
    rcContext context;
    for (unsigned z = 0; z < nz; ++z) {
        for (unsigned x = 0; x < nx; ++x) {
            auto cfg = whole;
            cfg.borderSize = whole.walkableRadius + 3;
            cfg.width = cfg.height = int(layer_width) + cfg.borderSize * 2;
            cfg.bmin[0] += float(int(x * layer_width) - cfg.borderSize) * cfg.cs;
            cfg.bmin[2] += float(int(z * layer_width) - cfg.borderSize) * cfg.cs;
            cfg.bmax[0] = cfg.bmin[0] + cfg.width * cfg.cs;
            cfg.bmax[2] = cfg.bmin[2] + cfg.height * cfg.cs;
            auto compact = rasterize(context, g, settings, cfg);
            std::unique_ptr<rcHeightfieldLayerSet, decltype(&rcFreeHeightfieldLayerSet)> layers(
                rcAllocHeightfieldLayerSet(), rcFreeHeightfieldLayerSet);
            require(bool(layers));
            require(rcBuildHeightfieldLayers(&context, *compact, cfg.borderSize, cfg.walkableHeight,
                                             *layers));
            require(layers->nlayers <= 4);
            for (int i = 0; i < layers->nlayers; ++i) {
                require(out.layers.size() < max_layers);
                const auto& source = layers->layers[i];
                require(source.width == int(layer_width) && source.height == int(layer_width));
                // Portal matching can raise an emitted height beyond the layer's
                // original region maximum. Reserve the climb range before any
                // byte can wrap, then publish bounds covering all emitted cells.
                require(source.hmin >= 0 && source.hmax >= source.hmin &&
                        source.hmax - source.hmin + cfg.walkableClimb < 255);
                HeightfieldLayer layer;
                layer.x = x;
                layer.z = z;
                layer.index = unsigned(i);
                layer.hmin = unsigned(source.hmin);
                layer.hmax = unsigned(source.hmax);
                layer.minx = unsigned(source.minx);
                layer.maxx = unsigned(source.maxx);
                layer.minz = unsigned(source.miny);
                layer.maxz = unsigned(source.maxy);
                std::copy(source.bmin, source.bmin + 3, layer.minimum.begin());
                std::copy(source.bmax, source.bmax + 3, layer.maximum.begin());
                constexpr auto grid = layer_width * layer_width;
                std::copy_n(source.heights, grid, layer.cells.begin());
                std::copy_n(source.areas, grid, layer.cells.begin() + grid);
                std::copy_n(source.cons, grid, layer.cells.begin() + grid * 2);
                for (unsigned cell = 0; cell < grid; ++cell)
                    if (source.heights[cell] != 255)
                        layer.hmax = std::max(layer.hmax, layer.hmin + source.heights[cell]);
                layer.maximum[1] = whole.bmin[1] + layer.hmax * cfg.ch;
                out.layers.push_back(std::move(layer));
            }
        }
    }
    validate_layers(out);
    return out;
}
} // namespace forge::navigation_detail
