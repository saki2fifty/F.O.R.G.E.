#include "prefabs.hpp"
#include <forge/animation_components.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, ImVec2 display) {
    const auto root = scratch / AssetId::generate().str();
    SceneDocument::create_project(root, "Member picker");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    auto create = [&](const char* name, const char* recipe) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
            .at("selected")
            .get<std::string>();
    };
    const auto original = create("Rig", "animation.sequence");
    const auto child = create("Track", "empty");
    authoring_command(scene, "component.add",
                      {{"entity", child}, {"component", "forge.transform_animator"}});
    scene.reparent_entity(child, original, ReparentMode::KeepLocal);
    const auto definition = create_prefab_source(scene, original);
    std::string member;
    for (const auto& item : definition.source.at("members"))
        if (item.at("name") == "Track")
            member = item.at("id");
    const auto asset = project.prefabs().create(scene, definition, "Assets/Picker.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
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
    ui::ContextScope context_scope(context);
    context.scene = &scene;
    PrefabEditor editor;
    editor.edit_source(project, asset);
    bool locked = false;
    bool collection_mode = false;
    auto collection_source = definition.source;
    Json collection = Json::array();
    const Json collection_field = {{"id", "links"},
                                   {"type", "vector"},
                                   {"maximum_count", 4096},
                                   {"element", {{"type", "entity_ref"}, {"nullable", true}}}};
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);
        if (collection_mode) {
            ImGui::Begin("Member collection###Member collection");
            {
                PrefabPropertyScope properties(collection_source);
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                property_field(root, collection_field, collection, false);
            }
            ImGui::End();
        } else
            editor.draw(scene, project, locked);
        check(prefab_property_source == nullptr, "Prefab property scope leaked into another panel");
        ImGui::Render();
    };
    frame();
    frame();
    auto point = [&](const std::string& id) {
        frame();
        check(test::ui_targets.contains(id), "Missing prefab control: " + id);
        auto box = test::ui_targets.at(id);
        auto inside = [&] {
            const auto x = (box.minimum.x + box.maximum.x) / 2;
            const auto y = (box.minimum.y + box.maximum.y) / 2;
            return x >= box.clip_minimum.x && x <= box.clip_maximum.x && y >= box.clip_minimum.y &&
                   y <= box.clip_maximum.y;
        };
        if (!inside()) {
            auto* window = ImGui::FindWindowByName(collection_mode ? "###Member collection"
                                                                   : "###Prefab source");
            check(window != nullptr, "Missing Prefab source window");
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y) / 2 -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            check(test::ui_targets.contains(id), "Prefab control disappeared after scrolling");
            box = test::ui_targets.at(id);
        }
        check(inside() && box.enabled, "Clipped or disabled prefab control: " + id);
        return ImVec2{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
    };
    auto mouse = [&](ImVec2 position, bool down) {
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    };
    auto click = [&](const std::string& id) {
        const auto position = point(id);
        mouse(position, false);
        mouse(position, true);
        mouse(position, false);
        frame();
    };
    click("prefab-source-member:" + member);
    frame();
    std::string picker;
    for (const auto& [id, box] : test::ui_targets) {
        (void)box;
        if (id.starts_with("prefab-member-picker:")) {
            check(picker.empty(), "Unexpected duplicate sequence reference picker");
            picker = id;
        }
    }
    check(!picker.empty(), "Source did not draw the typed member picker");
    click(picker);
    click("prefab-member-row:" + definition.root().str());
    check(editor.dirty() && editor.migration_document().at("version") == 3,
          "Mouse selection did not produce explicit portable source intent");
    editor.request_save();
    frame();
    check(!editor.dirty(), "Source member selection failed to publish");
    const auto snapshot = scene.document();
    for (const auto& item : snapshot.at("entities"))
        if (item.at("id") == first || item.at("id") == second) {
            const auto target =
                item.at("prefab_instance").at("members").at(member).get<std::string>();
            check(scene.entity(target).get<TransformAnimator>().sequence ==
                      EntityRef{scene.asset_id(), item.at("id").get<EntityId>()},
                  "Published UI member selection crossed instance boundaries");
        }
    locked = true;
    frame();
    check(test::ui_targets.contains(picker) && !test::ui_targets.at(picker).enabled,
          "Play lock left the prefab member picker editable");
    locked = false;
    frame();
    click(picker);
    click("prefab-member-clear");
    check(editor.dirty(), "Mouse clear did not change the source draft");
    editor.request_save();
    frame();
    check(!editor.dirty(), "Cleared member reference failed to publish");
    collection_mode = true;
    frame();
    frame();
    auto draft_picker = [&] {
        for (const auto& [id, box] : test::ui_targets) {
            (void)box;
            if (id.starts_with("prefab-member-picker:"))
                return id;
        }
        throw std::runtime_error("Missing new-item member picker");
    };
    click("button:Add item...");
    click(draft_picker());
    click("prefab-member-row:" + definition.root().str());
    check(collection.empty() && collection_source == definition.source,
          "Pending collection selection changed authored source/version before commit");
    click("button:Cancel");
    check(collection.empty() && collection_source == definition.source,
          "Cancel did not preserve the original collection and source format");
    click("button:Add item...");
    click(draft_picker());
    click("prefab-member-row:" + definition.root().str());
    click("button:Add");
    check(collection == Json::array({PrefabMemberRef{asset, definition.root()}}),
          "Collection draft validation rejected a valid source member reference");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide isolated UI scratch directory");
        std::filesystem::create_directories(argv[1]);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                exercise(argv[1], scale,
                         {(narrow ? 820.f : 1280.f) * scale, (narrow ? 650.f : 900.f) * scale});
        std::cout << "Prefab member picker actual input passed six layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
