#include "component_inspector.hpp"
#include "particle_runtime.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& scratch, float scale, ImVec2 size) {
    const auto root = scratch / AssetId::generate().str();
    SceneDocument::create_project(root, "Emitter Inspector");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto actor = authoring_command(scene, "entity.create", {{"recipe", "render.particles"}})
                           .at("selected")
                           .get<std::string>();
    const auto saved = scene.document();
    ComponentInspector inspector;
    EngineContext runtime(WorldRole::Runtime, false, {particle_module({})});
    Scene running(runtime.world());
    running.restore_snapshot(saved);
    auto particles = particle_runtime(runtime.world());
    const auto reference = runtime.world().reference(running.entity(actor).id()).value();
    bool live = false;
    unsigned submissions = 0;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    context.scene = &scene;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Emitter Inspector", nullptr, ImGuiWindowFlags_NoResize);
        if (live)
            inspector.draw_runtime(
                running.effective_document(), running.schema(), root, actor,
                [&](const auto& type, const auto& field, const auto& value) {
                    check(type == "forge.particle_emitter", "Unexpected Live type");
                    particles->tune_appearance(reference, field, value);
                    ++submissions;
                    return true;
                },
                false);
        else
            inspector.draw(scene, project, actor);
        ImGui::End();
        ImGui::Render();
    };
    auto target = [&](const char* field) {
        const auto key = std::string(live ? "runtime-field:" : "component-field:") +
                         "forge.particle_emitter:" + field;
        frame();
        frame();
        check(test::ui_targets.contains(key), "Missing emitter field: " + key);
        auto box = test::ui_targets.at(key);
        if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
            auto* window = ImGui::FindWindowByName("Emitter Inspector");
            if (live)
                for (auto* child : window->DC.ChildWindows)
                    if (child->ScrollMax.y > 0)
                        window = child;
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) *
                                                             .5f);
            frame();
            frame();
            box = test::ui_targets.at(key);
        }
        check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              "Clipped emitter field: " + key + " scale=" + std::to_string(scale) +
                  " box=" + std::to_string(box.minimum.x) + "," + std::to_string(box.minimum.y) +
                  ":" + std::to_string(box.maximum.x) + "," + std::to_string(box.maximum.y) +
                  " clip=" + std::to_string(box.clip_minimum.x) + "," +
                  std::to_string(box.clip_minimum.y) + ":" + std::to_string(box.clip_maximum.x) +
                  "," + std::to_string(box.clip_maximum.y));
        return ImVec2{box.minimum.x + std::min(40.f, (box.maximum.x - box.minimum.x) * .3f),
                      (box.minimum.y + box.maximum.y) * .5f};
    };
    auto edit = [&](const char* field, const char* text) {
        const auto point = target(field);
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        io.AddInputCharactersUTF8(text);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    for (const auto* field : {"size_multiplier", "tint_r", "tint_g", "tint_b"})
        (void)target(field);
    edit("size_multiplier", "2");
    check(scene.entity(actor).get<ParticleEmitter>().size_multiplier == 2 && scene.undo() &&
              scene.document() == saved,
          "Actual authored size edit/Undo failed");
    edit("tint_b", "0.25");
    check(scene.entity(actor).get<ParticleEmitter>().tint_b == .25f && scene.undo() &&
              scene.document() == saved,
          "Actual authored tint edit/Undo failed");
    live = true;
    frame();
    check(ComponentInspector::has_runtime_inspection(running.effective_document(), running.schema(),
                                                     actor),
          "Particle Live section unavailable");
    for (const auto* field : {"size_multiplier", "tint_r", "tint_g", "tint_b"})
        (void)target(field);
    for (const auto* field : {"seed", "enabled", "effect", "playback_speed"})
        check(!test::ui_targets.contains(std::string("runtime-field:forge.particle_emitter:") +
                                         field),
              "Particle Live Inspector offered a restart/resource control");
    edit("size_multiplier", "3");
    edit("tint_g", "0.5");
    check(submissions == 2 && running.entity(actor).get<ParticleEmitter>().size_multiplier == 3 &&
              running.entity(actor).get<ParticleEmitter>().tint_g == .5f &&
              scene.document() == saved,
          "Actual Live appearance failed or changed authored scene");
    running.restore_snapshot(saved); // Stop / next Play starts from authored values.
    check(running.entity(actor).get<ParticleEmitter>().size_multiplier == 1 &&
              running.entity(actor).get<ParticleEmitter>().tint_g == 1,
          "Live appearance leaked into restart");
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Inspector scratch root");
        std::filesystem::create_directories(argv[1]);
        for (const float scale : {1.f, 1.5f, 2.f})
            for (const auto size : {ImVec2{420, 640}, ImVec2{1200, 900}})
                exercise(argv[1], scale, size);
        std::cout << "Particle authored/Live appearance controls, Undo, Stop, scales passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
