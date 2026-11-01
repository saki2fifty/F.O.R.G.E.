#include "editor/source_document_automation.hpp"
#include "editor/ui_localization_editor.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& scratch, float scale) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::filesystem::path path = "Assets/Messages.localization.json";
    auto created = UiLocalizationDocument::create(project.writer_guard(), path, [](AssetId id) {
        auto source = UiLocalizationSource::create(id);
        source.document["vendor"] = {{"keep", true}};
        return source;
    });
    const auto original = created->source().document;
    created.reset();
    const auto actor = authoring_command(scene, "entity.create", {{"name", "UI controller"}})
                           .at("selected")
                           .get<std::string>();
    const auto authored = scene.document();
    UiLocalizationEditor editor;
    editor.open(project, path);
    require(editor.document() && editor.dirty(), "Unpublished localization did not open");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {2400, 2600};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.selection.select_entity(actor);
    std::string message;
    bool locked = false;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({2300, 2500}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    auto target = [&](const std::string& id) {
        const auto at = test::ui_targets.find(id);
        require(at != test::ui_targets.end(), "Missing localization control " + id);
        const auto b = at->second;
        const ImVec2 center{(b.minimum.x + b.maximum.x) / 2, (b.minimum.y + b.maximum.y) / 2};
        require(b.enabled && center.x >= b.clip_minimum.x && center.x <= b.clip_maximum.x &&
                    center.y >= b.clip_minimum.y && center.y <= b.clip_maximum.y,
                "Disabled/clipped localization control " + id);
        return center;
    };
    auto mouse = [&](ImVec2 p, bool down) {
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    };
    auto click = [&](const std::string& id) {
        frame();
        const auto p = target(id);
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        frame();
    };
    auto text = [&](const std::string& id, const char* value) {
        click(id);
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
    auto blur = [&] {
        mouse({2200, 2400}, true);
        mouse({2200, 2400}, false);
        frame();
    };
    frame();
    frame();
    const auto revision = editor.document()->revision();
    text("ui_localization:text", "Start <b>literal</b> & {{counter}}");
    require(editor.has_unapplied_edits() && editor.document()->revision() == revision,
            "Typing committed before the text gesture ended");
    {
        auto access = AssetDocumentAutomation::bind(editor, project);
        require(access.invoke("status", Json::object()).at("transient_draft"),
                "Automation did not observe the active native text draft");
        bool rejected = false;
        try {
            access.invoke(
                "patch",
                {{"document_session", editor.document()->session()},
                 {"document_revision", revision},
                 {"patch",
                  Json::array({{{"op", "add"}, {"path", "/agent_edit"}, {"value", true}}})}});
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && editor.document()->revision() == revision,
                "An agent edited through an unapplied GUI text draft");
    }
    blur();
    require(!editor.has_unapplied_edits() && editor.document()->revision() == revision + 1 &&
                editor.document()->source().compiled().message("menu_play", "en").text ==
                    "Start <b>literal</b> & {{counter}}",
            "A text gesture was not one literal-message transaction");
    editor.undo();
    frame();
    require(editor.document()->source().document == original, "Undo lost original/opaque data");
    text("ui_localization:new_locale", "de");
    click("button:Add language");
    require(editor.document()->source().compiled().locales.contains("de"),
            "Language was not added");
    text("ui_localization:label", "Deutsch");
    blur();
    require(editor.document()->source().document.at("locales").at("de").at("messages").empty(),
            "Changing a language label inserted an empty translation");
    text("ui_localization:text", "Spielen");
    blur();
    require(editor.document()->source().compiled().message("menu_play", "de").text == "Spielen",
            "Translation was not edited");
    click("button:Use default text");
    require(editor.document()->source().compiled().message("menu_play", "de").fallback,
            "Removing translation did not restore fallback");
    editor.undo();
    frame();
    click("button:Make default");
    require(editor.document()->source().compiled().default_locale == "de",
            "Default did not change");
    editor.undo();
    frame();
    text("ui_localization:new_key", "menu_start");
    click("button:Rename key");
    require(editor.document()->source().compiled().message("menu_start", "de").text == "Spielen" &&
                !editor.document()
                     ->source()
                     .document.at("locales")
                     .at("fr")
                     .at("messages")
                     .contains("menu_play"),
            "Key rename lost translations or left old keys");
    editor.undo();
    frame();
    text("ui_localization:new_key", "bad key");
    const auto before_invalid = editor.document()->source().document;
    const auto invalid_revision = editor.document()->revision();
    click("button:Rename key");
    require(editor.document()->source().document == before_invalid &&
                editor.document()->revision() == invalid_revision,
            "Invalid key partially committed");
    text("ui_localization:new_key", "menu_quit");
    click("button:Add message");
    text("ui_localization:text", "Quit");
    blur();
    require(editor.document()->source().compiled().message("menu_quit", "en").text == "Quit",
            "New message could not be edited");
    click("button:Remove message");
    require(!editor.document()
                 ->source()
                 .document.at("locales")
                 .at("en")
                 .at("messages")
                 .contains("menu_quit"),
            "Message was not removed");
    editor.undo();
    frame();
    locked = true;
    frame();
    require(!test::ui_targets.at("ui_localization:text").enabled &&
                !test::ui_targets.at("button:Add language").enabled,
            "Play lock allowed localization source edits");
    locked = false;
    frame();
    require(scene.document() == authored, "Source editing changed Scene history");
    // An agent edit while an unchanged text field has focus must retire its
    // native input buffer; the next user gesture starts from the new source.
    click("ui_localization:text");
    auto access = AssetDocumentAutomation::bind(editor, project);
    access.invoke("patch", {{"document_session", editor.document()->session()},
                            {"document_revision", editor.document()->revision()},
                            {"patch", Json::array({{{"op", "replace"},
                                                    {"path", "/locales/en/messages/menu_play"},
                                                    {"value", "From agent"}}})}});
    frame();
    click("ui_localization:text");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_End, true);
    frame();
    io.AddKeyEvent(ImGuiKey_End, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    io.AddInputCharactersUTF8("!");
    frame();
    blur();
    require(editor.document()->source().compiled().message("menu_play", "en").text == "From agent!",
            "A focused native text buffer overwrote an agent's newer source edit");
    editor.undo();
    editor.undo();
    frame();
    editor.request_save();
    frame();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    frame();
    require(!editor.dirty() && bool(editor.take_catalog()), "Localization Save / Publish failed");
    const auto saved = editor.document()->source().document;
    click("button:Attach to selected object");
    require(scene.entity(actor).has<UiLocalization>() &&
                scene.entity(actor).get<UiLocalization>().catalog.id ==
                    editor.document()->source().asset(),
            "Published catalog was not attached to the selected Scene object");
    scene.undo();
    require(scene.document() == authored, "Attach was not one Scene Undo step");
    text("ui_localization:text", "Unsaved");
    editor.request_save();
    frame();
    while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    frame();
    require(!editor.dirty() &&
                editor.document()->source().compiled().message("menu_play", "en").text == "Unsaved",
            "Save did not flush the active text draft");
    editor.request_close();
    frame();
    require(!editor.is_open(), "Published localization did not close");
    editor.open(project, path);
    frame();
    frame();
    text("ui_localization:text", "Discard this");
    blur();
    editor.request_close();
    frame();
    frame();
    click("button:Keep editing");
    require(editor.is_open() && editor.close_cancelled, "Pending close could not be cancelled");
    editor.request_close();
    frame();
    frame();
    click("button:Discard");
    require(!editor.is_open(), "Dirty source could not discard");
    editor.open(project, path);
    frame();
    require(editor.document()->source().compiled().message("menu_play", "en").text == "Unsaved" &&
                editor.document()->source().document.at("vendor") == saved.at("vendor"),
            "Discard wrote its draft or lost opaque data");
    editor.request_close();
    frame();
    ImGui::DestroyContext();
}
void capture_editor(const std::filesystem::path& gallery, const std::filesystem::path& output) {
    const auto root = output / ("Capture-" + AssetId::generate().str());
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    UiLocalizationEditor editor;
    editor.open(project, "Assets/UI/Localization/Messages.localization.json");
    require(editor.document() && !editor.dirty(), "Capture requires the published Gallery catalog");
    const auto source = editor.document()->source().document, authored = scene.document();
    test::ImGuiSoftwareCapture capture(1180, 1020);
    ui::style(1);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    for (unsigned frame = 0; frame < 5; ++frame) {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1160, 1000}, ImGuiCond_Always);
        editor.draw(project, scene, false);
        capture.render();
    }
    for (const auto* key : {"ui_localization:locale", "ui_localization:key", "ui_localization:text",
                            "button:Save / Publish", "button:Attach to selected object"}) {
        const auto& b = test::ui_targets.at(key);
        require(b.enabled && b.minimum.x >= b.clip_minimum.x && b.maximum.x <= b.clip_maximum.x &&
                    b.minimum.y >= b.clip_minimum.y && b.maximum.y <= b.clip_maximum.y,
                std::string("Manual capture has a disabled/clipped control: ") + key);
    }
    capture.save(output / "ui-localization-editor.png");
    require(editor.document()->source().document == source && !editor.dirty() &&
                scene.document() == authored,
            "Capture altered source or Scene values");
    editor.request_close();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--capture") {
            capture_editor(std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]));
            return 0;
        }
        require(argc == 2, "Need localization editor scratch root");
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(std::filesystem::u8path(argv[1]), scale);
        std::cout << "Localization source gestures/history/validation/Play "
                     "lock/publication/attach/close passed at 100/150/200%\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
