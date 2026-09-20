#pragma once
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_components.hpp>
#include <forge/scene.hpp>
inline void test_mesh_component() {
    using namespace forge;
    auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    EngineContext engine;
    Scene scene(engine.world());
    Json renderer = Json::object();
    const auto schema = scene.schema();
    for (const auto& component : schema.at("components"))
        if (component.at("id") == "forge.mesh_renderer")
            for (const auto& field : component.at("fields"))
                renderer[field.at("id").get<std::string>()] = field.at("default");
    const auto id = EntityId::generate().str();
    const auto mesh = AssetId::generate(), paint = AssetId::generate(), metal = AssetId::generate();
    renderer["mesh"] = mesh;
    renderer["materials"] = Json::array(
        {{{"slot", "paint"}, {"material", paint}, {"future", {{"source", "paint-only"}}}},
         {{"slot", "metal"}, {"material", metal}, {"future", nullptr}}});
    renderer["future_component"] = {{"owner", "opaque"}};
    Json doc{{"version", 4},
             {"asset_id", AssetId::generate()},
             {"entities", Json::array({{{"id", id},
                                        {"name", "Mesh"},
                                        {"components", {{"forge.mesh_renderer", renderer}}}}})}};
    scene.reset(doc);
    require(scene.document() == doc, "Mesh component nested unknown roundtrip changed data");
    const auto handle = scene.entity(id);
    const auto& native = handle.get<MeshRenderer>();
    require(native.mesh.id == mesh && native.materials.size() == 2 &&
                native.materials[0].material.id == paint &&
                native.materials[1].material.id == metal,
            "Mesh renderer is not stored as typed Flecs data");
    auto changed = native;
    std::reverse(changed.materials.begin(), changed.materials.end());
    changed.visible = false;
    handle.set<MeshRenderer>(changed);
    auto read = scene.document();
    const auto& slots = read["entities"][0]["components"]["forge.mesh_renderer"]["materials"];
    require(slots[0].at("slot") == "metal" && slots[0].at("future").is_null() &&
                slots[1].at("future").at("source") == "paint-only" &&
                !read["entities"][0]["components"]["forge.mesh_renderer"]["visible"].get<bool>(),
            "Native ECS edits used stale shadow state or retargeted unknown fields");
    scene.reset(doc);
    auto edited = doc;
    edited["entities"][0]["components"]["forge.mesh_renderer"]["materials"][0]["material"] =
        nullptr;
    authoring_command(
        scene, "property.set",
        {{"entity", id},
         {"component", "forge.mesh_renderer"},
         {"field", "materials"},
         {"value", edited["entities"][0]["components"]["forge.mesh_renderer"]["materials"]}});
    require(scene.document() == edited &&
                !scene.entity(id).get<MeshRenderer>().materials[0].material.id,
            "Explicit default mesh material did not commit");
    scene.undo();
    require(scene.document() == doc, "Mesh material undo lost extension/identity");
    scene.redo();
    require(scene.document() == edited, "Mesh material redo lost extension/identity");
    const auto before = scene.document();
    auto invalid = before;
    invalid["entities"][0]["components"]["forge.mesh_renderer"]["materials"][1]["slot"] = "paint";
    bool refused = false;
    try {
        scene.edit(invalid);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused && scene.document() == before, "Invalid material binding changed scene");
    scene.undo();
    require(scene.document() == doc, "Rejected material binding polluted scene history");
    // Collection intent is one top-level property, including equal-value edits.
    scene.reset(doc);
    const auto prefab = create_prefab_source(scene, id);
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto structured = instantiate_prefab(scene, prefab.asset());
    auto slots_value = renderer.at("materials");
    authoring_command(scene, "property.set",
                      {{"entity", structured},
                       {"component", "forge.mesh_renderer"},
                       {"field", "materials"},
                       {"value", slots_value}});
    auto instance_row = [&](const Json& document) -> Json {
        for (const auto& row : document.at("entities"))
            if (row.at("id") == structured)
                return row;
        throw std::runtime_error("Structured mesh instance disappeared");
    };
    require(instance_row(scene.document())
                    .at("property_overrides")
                    .at("forge.mesh_renderer")
                    .at("materials") == slots_value,
            "Equal-value material list override intent was lost");
    auto source_revision = prefab.source;
    source_revision["revision"] = 2u;
    auto& source_renderer = source_revision["members"][0]["components"]["forge.mesh_renderer"];
    source_renderer["visible"] = false;
    source_renderer["materials"][0]["material"] = metal;
    scene.publish_prefab_sources({{prefab.asset(), source_revision}}, [] {});
    const auto& owned_renderer = scene.entity(structured).get<MeshRenderer>();
    require(!owned_renderer.visible && owned_renderer.materials[0].material.id == paint &&
                instance_row(scene.document())
                        .at("property_overrides")
                        .at("forge.mesh_renderer")
                        .at("materials") == slots_value,
            "Source publish erased material list intent or blocked another property");
    authoring_command(
        scene, "property.revert",
        {{"entity", structured}, {"component", "forge.mesh_renderer"}, {"field", "materials"}});
    require(!scene.entity(structured).owns<MeshRenderer>() &&
                scene.entity(structured).get<MeshRenderer>().materials[0].material.id == metal,
            "Material Revert did not resume prefab inheritance");
    scene.undo();
    require(scene.entity(structured).get<MeshRenderer>().materials[0].material.id == paint,
            "Material Revert undo lost explicit intent");
    const auto before_failed_source = scene.document();
    auto invalid_source = source_revision;
    invalid_source["revision"] = 3u;
    invalid_source["members"][0]["components"]["forge.mesh_renderer"]["materials"][1]["slot"] =
        "paint";
    bool wrote = false;
    refused = false;
    try {
        scene.publish_prefab_sources({{prefab.asset(), invalid_source}}, [&] { wrote = true; });
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused && !wrote && scene.document() == before_failed_source,
            "Invalid prefab material candidate changed published source/instances");
    // Legacy native IsA ownership uses the same component; scalar/collection
    // property intent for structured prefabs is exercised by authoring tests.
    auto inheritance = doc;
    inheritance["entities"][0]["prefab"] = true;
    const auto instance = EntityId::generate().str();
    inheritance["entities"].push_back(
        {{"id", instance}, {"name", "Instance"}, {"base", id}, {"components", Json::object()}});
    scene.reset(inheritance);
    require(scene.entity(instance).has<MeshRenderer>() &&
                !scene.entity(instance).owns<MeshRenderer>() &&
                scene.entity(instance).get<MeshRenderer>().mesh.id == mesh,
            "MeshRenderer did not use native prefab inheritance");
    auto source_change = scene.entity(id).get<MeshRenderer>();
    source_change.visible = false;
    scene.entity(id).set<MeshRenderer>(source_change);
    require(!scene.entity(instance).get<MeshRenderer>().visible,
            "Inherited mesh source edit did not propagate");
}
