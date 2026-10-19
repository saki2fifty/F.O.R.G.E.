#include "editor/component_inspector.hpp"
#include "editor/creation_menu.hpp"
#include "editor/material_editor.hpp"
#include "sprite_material_profile.hpp"
#include <chrono>
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
void exercise(const std::filesystem::path& scratch, float scale) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1200, 1000};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    MaterialEditor material;
    ComponentInspector inspector;
    ui::EditorActions actions;
    std::string selected, message;
    for (const auto& recipe : entity_recipes())
        actions.entries.push_back({"Create / " + recipe.label, recipe.label, "", "Create entity",
                                   true, [&, id = recipe.id] {
                                       selected = authoring_command(scene, "entity.create",
                                                                    {{"recipe", id}})
                                                      .at("selected")
                                                      .get<std::string>();
                                   }});
    bool at_target = false, content = true, live = false;
    EngineContext runtime_engine(WorldRole::Runtime);
    Scene runtime(runtime_engine.world());
    auto frame = [&] {
        material.poll(project, message);
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({5, 5}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1190, 990}, ImGuiCond_Always);
        ImGui::Begin(content ? "Content" : "Inspector", nullptr, ImGuiWindowFlags_MenuBar);
        if (content) {
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Entity")) {
                    ui::creation_menu(actions, at_target);
                    ImGui::EndMenu();
                }
                FORGE_UI_PROBE("test:entity-menu");
                ImGui::EndMenuBar();
            }
            material.content(project, false);
        } else if (live) {
            inspector.draw_runtime(runtime.effective_document(), runtime.schema(), root, selected,
                                   [&](const auto& type, const auto& property, const auto& value) {
                                       runtime.tune_runtime_property(selected, type, property,
                                                                     value);
                                       return true;
                                   });
        } else
            inspector.draw(scene, project, selected);
        ImGui::End();
        material.draw(project, false);
        ImGui::Render();
    };
    auto target = [&](const std::string& key) {
        frame();
        check(test::ui_targets.contains(key), "Missing Sprite widget: " + key);
        auto box = test::ui_targets.at(key);
        const auto visible_height = [&] {
            return std::min(box.maximum.y, box.clip_maximum.y) -
                   std::max(box.minimum.y, box.clip_minimum.y);
        };
        if (visible_height() < ImGui::GetFontSize()) {
            auto* window = ImGui::FindWindowByName(content ? "Content" : "Inspector");
            check(window, "Missing Inspector window");
            // Live fields are in an automatically sized child; scroll its outer Inspector.
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) *
                                                             .5f);
            frame();
            frame();
            box = test::ui_targets.at(key);
        }
        // ImGui Selectable deliberately extends its hit rectangle into item
        // spacing, including beyond a child clip at the first/last row. Click
        // the visible intersection; reject a clipped body or unusably narrow hit.
        const auto visible_width = std::min(box.maximum.x, box.clip_maximum.x) -
                                   std::max(box.minimum.x, box.clip_minimum.x);
        check(box.enabled && visible_width >= std::min(40 * scale, box.maximum.x - box.minimum.x) &&
                  visible_height() >= ImGui::GetFontSize(),
              "Sprite widget clipped/disabled: " + key + " rect " + std::to_string(box.minimum.x) +
                  "," + std::to_string(box.minimum.y) + ".." + std::to_string(box.maximum.x) + "," +
                  std::to_string(box.maximum.y) + " clip " + std::to_string(box.clip_minimum.x) +
                  "," + std::to_string(box.clip_minimum.y) + ".." +
                  std::to_string(box.clip_maximum.x) + "," + std::to_string(box.clip_maximum.y));
        box.minimum = {std::max(box.minimum.x, box.clip_minimum.x),
                       std::max(box.minimum.y, box.clip_minimum.y)};
        box.maximum = {std::min(box.maximum.x, box.clip_maximum.x),
                       std::min(box.maximum.y, box.clip_maximum.y)};
        return box;
    };
    auto click = [&](const std::string& key) {
        const auto box = target(key);
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                            (box.minimum.y + box.maximum.y) * .5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    auto type = [&](const std::string& key, const char* text) {
        click(key);
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
    frame();
    frame();
    click("test:entity-menu");
    click("category:Rendering");
    click("action:Create / Sprite");
    check(!selected.empty() && scene.entity(selected).has<SpriteRenderer>() &&
              !scene.entity(selected).has<MeshRenderer>(),
          "Sprite Create menu composition failed");
    click("material:new");
    type("material:new-path", "Assets/Image.material.json");
    click("material:new-preset");
    click("material:new-preset:1");
    frame();
    check(!test::ui_targets.at("material:new-create").enabled,
          "Image-less Sprite material creation should stay disabled");
    click("asset-picker:texture:Image");
    type("asset-search:texture:Image", "Checker / 2D");
    click("picker-option:" + engine_texture(EngineTexture::Checker).id.str());
    click("material:new-create");
    check(material.document(), "Sprite material preset did not create its source");
    const auto material_id = material.document()->source().asset();
    validate_sprite_material_profile(resolve_material_source(material.document()->source()).values);
    material.request_save();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        frame();
        check(std::chrono::steady_clock::now() < deadline, "Sprite material publication timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (material.pending() || material.dirty());
    check(AssetCatalog::open_project(root).records().contains(material_id),
          "Sprite preset bypassed ordinary material publication");
    material.request_close();
    content = false;
    click("asset-picker:material:##Material");
    type("asset-search:material:##Material", "Image.material.json");
    click("picker-option:" + material_id.str());
    const auto field = [](const char* name) {
        return std::string("component-field:forge.sprite_renderer:") + name;
    };
    type(field("columns"), "2");
    type(field("rows"), "2");
    type(field("frame"), "3");
    type(field("width"), "2");
    click(field("flip_x"));
    const auto before_tint = scene.document();
    type(field("tint_a"), "0.5");
    const auto saved = scene.document();
    const auto native = scene.entity(selected).get<SpriteRenderer>();
    check(native.material.id == material_id && native.frame == 3 && native.width == 2 &&
              native.flip_x && native.tint_a == .5f && scene.undo() &&
              scene.document() == before_tint && scene.redo() && scene.document() == saved,
          "Inspector values did not use typed Scene history");
    type(field("frame"), "4");
    check(scene.document() == saved, "Out-of-grid Inspector frame partially changed scene");
    const auto path = root / "main.scene.json";
    scene.save(path);
    Scene reopened(engine.world());
    reopened.load(path);
    check(reopened.document() == saved, "Sprite Inspector edits did not survive save/reopen");
    runtime.restore_snapshot(saved);
    live = true;
    frame();
    check(!test::ui_targets.contains("runtime-field:forge.sprite_renderer:material") &&
              !test::ui_targets.contains("runtime-field:forge.sprite_renderer:columns"),
          "Live Sprite form exposed resource/grid edits outside its whitelist");
    type("runtime-field:forge.sprite_renderer:frame", "1");
    check(runtime.entity(selected).get<SpriteRenderer>().frame == 1 && scene.document() == saved,
          "Live Sprite Inspector mutated authored scene");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Sprite UI scratch root");
        for (const auto scale : {1.f, 1.5f, 2.f})
            exercise(argv[1], scale);
        std::cout << "Sprite creation, material preset/publication, Inspector history/reopen and "
                     "live controls passed at three scales\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
