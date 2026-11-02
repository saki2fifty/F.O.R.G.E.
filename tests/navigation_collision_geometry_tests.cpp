#include "navigation_asset.hpp"
#include "navigation_collision_geometry.hpp"
#include "navigation_geometry.hpp"
#include <iostream>

using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    bool failed = false;
    try {
        action();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    check(failed, "Invalid collider footprint was admitted");
}
void representability() {
    for (unsigned kind : {0u, 1u, 2u})
        for (double offset : {0., 3000.})
            for (float radius : {.005f, .5f, 5.f, 250.f})
                for (double angle : {-179., -90., -13., 0., 67., 179.})
                    for (float sign : {-1.f, 1.f}) {
                        const std::array<float, 2> raw = kind == 0
                                                             ? std::array{2 * radius, 3 * radius}
                                                         : kind == 1 ? std::array{radius, 0.f}
                                                                     : std::array{radius, .01f};
                        const auto transform =
                            affine_transform({{offset, -offset, 91},
                                              rotation_about_axis({0, 0, 1}, angle),
                                              {sign, 1, 0}});
                        for (bool obstacle : {false, true}) {
                            const auto points = collider_outline_2d(kind, raw, transform, obstacle);
                            check(points.size() >= 3 && points.size() <= 64,
                                  "Derived collider footprint exceeded its contour budget");
                            std::vector<geometry_detail::PolygonPoint> xy;
                            for (const auto& p : points) {
                                check(p[2] == 0 && double(float(p[0])) == p[0] &&
                                          double(float(p[1])) == p[1],
                                      "Collider footprint lost XY/native float conversion");
                                xy.push_back({p[0], p[1]});
                            }
                            check(geometry_detail::validate_polygon(xy) > 0,
                                  "Derived collider footprint lost CCW simple geometry");
                            if (kind == 1) {
                                // Independent support witnesses: every point is within the
                                // true disk for floors; masks include sampled disk boundary.
                                if (!obstacle)
                                    for (const auto& p : xy)
                                        check(std::hypot(p[0] - offset, p[1] + offset) <= radius,
                                              "Walkable circle exceeded the analytic disk");
                                else
                                    for (unsigned i = 0; i < 360; ++i) {
                                        const double a = i * std::numbers::pi / 180;
                                        const geometry_detail::PolygonPoint witness{
                                            offset + radius * std::cos(a),
                                            -offset + radius * std::sin(a)};
                                        for (std::size_t edge = 0; edge < xy.size(); ++edge)
                                            check(geometry_detail::polygon_turn(
                                                      xy[edge], xy[(edge + 1) % xy.size()],
                                                      witness) >= 0,
                                                  "Rounded circle mask missed an analytic arc");
                                    }
                            }
                        }
                    }
    rejects(
        [] { (void)collider_outline_2d(1, {1, 0}, affine_transform({{}, {}, {1, 2, 0}}), true); });
    rejects([] { (void)collider_outline_2d(0, {1, 1}, affine_transform({{4090, 0, 0}}), true); });
    rejects([] { (void)collider_outline_2d(2, {500, 1}, affine_transform({}), false); });
    rejects([] {
        (void)collider_outline_2d(1, {.5f, 0}, affine_transform({{4089.5001, 0, 0}}), false);
    });
    rejects([] {
        (void)collider_outline_2d(
            0, {1, 1}, affine_transform({{}, rotation_about_axis({1, 0, 0}, 30), {}}), true);
    });
}
void actual_routes() {
    for (unsigned kind : {0u, 1u, 2u}) {
        Geometry g;
        g.vertices = {-10, 0, -10, 10, 0, 10, 10, 0, -10, -10, 0, -10, -10, 0, 10, 10, 0, 10};
        g.indices = {0, 1, 2, 3, 4, 5};
        const auto outline = collider_outline_2d(kind,
                                                 kind == 0   ? std::array{2.f, 4.f}
                                                 : kind == 1 ? std::array{2.f, 0.f}
                                                             : std::array{1.f, 4.f},
                                                 affine_transform({}), true);
        std::vector<geometry_detail::PolygonPoint> xy;
        for (const auto& p : outline)
            xy.push_back({p[0], p[1]});
        std::vector<float> mask;
        for (const auto& point : xy)
            mask.insert(mask.end(), {float(point[0]), 0, float(point[1])});
        g.obstacles.push_back(std::move(mask));
        NavigationSettings settings;
        settings.plane = NavigationPlane::PlanarXY;
        Query query(std::make_shared<Mesh>(build_tile(g, settings), NavigationPlane::PlanarXY));
        const auto path = query.path({-8, 0, 7}, {8, 0, -19});
        check(path.status == NavStatus::Success && path.points.size() > 2,
              "Curved/box collider mask did not produce an actual detour");
        for (const auto& point : path.points)
            check(point[2] == 7, "Collider-derived XY route changed visual depth");
    }
}
} // namespace
int main() {
    try {
        representability();
        actual_routes();
        std::cout << "Conservative native-rounded collider footprints and actual routes PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
