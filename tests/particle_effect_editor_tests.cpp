#include "asset_bytes.hpp"
#include "editor/particle_effect_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, bool small) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::filesystem::path source = "Assets/Original.particles.json";
    auto created = ParticleEffectDocument::create(project.writer_guard(), source);
    const auto original = created->source().document;
    created.reset();
    const auto disk = asset_detail::read_bytes(root / source, particle_effect_byte_limit);
    ParticleEffectEditor editor;
    editor.open(project, source);
    require(editor.document() && editor.dirty(), "Unpublished effect failed to open");
    const auto actor =
        authoring_command(scene, "entity.create",
                          {{"name", "Particle UI fixture"}, {"recipe", "render.particles"}})
            .at("selected")
            .get<std::string>();
    const auto initial_scene = scene.document();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = small ? ImVec2{780, 820} : ImVec2{1500, 1800};
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
    unsigned previews = 0, releases = 0;
    editor.draw_preview = [&](const auto& value, double time, const auto& catalog) {
        require(value.asset() == editor.document()->source().asset() && catalog &&
                    std::isfinite(time),
                "Particle preview received a different document/selection");
        require(scene.document() == initial_scene,
                "Isolated effect preview mutated the authored Scene");
        ++previews;
    };
    editor.release_preview = [&] { ++releases; };
    std::string message;
    bool locked = false;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - 20, io.DisplaySize.y - 20}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    frame();
    frame();
    auto point = [&](const std::string& id) {
        for (unsigned attempt = 0; attempt < 80; ++attempt) {
            require(test::ui_targets.contains(id), "Particle control missing: " + id +
                                                       " scale=" + std::to_string(scale) +
                                                       " compact=" + std::to_string(small));
            const auto box = test::ui_targets.at(id);
            require(box.enabled, "Particle control disabled: " + id);
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f);
                frame();
                return;
            }
            // Exercise the actual scrollbar input, including compact/high-DPI panes.
            io.AddMousePosEvent(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f);
            frame();
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 1.f : -1.f);
            frame();
        }
        throw std::runtime_error("Particle control could not be reached by scrolling: " + id);
    };
    auto click = [&](const std::string& id) {
        point(id);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto number = [&](const std::string& id, const char* value) {
        click(id);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    const auto revision = editor.document()->revision();
    number("particles:spawn_rate", "48");
    const auto edited = editor.document()->source().document;
    require(edited.at("spawn_rate") == 48 && editor.document()->revision() == revision + 1 &&
                asset_detail::read_bytes(root / source, particle_effect_byte_limit) == disk,
            "Numeric input did not commit exactly one isolated draft history entry");
    click("particles:undo");
    require(editor.document()->source().document == original, "Particle UI Undo failed");
    click("particles:redo");
    require(editor.document()->source().document == edited, "Particle UI Redo failed");
    number("particles:lifetime", "30");
    require(editor.document()->source().document == edited && !editor.diagnostic().empty(),
            "Over-capacity emission edited the document instead of reporting an error");
    number("particles:cone_angle", "40");
    require(editor.document()->source().document.at("cone_angle") == 40,
            "Particle cone control did not edit the effect");
    number("particles:preview_time", "1.25");
    require(editor.preview_time() == 1.25 && !editor.preview_playing(),
            "Preview seek did not pause at the requested time");
    const auto source_before_preview = editor.document()->source().document;
    click("particles:preview_play");
    require(editor.preview_playing(), "Particle preview Play did not start");
    const auto playing_time = editor.preview_time();
    for (unsigned i = 0; i < 5; ++i)
        frame();
    require(editor.preview_time() > playing_time, "Particle preview clock did not advance");
    click("particles:preview_play");
    const auto paused_time = editor.preview_time();
    frame();
    frame();
    require(editor.preview_time() == paused_time && !editor.preview_playing(),
            "Paused preview reads advanced emission age");
    number("particles:preview_time", "121");
    require(editor.preview_time() == paused_time && !editor.diagnostic().empty(),
            "Unsupported preview seek was accepted");
    click("particles:preview_reset");
    require(editor.preview_time() == 0 && !editor.preview_playing() &&
                editor.document()->source().document == source_before_preview,
            "Preview reset mutated effect values/history or retained playback");
    click("particles:preview_play");
    locked = true;
    frame();
    require(!editor.preview_playing() && !test::ui_targets.at("particles:save").enabled,
            "Locked document retained playback or enabled publication");
    locked = false;
    frame();
    click("particles:save");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (editor.pending() && std::chrono::steady_clock::now() < deadline);
    require(!editor.dirty() && editor.diagnostic().empty(),
            "Particle UI publication failed: " + editor.diagnostic());
    number("particles:spawn_rate", "1");
    number("particles:lifetime", "1");
    number("particles:capacity", "1");
    require(editor.document()->source().compiled().capacity == 1 && editor.diagnostic().empty(),
            "Inactive burst count prevented a valid capacity-one continuous effect");
    for (unsigned i = 0; i < 3; ++i)
        click("particles:undo");
    require(!editor.dirty(), "Undo of capacity controls retained source dirtiness");
    editor.draw_preview = {};
    click("particles:attach");
    require(scene.entity(actor).get<ParticleEmitter>().effect.id ==
                    editor.document()->source().asset() &&
                scene.undo() && scene.document() == initial_scene,
            "Particle effect assignment did not use Scene Undo");
    number("particles:size_start", "0.2");
    click("particles:undo");
    require(!editor.dirty(), "Undo to the published source retained a false rebuild requirement");
    click("particles:redo");
    require(editor.dirty(), "Redo of a source edit lost its draft dirtiness");
    editor.request_close();
    frame();
    frame();
    click("particles:close_cancel");
    require(editor.is_open() && editor.dirty() && editor.close_cancelled,
            "Dirty Particle close Cancel lost the document");
    editor.request_close();
    frame();
    frame();
    click("particles:close_discard");
    require(!editor.is_open() && previews > 0 && releases > 0,
            "Particle discard retained document/preview ownership");
    ImGui::DestroyContext();
    std::filesystem::remove_all(root);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need particle UI scratch root");
        const auto scratch = std::filesystem::absolute(argv[1]);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool small : {false, true})
                exercise(scratch, scale, small);
        std::cout
            << "Particle actual mouse/key controls at100/150/200%, full/compact panes, "
               "history, preview clock isolation, publication, attach and dirty close passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
