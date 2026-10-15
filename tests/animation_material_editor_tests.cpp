#include "animation_timeline.hpp"
#include <forge/engine_assets.hpp>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double value, double expected, const char* why) {
    check(std::abs(value - expected) < 1e-6,
          std::string(why) + ": " + std::to_string(value) + " vs " + std::to_string(expected));
}
void exercise(const std::filesystem::path& scratch, float scale, ImVec2 display) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto actor = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                           .at("selected")
                           .get<std::string>();
    const Json parameters = Json::array({{{"slot", "surface"},
                                          {"key", "roughnessFactor"},
                                          {"future_owner", "preserve"},
                                          {"parameter",
                                           {{"type", int(MaterialParameterType::Scalar)},
                                            {"value", {.25, 0, 0, 0}},
                                            {"future_value", true}}}}});
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.mesh_renderer"},
                       {"field", "material_parameters"},
                       {"value", parameters}});
    auto created = TransformAnimationDocument::create(project.writer_guard(),
                                                      "Assets/Material.animation.json");
    const auto asset = created->source().asset();
    created.reset();
    const auto authored = scene.document();
    const auto revision = scene.revision();
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
    editor.open(project, "Assets/Material.animation.json");
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
        check(test::ui_targets.contains(name), "Missing material Timeline widget " + name);
        auto box = test::ui_targets.at(name);
        const bool selectable = name.starts_with("animation:key:");
        auto inside = [&] {
            const ImVec2 center{(box.minimum.x + box.maximum.x) / 2,
                                (box.minimum.y + box.maximum.y) / 2};
            return selectable ? center.x >= box.clip_minimum.x && center.x <= box.clip_maximum.x &&
                                    center.y >= box.clip_minimum.y && center.y <= box.clip_maximum.y
                              : box.minimum.x >= box.clip_minimum.x &&
                                    box.maximum.x <= box.clip_maximum.x &&
                                    box.minimum.y >= box.clip_minimum.y &&
                                    box.maximum.y <= box.clip_maximum.y;
        };
        if (!inside()) {
            auto* window = ImGui::FindWindowByName("###Animation Timeline");
            check(window != nullptr, "Missing material Timeline window");
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y) / 2 -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            box = test::ui_targets.at(name);
        }
        check(box.enabled && inside(), "Clipped/disabled material Timeline widget " + name +
                                           " at scale " + std::to_string(scale));
        return box;
    };
    auto mouse = [&](ImVec2 point, bool down) {
        io.AddMousePosEvent(point.x, point.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    };
    auto click = [&](const std::string& name) {
        const auto box = target(name);
        const ImVec2 point{(box.minimum.x + box.maximum.x) / 2,
                           (box.minimum.y + box.maximum.y) / 2};
        mouse(point, false);
        mouse(point, true);
        mouse(point, false);
        frame();
    };
    auto type = [&](const std::string& name, const char* text) {
        const auto box = target(name);
        // Numeric groups include labels and multiple lanes. Exercise lane zero.
        const ImVec2 point{box.minimum.x + 12, (box.minimum.y + box.maximum.y) / 2};
        mouse(point, false);
        mouse(point, true);
        mouse(point, false);
        check(ImGui::GetActiveID() != 0, "Material typing missed its field: " + name);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(text);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    auto wait_for = [&](auto predicate, const char* why) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!predicate()) {
            frame();
            check(std::chrono::steady_clock::now() < deadline, std::string(why) + ": " + message);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    frame();
    frame();
    click("animation:material_properties");
    wait_for(
        [&] {
            return test::ui_targets.contains(
                       "animation:material_property:surface:roughnessFactor") &&
                   test::ui_targets.at("animation:material_property:surface:roughnessFactor")
                       .enabled;
        },
        "Material declarations did not become ready");
    check(scene.document() == authored && scene.revision() == revision,
          "Opening async material controls changed Scene history");
    click("animation:material_property:surface:roughnessFactor");
    click("animation:material_property:surface:baseColorFactor");
    auto clip = editor.document()->source().clip();
    check(clip.material_bindings.size() == 2 &&
              editor.document()->source().document.at("version") == 5,
          "Material checkboxes did not author source5 tracks");
    auto index = [&](const char* key) {
        const auto& bindings = clip.material_bindings;
        const auto at = std::find_if(bindings.begin(), bindings.end(),
                                     [&](const auto& binding) { return binding.key == key; });
        check(at != bindings.end(), std::string("Missing material binding ") + key);
        return std::size_t(at - bindings.begin());
    };
    const auto roughness = index("roughnessFactor"), color = index("baseColorFactor");
    check(clip.material_bindings[roughness].type == MaterialParameterType::Scalar &&
              clip.material_bindings[color].type == MaterialParameterType::LinearColor4,
          "Material Timeline lost scalar/linear RGBA semantics");
    near(clip.keys.front().materials[roughness].values[0], .25,
         "Initial authored override capture");
    near(clip.keys.front().materials[color].values[0], 1,
         "Initial shared material default capture");
    for (const char* channel : {"Position", "Rotation", "Scale"})
        click(std::string("animation:channel:") + channel);
    check(editor.document()->source().clip().channels == 0,
          "Material-only Timeline clip was rejected");
    type("animation:key_time", "2");
    type("animation:material_value:surface:roughnessFactor", ".75");
    type("animation:material_value:surface:baseColorFactor", ".5");
    click("button:Add / Update key at playhead");
    clip = editor.document()->source().clip();
    check(clip.keys.size() == 2 && clip.keys.back().time == 2 * animation_timebase,
          "Material Timeline did not create the endpoint key");
    near(clip.keys.back().materials[roughness].values[0], .75, "Typed roughness endpoint");
    near(clip.keys.back().materials[color].values[0], .5, "Typed linear red endpoint");
    click("button:Play preview");
    click("button:Pause preview");
    type("animation:key_time", "1");
    const auto preview = editor.preview(scene.effective_document());
    const auto& values = preview.at("entities")[0]
                             .at("components")
                             .at("forge.mesh_renderer")
                             .at("material_parameters");
    auto preview_value = [&](const char* key) -> const Json& {
        const auto at = std::find_if(values.begin(), values.end(),
                                     [&](const auto& value) { return value.at("key") == key; });
        check(at != values.end(), std::string("Preview omitted ") + key);
        return *at;
    };
    near(preview_value("roughnessFactor").at("parameter").at("value")[0], .5, "Midpoint roughness");
    near(preview_value("baseColorFactor").at("parameter").at("value")[0], .75, "Midpoint red");
    check(preview_value("roughnessFactor").at("future_owner") == "preserve" &&
              preview_value("roughnessFactor").at("parameter").at("future_value") == true &&
              scene.document() == authored && scene.revision() == revision &&
              scene.entity(actor).get<MeshRenderer>().material_parameters.size() == 1,
          "Copied material preview erased extensions or wrote authored/native values");
    click("button:Stop preview");
    check(editor.preview(scene.effective_document()) == scene.effective_document(),
          "Stopping material preview did not restore authored values");
    const auto ruler = target("animation:ruler");
    const ImVec2 first_key{ruler.minimum.x + 2, ruler.minimum.y + 36};
    mouse(first_key, false);
    mouse(first_key, true);
    mouse(first_key, false);
    check(editor.selected_keys() == std::set{clip.keys.front().id},
          "Material curve test did not select the first key");
    click("animation:curve_section");
    click("animation:curve_channel");
    click("animation:curve_material_choice:surface:baseColorFactor");
    click("animation:curve_axis");
    click("animation:curve_axis_choice:3");
    click("animation:curve_mode");
    click("animation:curve_choice:1");
    type("animation:curve_material_out", "-.4");
    click("button:Apply curve settings");
    const auto curved = editor.document()->source().document;
    clip = editor.document()->source().clip();
    check(clip.keys.front().materials[color].curves[3].cubic,
          "Fourth color lane did not retain its cubic mode at " + std::to_string(scale) + ", " +
              std::to_string(display.x) + "x" + std::to_string(display.y) + ": " +
              editor.document()->source().document.at("keys").dump());
    near(clip.keys.front().materials[color].curves[3].outgoing, -.4, "RGBA alpha slope");
    std::array<AnimationMaterialValues, 2> samples;
    clip.sample_materials(animation_timebase, samples);
    near(samples[color][3], .9, "Independent cubic alpha midpoint");
    click("button:Fit curve and handles");
    const auto tangent = target("animation:tangent_out");
    const ImVec2 handle{(tangent.minimum.x + tangent.maximum.x) / 2,
                        (tangent.minimum.y + tangent.maximum.y) / 2};
    mouse(handle, false);
    mouse(handle, true);
    mouse({handle.x, handle.y + 15}, true);
    mouse({handle.x, handle.y + 15}, false);
    check(editor.document()->source().document != curved,
          "Actual material tangent drag did not commit a source transaction");
    editor.undo();
    check(editor.document()->source().document == curved, "Material tangent Undo lost tuple/lane");
    editor.redo();
    check(editor.document()->source().document != curved, "Material tangent Redo did not replay");
    editor.undo();
    frame();
    const auto cancel = target("animation:tangent_out");
    const ImVec2 cancel_handle{(cancel.minimum.x + cancel.maximum.x) / 2,
                               (cancel.minimum.y + cancel.maximum.y) / 2};
    mouse(cancel_handle, false);
    mouse(cancel_handle, true);
    mouse({cancel_handle.x, cancel_handle.y + 15}, true);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    mouse(cancel_handle, false);
    check(editor.document()->source().document == curved, "Material tangent Escape changed source");
    click("button:Flat tangents");
    near(editor.document()->source().clip().keys.front().materials[color].curves[3].outgoing, 0,
         "Material flat preset");
    editor.undo();
    frame();
    click("button:Linear tangents");
    near(editor.document()->source().clip().keys.front().materials[color].curves[3].outgoing, 0,
         "Material linear preset");
    editor.undo();
    frame();
    click("button:Smooth tangents");
    near(editor.document()->source().clip().keys.front().materials[color].curves[3].outgoing, 0,
         "Material smooth preset");
    editor.undo();
    frame();
    type("animation:key_time", "2");
    wait_for([&] { return test::ui_targets.at("button:Capture authored pose").enabled; },
             "Material capture resources did not become ready");
    click("button:Capture authored pose");
    click("button:Add / Update key at playhead");
    near(editor.document()->source().clip().keys.back().materials[roughness].values[0], .25,
         "Capture current authored override");
    near(editor.document()->source().clip().keys.back().materials[color].values[0], 1,
         "Capture current shared default");
    editor.undo();
    frame();
    check(editor.document()->source().document == curved, "Material capture Undo lost source");
    click("animation:material_remove:surface:roughnessFactor");
    check(editor.document()->source().clip().material_bindings.size() == 1 &&
              editor.document()->source().clip().material_bindings[0].key == "baseColorFactor" &&
              editor.document()->source().clip().keys.front().materials[0].curves[3].cubic,
          "Removing another track rebound the selected color tuple/lane");
    editor.undo();
    frame();
    check(editor.document()->source().document == curved,
          "Material track removal Undo lost source");
    locked = true;
    frame();
    check(!test::ui_targets.at("animation:material_value:surface:roughnessFactor").enabled &&
              !test::ui_targets.at("animation:material_property:surface:baseColorFactor").enabled &&
              !test::ui_targets.at("animation:curve_material_out").enabled,
          "Material Timeline controls stayed editable during Play");
    locked = false;
    editor.request_save();
    std::shared_ptr<const AssetCatalog> published;
    wait_for(
        [&] {
            published = editor.take_catalog();
            return bool(published);
        },
        "Material Timeline publication stalled");
    check(!editor.dirty() &&
              published->records().at(asset).metadata["forge.import"]["output_version"] == 5,
          "Material Timeline did not publish cooked5");
    const auto saved = editor.document()->source().document;
    editor.open(project, "Assets/Material.animation.json");
    check(editor.document()->source().document == saved,
          "Material Timeline save/reopen lost key identity, semantics or curves");
    editor.bind(scene, actor);
    frame();
    wait_for([&] { return test::ui_targets.at("button:Capture authored pose").enabled; },
             "Reopened material preview did not prepare");
    click("button:Play preview");
    auto invalid = parameters;
    invalid[0]["parameter"]["type"] = int(MaterialParameterType::Vector2);
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.mesh_renderer"},
                       {"field", "material_parameters"},
                       {"value", invalid}});
    frame();
    const auto rejected = scene.effective_document();
    const auto rejected_revision = scene.revision();
    check(
        editor.preview(rejected) == rejected && !editor.preview_active() &&
            scene.revision() == rejected_revision && editor.document()->source().document == saved,
        "Wrong semantic target partially previewed, retained an active preview or changed source");
    check(scene.undo() && scene.document() == authored,
          "Invalid preview target Undo lost authored intent");
    frame();
    wait_for([&] { return test::ui_targets.at("button:Capture authored pose").enabled; },
             "Corrected material preview did not prepare");
    const auto restored_revision = scene.revision();
    click("button:Play preview");
    check(editor.preview_active() &&
              editor.preview(scene.effective_document()) != scene.effective_document(),
          "Corrected material target could not preview again");
    editor.request_close();
    check(!editor.is_open() && scene.document() == authored &&
              scene.revision() == restored_revision,
          "Closing material Timeline changed authored Scene history");
    editor.open(project, "Assets/Material.animation.json");
    editor.bind(scene, actor);
    frame();
    wait_for([&] { return test::ui_targets.at("button:Capture authored pose").enabled; },
             "Material view did not prepare before workspace reset");
    editor = {};
    check(!editor.is_open() && scene.document() == authored &&
              scene.revision() == restored_revision,
          "Workspace reset failed to retire material editing ownership");
    editor.open(project, "Assets/Material.animation.json");
    editor.bind(scene, actor);
    frame();
    wait_for([&] { return test::ui_targets.at("button:Capture authored pose").enabled; },
             "Reset material view could not prepare again");
    const auto next_project = scratch / AssetId::generate().str();
    std::filesystem::create_directories(next_project);
    project.open_project(next_project, true);
    frame();
    check(!editor.is_open(), "Material preview ownership survived a project generation change");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need material Timeline scratch root");
        for (const float scale : {1.f, 1.5f, 2.f})
            for (const auto display : {ImVec2{1400, 1400}, ImVec2{960, 640}})
                exercise(argv[1], scale, display);
        std::cout << "Actual material Timeline input, typed tracks, copied preview, RGBA curves, "
                     "tangents, history, publication and Play lock PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
