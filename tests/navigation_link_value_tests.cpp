#include "navigation_links.hpp"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed navigation link was admitted");
}
} // namespace
int main() {
    try {
        NavigationLinkData a{EntityId::parse("11111111-1111-4111-8111-111111111111"),
                             {1, 2, 3},
                             {-2, 6, 7},
                             .5f,
                             false};
        const std::array links{a};
        NativeNavigationLinks native(links, MeshSpace::Spatial3D);
        dtNavMeshCreateParams params{};
        native.apply(params);
        check(params.offMeshConCount == 1 && params.offMeshConVerts[0] == 1 &&
                  params.offMeshConVerts[1] == 2 && params.offMeshConVerts[2] == 3 &&
                  params.offMeshConVerts[3] == -2 && params.offMeshConVerts[4] == 6 &&
                  params.offMeshConVerts[5] == 7 && params.offMeshConUserID[0] == 1 &&
                  params.offMeshConFlags[0] == 1 && params.offMeshConAreas[0] == 0 &&
                  params.offMeshConDir[0] == 0 && params.offMeshConRad[0] == .5f,
              "Spatial link direction, identity or builder coordinates changed");
        a.start = {1, 2, 0};
        a.end = {3, 4, 0};
        a.bidirectional = true;
        const std::array xy{a};
        NativeNavigationLinks planar(xy, MeshSpace::PlanarXY, .125f);
        planar.apply(params);
        check(params.offMeshConVerts[0] == 1 && params.offMeshConVerts[1] == .125f &&
                  params.offMeshConVerts[2] == 2 && params.offMeshConVerts[3] == 3 &&
                  params.offMeshConVerts[4] == .125f && params.offMeshConVerts[5] == 4 &&
                  params.offMeshConDir[0] == DT_OFFMESH_CON_BIDIR,
              "XY link basis or native flat altitude changed");
        auto values = navigation_link_values(xy, MeshSpace::PlanarXY);
        check(read_navigation_links(values, MeshSpace::PlanarXY) ==
                  std::vector<NavigationLinkData>{a},
              "Admitted source identity/value roundtrip changed");
        NativeNavigationLinks empty({}, MeshSpace::Spatial3D);
        empty.apply(params);
        check(params.offMeshConCount == 0 && !params.offMeshConVerts && !params.offMeshConUserID,
              "Empty link set retained stale native builder pointers");
        for (const auto& patch :
             {nlohmann::json{{"radius", true}}, nlohmann::json{{"radius", -.1}},
              nlohmann::json{{"radius", 6}}, nlohmann::json{{"radius", "0.5"}},
              nlohmann::json{{"bidirectional", 1}}, nlohmann::json{{"end", {true, 0, 0}}},
              nlohmann::json{{"start", {1, 2}}}, nlohmann::json{{"extra", 1}},
              nlohmann::json{{"entity", "00000000-0000-0000-0000-000000000000"}},
              nlohmann::json{{"end", {4091, 0, 0}}}, nlohmann::json{{"end", {3, 4, 1}}},
              nlohmann::json{{"end", {1, 2, 0}}}}) {
            auto bad = values;
            bad[0].update(patch);
            rejects([&] { (void)read_navigation_links(bad, MeshSpace::PlanarXY); });
        }
        auto duplicate = values;
        duplicate.push_back(values[0]);
        rejects([&] { (void)read_navigation_links(duplicate, MeshSpace::PlanarXY); });
        auto unsorted = values;
        unsorted[0]["entity"] = "22222222-2222-4222-8222-222222222222";
        unsorted.push_back(values[0]);
        rejects([&] { (void)read_navigation_links(unsorted, MeshSpace::PlanarXY); });
        auto boundary = a;
        boundary.start = {0, 0, 0};
        boundary.end = {.01f, 0, 0};
        boundary.radius = .05f;
        validate_navigation_links(std::array{boundary}, MeshSpace::Spatial3D);
        boundary.end[0] = std::nextafter(.01f, 0.f);
        rejects([&] { validate_navigation_links(std::array{boundary}, MeshSpace::Spatial3D); });
        std::vector<NavigationLinkData> full;
        for (unsigned i = 0; i < navigation_link_limit; ++i) {
            full.push_back(a);
            full.back().entity = EntityId::generate();
        }
        std::ranges::sort(full, {}, &NavigationLinkData::entity);
        NativeNavigationLinks capacity(full, MeshSpace::PlanarXY, .125f);
        capacity.apply(params);
        check(params.offMeshConCount == 32 && params.offMeshConUserID[31] == 32 &&
                  params.offMeshConVerts[31 * 6 + 5] == 4,
              "Maximum link capacity truncated the final native entry");
        auto overflow = nlohmann::json::array();
        for (unsigned i = 0; i < 33; ++i)
            overflow.push_back(values[0]);
        rejects([&] { (void)read_navigation_links(overflow, MeshSpace::PlanarXY); });
        a.radius = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { validate_navigation_links(std::array{a}, MeshSpace::PlanarXY); });
        rejects([&] {
            (void)NativeNavigationLinks(xy, MeshSpace::PlanarXY,
                                        std::numeric_limits<float>::infinity());
        });
        rejects([&] { validate_navigation_links({}, MeshSpace(2)); });
        std::cout << "Bounded navigation link values/native parameters PASS; traversal integration "
                     "pending\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
