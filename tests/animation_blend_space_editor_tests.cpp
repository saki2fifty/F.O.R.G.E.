#include "editor/animation_blend_space_editor.hpp"
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
            const auto source = std::filesystem::path("Assets/Motion.blendspace.json");
            const auto disk =
                asset_detail::read_bytes(root / source, animation_blend_space_byte_limit);
            AnimationBlendSpaceEditor editor;
            editor.open(project, source);
            require(editor.document() && !editor.dirty(),
                    "Published blend space failed to open cleanly");
            const auto initial = editor.document()->source().document;
            const auto sample_id = initial.at("samples")[0].at("id").get<AssetId>();
            const auto catalog = AssetCatalog::open_project(root);
            const auto selected =
                catalog.resolve(initial.at("skeleton").get<AssetId>(), SkeletonAsset::type);
            const auto clips = asset_picker_entries(catalog, AnimationClipAsset::type, "", false,
                                                    selected.record->subasset->owner);
            require(clips.size() == 3,
                    "Blend Space picker did not scope to the imported model's clips");
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
            bool show_create = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                if (show_create) {
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({1450, 1750}, ImGuiCond_Always);
                    ImGui::Begin("Content creation fixture");
                    editor.content(project, false);
                    ImGui::End();
                }
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
                const bool visible = (id.starts_with("picker-option:") ||
                                      id.starts_with("animation_blend_space:new_dimensions:"))
                                         ? bottom > top
                                         : box.minimum.y >= box.clip_minimum.y &&
                                               box.maximum.y <= box.clip_maximum.y;
                require(box.enabled && visible,
                        "Blend Space control was disabled or vertically clipped: " + id +
                            " enabled=" + std::to_string(box.enabled) + " y=" +
                            std::to_string(box.minimum.y) + "/" + std::to_string(box.maximum.y) +
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
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
                io.AddInputCharactersUTF8(value);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            click("animation_blend_space:sample:" + sample_id.str());
            const auto revision = editor.document()->revision();
            number("animation_blend_space:position", "-0.375");
            require(editor.document()->revision() == revision + 1 &&
                        editor.document()->source().document.at("samples")[0].at("position") ==
                            -.375,
                    "Actual sample position did not commit one history entry on Enter");
            const auto edited = editor.document()->source().document;
            require(editor.dirty() && asset_detail::read_bytes(
                                          root / source, animation_blend_space_byte_limit) == disk,
                    "Blend Space draft wrote source before Save");
            editor.undo();
            frame();
            require(editor.document()->source().document == initial, "Blend Space Undo failed");
            editor.redo();
            frame();
            require(editor.document()->source().document == edited, "Blend Space Redo failed");
            number("animation_blend_space:position", "1");
            require(editor.document()->source().document == edited,
                    "Duplicate position entered history");
            number("animation_blend_space:cycle", "0");
            require(editor.document()->source().document == edited,
                    "Invalid cycle entered history");
            click("asset-picker:animation_clip:Sample clip");
            frame();
            frame();
            const auto other = std::find_if(clips.begin(), clips.end(), [&](const auto& clip) {
                return Json(clip.id) != edited.at("samples")[0].at("clip");
            });
            require(other != clips.end(), "No compatible alternative clip");
            click("picker-option:" + other->id.str());
            require(editor.document()->source().document.at("samples")[0].at("clip") ==
                        Json(other->id),
                    "Actual model-scoped clip picker did not edit the selected sample");
            editor.undo();
            frame();
            click("animation_blend_space:remove_sample");
            require(editor.document()->source().document.at("samples").size() + 1 ==
                        edited.at("samples").size(),
                    "Remove sample did not remove its durable row");
            click("animation_blend_space:add_sample");
            const auto& added = editor.document()->source().document.at("samples").back();
            require(added.at("id") != Json(sample_id) && added.at("clip").is_null(),
                    "Add sample did not create a fresh incomplete row");
            editor.undo();
            editor.undo();
            frame();
            require(editor.document()->source().document == edited,
                    "Add/remove Undo did not restore ordered samples");
            const auto actor = authoring_command(scene, "entity.create",
                                                 {{"name", "Compatible blend-space Animator"}})
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
            for (const auto& [id, record] : catalog.records()) {
                if (record.type == AnimationMaskAsset::type)
                    config.blend_mask.id = id;
                if (record.type == AnimationStatesAsset::type)
                    config.states.id = id;
            }
            entity.set(config);
            context.selection.select_entity(actor);
            editor.undo();
            frame();
            require(editor.document()->source().document == initial,
                    "Blend Space baseline restore failed");
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty(), "Blend Space UI publication did not finish cleanly, scale=" +
                                         std::to_string(scale) +
                                         " pending=" + std::to_string(editor.pending()) + ": " +
                                         editor.diagnostic());
            const auto before = scene.document();
            click("animation_blend_space:attach");
            require(entity.get<Animator>().blend_space.id == editor.document()->source().asset() &&
                        !entity.get<Animator>().blend_clip.id &&
                        !entity.get<Animator>().blend_mask.id &&
                        !entity.get<Animator>().states.id && scene.undo() &&
                        scene.document() == before,
                    "Blend Space UI attach lost Scene Undo isolation");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Clean blend-space document failed to close");
            editor.open(project, source);
            frame();
            number("animation_blend_space:cycle", "3");
            require(editor.dirty(), "Cycle edit did not create a dirty source document");
            editor.request_close();
            frame();
            frame();
            frame(); // Modal auto-size and centering settle before hit testing.
            click("button:Keep editing");
            require(editor.is_open() && editor.dirty() && editor.close_cancelled,
                    "Dirty close cancellation lost the draft");
            editor.request_close();
            frame();
            frame();
            frame();
            click("button:Discard");
            require(
                !editor.is_open() &&
                    AnimationBlendSpaceDocument(project.writer_guard(), source).source().document ==
                        initial,
                "Dirty close discard changed the saved source");
            auto invalid = initial;
            invalid["samples"][1]["position"] = invalid["samples"][0]["position"];
            atomic_write(root / "Assets/Invalid.blendspace.json", invalid.dump());
            editor.open(project, "Assets/Invalid.blendspace.json");
            frame();
            require(editor.is_open() && !editor.document(),
                    "Invalid source did not remain a visible error document");
            click("button:Close");
            require(!editor.is_open(), "Invalid source error document failed to close");
#include "animation_blend_space_plane_editor_tests.hpp"
            ImGui::DestroyContext();
        }
        std::cout
            << "Actual blend-space controls at100/150/200%, source isolation, history, publication "
               "and Scene attachment passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
