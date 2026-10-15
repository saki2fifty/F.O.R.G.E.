#include "animation_timeline.hpp"
#include <chrono>
#include <imgui_internal.h>
#include <iostream>
#include <numbers>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& scratch, float scale, ImVec2 display) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto actor =
        authoring_command(scene, "entity.create", {{"name", "Lens and light"}})["selected"]
            .get<std::string>();
    for (const char* component :
         {"forge.camera", "forge.light", "forge.audio_source", "forge.particle_emitter"})
        authoring_command(scene, "component.add", {{"entity", actor}, {"component", component}});
    auto document = TransformAnimationDocument::create(project.writer_guard(),
                                                       "Assets/Properties.animation.json");
    const auto id = document->source().asset();
    document.reset();
    const auto authored = scene.document();
    const auto scene_revision = scene.revision();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    AnimationTimelineEditor editor;
    editor.open(project, "Assets/Properties.animation.json");
    editor.bind(scene, actor);
    bool locked = false;
    std::string message;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, scene, locked, io.DeltaTime, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({5, 5}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({display.x - 10, display.y - 10}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    auto target = [&](const std::string& name) {
        frame();
        check(test::ui_targets.contains(name), "Missing property widget " + name);
        auto box = test::ui_targets.at(name);
        if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
            auto* window = ImGui::FindWindowByName("###Animation Timeline");
            check(window != nullptr, "Timeline window missing");
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y) / 2 -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            box = test::ui_targets.at(name);
        }
        check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              "Property widget clipped/disabled " + name);
        return box;
    };
    auto mouse = [&](ImVec2 p, bool down) {
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    };
    auto click = [&](const std::string& name) {
        const auto box = target(name);
        const ImVec2 p{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        frame();
    };
    auto type = [&](const std::string& name, const char* value) {
        click(name);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    frame();
    frame();
    click("animation:properties");
    click("animation:property:forge.camera.vertical_fov");
    click("animation:property:forge.light.intensity");
    click("animation:property:forge.audio_source.gain");
    click("animation:property:forge.audio_source.pitch");
    for (const char* field : {"size_multiplier", "tint_r", "tint_g", "tint_b"})
        click(std::string("animation:property:forge.particle_emitter.") + field);
    check(editor.document()->source().document["version"] == 3 &&
              editor.document()->source().clip().properties == 4037,
          "Typed property checkboxes did not add bindings");
    for (const char* name : {"Position", "Rotation", "Scale"})
        click(std::string("animation:channel:") + name);
    check(editor.document()->source().clip().channels == 0, "UI rejected property-only animation");
    const auto ruler = target("animation:ruler");
    const ImVec2 p{ruler.minimum.x + (ruler.maximum.x - ruler.minimum.x) * .75f,
                   ruler.minimum.y + 50};
    mouse(p, false);
    mouse(p, true);
    mouse(p, false);
    type("animation:value:forge.camera.vertical_fov", "90");
    type("animation:value:forge.light.intensity", "10");
    type("animation:value:forge.audio_source.gain", "0.25");
    type("animation:value:forge.audio_source.pitch", "2");
    type("animation:value:forge.particle_emitter.size_multiplier", "2.5");
    type("animation:value:forge.particle_emitter.tint_r", "0.25");
    type("animation:value:forge.particle_emitter.tint_g", "0.5");
    type("animation:value:forge.particle_emitter.tint_b", "0.75");
    click("button:Add / Update key at playhead");
    const auto source = editor.document()->source();
    check(source.clip().keys.size() == 2, "Property UI did not create second key");
    check(std::abs(source.clip().keys.back().properties[0] - std::numbers::pi / 2) < 1e-7 &&
              source.clip().keys.back().properties[2] == 10 &&
              source.clip().keys.back().properties[6] == .25 &&
              source.clip().keys.back().properties[7] == 2 &&
              source.clip().keys.back().properties[8] == 2.5 &&
              source.clip().keys.back().properties[9] == .25 &&
              source.clip().keys.back().properties[10] == .5 &&
              source.clip().keys.back().properties[11] == .75,
          "Property UI did not convert degrees or save values");
    const auto revision = editor.document()->revision();
    type("animation:value:forge.camera.vertical_fov", "0");
    click("button:Add / Update key at playhead");
    check(editor.document()->revision() == revision &&
              editor.document()->source().document == source.document,
          "Rejected property UI edit changed history");
    type("animation:value:forge.camera.vertical_fov", "75");
    click("button:Add / Update key at playhead");
    check(editor.document()->revision() != revision, "Property UI did not recover after rejection");
    editor.undo();
    check(editor.document()->source().document == source.document,
          "Property UI Undo lost key values/identity");
    const auto audio_revision = editor.document()->revision();
    type("animation:value:forge.audio_source.pitch", "0");
    click("button:Add / Update key at playhead");
    check(editor.document()->revision() == audio_revision &&
              editor.document()->source().document == source.document,
          "Rejected audio pitch UI edit changed source/history");
    type("animation:value:forge.audio_source.pitch", "2");
    click("button:Capture authored pose");
    click("button:Add / Update key at playhead");
    check(std::abs(editor.document()->source().clip().keys.back().properties[0] -
                   std::numbers::pi / 3) < 1e-7 &&
              editor.document()->source().clip().keys.back().properties[6] == 1 &&
              editor.document()->source().clip().keys.back().properties[7] == 1 &&
              editor.document()->source().clip().keys.back().properties[8] == 1 &&
              editor.document()->source().clip().keys.back().properties[11] == 1,
          "Capture omitted authored camera field");
    editor.undo();
    const auto preview = editor.preview(scene.effective_document());
    check(preview != scene.effective_document() && scene.document() == authored &&
              scene.revision() == scene_revision &&
              preview["entities"][0]["components"]["forge.audio_source"]["gain"] == .25 &&
              preview["entities"][0]["components"]["forge.audio_source"]["pitch"] == 2 &&
              preview["entities"][0]["components"]["forge.particle_emitter"]["size_multiplier"] ==
                  2.5 &&
              preview["entities"][0]["components"]["forge.particle_emitter"]["tint_b"] == .75,
          "Property preview changed Scene history or omitted substitution");
    editor.stop_preview();
    check(editor.preview(scene.effective_document()) == scene.effective_document(),
          "Property preview stop did not restore authored view");
    locked = true;
    frame();
    check(!test::ui_targets.at("animation:property:forge.camera.vertical_fov").enabled &&
              !test::ui_targets.at("animation:value:forge.light.intensity").enabled &&
              !test::ui_targets.at("animation:property:forge.audio_source.gain").enabled &&
              !test::ui_targets.at("animation:value:forge.audio_source.pitch").enabled &&
              !test::ui_targets.at("animation:property:forge.particle_emitter.size_multiplier")
                   .enabled &&
              !test::ui_targets.at("animation:value:forge.particle_emitter.tint_b").enabled,
          "Property editing stayed enabled during Play");
    locked = false;
    editor.request_save();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    std::shared_ptr<const AssetCatalog> catalog;
    while (!catalog) {
        frame();
        catalog = editor.take_catalog();
        check(std::chrono::steady_clock::now() < deadline,
              "Property Timeline publication stalled: " + message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(catalog->records().at(id).metadata["forge.import"]["output_version"] == 5 &&
              !editor.dirty(),
          "Timeline published wrong property cook");
    click("button:Attach animation to object");
    check(scene.entity(actor).get<TransformAnimator>().clip.id == id && scene.undo() &&
              scene.document() == authored,
          "Property attachment did not use Scene history");
    const auto first = editor.document()->source().clip().keys.front().id;
    const auto first_ruler = target("animation:ruler");
    const ImVec2 first_point{first_ruler.minimum.x + 2, first_ruler.minimum.y + 36};
    mouse(first_point, false);
    mouse(first_point, true);
    mouse(first_point, false);
    frame();
    click("animation:curve_section");
    click("animation:curve_channel");
    click("animation:curve_channel_choice:10"); // Particle size, after2 vector choices.
    click("animation:curve_mode");
    click("animation:curve_choice:1");
    type("animation:curve_property_in", "0");
    type("animation:curve_property_out", "0");
    click("button:Apply curve settings");
    auto curved = editor.document()->source();
    check(curved.document["version"] == 4 && curved.clip().keys.front().curves.properties[8].cubic,
          "Scalar curve widgets did not commit source4 settings");
    const auto interval = curved.clip().keys.back().time - curved.clip().keys.front().time;
    check(std::abs(curved.clip().sample_properties(double(interval) / 4)[8] - 1.234375) < 1e-9,
          "Scalar Timeline graph/source retained linear timing");
    const auto tangent_box = target("animation:tangent_out");
    const ImVec2 handle{(tangent_box.minimum.x + tangent_box.maximum.x) / 2,
                        (tangent_box.minimum.y + tangent_box.maximum.y) / 2};
    mouse(handle, false);
    mouse(handle, true);
    mouse({handle.x, handle.y - 20}, true);
    mouse({handle.x, handle.y - 20}, false);
    check(editor.document()->source().clip().keys.front().curves.properties[8].outgoing > 0,
          "Scalar tangent drag did not update the selected slope");
    editor.undo();
    frame();
    check(editor.document()->source().document == curved.document,
          "Scalar tangent Undo changed key data or identity");
    const auto cancel_box = target("animation:tangent_out");
    const ImVec2 cancel_handle{(cancel_box.minimum.x + cancel_box.maximum.x) / 2,
                               (cancel_box.minimum.y + cancel_box.maximum.y) / 2};
    mouse(cancel_handle, false);
    mouse(cancel_handle, true);
    mouse({cancel_handle.x, cancel_handle.y - 20}, true);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    mouse(cancel_handle, false);
    frame();
    check(editor.document()->source().document == curved.document,
          "Scalar tangent Escape committed a pending edit");
    click("animation:curve_channel");
    click("animation:curve_channel_choice:2");
    click("animation:curve_mode");
    click("animation:curve_choice:1");
    type("animation:curve_property_out", "90");
    click("button:Apply curve settings");
    check(std::abs(editor.document()->source().clip().keys.front().curves.properties[0].outgoing -
                   std::numbers::pi / 2) < 1e-12,
          "FOV slope fields did not convert displayed degrees to native radians");
    click("button:Flat tangents");
    check(editor.document()->source().clip().keys.front().curves.properties[0].outgoing == 0,
          "Scalar tangent preset did not edit the selected property");
    locked = true;
    frame();
    check(!test::ui_targets.at("animation:curve_property_out").enabled,
          "Scalar curve editing remained enabled during Play");
    locked = false;
    const auto saved_curves = editor.document()->source().document;
    editor.request_save();
    catalog.reset();
    const auto curve_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!catalog) {
        frame();
        catalog = editor.take_catalog();
        check(std::chrono::steady_clock::now() < curve_deadline,
              "Scalar curve publication stalled: " + message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!editor.dirty() &&
              catalog->records().at(id).metadata["forge.import"]["output_version"] == 5,
          "Scalar curves did not publish through the existing document");
    editor.open(project, "Assets/Properties.animation.json");
    check(editor.document()->source().document == saved_curves,
          "Timeline save/reopen changed scalar curves or key identities");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need property UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f})
            for (auto display : {ImVec2{1400, 1400}, ImVec2{960, 640}})
                exercise(argv[1], scale, display);
        std::cout << "Actual property Timeline input, units, history, preview, publication and "
                     "locked controls PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
