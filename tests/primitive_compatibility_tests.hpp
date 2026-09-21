#pragma once
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/geometry.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
inline void test_primitive_compatibility() {
    using namespace forge;
    auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto legacy =
        authoring_command(scene, "entity.create", {{"kind", 17}}).at("selected").get<std::string>();
    authoring_command(scene, "appearance.color",
                      {{"entity", legacy}, {"value", {{"r", .7}, {"g", .3}, {"b", .1}}}});
    auto original = scene.document();
    original["entities"][0]["components"]["forge.tint"]["future"] = "retained";
    scene.reset(original);
    const auto prefab = create_prefab_source(scene, legacy);
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    authoring_command(scene, "transform.position",
                      {{"entity", instance}, {"value", {{"x", 3}, {"y", 0}, {"z", 0}}}});
    const auto authored = scene.document();
    const auto revision = scene.revision();
    const auto rendered = extract_render_scene(scene.effective_document());
    require(rendered.meshes.size() == 2 && rendered.diagnostics.empty(),
            "Legacy prefab/scene did not project into shared mesh rendering");
    for (const auto& mesh : rendered.meshes)
        require(mesh.renderer.mesh == engine_primitive(17) && mesh.legacy_tint &&
                    std::abs((*mesh.legacy_tint)[0] - .7f) < 1e-6f &&
                    mesh.renderer.materials[0].material ==
                        engine_material(EngineMaterial::LegacyBlockout),
                "Legacy shape/tint was changed during rendering");
    require(scene.document() == authored && scene.revision() == revision &&
                !scene.entity(instance).owns<Tint>() && !scene.entity(instance).owns<Primitive>(),
            "Compatibility projection dirtied scene or materialized prefab appearance overrides");
    const auto path = std::filesystem::current_path() /
                      ("primitive-roundtrip-" + AssetId::generate().str() + ".json");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};
    scene.save(path);
    scene.load(path);
    require(scene.document() == authored && !scene.entity(instance).owns<Tint>(),
            "Legacy prefab roundtrip rewrote authored state or inherited tint");
    auto changed = prefab.source;
    changed["revision"] = 2;
    changed["members"][0]["components"]["forge.tint"]["g"] = .8;
    scene.publish_prefab_sources({{prefab.asset(), changed}}, [] {});
    require(std::abs(scene.entity(instance).get<Tint>().g - .8f) < 1e-6f &&
                scene.entity(instance).owns<LocalTranslation>() &&
                !scene.entity(instance).owns<Primitive>(),
            "Legacy prefab update stopped propagating through compatibility rendering");

    const auto fresh = authoring_command(scene, "entity.create", {{"recipe", "primitive.1"}})
                           .at("selected")
                           .get<std::string>();
    const auto fresh_doc = scene.document();
    const auto fresh_revision = scene.revision();
    for (const char* op : {"appearance.shape", "appearance.color"}) {
        bool rejected = false;
        try {
            auto args = Json{{"entity", fresh}};
            if (std::string_view(op) == "appearance.shape")
                args["kind"] = 0;
            else
                args["value"] = {{"r", 1}, {"g", 0}, {"b", 0}};
            authoring_command(scene, op, args);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && scene.document() == fresh_doc && scene.revision() == fresh_revision,
                "Ignored legacy appearance command changed MeshRenderer scene/history");
    }
    scene.save(path);
    scene.load(path);
    require(scene.document() == fresh_doc && scene.entity(fresh).has<MeshRenderer>() &&
                !scene.entity(fresh).has<Primitive>() && !scene.entity(fresh).has<Tint>(),
            "New primitive recipe roundtrip acquired legacy appearance authority");
    auto effective = scene.effective_document();
    auto row = *std::find_if(effective["entities"].begin(), effective["entities"].end(),
                             [&](const auto& e) { return e.at("id") == fresh; });
    require(primitive_kind(row) == 1, "Built-in mesh geometry query lost sphere identity");
    row["components"]["forge.primitive"] = {{"kind", 0}};
    row["components"]["forge.mesh_renderer"]["enabled"] = false;
    require(primitive_kind(row) == no_primitive, "Disabled MeshRenderer revealed legacy cube");
    row["components"]["forge.mesh_renderer"]["enabled"] = true;
    row["components"]["forge.mesh_renderer"]["mesh"] = AssetId::generate();
    require(primitive_kind(row) == no_primitive, "Imported mesh was approximated as legacy cube");
}
