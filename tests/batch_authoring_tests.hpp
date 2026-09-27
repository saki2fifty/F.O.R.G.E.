#pragma once
#include "batch_inspector.hpp"
#include "transform_gesture.hpp"
inline void test_batch_authoring() {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    const std::string parent = authoring_command(scene, "entity.create").at("selected");
    const std::string child = authoring_command(scene, "entity.create").at("selected");
    const std::string other = authoring_command(scene, "entity.create").at("selected");
    scene.reparent_entity(child, parent);
    ui::EditorSelection selected;
    selected.select_entity(parent);
    selected.click_entity(child, true);
    require(selected.entities() == std::vector<std::string>{parent, child},
            "Additive selection lost primary order");
    selected.click_entity(other, false, true, {parent, child, other});
    require(selected.entities() == std::vector<std::string>{child, other},
            "Range selection lost anchor");
    selected.click_entity(other, true);
    require(selected.entity() == child, "Toggling primary did not choose survivor");
    selected.entity_slot() = parent;
    require(selected.entities() == std::vector<std::string>{parent},
            "Legacy single selection retained stale targets");
    selected.select_entities({child, parent, other});
    const auto ids = selected.entities();
    require(selection_roots(scene.effective_document(), ids, true) ==
                std::vector<std::string>{parent, other},
            "Spatial selection double-targets a descendant");
    const auto original = scene.document();
    const auto child_position = *entity_position(scene.effective_document(), child);
    MoveGesture move;
    require(move.begin(scene, parent, 0, ids), "Batch move did not begin");
    move.update({2, 0, 0}, false, 1);
    require(move.commit(scene), "Batch move failed");
    const auto after = *entity_position(scene.effective_document(), child);
    require(std::abs(after[0] - child_position[0] - 2) < .0001f, "Parent and child moved twice");
    require(scene.undo() && scene.document() == original, "Batch move was not one Undo step");
    require(scene.redo(), "Batch move redo failed");
    scene.undo();
    authoring_command(scene, "transform.binding",
                      {{"entity", child}, {"spatial", {{"mode", "world"}}}});
    require(selection_roots(scene.effective_document(), ids, true).size() == 3,
            "World-bound child incorrectly omitted from transform");
    authoring_command(scene, "transform.binding",
                      {{"entity", other},
                       {"spatial", {{"mode", "explicit"}, {"target", scene.reference(parent)}}}});
    require(selection_roots(scene.effective_document(), ids, true) ==
                std::vector<std::string>{child, parent},
            "Explicit spatial ancestry ignored");
    const auto before = scene.document();
    const auto revision = scene.revision();
    auto invalid = selection_commands(scene, {parent, "missing"}, "transform.scale",
                                      {{"value", {{"x", 2}, {"y", 2}, {"z", 2}}}});
    bool rejected = false;
    try {
        apply_authoring(scene, invalid, revision);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && scene.document() == before && scene.revision() == revision,
            "Invalid batch partially mutated scene");
    TransformGesture scale;
    require(!scale.begin(scene, "", TransformGesture::Mode::Scale, {0, 0, 1}),
            "Empty selection activated scale");
    require(
        !scale.begin(scene, parent, TransformGesture::Mode::Scale, {0, 0, 1}, {parent, "missing"}),
        "Stale selection activated scale");
    require(scale.begin(scene, parent, TransformGesture::Mode::Scale, {0, 0, 1}, {parent, child}),
            "Batch scale did not begin");
    require(scale.update(2) && scale.accept(scene), "Batch scale failed");
    require(scene.undo() && scene.document() == before, "Batch scale was not one Undo step");
    const auto count = before.at("entities").size();
    apply_authoring(scene, selection_commands(scene, {child, parent}, "entity.duplicate"),
                    scene.revision());
    require(scene.document().at("entities").size() == count + 2,
            "Duplicate copied selected child twice");
    require(scene.undo() && scene.document() == before, "Duplicate batch Undo failed");
    apply_authoring(scene, selection_commands(scene, {child, parent}, "entity.delete"),
                    scene.revision());
    require(scene.document().at("entities").size() == count - 2, "Batch subtree deletion failed");
    selected.reconcile(scene.document());
    require(selected.entities() == std::vector<std::string>{other},
            "Deleted selection was not pruned");
    require(scene.undo() && scene.document() == before, "Delete batch Undo failed");
    // Inherited channels must remain inherited when another channel is edited.
    scene.reset(migrate_scene(Json::parse(R"({"version":1,"entities":[
        {"id":"base","name":"Base","prefab":true,"components":{
            "forge.position":{"x":0,"y":0,"z":0},
            "forge.rotation":{"x":0,"y":0,"z":0},
            "forge.scale":{"x":1,"y":1,"z":1}}},
        {"id":"one","name":"One","base":"base","components":{}},
        {"id":"two","name":"Two","base":"base","components":{}}
    ]})")));
    std::vector<std::string> instances;
    for (const auto& row : Json(scene.document().at("entities")))
        if (row.at("name") != "Base")
            instances.push_back(row.at("id"));
    const auto inherited = scene.document();
    require(move.begin(scene, instances.front(), 0, instances),
            "Inherited batch move failed to begin");
    move.update({1, 0, 0}, false, 1);
    move.commit(scene);
    for (const auto& row : Json(scene.document().at("entities")))
        if (row.at("name") != "Base") {
            const auto& c = row.at("components");
            require(c.contains("forge.local_translation") && !c.contains("forge.local_rotation") &&
                        !c.contains("forge.local_scale"),
                    "Move materialized unrelated prefab overrides");
        }
    require(scene.undo() && scene.document() == inherited, "Inherited move Undo lost ownership");
    TransformGesture rotate;
    require(rotate.begin(scene, instances.front(), TransformGesture::Mode::Rotate, {0, 1, 0},
                         instances),
            "Inherited rotation failed to begin");
    require(rotate.update(30) && rotate.accept(scene), "Inherited batch rotation failed");
    for (const auto& row : Json(scene.document().at("entities")))
        if (row.at("name") != "Base") {
            const auto& c = row.at("components");
            require(c.contains("forge.local_rotation") && !c.contains("forge.local_translation") &&
                        !c.contains("forge.local_scale"),
                    "Rotate materialized unrelated prefab overrides");
        }
    require(scene.undo() && scene.document() == inherited, "Inherited rotation Undo failed");
    require(
        scale.begin(scene, instances.front(), TransformGesture::Mode::Scale, {0, 0, 1}, instances),
        "Inherited scale failed to begin");
    require(scale.update(2) && scale.accept(scene), "Inherited batch scale failed");
    for (const auto& row : Json(scene.document().at("entities")))
        if (row.at("name") != "Base") {
            const auto& c = row.at("components");
            require(c.contains("forge.local_scale") && !c.contains("forge.local_translation") &&
                        !c.contains("forge.local_rotation"),
                    "Scale materialized unrelated prefab overrides");
        }
    require(scene.undo() && scene.document() == inherited, "Inherited scale Undo failed");
    selected.select_entities(instances);
    selected.select_asset(AssetId::generate());
    require(selected.entities().empty(), "Asset selection retained multi-entity targets");

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1200, 900};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    ImGui::Begin("Batch Inspector");
    ui::batch_inspector(scene, std::filesystem::current_path(), instances);
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    require(scene.document() == inherited, "Drawing mixed Inspector changed authored ownership");
}
