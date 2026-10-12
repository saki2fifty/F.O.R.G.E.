#include "component_inspector.hpp"
#include <forge/audio_components.hpp>
#include <forge/authoring.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need audio Inspector scratch folder");
        const auto scratch = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(scratch);
        for (float scale : {.65f, 1.f, 1.5f, 2.f}) {
            const auto root = scratch / AssetId::generate().str();
            SceneDocument::create_project(root, "Audio falloff Inspector");
            {
                EngineContext engine;
                Scene scene(engine.world());
                SceneDocument project(scene);
                project.open_project(root);
                const auto id = authoring_command(scene, "entity.create", {{"name", "Speaker"}})
                                    .at("selected")
                                    .get<std::string>();
                authoring_command(scene, "component.add",
                                  {{"entity", id}, {"component", "forge.audio_source"}});
                const auto schema = scene.schema();
                Json source_type;
                for (const auto& type : schema.at("components"))
                    if (type.at("id") == "forge.audio_source")
                        source_type = type;
                require(!source_type.is_null(), "AudioSource absent from builtin schema");
                for (const auto& field : source_type.at("fields")) {
                    if (field.at("id") == "attenuation")
                        require(field.at("display_name") == "Distance falloff" &&
                                    field.at("choices").size() == 3,
                                "Native falloff choices or Inspector label differs");
                    if (field.at("id") == "rolloff")
                        require(field.at("display_name") == "Falloff strength" &&
                                    field.at("minimum") == 0 && field.at("maximum") == 10,
                                "Native strength range or Inspector label differs");
                }
                ImGui::CreateContext();
                auto& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.DisplaySize = {1500, 1400};
                io.DeltaTime = 1.f / 60;
                io.FontGlobalScale = scale;
                io.ConfigInputTrickleEventQueue = false;
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                unsigned char* pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                ui::interface_scale = scale;
                ui::EditorUiContext context;
                context.scene = &scene;
                ui::ContextScope scope(context);
                test::observe_ui = true;
                ComponentInspector inspector;
                std::string selected = id;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({1500, 1400}, ImGuiCond_Always);
                    ImGui::Begin("Audio Inspector", nullptr, ImGuiWindowFlags_NoResize);
                    inspector.draw(scene, project, selected);
                    ImGui::End();
                    ImGui::Render();
                };
                auto click = [&](const char* field) {
                    const auto name = std::string("component-field:forge.audio_source:") + field;
                    frame();
                    require(test::ui_targets.contains(name), "Missing audio widget: " + name);
                    auto target = test::ui_targets.at(name);
                    if (target.minimum.y < target.clip_minimum.y ||
                        target.maximum.y > target.clip_maximum.y) {
                        auto* window = ImGui::FindWindowByName("Audio Inspector");
                        require(window, "Audio Inspector window missing");
                        ImGui::SetScrollY(window,
                                          window->Scroll.y +
                                              (target.minimum.y + target.maximum.y -
                                               target.clip_minimum.y - target.clip_maximum.y) *
                                                  .5f);
                        frame();
                        frame();
                        target = test::ui_targets.at(name);
                    }
                    require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                                target.maximum.y <= target.clip_maximum.y,
                            "Audio widget is clipped or disabled: " + name);
                    for (unsigned i = 0; i < 6; ++i) {
                        io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                                            (target.minimum.y + target.maximum.y) * .5f);
                        frame();
                        target = test::ui_targets.at(name);
                        if (ImGui::GetCurrentContext()->HoveredId == target.id)
                            break;
                    }
                    require(ImGui::GetCurrentContext()->HoveredId == target.id,
                            "Pointer did not reach actual audio widget: " + name);
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                };
                auto key = [&](ImGuiKey value) {
                    io.AddKeyEvent(value, true);
                    frame();
                    io.AddKeyEvent(value, false);
                    frame();
                };
                frame();
                frame();
                click("attenuation");
                key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                require(scene.entity(id).get<AudioSource>().attenuation == 1,
                        "Actual dropdown did not author linear falloff");
                require(scene.undo() && scene.entity(id).get<AudioSource>().attenuation == 0 &&
                            scene.redo() && scene.entity(id).get<AudioSource>().attenuation == 1,
                        "Falloff dropdown lost Scene Undo/Redo");
                click("rolloff");
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                key(ImGuiKey_A);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
                io.AddInputCharactersUTF8("2.5");
                frame();
                key(ImGuiKey_Enter);
                require(scene.entity(id).get<AudioSource>().rolloff == 2.5f,
                        "Actual numeric control did not author strength");
                require(scene.undo() && scene.entity(id).get<AudioSource>().rolloff == 1 &&
                            scene.redo() && scene.entity(id).get<AudioSource>().rolloff == 2.5f,
                        "Strength control lost Scene Undo/Redo");
                project.save();
                project.open_project(root);
                require(scene.entity(id).get<AudioSource>().attenuation == 1 &&
                            scene.entity(id).get<AudioSource>().rolloff == 2.5f,
                        "Audio settings did not survive save/reopen");
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
            std::filesystem::remove_all(root);
        }
        std::cout
            << "Audio falloff Inspector controls/history/save/reopen passed at65/100/150/200%\n";
        return 0;
    } catch (const std::exception& error) {
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
