#include "imgui_software_capture.hpp"
#include "prefabs.hpp"
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, bool narrow,
              const std::filesystem::path& image = {}) {
    const auto root = scratch / AssetId::generate().str();
    SceneDocument::create_project(root, "Prefab variants");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto original =
        authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Beacon"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", original}, {"component", "forge.mesh_renderer"}});
    auto definition = create_prefab_source(scene, original);
    const auto inherited_green =
        definition.source.at("members")[0].at("components").at("forge.tint").at("g");
    definition.source["members"][0]["components"]["forge.mesh_renderer"].erase(
        "material_parameters");
    const auto base = project.prefabs().create(scene, definition, "Assets/Beacon.prefab.json");
    const auto variant =
        project.prefabs().create_variant(scene, base, "Assets/AmberBeacon.prefab.json");
    test::ImGuiSoftwareCapture capture(int((narrow ? 820 : 1180) * scale), int(1080 * scale));
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.scene = &scene;
    PrefabEditor editor;
    editor.edit_source(scene, project, variant);
    editor.rename_member("Amber beacon variant");
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        if (auto* window = ImGui::FindWindowByName("###Prefab source"))
            ImGui::SetWindowSize(window, {(narrow ? 800 : 1160) * scale, 1060 * scale});
        ImGui::SetNextWindowPos({10 * scale, 10 * scale});
        ImGui::SetNextWindowSize({(narrow ? 800 : 1160) * scale, 1060 * scale});
        editor.draw(scene, project, false);
        capture.render();
    };
    frame();
    frame();
    check(test::ui_targets.contains("button:Add child") &&
              !test::ui_targets.at("button:Add child").enabled &&
              !test::ui_targets.at("button:Remove member subtree").enabled,
          "Variant editor enabled unsupported structural edits");
    auto point = [&](const std::string& id) {
        frame();
        check(test::ui_targets.contains(id), "Missing variant control: " + id);
        auto box = test::ui_targets.at(id);
        const auto center = [](const auto& box) {
            return ImVec2{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
        };
        auto p = center(box);
        if (p.y < box.clip_minimum.y || p.y > box.clip_maximum.y) {
            auto* window = ImGui::FindWindowByName("###Prefab source");
            check(window, "Missing variant source window");
            ImGui::SetScrollY(window, window->Scroll.y + p.y -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            box = test::ui_targets.at(id);
            p = center(box);
        }
        check(box.enabled && p.x >= box.clip_minimum.x && p.x <= box.clip_maximum.x &&
                  p.y >= box.clip_minimum.y && p.y <= box.clip_maximum.y,
              "Clipped or disabled variant control: " + id);
        return p;
    };
    auto click = [&](const std::string& id) {
        const auto p = point(id);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(p.x, p.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    click("button:Revert name");
    check(!editor.dirty(), "Actual name Revert retained variant intent");
    click("button:Override name");
    check(editor.dirty() && editor.migration_document().at("overrides")[0].at("name") == "Beacon",
          "Equal-value name ownership could not be selected explicitly");
    click("button:Revert name");
    const auto p = point("prefab-source-field:forge.tint:g");
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    check(ImGui::GetInputTextState(test::ui_targets.at("prefab-source-field:forge.tint:g").id),
          "Variant numeric field did not activate from actual input");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    frame();
    io.AddInputCharactersUTF8("0.65");
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    const auto edited = editor.migration_document();
    check(edited.at("overrides").size() == 1 &&
              edited.at("overrides")[0].at("components").empty() &&
              std::abs(edited.at("overrides")[0]
                           .at("property_overrides")
                           .at("forge.tint")
                           .at("g")
                           .get<double>() -
                       .65) < 1e-5,
          "Actual variant field input did not author a property override: " + edited.dump());
    click("button:Revert field");
    check(!editor.dirty(), "Actual field Revert failed to restore inheritance");
    const auto equal = point("prefab-source-field:forge.tint:g");
    io.AddMousePosEvent(equal.x, equal.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    frame();
    click("prefab-field-override:forge.tint:g");
    check(editor.dirty() && editor.migration_document()
                                    .at("overrides")[0]
                                    .at("property_overrides")
                                    .at("forge.tint")
                                    .at("g") == inherited_green,
          "Context action did not retain an explicit equal/default-valued field override");
    click("button:Revert field");
    check(!editor.dirty() && !project.prefabs()
                                  .source(base)
                                  .at("members")[0]
                                  .at("components")
                                  .at("forge.mesh_renderer")
                                  .contains("material_parameters"),
          "Reading/reverting a schema default authored it into the base file");
    click("prefab-component-override:forge.tint");
    check(
        editor.dirty() &&
            editor.migration_document().at("overrides")[0].at("components").contains("forge.tint"),
        "Equal complete-component ownership could not be selected explicitly");
    click("prefab-component-revert:forge.tint");
    check(!editor.dirty(), "Complete-component Revert retained variant ownership");
    editor.rename_member("Amber beacon variant");
    editor.set_property("forge.tint", "r", 1);
    editor.set_property("forge.tint", "g", .65);
    if (!image.empty()) {
        auto* window = ImGui::FindWindowByName("###Prefab source");
        ImGui::SetScrollY(window, 0);
        frame();
        frame();
        std::filesystem::create_directories(image.parent_path());
        capture.save(image);
    }
    test::observe_ui = false;
}
void capture_gallery(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                     const std::filesystem::path& output) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    AssetId amber;
    for (const auto& [id, record] : project.prefabs().records())
        if (record.source == "Assets/Variants/AmberBeacon.prefab.json")
            amber = id;
    check(bool(amber), "Missing Gallery variant capture source");
    test::ImGuiSoftwareCapture capture(1180, 1520);
    ui::style(1);
    PrefabEditor editor;
    editor.edit_source(scene, project, amber);
    for (unsigned frame = 0; frame < 6; ++frame) {
        capture.new_frame();
        if (auto* window = ImGui::FindWindowByName("###Prefab source"))
            ImGui::SetWindowSize(window, {1160, 1500});
        ImGui::SetNextWindowPos({10, 10});
        editor.draw(scene, project, false);
        capture.render();
    }
    std::filesystem::create_directories(output.parent_path());
    capture.save(output);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--gallery-capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::filesystem::absolute(argv[4]));
            return 0;
        }
        check(argc == 2 || argc == 3, "Provide UI scratch and optional capture path");
        const auto scratch = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(scratch);
        if (argc == 3)
            exercise(scratch, 1, false, std::filesystem::absolute(argv[2]));
        else
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool narrow : {false, true})
                    exercise(scratch, scale, narrow);
        std::cout << "Actual variant source input/Revert and structural bounds passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
