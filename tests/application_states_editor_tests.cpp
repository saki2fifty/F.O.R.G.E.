#include "editor/application_state_controls.hpp"
#include "editor/application_states_editor.hpp"
#include "editor/source_document_automation.hpp"
#include "imgui_software_capture.hpp"
#include <chrono>
#include <forge/application_states.hpp>
#include <iostream>

namespace {
using namespace forge;
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& scratch, float scale, int height = 1500) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    auto source = ApplicationStatesDocument::create(
        project.writer_guard(), "Assets/Modes.appstates.json", [](AssetId id) {
            auto value = ApplicationStatesSource::create(id);
            value.document["opaque"] = {{"domain", "app"}, {"state", "menu"}};
            value.document["domains"].push_back(
                {{"key", "hud"},
                 {"computed", true},
                 {"initial", "hidden"},
                 {"states", Json::array({{{"key", "hidden"}}, {{"key", "visible"}}})},
                 {"rules",
                  Json::array(
                      {{{"state", "visible"},
                        {"all", Json::array({{{"domain", "app"}, {"state", "menu"}}})}}})}});
            return value;
        });
    const auto original = source->source().document;
    source.reset();
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Game modes"}})
                           .at("selected")
                           .get<std::string>();
    const auto authored = scene.snapshot();
    ApplicationStatesEditor editor;
    editor.open(project, "Assets/Modes.appstates.json");
    test::ImGuiSoftwareCapture capture(int(1200 * scale), int(height * scale));
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope context_scope(context);
    context.selection.select_entity(actor);
    test::observe_ui = true;
    bool locked = false;
    std::string message;
    const auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        // Native layout/input advances on every gesture. Rasterize only the
        // captured frame, rather than a multi-megapixel surface for each event.
        ImGui::Render();
    };
    const auto target = [&](const std::string& name) {
        const auto at = test::ui_targets.find(name);
        require(at != test::ui_targets.end(), "Missing state control " + name);
        const auto& b = at->second;
        const ImVec2 p{(b.minimum.x + b.maximum.x) / 2, (b.minimum.y + b.maximum.y) / 2};
        require(b.enabled && p.x >= b.clip_minimum.x && p.x <= b.clip_maximum.x &&
                    p.y >= b.clip_minimum.y && p.y <= b.clip_maximum.y,
                "Disabled/clipped state control " + name);
        return p;
    };
    const auto click = [&](const std::string& name) {
        frame();
        const auto p = target(name);
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    const auto text = [&](const char* value) {
        click("application_states:text");
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(value);
        frame();
    };
    const auto values = [&] { return editor.document()->source().document; };
    frame();
    frame();
    auto revision = editor.document()->revision();
    click("button:Rename state");
    text("intro");
    require(editor.has_unapplied_edits() && editor.document()->revision() == revision,
            "Typing committed a semantic rename before Apply");
    auto automation = AssetDocumentAutomation::bind(editor, project);
    require(automation.invoke("status", Json::object()).at("transient_draft"),
            "Agent missed native text draft");
    bool rejected = false;
    try {
        automation.invoke(
            "patch",
            {{"document_session", editor.document()->session()},
             {"document_revision", revision},
             {"patch", Json::array({{{"op", "add"}, {"path", "/agent"}, {"value", true}}})}});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && editor.document()->revision() == revision, "Agent overwrote a GUI draft");
    click("application_states:apply_text");
    require(values()["domains"][0]["initial"] == "intro" &&
                values()["domains"][1]["rules"][0]["all"][0]["state"] == "intro" &&
                values()["opaque"] == original["opaque"],
            "State rename missed conditions or rewrote opaque text");
    editor.undo();
    frame();
    require(values() == original, "GUI rename was not one source Undo step");
    editor.redo();
    frame();
    click("button:Rename domain");
    text("mode");
    click("application_states:apply_text");
    require(values()["domains"][0]["key"] == "mode" &&
                values()["domains"][1]["rules"][0]["all"][0]["domain"] == "mode" &&
                values()["opaque"] == original["opaque"],
            "Domain rename lost semantic/opaque boundaries");
    const auto valid = values();
    revision = editor.document()->revision();
    click("button:Rename state");
    text("bad key");
    click("application_states:apply_text");
    require(editor.has_unapplied_edits() && editor.document()->revision() == revision &&
                values() == valid,
            "Invalid GUI key changed source/history");
    click("button:Cancel");
    require(!editor.has_unapplied_edits(), "Cancel retained a native text draft");
    click("button:Edit state label");
    text("Unapplied label");
    editor.undo();
    frame();
    require(values() == valid && !editor.has_unapplied_edits(),
            "Undo did not cancel the text draft");
    click("button:Add state");
    text("bonus");
    click("application_states:apply_text");
    const auto parent_pointer = std::string("/domains/0/states/3/parent");
    click("application_states:combo:" + parent_pointer);
    click("application_states:choice:" + parent_pointer + ":playing");
    require(values()["domains"][0]["states"][3]["parent"] == "playing",
            "Parent selection did not use the source owner");
    click("application_states:domain");
    click("application_states:domain:hud");
    require(values()["domains"][1]["computed"] == true, "Domain selection changed source mode");
    click("application_states:computed");
    require(!values()["domains"][1]["computed"].get<bool>() &&
                !values()["domains"][1].contains("rules"),
            "Direct mode did not remove computed rules");
    editor.undo();
    frame();
    require(values()["domains"][1]["computed"] == true, "Mode switch Undo lost rules");
    locked = true;
    frame();
    require(!test::ui_targets.at("button:Save / Publish").enabled &&
                !test::ui_targets.at("button:Rename domain").enabled,
            "Play lock left source controls writable");
    locked = false;
    frame();
    click("button:Save / Publish");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        frame();
        require(std::chrono::steady_clock::now() < deadline, "State publication timed out");
    } while (editor.pending() || editor.dirty());
    click("application_states:attach");
    require(scene.entity(actor).has<ApplicationStates>() &&
                scene.entity(actor).get<ApplicationStates>().definition.id ==
                    editor.document()->source().asset(),
            "GUI attach did not assign the typed controller: " +
                automation.invoke("status", Json::object()).at("diagnostic").get<std::string>());
    scene.undo();
    require(scene.snapshot() == authored, "Attach was not one Scene Undo step");
    scene.redo();
    capture.render();
    capture.save(root / ("state-editor-" + std::to_string(int(scale * 100)) + ".png"));
    click("button:Edit domain label");
    text("Discarded");
    click("application_states:apply_text");
    editor.request_close();
    frame();
    click("button:Keep editing");
    require(editor.is_open(), "Keep editing closed source");
    editor.request_close();
    frame();
    click("button:Discard");
    require(!editor.is_open(), "Discard kept the dirty source open");
    test::observe_ui = false;
}
void exercise_controls(const std::filesystem::path& scratch, float scale) {
    WorldContext world;
    auto source = ApplicationStatesSource::create(AssetId::generate());
    source.document["domains"].push_back({{"key", "computed"},
                                          {"computed", true},
                                          {"initial", "shown"},
                                          {"states", Json::array({{{"key", "shown"}}})}});
    source.document["domains"].push_back(
        {{"key", "level"},
         {"initial", "warmup"},
         {"enabled_when", {{"domain", "app"}, {"state", "playing"}}},
         {"states", Json::array({{{"key", "warmup"}}})}});
    configure_application_states(world.world(), source.compiled());
    initialize_application_states(world.world());
    const auto status = application_states_status(world.world(), true);
    test::ImGuiSoftwareCapture capture(int(800 * scale), int(460 * scale));
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    test::observe_ui = true;
    bool pending = false, available = true;
    std::optional<ui::ApplicationStateControls::Request> selected;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Game controls");
        if (const auto value = ui::ApplicationStateControls::draw(status, available, pending, ""))
            selected = value;
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](const std::string& name) {
        frame();
        const auto at = test::ui_targets.find(name);
        require(at != test::ui_targets.end() && at->second.enabled,
                "Missing live state control " + name);
        const auto target = at->second;
        const ImVec2 position{(target.minimum.x + target.maximum.x) / 2,
                              (target.minimum.y + target.maximum.y) / 2};
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    frame();
    frame();
    click("application_states:live_header");
    require(!test::ui_targets.at("application_states:live_domain:computed").enabled &&
                !test::ui_targets.at("application_states:live_domain:level").enabled,
            "Computed/inactive domain was editable");
    click("application_states:live_domain:app");
    click("application_states:live_choice:app:playing");
    require(selected && selected->domain == "app" && selected->state == "playing" &&
                selected->revision == 1,
            "Native Game selection lost the copied domain revision");
    require(application_state_current(world.world(), "app") == "menu",
            "Drawing the controls mutated the runtime owner");
    pending = true;
    frame();
    require(!test::ui_targets.at("application_states:live_domain:app").enabled,
            "Pending state control stayed editable");
    pending = false;
    available = false;
    frame();
    require(!test::ui_targets.at("application_states:live_domain:app").enabled,
            "Unavailable control stayed editable");
    available = true;
    frame();
    capture.new_frame();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Game controls");
    (void)ui::ApplicationStateControls::draw(status, available, pending, "");
    ImGui::End();
    capture.render();
    capture.save(scratch / ("state-game-controls-" + std::to_string(int(scale * 100)) + ".png"));
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    int result = 0;
    try {
        require(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--manual-capture"),
                "Need state editor scratch root and optional --manual-capture");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        if (argc == 3) {
            exercise(std::filesystem::u8path(argv[1]), 1.f, 900);
            exercise_controls(std::filesystem::u8path(argv[1]), 1.f);
        } else
            for (const float scale : {1.f, 1.5f, 2.f}) {
                exercise(std::filesystem::u8path(argv[1]), scale);
                exercise_controls(std::filesystem::u8path(argv[1]), scale);
            }
        std::cout << "State editor native gestures, semantic renames, drafts/agent protection, "
                     "hierarchy, mode Undo, Play lock, publication, Scene attach and close passed "
                     "at 100/150/200%\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
