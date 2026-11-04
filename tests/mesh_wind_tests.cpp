#include "mesh_draw_shader.hpp"
#include "mesh_pick.hpp"
#include "mesh_wind.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid wind input was accepted");
}
double dot(Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Double3 subtract(Double3 a, Double3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
void values_and_phase() {
    MeshWind value;
    validate_mesh_wind(value);
    const AffineTransform identity;
    check(!sample_mesh_wind(value, identity, 0).enabled, "Default wind is not off");
    value.enabled = true;
    const auto a = sample_mesh_wind(value, identity, .5),
               b = sample_mesh_wind(value, identity, 2.5);
    check(std::abs(a.displacement[0] + .2) < 1e-15 &&
              std::abs(a.displacement[0] - b.displacement[0]) < 1e-15 && a.displacement[1] == 0,
          "Wind time phase does not follow its independent cycle oracle");
    auto shifted = identity;
    shifted.m[3] = value.wavelength;
    check(std::abs(sample_mesh_wind(value, shifted, .5).displacement[0] - a.displacement[0]) <
              1e-15,
          "One spatial wavelength changed the wind phase");
    shifted.m[3] = value.wavelength / 4;
    check(std::abs(sample_mesh_wind(value, shifted, 0).displacement[0] - .2) < 1e-15,
          "Quarter spatial phase is wrong");
    value.direction_x = std::numeric_limits<double>::denorm_min();
    check(sample_mesh_wind(value, identity, .5).displacement == a.displacement,
          "Subnormal direction lost its angle");
    value.direction_x = 0;
    rejects([&] { validate_mesh_wind(value); });
    value.amplitude = 0;
    validate_mesh_wind(value);
    check(!sample_mesh_wind(value, identity, .5).enabled, "Zero amplitude retained deformation");
    value = MeshWind{};
    value.enabled = true;
    for (auto member :
         {&MeshWind::amplitude, &MeshWind::height, &MeshWind::direction_x, &MeshWind::direction_z,
          &MeshWind::frequency, &MeshWind::wavelength, &MeshWind::phase}) {
        auto bad = value;
        bad.*member = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { validate_mesh_wind(bad); });
        bad.*member = std::numeric_limits<double>::infinity();
        rejects([&] { validate_mesh_wind(bad); });
    }
    for (auto member :
         {&MeshWind::amplitude, &MeshWind::height, &MeshWind::frequency, &MeshWind::wavelength}) {
        auto bad = value;
        bad.*member = -1;
        rejects([&] { validate_mesh_wind(bad); });
        bad.*member = 1e9;
        rejects([&] { validate_mesh_wind(bad); });
    }
    rejects([&] { sample_mesh_wind(value, identity, -1); });
    rejects([&] { sample_mesh_wind(value, identity, 1e9 + 1); });
    shifted.m[3] = 1e9 + 1;
    rejects([&] { sample_mesh_wind(value, shifted, 0); });
}
void geometry_and_bounds() {
    MeshWind value;
    value.enabled = true;
    value.direction_x = 3;
    value.direction_z = 4;
    value.phase = 90;
    AffineTransform world;
    world.m[3] = 0;
    world.m[7] = 5;
    world.m[11] = 0;
    const auto sample = sample_mesh_wind(value, world, 0);
    check(std::abs(sample.displacement[0] - .12) < 1e-15 &&
              std::abs(sample.displacement[1] - .16) < 1e-15,
          "Horizontal direction was not normalized");
    const Double3 root{0, 5, 0}, tip{0, 5 + value.height, 0};
    check(wind_point(sample, root) == root && wind_point(sample, {0, 4, 0}) == Double3{0, 4, 0},
          "Wind moved pinned roots or positions below them");
    const auto top = wind_point(sample, tip);
    check(std::abs(top[0] - .12) < 1e-15 && std::abs(top[2] - .16) < 1e-15 && top[1] == tip[1],
          "Wind tip displacement is wrong");
    const Double3 point{.2, 5 + value.height * .4, -.3}, vector{.3, .6, -.7}, normal{1, .2, .6};
    const double epsilon = 1e-5;
    auto plus = point, minus = point;
    for (unsigned axis = 0; axis < 3; ++axis) {
        plus[axis] += epsilon * vector[axis];
        minus[axis] -= epsilon * vector[axis];
    }
    auto finite_difference = subtract(wind_point(sample, plus), wind_point(sample, minus));
    const auto tangent = wind_vector(sample, point, vector),
               covector = wind_normal(sample, point, normal);
    for (unsigned axis = 0; axis < 3; ++axis)
        check(std::abs(finite_difference[axis] / (2 * epsilon) - tangent[axis]) < 1e-9,
              "Analytic wind tangent differs from independent finite differences");
    check(std::abs(dot(tangent, covector) - dot(vector, normal)) < 1e-14,
          "Wind normal is not the inverse transpose of its tangent map");
    const auto ex = wind_vector(sample, point, {1, 0, 0}),
               ey = wind_vector(sample, point, {0, 1, 0}),
               ez = wind_vector(sample, point, {0, 0, 1});
    const auto determinant = ex[0] * (ey[1] * ez[2] - ey[2] * ez[1]) -
                             ey[0] * (ex[1] * ez[2] - ex[2] * ez[1]) +
                             ez[0] * (ex[1] * ey[2] - ex[2] * ey[1]);
    check(determinant == 1, "Wind shear changed volume orientation");
    const RenderBounds base{{-1, 4, -2}, {1, 8, 2}};
    for (const Double3 camera : {Double3{0, 0, 0}, Double3{1e9, 1e9, -1e9}, Double3{.3, 5.2, .7}}) {
        const auto bounds = wind_bounds(sample, base, camera);
        for (unsigned y = 0; y <= 32; ++y)
            for (unsigned x = 0; x <= 8; ++x)
                for (unsigned z = 0; z <= 8; ++z) {
                    const Double3 input{-1 + x * .25, 4 + y * .125, -2 + z * .5};
                    const auto deformed = wind_point(sample, input);
                    // Independent float shader arithmetic in camera-relative world space.
                    float fx = float(input[0] - camera[0]), fy = float(input[1] - camera[1]),
                          fz = float(input[2] - camera[2]);
                    const float h = std::min(1.f, std::max(0.f, float(input[1] - sample.base_y) *
                                                                    float(sample.inverse_height)));
                    fx += float(sample.displacement[0]) * h * h;
                    fz += float(sample.displacement[1]) * h * h;
                    const Double3 gpu{double(fx) + camera[0], double(fy) + camera[1],
                                      double(fz) + camera[2]};
                    for (unsigned axis = 0; axis < 3; ++axis)
                        check(deformed[axis] >= bounds.minimum[axis] &&
                                  deformed[axis] <= bounds.maximum[axis] &&
                                  gpu[axis] >= bounds.minimum[axis] &&
                                  gpu[axis] <= bounds.maximum[axis],
                              "CPU or float-shader wind escaped conservative bounds");
                }
    }
    MeshWindSample disabled;
    check(wind_bounds(disabled, base, {0, 0, 0}) == base && wind_point(disabled, point) == point &&
              wind_vector(disabled, point, vector) == vector &&
              wind_normal(disabled, point, normal) == normal,
          "Disabled wind changed geometry or bounds");
    auto invalid = base;
    invalid.minimum[0] = 2;
    rejects([&] { wind_bounds(sample, invalid, {0, 0, 0}); });
    invalid = base;
    invalid.maximum[0] = std::numeric_limits<double>::max();
    rejects([&] { wind_bounds(sample, invalid, {0, 0, 0}); });
}
void instance_bounds_and_picking() {
    MeshResourceData resource;
    MeshPart part;
    part.vertices = 3;
    part.indices = {0, 1, 2};
    const std::array<Double3, 3> vertices{{{-.35, 0, 0}, {.35, 0, 0}, {0, 1.6, 0}}};
    part.streams = {{"POSITION", 3, std::vector<float>{-.35f, 0, 0, .35f, 0, 0, 0, 1.6f, 0}}};
    part.bounds = mesh_bounds(part);
    resource.mesh.lods = {{1, {part}}};
    const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
    RenderMesh mesh;
    mesh.renderer.wind.enabled = true;
    mesh.renderer.wind.amplitude = .9;
    mesh.renderer.wind.phase = 90;
    Camera lens;
    lens.projection = 1;
    lens.orthographic_height = 4;
    lens.near_plane = .1;
    lens.far_plane = 30;
    unsigned changed_hits = 0, checked_hits = 0;
    for (unsigned mode = 0; mode < 4; ++mode)
        for (const double base : {0., 1e9 - 100})
            for (const double seconds : {0., .25, .75}) {
                mesh.world = {};
                mesh.world.m[3] = mesh.world.m[7] = base;
                mesh.world.m[11] = base + 8;
                if (mode == 1) {
                    // Tilt, signed/nonuniform scale and shear require no inverse.
                    mesh.world.m[0] = -1.3;
                    mesh.world.m[1] = .3;
                    mesh.world.m[4] = .2;
                    mesh.world.m[5] = .8;
                    mesh.world.m[6] = .15;
                    mesh.world.m[9] = .4;
                } else if (mode == 2)
                    mesh.world.m[5] = 0;
                else if (mode == 3)
                    mesh.world.m[0] = 0;
                AffineTransform camera;
                camera.m[3] = camera.m[11] = base;
                camera.m[7] = base + .6;
                const auto view = camera_view(lens, camera, 160, 100);
                const auto pose = prepare_mesh_instance_pose(resource, geometry, mesh,
                                                             ModelSceneIndex(RenderScene{}),
                                                             mesh_pose_payload_limit, seconds);
                check(pose.wind.enabled && !pose.skinned && pose.morph_weights.empty(),
                      "Static instance lost its admitted wind sample");
                const auto bounds = mesh_instance_bounds(pose, view.position);
                auto baked = part;
                auto& positions = std::get<std::vector<float>>(baked.streams[0].values);
                for (unsigned i = 0; i < vertices.size(); ++i) {
                    // Bake an independent displaced triangle, keeping translation
                    // separate so distant origins cannot hide a picking error.
                    Double3 local{};
                    for (unsigned axis = 0; axis < 3; ++axis)
                        local[axis] =
                            std::get<std::vector<float>>(part.streams[0].values)[3 * i + axis];
                    auto point = mesh.world.vector(local);
                    const auto h = std::clamp(point[1] / mesh.renderer.wind.height, 0., 1.);
                    point[0] += pose.wind.displacement[0] * h * h;
                    point[2] += pose.wind.displacement[1] * h * h;
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        positions[3 * i + axis] = float(point[axis]);
                        const auto world = point[axis] + mesh.world.m[4 * axis + 3];
                        check(world >= bounds.minimum[axis] && world <= bounds.maximum[axis],
                              "Admitted wind vertex escaped instance bounds");
                    }
                }
                MeshInstancePose baked_pose;
                baked_pose.world.m[3] = mesh.world.m[3];
                baked_pose.world.m[7] = mesh.world.m[7];
                baked_pose.world.m[11] = mesh.world.m[11];
                auto disabled = pose;
                disabled.wind = {};
                for (unsigned y = 15; y < 90; y += 10)
                    for (unsigned x = 20; x < 145; x += 12) {
                        MeshPickBudget a, b, c;
                        const auto hit = pick_mesh_part(part, pose.lods[0][0], pose, view, x, y, a),
                                   expected = pick_mesh_part(baked, {}, baked_pose, view, x, y, b),
                                   off = pick_mesh_part(part, disabled.lods[0][0], disabled, view,
                                                        x, y, c);
                        check(bool(hit) == bool(expected) &&
                                  (!hit || std::abs(*hit - *expected) < 1e-7),
                              "Wind picking differs from independently baked triangles");
                        checked_hits += bool(hit);
                        changed_hits += bool(hit) != bool(off);
                    }
                const auto retained = pose;
                rejects([&] {
                    (void)prepare_mesh_instance_pose(resource, geometry, mesh,
                                                     ModelSceneIndex(RenderScene{}),
                                                     mesh_pose_bytes(pose) - 1, seconds);
                });
                check(pose == retained, "Wind payload rejection mutated an admitted pose");
            }
    check(checked_hits > 20 && changed_hits > 10,
          "Wind pick oracle did not exercise enough changed surface intersections");
    resource.mesh.morph_defaults = {0};
    resource.mesh.morph_names = {"Offset"};
    resource.mesh.lods[0].parts[0].morph_targets = {{{"POSITION", 3, std::vector<float>(9)}}};
    const auto morph_geometry = prepare_mesh_pose_geometry(resource.mesh);
    rejects([&] {
        (void)prepare_mesh_instance_pose(resource, morph_geometry, mesh,
                                         ModelSceneIndex(RenderScene{}));
    });
    mesh.renderer.wind.enabled = false;
    check(
        !prepare_mesh_instance_pose(resource, morph_geometry, mesh, ModelSceneIndex(RenderScene{}))
             .wind.enabled,
        "Disabled wind rejected ordinary morph geometry");
}
void shared_geometry_sources() {
    MeshVertexFetch fetch;
    fetch.triangles = true;
    const auto ordinary = mesh_geometry_shader(fetch);
    check(ordinary.vertex.find("ForgeWind") == std::string::npos &&
              ordinary.vertex.find("ATTRIB7") == std::string::npos,
          "Disabled wind added shader inputs or arithmetic");
    fetch.wind = true;
    const auto wind = mesh_geometry_shader(fetch);
    check(wind.instanced && wind.vertex.find("Wind:ATTRIB7") != std::string::npos &&
              wind.vertex.find("PreviousWind") == std::string::npos &&
              wind.vertex.find("ForgeWindFrame(frame,windHeight,instance.Wind)") !=
                  std::string::npos,
          "Static wind geometry omitted its instance input or normal/tangent frame");
    fetch.temporal = true;
    const auto temporal = mesh_geometry_shader(fetch);
    check(temporal.vertex.find("Wind:ATTRIB11") != std::string::npos &&
              temporal.vertex.find("PreviousWind:ATTRIB12") != std::string::npos &&
              temporal.vertex.find(
                  "previous.xz+=ForgeWindOffset(previousWindHeight,instance.PreviousWind)") !=
                  std::string::npos,
          "Wind motion lost previous geometry correspondence");
    rejects([&] { (void)mesh_geometry_shader(fetch, false); });
    rejects([&] { (void)mesh_geometry_shader(fetch, true, true); });
    fetch.skin = true;
    rejects([&] { (void)mesh_geometry_shader(fetch); });
    fetch.skin = false;
    fetch.morph_count = 1;
    rejects([&] { (void)mesh_geometry_shader(fetch); });
}
} // namespace
int main() {
    try {
        values_and_phase();
        geometry_and_bounds();
        instance_bounds_and_picking();
        shared_geometry_sources();
        std::cout
            << "Wind admission, independent phase/Jacobian and conservative float bounds passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
