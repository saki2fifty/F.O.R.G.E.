#pragma once
#include <cmath>
#include <limits>
#include <numbers>

inline void curved_planar_mesh_tests() {
    using Point = std::array<double, 2>;
    const auto asset = AssetId::generate();
    const auto check_projection = [](const EditableMeshSource& source) {
        const auto mesh = source.cook();
        const auto& part = mesh.lods[0].parts[0];
        const auto& p = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& uv = std::get<std::vector<float>>(part.find("TEXCOORD_0")->values);
        const auto& normals = std::get<std::vector<float>>(part.find("NORMAL")->values);
        double area = 0;
        for (unsigned i = 0; i < part.indices.size(); i += 3) {
            const auto a = part.indices[i] * 3, b = part.indices[i + 1] * 3,
                       c = part.indices[i + 2] * 3;
            const auto twice =
                (p[b] - p[a]) * (p[c + 1] - p[a + 1]) - (p[b + 1] - p[a + 1]) * (p[c] - p[a]);
            require(twice > 0, "Curved construction lost its +Z front");
            area += twice / 2.;
        }
        for (unsigned i = 0; i < part.vertices; ++i) {
            require(normals[i * 3] == 0 && normals[i * 3 + 1] == 0 && normals[i * 3 + 2] == 1,
                    "Curved construction lost flat normals");
            for (unsigned axis = 0; axis < 2; ++axis) {
                const auto value = (p[i * 3 + axis] - part.bounds.minimum[axis]) /
                                   (part.bounds.maximum[axis] - part.bounds.minimum[axis]);
                require(std::abs(uv[i * 2 + axis] - (axis == 0 ? value : 1 - value)) < 1e-6,
                        "Curved construction lost global bounds UVs");
            }
        }
        require(source.document.at("version") == 1 &&
                    source.document.at("next_id") == source.document.at("vertices").size() +
                                                         source.document.at("faces").size() + 1,
                "Curved construction changed source format or local allocator");
        const auto encoded = source.document.dump();
        require(EditableMeshSource::parse(std::as_bytes(std::span(encoded.data(), encoded.size())))
                        .document == source.document,
                "Curved source changed on parse/reopen");
        return area;
    };
    for (const Point radii : {Point{1, 1}, Point{2, .5}})
        for (unsigned segments : {8u, 32u, 64u})
            for (double opening : {0., .5}) {
                if (opening > 0 && segments > 32)
                    continue;
                const auto source =
                    EditableMeshSource::create_planar_ellipse(asset, radii, segments, opening);
                require(source.document.at("vertices").size() == segments * (opening > 0 ? 2 : 1),
                        "Ellipse construction changed the segment/vertex budget");
                const auto expected = .5 * segments * std::sin(2 * std::numbers::pi / segments) *
                                      radii[0] * radii[1] * (1 - opening * opening);
                require(std::abs(check_projection(source) - expected) < 1e-5,
                        "Ellipse/ring differs from the independent regular-polygon area");
            }
    // Cover every admitted ring segment count: proportional24-corner loops
    // exposed a nearly zero-area virtual bridge in generic hole decomposition.
    for (unsigned segments = 8; segments <= 32; ++segments)
        for (const auto opening : {.5, .65}) {
            const auto source =
                EditableMeshSource::create_planar_ellipse(asset, {2, 1}, segments, opening);
            const auto expected =
                segments * std::sin(2 * std::numbers::pi / segments) * (1 - opening * opening);
            require(source.document.at("faces").size() == segments * 2 &&
                        std::abs(check_projection(source) - expected) < 1e-5,
                    "An admitted ellipse ring count changed its sector topology or area");
        }
    for (double radius : {0., -1., 100001., std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()})
        rejects([&] { (void)EditableMeshSource::create_planar_ellipse(asset, {radius, 1}); });
    for (unsigned segments : {0u, 7u, 65u})
        rejects([&] { (void)EditableMeshSource::create_planar_ellipse(asset, {1, 1}, segments); });
    for (double opening : {-1., 1., std::numeric_limits<double>::quiet_NaN()})
        rejects(
            [&] { (void)EditableMeshSource::create_planar_ellipse(asset, {1, 1}, 32, opening); });
    rejects([&] { (void)EditableMeshSource::create_planar_ellipse(asset, {1, 1}, 33, .5); });

    const auto k = 4 * (std::sqrt(2.) - 1) / 3;
    const std::vector<MeshCurveKnot> circle{{{1, 0}, {0, -k}, {0, k}},
                                            {{0, 1}, {k, 0}, {-k, 0}},
                                            {{-1, 0}, {0, k}, {0, -k}},
                                            {{0, -1}, {-k, 0}, {k, 0}}};
    for (bool clockwise : {false, true}) {
        auto knots = circle;
        if (clockwise) {
            std::reverse(knots.begin(), knots.end());
            for (auto& knot : knots)
                std::swap(knot.incoming, knot.outgoing);
        }
        std::size_t previous = 0;
        for (double tolerance : {.02, .01, .005}) {
            const auto source = EditableMeshSource::create_planar_curve(asset, knots, tolerance);
            const auto& vertices = source.document.at("vertices");
            require(vertices.size() >= previous && vertices.size() <= 64,
                    "Cubic quality changed nonmonotonically or exceeded the vertex budget");
            previous = vertices.size();
            require(std::abs(check_projection(source) - std::numbers::pi) < .05,
                    "Cubic circle construction lost the independent circular area");
            // Independent dense Bernstein evaluation against the saved polygon.
            for (unsigned segment = 0; segment < knots.size(); ++segment)
                for (unsigned sample = 0; sample <= 100; ++sample) {
                    const double t = sample / 100., u = 1 - t;
                    const auto& a = knots[segment];
                    const auto& b = knots[(segment + 1) % knots.size()];
                    Point p{};
                    for (unsigned axis = 0; axis < 2; ++axis)
                        p[axis] = u * u * u * a.point[axis] +
                                  3 * u * u * t * (a.point[axis] + a.outgoing[axis]) +
                                  3 * u * t * t * (b.point[axis] + b.incoming[axis]) +
                                  t * t * t * b.point[axis];
                    double nearest = std::numeric_limits<double>::infinity();
                    for (unsigned i = 0; i < vertices.size(); ++i) {
                        const auto x = vertices[i].at("position").get<std::array<double, 3>>();
                        const auto y = vertices[(i + 1) % vertices.size()]
                                           .at("position")
                                           .get<std::array<double, 3>>();
                        const auto dx = y[0] - x[0], dy = y[1] - x[1];
                        const auto projection = std::clamp(
                            ((p[0] - x[0]) * dx + (p[1] - x[1]) * dy) / (dx * dx + dy * dy), 0.,
                            1.);
                        nearest = std::min(nearest, std::hypot(p[0] - x[0] - projection * dx,
                                                               p[1] - x[1] - projection * dy));
                    }
                    require(nearest <= tolerance + 1e-12,
                            "Saved sampled polygon exceeds the requested cubic chord tolerance");
                }
        }
    }
    const std::vector<Point> l{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
    std::vector<MeshCurveKnot> linear;
    for (const auto p : l)
        linear.push_back({p, {}, {}});
    require(EditableMeshSource::create_planar_curve(asset, linear).document ==
                EditableMeshSource::create_planar_outline(asset, l).document,
            "Straight cubic controls changed the existing concave source intent");
    for (double bad : {0., -1., 100001., std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { (void)EditableMeshSource::create_planar_curve(asset, circle, bad); });
    rejects([&] { (void)EditableMeshSource::create_planar_curve(asset, circle, 1e-8); });
    rejects(
        [&] { (void)EditableMeshSource::create_planar_curve(asset, std::span(circle).first(2)); });
    auto invalid = circle;
    invalid[1].point = invalid[0].point;
    rejects([&] { (void)EditableMeshSource::create_planar_curve(asset, invalid); });
    invalid = circle;
    invalid[0].outgoing = {100001, 0};
    rejects([&] { (void)EditableMeshSource::create_planar_curve(asset, invalid); });
    invalid = {{{0, 0}, {}, {}}, {{2, 2}, {}, {}}, {{0, 2}, {}, {}}, {{2, 0}, {}, {}}};
    rejects([&] { (void)EditableMeshSource::create_planar_curve(asset, invalid); });
}
