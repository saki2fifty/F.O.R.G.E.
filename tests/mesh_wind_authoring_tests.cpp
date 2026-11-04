#include "mesh_scatter_authoring.hpp"
#include "render_values.hpp"
#include "runtime_package.hpp"
#include "runtime_property_tune.hpp"
#include "standalone_manifest.hpp"
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/prefab_authoring.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
const Json& renderer(const Json& doc, const std::string& id) {
    for (const auto& row : doc.at("entities"))
        if (row.at("id") == id)
            return row.at("components").at("forge.mesh_renderer");
    throw std::runtime_error("Missing renderer");
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto id = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
        const auto original = scene.document();
        bool metadata = false;
        const auto schema = scene.schema();
        for (const auto& type : schema.at("components"))
            if (type.at("id") == "forge.mesh_renderer")
                for (const auto& field : type.at("fields"))
                    if (field.at("id") == "wind") {
                        check(field.at("type") == "struct" && field.contains("description"),
                              "Wind does not expose its reflected Inspector/MCP value structure");
                        for (const auto& member : field.at("fields"))
                            if (member.at("id") == "amplitude") {
                                metadata = member.at("minimum") == 0 &&
                                           member.at("maximum") == 100 && member.at("unit") == "m";
                            }
                    }
        check(metadata, "Wind Inspector/MCP schema lost native range or unit metadata");
        check(renderer(original, id).at("wind") == detail::render_value(MeshWind{}) &&
                  scene.entity(id).get<MeshRenderer>().wind == MeshWind{},
              "Default wind did not retain disabled typed values");
        auto legacy = original;
        for (auto& row : legacy["entities"])
            row["components"]["forge.mesh_renderer"].erase("wind");
        scene.reset(legacy);
        check(scene.document() == original,
              "Missing legacy wind did not retain disabled/default intent");
        MeshWind configured;
        configured.enabled = true;
        configured.amplitude = .4;
        configured.direction_x = 2;
        configured.direction_z = -3;
        configured.phase = 37;
        const auto values = detail::render_value(configured);
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.mesh_renderer"},
                           {"field", "wind"},
                           {"value", values}});
        const auto changed = scene.document();
        check(scene.entity(id).get<MeshRenderer>().wind == configured &&
                  renderer(changed, id).at("wind") == values,
              "Copied/native renderer wind values disagree");
        check(scene.undo() && scene.document() == original && scene.redo() &&
                  scene.document() == changed,
              "Wind edit bypassed ordinary history");
        for (unsigned mode = 0; mode < 5; ++mode) {
            auto bad = values;
            if (mode == 0)
                bad["amplitude"] = 101;
            if (mode == 1)
                bad["height"] = 0;
            if (mode == 2)
                bad["frequency"] = true;
            if (mode == 3)
                bad["direction_x"] = bad["direction_z"] = 0;
            if (mode == 4)
                bad = Json::array();
            bool rejected = false;
            try {
                authoring_command(scene, "property.set",
                                  {{"entity", id},
                                   {"component", "forge.mesh_renderer"},
                                   {"field", "wind"},
                                   {"value", bad}});
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && scene.document() == changed,
                  "Invalid wind edit changed authored intent");
        }
        detail::MeshScatterSettings layout;
        layout.count = 4;
        layout.radius = 2;
        layout.spacing = 0;
        const auto placed =
            authoring_command(scene, "mesh.scatter",
                              detail::mesh_scatter_arguments(id, layout, "Wind render values"));
        const auto after = scene.document();
        {
            EngineContext runtime_engine(WorldRole::Runtime);
            Scene live(runtime_engine.world());
            live.restore_snapshot(after);
            const auto revision = live.revision();
            auto tuned = values;
            tuned["amplitude"] = .7;
            tuned["phase"] = -90.;
            check(RuntimePropertyTune::mesh_renderer_property("wind") &&
                      live.tune_runtime_property(id, "forge.mesh_renderer", "wind", tuned) ==
                          tuned &&
                      live.entity(id).get<MeshRenderer>().wind.amplitude == .7 &&
                      live.revision() > revision && !live.undo() && !live.redo() &&
                      scene.document() == after,
                  "Live wind tuning changed authored intent/history or bypassed native values");
            const auto retained = live.entity(id).get<MeshRenderer>();
            const auto tuned_revision = live.revision();
            tuned["height"] = -1;
            bool rejected = false;
            try {
                (void)live.tune_runtime_property(id, "forge.mesh_renderer", "wind", tuned);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && live.entity(id).get<MeshRenderer>() == retained &&
                      live.revision() == tuned_revision,
                  "Invalid live wind changed the retained renderer/history");
            live.restore_snapshot(after);
            check(live.entity(id).get<MeshRenderer>().wind == configured,
                  "Restoring the authored world retained transient wind tuning");
        }
        for (const auto& child : placed.at("created"))
            check(renderer(after, child.get<std::string>()).at("wind") == values,
                  "Foliage renderer copying lost wind intent");
        check(scene.undo() && scene.document() == changed && scene.redo() &&
                  scene.document() == after,
              "Wind-bearing foliage did not use one ordinary history entry");
        const auto prefab = create_prefab_source(scene, id);
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto instance = instantiate_prefab(scene, prefab.asset());
        authoring_command(scene, "property.set",
                          {{"entity", instance},
                           {"component", "forge.mesh_renderer"},
                           {"field", "wind"},
                           {"value", detail::render_value(MeshWind{})}});
        check(renderer(scene.effective_document(), instance).at("wind").at("enabled") == false,
              "Explicit disabled wind did not override the prefab's enabled value");
        const auto snapshot = scene.snapshot();
        EngineContext separate;
        Scene reloaded(separate.world());
        reloaded.restore_snapshot(snapshot);
        check(renderer(reloaded.effective_document(), instance).at("wind").at("enabled") == false,
              "Prefab wind default override was lost on snapshot restoration");
        Scene restored(engine.world());
        restored.reset(after);
        check(restored.document() == after,
              "Wind intent did not round trip through ordinary Scene values");
        {
            const auto root = std::filesystem::temp_directory_path() /
                              ("forge-wind-package-" + AssetId::generate().str());
            struct Cleanup {
                std::filesystem::path root;
                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(root, error);
                }
            } cleanup{root};
            const auto source = root / "source", package = root / "package",
                       relocated = root / "relocated";
            std::filesystem::create_directories(source);
            restored.save(source / "wind.scene.json");
            AssetCatalog catalog(source);
            catalog.add({restored.asset_id(), "scene", "wind.scene.json",
                         after.at("version").get<unsigned>()});
            catalog.save(AssetCatalog::project_index(source));
            const std::array roots{restored.asset_id()};
            const RuntimePackageTarget target{"linux", "none"};
            package_runtime_content(source, package, roots, target, {}, {}, schema);
            std::filesystem::rename(source, root / "hidden-source");
            std::filesystem::rename(package, relocated);
            const auto packaged = open_runtime_content(relocated, target);
            check(packaged.records()
                              .at(restored.asset_id())
                              .metadata.at("forge.runtime_document")
                              .at("runtime_features")
                              .at("mesh_wind") == mesh_wind_profile_version &&
                      renderer(load_game_scene(relocated, {restored.asset_id()}), id).at("wind") ==
                          values,
                  "Relocated source-hidden runtime package lost wind values or required profile");
            validate_runtime_compatibility(packaged,
                                           {{"runtime_mesh_wind", mesh_wind_profile_version}});
            bool rejected = false;
            try {
                validate_runtime_compatibility(packaged, Json::object());
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, "Wind package accepted a runtime kit without its geometry profile");
        }
        std::cout << "Wind default/legacy/native values, atomic edits, scatter, history and Scene "
                     "round trip passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
