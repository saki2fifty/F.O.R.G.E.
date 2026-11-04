#include "fog_volume_geometry.hpp"
#include "fog_volume_view_tests.hpp"
#include "render_values.hpp"
#include "runtime_property_tune.hpp"
#include <cmath>
#include <forge/render_scene.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class Operation>
void rejects(Operation operation, const std::string& label = "Invalid Fog Volume admitted") {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, label.c_str());
}
void near(double actual, double expected, double tolerance = 1e-6) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          "Fog Volume canonical coordinates differ");
}
void geometry() {
    FogVolume volume;
    volume.half_extent_x = 2;
    volume.half_extent_y = 4;
    volume.half_extent_z = 8;
    AffineTransform world;
    world.m[3] = 4;
    world.m[7] = 6;
    world.m[11] = 11;
    const auto sample = prepare_fog_volume(volume, world, {1, 2, 3});
    near(sample.rows[0][0], .5);
    near(sample.rows[1][1], .25);
    near(sample.rows[2][2], .125);
    near(sample.rows[0][3], -1.5);
    near(sample.rows[1][3], -1);
    near(sample.rows[2][3], -1);
    check(sample.rows[3] == std::array<float, 4>{.5f, .5f, .5f, .15f} && sample.rows[4][0] == 0,
          "Fog Volume color/density/shape packing changed");
    volume.shape = std::uint32_t(FogVolumeShape::Sphere);
    const auto sphere = prepare_fog_volume(volume, world, {1, 2, 3});
    check(sphere.rows[4][0] == 1 && sphere.rows[0] == sample.rows[0],
          "Sphere lost the shared ellipsoid basis");
    auto shifted = world;
    constexpr double origin = 1e12;
    shifted.m[3] += origin;
    shifted.m[7] -= origin;
    shifted.m[11] += origin;
    check(prepare_fog_volume(volume, shifted, {1 + origin, 2 - origin, 3 + origin}) == sphere,
          "Camera-relative preparation lost a representable large-origin offset");
    // A reflected, sheared, nonuniform basis maps all eight local corners.
    world.m = {-2, .5, .2, 4, 0, 3, .4, 6, 0, 0, 1.5, 11};
    const Double3 camera{1, 2, 3};
    const auto changed = prepare_fog_volume(volume, world, camera);
    for (double x : {-1., 1.})
        for (double y : {-1., 1.})
            for (double z : {-1., 1.}) {
                const auto point = world.point({2 * x, 4 * y, 8 * z});
                const Double3 expected{x, y, z};
                for (unsigned row = 0; row < 3; ++row) {
                    double actual = changed.rows[row][3];
                    for (unsigned column = 0; column < 3; ++column)
                        actual += changed.rows[row][column] * (point[column] - camera[column]);
                    near(actual, expected[row], 2e-6);
                }
            }
}
void validation() {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const double invalid : {nan, -1., 0., .0001, 1000001.}) {
        FogVolume value;
        value.half_extent_x = invalid;
        rejects([&] { value.validate(); });
    }
    for (float invalid : {std::numeric_limits<float>::infinity(), -1.f, 65505.f}) {
        FogVolume value;
        value.color_g = invalid;
        rejects([&] { value.validate(); });
    }
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), -1.f, 1001.f}) {
        FogVolume value;
        value.density = invalid;
        rejects([&] { value.validate(); });
    }
    FogVolume value;
    value.shape = 2;
    rejects([&] { value.validate(); });
    value = {};
    auto transform = AffineTransform{};
    transform.m[0] = 0;
    rejects([&] { prepare_fog_volume(value, transform, {}); });
    transform.m[0] = nan;
    rejects([&] { prepare_fog_volume(value, transform, {}); });
    transform = {};
    rejects([&] { prepare_fog_volume(value, transform, {nan, 0, 0}); });
    rejects([&] { prepare_fog_volume(value, transform, {fog_volume_max_view_offset + 1, 0, 0}); });
    for (double scale : {1e-10, 1e10}) {
        transform.m[0] = scale;
        rejects([&] { prepare_fog_volume(value, transform, {}); });
    }
    value.enabled = false;
    value.density = 0;
    value.validate();
}
void packing() {
    const auto empty = fog_volume_uniform({});
    check(empty == FogVolumeUniform{}, "Empty volume fallback is not neutral");
    FogVolume volume;
    std::array<FogVolumeSample, fog_volume_limit + 1> samples;
    samples.fill(prepare_fog_volume(volume, {}, {}));
    rejects([&] { (void)fog_volume_uniform(samples); });
    const auto data = fog_volume_uniform(std::span(samples).first(fog_volume_limit));
    check(data[0] == std::array<float, 4>{16, 0, 0, 0}, "Volume count metadata is inconsistent");
    for (unsigned source = 0; source < fog_volume_limit; ++source)
        for (unsigned row = 0; row < 5; ++row)
            check(data[1 + source * 5 + row] == samples[source].rows[row],
                  "Volume packing changed its source or crossed a row boundary");
    const Double3 forward{0, 1, 0}, invalid{0, 0, 2};
    check(fog_volume_uniform({}, &forward)[0] == std::array<float, 4>{0, 0, 1, 0},
          "Orthographic ray direction was not packed");
    rejects([&] { (void)fog_volume_uniform({}, &invalid); });
    for (const auto [row, column, value] :
         {std::array<float, 3>{0, 0, std::numeric_limits<float>::quiet_NaN()},
          std::array<float, 3>{3, 0, -1}, std::array<float, 3>{3, 3, 1001},
          std::array<float, 3>{4, 0, 1.5}}) {
        auto bad = samples[0];
        bad.rows[unsigned(row)][unsigned(column)] = value;
        rejects([&] { (void)fog_volume_uniform(std::span(&bad, 1)); });
    }
}
void authoring_and_extraction() {
    WorldContext world;
    Scene scene(world);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "fog"}, {"name", "Fog"}, {"components", Json::object()}}})}});
    scene.entity("fog").set<LocalTranslation>({0, 0, 3}).set<Primitive>({4});
    const auto original = scene.document();
    auto document = original;
    document["entities"][0]["components"]["forge.fog_volume"] = detail::render_value(FogVolume{});
    scene.edit(document);
    check(scene.entity("fog").get<FogVolume>() == FogVolume{},
          "Fog authoring did not create the native value");
    const auto saved = scene.document();
    check(scene.undo() && scene.document() == original && scene.redo() && scene.document() == saved,
          "Fog authoring bypassed Scene history");
    for (const auto& [field, value] :
         std::vector<std::pair<const char*, Json>>{{"shape", -1},
                                                   {"shape", 2},
                                                   {"shape", std::uint64_t{1} << 32},
                                                   {"shape", true},
                                                   {"shape", .5},
                                                   {"density", -1},
                                                   {"density", 1001},
                                                   {"density", "dense"},
                                                   {"color_r", 65504.001},
                                                   {"half_extent_y", .0001},
                                                   {"half_extent_z", 1000001},
                                                   {"layers", std::uint64_t{1} << 32},
                                                   {"layers", false},
                                                   {"priority", std::int64_t{INT32_MAX} + 1},
                                                   {"enabled", 1}}) {
        auto bad = saved;
        bad["entities"][0]["components"]["forge.fog_volume"][field] = value;
        rejects([&] { scene.edit(bad); },
                std::string("Invalid Fog field admitted: ") + field + "=" + value.dump());
        check(scene.document() == saved, "Rejected Fog values changed source/history");
    }
    const auto schema = scene.schema();
    bool found = false;
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.fog_volume") {
            found = true;
            check(type.at("optional") && type.at("category") == "Rendering" &&
                      type.at("fields").size() == 11,
                  "Fog type discovery is incomplete");
        }
    check(found, "Fog type is missing from the shared schema");
    auto frame = scene.effective_document();
    check(extract_render_scene(frame).fog_volumes.size() == 1,
          "Active Fog source was not extracted");
    frame["entities"][0]["state_active"] = false;
    check(extract_render_scene(frame).fog_volumes.empty(),
          "Inactive Fog source affected presentation");
    frame = scene.effective_document();
    frame["entities"][0]["components"]["forge.node_visibility"] = {{"visible", false}};
    check(extract_render_scene(frame).fog_volumes.empty(),
          "Hidden Fog source affected presentation");
    frame = scene.effective_document();
    frame["entities"][0]["components"]["forge.fog_volume"]["density"] = -1;
    const auto invalid_frame = extract_render_scene(frame);
    check(invalid_frame.fog_volumes.empty() && invalid_frame.diagnostics.size() == 1 &&
              invalid_frame.diagnostics[0].context.property == "forge.fog_volume",
          "Invalid copied Fog source did not produce a scoped diagnostic");
    WorldContext loaded_world;
    Scene restored(loaded_world);
    restored.restore_snapshot(scene.snapshot());
    check(restored.document() == saved && restored.entity("fog").get<FogVolume>() == FogVolume{},
          "Fog snapshot changed values or durable source identity");
    WorldContext runtime_world(WorldRole::Runtime);
    Scene runtime(runtime_world);
    runtime.restore_snapshot(scene.snapshot());
    check(RuntimePropertyTune::fog_property("density") &&
              runtime.tune_runtime_property("fog", "forge.fog_volume", "density", .5) == .5 &&
              runtime.entity("fog").get<FogVolume>().density == .5 && scene.document() == saved,
          "Live Fog tuning changed authored values or missed the native component");
    const auto live = runtime.document();
    rejects([&] { runtime.tune_runtime_property("fog", "forge.fog_volume", "density", -1); });
    rejects([&] { runtime.tune_runtime_property("fog", "forge.fog_volume", "shape", 2); });
    rejects([&] { runtime.tune_runtime_property("fog", "forge.fog_volume", "unlisted", 1); });
    check(runtime.document() == live, "Rejected live Fog value changed runtime state");
}
} // namespace
int main() {
    try {
        geometry();
        validation();
        packing();
        authoring_and_extraction();
        test_fog_view::exercise();
        std::cout << "Fog Volume value and camera-relative GPU geometry preparation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
