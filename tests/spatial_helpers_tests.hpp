#pragma once
#include "asset_actions.hpp"
#include "command_workspace.hpp"
#include "spatial_helpers.hpp"
inline void test_spatial_helpers() {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    const std::string camera =
        authoring_command(scene, "entity.create", {{"recipe", "render.camera"}}).at("selected");
    const std::string light =
        authoring_command(scene, "entity.create", {{"recipe", "render.light"}}).at("selected");
    auto doc = scene.document();
    for (auto& row : doc["entities"]) {
        auto& c = row["components"];
        if (row.at("id") == camera) {
            c["forge.camera"]["far_plane"] = 4.;
            c["forge.camera"]["aspect"] = 2.;
        } else {
            c["forge.light"]["kind"] = 2;
            c["forge.light"]["range"] = 3.;
            c["forge.light"]["outer_cone"] = std::numbers::pi / 2;
        }
    }
    scene.reset(doc);
    const auto before = scene.document();
    const auto markers = ui::spatial_markers(scene.effective_document(), 800, 600, 10);
    require(markers.size() == 2, "Camera/light markers missing");
    for (const auto& m : markers) {
        require(!m.lines.empty(), "Spatial guide missing");
        for (const auto& [a, b] : m.lines)
            for (auto p : {a, b})
                for (auto x : p)
                    require(std::isfinite(x), "Valid wide spot cone produced a nonfinite guide");
        if (m.camera) {
            require(m.lines.size() == 12, "Finite camera frustum must have twelve edges");
            const auto corner = m.lines.front().second;
            require(
                std::abs(corner[2] - m.position[2] - 4) < 1e-6 &&
                    std::abs(std::abs((corner[0] - m.position[0]) / (corner[1] - m.position[1])) -
                             2) < 1e-5,
                "Camera guide disagrees with far plane/aspect");
        } else {
            for (const auto& [a, b] : m.lines)
                for (auto p : {a, b})
                    require(std::hypot(p[0] - m.position[0], p[1] - m.position[1],
                                       p[2] - m.position[2]) <= 3.00001,
                            "Spot range guide exceeds radial light range");
        }
    }
    require(scene.document() == before, "Scene helper generation mutated authored state");
    for (auto& row : doc["entities"])
        if (row.at("id") == camera) {
            row["components"]["forge.camera"]["enabled"] = false;
            row["components"]["forge.camera"]["infinite_far"] = true;
            row["components"]["forge.camera"]["basis"] = 1;
        }
    scene.reset(doc);
    const auto disabled = ui::spatial_markers(scene.effective_document(), 800, 600, 10);
    for (const auto& m : disabled)
        if (m.camera) {
            require(!m.enabled && m.lines.size() == 8,
                    "Disabled camera should remain visible; infinite far must have open rays");
            require(m.lines[0].second[2] < 0, "Imported camera guide ignored negative-Z basis");
        }
    for (auto& row : doc["entities"])
        if (row.at("id") == light)
            row["components"]["forge.node_visibility"] = {{"visible", false}};
    scene.reset(doc);
    require(ui::spatial_markers(scene.effective_document(), 800, 600, 10).size() == 1,
            "Hidden helper remained visible");
    // Structural inheritance, effective world position and overlapping component helpers.
    auto nested = before;
    for (auto& row : nested["entities"])
        if (row.at("id") == camera) {
            row["parent"] = light;
            row["components"]["forge.camera"]["projection"] = 1;
            row["components"]["forge.camera"]["orthographic_height"] = 4.;
        } else {
            row["components"]["forge.local_translation"] = {{"x", 2.}, {"y", 5.}, {"z", 1.}};
            row["components"]["forge.node_selectability"] = {{"selectable", false}};
        }
    scene.reset(nested);
    auto nested_markers = ui::spatial_markers(scene.effective_document(), 800, 600, 10);
    for (const auto& marker : nested_markers) {
        require(!marker.selectable, "Ancestor selection lock did not affect helpers");
        if (marker.camera) {
            require(marker.position == Double3{2, 6, 1}, "Helper ignored spatial parent transform");
            require(std::abs(marker.lines[0].first[0] - marker.lines[0].second[0]) < 1e-6 &&
                        std::abs(marker.lines[0].first[1] - marker.lines[0].second[1]) < 1e-6,
                    "Orthographic frustum sides are not parallel");
        }
    }
    for (auto& row : nested["entities"])
        if (row.at("id") == light)
            row["components"]["forge.node_visibility"] = {{"visible", false}};
    scene.reset(nested);
    require(ui::spatial_markers(scene.effective_document(), 800, 600, 10).empty(),
            "Hidden ancestor did not hide child helper");
    auto combined = before;
    combined["entities"][0]["components"]["forge.light"] =
        combined["entities"][1]["components"]["forge.light"];
    combined["entities"].erase(1);
    scene.reset(combined);
    const auto both = ui::spatial_markers(scene.effective_document(), 800, 600, 10);
    require(both.size() == 2 && both[0].camera != both[1].camera &&
                both[0].offset != both[1].offset,
            "Camera and light on the same entity lost a distinct helper");
    scene.reset(doc);
    ui::SpatialHelpers helpers;
    helpers.update(scene.effective_document(), scene.revision(), 800, 600);
    EditorCamera view;
    const auto position = helpers.markers().front().position;
    const auto p =
        project_point(view, {float(position[0]), float(position[1]), float(position[2])}, 800, 600);
    require(p && helpers.pick(view, {800, 600}, {(*p)[0], (*p)[1]}) == camera,
            "Meshless icon was not pickable");
    helpers.visible = false;
    require(helpers.pick(view, {800, 600}, {(*p)[0], (*p)[1]}).empty(),
            "Hidden helpers remained pickable");
}
inline void test_asset_action_routes() {
    using namespace forge;
    ui::AssetActionContext context;
    context.target = AssetRecord{AssetId::generate(), "model", "Assets/Model.glb", 1, {}};
    context.selected = {context.target->id};
    context.openable = context.placeable = context.reimportable = true;
    std::vector<std::string> calls;
    ui::AssetActionHandlers handlers;
    handlers.import_files = [&] { calls.push_back("import"); };
    handlers.open = [&](const auto& a) {
        require(a.id == context.target->id, "Open changed target");
        calls.push_back("open");
    };
    handlers.reimport = [&](const auto& ids) {
        require(ids == context.selected, "Reimport changed selection");
        calls.push_back("reimport");
    };
    handlers.place = [&](const auto& a) {
        require(a.id == context.target->id, "Placement changed target");
        calls.push_back("place");
    };
    handlers.collision = [&](const auto& a) {
        require(a.id == context.target->id && a.type == "mesh",
                "Collision creation changed target or accepted a non-Mesh");
        calls.push_back("collision");
    };
    handlers.files = [&](const auto&, auto) { calls.push_back("files"); };
    handlers.cache = [&] { calls.push_back("cache"); };
    auto actions = ui::asset_actions(context, handlers);
    for (const auto* id : {"asset.import", "asset.open", "asset.reimport", "asset.place",
                           "asset.move", "asset.duplicate", "asset.delete", "asset.cache"})
        require(actions.invoke(id), "Available asset action failed");
    require(calls.size() == 8, "Asset action routes lost an invocation");
    // Exercise the real palette input route with the production asset registry.
    ImGui::CreateContext();
    ui::style(1);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1100, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EngineContext engine;
    Scene scene(engine.world());
    ui::CommandWorkspace palette;
    palette.actions = &actions;
    std::string selected, message;
    auto frame = [&] {
        ImGui::NewFrame();
        palette.draw(scene, selected, message, 1, {0, 0, 0}, false, false);
        ImGui::Render();
    };
    auto key = [&](ImGuiKey value) {
        io.AddKeyEvent(value, true);
        frame();
        io.AddKeyEvent(value, false);
        frame();
    };
    auto search = [&](const char* label) {
        palette.open_palette();
        frame();
        frame();
        io.AddInputCharactersUTF8(label);
        frame();
        frame();
    };
    frame();
    auto exercise_palette = [&] {
        for (const auto& action : actions.entries) {
            const auto before = calls.size();
            search(action.label.c_str());
            key(ImGuiKey_Enter);
            require(calls.size() == before + (action.available ? 1 : 0),
                    "Asset palette disagrees with shared action availability");
            if (!action.available)
                key(ImGuiKey_Escape);
        }
    };
    require(!actions.invoke("asset.collision"), "Model accepted Mesh-only collision creation");
    exercise_palette();
    context.target->type = "mesh";
    actions = ui::asset_actions(context, handlers);
    exercise_palette();
    require(std::count(calls.begin(), calls.end(), "collision") == 1,
            "Mesh palette did not invoke collision creation exactly once");
    context.blocked = "Play is active";
    actions = ui::asset_actions(context, handlers);
    const auto before_blocked = calls.size();
    for (const auto& action : actions.entries) {
        search(action.label.c_str());
        key(ImGuiKey_Enter);
        require(calls.size() == before_blocked, "Disabled asset palette action executed");
        key(ImGuiKey_Escape);
    }
    ImGui::DestroyContext();
    context.blocked = "Play is active";
    actions = ui::asset_actions(context, handlers);
    for (const auto& a : actions.entries)
        require(!actions.invoke(a.id) && a.help_text().find(context.blocked) != std::string::npos,
                "Busy action missing rejection/help");
    require(calls.size() == before_blocked, "Blocked asset action changed state");
    context.blocked.clear();
    context.target.reset();
    context.selected.clear();
    context.reimportable = false;
    actions = ui::asset_actions(context, handlers);
    require(!actions.invoke("asset.place") && !actions.invoke("asset.delete") &&
                !actions.invoke("asset.reimport"),
            "Empty selection allowed an asset operation");
}
