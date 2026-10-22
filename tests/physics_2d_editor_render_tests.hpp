#pragma once
#include "ImGuiImplDiligent.hpp"
#include "editor/component_inspector.hpp"
#include "editor/physics_overlay.hpp"
#include "editor/property_drawer.hpp"
#include "editor/published_tile_map.hpp"
#include "editor/viewport.hpp"
#include "physics_2d_character_capture.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <thread>

namespace forge::test_physics_2d_editor_render {
// Staged native rendering of the shipping Scene guide and property controls.
// Separate input fixtures own selector interaction/Undo/Redo; no OS walkthrough.
template <class Readback, class Save>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              const std::filesystem::path& root, const std::filesystem::path& images,
              const Readback& readback, const Save& save, int joint_kind = -1, bool painted = false,
              bool character = false) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(images);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    AssetId scene_id;
    for (const auto& [id, record] : catalog->records())
        if (!record.subasset &&
            record.source == (painted          ? "Scenes/77-painted-2d-collision.scene.json"
                              : joint_kind < 0 ? "Scenes/75-2d-physics.scene.json"
                                               : "Scenes/76-connected-2d-bodies.scene.json"))
            scene_id = id;
    check(bool(scene_id), "2D editor Gallery Scene missing");
    EngineContext engine(WorldRole::Authoring, false, {physics_schema_module()});
    Scene scene(engine.world());
    auto input = load_game_scene(root, {scene_id});
    if (character)
        input = test::character_capture_source(std::move(input));
    scene.restore_snapshot(input);
    ui::EditorUiContext editor_context;
    editor_context.scene = &scene;
    ui::ContextScope ui_scope(editor_context);
    const auto source = scene.document();
    auto effective = scene.effective_document();
    Json selected;
    for (const auto& row : effective.at("entities"))
        if ((painted && row.at("components").contains("forge.tile_map_collider_2d")) ||
            (!painted && joint_kind < 0 &&
             row.at("components")
                 .contains(character ? "forge.character_controller_2d"
                                     : "forge.capsule_collider_2d")) ||
            (joint_kind >= 0 && row.at("components").contains("forge.physics_joint_2d") &&
             row.at("components").at("forge.physics_joint_2d").at("kind") == joint_kind))
            selected = row;
    check(!selected.is_null(), "2D editor Gallery selected component missing");
    const auto selected_id = selected.at("id").get<std::string>();
    const auto schema = scene.schema();
    std::unique_ptr<RuntimeWorld> runtime;
    Json runtime_debug;
    Double2 capsule_center{2.2, 4.6};
    if (character) {
        runtime = std::make_unique<RuntimeWorld>(std::vector<EngineModule>{}, PhysicsConfig{},
                                                 std::nullopt, root, false);
        runtime_debug = test::prepare_character_capture(*runtime, source);
        effective = runtime->simulation.presentation(1);
        const auto state = runtime->physics()->character_2d(runtime->scene.reference(selected_id));
        const auto& profile = selected.at("components").at("forge.character_controller_2d");
        capsule_center = {state.position[0], state.position[1] +
                                                 profile.at("radius").get<double>() +
                                                 .5 * profile.at("height").get<double>()};
    }
    SceneDocument document(scene);
    ComponentInspector inspector;
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(catalog);
    ui::PublishedTileMapInspection map_view;
    const ui::PublishedTileMapReader read_map = [&](AssetRef<TileMapAsset> asset) {
        return map_view.select(host, asset);
    };
    inspector.tile_map_layer = [&](const Json& components, Json& value) {
        return ui::tile_map_layer_field(read_map, components, value);
    };
    if (painted)
        inspector.fixture_focus_component = "forge.tile_map_collider_2d";
    Viewport viewport(presentation);
    viewport.resources(host);
    EditorCamera camera;
    camera.target = {0, 0, 0};
    camera.yaw = camera.pitch = 0;
    camera.distance = camera.orthographic_height = painted || joint_kind >= 0 ? 16 : 12;
    camera.projection = CameraProjection::Orthographic;
    ui::PhysicsOverlay outline;
    outline.visible = true;
    const auto guide_ready = [&] {
        return joint_kind < 0 ? outline.ready() : outline.status().starts_with("Authored 2D ");
    };
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_UNKNOWN});
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    io.Fonts->AddFontDefaultBitmap();
    for (unsigned index = 0; index < 3; ++index) {
        const float scale = 1 + .5f * index;
        const auto width = unsigned(1280 * scale), height = unsigned(800 * scale);
        ui::style(scale);
        Diligent::TextureDesc desc;
        desc.Name = "2D Scene and Inspector capture";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = Diligent::BIND_RENDER_TARGET;
        Diligent::RefCntAutoPtr<Diligent::ITexture> target;
        presentation.device()->CreateTexture(desc, nullptr, &target);
        check(bool(target), "2D editor capture target allocation failed");
        auto* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        unsigned frames = 0;
        ImVec2 origin{}, area{};
        do {
            presentation.begin_frame();
            io.DisplaySize = {float(width), float(height)};
            gui.NewFrame(width, height, Diligent::SURFACE_TRANSFORM_IDENTITY);
            const float split = width * .67f;
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({split, float(height)}, ImGuiCond_Always);
            ImGui::Begin("Scene - 2D physics", nullptr, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(character
                                       ? "Character: actual ground, velocity and blocked placement"
                                   : painted ? "Selected collision: painted solid layer / XY view"
                                   : joint_kind < 0 ? "Selected collision: capsule / XY view"
                                                    : "Authored 2D joint / XY view");
            origin = ImGui::GetCursorScreenPos();
            area = ImGui::GetContentRegionAvail();
            auto* image = viewport.render(context, effective, unsigned(area.x), unsigned(area.y),
                                          camera, 1, false);
            ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(image)}, area);
            outline.draw(effective, selected_id, camera, origin, area, root, catalog, runtime_debug,
                         read_map);
            ImGui::End();
            ImGui::SetNextWindowPos({split, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({width - split, float(height)}, ImGuiCond_Always);
            ImGui::Begin("Inspector");
            if (painted || joint_kind >= 0 || character) {
                ImGui::TextWrapped("%s", selected.at("name").get<std::string>().c_str());
                if (character)
                    inspector.draw_runtime(
                        effective, schema, root, selected_id,
                        [](const auto&, const auto&, const auto&) { return false; }, false, {},
                        runtime_debug);
                ImGui::BeginDisabled(character);
                inspector.draw(scene, document, selected_id);
                ImGui::EndDisabled();
            } else {
                ImGui::TextWrapped("Dynamic lime capsule - visual children follow the body");
                for (const auto component : {"forge.physics_body_2d", "forge.capsule_collider_2d"})
                    for (const auto& type : schema.at("components"))
                        if (type.at("id") == component &&
                            ImGui::CollapsingHeader(
                                type.at("display_name").get<std::string>().c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen))
                            for (const auto& field : type.at("fields")) {
                                ImGui::PushID(component);
                                auto value = selected.at("components")
                                                 .at(component)
                                                 .at(field.at("id").get<std::string>());
                                check(!property_field(root, field, value),
                                      "Staged 2D Inspector unexpectedly edited a value");
                                ImGui::PopID();
                            }
            }
            ImGui::End();
            context->SetRenderTargets(1, &rtv, nullptr,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float background[]{.02f, .025f, .03f, 1};
            context->ClearRenderTarget(rtv, background,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            gui.Render(context);
            presentation.end_frame(context);
            host->submit();
            context->Flush();
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            ++frames;
            check(std::chrono::steady_clock::now() < deadline,
                  "Native 2D editor image/outline did not settle");
            if (!guide_ready() || (viewport.meshes() && viewport.meshes()->pending()))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frames < 3 || !guide_ready() ||
                 (viewport.meshes() && viewport.meshes()->pending()));
        const auto pixels = readback(rtv);
        save(pixels, width, height,
             images / ("physics2d-editor-" + std::to_string(100 + index * 50) + ".ppm"));
        unsigned text = 0, guide = 0;
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto& p = pixels[y * width + x];
                if (x > width * .67f)
                    text += p[0] > 100 && p[1] > 100 && p[2] > 100;
                else {
                    const float center_x =
                        origin.x + area.x * .5f + float(capsule_center[0]) * area.y / 12;
                    const float center_y =
                        origin.y + area.y * .5f - float(capsule_center[1]) * area.y / 12;
                    if (painted || joint_kind >= 0 ||
                        (std::abs(float(x) - center_x) < .6f * area.y / 12 &&
                         std::abs(float(y) - center_y) < area.y / 12))
                        guide += p[0] < 120 && p[1] > 190 && p[2] > 145;
                }
            }
        check(text > 100 && guide > 20, "Native 2D Inspector text/collision guide pixels missing");
        if (joint_kind >= 0)
            check(outline.status().starts_with("Authored 2D "),
                  "Native joint capture did not draw the actual authored guide");
        if (painted)
            check(outline.status() == "Tile Map collision - published layer outline",
                  "Painted Scene capture did not draw the published physical layer");
        if (character)
            check(outline.status().find("On ground") != std::string::npos &&
                      outline.status().find("shape/placement blocked") != std::string::npos,
                  "Character capture did not draw actual support/blocked feedback");
    }
    check(scene.document() == source && !engine.services().available(Capability::Physics),
          "Staged 2D editor rendering changed source or created a solver");
}
} // namespace forge::test_physics_2d_editor_render
