#pragma once
#include "ImGuiImplDiligent.hpp"
#include "asset_bytes.hpp"
#include "editor/tile_map_editor.hpp"
#include "editor/tile_map_preview.hpp"
#include "sprite_draw_render_tests.hpp"
#include <thread>

namespace forge::test_tile_editor_render {
// Staged native document/Inspector images. Pointer workflows are covered by the
// separate editor fixtures; these captures do not claim a performed walkthrough.
template <class Readback, class Save>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              const std::filesystem::path& gallery, const std::filesystem::path& images,
              const std::filesystem::path& worker, const Readback& readback, const Save& save) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(images);
    const auto root =
        images.parent_path().parent_path() / ("tile-document-" + AssetId::generate().str());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    {
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root, true);
        const auto source = root / "Assets/Tiles/ArrowCourse.tilemap.json";
        const auto baseline = asset_detail::read_bytes(source, tile_map_source_byte_limit);
        TileMapEditor editor(worker);
        editor.open(project, "Assets/Tiles/ArrowCourse.tilemap.json");
        check(editor.document(), "Native Tile Map document did not open");
        TileMapPreview preview(presentation, context, root, editor.document()->source().asset(),
                               editor.document()->source().document.tile_set());
        bool definitions_ready = false;
        editor.draw_preview = [&](auto cells, auto definitions, auto catalog, bool stroke,
                                  bool painting) {
            definitions_ready = bool(definitions);
            return preview.draw(std::move(cells), std::move(definitions), std::move(catalog),
                                stroke, painting);
        };
        Diligent::ImGuiImplDiligent gui({presentation.device(), Diligent::TEX_FORMAT_RGBA8_UNORM,
                                         Diligent::TEX_FORMAT_UNKNOWN});
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        io.Fonts->AddFontDefaultBitmap();
        ui::EditorUiContext state;
        ui::ContextScope scope(state);
        state.selection.select_document_item("tile_map", "map");
        test::observe_ui = true;
        for (unsigned index = 0; index < 3; ++index) {
            const float scale = 1.f + index * .5f;
            const unsigned width = 1280 + index * 320, height = 800 + index * 200;
            ui::style(scale);
            Diligent::TextureDesc desc;
            desc.Name = "Tile Map document capture";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = width;
            desc.Height = height;
            desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = Diligent::BIND_RENDER_TARGET;
            Diligent::RefCntAutoPtr<Diligent::ITexture> target;
            presentation.device()->CreateTexture(desc, nullptr, &target);
            check(bool(target), "Tile Map document capture target allocation failed");
            auto* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            unsigned frames = 0;
            do {
                std::string message;
                editor.poll(project, message);
                presentation.begin_frame();
                test::ui_targets.clear();
                // The generic renderer does not own platform IO/display sizing.
                io.DisplaySize = {float(width), float(height)};
                gui.NewFrame(width, height, Diligent::SURFACE_TRANSFORM_IDENTITY);
                const float center = width * .67f;
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({center, float(height)}, ImGuiCond_Always);
                editor.draw(project, scene, false);
                ImGui::SetNextWindowPos({center, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({width - center, float(height)}, ImGuiCond_Always);
                ImGui::Begin("Inspector");
                editor.inspect(state.selection.member(), false);
                ImGui::End();
                context->SetRenderTargets(1, &rtv, nullptr,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float background[]{.02f, .025f, .03f, 1};
                context->ClearRenderTarget(rtv, background,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                gui.Render(context);
                presentation.end_frame(context);
                context->Flush();
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                ++frames;
                check(std::chrono::steady_clock::now() < end,
                      "Native Tile Map document capture did not settle");
                if (!definitions_ready || preview.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (frames < 4 || !definitions_ready || preview.pending());
            check(editor.diagnostic().empty() && preview.diagnostics().empty(),
                  "Native Tile Map document capture has diagnostics");
            for (const auto* field : {"tile_map:cell_width", "tile_map:cell_height"}) {
                const auto& control = test::ui_targets.at(field);
                check(control.maximum.x <= control.clip_maximum.x &&
                          control.minimum.x >= control.clip_minimum.x,
                      "Native Tile Map Inspector clipped its cell dimension control");
            }
            const auto box = test::ui_targets.at("tile_map:preview_image");
            check(box.minimum.x >= 0 && box.minimum.y >= 0 && box.maximum.x <= width &&
                      box.maximum.y <= height && box.maximum.x - box.minimum.x > 200 * scale &&
                      box.maximum.y - box.minimum.y > 120 * scale,
                  "Native Tile Map document clipped or collapsed its paint canvas");
            const auto pixels = readback(rtv);
            // Preserve failed pixels too; layout rectangles alone cannot prove drawing.
            save(pixels, width, height,
                 images / ("tile-document-" + std::to_string(100 + index * 50) + ".ppm"));
            unsigned inspector_text = 0, colored_cells = 0;
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const auto& pixel = pixels[y * width + x];
                    if (x > width * .67f)
                        inspector_text += pixel[0] > 100 && pixel[1] > 100 && pixel[2] > 100;
                    if (x >= box.minimum.x && x < box.maximum.x && y >= box.minimum.y &&
                        y < box.maximum.y) {
                        const auto [low, high] = std::minmax({pixel[0], pixel[1], pixel[2]});
                        colored_cells += high > 100 && high - low > 60;
                    }
                }
            check(inspector_text > 100 && colored_cells > 100,
                  "Native Tile Map capture did not render Inspector text and colored cells");
        }
        check(asset_detail::read_bytes(source, tile_map_source_byte_limit) == baseline &&
                  !editor.dirty(),
              "Staged Tile Map images changed authored source");
        test::observe_ui = false;
        context->WaitForIdle();
    }
    // Release the writer and native resources before removing this private copy.
    // A failed fixture retains its isolated project for diagnosis.
    std::filesystem::remove_all(root);
}
} // namespace forge::test_tile_editor_render
