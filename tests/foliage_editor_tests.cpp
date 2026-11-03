#include "editor/foliage_placement.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
std::string cube(Scene& scene) {
    return authoring_command(scene, "entity.create",
                             {{"recipe", "primitive.0"}, {"name", "Fern template"}})
        .at("selected");
}
void exercise(float scale, bool compact) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = cube(scene);
    const auto before = scene.document();
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(ImGui::CreateContext(),
                                                                            ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.DisplaySize = {compact ? 460 * scale : 1000 * scale, 900 * scale};
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext owner;
    ui::ContextScope scope(owner);
    owner.selection.select_entity(source);
    ui::FoliagePlacement panel;
    panel.open = true;
    bool locked = false;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetWindowSize("Foliage placement", {(compact ? 400.f : 650.f) * scale, 720 * scale},
                             ImGuiCond_Always);
        panel.draw(scene, owner.selection, locked);
        ImGui::Render();
    };
    test::ImGuiInputDriver input(frame);
    for (unsigned i = 0; i < 4; ++i)
        frame();
    const auto* window = ImGui::FindWindowByName("Foliage placement");
    check(window && std::abs(window->Size.x - (compact ? 400.f : 650.f) * scale) < 1 &&
              std::abs(window->Size.y - 720 * scale) < 1,
          "Fixture did not exercise its requested full/compact layout");
    input.click("foliage:source");
    input.type("foliage:name", "Meadow");
    input.type("foliage:count", "12");
    input.type("foliage:radius", "3.5");
    input.type("foliage:spacing", "0.6");
    input.type("foliage:seed", "42");
    input.type("foliage:y", "2");
    input.click("foliage:check");
    check(scene.document() == before && owner.problems.size() == 0,
          "Layout check mutated Scene or reported valid input as invalid");
    input.click("foliage:place");
    const auto placed = scene.document();
    check(placed.at("entities").size() == 14 && owner.selection.entity() != source &&
              placed.at("entities").at(1).at("name") == "Meadow" && owner.problems.size() == 0,
          "Actual placement controls did not create/select one named group");
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == placed,
          "Actual placement control did not use one normal history step");
    input.type("foliage:count", "513");
    input.click("foliage:place");
    check(scene.document() == placed && owner.problems.size() == 1 &&
              !owner.problems.items().front().occurred_at.empty(),
          "Rejected actual input mutated Scene or lacked timestamped Problem");
    input.type("foliage:count", "1");
    input.click("foliage:check");
    check(owner.problems.size() == 0, "Corrected actual layout retained its Problem");
    locked = true;
    frame();
    check(!test::ui_targets.at("foliage:place").enabled &&
              !test::ui_targets.at("foliage:source").enabled,
          "Play/task lock did not disable foliage editing");
    locked = false;
    Scene other(engine.world());
    frame();
    // A different Scene identity must never keep a template from the old project.
    auto replacement = other.document();
    scene.reset(replacement);
    frame();
    check(!test::ui_targets.at("foliage:place").enabled,
          "Scene switch retained the old foliage template");
    test::observe_ui = false;
}
void capture(const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        test::ImGuiSoftwareCapture capture(720, 1100);
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        ui::EditorUiContext owner;
        ui::ContextScope scope(owner);
        owner.selection.select_entity(cube(scene));
        ui::FoliagePlacement panel;
        panel.open = true;
        test::observe_ui = true;
        auto frame = [&] {
            test::ui_targets.clear();
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetWindowSize("Foliage placement", {720, 1100}, ImGuiCond_Always);
            panel.draw(scene, owner.selection, false);
            capture.render();
        };
        test::ImGuiInputDriver input(frame);
        for (unsigned i = 0; i < 4; ++i)
            frame();
        input.click("foliage:source");
        input.click("foliage:check");
        for (unsigned i = 0; i < 4; ++i)
            frame();
        capture.save(output / "foliage-placement-controls.png");
        test::observe_ui = false;
    }
    SDL_Quit();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2)
            capture(argv[1]);
        else
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool compact : {false, true})
                    exercise(scale, compact);
        std::cout << "Actual foliage control checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
