#include "fixtures/prefab_nested_gallery.hpp"
#include "fixtures/prefab_structure_gallery.hpp"
#include "fixtures/prefab_suppression_gallery.hpp"
#include "imgui_software_capture.hpp"
#include "prefabs.hpp"
#include <algorithm>
#include <forge/navigation_behavior_components.hpp>
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, bool narrow,
              const std::filesystem::path& image = {}, bool nested_only = false) {
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
    const auto inherited_entity =
        authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Inherited child"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "entity.reparent",
                      {{"entity", inherited_entity}, {"parent", original}});
    auto definition = create_prefab_source(scene, original);
    auto& members = definition.source["members"];
    const auto root_at = std::find_if(members.begin(), members.end(), [&](const Json& row) {
        return row.at("id") == definition.source.at("root");
    });
    check(root_at != members.end(), "Captured source has no root member");
    std::iter_swap(members.begin(), root_at);
    std::string inherited_child;
    for (const auto& row : definition.source.at("members"))
        if (row.at("name") == "Inherited child")
            inherited_child = row.at("id");
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
    bool locked = false;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        if (auto* window = ImGui::FindWindowByName("###Prefab source"))
            ImGui::SetWindowSize(window, {(narrow ? 800 : 1160) * scale, 1060 * scale});
        ImGui::SetNextWindowPos({10 * scale, 10 * scale});
        ImGui::SetNextWindowSize({(narrow ? 800 : 1160) * scale, 1060 * scale});
        editor.draw(scene, project, locked);
        capture.render();
    };
    frame();
    frame();
    check(test::ui_targets.contains("button:Add child") &&
              test::ui_targets.at("button:Add child").enabled &&
              !test::ui_targets.at("button:Remove member subtree").enabled,
          "Variant editor did not allow local additions or protect the root");
    std::string stage = "inherited values";
    auto point = [&](const std::string& id) {
        // Settle automatic scroll clamping after changing the selected member's
        // inspector height before sampling the next real input coordinate.
        frame();
        frame();
        check(test::ui_targets.contains(id), "Missing variant control during " + stage + ": " + id);
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
    if (nested_only) {
        stage = "nested mount selection";
        check(editor.resolve_close(ui::DraftResolution::Discard, scene, project),
              "Could not settle the setup draft");
        const auto child_asset =
            project.prefabs().duplicate(scene, base, "Assets/NativeLinked.prefab.json");
        const auto child_root =
            project.prefabs().source(child_asset).at("root").get<PrefabMemberId>();
        const auto parent_asset = AssetId::generate();
        const auto parent_root = PrefabMemberId::generate();
        project.prefabs().create(
            scene,
            PrefabDocument(
                {{"format", "forge.prefab"},
                 {"version", 1},
                 {"asset_id", parent_asset},
                 {"revision", 1},
                 {"root", parent_root},
                 {"members",
                  Json::array(
                      {{{"id", parent_root},
                        {"name", "Nested assembly"},
                        {"components",
                         {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                          {"forge.local_rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}},
                          {"forge.local_scale", {{"x", 1}, {"y", 1}, {"z", 1}}},
                          {"forge.navigation_behavior",
                           {{"graph", nullptr},
                            {"target", nullptr},
                            {"anchor", nullptr},
                            {"enabled", true}}}}}}})}}),
            "Assets/NativeAssembly.prefab.json");
        editor.edit_source(scene, project, parent_asset);
        click("prefab-nested-choice");
        click("prefab-nested-asset:" + child_asset.str());
        auto source = editor.migration_document();
        check(source.at("version") == 7 &&
                  source.at("members")[0].at("prefab_instance").at("asset") == Json(child_asset),
              "Actual nested selection failed to author the shared source link");
        stage = "scoped native member picker";
        std::string picker;
        const auto picker_schema = scene.schema();
        for (const auto& component : picker_schema.at("components"))
            if (component.at("id") == "forge.navigation_behavior")
                for (const auto& field : component.at("fields"))
                    if (field.at("id") == "target")
                        picker = "prefab-member-picker:##" + property_label(field);
        check(!picker.empty(), "Missing native target field schema");
        const PrefabMemberPath address{parent_asset, {{parent_root, child_asset}}, child_root};
        const auto encoded = Json(address);
        click(picker);
        click("prefab-member-row:" + encoded.at("member").dump());
        source = editor.migration_document();
        check(
            source.at("members")[0].at("components").at("forge.navigation_behavior").at("target") ==
                encoded,
            "Actual scoped picker retargeted or dropped the selected nested occurrence");
        check(prefab_property_source == nullptr && prefab_property_definitions == nullptr,
              "Nested reference scope leaked into another panel");
        if (!image.empty()) {
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            frame();
            frame();
            capture.save(image);
        }
        stage = "native publication";
        click("button:Publish source");
        check(!editor.dirty() &&
                  project.prefabs().source(parent_asset) == editor.migration_document(),
              "Actual Publish did not save the reviewed nested source");
        const auto instance = instantiate_prefab(scene, parent_asset);
        const auto reference =
            *prefab_member_path_reference(scene.document(), EntityId::parse(instance), address);
        check(scene.entity(reference.entity.str()) &&
                  scene.entity(instance).get<NavigationBehavior>().target == reference,
              "Native-authored source did not realize its actual linked target");
        const auto derived = project.prefabs().create_variant(
            scene, parent_asset, "Assets/NativeAssemblyVariant.prefab.json");
        check(editor.resolve_close(ui::DraftResolution::Discard, scene, project),
              "Could not settle source switch");
        editor.edit_source(scene, project, derived);
        stage = "inherited nested clear and Revert";
        click("prefab-nested-choice");
        click("prefab-nested-none");
        check(editor.migration_document().at("version") == 7 &&
                  editor.migration_document().at("overrides")[0].at("prefab_instance").is_null(),
              "Actual None selection did not retain explicit inherited-link clearing");
        click("prefab-nested-revert");
        check(editor.migration_document().at("overrides").empty() &&
                  editor.source_view(scene.schema())
                          .at("members")[0]
                          .at("prefab_instance")
                          .at("asset") == Json(child_asset),
              "Actual nested Revert failed to restore its current base link");
        locked = true;
        frame();
        frame();
        check(!test::ui_targets.at("prefab-nested-choice").enabled,
              "Locked source allowed nested link editing");
        test::observe_ui = false;
        return;
    }
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
    check(!test::ui_targets.contains("prefab-component-suppress:forge.tint"),
          "Protected component offered a suppression control");
    click("prefab-component-suppress:forge.mesh_renderer");
    check(editor.migration_document().at("version") == 5 && !editor.source_view(scene.schema())
                                                                 .at("members")[0]
                                                                 .at("components")
                                                                 .contains("forge.mesh_renderer"),
          "Actual Suppress input did not remove the inherited component");
    frame();
    check(test::ui_targets.contains("prefab-component-restore:forge.mesh_renderer"),
          "Suppression hid the only restoration control");
    click("prefab-component-restore:forge.mesh_renderer");
    check(editor.migration_document().at("overrides").empty() &&
              editor.source_view(scene.schema())
                  .at("members")[0]
                  .at("components")
                  .contains("forge.mesh_renderer"),
          "Actual Restore input did not recover inheritance");
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
    stage = "owned child creation";
    click("button:Add child");
    check(editor.migration_document().contains("added_members") &&
              editor.migration_document().at("added_members").size() == 1,
          "First actual Add child input did not create one owned definition");
    const auto local =
        editor.migration_document().at("added_members")[0].at("id").get<std::string>();
    check(editor.migration_document().at("version") == 6,
          "Actual Add child input did not enable structural composition");
    editor.set_property("forge.tint", "r", .3);
    const auto owned_edit = editor.migration_document();
    check(owned_edit.at("added_members")[0].at("components").at("forge.tint").at("r") == .3 &&
              std::none_of(owned_edit.at("overrides").begin(), owned_edit.at("overrides").end(),
                           [&](const Json& row) { return row.at("id") == local; }),
          "Editing an owned addition manufactured inherited property intent");
    click("button:Add child");
    check(editor.migration_document().at("added_members").size() == 2,
          "Second actual Add child input did not create the selected member's child");
    const auto child =
        editor.migration_document().at("added_members")[1].at("id").get<std::string>();
    const auto root_member = editor.migration_document().at("root").get<std::string>();
    stage = "local parenting and sibling order";
    click("prefab-source-parent:" + child);
    click("prefab-parent-target:" + root_member);
    check(editor.migration_document().at("added_members")[1].at("parent") == root_member,
          "Actual parent input did not update the owned child definition");
    click("prefab-source-member:" + local);
    click("button:Move member down");
    check(editor.migration_document().at("member_order")[0].at("members") ==
              Json::array({inherited_child, child, local}),
          "Actual sibling move did not author a stable member-ID order prefix");
    click("prefab-source-parent:" + local);
    click("prefab-parent-target:" + child);
    click("prefab-source-member:" + child);
    stage = "cycle rejection";
    const auto before_cycle = editor.migration_document();
    click("prefab-source-parent:" + child);
    click("prefab-parent-target:" + local);
    check(editor.migration_document() == before_cycle,
          "Actual cyclic parent input changed the shared source draft");
    click("prefab-source-member:" + local);
    click("prefab-source-spatial:" + local);
    click("prefab-spatial-mode:world");
    check(editor.migration_document().at("added_members")[0].at("spatial").at("mode") == "world",
          "Actual spatial input did not retain world binding");
    click("button:Remove member subtree");
    check(editor.migration_document().at("added_members")[0].at("removed") == true &&
              editor.source_view(scene.schema()).at("members").size() == 3,
          "Actual removal lost the owned definition or left it effective");
    stage = "local member restoration";
    click("prefab-unavailable:" + local);
    click("prefab-member-restore:" + local);
    check(!editor.migration_document().at("added_members")[0].contains("removed") &&
              editor.source_view(scene.schema()).at("members").size() == 4,
          "Actual Restore input did not recover the same local member");
    stage = "inherited attachment Revert and restoration";
    click("prefab-source-member:" + inherited_child);
    click("prefab-source-parent:" + inherited_child);
    click("prefab-parent-target:" + local);
    click("button:Revert parent");
    click("prefab-source-spatial:" + inherited_child);
    click("prefab-spatial-mode:world");
    click("button:Revert spatial binding");
    const auto reverted_structure = editor.migration_document();
    check(std::none_of(reverted_structure.at("overrides").begin(),
                       reverted_structure.at("overrides").end(),
                       [&](const Json& row) { return row.at("id") == inherited_child; }),
          "Actual structural Revert retained inherited parent/spatial intent");
    click("button:Remove member subtree");
    click("prefab-unavailable:" + inherited_child);
    click("prefab-member-restore:" + inherited_child);
    check(editor.source_view(scene.schema()).at("members").size() == 4,
          "Actual inherited removal/restoration lost a member");
    const auto missing = PrefabMemberId::generate();
    stage = "unavailable local parent repair";
    editor.set_member_parent(PrefabMemberId::parse(local), missing, scene.schema());
    frame();
    frame();
    if (!test::ui_targets.contains("prefab-source-parent:" + local))
        click("prefab-unavailable:" + local);
    click("prefab-source-parent:" + local);
    click("prefab-parent-target:" + root_member);
    check(editor.source_view(scene.schema()).at("members").size() == 4,
          "Unavailable local parent could not be repaired through actual input");
    stage = "unavailable local spatial repair";
    editor.set_member_spatial(PrefabMemberId::parse(local),
                              {{"mode", "explicit"}, {"member", missing}}, scene.schema());
    frame();
    frame();
    click("prefab-source-spatial:" + local);
    click("prefab-spatial-mode:world");
    check(editor.source_view(scene.schema()).at("members").size() == 4,
          "Unavailable local spatial target could not be repaired through actual input");
    stage = "dormant sibling order retention";
    editor.set_member_order(PrefabMemberId::parse(root_member),
                            {PrefabMemberId::parse(inherited_child), missing,
                             PrefabMemberId::parse(child), PrefabMemberId::parse(local)},
                            scene.schema());
    click("prefab-source-member:" + local);
    click("button:Move member up");
    check(editor.migration_document().at("member_order")[0].at("members") ==
              Json::array({inherited_child, missing.str(), local, child}),
          "Native sibling move discarded dormant ordering intent");
    click("button:Revert sibling order");
    check(!editor.migration_document().contains("member_order"),
          "Native order Revert retained the source's ordering prefix");
    test::observe_ui = false;
}
enum class SourceCapture { Variant, Suppression, Structure, Nested, Basic };
void capture_gallery(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                     const std::filesystem::path& output,
                     SourceCapture mode = SourceCapture::Variant) {
    const bool suppression = mode == SourceCapture::Suppression;
    const bool structural = mode == SourceCapture::Structure;
    const bool nested = mode == SourceCapture::Nested;
    const bool basic = mode == SourceCapture::Basic;
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    AssetId amber;
    for (const auto& [id, record] : project.prefabs().records())
        if (record.source == (basic         ? test_prefab_nested_gallery::child_path
                              : nested      ? test_prefab_nested_gallery::prefab_paths[0]
                              : structural  ? test_prefab_structure_gallery::prefab_paths[0]
                              : suppression ? test_prefab_suppression_gallery::prefab_paths[1]
                                            : "Assets/Variants/AmberBeacon.prefab.json"))
            amber = id;
    check(bool(amber), "Missing Gallery variant capture source");
    test::ImGuiSoftwareCapture capture(1180, 1520);
    ui::style(1);
    PrefabEditor editor;
    editor.edit_source(scene, project, amber);
    test::observe_ui = suppression || structural || nested || basic;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        if (auto* window = ImGui::FindWindowByName("###Prefab source"))
            ImGui::SetWindowSize(window, {1160, 1500});
        ImGui::SetNextWindowPos({10, 10});
        editor.draw(scene, project, false);
        capture.render();
    };
    for (unsigned i = 0; i < 6; ++i)
        frame();
    if (suppression || structural || nested || basic) {
        std::string crown;
        const auto view = editor.source_view(scene.schema());
        for (const auto& row : view.at("members"))
            if (basic ? row.at("id") == view.at("root")
                      : row.at("name") == (nested       ? "Right linked marker"
                                           : structural ? "Crossbar"
                                                        : "Crown"))
                crown = row.at("id");
        const auto box = test::ui_targets.at("prefab-source-member:" + crown);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                            (box.minimum.y + box.maximum.y) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        if (nested || basic)
            check(test::ui_targets.contains("prefab-nested-choice") &&
                      test::ui_targets.at("prefab-nested-choice").enabled,
                  "Gallery source capture did not reach its actual nested asset control");
        else if (structural)
            check(test::ui_targets.contains("prefab-source-parent:" + crown) &&
                      test::ui_targets.contains("prefab-source-spatial:" + crown),
                  "Gallery source capture did not reach its actual local attachment controls");
        else
            check(test::ui_targets.contains("prefab-component-restore:forge.mesh_renderer"),
                  "Gallery source screenshot did not reach its actual Restore control");
    }
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    frame();
    frame();
    std::filesystem::create_directories(output.parent_path());
    capture.save(output);
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "--nested-ui") {
            const auto scratch = std::filesystem::absolute(argv[2]);
            std::filesystem::create_directories(scratch);
            if (argc == 4)
                exercise(scratch, 1, false, std::filesystem::absolute(argv[3]), true);
            else
                for (float scale : {1.f, 1.5f, 2.f})
                    for (bool narrow : {false, true})
                        exercise(scratch, scale, narrow, {}, true);
            std::cout << "Actual native nested links and scoped picker passed\n";
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--gallery-capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::filesystem::absolute(argv[4]));
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--suppression-capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::filesystem::absolute(argv[4]), SourceCapture::Suppression);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--structure-capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::filesystem::absolute(argv[4]), SourceCapture::Structure);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--nested-capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::filesystem::absolute(argv[4]), SourceCapture::Nested);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--basic-capture") {
            capture_gallery(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                            std::filesystem::absolute(argv[4]), SourceCapture::Basic);
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
