#include "audio_source.hpp"
#include "component_inspector.hpp"
#include "project_settings.hpp"
#include "spatial_helpers.hpp"
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
void directional_guides() {
    EngineContext engine;
    Scene scene(engine.world());
    const std::string id = authoring_command(scene, "entity.create").at("selected");
    authoring_command(scene, "component.add",
                      {{"entity", id}, {"component", "forge.audio_source"}});
    auto actor = scene.entity(id);
    auto source = actor.get<AudioSource>();
    source.cone_enabled = true;
    actor.set(source);
    actor.set<LocalTranslation>({0, 0, 0});
    auto inspect = [&] {
        const auto before = scene.snapshot();
        const auto markers = ui::spatial_markers(scene.effective_document(), 800, 600, 3);
        require(scene.snapshot() == before, "Sound guide mutated authored scene");
        require(markers.size() == 1 && markers[0].audio,
                "Sound guide absent from existing helper owner");
        for (const auto& line : markers[0].lines)
            for (const auto& point : {line.first, line.second})
                for (double value : point)
                    require(std::isfinite(value), "Cone guide produced nonfinite geometry");
        return markers[0];
    };
    auto marker = inspect();
    require(marker.lines.size() == 227 && marker.lines[0].second == Double3{0, 0, -2},
            "Cone guide ignored negative-Z forward or bounded spherical construction");
    for (float opening : {0.f, 180.f, 360.f}) {
        source.inner_angle = source.outer_angle = opening;
        actor.set(source);
        marker = inspect();
        require(marker.lines.size() == 227, "Boundary opening lost finite angular guide");
    }
    actor.set<LocalRotation>(
        {0, float(std::sin(std::numbers::pi / 4)), 0, float(std::cos(std::numbers::pi / 4))});
    actor.set<LocalScale>({3, 2, 4});
    marker = inspect();
    require(std::abs(marker.lines[0].second[0] + 2) < 1e-6 &&
                std::abs(marker.lines[0].second[2]) < 1e-6,
            "Angular helper did not normalize effective rotated source direction");
    actor.set<LocalScale>({1, 1, 0});
    marker = inspect();
    require(marker.lines.empty() && marker.note.find("Emitter forward") != std::string::npos,
            "Degenerate direction fabricated a cone guide");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need audio Inspector scratch folder");
        const auto scratch = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(scratch);
        directional_guides();
        for (float scale : {.65f, 1.f, 1.5f, 2.f}) {
            const auto root = scratch / AssetId::generate().str();
            SceneDocument::create_project(root, "Audio falloff Inspector");
            {
                ProjectSettings initial(root);
                auto data = initial.document();
                data["game"] = default_game_settings("org.forge.audio-ui", "Audio UI");
                initial.save(std::move(data));
            }
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
                    if (field.at("id") == "doppler")
                        require(field.at("display_name") == "Doppler strength" &&
                                    field.at("minimum") == 0 && field.at("maximum") == 5,
                                "Doppler strength range or Inspector label differs");
                    if (field.at("id") == "inner_angle" || field.at("id") == "outer_angle")
                        require(field.at("minimum") == 0 && field.at("maximum") == 360,
                                "Cone opening range differs");
                    if (field.at("id") == "outer_gain")
                        require(field.at("minimum") == 0 && field.at("maximum") == 1,
                                "Outside volume range differs");
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
                ProjectSettingsEditor settings;
                bool mixing = false;
                bool live = false, live_ready = true, live_pending = false;
                float live_width = 1500;
                unsigned live_requests = 0;
                AudioSourceOperation live_operation = AudioSourceOperation::Stop;
                AudioSourceEditor sound;
                const EntityRef sound_ref{scene.asset_id(), EntityId::parse(id)};
                Json sound_status{{"source", sound_ref}, {"available", true}, {"playing", false},
                                  {"paused", true},      {"loop", true},      {"duration", 1}};
                std::string status;
                std::string selected = id;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({live ? live_width : 1500, 1400}, ImGuiCond_Always);
                    if (mixing)
                        settings.draw(project, scene, false, status);
                    else {
                        ImGui::Begin("Audio Inspector", nullptr, ImGuiWindowFlags_NoResize);
                        if (live) {
                            ImGui::SetScrollY(0);
                            sound.draw("test-session", sound_ref, sound_status, live_ready,
                                       live_pending, "", [&](AudioSourceOperation value) {
                                           ++live_requests;
                                           live_operation = value;
                                           return true;
                                       });
                        } else
                            inspector.draw(scene, project, selected);
                        ImGui::End();
                    }
                    ImGui::Render();
                };
                auto click = [&](const char* field) {
                    const auto name =
                        std::string(live     ? "live-audio-source:"
                                    : mixing ? "project-audio:"
                                             : "component-field:forge.audio_source:") +
                        field;
                    frame();
                    require(test::ui_targets.contains(name), "Missing audio widget: " + name);
                    auto target = test::ui_targets.at(name);
                    if (target.minimum.y < target.clip_minimum.y ||
                        target.maximum.y > target.clip_maximum.y) {
                        auto* window = ImGui::FindWindowByName(
                            mixing ? "Project Settings###Project Settings" : "Audio Inspector");
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
                click("cone_enabled");
                require(scene.entity(id).get<AudioSource>().cone_enabled,
                        "Actual directional checkbox did not author its field");
                require(scene.undo() && !scene.entity(id).get<AudioSource>().cone_enabled &&
                            scene.redo() && scene.entity(id).get<AudioSource>().cone_enabled,
                        "Directional checkbox lost Scene Undo/Redo");
                auto numeric = [&](const char* field, const char* text) {
                    click(field);
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    key(ImGuiKey_A);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    frame();
                    io.AddInputCharactersUTF8(text);
                    frame();
                    key(ImGuiKey_Enter);
                };
                numeric("inner_angle", "40");
                numeric("outer_angle", "100");
                numeric("outer_gain", "0.4");
                auto cone = scene.entity(id).get<AudioSource>();
                require(cone.inner_angle == 40 && cone.outer_angle == 100 &&
                            std::abs(cone.outer_gain - .4f) < 1e-6,
                        "Actual cone controls did not author the openings/outside volume");
                require(scene.undo() && scene.entity(id).get<AudioSource>().outer_gain == .25f &&
                            scene.redo() &&
                            std::abs(scene.entity(id).get<AudioSource>().outer_gain - .4f) < 1e-6,
                        "Cone outside volume lost Scene Undo/Redo");
                project.save();
                click("bus");
                key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                require(scene.entity(id).get<AudioSource>().bus == 1,
                        "Actual mix dropdown did not route source to Music");
                require(scene.undo() && scene.entity(id).get<AudioSource>().bus == 0 &&
                            scene.redo() && scene.entity(id).get<AudioSource>().bus == 1,
                        "Mix channel dropdown lost Scene Undo/Redo");
                numeric("doppler", "1.5");
                require(scene.entity(id).get<AudioSource>().doppler == 1.5f,
                        "Actual Doppler control did not commit");
                require(scene.undo() && scene.entity(id).get<AudioSource>().doppler == 0 &&
                            scene.redo() && scene.entity(id).get<AudioSource>().doppler == 1.5f,
                        "Doppler control lost Scene Undo/Redo");
                project.save();
                project.open_project(root);
                require(scene.entity(id).get<AudioSource>().attenuation == 1 &&
                            scene.entity(id).get<AudioSource>().rolloff == 2.5f,
                        "Audio settings did not survive save/reopen");
                cone = scene.entity(id).get<AudioSource>();
                require(cone.doppler == 1.5f && cone.bus == 1 && cone.cone_enabled &&
                            cone.inner_angle == 40 && cone.outer_angle == 100 &&
                            std::abs(cone.outer_gain - .4f) < 1e-6,
                        "Directional source settings did not survive save/reopen");
                const auto untouched_scene = scene.snapshot();
                const auto untouched_settings = project.settings().document();
                require(AudioSourceEditor::selected(scene.effective_document(), id) == sound_ref &&
                            !AudioSourceEditor::selected(scene.effective_document(), "unknown"),
                        "Live source selection lost scene/entity identity");
                live = true;
                click("play");
                require(live_requests == 1 && live_operation == AudioSourceOperation::Restart,
                        "Actual Play/Restart button did not submit source playback");
                sound_status["playing"] = true;
                click("pause");
                require(live_requests == 2 && live_operation == AudioSourceOperation::Pause,
                        "Actual Pause sound button did not submit source pause");
                sound_status["playing"] = false;
                sound_status["source_paused"] = true;
                click("pause");
                require(live_requests == 3 && live_operation == AudioSourceOperation::Resume,
                        "Actual Resume sound button did not submit source resume");
                sound_status["source_paused"] = false;
                frame();
                require(!test::ui_targets.at("live-audio-source:pause").enabled,
                        "Stopped source enabled Pause/Resume");
                live_width = 360;
                frame();
                for (const auto* key : {"live-audio-source:play", "live-audio-source:pause",
                                        "live-audio-source:stop"}) {
                    const auto& target = test::ui_targets.at(key);
                    require(target.minimum.x >= target.clip_minimum.x &&
                                target.maximum.x <= target.clip_maximum.x,
                            "Narrow scaled Inspector clipped a playback button");
                }
                live_width = 1500;
                click("stop");
                require(live_requests == 4 && live_operation == AudioSourceOperation::Stop,
                        "Actual Stop sound button did not submit source stop");
                live_pending = true;
                frame();
                require(!test::ui_targets.at("live-audio-source:play").enabled &&
                            !test::ui_targets.at("live-audio-source:pause").enabled &&
                            !test::ui_targets.at("live-audio-source:stop").enabled,
                        "Pending source control admitted another button action");
                live_pending = false;
                live_ready = false;
                frame();
                require(!test::ui_targets.at("live-audio-source:play").enabled,
                        "Busy Play admitted a source button action");
                live_ready = true;
                sound_status["available"] = false;
                frame();
                require(!test::ui_targets.at("live-audio-source:play").enabled &&
                            live_requests == 4 && scene.snapshot() == untouched_scene &&
                            project.settings().document() == untouched_settings,
                        "Unavailable/live source controls changed authoring or submitted per-frame "
                        "traffic");
                live = false;
                mixing = true;
                settings.open();
                frame();
                frame();
                const auto scene_before_mix = scene.snapshot();
                const std::array mix_fields{"master_volume", "music_volume", "effects_volume",
                                            "dialogue_volume"};
                const std::array mix_text{"0.8", "0.25", "0.5", "0.75"};
                for (unsigned i = 0; i < mix_fields.size(); ++i) {
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    click(mix_fields[i]);
                    key(ImGuiKey_A);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    frame();
                    io.AddInputCharactersUTF8(mix_text[i]);
                    frame();
                    key(ImGuiKey_Enter);
                }
                require(settings.dirty() && settings.save(project, status),
                        "Actual mixer controls did not publish owning project settings");
                const auto saved = ProjectSettings(root).document().at("game").at("audio");
                require(std::abs(saved.at("master_volume").get<float>() - .8f) < 1e-6 &&
                            audio_channel_volumes(saved) == std::array<float, 3>{.25f, .5f, .75f} &&
                            scene.snapshot() == scene_before_mix,
                        "Scaled mixer values did not persist independently of authored scene");
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
