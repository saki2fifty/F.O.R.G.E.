#pragma once
#include "navigation_destination_input_tests.hpp"
#include <fstream>
#include <iterator>
#include <limits>
// Actual saved nav assets and ImGui drawing; no replacement overlay renderer.
inline void check_navigation_destinations(const std::filesystem::path& root,
                                          const std::filesystem::path& worker,
                                          forge::NavigationPlane plane) {
    using namespace forge;
    SceneDocument::create_project(root, "Destination guides");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const bool xy = plane == NavigationPlane::PlanarXY;
    const auto surface = authoring_command(scene, "entity.create", {{"kind", xy ? 4 : 0}})
                             .at("selected")
                             .get<std::string>();
    if (xy)
        scene.entity(surface).set<NavigationRegion2D>({});
    else {
        scene.entity(surface).set<LocalScale>({20, 1, 20});
        scene.entity(surface).set<LocalTranslation>({0, -.5, 0});
        scene.entity(surface).set<NavigationSurface>({});
    }
    NavigationTools tools(engine.world(), worker);
    NavigationSettings settings;
    settings.plane = plane;
    project.save();
    tools.build(scene, project, settings);
    std::string message;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (tools.pending()) {
        tools.poll(scene, project, false, message);
        check(std::chrono::steady_clock::now() < deadline, "Destination fixture bake timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(message.find("failed") == std::string::npos, "Destination fixture bake failed");
    const auto catalog = AssetCatalog::open_project(root);
    AssetId mesh;
    for (const auto& [id, record] : catalog.records())
        if (record.type == NavMeshAsset::type)
            mesh = id;
    check(bool(mesh), "Destination fixture has no saved navmesh");
    const auto actor = authoring_command(scene, "entity.create", {{"kind", 4}, {"name", "Agent"}})
                           .at("selected")
                           .get<std::string>();
    scene.entity(actor).set<LocalTranslation>({1, 0, xy ? 7. : 1.});
    NavigationAgent agent;
    agent.navmesh = {mesh};
    agent.has_destination = true;
    agent.destination_x = 3;
    agent.destination_y = xy ? 2 : .25;
    agent.destination_z = xy ? 99 : -2;
    scene.entity(actor).set<NavigationAgent>(agent);
    project.save();
    const auto saved = scene.document();
    const auto source = scene.effective_document();
    const auto saved_bytes = [&] {
        std::ifstream in(project.path(), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }();
    for (const float scale : {1.f, 1.5f, 2.f})
        for (const ImVec2 display : {ImVec2{1200, 900}, ImVec2{420, 640}}) {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = display;
            io.DeltaTime = 1.f / 60;
            unsigned char* pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            ui::style(scale);
            test::observe_ui = true;
            EditorCamera camera;
            if (xy) {
                camera.align(2, 1);
                camera.target = {0, 0, 7};
            } else
                camera.pitch = -.7f;
            camera.distance = 20;
            const ImVec2 origin{10, 140}, size{display.x - 20, display.y - 150};
            const auto frame = [&](const Json& doc, const std::string& selection) {
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(display, ImGuiCond_Always);
                ImGui::Begin("Navigation destination");
                tools.overlay_control();
                tools.draw(doc, camera, origin, size, selection);
                ImGui::End();
                ImGui::Render();
            };
            const auto shown = [&](const Json& doc, bool enabled = true) {
                frame(doc, actor);
                frame(doc, actor);
                check(test::ui_targets.contains("navigation:requested-destination"),
                      "Selected saved-asset destination guide is missing");
                const auto marker = test::ui_targets.at("navigation:requested-destination");
                const auto& rows = doc.at("entities");
                const auto row = std::find_if(rows.begin(), rows.end(),
                                              [&](const auto& e) { return e.at("id") == actor; });
                const auto& data = row->at("components").at("forge.navigation_agent");
                const auto expected = project_point(
                    camera,
                    {data.at("destination_x").template get<float>(),
                     data.at("destination_y").template get<float>() + (xy ? 0.f : .02f),
                     xy ? row->at("world_affine").at(11).template get<float>() + .02f
                        : data.at("destination_z").template get<float>()},
                    size.x, size.y);
                check(expected &&
                          std::abs((marker.minimum.x + marker.maximum.x) * .5f - origin.x -
                                   (*expected)[0]) < .01f &&
                          std::abs((marker.minimum.y + marker.maximum.y) * .5f - origin.y -
                                   (*expected)[1]) < .01f &&
                          marker.enabled == enabled,
                      "Destination guide lost world coordinates, XY visual depth or enabled state");
                const auto label = test::ui_targets.at("navigation:requested-destination-label");
                check(label.minimum.x >= origin.x && label.minimum.y >= origin.y &&
                          label.maximum.x <= origin.x + size.x &&
                          label.maximum.y <= origin.y + size.y,
                      "Destination caption clips at a supported scale or narrow viewport");
                const auto* output = ImGui::GetDrawData();
                bool colored_geometry = false;
                const auto color =
                    enabled ? IM_COL32(105, 225, 245, 255) : IM_COL32(155, 160, 170, 230);
                for (int i = 0; i < output->CmdListsCount; ++i)
                    for (const auto& vertex : output->CmdLists[i]->VtxBuffer)
                        colored_geometry |= vertex.col == color;
                check(colored_geometry,
                      "Destination observation did not produce actual ImGui geometry");
            };
            shown(source);
            auto disabled = source;
            for (auto& row : disabled["entities"])
                if (row["id"] == actor)
                    row["components"]["forge.navigation_agent"]["enabled"] = false;
            shown(disabled, false);
            for (unsigned invalid = 0; invalid < 9; ++invalid) {
                auto doc = source;
                for (auto& row : doc["entities"])
                    if (row["id"] == actor) {
                        if (invalid == 0)
                            row["components"]["forge.navigation_agent"]["has_destination"] = false;
                        if (invalid == 1)
                            row["components"]["forge.navigation_agent"]["navmesh"] =
                                AssetId::generate();
                        if (invalid == 2)
                            row["components"]["forge.node_visibility"] = {{"visible", false}};
                        if (invalid == 3)
                            row["spatial_resolved"] = false;
                        if (invalid == 4)
                            row["prefab"] = true;
                        if (invalid == 5)
                            row["components"]["forge.navigation_agent"]["destination_x"] = "bad";
                        if (invalid == 6)
                            row["parent"] = surface;
                        if (invalid == 7)
                            row["world_affine"][11] = std::numeric_limits<double>::quiet_NaN();
                    }
                if (invalid == 6)
                    for (auto& row : doc["entities"])
                        if (row["id"] == surface)
                            row["components"]["forge.node_visibility"] = {{"visible", false}};
                if (invalid == 8)
                    doc["asset_id"] = AssetId::generate();
                frame(doc, actor);
                check(!test::ui_targets.contains("navigation:requested-destination"),
                      "Invalid or hidden destination invented a guide");
            }
            frame(source, surface);
            check(!test::ui_targets.contains("navigation:requested-destination"),
                  "Unselected agent retained a destination guide");
            // Real checkbox input, not a direct test-only view-state setter.
            frame(source, actor);
            const auto box = test::ui_targets.at("navigation:overlay");
            const auto click = [&] {
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f);
                frame(source, actor);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                frame(source, actor);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                frame(source, actor);
            };
            click();
            check(!tools.overlay_visible() &&
                      !test::ui_targets.contains("navigation:requested-destination"),
                  "Overlay toggle retained the destination guide");
            click();
            shown(source);
            authoring_command(scene, "property.set",
                              {{"entity", actor},
                               {"component", "forge.navigation_agent"},
                               {"field", "destination_x"},
                               {"value", 2}});
            shown(scene.effective_document());
            check(scene.undo() && scene.document() == saved,
                  "Destination edit Undo changed source ownership");
            shown(scene.effective_document());
            check(AssetCatalog::open_project(root).document() == catalog.document(),
                  "Drawing a destination republished navigation");
            check_destination_input(tools, scene, project, actor, surface, camera, origin, size);
            test::observe_ui = false;
            ImGui::DestroyContext();
        }
    std::ifstream in(project.path(), std::ios::binary);
    check(std::string(std::istreambuf_iterator<char>(in), {}) == saved_bytes,
          "Destination drawing or view toggle wrote the saved scene");
    project.open_project(root);
    check(scene.document() == saved, "Destination authored values failed Save/reopen");
    const auto target = tools.requested_destination(scene.effective_document(), actor);
    NavigationDestinationGesture persistence;
    check(target && persistence.begin(scene, *target, -1) &&
              persistence.update({.4, -.3, .7}, false, 1) && persistence.commit(scene),
          "Destination persistence fixture did not commit");
    const auto authored = scene.document();
    project.save();
    project.open_project(root);
    check(scene.document() == authored, "Committed destination gesture failed normal Save/reopen");
    std::cout << (xy ? "XY" : "3D")
              << " requested destination, actual toggle, scale/narrow caption, field/Undo and "
                 "Save/reopen passed\n";
}
