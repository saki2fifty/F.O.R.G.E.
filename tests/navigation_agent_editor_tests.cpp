#include "component_inspector.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(float scale, ImVec2 size) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Agent"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.navigation_agent"}});
    const auto saved = scene.document();
    const auto schema = scene.schema();
    auto effective = scene.effective_document();
    Json* entity = nullptr;
    for (auto& e : effective.at("entities"))
        if (e.at("id") == actor)
            entity = &e;
    check(entity != nullptr, "Missing agent fixture");
    check(!ComponentInspector::has_runtime_inspection(effective, schema, actor),
          "Unsampled authored Nav component enabled live Inspector");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    ComponentInspector inspector;
    unsigned submissions = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Inspector");
        inspector.draw_runtime(
            effective, schema, {}, actor,
            [&](const auto&, const auto&, const auto&) {
                ++submissions;
                return true;
            },
            false);
        ImGui::End();
        ImGui::Render();
    };
    for (const char* phase :
         {"Pending", "Moving", "Arrived", "Paused", "Blocked", "Failed", "Idle", "Disabled"}) {
        const bool sampled =
            std::string_view(phase) == "Moving" || std::string_view(phase) == "Arrived" ||
            std::string_view(phase) == "Paused" || std::string_view(phase) == "Blocked";
        (*entity)["navigation_debug"] = {{"phase", phase},
                                         {"tick", 42},
                                         {"status", "stale navmesh"},
                                         {"sampled", sampled},
                                         {"has_path", sampled},
                                         {"remaining_distance", 1234.567},
                                         {"velocity", Double3{1, 2, 3}}};
        check(ComponentInspector::has_runtime_inspection(effective, schema, actor),
              "Editor visibility gate hid navigation-only progress");
        frame();
        frame();
        check(test::ui_targets.contains("navigation_agent:phase") &&
                  test::ui_targets.contains("navigation_agent:tick"),
              "Navigation-only entity did not expose readonly progress");
        check(test::ui_targets.contains("navigation_agent:remaining") == sampled &&
                  test::ui_targets.contains("navigation_agent:speed") == sampled,
              "Unprocessed/failed agent exposed invented metrics");
        check(test::ui_targets.contains("navigation_agent:failure") ==
                  (std::string_view(phase) == "Failed"),
              "Failure reason visibility wrong");
        for (const auto& [name, box] : test::ui_targets)
            if (name.starts_with("navigation_agent:"))
                check(box.minimum.x >= box.clip_minimum.x && box.maximum.x <= box.clip_maximum.x &&
                          box.minimum.y >= box.clip_minimum.y &&
                          box.maximum.y <= box.clip_maximum.y,
                      "Navigation state clipped at supported Inspector size/scale");
    }
    check(submissions == 0 && scene.document() == saved,
          "Readonly progress submitted/mutated authored values");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        for (float scale : {1.f, 1.5f, 2.f})
            for (ImVec2 size : {ImVec2{420, 640}, ImVec2{1200, 900}})
                exercise(scale, size);
        std::cout << "Navigation Live progress, failure/metric gating, scales and readonly "
                     "isolation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
