#pragma once
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
inline void test_node_policy() {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(empty_scene());
    auto create = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto root = create("Visibility root"), middle = create("Intermediary"),
               child = create("Independent spatial child");
    auto add = [&](const std::string& entity, const char* component) {
        authoring_command(scene, "component.add", {{"entity", entity}, {"component", component}});
    };
    auto set = [&](const std::string& entity, const char* component, const char* field,
                   Json value) {
        authoring_command(scene, "property.set",
                          {{"entity", entity},
                           {"component", component},
                           {"field", field},
                           {"value", std::move(value)}});
    };
    // These must be optional in the actual shared Inspector/API catalog; direct
    // recipe composition alone did not prove that they were discoverable/editable.
    for (const auto* component : {"forge.camera", "forge.light", "forge.mesh_renderer",
                                  "forge.node_visibility", "forge.node_selectability"})
        add(child, component);
    set(child, "forge.mesh_renderer", "mesh", engine_primitive(0).id);
    add(root, "forge.node_visibility");
    add(root, "forge.node_selectability");
    auto doc = scene.document();
    doc["entities"][1]["parent"] = root;
    doc["entities"][2]["parent"] = middle;
    doc["entities"][2]["spatial"] = {{"mode", "world"}};
    doc["entities"][0]["components"]["forge.node_visibility"]["future_metadata"] = "preserve";
    scene.edit(doc);
    set(root, "forge.node_visibility", "visible", false);
    const auto hidden = scene.document();
    auto policy = extract_node_policies(scene.effective_document());
    const auto key = EntityId::parse(child);
    check(!policy.entities.at(key).visible && policy.entities.at(key).selectable &&
              policy.entities.at(key).valid && policy.diagnostics.empty(),
          "World spatial binding broke structural visibility or also suppressed selection");
    auto render = extract_render_scene(scene.effective_document());
    check(render.cameras.size() == 1 && render.lights.empty() && render.meshes.size() == 1 &&
              !render.meshes[0].renderer.visible && render.meshes[0].selectable,
          "Hidden subtree disabled cameras, retained lights or lost selectable geometry");
    check(scene.undo() && scene.entity(root).get<NodeVisibility>().visible,
          "Visibility Undo did not restore the authored flag");
    check(scene.redo() && scene.document() == hidden, "Visibility Redo changed authored intent");
    set(root, "forge.node_selectability", "selectable", false);
    check(!extract_node_policies(scene.effective_document()).entities.at(key).selectable,
          "Unselectable ancestor did not suppress a locally selectable descendant");
    set(root, "forge.node_visibility", "visible", true);
    render = extract_render_scene(scene.effective_document());
    check(render.cameras.size() == 1 && render.lights.size() == 1 &&
              render.meshes[0].renderer.visible && !render.meshes[0].selectable,
          "Selectability changed visible geometry or lights");
    const auto roundtrip = scene.document();
    EngineContext copy_engine;
    Scene copy(copy_engine.world());
    copy.reset(roundtrip);
    check(copy.document() == roundtrip, "Node policy round trip discarded unknown data");
    const auto before = scene.document();
    const auto revision = scene.revision();
    bool rejected = false;
    try {
        set(root, "forge.node_visibility", "visible", 0);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.document() == before && scene.revision() == revision,
          "Non-boolean visibility mutated the scene/history");
    // Explicit spatial ancestry also does not control either policy.
    auto explicit_snapshot = scene.effective_document();
    explicit_snapshot["entities"][2]["spatial"] = {{"mode", "explicit"}, {"parent", nullptr}};
    check(!extract_node_policies(explicit_snapshot).entities.at(key).selectable,
          "Explicit spatial binding bypassed structural selectability");
    auto invalid = explicit_snapshot;
    invalid["entities"][0]["parent"] = child;
    auto cyclic = extract_node_policies(invalid);
    check(!cyclic.entities.at(key).valid && !cyclic.entities.at(key).selectable &&
              cyclic.diagnostics.size() == 1,
          "Cyclic producer ancestry was not diagnosed safely");
    invalid = explicit_snapshot;
    invalid["entities"][1]["parent"] = EntityId::generate();
    check(!extract_node_policies(invalid).entities.at(key).valid,
          "Missing producer ancestry silently became a root");
    invalid = explicit_snapshot;
    invalid["entities"][0]["components"]["forge.node_visibility"]["visible"] = "false";
    const auto bad = extract_node_policies(invalid);
    check(!bad.entities.at(key).valid && bad.diagnostics.size() == 1 &&
              bad.diagnostics[0].context.entity == EntityId::parse(root),
          "Malformed producer policy lacked an affected-entity diagnostic");
    // Native IsA ownership remains separate from structural inheritance.
    const auto prefab = create_prefab_source(scene, root);
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    check(!scene.entity(instance).owns<NodeVisibility>() &&
              !scene.entity(instance).owns<NodeSelectability>(),
          "Node policy was materialized as a prefab override");
    set(instance, "forge.node_visibility", "visible", true); // Explicit equal-value intent.
    auto changed = prefab.source;
    changed["revision"] = 2u;
    for (auto& member : changed["members"])
        if (member.at("id") == changed.at("root"))
            member["components"]["forge.node_visibility"]["visible"] = false;
    scene.publish_prefab_sources({{prefab.asset(), changed}}, [] {});
    check(scene.entity(instance).get<NodeVisibility>().visible,
          "Source publication overwrote an explicit equal-value visibility override");
    authoring_command(
        scene, "property.revert",
        {{"entity", instance}, {"component", "forge.node_visibility"}, {"field", "visible"}});
    check(!scene.entity(instance).get<NodeVisibility>().visible,
          "Revert did not resume prefab visibility inheritance");
    check(scene.undo() && scene.entity(instance).get<NodeVisibility>().visible,
          "Visibility Revert was not one scene Undo step");
}
