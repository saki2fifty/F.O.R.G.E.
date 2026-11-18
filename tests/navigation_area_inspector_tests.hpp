#pragma once
void exercise_area_live(float scale, ImVec2 size, const std::filesystem::path& image = {}) {
    EngineContext engine(WorldRole::Runtime, false, {navigation_module({})});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Route preferences"}})
                           .at("selected")
                           .get<std::string>();
    for (const auto* type : {"forge.navigation_agent", "forge.navigation_area_filter"})
        authoring_command(scene, "component.add", {{"entity", actor}, {"component", type}});
    auto navigation = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    const auto ref = engine.world().reference(scene.entity(actor).id()).value();
    const auto schema = scene.schema();
    test::ImGuiSoftwareCapture capture(int(size.x), int(size.y));
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    unsigned submissions = 0;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        inspector.draw_runtime(
            scene.effective_document(), schema, {}, actor,
            [&](const auto& type, const auto& field, const auto& value) {
                check(type == "forge.navigation_area_filter",
                      "Area controls changed a different participant");
                navigation->tune(ref, type, field, value);
                ++submissions;
                return true;
            },
            false);
        ImGui::End();
        capture.render();
    };
    frame();
    frame();
    check(test::ui_targets.contains("runtime-component:forge.navigation_area_filter") &&
              test::ui_targets.contains("runtime-field:forge.navigation_area_filter:mask") &&
              test::ui_targets.contains("property-tree:costs"),
          "Live Inspector omitted the existing policy mask or fixed-array disclosure");
    edit_live_number(frame, "runtime-field:forge.navigation_area_filter:mask", "5");
    const auto costs = visible_live_item(frame, "property-tree:costs");
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(costs.minimum.x + 8, (costs.minimum.y + costs.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    check(test::ui_targets.contains("property-collection-entry:costs:0") &&
              test::ui_targets.contains("property-collection-entry:costs:15") &&
              !test::ui_targets.contains("property-collection-entry:costs:16"),
          "Native collection controls did not preserve sixteen fixed costs");
    edit_live_number(frame, "property-collection-entry:costs:2", "20");
    const auto typed = scene.entity(actor).get<NavigationAreaFilter>();
    check(submissions == 2 && typed.mask == 5 && typed.costs[2] == 20 && typed.costs[0] == 1 &&
              typed.costs[15] == 1 && navigation->agent_state(ref)->tick == 0,
          "Native mask/cost controls did not submit one bounded value without advancing Play");
    if (!image.empty()) {
        frame();
        capture.save(image);
    }
    check(scene.undo() && !scene.entity(actor).has<NavigationAreaFilter>() &&
              scene.entity(actor).has<NavigationAgent>(),
          "Live policy editing created an authored history entry");
    test::observe_ui = false;
}
