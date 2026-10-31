#include "asset_reference_impact.hpp"
#include "reflection_probe_volume.hpp"
#include "render_values.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
#include <limits>
#include <vector>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void volumes() {
    ReflectionProbe probe;
    probe.half_extent_x = 2;
    probe.half_extent_y = 3;
    probe.half_extent_z = 4;
    AffineTransform transform;
    transform.m[0] = 2;
    transform.m[5] = -3;
    transform.m[10] = .5;
    transform.m[3] = 1000000000000.25;
    transform.m[7] = -.125;
    transform.m[11] = 8;
    auto volume = reflection_probe_volume(probe, transform);
    check(volume.center == Double3{1000000000000.25, -.125, 8} &&
              volume.planes[0] == std::array<double, 4>{1, 0, 0, 4} &&
              volume.planes[1] == std::array<double, 4>{0, -1, 0, 9} &&
              volume.planes[2] == std::array<double, 4>{0, 0, 1, 2},
          "Probe volume lost mirrored scale, physical half distances or double origin");
    check(reflection_probe_view_center(volume, {1000000000000., 0, 8}) ==
              std::array<float, 3>{.25f, -.125f, 0},
          "Probe camera-relative origin narrowed before double subtraction");
    bool distant_rejected = false;
    try {
        (void)reflection_probe_view_center(volume, {-1e100, 0, 0});
    } catch (const std::exception&) {
        distant_rejected = true;
    }
    check(distant_rejected, "Unrepresentable probe view center entered GPU packing");
    transform = {};
    transform.m[1] = .5;
    volume = reflection_probe_volume(probe, transform);
    check(std::abs(volume.planes[0][0] - 1 / std::sqrt(1.25)) < 1e-12 &&
              std::abs(volume.planes[0][1] + .5 / std::sqrt(1.25)) < 1e-12 &&
              std::abs(volume.planes[0][3] - 2 / std::sqrt(1.25)) < 1e-12,
          "Probe shear volume did not preserve world plane distances");
    probe.rotation_degrees = 90;
    volume = reflection_probe_volume(probe, transform);
    check(std::abs(volume.map_rotation[0]) < 1e-12 && std::abs(volume.map_rotation[1] - 1) < 1e-12,
          "Probe map yaw followed volume shape instead of its own angle");
    probe.rotation_degrees = std::numeric_limits<double>::max();
    volume = reflection_probe_volume(probe, transform);
    check(std::isfinite(volume.map_rotation[0]) && std::isfinite(volume.map_rotation[1]),
          "Finite probe yaw overflowed during unit conversion");
    for (unsigned bad = 0; bad < 3; ++bad) {
        transform = {};
        if (bad == 0)
            transform.m[0] = 0;
        if (bad == 1)
            transform.m[0] = 1e-10;
        if (bad == 2)
            transform.m[3] = std::numeric_limits<double>::infinity();
        bool rejected = false;
        try {
            (void)reflection_probe_volume(probe, transform);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid probe transform entered prepared geometry");
    }
}
void exercise(const std::filesystem::path& directory) {
    EngineContext engine;
    Scene scene(engine.world());
    bool found = false, texture = false, extent = false, diffuse = false;
    const auto schema = scene.schema();
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.reflection_probe") {
            found = type.at("optional") && type.at("fields").size() == 11;
            for (const auto& field : type.at("fields")) {
                if (field.at("id") == "texture")
                    texture = field.at("type") == "asset_ref" &&
                              field.at("asset_type") == TextureAsset::type;
                if (field.at("id") == "diffuse_intensity")
                    diffuse = field.at("default") == 0 && field.at("minimum") == 0 &&
                              field.at("maximum") == 1000000 &&
                              field.at("display_name") == "Diffuse intensity";
                if (field.at("id") == "half_extent_x")
                    extent = field.at("minimum") == .0001 && field.at("maximum") == 1000000;
            }
        }
    check(found && texture && extent && diffuse,
          "Probe missing typed optional schema or precise bounds");
    const std::string id =
        authoring_command(scene, "entity.create", {{"name", "Probe"}, {"kind", 4}}).at("selected");
    const auto before = scene.document();
    authoring_command(scene, "component.add",
                      {{"entity", id}, {"component", "forge.reflection_probe"}});
    check(scene.entity(id).get<ReflectionProbe>() == ReflectionProbe{} && scene.undo() &&
              scene.document() == before && scene.redo(),
          "Probe composition bypassed native values or history");
    const auto set = [&](const char* field, Json value) {
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.reflection_probe"},
                           {"field", field},
                           {"value", std::move(value)}});
    };
    const auto image = AssetId::generate();
    set("texture", image);
    set("half_extent_x", .0001);
    set("intensity", 2.);
    set("diffuse_intensity", .75);
    set("rotation_degrees", -45.);
    set("priority", INT32_MIN);
    const auto saved = scene.document();
    const auto value = scene.entity(id).get<ReflectionProbe>();
    check(value.texture.id == image && value.half_extent_x == .0001 && value.intensity == 2 &&
              value.rotation_degrees == -45 && value.priority == INT32_MIN &&
              value.diffuse_intensity == .75,
          "Probe typed fields did not retain their values");
    for (const auto& [field, bad] :
         std::vector<std::pair<const char*, Json>>{{"half_extent_x", .000099999999},
                                                   {"half_extent_z", 1000000.0001},
                                                   {"intensity", -1},
                                                   {"diffuse_intensity", -1},
                                                   {"diffuse_intensity", 1000000.1},
                                                   {"diffuse_intensity", true},
                                                   {"diffuse_intensity", "1"},
                                                   {"blend_distance", -1},
                                                   {"priority", 2147483648LL},
                                                   {"enabled", 1}}) {
        bool rejected = false;
        try {
            set(field, bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == saved,
              "Invalid probe mutation changed authored state");
    }
    auto frame = extract_render_scene(scene.preview_document(saved));
    check(frame.diagnostics.empty() && frame.reflection_probes.size() == 1 &&
              frame.reflection_probes[0].entity == EntityId::parse(id) &&
              frame.reflection_probes[0].probe == value,
          "Probe detached presentation lost identity, type or fields");
    for (unsigned zero = 0; zero < 3; ++zero) {
        if (zero == 0)
            set("enabled", false);
        if (zero == 1) {
            set("intensity", 0.);
            check(extract_render_scene(scene.preview_document(scene.document()))
                          .reflection_probes.size() == 1,
                  "Diffuse-only probe was dropped with zero specular intensity");
            set("diffuse_intensity", 0.);
        }
        if (zero == 2)
            set("texture", nullptr);
        check(extract_render_scene(scene.preview_document(scene.document()))
                  .reflection_probes.empty(),
              "Inactive probe entered presentation");
        check(scene.undo() && (zero != 1 || scene.undo()) && scene.document() == saved,
              "Probe zero edit did not Undo cleanly");
    }
    const std::array documents{AssetReferenceDocument{"Probe.scene.json", saved}};
    const auto references = collect_asset_references(scene.schema(), documents);
    check(references.uninspected.empty() && references.references.size() == 1 &&
              references.references[0].target == image &&
              references.references[0].expected_type == TextureAsset::type &&
              references.references[0].source == "Probe.scene.json",
          "Probe reference inspection lost typed image identity or native component knowledge");
    std::filesystem::create_directories(directory);
    scene.save(directory / "probe.scene.json");
    Scene reopened(engine.world());
    reopened.load(directory / "probe.scene.json");
    check(reopened.document() == saved, "Probe Save/reopen changed values or identities");
    auto invalid = value;
    invalid.enabled = false;
    invalid.intensity = std::numeric_limits<double>::infinity();
    bool rejected = false;
    try {
        detail::validate_reflection_probe(invalid);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Disabled native probe admitted nonfinite values");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected probe scratch directory");
        volumes();
        exercise(std::filesystem::absolute(argv[1]));
        std::cout << "Probe native schema, fields, history, bounds, extraction and Save PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
