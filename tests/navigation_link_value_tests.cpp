#include "navigation_link_collision.hpp"
#include "navigation_link_curve.hpp"
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
void curve_geometry() {
    struct Case {
        Double3 start, end;
        double height;
    };
    const std::array cases{Case{{0, 0, 7}, {4, 0, 7}, 0},
                           Case{{0, 0, 7}, {4, 0, 7}, 1},
                           Case{{0, 0, 7}, {double(.01f), 0, 7}, 100},
                           Case{{0, -4090, 0}, {double(.01f), 4090, 0}, .001},
                           Case{{0, 4090, 0}, {double(.01f), -4090, 0}, .001},
                           Case{{-20, 2, -30}, {40, 5, 25}, 10},
                           Case{{0, 0, 7}, {4, 3, 7}, 1e-30},
                           Case{{0, 0, 7}, {4, -3, 7}, std::numeric_limits<float>::denorm_min()},
                           Case{{0, 0, 0}, {0, 3, 0}, 0}};
    for (const auto& c : cases) {
        const NavigationLinkCurve curve(c.start, c.end, c.height);
        for (const auto span : {std::array{0., 1.}, std::array{.25, .75}, std::array{.125, .126},
                                std::array{.5, .5}}) {
            const auto bound = curve.chord_deviation(span[0], span[1]);
            const auto first = curve.point(span[0]), last = curve.point(span[1]);
            for (unsigned sample = 0; sample <= 64; ++sample) {
                const auto u = double(sample) / 64;
                const auto point = curve.point(std::lerp(span[0], span[1], u));
                for (unsigned axis = 0; axis != 3; ++axis) {
                    const auto chord = std::lerp(first[axis], last[axis], u);
                    check(std::abs(point[axis] - chord) <= (axis == 1 ? bound : 0) + 1e-9,
                          "Curve escaped its analytic sweep chord bound");
                }
            }
        }
        for (const auto tolerance : {.0005, .01, .5}) {
            const auto end = curve.sweep_end(.123, .876, tolerance);
            check(end > .123 && end <= .876 && curve.chord_deviation(.123, end) <= tolerance,
                  "Curve sweep interval lost progress, endpoint or padding bounds");
        }
        check(curve.point(0) == c.start && curve.point(1) == c.end &&
                  curve.advance(0, curve.length()) == 1 && curve.advance(.5, 0) == .5,
              "Navigation curve lost exact endpoints or zero-distance hold");
        const auto middle = curve.point(.5);
        check(std::abs(middle[1] - ((c.start[1] + c.end[1]) * .5 + c.height)) < 1e-10,
              "Navigation curve midpoint lost authored world-Y lift");
        // Norm of an affine derivative is convex. Independent midpoint/trapezoid
        // integration bounds the true arc length, without using its primitive.
        const auto speed = [&](long double t) {
            const auto x = static_cast<long double>(c.end[0]) - c.start[0];
            const auto z = static_cast<long double>(c.end[2]) - c.start[2];
            const auto y =
                static_cast<long double>(c.end[1]) - c.start[1] + 4 * c.height * (1 - 2 * t);
            return std::sqrt(x * x + y * y + z * z);
        };
        constexpr unsigned subdivisions = 32768;
        long double lower = 0, upper = (speed(0) + speed(1)) * .5;
        for (unsigned i = 0; i != subdivisions; ++i) {
            lower += speed((static_cast<long double>(i) + .5) / subdivisions);
            if (i)
                upper += speed(static_cast<long double>(i) / subdivisions);
        }
        lower /= subdivisions;
        upper /= subdivisions;
        check(curve.length() >= lower - 5e-8 && curve.length() <= upper + 5e-8,
              "Navigation curve length escaped independent integration bounds");
        const NavigationLinkCurve reverse(c.end, c.start, c.height);
        check(std::abs(reverse.length() - curve.length()) < 5e-8,
              "Reversing a navigation arc changed its world-distance length");
        double t = 0;
        for (unsigned step = 0; step != 200 && t != 1; ++step) {
            const auto next = curve.advance(t, .017);
            check(next > t && curve.length(t, next) <= .017 + 1e-9,
                  "Navigation arc exceeded its per-tick distance budget or stopped advancing");
            const auto a = curve.point(t), b = curve.point(next);
            check(std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]) <= .017 + 1e-9,
                  "Navigation arc world displacement exceeded its distance budget");
            check(std::abs(curve.parameter(b) - next) < 1e-10,
                  "Navigation arc progress did not derive from its current pose");
            t = next;
        }
        check(std::abs(curve.length(0, .25) + curve.length(.25, 1) - curve.length()) < 5e-8,
              "Navigation arc interval distances do not compose");
    }
    const NavigationLinkCurve curve({0, 0, 7}, {4, 0, 7}, 1);
    check(curve.parameter({-1, 0, 7}) < 0 && curve.parameter({5, 0, 7}) > 1,
          "Navigation curve silently clamped off-span recovery poses");
    for (double invalid : {-1., 1.1, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
        rejects([&] { (void)curve.point(invalid); });
        rejects([&] { (void)curve.advance(invalid, 1); });
    }
    rejects([&] { (void)curve.length(.8, .2); });
    rejects([&] { (void)curve.chord_deviation(.8, .2); });
    for (const auto tolerance : {0., -1., std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { (void)curve.sweep_end(0, 1, tolerance); });
    rejects([&] { (void)curve.advance(0, -.1); });
    rejects([&] { (void)curve.advance(0, std::numeric_limits<double>::infinity()); });
    rejects([] { (void)NavigationLinkCurve({0, 0, 0}, {0, 3, 0}, 1); });
    rejects([] { (void)NavigationLinkCurve({0, 0, 0}, {.001, 0, 0}, 1); });
    rejects([] { (void)NavigationLinkCurve({0, 0, 0}, {4, 0, 0}, -1); });
    rejects([] { (void)NavigationLinkCurve({0, 0, 0}, {4, 0, 0}, 101); });
    rejects([] { (void)NavigationLinkCurve({0, 4090, 0}, {4, 4090, 0}, 10); });
}
void collision_values() {
    const NavigationLinkCollision defaults;
    const auto source = link_collision_value(defaults);
    check(link_collision_from_value(source) == defaults,
          "Navigation link collision defaults did not round trip");
    for (const auto& mask :
         {nlohmann::json(-1), nlohmann::json(.5), nlohmann::json(std::uint64_t{1} << 32),
          nlohmann::json(UINT64_MAX), nlohmann::json(true), nlohmann::json("7")}) {
        auto invalid = source;
        invalid["mask"] = mask;
        rejects([&] { (void)link_collision_from_value(invalid); });
    }
    for (const auto mask : {0u, 1u, UINT32_MAX}) {
        auto value = source;
        value["mask"] = mask;
        check(link_collision_from_value(value).mask == mask,
              "Navigation link collision narrowed a valid mask");
    }
    for (const auto* field : {"radius", "straight_height", "clearance"})
        for (const auto invalid_length : {0., -.1, 101., std::numeric_limits<double>::infinity(),
                                          std::numeric_limits<double>::quiet_NaN()}) {
            auto invalid = source;
            invalid[field] = invalid_length;
            rejects([&] { (void)link_collision_from_value(invalid); });
        }
    auto invalid = defaults;
    invalid.clearance = 2;
    rejects([&] { (void)link_collision_value(invalid); });
}
} // namespace
int main() {
    try {
        curve_geometry();
        collision_values();
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
        std::cout << "Bounded navigation link values/native parameters and curve geometry PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
