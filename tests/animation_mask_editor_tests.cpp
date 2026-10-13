#include "editor/animation_mask_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery project and scratch root");
        for (const float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
            std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root);
            const auto source = std::filesystem::path("Assets/Weighted.mask.json");
            const auto disk = asset_detail::read_bytes(root / source, animation_mask_byte_limit);
            AnimationMaskEditor editor;
            editor.open(project, source);
            require(editor.document() && !editor.dirty(), "Published mask failed to open cleanly");
            const auto initial = editor.document()->source().document;
            const auto bone = initial.at("entries")[0].at("joint").get<AssetId>();
            const auto catalog = AssetCatalog::open_project(root);
            const auto selected =
                catalog.resolve(initial.at("skeleton").get<AssetId>(), SkeletonAsset::type);
            const std::set allowed{bone};
            const auto rows = asset_picker_entries(catalog, ModelNodeAsset::type, "", false,
                                                   selected.record->subasset->owner, &allowed);
            require(rows.size() == 1 && rows[0].id == bone,
                    "Mask picker exposed joints outside its accepted skeleton set");
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1500, 1800};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::style(scale);
            test::observe_ui = true;
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            std::string message;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1450, 1750}, ImGuiCond_Always);
                editor.draw(project, scene, false);
                ImGui::Render();
            };
            frame();
            frame();
            auto point = [&](const std::string& id) {
                const auto box = test::ui_targets.at(id);
                const auto top = std::max(box.minimum.y, box.clip_minimum.y);
                const auto bottom = std::min(box.maximum.y, box.clip_maximum.y);
                // Popup Selectable hit boxes include spacing above their visible text.
                // Click the visible area; document fields must remain fully visible.
                const bool visible = id.starts_with("picker-option:")
                                         ? bottom > top
                                         : box.minimum.y >= box.clip_minimum.y &&
                                               box.maximum.y <= box.clip_maximum.y;
                require(box.enabled && visible,
                        "Mask control was disabled or vertically clipped: " + id + " enabled=" +
                            std::to_string(box.enabled) + " y=" + std::to_string(box.minimum.y) +
                            "/" + std::to_string(box.maximum.y) +
                            " clip=" + std::to_string(box.clip_minimum.y) + "/" +
                            std::to_string(box.clip_maximum.y));
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f, (top + bottom) * .5f);
                frame();
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
            click("animation_mask:bone:" + bone.str());
            const auto revision = editor.document()->revision();
            number("animation_mask:weight", "0.375");
            require(editor.document()->revision() == revision + 1 &&
                        editor.document()->source().document.at("entries")[0].at("weight") == .375,
                    "Actual mask input did not commit one history entry on Enter");
            const auto edited = editor.document()->source().document;
            require(editor.dirty() &&
                        asset_detail::read_bytes(root / source, animation_mask_byte_limit) == disk,
                    "Draft mask input wrote source before Save");
            editor.undo();
            frame();
            require(editor.document()->source().document == initial, "Mask UI Undo failed");
            editor.redo();
            frame();
            require(editor.document()->source().document == edited, "Mask UI Redo failed");
            click("animation_mask:descendants");
            require(editor.document()->source().document.at("entries")[0].at("descendants") !=
                        edited.at("entries")[0].at("descendants"),
                    "Mask descendant checkbox did not edit its selected bone");
            editor.undo();
            frame();
            number("animation_mask:weight", "1.5");
            require(editor.document()->source().document == edited,
                    "Rejected mask weight entered document history");
            click("animation_mask:remove_bone");
            require(editor.document()->source().document.at("entries").size() + 1 ==
                        edited.at("entries").size(),
                    "Actual Remove bone control did not remove its selected override");
            click("asset-picker:model_node:New bone");
            // ImGui measures a newly opened popup before exposing its child clip rectangle.
            frame();
            frame();
            click("picker-option:" + bone.str());
            click("animation_mask:add_bone");
            const auto& added = editor.document()->source().document.at("entries").back();
            require(added.at("joint") == Json(bone) && added.at("weight") == 1 &&
                        added.at("descendants") == true,
                    "Actual skeleton picker/Add bone did not create the selected override");
            editor.undo();
            editor.undo();
            frame();
            require(editor.document()->source().document == edited,
                    "Add/remove bone Undo failed to restore the original ordered entries");
            const auto actor =
                authoring_command(scene, "entity.create", {{"name", "Compatible mask Animator"}})
                    .at("selected")
                    .get<std::string>();
            auto entity = scene.entity(actor);
            entity.add<Animator>();
            auto config = entity.get<Animator>();
            config.skeleton.id = initial.at("skeleton").get<AssetId>();
            for (const auto& [id, record] : catalog.records())
                if (record.type == AnimationClipAsset::type && record.subasset &&
                    record.subasset->owner == selected.record->subasset->owner) {
                    config.clip.id = id;
                    config.blend_clip.id = id;
                    break;
                }
            entity.set(config);
            context.selection.select_entity(actor);
            editor.undo();
            frame();
            require(editor.document()->source().document == initial,
                    "Mask draft baseline restore failed");
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty(),
                    "Mask UI publication did not finish cleanly, scale=" + std::to_string(scale) +
                        " pending=" + std::to_string(editor.pending()) + ": " +
                        editor.diagnostic());
            const auto before = scene.document();
            click("animation_mask:attach");
            require(entity.get<Animator>().blend_mask.id == editor.document()->source().asset() &&
                        scene.undo() && scene.document() == before,
                    "Mask UI attach lost Scene Undo isolation");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Clean mask document failed to close");
            ImGui::DestroyContext();
            std::filesystem::remove_all(root);
        }
        std::cout << "Actual mask controls at100/150/200%, source isolation, history, publication "
                     "and Scene attachment passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
