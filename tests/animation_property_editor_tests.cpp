#include "animation_timeline.hpp"
#include "authored_schema.hpp"
#include "imgui_software_capture.hpp"
#include "sprite_frame_picker.hpp"
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
enum class UiMode : std::int32_t { Forward = 1, Reverse = -1 };
struct UiMotion {
    float speed = 90;
    bool enabled = true;
    std::int64_t marker = 0;
    std::uint64_t score = 0;
    UiMode mode = UiMode::Forward;
    std::uint32_t flags = 3;
};
EngineModule motion_module() {
    EngineModule module;
    module.id = "project.ui_motion";
    module.dependencies = {"forge.transforms"};
    module.schemas = [](ModuleContext& context) {
        context.world.component<UiMode>()
            .constant("Forward", UiMode::Forward)
            .constant("Reverse", UiMode::Reverse);
        ecs_bitmask_desc_t mask{};
        mask.entity = context.world.entity("project.UiFlags");
        mask.constants[0].name = "Left";
        mask.constants[0].value = 1;
        mask.constants[1].name = "Right";
        mask.constants[1].value = 2;
        const auto mask_type = ecs_bitmask_init(context.world, &mask);
        check(mask_type != 0, "UI flag metadata registration failed");
        auto type = context.world.component<UiMotion>()
                        .member<float>("speed")
                        .member<bool>("enabled")
                        .member<std::int64_t>("marker")
                        .member<std::uint64_t>("score")
                        .member<UiMode>("mode")
                        .member(mask_type, "flags");
        detail::opt_in_authoring(context.world, type, "project.ui_motion", "project.ui_motion", 1,
                                 {{"speed", 90},
                                  {"enabled", true},
                                  {"marker", 0},
                                  {"score", 0},
                                  {"mode", 1},
                                  {"flags", 3}},
                                 "Gameplay");
    };
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.start = [](ModuleContext&) {
        throw std::runtime_error("Project C++ callbacks must stay outside copied sequence preview");
    };
    return module;
}
void exercise(const std::filesystem::path& scratch, float scale, ImVec2 display) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine(WorldRole::Authoring, false, {motion_module()});
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto actor =
        authoring_command(scene, "entity.create", {{"name", "Lens and light"}})["selected"]
            .get<std::string>();
    for (const char* component : {"forge.camera", "forge.light", "forge.audio_source",
                                  "forge.particle_emitter", "forge.sprite_renderer"})
        authoring_command(scene, "component.add", {{"entity", actor}, {"component", component}});
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "project.ui_motion"}});
    auto sprite = scene.entity(actor).get<SpriteRenderer>();
    sprite.columns = sprite.rows = 2;
    scene.entity(actor).set(sprite);
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    AnimationTimelineEditor editor;
    ui::SpriteFramePicker picker;
    editor.sprite_frames = [&](const auto& field, auto& value) {
        return picker.draw(field, value);
    };
    picker.image_source = [&](auto) {
        ui::SpriteSheetImage image;
        image.width = image.height = 32;
        image.revision = ResourceIdentity{};
        image.render = [](auto, auto, auto) {
            static int marker;
            return reinterpret_cast<ImTextureID>(&marker);
        };
        return image;
    };
    editor.open(project, "Assets/Properties.animation.json");
    editor.bind(scene, actor);
    bool locked = false;
    std::string message;
    auto frame = [&] {
        test::ui_targets.clear();
        picker.begin_frame();
        editor.poll(project, scene, locked, io.DeltaTime, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({5, 5}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({display.x - 10, display.y - 10}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        picker.after_ui();
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
        if (name == "button:Add / Update key at playhead") {
            const auto settled = test::ui_targets.at(name);
            check(p.x >= settled.minimum.x && p.x <= settled.maximum.x &&
                      p.y >= settled.minimum.y && p.y <= settled.maximum.y,
                  "Timeline Apply key moved after pointer hover");
        }
        mouse(p, true);
        if (name == "button:Add / Update key at playhead")
            check(GImGui->ActiveId == test::ui_targets.at(name).id,
                  "Timeline Apply key did not receive the pointer press");
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
    click("animation:property:forge.sprite_renderer.frame");
    check(editor.document()->source().document["version"] == 3 &&
              editor.document()->source().clip().properties == 8133,
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
    type("animation:value:forge.sprite_renderer.frame", "0");
    const auto before_frame_choice = editor.document()->source().document;
    click("sprite:choose-frame");
    const auto frame_key = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        frame();
    };
    frame_key(ImGuiKey_End);
    frame_key(ImGuiKey_Enter);
    check(!picker.open(), "Timeline chooser did not close after frame confirmation");
    check(editor.document()->source().document == before_frame_choice &&
              scene.document() == authored,
          "Visual Timeline frame choice published a key or changed the authored target");
    click("sprite:choose-frame");
    frame_key(ImGuiKey_Home);
    frame_key(ImGuiKey_Escape);
    check(!picker.open(), "Timeline chooser did not close after cancellation");
    check(editor.document()->source().document == before_frame_choice,
          "Cancelled Timeline frame navigation changed the clip");
    click("button:Add / Update key at playhead");
    const auto source = editor.document()->source();
    check(source.clip().keys.size() == 2,
          "Property UI did not create second key at scale " + std::to_string(scale) + " display " +
              std::to_string(display.x) + "x" + std::to_string(display.y) +
              "; keys=" + std::to_string(source.clip().keys.size()) +
              "; problems=" + std::to_string(context.problems.size()) +
              "; revision=" + std::to_string(editor.document()->revision()) + "; frame=" +
              std::to_string(
                  source.clip().keys.front().properties[unsigned(AnimationProperty::SpriteFrame)]) +
              "; message=" + message);
    check(std::abs(source.clip().keys.back().properties[0] - std::numbers::pi / 2) < 1e-7 &&
              source.clip().keys.back().properties[2] == 10 &&
              source.clip().keys.back().properties[6] == .25 &&
              source.clip().keys.back().properties[7] == 2 &&
              source.clip().keys.back().properties[8] == 2.5 &&
              source.clip().keys.back().properties[9] == .25 &&
              source.clip().keys.back().properties[10] == .5 &&
              source.clip().keys.back().properties[11] == .75 &&
              source.clip().keys.back().properties[unsigned(AnimationProperty::SpriteFrame)] == 3,
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
              editor.document()->source().clip().keys.back().properties[11] == 1 &&
              editor.document()->source().clip().keys.back().properties[unsigned(
                  AnimationProperty::SpriteFrame)] == 0,
          "Capture omitted authored camera field");
    editor.undo();
    const auto preview = editor.preview(scene.effective_document());
    check(preview != scene.effective_document() && scene.document() == authored &&
              scene.revision() == scene_revision &&
              preview["entities"][0]["components"]["forge.audio_source"]["gain"] == .25 &&
              preview["entities"][0]["components"]["forge.audio_source"]["pitch"] == 2 &&
              preview["entities"][0]["components"]["forge.particle_emitter"]["size_multiplier"] ==
                  2.5 &&
              preview["entities"][0]["components"]["forge.particle_emitter"]["tint_b"] == .75 &&
              preview["entities"][0]["components"]["forge.sprite_renderer"]["frame"]
                  .is_number_unsigned() &&
              preview["entities"][0]["components"]["forge.sprite_renderer"]["frame"] == 3,
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
    check(catalog->records().at(id).metadata["forge.import"]["output_version"] == 7 &&
              !editor.dirty(),
          "Timeline published wrong property cook");
    click("button:Attach animation to object");
    check(scene.entity(actor).get<TransformAnimator>().clip.id == id && scene.undo() &&
              scene.document() == authored,
          "Property attachment did not use Scene history");
    const auto first_ruler = target("animation:ruler");
    const ImVec2 first_point{first_ruler.minimum.x + 2, first_ruler.minimum.y + 36};
    mouse(first_point, false);
    mouse(first_point, true);
    mouse(first_point, false);
    frame();
    click("animation:curve_section");
    click("animation:curve_channel");
    check(!test::ui_targets.contains("animation:curve_channel_choice:14"),
          "Sprite step binding was offered as a curve/tangent channel");
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
              catalog->records().at(id).metadata["forge.import"]["output_version"] == 7,
          "Scalar curves did not publish through the existing document");
    editor.open(project, "Assets/Properties.animation.json");
    check(editor.document()->source().document == saved_curves,
          "Timeline save/reopen changed scalar curves or key identities");
    editor.bind(scene, actor);
    frame();
    click("animation:component_fields");
    click("animation:component_add:project.ui_motion:speed");
    check(editor.document()->source().clip().component_bindings.size() == 1 &&
              editor.document()->source().document.at("version") == 6,
          "Component field input did not add a versioned binding");
    const auto before_field_edit = editor.document()->source().document;
    type("animation:component_value:project.ui_motion:speed", "120");
    click("animation:component_cubic:project.ui_motion:speed");
    type("animation:component_out:project.ui_motion:speed", "5");
    click("button:Add / Update key at playhead");
    const auto field_clip = editor.document()->source().clip();
    check(field_clip.keys.front().component_values.front().value == 120 &&
              field_clip.keys.front().component_values.front().curve.cubic &&
              field_clip.keys.front().component_values.front().curve.outgoing == 5,
          "Actual component value/curve input did not commit the key");
    editor.undo();
    frame();
    check(editor.document()->source().document == before_field_edit,
          "Component key Undo lost its source identity or values");
    editor.redo();
    frame();
    check(editor.document()->source().clip().keys.front().component_values.front().value == 120,
          "Component key Redo failed");
    for (const char* name : {"enabled", "marker", "score", "mode", "flags"})
        click(std::string("animation:component_add:project.ui_motion:") + name);
    check(editor.document()->source().document.at("version") == 7,
          "Discrete component controls did not promote the source explicitly");
    const auto before_discrete = editor.document()->source().document;
    click("animation:component_value:project.ui_motion:enabled");
    type("animation:component_value:project.ui_motion:marker", "-9223372036854775808");
    type("animation:component_value:project.ui_motion:score", "18446744073709551615");
    click("animation:component_value:project.ui_motion:mode");
    click("property:choice:mode:-1");
    click("animation:component_value:project.ui_motion:flags");
    click("property:flag:flags:1");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    click("button:Add / Update key at playhead");
    const auto discrete_clip = editor.document()->source().clip();
    const auto& discrete_key = discrete_clip.keys.front();
    check(discrete_key.component_values[1].value == false &&
              discrete_key.component_values[2].value.get<std::int64_t>() == INT64_MIN &&
              discrete_key.component_values[3].value.get<std::uint64_t>() == UINT64_MAX &&
              discrete_key.component_values[4].value == -1 &&
              discrete_key.component_values[5].value == 2,
          "Actual Boolean/integer/enum controls changed precision or did not commit");
    editor.undo();
    frame();
    check(editor.document()->source().document == before_discrete,
          "Discrete key Undo changed identities or values");
    editor.redo();
    frame();
    locked = true;
    frame();
    check(!test::ui_targets.at("animation:component_value:project.ui_motion:speed").enabled,
          "Component key field remained editable during Play");
    check(!test::ui_targets.at("animation:component_value:project.ui_motion:marker").enabled &&
              !test::ui_targets.contains("animation:component_cubic:project.ui_motion:marker"),
          "Discrete field exposed editing during Play or a continuous curve control");
    locked = false;
    frame();
    check(scene.entity(actor).get<UiMotion>().speed == 90,
          "Timeline key editing changed the authored component");
    editor.request_save();
    catalog.reset();
    const auto field_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!catalog) {
        frame();
        catalog = editor.take_catalog();
        check(std::chrono::steady_clock::now() < field_deadline,
              "Component field publication stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto controller =
        authoring_command(scene, "entity.create",
                          {{"name", "Component sequence"}, {"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    auto sequence_config = scene.entity(controller).get<AnimationSequence>();
    sequence_config.duration = 2;
    sequence_config.loop = false;
    scene.entity(controller).set(sequence_config);
    TransformAnimator track;
    track.clip = {id};
    track.sequence = scene.reference(controller);
    scene.entity(actor).set(track);
    const auto scene_before_sequence = scene.document();
    {
        AnimationSequenceEditor sequence;
        sequence.open(project, scene, scene.reference(controller));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!sequence.frame_export_ready()) {
            sequence.poll(false, 0);
            check(std::chrono::steady_clock::now() < deadline,
                  "Copied component sequence preview stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto token = sequence.begin_frame_export();
        sequence.sample_frame_export(token, 1);
        const auto sampled = sequence.preview(scene.effective_document());
        const auto sequence_clip = editor.document()->source().clip();
        std::vector<Json> expected(sequence_clip.component_bindings.size());
        sequence_clip.sample_component_values(animation_timebase, expected);
        const auto row = std::find_if(sampled.at("entities").begin(), sampled.at("entities").end(),
                                      [&](const auto& value) { return value.at("id") == actor; });
        check(row != sampled.at("entities").end() &&
                  std::abs(row->at("components").at("project.ui_motion").at("speed").get<double>() -
                           float(expected.front().get<double>())) < 1e-6,
              "Sequence preview lost its copied component field projection");
        for (std::size_t i = 1; i < expected.size(); ++i)
            check(row->at("components")
                          .at("project.ui_motion")
                          .at(sequence_clip.component_bindings[i].field) == expected[i],
                  "Sequence preview changed a held Boolean/integer/enum value");
        sequence.end_frame_export(token);
    }
    check(scene.document() == scene_before_sequence &&
              scene.entity(actor).get<UiMotion>().speed == 90,
          "Sequence component preview changed authored state");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
void capture(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "project/Assets");
    test::ImGuiSoftwareCapture capture(1080, 1300);
    ui::style(1);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    EngineContext engine(WorldRole::Authoring, false, {motion_module()});
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root / "project", true);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Animated cube"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "project.ui_motion"}});
    auto document =
        TransformAnimationDocument::create(project.writer_guard(), "Assets/Speed.animation.json");
    document->edit(document->revision(), "Component field capture", [&](auto& json) {
        TransformAnimationSource source{json};
        const auto& codec = engine.world().authored_codecs().front();
        const AnimationComponentBinding binding{codec.key(), "speed", codec.stamp()};
        source.add_component_field(binding, 90);
        const auto add = [&](const char* name, AnimationComponentValueType kind,
                             const Json& value) {
            source.add_component_field({codec.key(), name, codec.stamp(), kind}, value);
        };
        add("enabled", AnimationComponentValueType::Boolean, true);
        add("marker", AnimationComponentValueType::SignedInteger, INT64_MIN);
        add("score", AnimationComponentValueType::UnsignedInteger, UINT64_MAX);
        add("mode", AnimationComponentValueType::SignedInteger, -1);
        add("flags", AnimationComponentValueType::UnsignedInteger, 3);
        const auto last = source.set_key(2 * animation_timebase, {});
        source.set_component_value(last, binding.component, binding.field, 180);
        source.set_component_curve(source.clip().keys.front().id, binding.component, binding.field,
                                   {true, 0, 60});
        json = source.document;
    });
    document->save();
    document.reset();
    AnimationTimelineEditor editor;
    editor.open(project, "Assets/Speed.animation.json");
    editor.bind(scene, actor);
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    std::string message;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, scene, false, io.DeltaTime, message);
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1060, 1280}, ImGuiCond_Always);
        editor.draw(project, scene, false);
        capture.render();
    };
    frame();
    frame();
    const auto box = test::ui_targets.at("animation:component_fields");
    io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    const auto reveal = [&](const std::string& name) {
        check(test::ui_targets.contains(name), "Capture control missing: " + name);
        auto rect = test::ui_targets.at(name);
        if (rect.minimum.y < rect.clip_minimum.y || rect.maximum.y > rect.clip_maximum.y) {
            auto* window = ImGui::FindWindowByName("###Animation Timeline");
            check(window != nullptr, "Capture Timeline window missing");
            ImGui::SetScrollY(window, window->Scroll.y + (rect.minimum.y + rect.maximum.y) / 2 -
                                          (rect.clip_minimum.y + rect.clip_maximum.y) / 2);
            frame();
            frame();
        }
        return test::ui_targets.at(name);
    };
    // Binding a preview object pre-fills editing fields from authored values.
    // Select the saved key through actual input to show its stored discrete values.
    auto* timeline = ImGui::FindWindowByName("###Animation Timeline");
    check(timeline != nullptr, "Capture Timeline window missing");
    ImGui::SetScrollY(timeline, timeline->ScrollMax.y);
    frame();
    frame();
    const auto key = editor.document()->source().clip().keys.front().id;
    const auto selected = reveal("animation:key:" + key.str());
    io.AddMousePosEvent((selected.minimum.x + selected.maximum.x) / 2,
                        (selected.minimum.y + selected.maximum.y) / 2);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    reveal("animation:component_out:project.ui_motion:speed");
    capture.save(root / "animation-component-fields.png");
    reveal("animation:component_value:project.ui_motion:flags");
    capture.save(root / "animation-discrete-fields.png");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[2]) == "--capture") {
            capture(std::filesystem::absolute(argv[1]));
            return 0;
        }
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
