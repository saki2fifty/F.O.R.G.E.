#include "editor/component_inspector.hpp"
#include "editor/creation_menu.hpp"
#include "editor/material_editor.hpp"
#include "editor/prefabs.hpp"
#include "sprite_frame_picker_tests.hpp"
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    MaterialEditor material;
    ComponentInspector inspector;
    PrefabEditor prefab;
    ui::SpriteFramePicker picker;
    inspector.sprite_frames = [&](const auto& field, auto& value) {
        return picker.draw(field, value);
    };
    prefab.sprite_frames = inspector.sprite_frames;
    picker.image_source = [&](auto material) {
        check(bool(material.id), "Inspector chooser lost its image material");
        ui::SpriteSheetImage image;
        image.width = image.height = 32;
        image.revision = ResourceIdentity{};
        image.render = [](auto, auto, auto) {
            static int marker;
            return reinterpret_cast<ImTextureID>(&marker);
        };
        return image;
    };
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
    bool at_target = false, content = true, live = false, prefab_mode = false;
    EngineContext runtime_engine(WorldRole::Runtime);
    Scene runtime(runtime_engine.world());
    auto frame = [&] {
        material.poll(project, message);
        test::ui_targets.clear();
        picker.begin_frame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({5, 5}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1190, 990}, ImGuiCond_Always);
        ImGui::Begin(content ? "Content" : "Inspector", nullptr, ImGuiWindowFlags_MenuBar);
        if (prefab_mode) {
            // The independent prefab source window below owns its own draft.
        } else if (content) {
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
        if (prefab_mode) {
            ImGui::SetNextWindowPos({5, 5}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1190, 990}, ImGuiCond_Always);
            prefab.draw(scene, project, false);
        }
        picker.after_ui();
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
            auto* window = ImGui::FindWindowByName(prefab_mode ? "###Prefab source"
                                                   : content   ? "Content"
                                                               : "Inspector");
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
    const auto key = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        frame();
    };
    frame();
    const auto simple_image = scene.document();
    check(!test::ui_targets.contains(field("pixels_per_meter")) &&
              !test::ui_targets.contains(field("border_left")),
          "Simple Sprite exposes inactive fill fields");
    click(field("image_mode"));
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    check(scene.entity(selected).get<SpriteRenderer>().image_mode == 1,
          "Image mode combo did not select Sliced through actual input");
    type(field("pixels_per_meter"), "32");
    type(field("border_left"), "2");
    const auto sliced_image = scene.document();
    check(scene.entity(selected).get<SpriteRenderer>().pixels_per_meter == 32 &&
              scene.entity(selected).get<SpriteRenderer>().border_left == 2,
          "Sliced controls did not author the typed Sprite");
    for (unsigned step = 0; step < 3; ++step)
        check(scene.undo(), "Sliced Inspector edit lost its ordinary Undo step");
    check(scene.document() == simple_image, "Sliced Inspector Undo changed unrelated source");
    for (unsigned step = 0; step < 3; ++step)
        check(scene.redo(), "Sliced Inspector Redo failed");
    check(scene.document() == sliced_image, "Sliced Inspector Redo lost field intent");
    click(field("image_mode"));
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    frame();
    check(scene.entity(selected).get<SpriteRenderer>().image_mode == 2 &&
              scene.entity(selected).get<SpriteRenderer>().border_left == 2 &&
              test::ui_targets.contains(field("pixels_per_meter")) &&
              !test::ui_targets.contains(field("border_left")),
          "Tiled mode lost inactive borders or exposed sliced-only controls");
    click(field("image_mode"));
    key(ImGuiKey_Home);
    key(ImGuiKey_Enter);
    frame();
    check(scene.entity(selected).get<SpriteRenderer>().image_mode == 0 &&
              !test::ui_targets.contains(field("pixels_per_meter")),
          "Simple mode did not hide inactive density controls");
    for (unsigned step = 0; step < 5; ++step)
        check(scene.undo(), "Image fill mode/history did not remain ordinary property edits");
    check(scene.document() == simple_image,
          "Image fill test did not restore its initial authored Scene");
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
    click("sprite:choose-frame");
    key(ImGuiKey_Home);
    key(ImGuiKey_RightArrow);
    key(ImGuiKey_Enter);
    check(scene.entity(selected).get<SpriteRenderer>().frame == 1 && scene.undo() &&
              scene.document() == saved && scene.redo() &&
              scene.entity(selected).get<SpriteRenderer>().frame == 1 && scene.undo(),
          "Visual frame selection did not form one reversible Scene property edit");
    click("sprite:choose-frame");
    key(ImGuiKey_Home);
    key(ImGuiKey_Escape);
    check(scene.document() == saved && scene.redo() &&
              scene.entity(selected).get<SpriteRenderer>().frame == 1 && scene.undo(),
          "Cancelling visual selection mutated Scene or discarded redo history");
    runtime.restore_snapshot(saved);
    live = true;
    frame();
    click("runtime-field:forge.sprite_renderer:image_mode");
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    type("runtime-field:forge.sprite_renderer:pixels_per_meter", "16");
    type("runtime-field:forge.sprite_renderer:border_left", "1");
    check(runtime.entity(selected).get<SpriteRenderer>().image_mode == 1 &&
              runtime.entity(selected).get<SpriteRenderer>().pixels_per_meter == 16 &&
              runtime.entity(selected).get<SpriteRenderer>().border_left == 1 &&
              scene.document() == saved,
          "Live image fill controls changed authored source or bypassed runtime values");
    runtime.restore_snapshot(saved);
    frame();
    check(runtime.entity(selected).get<SpriteRenderer>().image_mode == 0 &&
              runtime.entity(selected).get<SpriteRenderer>().pixels_per_meter == 100 &&
              runtime.entity(selected).get<SpriteRenderer>().border_left == 0 &&
              !test::ui_targets.contains("runtime-field:forge.sprite_renderer:pixels_per_meter"),
          "Temporary image fill controls survived authored snapshot restoration");
    check(!test::ui_targets.contains("runtime-field:forge.sprite_renderer:material") &&
              !test::ui_targets.contains("runtime-field:forge.sprite_renderer:columns"),
          "Live Sprite form exposed resource/grid edits outside its whitelist");
    type("runtime-field:forge.sprite_renderer:frame", "1");
    check(runtime.entity(selected).get<SpriteRenderer>().frame == 1 && scene.document() == saved,
          "Live Sprite Inspector mutated authored scene");
    click("sprite:choose-frame");
    key(ImGuiKey_Home);
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    check(runtime.entity(selected).get<SpriteRenderer>().frame == 2 && scene.document() == saved,
          "Visual live selection bypassed runtime submission or changed authored source");
    runtime.restore_snapshot(saved);
    live = false;
    frame();
    check(runtime.entity(selected).get<SpriteRenderer>().frame == 3 && scene.document() == saved,
          "Temporary visual frame selection survived authored snapshot restoration");
    const auto prefab_id = project.prefabs().create(scene, create_prefab_source(scene, selected),
                                                    "Assets/Sheet.prefab.json");
    const auto published_prefab = project.prefabs().source(prefab_id);
    const auto instance_before = scene.document();
    prefab.edit_source(project, prefab_id);
    prefab_mode = true;
    click("sprite:choose-frame");
    key(ImGuiKey_Home);
    key(ImGuiKey_RightArrow);
    key(ImGuiKey_Enter);
    const auto draft = prefab.migration_document();
    check(prefab.dirty() &&
              draft["members"][0]["components"]["forge.sprite_renderer"]["frame"] == 1 &&
              project.prefabs().source(prefab_id) == published_prefab &&
              scene.document() == instance_before,
          "Visual prefab selection bypassed draft ownership or published without a request");
    click("sprite:choose-frame");
    key(ImGuiKey_End);
    key(ImGuiKey_Escape);
    check(prefab.migration_document() == draft, "Cancelled frame selection changed a prefab draft");
    prefab.request_close();
    check(prefab.resolve_close(ui::DraftResolution::Discard, scene, project) &&
              project.prefabs().source(prefab_id) == published_prefab &&
              scene.document() == instance_before,
          "Discarding frame selection changed the published prefab or its scene instance");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need Sprite UI scratch root");
        for (const auto scale : {1.f, 1.5f, 2.f}) {
            exercise(argv[1], scale);
            test_sprite_picker::exercise(scale);
            test_sprite_picker::exercise(scale, {960, 640});
        }
        std::cout << "Sprite creation, material preset/publication, Inspector history/reopen and "
                     "live controls passed at three scales\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
