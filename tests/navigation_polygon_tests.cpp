#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include "navigation_region_value.hpp"
#include <iostream>
#include <limits>
#include <numbers>

namespace {
using namespace forge;
using namespace forge::navigation_detail;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid navigation polygon was admitted");
}
NavigationRegion2D l_region(bool obstacle = false) {
    NavigationRegion2D value;
    value.obstacle = obstacle;
    value.points = {{-8, -8}, {8, -8}, {8, -2}, {-2, -2}, {-2, 8}, {-8, 8}};
    return value;
}
Json document(const NavigationRegion2D& region,
              const AffineTransform& transform = affine_transform({})) {
    return {
        {"asset_id", "11111111-1111-4111-8111-111111111111"},
        {"entities",
         Json::array({{{"id", "22222222-2222-4222-8222-222222222222"},
                       {"spatial_resolved", true},
                       {"world_affine", transform.m},
                       {"components", {{"forge.navigation_region_2d", region_value(region)}}}}})}};
}
Geometry capture_geometry(const Json& value) {
    return geometry(capture(value, NavigationPlane::PlanarXY));
}
NavigationSettings settings() {
    NavigationSettings value;
    value.plane = NavigationPlane::PlanarXY;
    return value;
}
void legacy_rectangle() {
    NavigationRegion2D rectangle{true, false, 20, 20};
    auto source = document(rectangle);
    source["entities"][0]["components"]["forge.navigation_region_2d"].erase("points");
    const auto old = capture_geometry(source);
    const auto complete = capture_geometry(document(rectangle));
    check(old.digest == "65f4ce382065bda0c1029509f8e34019f65aad8fbd397b8cbd6e9c5e78d6de03" &&
              complete.digest == old.digest && complete.vertices == old.vertices,
          "Legacy rectangle digest/default admission changed");
    const std::vector<float> expected{-10, 0, -10, 10,  0, 10, 10, 0, -10,
                                      -10, 0, -10, -10, 0, 10, 10, 0, 10};
    check(old.vertices == expected && old.indices == std::vector<int>({0, 1, 2, 3, 4, 5}),
          "Legacy rectangle triangle bytes changed");
    check(build_tile(old, settings()) == build_tile(complete, settings()),
          "Legacy rectangle cooked bytes changed with an empty contour");
}
void contour_validation() {
    const auto good = l_region();
    check(region_from_value(region_value(good)) == good, "Polygon value lost typed points");
    const auto triangles = geometry_detail::polygon_triangles(region_local_points(good));
    long double area = 0;
    for (const auto& t : triangles) {
        const auto a = good.points[t[0]], b = good.points[t[1]], c = good.points[t[2]];
        area += ((static_cast<long double>(b.x) - a.x) * (c.y - a.y) -
                 (static_cast<long double>(b.y) - a.y) * (c.x - a.x)) /
                2;
    }
    check(triangles.size() == 4 && area == 156, "Concave contour lost analytic area");
    for (const auto& points :
         std::vector<std::vector<NavigationRegionVertex>>{{{0, 0}},
                                                          {{0, 0}, {1, 0}},
                                                          {{0, 0}, {1, 1}, {0, 1}, {1, 0}},
                                                          {{0, 0}, {1, 0}, {1, 1}, {0, 0}},
                                                          {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {0, 1}},
                                                          {{0, 0}, {0, 1}, {1, 0}},
                                                          {{0, 0}, {1001, 0}, {0, 1}}}) {
        auto bad = good;
        bad.points = points;
        rejects([&] { (void)region_local_points(bad); });
        rejects([&] { (void)capture_geometry(document(bad)); });
    }
    auto bad = good;
    bad.points[0].x = std::numeric_limits<float>::infinity();
    rejects([&] { (void)region_local_points(bad); });
    bad = good;
    bad.points[0].y = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { (void)region_local_points(bad); });
    NavigationRegion2D circle;
    for (unsigned i = 0; i < 64; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 64;
        circle.points.push_back({float(7 * std::cos(angle)), float(7 * std::sin(angle))});
    }
    check(capture_geometry(document(circle)).indices.size() == 62 * 3,
          "Maximum polygon capacity did not triangulate");
    circle.points.push_back({0, 0});
    rejects([&] { (void)region_local_points(circle); });
    rejects([&] { (void)region_from_value(region_value(circle)); });
    bad = good;
    bad.points = {{0, 0}, {1e-8f, 0}, {0, 1}};
    rejects([&] { (void)capture_geometry(document(bad, affine_transform({{3000, 0, 0}}))); });
    rejects([&] { (void)region_quad(good, affine_transform({})); });
}
void walkable_routes() {
    const auto region = l_region();
    const auto original = capture_geometry(document(region));
    for (float sx : {-1.f, 1.f})
        for (float sy : {-1.f, 1.f}) {
            const auto transform = affine_transform({{19, 17, 91}, {}, {sx, sy, 0}});
            const auto g = capture_geometry(document(region, transform));
            check(g.indices.size() == 12 && g.obstacles.empty(),
                  "Concave walkable contour did not produce four triangles");
            Query query(
                std::make_shared<Mesh>(build_tile(g, settings()), NavigationPlane::PlanarXY));
            const auto from = transform.point({-5, 5, 0});
            const auto to = transform.point({5, -5, 0});
            const auto notch = transform.point({5, 5, 0});
            const auto path = query.path({from[0], from[1], 7}, {to[0], to[1], -19});
            check(path.status == NavStatus::Success && path.points.size() >= 3,
                  "Concave walkable route did not turn around its notch");
            for (const auto& point : path.points)
                check(point[2] == 7, "Polygon route changed visual depth");
            check(query.project({notch[0], notch[1], 7}).status == NavStatus::StartOutside,
                  "Polygon decomposition filled the concave notch");
        }
    auto moved = document(region);
    moved["entities"][0]["world_affine"][11] = 91;
    check(capture_geometry(moved).digest == original.digest,
          "Visual depth changed polygon source identity");
    auto unused_extents = region;
    unused_extents.width = 19;
    unused_extents.height = 23;
    check(capture_geometry(document(unused_extents)).digest == original.digest,
          "Unused rectangle extents invalidated polygon geometry");
    auto edited = region;
    edited.points[2].x = 7;
    check(capture_geometry(document(edited)).digest != original.digest,
          "Polygon point edit did not invalidate baked geometry");
}
bool covers_point(const Mesh& mesh, Double3 point) {
    const auto triangles = mesh.triangles();
    const auto side = [&](Double3 a, Double3 b) {
        return (static_cast<long double>(b[0]) - a[0]) * (point[1] - a[1]) -
               (static_cast<long double>(b[1]) - a[1]) * (point[0] - a[0]);
    };
    for (std::size_t i = 0; i < triangles.size(); i += 3) {
        const auto a = side(triangles[i], triangles[i + 1]);
        const auto b = side(triangles[i + 1], triangles[i + 2]);
        const auto c = side(triangles[i + 2], triangles[i]);
        if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0))
            return true;
    }
    return false;
}
void obstacle_routes() {
    const auto outline = [] {
        NavigationRegion2D obstacle;
        obstacle.obstacle = true;
        obstacle.points = {{-3, -3}, {3, -3}, {3, -1}, {-1, -1}, {-1, 3}, {-3, 3}};
        return obstacle;
    }();
    for (float sign : {-1.f, 1.f}) {
        auto source = document(NavigationRegion2D{true, false, 20, 20});
        auto row = document(outline, affine_transform({{}, {}, {sign, 1, 0}}))["entities"][0];
        row["id"] = "33333333-3333-4333-8333-333333333333";
        source["entities"].push_back(row);
        const auto g = capture_geometry(source);
        check(g.obstacles.size() == 4 && g.obstacles[0].size() == 9,
              "Concave obstacle did not produce bounded triangle masks");
        const auto mesh =
            std::make_shared<Mesh>(build_tile(g, settings()), NavigationPlane::PlanarXY);
        Query query(mesh);
        const auto notch = query.project({sign, 1, 11});
        check(notch.status == NavStatus::Success &&
                  std::hypot(notch.points[0][0] - sign, notch.points[0][1] - 1) < .01,
              "Concave obstacle incorrectly filled its free notch");
        // Projection may snap an off-mesh point onto a nearby polygon. Inspect
        // the actual baked triangle union to prove this interior is excluded.
        check(!covers_point(*mesh, {-2 * sign, 0, 11}), "Obstacle interior remained navigable");
        const auto path = query.path({-7, 0, 11}, {7, 0, -4});
        check(path.status == NavStatus::Success && path.points.size() > 2,
              "Polygon obstacle failed to route through the existing native query");
        for (std::size_t i = 1; i < path.points.size(); ++i)
            for (unsigned step = 0; step <= 100; ++step) {
                const auto t = step / 100.;
                const double x = sign * (path.points[i - 1][0] * (1 - t) + path.points[i][0] * t);
                const double y = path.points[i - 1][1] * (1 - t) + path.points[i][1] * t;
                check(!((x > -3 && x < -1 && y > -3 && y < 3) ||
                        (x > -3 && x < 3 && y > -3 && y < -1)),
                      "Native route crossed the analytic obstacle interior");
            }
    }
}
} // namespace
int main() {
    try {
        legacy_rectangle();
        contour_validation();
        walkable_routes();
        obstacle_routes();
        std::cout << "Navigation polygon values, capacity/invalid admission, legacy digest/bytes, "
                     "concave routes/masks and signed/visual-Z behavior PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
