#include "editor/render_texture_editor.hpp"
#include "editor/source_document_automation.hpp"
#include "imgui_software_capture.hpp"
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale,
              const std::filesystem::path& image = {}) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto scene_before = scene.document();
    const std::filesystem::path path = "Assets/Monitor.rendertexture.json";
    auto document = RenderTextureDocument::create(project.writer_guard(), path, [](AssetId id) {
        auto value = RenderTextureSource::create(id);
        value.document["vendor"] = {{"keep", "opaque source data"}};
        return value;
    });
    const auto initial = document->source().document;
    document.reset();
    RenderTextureEditor editor;
    editor.open(project, path);
    check(editor.document() && editor.dirty(), "Unpublished camera output did not open");
    const int capture_height = image.empty() ? 740 : 360;
    test::ImGuiSoftwareCapture capture(1080, capture_height);
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope context_scope(context);
    std::string message;
    bool locked = false;
    const auto frame = [&] {
        editor.poll(project, message);
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1064, float(capture_height - 16)}, ImGuiCond_Always);
        // Match the editor host's pinned numeric input policy.
        ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);
        editor.draw(project, scene, locked);
        ImGui::PopItemFlag();
        capture.render();
    };
    const auto point = [&](const std::string& id, float fraction = .5f) {
        const auto at = test::ui_targets.find(id);
        check(at != test::ui_targets.end(), "Missing camera output control: " + id);
        const auto& b = at->second;
        const ImVec2 p{b.minimum.x + (b.maximum.x - b.minimum.x) * fraction,
                       (b.minimum.y + b.maximum.y) * .5f};
        check(b.enabled && p.x >= b.clip_minimum.x && p.x <= b.clip_maximum.x &&
                  p.y >= b.clip_minimum.y && p.y <= b.clip_maximum.y,
              "Disabled or clipped camera output control: " + id);
        return p;
    };
    const auto click_at = [&](ImVec2 p) {
        io.AddMousePosEvent(p.x, p.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    const auto click = [&](const std::string& id) {
        frame();
        click_at(point(id));
    };
    const auto width = [&](const char* value) {
        frame();
        click_at(point("render-texture:width", .15f));
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
    frame();
    frame();
    const auto revision = editor.document()->revision();
    width("128");
    if (!editor.has_unapplied_edits() || editor.document()->revision() != revision) {
        capture.save(scratch / "render-texture-size-failure.png");
        const auto& bounds = test::ui_targets.at("render-texture:width");
        std::cerr << "Size input bounds " << bounds.minimum.x << ',' << bounds.minimum.y << " -> "
                  << bounds.maximum.x << ',' << bounds.maximum.y << "; active "
                  << ImGui::GetActiveID() << "; source "
                  << editor.document()->source().document.dump() << '\n';
    }
    check(editor.has_unapplied_edits() && editor.document()->revision() == revision,
          "Typing image dimensions bypassed the source gesture boundary");
    auto automation = AssetDocumentAutomation::bind(editor, project);
    bool rejected = false;
    try {
        automation.invoke(
            "patch",
            {{"document_session", editor.document()->session()},
             {"document_revision", revision},
             {"patch", Json::array({{{"op", "replace"}, {"path", "/width"}, {"value", 64}}})}});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && editor.document()->source().document == initial,
          "Automation edited through an unapplied size draft");
    editor.undo();
    frame();
    check(!editor.has_unapplied_edits() && editor.document()->source().document == initial,
          "Undo failed to cancel active numeric input");
    width("256");
    click("button:Apply dimensions");
    check(editor.document()->revision() == revision + 1 &&
              editor.document()->source().compiled() == RenderTextureData{256, 512},
          "Apply did not record one complete dimension transaction");
    const auto edited = editor.document()->source().document;
    editor.undo();
    frame();
    check(editor.document()->source().document == initial, "Source Undo lost original values");
    editor.redo();
    frame();
    check(editor.document()->source().document == edited,
          "Source Redo lost dimensions or extensions");
    const auto valid_revision = editor.document()->revision();
    width("2049");
    click("button:Apply dimensions");
    check(editor.has_unapplied_edits() && editor.document()->revision() == valid_revision &&
              editor.document()->source().document == edited,
          "Invalid dimension changed source or discarded its correction draft");
    click("button:Cancel dimensions");
    locked = true;
    frame();
    check(!test::ui_targets.at("render-texture:width").enabled &&
              !test::ui_targets.at("render-texture:height").enabled,
          "Locked source dimensions remained editable");
    locked = false;
    editor.request_save();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (editor.dirty()) {
        frame();
        check(std::chrono::steady_clock::now() < deadline,
              "Camera output publication did not complete: " + message);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    frame();
    const auto catalog = AssetCatalog::open_project(root);
    check(catalog.records().at(editor.document()->source().asset()).type ==
                  RenderTextureAsset::type &&
              scene.document() == scene_before && editor.document()->source().document == edited,
          "Source publication mutated the Scene, identity or opaque source data");
    if (!image.empty())
        capture.save(image);
    editor.request_close();
    check(!editor.is_open(), "Clean output editor did not close");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 4, "Need editor scratch root, optionally --capture image.png");
        const auto scratch = std::filesystem::absolute(argv[1]);
        if (argc == 4) {
            check(std::string_view(argv[2]) == "--capture", "Unknown capture option");
            exercise(scratch, 1, std::filesystem::absolute(argv[3]));
        } else {
            exercise(scratch, 1);
            exercise(scratch, 1.5f);
        }
        std::cout
            << "Render texture production controls, source history, locks and publication passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
