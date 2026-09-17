#pragma once
#include "authoring.hpp"
#include "content.hpp"
#include "hierarchy.hpp"
#include "scene_tools.hpp"
#include "view_state.hpp"
void require(bool condition, const char* message);
inline forge::Json authoring_fixture() {
    return {{"version", 2},
            {"asset_id", "44444444-4444-4444-8444-444444444444"},
            {"entities",
             forge::Json::array(
                 {{{"id", "11111111-1111-4111-8111-111111111111"},
                   {"name", "Front"},
                   {"components",
                    {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 0}, {"extra", "keep"}}}}}},
                  {{"id", "22222222-2222-4222-8222-222222222222"},
                   {"name", "Child"},
                   {"parent", "11111111-1111-4111-8111-111111111111"},
                   {"components", {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 3}}}}}},
                  {{"id", "33333333-3333-4333-8333-333333333333"},
                   {"name", "Other"},
                   {"components", {{"forge.position", {{"x", 4}, {"y", 1}, {"z", 0}}}}}}})}};
}
inline void test_authoring() {
    forge::EditorCamera camera;
    const auto original = authoring_fixture();
    require(forge::pick_block(original, camera, 400, 300, 800, 600) ==
                "11111111-1111-4111-8111-111111111111",
            "Pick did not choose nearest block");
    require(forge::pick_block(original, camera, 0, 0, 800, 600).empty(),
            "Background pick did not clear");
    require(forge::pick_block(original, camera, -1, 300, 800, 600).empty(), "Picked outside image");
    auto hidden = original;
    hidden["entities"][0]["prefab"] = true;
    require(forge::pick_block(hidden, camera, 400, 300, 800, 600) ==
                "22222222-2222-4222-8222-222222222222",
            "Prefab was picked");
    camera.orbit(100, -40);
    const auto center = forge::project_point(camera, {4, 1, 0}, 900, 700);
    require(center && forge::pick_block(original, camera, (*center)[0], (*center)[1], 900, 700) ==
                          "33333333-3333-4333-8333-333333333333",
            "Rotated projection/picking disagree");
    const auto begin = forge::project_point(camera, {0, 1, 0}, 900, 700);
    const auto end = forge::project_point(camera, {0, 1, 2}, 900, 700);
    require(begin && end, "Axis test points unavailable");
    const auto movement = forge::axis_drag(camera, {0, 1, 0}, 2, *begin, *end, 900, 700);
    require(movement && std::abs(*movement - 2) < 0.0001f,
            "Oblique axis drag is not perspective-correct");
    require(!forge::project_point(forge::EditorCamera{}, {0, 1, -10}, 800, 600),
            "Point behind eye projected");
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    scene.reset(original);
    forge::MoveGesture move;
    require(move.begin(scene, "11111111-1111-4111-8111-111111111111", 0), "Move did not start");
    move.update({1.3f, 9, 9}, true, 0.5f);
    require(move.position() == forge::Vec3{1.5f, 1, 0} && scene.document() == original,
            "Move preview changed authoring or failed axis snap");
    require(move.preview(original)["entities"][0]["components"]["forge.position"]["extra"] ==
                "keep",
            "Move discarded unknown position fields");
    require(move.commit(scene), "Move did not commit");
    require(scene.undo() && scene.document() == original && !scene.undo(),
            "Move was not exactly one undo command");
    scene.redo();
    const auto before_cancel = scene.document();
    move.begin(scene, "11111111-1111-4111-8111-111111111111", -1);
    move.update({-2, 3, 1}, false, 1);
    move.cancel();
    require(scene.document() == before_cancel, "Cancel changed authored state");
    move.begin(scene, "11111111-1111-4111-8111-111111111111", 1);
    scene.rename_entity("11111111-1111-4111-8111-111111111111", "Changed");
    bool rejected = false;
    try {
        move.commit(scene);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && !move.active(), "Stale move overwrote scene edits");
    const auto matches = forge::ui::hierarchy_matches(original, "CHILD");
    require(matches.contains("11111111-1111-4111-8111-111111111111") &&
                matches.contains("22222222-2222-4222-8222-222222222222") &&
                !matches.contains("33333333-3333-4333-8333-333333333333"),
            "Hierarchy filter lost ancestors or case matching");
    camera = {};
    camera.fly_speed = 10;
    const auto eye = camera.eye();
    camera.fly(0, 1, 0.1f);
    require(std::abs(camera.eye()[2] - eye[2] - 1) < 0.0001f, "Flight speed preference ignored");
    const auto root = std::filesystem::current_path() / "authoring-project-tests";
    if (std::filesystem::exists(root))
        throw std::runtime_error("Authoring test folder already exists");
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{root};
    forge::SceneDocument::create_project(root / "Game", "Game");
    forge::SceneDocument document(scene);
    document.open_project(root / "Game");
    forge::save_view(document, camera);
    auto restored = forge::EditorCamera{};
    require(forge::restore_view(document, restored) && restored.target == camera.target,
            "View bookmark round trip failed");
    forge::atomic_write(root / "Game/.forge/editor-views.json", "{}");
    const auto old = restored.target;
    rejected = false;
    try {
        forge::restore_view(document, restored);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && restored.target == old, "Invalid bookmark changed camera");
    forge::atomic_write(root / "Game/.forge/internal.json", "{}");
    forge::atomic_write(root / "Game/Scenes/second.json", "{}");
    const auto files = forge::scene_files(root / "Game");
    require(files.size() == 1, "Browser included non-scene JSON, manifest or internal data");
    forge::atomic_write(root / "Game/Scenes/custom.json", authoring_fixture().dump());
    require(forge::scene_files(root / "Game").size() == 2,
            "Browser excluded a legacy custom JSON scene");
}
inline void test_authoring_input(float scale) {
    ImGui::CreateContext();
    forge::ui::style(scale);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 700};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    scene.reset(authoring_fixture());
    forge::EditorCamera camera;
    forge::ui::SceneTools tools;
    std::string selected, status;
    ImVec2 origin;
    const ImVec2 size{800, 500};
    auto frame = [&](bool enabled = true) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({900, 650});
        ImGui::Begin("Authoring test");
        origin = ImGui::GetCursorScreenPos();
        forge::ui::ViewportInput input;
        forge::ui::camera_controls(camera, size, enabled, &input);
        tools.input(scene, camera, selected, origin, size, input, enabled, status);
        tools.draw(tools.move.preview(scene.document()), camera, selected, origin, size, enabled);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    io.AddMousePosEvent(origin.x + 400, origin.y + 250);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    require(selected == "11111111-1111-4111-8111-111111111111", "LMB did not pick nearest entity");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Q, true);
    frame();
    require(!tools.move_tool, "Q did not switch to Select tool");
    io.AddKeyEvent(ImGuiKey_Q, false);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    require(!tools.move.active() && selected == "11111111-1111-4111-8111-111111111111",
            "Select tool acquired a hidden move handle");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    io.AddKeyEvent(ImGuiKey_W, true);
    frame();
    require(!tools.move_tool, "RMB+W flight changed the active object tool");
    io.AddKeyEvent(ImGuiKey_W, false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    frame();
    camera = {};
    io.AddKeyEvent(ImGuiKey_W, true);
    frame();
    require(tools.move_tool, "W did not restore selected-object move handles");
    io.AddKeyEvent(ImGuiKey_W, false);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    require(tools.move.active(), "Center handle did not acquire drag");
    io.AddMousePosEvent(origin.x + 450, origin.y + 230);
    frame();
    require(scene.document() == authoring_fixture(), "Mouse drag leaked uncommitted scene edits");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    require(!tools.move.active() &&
                (*forge::entity_position(scene.document(),
                                         "11111111-1111-4111-8111-111111111111"))[0] > 0,
            "Mouse release did not apply move");
    require(scene.undo() && scene.document() == authoring_fixture(), "Mouse drag undo failed");
    io.AddMousePosEvent(origin.x + 400, origin.y + 250);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMousePosEvent(origin.x + 450, origin.y + 250);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    require(!tools.move.active() && scene.document() == authoring_fixture(),
            "Escape did not cancel drag");
    io.AddKeyEvent(ImGuiKey_Escape, false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    io.AddMousePosEvent(origin.x + 400, origin.y + 250);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    frame(false);
    require(!tools.move.active(), "Focus loss did not cancel move");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    io.AddMousePosEvent(origin.x + 400 + 60 * scale, origin.y + 250);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    require(tools.move.active() && tools.move.axis() == 0,
            "X handle did not acquire world-axis drag");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddMousePosEvent(origin.x + 445 + 60 * scale, origin.y + 250);
    frame();
    const auto snapped = tools.move.position();
    require(snapped[0] == std::round(snapped[0]) && snapped[1] == 1 && snapped[2] == 0,
            "Temporary snap changed other axes");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    require(scene.undo() && scene.document() == authoring_fixture(), "Axis drag undo failed");
    io.AddMousePosEvent(origin.x + 20, origin.y + 20);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    require(selected.empty(), "Background click did not clear selection");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    io.AddMousePosEvent(origin.x + 400, origin.y + 250);
    frame(false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(false);
    require(selected.empty(), "Disabled authoring still selected a block");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    ImGui::DestroyContext();
}
