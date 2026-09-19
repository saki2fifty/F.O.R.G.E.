#pragma once
#include "ecs_tools.hpp"
#include "flecs_script.hpp"
void require(bool, const char*);
inline void test_ecs_workspaces() {
    using namespace forge;
    const auto scratch =
        std::filesystem::current_path() / ("ecs-editor-" + AssetId::generate().str());
    std::filesystem::create_directory(scratch);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{scratch};
    const auto root = scratch / "Project";
    SceneDocument::create_project(root, "ECS tools");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    atomic_write(root / "Assets/Test.flecs", "Example {}\n");
    const auto asset = register_flecs_script(root, "Assets/Test.flecs");
    require(register_flecs_script(root, "Assets/Test.flecs").id == asset.id,
            "Script registration changed asset identity");
    const auto before = scene.snapshot();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440, 1000};
    io.DeltaTime = .02f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::Problems problems;
    ui::EcsWorkspace ecs(engine.world());
    ecs.request_open();
    ui::FlecsScriptEditor script({});
    script.open(document, asset);
    require(script.is_open() && !script.dirty(), "Script document did not open cleanly");
    for (float scale : {1.f, 1.5f, 2.f}) {
        ui::style(scale);
        for (const char* tab :
             {"Statistics", "Query", "Metrics", "Entity / JSON", "REST / Explorer", "Alerts"}) {
            for (int pass = 0; pass < 2; ++pass) {
                if (auto* window = ImGui::FindWindowByName("ECS World inspection"))
                    if (auto* bar = GImGui->TabBars.GetByKey(window->GetID("ecs-tabs")))
                        if (auto* item = ImGui::TabBarFindTabByID(bar, ImHashStr(tab, 0, bar->ID)))
                            ImGui::TabBarQueueFocus(bar, item);
                ImGui::NewFrame();
                ecs.draw(scene, problems);
                script.draw(document, false);
                ImGui::Render();
                if (pass == 1) {
                    auto* window = ImGui::FindWindowByName("ECS World inspection");
                    auto* bar =
                        window ? GImGui->TabBars.GetByKey(window->GetID("ecs-tabs")) : nullptr;
                    require(bar && bar->SelectedTabId == ImHashStr(tab, 0, bar->ID),
                            "ECS tab fixture did not exercise the requested tab");
                }
                require(ImGui::GetDrawData()->TotalVtxCount > 0,
                        "ECS/script workspace produced no draw data");
            }
        }
    }
    atomic_write(root / "Assets/Included.flecs", "First {}\nSecond {}\n");
    script.navigate_source(document, "Assets/Included.flecs", 2, 1);
    ImGui::NewFrame();
    script.draw_source_viewer(document);
    ImGui::Render();
    require(ImGui::FindWindowByName("Script diagnostic source") != nullptr,
            "Included source navigation did not open the read-only viewer");
    require(!script.dirty(), "Diagnostic navigation changed the active draft");
    bool outside_rejected = false;
    try {
        script.navigate_source(document, "../outside.flecs");
    } catch (const std::exception&) {
        outside_rejected = true;
    }
    require(outside_rejected, "Diagnostic navigation escaped project containment");
    require(scene.snapshot() == before, "Inspecting ECS/script workspaces mutated authored scene");
    script.request_save();
    ImGui::NewFrame();
    script.draw(document, false);
    ImGui::Render();
    require(!script.dirty(), "Saving clean Script introduced draft changes");
    script.request_close();
    require(!script.is_open(), "Clean Script failed to close");
    ui::style(1.f);
    ImGui::DestroyContext();
}
