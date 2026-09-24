#ifdef FORGE_UI_FIXTURE
#include "authored_capture_fixture.hpp"
#include "editor_capture_layout.hpp"
#include "editor_fixture.hpp"
#include "editor_input_workflow.hpp"
#include "gltf_variant_fixture.hpp"
#include "thumbnail_cache_tests.hpp"
#include <backends/imgui_impl_sdl3.h>
#endif
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "ImGuiImplSDL3.hpp"
#include "actions.hpp"
#include "animation_debug.hpp"
#include "animation_tools.hpp"
#include "asset_actions.hpp"
#include "audio_imports.hpp"
#include "authored_components.hpp"
#include "automation.hpp"
#include "blockout.hpp"
#include "cache_tools.hpp"
#include "camera_controls.hpp"
#include "collision_editor.hpp"
#include "command_workspace.hpp"
#include "component_inspector.hpp"
#include "content.hpp"
#include "content_files.hpp"
#include "content_imports.hpp"
#include "content_thumbnails.hpp"
#include "creation_menu.hpp"
#include "document_workspace.hpp"
#include "ecs_tools.hpp"
#include "files.hpp"
#include "flecs_script.hpp"
#include "frame_renderer.hpp"
#include "game_export.hpp"
#include "game_input.hpp"
#include "help.hpp"
#include "hierarchy.hpp"
#include "material_editor.hpp"
#include "material_preview.hpp"
#include "mesh_material_inspector.hpp"
#include "model_imports.hpp"
#include "model_viewer.hpp"
#include "native_build.hpp"
#include "navigation_tools.hpp"
#include "orientation.hpp"
#include "performance.hpp"
#include "physics_overlay.hpp"
#include "play.hpp"
#include "prefabs.hpp"
#include "project_settings.hpp"
#include "resource_inspector.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_ui_host.hpp"
#include "runtime_ui_tools.hpp"
#include "scene_asset_drop.hpp"
#include "scene_cache.hpp"
#include "scene_lighting.hpp"
#include "scene_tools.hpp"
#include "shader_diligent.hpp"
#include "shader_imports.hpp"
#include "source_import.hpp"
#include "spatial_helpers.hpp"
#include "status_bar.hpp"
#include "texture_imports.hpp"
#include "texture_viewer.hpp"
#include "transform_gesture.hpp"
#include "view_state.hpp"
#include "viewport.hpp"
#include "widgets.hpp"
#include "workspace.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <forge/build.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <tuple>
using namespace Diligent;
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "FORGE editor | Build: " << forge::build_id << '\n';
        return 0;
    }
    std::clog << "FORGE editor | Build: " << forge::build_id << '\n';
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << SDL_GetError();
        return 1;
    }
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("F.O.R.G.E. | Native ECS Editor", 1440, 900,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow);
    if (!window) {
        std::cerr << SDL_GetError();
        SDL_Quit();
        return 1;
    }
    int result = 0;
    try {
#ifdef FORGE_UI_FIXTURE
        forge::test::EditorFixture fixture(argc, argv);
        forge::test::EditorInputWorkflow input_workflow(fixture.workflow, fixture.output,
                                                        fixture.physics ? fixture.project
                                                                        : std::filesystem::path{});
#endif
        auto* factory = LoadAndGetEngineFactoryD3D12();
        if (!factory)
            throw std::runtime_error("D3D12 backend unavailable");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        RefCntAutoPtr<ISwapChain> swap;
        EngineD3D12CreateInfo engine;
#ifdef FORGE_UI_FIXTURE
        factory->SetMessageCallback(&forge::test::EditorFixture::message);
        fixture.device(factory, &device, &context);
#else
        factory->CreateDeviceAndContextsD3D12(engine, &device, &context);
#endif
        if (!device || !context)
            throw std::runtime_error("D3D12 device initialization failed");
        auto hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window.get()),
                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        SwapChainDesc desc;
        desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
        desc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;
        factory->CreateSwapChainD3D12(device, context, desc, FullScreenModeDesc{},
                                      Win32NativeWindow{hwnd}, &swap);
        if (!swap)
            throw std::runtime_error("D3D12 swap chain initialization failed");
        std::string ini;
        auto gui =
            ImGuiImplSDL3::Create(ImGuiDiligentCreateInfo{device, swap->GetDesc()}, window.get());
        forge::ui::style();
        // Reuse the already packaged OFL-licensed runtime UI font; no new dependency.
        const auto editor_font =
            std::filesystem::path(SDL_GetBasePath()) / "resources/ui/LatoLatin-Regular.ttf";
        if (std::filesystem::exists(editor_font))
            ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->AddFontFromFileTTF(
                forge::path_utf8(editor_font).c_str(), 16.f);
        else
            ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->AddFontDefaultVector();
        auto* log_font = ImGui::GetIO().Fonts->AddFontDefaultBitmap();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        char* preferences = SDL_GetPrefPath("FORGE", "Editor");
        if (!preferences)
            throw std::runtime_error("Cannot locate editor preferences directory");
#ifdef FORGE_UI_FIXTURE
        const std::filesystem::path config = fixture.config;
#else
        const std::filesystem::path config = preferences;
#endif
        SDL_free(preferences);
        ini = (config / "workspace.ini").string();
        const auto startup_layout = forge::ui::prepare_layout(config / "workspace.ini");
        forge::ui::load_startup_layout(startup_layout, ini.c_str());
        if (!startup_layout.warning.empty())
            std::clog << startup_layout.warning << '\n';
        const auto settings = config / "settings.json";
        char cmake_path[1024] = "cmake";
        char ninja_path[1024] = "ninja";
        char exact_sdk_root[1024]{};
        bool auto_build = false;
        forge::ui::SceneTools scene_tools;
        forge::ui::SpatialHelpers spatial_helpers;
        bool preview_lighting = true;
        forge::ui::ModalTransform modal;
        forge::ui::OrientationGizmo orientation;
        forge::ui::Workspace workspace;
        forge::ui::EditorUiContext editor;
        forge::ui::ContextScope editor_scope(editor);
        forge::ComponentInspector component_inspector;
        forge::ContentBrowser content;
        forge::ui::PhysicsOverlay physics_overlay;
        forge::ui::RuntimeDependenciesEditor runtime_dependencies;
        forge::ui::GameExportTask game_export;
        forge::ui::DocumentWorkspace documents;
        forge::ui::AssetEditors asset_editors;
        content.editors = &asset_editors;
        bool document_locked = false, asset_document_locked = false;
        forge::BlockoutProperties blockout;
        forge::ui::CommandWorkspace commands;
        char hierarchy_filter[256]{};
        std::vector<std::string> recent_projects;
        std::string last_project;
        try {
            std::ifstream f(settings);
            if (f) {
                forge::Json j;
                f >> j;
                forge::ui::tooltips = j.value("tooltips", true);
                forge::ui::style(j.value("interface_scale", 1.0f));
                SDL_strlcpy(cmake_path, j.value("cmake", std::string("cmake")).c_str(),
                            sizeof(cmake_path));
                SDL_strlcpy(ninja_path, j.value("ninja", std::string("ninja")).c_str(),
                            sizeof(ninja_path));
                SDL_strlcpy(exact_sdk_root, j.value("exact_sdk_root", std::string{}).c_str(),
                            sizeof(exact_sdk_root));
                workspace.load(j);
                content.load_settings(j.value("content_browser", forge::Json::object()));
                orientation.visible = j.value("orientation_gizmo", true);
                physics_overlay.visible = j.value("collision_overlay", false);
                auto_build = j.value("auto_build", false);
                blockout.at_view_target = j.value("create_at_view_target", false);
                scene_tools.grid = j.value("grid", true);
                spatial_helpers.visible = j.value("spatial_helpers", true);
                spatial_helpers.size = std::clamp(j.value("helper_size", 24.f), 16.f, 40.f);
                spatial_helpers.extent = std::clamp(j.value("helper_extent", 10.f), 1.f, 1000.f);
                preview_lighting = j.value("preview_lighting", true);
                scene_tools.move_tool = j.value("scene_tool", std::string("move")) != "select";
                scene_tools.snap = j.value("snap", false);
                scene_tools.snap_step = std::clamp(j.value("snap_step", 1.0f), 0.01f, 1000.0f);
                scene_tools.grid_step = std::clamp(j.value("grid_step", 1.0f), 0.1f, 1000.0f);
                scene_tools.exposure = std::clamp(j.value("exposure", 0.0f), -20.0f, 20.0f);
                scene_tools.fly_speed = std::clamp(j.value("fly_speed", 5.0f), 0.1f, 1000.0f);
                recent_projects = j.value("recent_projects", std::vector<std::string>{});
                last_project = j.value("last_project", std::string{});
            }
        } catch (const std::exception&) { /* Recover malformed user preferences with defaults. */
        }
        auto save_preferences = [&] {
            forge::atomic_write(
                settings, forge::Json{{"tooltips", forge::ui::tooltips},
                                      {"panels", workspace.settings()},
                                      {"content_browser", content.settings()},
                                      {"orientation_gizmo", orientation.visible},
                                      {"collision_overlay", physics_overlay.visible},
                                      {"interface_scale", forge::ui::interface_scale},
                                      {"cmake", cmake_path},
                                      {"ninja", ninja_path},
                                      {"exact_sdk_root", exact_sdk_root},
                                      {"auto_build", auto_build},
                                      {"create_at_view_target", blockout.at_view_target},
                                      {"grid", scene_tools.grid},
                                      {"spatial_helpers", spatial_helpers.visible},
                                      {"helper_size", spatial_helpers.size},
                                      {"helper_extent", spatial_helpers.extent},
                                      {"preview_lighting", preview_lighting},
                                      {"scene_tool", scene_tools.move_tool ? "move" : "select"},
                                      {"snap", scene_tools.snap},
                                      {"snap_step", scene_tools.snap_step},
                                      {"grid_step", scene_tools.grid_step},
                                      {"fly_speed", scene_tools.fly_speed},
                                      {"exposure", scene_tools.exposure},
                                      {"recent_projects", recent_projects},
                                      {"last_project", last_project}}
                              .dump(2));
        };
        forge::EngineContext scene_engine;
        forge::Scene scene(scene_engine.world());
        forge::ui::EcsWorkspace ecs_workspace(scene_engine.world());
        editor.scene = &scene;
        forge::ui::AutomationWorkspace automation;
        forge::PlaySession play;
        forge::GameInput game_input;
        forge::ProjectSettingsEditor project_settings;
        bool scene_lighting_open = false;
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("Cannot locate runtime directory");
        const auto runtime_path = (std::filesystem::path(base) / "forge_runtime.exe").string();
        forge::EditorFiles files(scene, window.get(), recent_projects);
        forge::NavigationTools navigation_tools(std::filesystem::path(base) /
                                                "forge_nav_build.exe");
        forge::AnimationTools animation_tools(std::filesystem::path(base) / "tools/gltf2ozz.exe");
        std::string message = "Ready. Use Add Entity in Scene or Hierarchy.";
        auto perform = [&](auto&& action) {
            try {
                action();
            } catch (const std::exception& e) {
                message = e.what();
                forge::Diagnostic diagnostic{forge::Severity::Error, "editor", message, {}};
                diagnostic.context.asset = scene.asset_id();
                scene_engine.services().emit(std::move(diagnostic));
                forge::ui::report_error("editor", message);
            }
        };
        auto open_scratch = [&] {
            const auto scratch = config / "Projects" / "Scratch";
            if (!std::filesystem::exists(scratch)) {
                std::filesystem::create_directories(scratch.parent_path());
                forge::SceneDocument::create_project(scratch, "Scratch");
            }
            files.start(scratch);
        };
        try {
#ifdef FORGE_UI_FIXTURE
            files.start(fixture.project);
#else
            if (argc > 1)
                files.start(std::filesystem::u8path(argv[1]));
            else if (!last_project.empty())
                files.start(std::filesystem::u8path(last_project));
            else {
                open_scratch();
                message = "Scratch project opened. Use File > New project or Open project for your "
                          "own game.";
            }
#endif
        } catch (const std::exception& e) {
            if (argc > 1)
                throw;
            message = std::string(e.what()) + ". Opening the Scratch project.";
            open_scratch();
        }
        if (!startup_layout.warning.empty())
            message = startup_layout.warning;
        auto native = std::make_unique<forge::NativeBuild>(
            files.document.project(), std::filesystem::path(base) / "sdk", runtime_path);
        native->cmake = cmake_path;
        native->ninja = ninja_path;
        native->auto_build = auto_build;
        auto active_project = files.document.project();
        bool initialize_layout = startup_layout.text.empty();
        forge::DiligentPresentation presentation(device);
        forge::Viewport viewport(presentation, true);
        forge::FrameRenderer game_viewport(presentation);
        std::shared_ptr<forge::MeshResourceHost> mesh_resources;
        auto reset_mesh_resources = [&] {
            viewport.resources({});
            game_viewport.resources({});
            mesh_resources.reset();
            try {
                auto host = std::make_shared<forge::MeshResourceHost>(presentation, context,
                                                                      files.document.project());
                host->catalog(std::make_shared<const forge::AssetCatalog>(
                    forge::AssetCatalog::open_project(files.document.project())));
                viewport.resources(host);
                game_viewport.resources(host);
                mesh_resources = std::move(host);
            } catch (const std::exception& e) {
                message = std::string("Mesh resources unavailable: ") + e.what();
                forge::ui::report_error("render.mesh.resource", message);
            }
        };
        reset_mesh_resources();
        forge::MeshMaterialInspector mesh_material_inspector;
        component_inspector.material_slots = [&](const forge::Json& renderer,
                                                 forge::Json& materials) {
            forge::AssetRef<forge::MeshAsset> mesh;
            if (!renderer.at("mesh").is_null())
                mesh.id = renderer.at("mesh").get<forge::AssetId>();
            mesh_material_inspector.select(files.document.project(),
                                           mesh_resources ? mesh_resources->catalog() : nullptr,
                                           mesh);
            return mesh_material_inspector.draw(materials);
        };
        forge::RuntimeUiHost runtime_ui(window.get(), device,
                                        std::filesystem::path(base) /
                                            "resources/ui/LatoLatin-Regular.ttf");
        forge::RuntimeUiTools runtime_ui_tools;
        forge::AuthoringSnapshot authoring_snapshot;
        forge::PrefabEditor prefab_editor;
        forge::AuthoredComponents authored_components;
        forge::SceneAssetDrop scene_asset_drop;
        authored_components.project_changed(scene, files.document);
        forge::PreviewSnapshot preview_snapshot, game_preview_snapshot;
        bool game_visible = false, focus_game = false;
        forge::EditorCamera camera;
        try {
            forge::restore_view(files.document, camera);
        } catch (const std::exception& e) {
            message = e.what();
        }
        forge::ui::FlecsScriptEditor script_editor(std::filesystem::path(base) / "forge_tools.exe");
        forge::ui::DiagnosticSourceViewer diagnostic_source;
        forge::TextureImportEditor texture_imports(std::filesystem::path(base) /
                                                   "forge_asset_build.exe");
        forge::AudioImportEditor audio_imports(std::filesystem::path(base) /
                                               "forge_asset_build.exe");
        audio_imports.draw_extension = [&](auto&, bool) {
            forge::ui::heading("Clip details",
                               "Metadata of the successfully published audio clip.");
            if (const auto* record = content.record(audio_imports.selected_asset()))
                forge::draw_audio_details(*record);
            else {
                ImGui::TextWrapped("Clip details appear after a successful import.");
                forge::ui::help(
                    "The catalog refreshes asynchronously, even when Content is closed.");
            }
        };
        std::unique_ptr<forge::TextureViewer> texture_viewer;
        forge::TextureViewerDocument texture_view_document(presentation, context);
        texture_imports.preview_before_settings = true;
        texture_imports.draw_extension = [&](auto& document, bool) {
            if (!mesh_resources || !texture_imports.selected_asset())
                return;
            if (!texture_viewer || texture_viewer->project() != document.project())
                texture_viewer = std::make_unique<forge::TextureViewer>(presentation, context,
                                                                        document.project());
            texture_viewer->draw(mesh_resources->catalog(), {texture_imports.selected_asset()},
                                 texture_imports.dirty());
        };
        forge::ModelImportEditor model_imports(
            std::filesystem::path(base) / "forge_asset_build.exe",
            std::filesystem::path(base) / "tools/gltf2ozz.exe", scene, editor.selection);
        model_imports.placement_allowed = [&] { return !document_locked; };
        std::unique_ptr<forge::ModelViewer> model_viewer;
        forge::AssetViewerDocument asset_view_document(presentation, context);
        std::unique_ptr<forge::ContentThumbnails> content_thumbnails;
        content.thumbnail = [&](forge::AssetId id) {
            if (!mesh_resources)
                return forge::ContentThumbnail{};
            if (!content_thumbnails) {
                content_thumbnails = std::make_unique<forge::ContentThumbnails>(
                    presentation, context, files.document.project(), mesh_resources);
                content_thumbnails->begin(mesh_resources->catalog());
            }
            const auto view = content_thumbnails->request(id);
            const auto aspect = view.image ? float(view.image->GetTexture()->GetDesc().Width) /
                                                 view.image->GetTexture()->GetDesc().Height
                                           : 1.f;
            return forge::ContentThumbnail{reinterpret_cast<ImTextureID>(view.image), aspect,
                                           view.status};
        };
        content.retry_thumbnail = [&](forge::AssetId id) {
            if (content_thumbnails)
                content_thumbnails->retry(id);
        };
        model_imports.preview_before_settings = true;
        model_imports.draw_preview = [&](auto& document, bool draft) {
            if (!mesh_resources || !model_imports.selected_asset())
                return;
            if (!model_viewer || model_viewer->project() != document.project())
                model_viewer = std::make_unique<forge::ModelViewer>(
                    presentation, context, document.project(), mesh_resources);
            model_viewer->draw(mesh_resources->catalog(), model_imports.selected_asset(), draft);
        };
        asset_view_document.open_source = [&](forge::AssetId asset) {
            if (!mesh_resources)
                return;
            const auto catalog = mesh_resources->catalog();
            const auto found = catalog->records().find(asset);
            if (found != catalog->records().end())
                model_imports.open(files.document, found->second.source);
        };
        forge::ShaderImportEditor shader_imports(
            std::filesystem::path(base) / "forge_shader_build.exe", [] {
                return forge::asset_detail::ShaderCompilerProfile{
                    forge::asset_detail::diligent_shader_compiler_digest(),
                    forge::asset_detail::diligent_shader_compiler_debug()};
            });
        forge::MaterialEditor material_editor;
        forge::CollisionEditor collision_editor;
        std::unique_ptr<forge::MaterialPreview> material_preview;
        std::shared_ptr<const forge::AssetCatalog> material_preview_catalog;
        material_editor.update_preview = [&](auto ref, auto data, auto catalog) {
            if (!material_preview)
                material_preview = std::make_unique<forge::MaterialPreview>(
                    presentation, context, files.document.project(), catalog);
            material_preview->catalog(catalog);
            material_preview->material(ref, std::move(data));
            material_preview_catalog = std::move(catalog);
        };
        material_editor.draw_preview = [&](bool stacked) {
            if (material_preview && material_preview_catalog)
                material_preview->draw(*material_preview_catalog, stacked);
        };
        material_editor.release_preview = [&] {
            material_preview.reset();
            material_preview_catalog.reset();
        };
        forge::ContentImports content_imports;
        forge::ContentFiles content_files;
        forge::CacheTools cache_tools;
        forge::ui::ResourceInspector resource_inspector;
        resource_inspector.reveal = [&](forge::AssetId id) {
            editor.selection.select_asset(id);
            editor.reveal_content = true;
        };
        forge::SourceImport source_import;
        std::vector<std::filesystem::path> dropped_sources;
        bool source_drop_rejected = false, source_drop_position = false;
        files.external_busy = [&] {
            return cache_tools.busy() || content_files.busy() || authored_components.busy() ||
                   scene_asset_drop.busy() || source_import.busy();
        };

        content_files.unavailable = [&](const forge::AssetRecord& asset,
                                        forge::AssetFileAction action) -> std::string {
            if (play.active())
                return "Stop Play before changing source files.";
            if (native->busy() || files.busy() || animation_tools.pending() ||
                navigation_tools.pending() || script_editor.pending() ||
                texture_imports.pending() || audio_imports.pending() || model_imports.pending() ||
                shader_imports.pending() || material_editor.pending() || collision_editor.pending())
                return "Finish the current file/import/build job before changing source files.";
            if (documents.source_drafts_dirty())
                return "Save or discard open source-document drafts before reviewing file changes.";
            if (action == forge::AssetFileAction::Delete && asset.id == scene.asset_id())
                return "Open another scene before deleting the active scene asset.";
            return {};
        };
        content_files.adopted = [&](const auto& review, const auto&, auto catalog) {
            const auto& plan = review.plan;
            if (plan.request.action == forge::AssetFileAction::Move) {
                const auto old = std::find_if(plan.changes.begin(), plan.changes.end(),
                                              [](const auto& c) { return c.before && !c.after; });
                if (old != plan.changes.end())
                    files.document.source_relocated(old->source, plan.request.destination);
            }
            if (plan.request.action != forge::AssetFileAction::Duplicate) {
                const auto id = plan.request.asset;
                const auto refresh_import = [&](auto& document) {
                    if (document.selected_asset() == id) {
                        document.request_close();
                        if (plan.request.action == forge::AssetFileAction::Move)
                            document.open(files.document, plan.request.destination);
                    }
                };
                refresh_import(texture_imports);
                refresh_import(audio_imports);
                refresh_import(model_imports);
                refresh_import(shader_imports);
                if (material_editor.document() &&
                    material_editor.document()->source().asset() == id) {
                    material_editor.request_close();
                    if (plan.request.action == forge::AssetFileAction::Move)
                        material_editor.open(files.document, plan.request.destination);
                }
                if (collision_editor.document() &&
                    collision_editor.document()->source().asset() == id) {
                    collision_editor.request_close();
                    if (plan.request.action == forge::AssetFileAction::Move)
                        collision_editor.open(files.document, plan.request.destination);
                }
                if (files.document.prefabs().records().contains(id))
                    prefab_editor.request_close();
            }
            if (mesh_resources)
                mesh_resources->catalog(catalog);
            material_editor.asset_catalog_changed(catalog);
            collision_editor.asset_catalog_changed(catalog);
            play.model_assets_changed();
            if (plan.request.action == forge::AssetFileAction::Delete) {
                if (editor.selection.kind() == forge::ui::SelectionKind::Asset &&
                    std::find(plan.affected.begin(), plan.affected.end(),
                              editor.selection.asset()) != plan.affected.end())
                    editor.selection.clear();
            } else
                editor.selection.select_asset(plan.result);
            content.refresh(files);
            // Refresh only the affected prefab family, preserving the usual missing-reference
            // diagnostics.
            if (files.document.prefabs().records().contains(plan.request.asset) ||
                (catalog->records().contains(plan.result) &&
                 catalog->records().at(plan.result).type == "prefab"))
                files.document.prefabs().refresh(scene);
        };
        content_imports.routes = [&] {
            auto routes = texture_imports.automatic_routes();
            for (auto route : audio_imports.automatic_routes())
                routes.push_back(std::move(route));
            for (auto route : model_imports.automatic_routes())
                routes.push_back(std::move(route));
            for (auto route : shader_imports.automatic_routes())
                routes.push_back(std::move(route));
            routes.push_back({"forge.material.builtin", forge::desktop_texture_target(),
                              forge::material_import_registry(),
                              [](auto& c, const auto& plan, const auto&) {
                                  forge::prepare_material_publication(c, plan);
                              }});
            routes.push_back({"forge.collision.builtin", forge::collision_import_target(),
                              forge::collision_import_registry(),
                              [](auto& c, const auto& p, const auto&) {
                                  forge::prepare_collision_publication(c, p);
                              }});
            return routes;
        };
        content_imports.blocked = [&](forge::AssetId id) {
            return files.busy() ||
                   (collision_editor.document() &&
                    collision_editor.document()->source().asset() == id &&
                    collision_editor.dirty()) ||
                   (texture_imports.selected_asset() == id && texture_imports.dirty()) ||
                   (audio_imports.selected_asset() == id && audio_imports.dirty()) ||
                   (model_imports.selected_asset() == id && model_imports.dirty()) ||
                   (shader_imports.selected_asset() == id && shader_imports.dirty()) ||
                   (material_editor.document() &&
                    material_editor.document()->source().asset() == id && material_editor.dirty());
        };
        content_imports.published = [&](forge::AssetId id, auto catalog) {
            texture_imports.source_published(id);
            audio_imports.source_published(id);
            model_imports.source_published(id);
            shader_imports.source_published(id);
            material_editor.source_published(files.document, id);
            collision_editor.source_published(files.document, id);
            collision_editor.asset_catalog_changed(catalog);
            material_editor.asset_catalog_changed(catalog);
            if (mesh_resources)
                mesh_resources->catalog(catalog);
            play.model_assets_changed();
            content.refresh(files);
        };
        content.rescan_sources = [&] { content_imports.rescan(); };
        source_import.routes = content_imports.routes;
        source_import.published = content_imports.published;
        content.import_files = [&] { source_import.picker(files.document, window.get()); };
        content.reimport = [&](const auto& assets) { content_imports.reimport(assets); };
        content.import_status = [&] { content_imports.status(); };
        content.import_activity = [&] {
            std::map<forge::AssetId, forge::ContentState> result;
            for (const auto& [id, state] : content_imports.activity())
                result[id] = state == forge::AssetJobState::Failed ? forge::ContentState::Failed
                             : state == forge::AssetJobState::Queued
                                 ? forge::ContentState::Queued
                                 : forge::ContentState::Importing;
            return result;
        };
        content.open_source = [&](const auto& path, const std::string& kind, bool open) {
            if (kind == "model") {
                if (open)
                    model_imports.open(files.document, path);
            } else if (kind == "image") {
                if (open)
                    texture_imports.open(files.document, path);
            } else if (kind == "audio") {
                if (open)
                    audio_imports.open(files.document, path);
            } else if (kind == "collision") {
                if (open)
                    collision_editor.open(files.document, path);
            } else if (kind == "material") {
                if (open)
                    material_editor.open(files.document, path);
            } else if (kind == "shader_program") {
                if (open)
                    shader_imports.open(files.document, path);
            } else
                return false;
            return true;
        };
        documents.add({"content.source",
                       "Source file",
                       "Content",
                       false,
                       [&] { return true; },
                       {},
                       {},
                       {},
                       {},
                       {},
                       {},
                       {},
                       {},
                       [&](const std::string& locator) { content.inspect_source(locator); }});
        forge::Telemetry telemetry;
        forge::ui::Performance performance;
        auto& selected = editor.selection.entity_slot();
        std::string name_entity, authored_name;
        std::optional<forge::EditorFiles::Action> pending_switch;
        files.before_request = [&](const forge::EditorFiles::Action& action) {
            if (game_export.busy() || runtime_dependencies.busy()) {
                message =
                    "Finish or cancel the export/dependency task before switching or closing.";
                return false;
            }
            if (runtime_dependencies.dirty()) {
                runtime_dependencies.reveal_draft();
                message = "Save declarations or discard the Runtime Dependencies draft in "
                          "Inspector before switching or closing.";
                return false;
            }
            if (content_files.busy()) {
                message =
                    "Finish or cancel the Content file operation before switching or closing.";
                return false;
            }
            if (documents.close_pending_sources())
                return true;
            pending_switch = action;
            return false;
        };
        documents.add({"scene",
                       "Scene",
                       "Scene###Scene",
                       true,
                       [] { return true; },
                       [&] { return files.document.dirty(); },
                       {},
                       [&] { files.save(); },
                       [&] { forge::authoring_history(scene, false); },
                       [&] { forge::authoring_history(scene, true); },
                       {},
                       [&] { return scene.can_undo(); },
                       [&] { return scene.can_redo(); },
                       {}});
        documents.add({"prefab",
                       "Prefab source",
                       "Prefab source###Prefab source",
                       true,
                       [&] { return prefab_editor.is_open(); },
                       [&] { return prefab_editor.dirty(); },
                       [&] { prefab_editor.draw(scene, files.document, document_locked); },
                       [&] { prefab_editor.request_save(); },
                       {},
                       {},
                       [&] { prefab_editor.request_close(); },
                       {},
                       {},
                       {},
                       [&] { return std::exchange(prefab_editor.close_cancelled, false); }});
        documents.add(
            {"settings",
             "Project Settings",
             "Project Settings###Project Settings",
             false,
             [&] { return project_settings.is_open(); },
             [&] { return project_settings.dirty(); },
             [&] { project_settings.draw(files.document, scene, document_locked, message); },
             [&] { project_settings.request_save(); },
             {},
             {},
             [&] { project_settings.request_close(); },
             {},
             {},
             {},
             [&] { return std::exchange(project_settings.close_cancelled, false); }});
        documents.add({"flecs_script",
                       "Flecs Script",
                       "Flecs Script###Flecs Script",
                       true,
                       [&] { return script_editor.is_open(); },
                       [&] { return script_editor.dirty(); },
                       [&] { script_editor.draw(files.document, document_locked); },
                       [&] { script_editor.request_save(); },
                       {},
                       {},
                       [&] { script_editor.request_close(); },
                       {},
                       {},
                       {},
                       [&] { return std::exchange(script_editor.close_cancelled, false); }});
        asset_editors.add(
            {"flecs_script", "Edit Flecs Script",
             [&](const forge::AssetRecord& asset) { script_editor.open(files.document, asset); }});
        asset_editors.add({"scene", "Open scene", [&](const forge::AssetRecord& a) {
                               files.request({forge::EditorFiles::Command::OpenScene,
                                              files.document.project() / a.source,
                                              {}});
                           }});
        asset_editors.add({"prefab", "Edit prefab source", [&](const forge::AssetRecord& a) {
                               prefab_editor.edit_source(files.document, a.id);
                           }});
        documents.add({"audio_import",
                       "Audio clip",
                       "Audio clip###Audio clip",
                       true,
                       [&] { return audio_imports.is_open(); },
                       [&] { return audio_imports.dirty(); },
                       [&] { audio_imports.draw(files.document, asset_document_locked); },
                       [&] { audio_imports.request_save(); },
                       {},
                       {},
                       [&] { audio_imports.request_close(); },
                       {},
                       {},
                       {},
                       [&] { return std::exchange(audio_imports.close_cancelled, false); }});
        asset_editors.add({"audio_clip", "Open audio clip", [&](const forge::AssetRecord& asset) {
                               audio_imports.open(files.document, asset.source);
                           }});
        documents.add({"texture_import",
                       "Texture import",
                       "Texture import###Texture import",
                       true,
                       [&] { return texture_imports.is_open(); },
                       [&] { return texture_imports.dirty(); },
                       [&] { texture_imports.draw(files.document, asset_document_locked); },
                       [&] { texture_imports.request_save(); },
                       {},
                       {},
                       [&] { texture_imports.request_close(); },
                       {},
                       {},
                       {},
                       [&] { return std::exchange(texture_imports.close_cancelled, false); }});
        documents.add({"texture_viewer",
                       "Texture",
                       "Texture###Texture viewer",
                       true,
                       [&] { return texture_view_document.is_open(); },
                       {},
                       [&] {
                           texture_view_document.draw(files.document.project(),
                                                      mesh_resources ? mesh_resources->catalog()
                                                                     : nullptr);
                       },
                       {},
                       {},
                       {},
                       [&] { texture_view_document.close(); }});
        asset_editors.add({"texture", "Open texture", [&](const forge::AssetRecord& asset) {
                               if (asset.subasset)
                                   texture_view_document.open(files.document.project(), asset.id);
                               else
                                   texture_imports.open(files.document, asset.source);
                           }});
        documents.add({"model_import",
                       "Model import",
                       "Model import###Model import",
                       true,
                       [&] { return model_imports.is_open(); },
                       [&] { return model_imports.dirty(); },
                       [&] { model_imports.draw(files.document, asset_document_locked); },
                       [&] { model_imports.request_save(); },
                       {},
                       {},
                       [&] { model_imports.request_close(); },
                       {},
                       {},
                       {},
                       [&] { return std::exchange(model_imports.close_cancelled, false); }});
        documents.add({"asset_viewer",
                       "Asset preview",
                       "Asset preview###Asset viewer",
                       true,
                       [&] { return asset_view_document.is_open(); },
                       {},
                       [&] { asset_view_document.draw(files.document.project(), mesh_resources); },
                       {},
                       {},
                       {},
                       [&] { asset_view_document.close(); }});
        asset_editors.add({"mesh", "View mesh", [&](const forge::AssetRecord& asset) {
                               asset_view_document.open(files.document.project(), asset);
                           }});
        asset_editors.add(
            {"model", "Import settings / Place", [&](const forge::AssetRecord& asset) {
                 model_imports.open(files.document, asset.source);
             }});
        documents.add({"shader_import",
                       "Shader import",
                       "Shader import###Shader import",
                       true,
                       [&] { return shader_imports.is_open(); },
                       [&] { return shader_imports.dirty(); },
                       [&] { shader_imports.draw(files.document, asset_document_locked); },
                       [&] { shader_imports.request_save(); },
                       {},
                       {},
                       [&] { shader_imports.request_close(); },
                       {},
                       {},
                       {},
                       [&] { return std::exchange(shader_imports.close_cancelled, false); }});
        asset_editors.add(
            {"shader", "Import settings / Compile", [&](const forge::AssetRecord& asset) {
                 shader_imports.open(files.document, asset.source);
             }});
        documents.add({"material",
                       "Material",
                       "Material###Material",
                       true,
                       [&] { return material_editor.is_open(); },
                       [&] { return material_editor.dirty(); },
                       [&] { material_editor.draw(files.document, asset_document_locked); },
                       [&] { material_editor.request_save(); },
                       [&] { material_editor.undo(); },
                       [&] { material_editor.redo(); },
                       [&] { material_editor.request_close(); },
                       [&] { return material_editor.can_undo(); },
                       [&] { return material_editor.can_redo(); },
                       {},
                       [&] { return std::exchange(material_editor.close_cancelled, false); }});
        asset_editors.add({"material", "Open material", [&](const forge::AssetRecord& asset) {
                               if (asset.subasset)
                                   asset_view_document.open(files.document.project(), asset);
                               else
                                   material_editor.open(files.document, asset.source);
                           }});
        documents.add({"collision",
                       "Collision",
                       "Collision###Collision",
                       true,
                       [&] { return collision_editor.is_open(); },
                       [&] { return collision_editor.dirty(); },
                       [&] { collision_editor.draw(files.document, asset_document_locked); },
                       [&] { collision_editor.request_save(); },
                       [&] { collision_editor.undo(); },
                       [&] { collision_editor.redo(); },
                       [&] { collision_editor.request_close(); },
                       [&] { return collision_editor.can_undo(); },
                       [&] { return collision_editor.can_redo(); },
                       {},
                       [&] { return std::exchange(collision_editor.close_cancelled, false); }});
        asset_editors.add({"collision", "Open collision", [&](const forge::AssetRecord& asset) {
                               collision_editor.open(files.document, asset.source);
                           }});
        files.save_active = [&] {
            if (content_files.busy() || cache_tools.busy())
                throw std::runtime_error("Finish the Content file/cache operation before saving");
            if (!documents.save(editor.task.id()))
                throw std::runtime_error("Active document cannot save");
        };
        char entity_name[1024]{};
        bool running = true;
        std::string current_title;
#ifdef FORGE_UI_FIXTURE
        if (!fixture.workflow) {
            editor.selection.select_entity(
                forge::authoring_command(scene, "entity.create", {{"name", "Fixture cube"}})
                    .at("selected"));
            forge::authoring_command(scene, "component.add",
                                     {{"entity", selected}, {"component", "forge.ui_document"}});
            forge::authoring_command(scene, "component.add",
                                     {{"entity", selected}, {"component", "forge.audio_source"}});
            fixture.prefab = files.document.prefabs().create(
                scene, forge::create_prefab_source(scene, selected), "Assets/Fixture.prefab.json");
            files.document.save();
        }
#endif
        content.runtime_dependencies = [&](const forge::AssetCatalog& catalog,
                                           const forge::AssetRecord& asset) {
            runtime_dependencies.draw(
                files.document, catalog, asset,
                asset_document_locked || play.active() || native->busy() || files.busy() ||
                    files.changed || cache_tools.busy() || source_import.busy() ||
                    content_files.busy(),
                [&](const forge::AssetRecord& target) { asset_editors.open(target); });
        };
        content.action_set = [&](const forge::AssetRecord* explicit_target) {
            forge::ui::AssetActionContext context;
            bool selection_complete = true;
            if (explicit_target) {
                context.target = *explicit_target;
                context.selected = {explicit_target->id};
            } else {
                if (editor.selection.kind() == forge::ui::SelectionKind::Asset) {
                    if (const auto* target = content.record(editor.selection.asset()))
                        context.target = *target;
                    context.selected = content.selected_assets();
                    if (context.target &&
                        std::find(context.selected.begin(), context.selected.end(),
                                  context.target->id) == context.selected.end())
                        context.selected = {context.target->id};
                    else
                        selection_complete = content.selected_assets_complete();
                }
            }
            if (game_export.busy() || runtime_dependencies.busy() || play.active() ||
                native->busy() || files.busy() || files.changed || cache_tools.busy() ||
                source_import.busy() || content_files.busy() || scene_asset_drop.busy() ||
                scene_tools.move.active() || modal.active() || blockout.active())
                context.blocked =
                    "Stop Play and finish the current gesture, file, import, or cache operation.";
            context.openable = context.target && asset_editors.find(context.target->type);
            context.placeable = context.target && (context.target->type == "model" ||
                                                   context.target->type == "mesh" ||
                                                   context.target->type == "prefab");
            context.reimportable = !context.selected.empty() && selection_complete;
            for (auto id : context.selected) {
                const auto* asset = content.record(id);
                if (asset && asset->subasset)
                    asset = content.record(asset->subasset->owner);
                context.reimportable &= asset && asset->metadata.contains("forge.import");
            }
            if (context.target)
                context.file_blocked =
                    content_files.unavailable(*context.target, forge::AssetFileAction::Move);
            forge::ui::AssetActionHandlers handlers;
            handlers.import_files = [&] { source_import.picker(files.document, window.get()); };
            handlers.open = [&](const auto& asset) { asset_editors.open(asset); };
            handlers.collision = [&](const auto& asset) { collision_editor.from_mesh(asset); };
            handlers.reimport = [&](const auto& ids) { content_imports.reimport(ids); };
            handlers.files = [&](const auto& asset, auto op) { content_files.begin(asset, op); };
            handlers.place = [&](const auto& asset) {
                const auto target =
                    blockout.at_view_target ? camera.target : forge::Float3{0, 0, 0};
                scene_asset_drop.queue(asset, scene, files.document,
                                       {target[0], target[1], target[2]});
            };
            handlers.cache = [&] { cache_tools.open(); };
            auto result = forge::ui::asset_actions(std::move(context), handlers);
            for (auto& action : result.entries) {
                if (action.id == "asset.delete" &&
                    (explicit_target ? explicit_target->id : editor.selection.asset()) ==
                        scene.asset_id()) {
                    action.available = false;
                    action.unavailable_reason = "Open another scene before deleting this source.";
                }
                const auto run = action.execute;
                action.execute = [&, run] { perform(run); };
            }
            return result;
        };
        while (running) {
#ifdef FORGE_UI_FIXTURE
            fixture.graphics_context(window.get(), 0);
#endif
            performance.begin();
#ifdef FORGE_UI_FIXTURE
            if (fixture.workflow)
                input_workflow.platform_input(SDL_GetWindowID(window.get()));
#endif
            SDL_Event event;
            game_input.pump(play,
                            workspace.game && game_visible && !files.busy() &&
                                !ImGui::GetIO().WantTextInput &&
                                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                                 ImGuiPopupFlags_AnyPopupLevel) &&
                                (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS));
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_DROP_BEGIN) {
                    dropped_sources.clear();
                    source_drop_rejected = false;
                    source_drop_position = false;
                }
                if (event.type == SDL_EVENT_DROP_POSITION)
                    source_drop_position = true;
                if (event.type == SDL_EVENT_DROP_FILE) {
                    const auto origin = ImGui::GetMainViewport()->Pos;
                    float drop_x = event.drop.x, drop_y = event.drop.y;
                    // SDL3's legacy Windows WM_DROPFILES path emits no DROP_POSITION.
                    // Use the current window-relative pointer only for that path.
                    if (!source_drop_position)
                        SDL_GetMouseState(&drop_x, &drop_y);
                    if (files.busy() || native->busy() || play.active() ||
                        !content.accepts_file_drop({origin.x + drop_x, origin.y + drop_y}) ||
                        dropped_sources.size() >= 256 || !event.drop.data) {
                        source_drop_rejected = true;
                        message =
                            "Drop up to 256 raw source files onto Content after finishing the "
                            "current operation.";
                    } else {
                        dropped_sources.push_back(std::filesystem::u8path(event.drop.data));
                    }
                }
                if (event.type == SDL_EVENT_DROP_COMPLETE) {
                    if (!source_drop_rejected && !dropped_sources.empty()) {
                        try {
                            source_import.select(files.document, std::move(dropped_sources));
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                    }
                    dropped_sources.clear();
                }
                const bool zoom_key =
                    (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                    (event.key.mod & SDL_KMOD_CTRL) &&
                    (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS ||
                     event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
                     event.key.key == SDLK_KP_PLUS || event.key.key == SDLK_0 ||
                     event.key.key == SDLK_KP_0);
                if (zoom_key || (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                                 game_input.outside(event.button.x, event.button.y)))
                    game_input.release(play);
                if (runtime_ui.event(event, game_input, play))
                    continue;
                if (game_input.event(event, play))
                    continue;
                gui->HandleSDLEvent(&event);
                if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & SDL_KMOD_CTRL)) {
                    float scale = forge::ui::interface_scale;
                    if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                        scale -= 0.1f;
                    else if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
                             event.key.key == SDLK_KP_PLUS)
                        scale += 0.1f;
                    else if (event.key.key == SDLK_0 || event.key.key == SDLK_KP_0)
                        scale = 1.0f;
                    if (scale != forge::ui::interface_scale) {
                        forge::ui::style(scale);
                        try {
                            perform(save_preferences);
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                    }
                }
                if (event.type == SDL_EVENT_QUIT)
                    files.request({forge::EditorFiles::Command::Quit, {}, {}});
            }
            scene_asset_drop.poll(scene, files, editor.selection,
                                  play.active() || native->busy() || content_files.busy() ||
                                      authored_components.busy(),
                                  message);
            files.pump(!native->busy());
            if (files.changed) {
                play.stop();
                editor.selection.clear();
                camera = {};
                scene_tools.move.cancel();
                modal.cancel();
                blockout.cancel();
                try {
                    forge::restore_view(files.document, camera);
                } catch (const std::exception& e) {
                    message = e.what();
                }
                if (active_project != files.document.project()) {
                    active_project = files.document.project();
                    perform([&] { authored_components.project_changed(scene, files.document); });
                    reset_mesh_resources();
                    editor.problems.reset();
                    editor.log.clear();
                    editor.last_status.clear();
                    native = std::make_unique<forge::NativeBuild>(
                        active_project, std::filesystem::path(base) / "sdk", runtime_path);
                    native->cmake = cmake_path;
                    native->ninja = ninja_path;
                    native->auto_build = auto_build;
                }
                files.changed = false;
            }
            if (files.preferences_changed) {
                last_project = forge::path_text(files.document.project());
                try {
                    perform(save_preferences);
                } catch (const std::exception& e) {
                    message = e.what();
                }
                files.preferences_changed = false;
            }
            if (files.quit) {
                running = false;
                continue;
            }
            play.pump();
            authored_components.poll(scene, files.document);
            runtime_ui.sync(play, files.document.project(), workspace.game);
            native->simulation_hz = files.document.settings().simulation_hz();
            native->gravity = files.document.settings().physics().gravity;
            if (!files.document.settings().requires_native_sdk())
                native->pump(play, authoring_snapshot.snapshot(scene));
            files.set_switch_available(!game_export.busy() && !native->busy());
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window.get(), &width, &height);
            if (width <= 0 || height <= 0 ||
                SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) {
                telemetry.pause();
                SDL_Delay(20);
                continue;
            }
            if (swap->GetDesc().Width != unsigned(width) ||
                swap->GetDesc().Height != unsigned(height))
                swap->Resize(width, height);
            const std::string automation_busy =
                play.active()                     ? "Stop play before editing"
                : native->busy()                  ? "Wait for the native build"
                : (files.busy() || files.changed) ? "Finish the file operation"
                : (scene_tools.move.active() || modal.active() || blockout.active() ||
                   ImGui::IsAnyItemActive() ||
                   ImGui::IsPopupOpen(nullptr,
                                      ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    ? "Finish the active UI interaction"
                    : "";
            automation.pump(files.document, automation_busy);
#ifdef FORGE_UI_FIXTURE
            if (fixture.workflow) {
                ImGui_ImplSDL3_NewFrame();
                input_workflow.input();
                gui->ImGuiImplDiligent::NewFrame(width, height, swap->GetDesc().PreTransform);
            } else if (fixture.stage >= 26) {
                // Run the platform update once, then override its real desktop pointer
                // before Dear ImGui consumes queued input for these static captures.
                ImGui_ImplSDL3_NewFrame();
                ImGui::GetIO().ConfigInputTrickleEventQueue = false;
                ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                gui->ImGuiImplDiligent::NewFrame(width, height, swap->GetDesc().PreTransform);
                // Pinned IsItemHovered also considers the keyboard navigation cursor.
                // Static captures have no active keyboard gesture; keep production help intact.
                ImGui::SetNavCursorVisible(false);
            } else
#endif
                gui->NewFrame(width, height, swap->GetDesc().PreTransform);
            // Preserve live numeric edits; ImGui 1.92.9 changed its default to commit-on-exit.
            ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);
            telemetry.frame();
            forge::ui::status_bar(
                telemetry, play.active(), scene.entity_count(),
                play.can_recover() ? "CRASHED"
                : play.active()    ? (play.paused() ? "PAUSED" : "PLAYING")
                                   : "EDIT",
                editor.problems.size(), !selected.empty(), native->busy(),
                [&] {
                    workspace.toggle_bottom();
                    perform(save_preferences);
                },
                workspace.bottom_folded);
            const auto dock = ImGui::DockSpaceOverViewport();
            const bool rebuilt_workspace = initialize_layout || workspace.reset;
            if (rebuilt_workspace) {
                if (ImGui::GetMainViewport()->WorkSize.y / forge::ui::interface_scale < 480)
                    workspace.bottom_folded = true;
                workspace.reset = false;
                forge::ui::initialize_workspace(dock,
                                                [&](ImGuiID center) { documents.dock(center); });
                initialize_layout = false;
            }
            game_export.poll(content_imports, [&] { content.refresh(files); });
            runtime_dependencies.poll([&] { content.refresh(files); });
            const bool edit_locked = game_export.busy() || runtime_dependencies.busy() ||
                                     play.active() || native->busy() || files.busy() ||
                                     scene_tools.move.active() || modal.active() ||
                                     blockout.active();
            document_locked = edit_locked;
            asset_document_locked = game_export.busy() || runtime_dependencies.busy() ||
                                    native->busy() || files.busy() || scene_tools.move.active() ||
                                    modal.active() || blockout.active();
            editor.selection.reconcile(scene.document());
            if (editor.task.owner == forge::ui::DocumentTask::Extension &&
                !documents.available(editor.task.id()))
                editor.task.owner = forge::ui::DocumentTask::Scene;
            if ((editor.task.owner == forge::ui::DocumentTask::Prefab &&
                 !prefab_editor.is_open()) ||
                (editor.task.owner == forge::ui::DocumentTask::Settings &&
                 !project_settings.is_open()))
                editor.task.owner = forge::ui::DocumentTask::Scene;
            const bool scene_task = editor.task.owner == forge::ui::DocumentTask::Scene;
            forge::ui::EditorActions actions;
            auto add_action = [&](std::string id, std::string label, std::string shortcut,
                                  std::string help, bool enabled, std::function<void()> run) {
                actions.entries.push_back({std::move(id),
                                           std::move(label),
                                           std::move(shortcut),
                                           std::move(help),
                                           enabled,
                                           [&, run] { perform(run); },
                                           {}});
                auto& action = actions.entries.back();
                if (!enabled) {
                    if (action.id == "pause" || action.id == "stop")
                        action.unavailable_reason =
                            "Start Play and wait for the runtime to become ready.";
                    else if (action.id == "step")
                        action.unavailable_reason = "Pause a ready Play session first.";
                    else if (action.id == "recover")
                        action.unavailable_reason = "No recoverable runtime checkpoint, or a "
                                                    "file/build operation is active.";
                    else if (edit_locked)
                        action.unavailable_reason =
                            "Stop Play or finish the active gesture, file operation or build.";
                    else if (action.id == "undo" || action.id == "redo")
                        action.unavailable_reason = "This task has no available history step. "
                                                    "Prefab/Settings drafts have no Undo history.";
                    else if (!scene_task)
                        action.unavailable_reason =
                            "Select the Scene task for this authored-entity action.";
                    else
                        action.unavailable_reason = "Select an authored entity for this action.";
                }
            };
            add_action(
                "game.export", "Export Game...", "",
                "Build a relocatable Development standalone game from saved project content.", true,
                [&] {
                    const auto modules =
                        files.document.settings().document().value("modules", forge::Json::array());
                    game_export.open(
                        std::any_of(modules.begin(), modules.end(),
                                    [](const auto& module) { return module.is_object(); }));
                });
            for (auto action : content.action_set(nullptr).entries)
                actions.entries.push_back(std::move(action));
            actions.entries.push_back(model_imports.placement_action(files.document));
            add_action(
                "scene.lighting", "Scene / Lighting", "",
                "Edit environment lighting, sky and game exposure using Scene Save and Undo.", true,
                [&] { scene_lighting_open = true; });
            add_action("save", std::string("Save / ") + editor.task.name(), "Ctrl+S",
                       "Save or Publish the active task. Scene, prefab and settings have "
                       "independent ownership.",
                       !edit_locked, [&] { files.save_active(); });
            add_action(
                "undo", std::string("Undo / ") + editor.task.name(), "Ctrl+Z",
                "Undo in the active task. Prefab and Settings drafts do not provide history.",
                !edit_locked && documents.history(editor.task.id(), false),
                [&] { documents.undo(editor.task.id(), false); });
            add_action("redo", std::string("Redo / ") + editor.task.name(), "Ctrl+Y",
                       "Redo in the active task. Ctrl+Shift+Z also works.",
                       !edit_locked && documents.history(editor.task.id(), true),
                       [&] { documents.undo(editor.task.id(), true); });
            for (auto entry : forge::ui::palette_entries(selected, scene_tools.snap_step,
                                                         camera.target, blockout.at_view_target)) {
                if (entry.operation == "history.undo" || entry.operation == "history.redo")
                    continue;
                const bool inspect =
                    entry.operation == "diagnostics" || entry.operation == "schema";
                std::string shortcut = entry.operation == "entity.duplicate" ? "Ctrl+D"
                                       : entry.operation == "entity.delete"  ? "Delete"
                                                                             : "";
                add_action(entry.label, entry.label, shortcut, entry.help,
                           entry.available &&
                               (inspect || (!edit_locked &&
                                            (scene_task || entry.operation == "entity.create"))),
                           [&, entry] {
                               if (entry.operation == "diagnostics")
                                   commands.diagnostics_open = true;
                               else if (entry.operation == "schema")
                                   commands.schema_open = true;
                               else {
                                   if (entry.operation == "entity.create") {
                                       editor.task.owner = forge::ui::DocumentTask::Scene;
                                       workspace.scene = workspace.inspector = true;
                                       ImGui::SetWindowFocus("###Scene");
                                   }
                                   auto arguments = entry.arguments;
                                   if (arguments.contains("entity"))
                                       arguments["entity"] = selected;
                                   editor.selection.select_entity(
                                       forge::authoring_command(scene, entry.operation, arguments)
                                           .at("selected")
                                           .get<std::string>());
                               }
                           });
            }
            add_action("rename", "Rename entity", "F2",
                       "Focus the selected entity name in Inspector.",
                       !selected.empty() && !edit_locked && scene_task, [&] {
                           editor.rename_entity = true;
                           workspace.inspector = true;
                       });
            add_action("add_component", "Add Component", "",
                       "Search registered components for the selected entity.",
                       !selected.empty() && !edit_locked && scene_task, [&] {
                           editor.add_component = true;
                           workspace.inspector = true;
                       });
            add_action("play", "Play", "",
                       "Start the isolated runtime and show Game. Authoring remains in Scene.",
                       !play.active() && !native->busy() && !files.busy() && !modal.active() &&
                           !blockout.active() && !scene_tools.move.active(),
                       [&] {
                           const bool sdk = files.document.settings().requires_native_sdk();
                           auto executable = std::filesystem::path(runtime_path);
                           if (sdk) {
                               const auto root = exact_sdk_root[0]
                                                     ? std::filesystem::u8path(exact_sdk_root)
                                                     : std::filesystem::path(base) / "NativeSdk";
                               executable = root / "bin/forge_runtime.exe";
                               if (!std::filesystem::is_regular_file(executable))
                                   throw std::runtime_error(
                                       "Select a matching Native SDK installation in Gameplay "
                                       "Code. Its bin/forge_runtime.exe is missing.");
                           }
                           play.configure(files.document.settings().simulation_hz(),
                                          files.document.settings().input(),
                                          files.document.settings().physics().gravity,
                                          files.document.project(), sdk);
                           play.start(forge::path_utf8(executable), scene.snapshot(),
                                      sdk ? std::string{} : native->artifact());
                           workspace.game = true;
                           focus_game = true;
                       });
            add_action("pause", play.active() && play.paused() ? "Resume" : "Pause", "F6",
                       "Pause or resume the runtime clock.", play.control_ready(), [&] {
                           if (play.paused())
                               play.resume();
                           else
                               play.pause();
                       });
            add_action("step", "Step", "F7", "Run exactly one fixed tick and remain paused.",
                       play.control_ready() && play.paused(), [&] { play.step(); });
            add_action("stop", "Stop", "",
                       "Stop the isolated runtime. The authored scene is preserved.", play.active(),
                       [&] {
                           game_input.release(play);
                           play.stop();
                       });
            add_action("recover", "Recover runtime", "",
                       "Reconstruct the last compatible completed checkpoint.",
                       play.can_recover() && !native->busy() && !files.busy(), [&] {
                           play.recover();
                           workspace.game = true;
                           focus_game = true;
                       });
            for (auto [id, label, key] : {std::tuple{"tool.select", "Select", "Q"},
                                          {"tool.move", "Move", "W"},
                                          {"tool.rotate", "Rotate", "R"},
                                          {"tool.scale", "Scale", "S"}}) {
                const std::string tool = id;
                add_action(tool, std::string("Transform / ") + label, key,
                           "Scene transform tool. Rotate/Scale: X/Y/Z constrain, Enter commits, "
                           "Escape cancels. No change until gesture is committed.",
                           !edit_locked &&
                               (tool == "tool.select" || tool == "tool.move" || !selected.empty()),
                           [&, tool] {
                               workspace.scene = true;
                               editor.task.owner = forge::ui::DocumentTask::Scene;
                               ImGui::SetWindowFocus("###Scene");
                               if (tool == "tool.rotate" || tool == "tool.scale")
                                   modal.request(tool == "tool.scale");
                               else {
                                   scene_tools.move_tool = tool == "tool.move";
                                   perform(save_preferences);
                               }
                           });
            }
            auto create_menu = [&] {
                if (forge::ui::creation_menu(actions, blockout.at_view_target))
                    perform(save_preferences);
            };
            auto preferences_menu = [&] {
                if (ImGui::BeginMenu("Preferences")) {
                    if (ImGui::Checkbox("Tooltips", &forge::ui::tooltips))
                        perform(save_preferences);
                    forge::ui::help("Persistent contextual help for editor controls.");
                    ImGui::Text("UI scale %.0f%%", forge::ui::interface_scale * 100);
                    if (ImGui::MenuItem("Reset UI scale", "Ctrl+0")) {
                        forge::ui::style(1);
                        perform(save_preferences);
                    }
                    forge::ui::help("Ctrl+Minus/Plus zooms the interface; Ctrl+0 resets it.");
                    ImGui::EndMenu();
                }
            };
            commands.actions = &actions;
#ifdef FORGE_UI_FIXTURE
            if (!fixture.workflow && (SDL_GetTicks() - fixture.started > 240000 ||
                                      SDL_GetTicks() - fixture.stage_started > 45000)) {
                forge::Json stalled{
                    {"stage", fixture.stage},
                    {"frames", fixture.frames},
                    {"ui_frame", ImGui::GetFrameCount()},
                    {"mesh_picker_requested", forge::fixture_open_mesh_picker},
                    {"model_open", model_imports.is_open()},
                    {"model_import_pending", model_imports.pending()},
                    {"model_import_error", model_imports.diagnostic()},
                    {"model_preview", model_viewer ? model_viewer->loading_state() : "absent"},
                    {"status", message}};
                if (auto* w = ImGui::FindWindowByName("###Model import"))
                    stalled["model_window"] = {{"active", bool(w->Active)},
                                               {"hidden", bool(w->Hidden)},
                                               {"tab_visible", bool(w->DockTabIsVisible)}};
                forge::atomic_write(fixture.output / "stalled-state.json", stalled.dump(2));
                fixture.capture(device, context, swap->GetCurrentBackBufferRTV(), false);
                throw std::runtime_error(
                    "Editor fixture timed out at stage " + std::to_string(fixture.stage) +
                    " after " + std::to_string(fixture.frames) + " frames; model ready=" +
                    std::to_string(model_viewer && model_viewer->ready()) + "; material pending=" +
                    std::to_string(material_preview && material_preview->pending()) +
                    "; member ready=" + std::to_string(asset_view_document.ready()) +
                    "; model state=" + (model_viewer ? model_viewer->loading_state() : "absent") +
                    "; import=" + model_imports.diagnostic());
            }
            if (!fixture.workflow && !fixture.prepared) {
                bool ready = true;
                switch (fixture.stage) {
                case 0:
                    break;
                case 1:
                    editor.add_component = true;
                    break;
                case 2:
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    ImGui::SetWindowFocus("Content");
                    break;
                case 3:
                    try {
                        forge::authoring_command(scene, "property.set",
                                                 {{"entity", selected},
                                                  {"component", "forge.ui_document"},
                                                  {"field", "layer"},
                                                  {"value", -1}});
                    } catch (const std::exception& e) {
                        editor.problems.report({"fixture-invalid-layer",
                                                "Error",
                                                e.what(),
                                                selected,
                                                {},
                                                "UI Document / Layer",
                                                scene.asset_id()});
                    }
                    ImGui::SetWindowFocus("Problems###Problems");
                    break;
                case 4:
                    prefab_editor.edit_source(files.document, fixture.prefab);
                    prefab_editor.rename_member("Draft name");
                    break;
                case 5:
                    prefab_editor.resolve_close(forge::ui::DraftResolution::Discard, scene,
                                                files.document);
                    project_settings.open();
                    break;
                case 6:
                    project_settings.request_close();
                    actions.invoke("play");
                    break;
                case 7:
                    if (play.control_ready()) {
                        if (!play.paused())
                            play.pause();
                        game_input.capture(play);
                    } else
                        ready = false;
                    break;
                case 8:
                    editor.selection.select_entity(
                        scene.document().at("entities")[0].at("id").get<std::string>());
                    game_input.release(play);
                    play.stop();
                    SDL_SetWindowSize(window.get(), 960, 640);
                    ImGui::SetWindowFocus("###Scene");
                    break;
                case 9:
                    forge::ui::style(2);
                    break;
                case 10:
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    workspace.reset = true;
                    break;
                case 11:
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 2560, 1080);
                    break;
                case 12:
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    scene.reset({{"version", 1}, {"entities", forge::Json::array()}});
                    editor.selection.clear();
                    editor.problems.clear();
                    workspace.reset = true;
                    break;
                case 13:
                    actions.invoke("Create / Sphere");
                    ImGui::SetWindowFocus("###Scene");
                    break;
                case 14:
                    fixture.scene_create = true;
                    break;
                case 15:
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    fixture.hierarchy_create = true;
                    break;
                case 16:
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    commands.open_palette();
                    break;
                case 17:
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1.25f);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    workspace.reset = true;
                    break;
                case 18:
                    forge::ui::style(1.5f);
                    workspace.reset = true;
                    break;
                case 19:
                    forge::ui::style(1);
                    ecs_workspace.request_open();
                    break;
                case 20:
                    if (auto* w = ImGui::FindWindowByName("ECS World inspection"))
                        if (auto* bar = GImGui->TabBars.GetByKey(w->GetID("ecs-tabs")))
                            if (auto* tab =
                                    ImGui::TabBarFindTabByID(bar, ImHashStr("Metrics", 0, bar->ID)))
                                ImGui::TabBarQueueFocus(bar, tab);
                    break;
                case 21: {
                    ecs_workspace.request_close();
                    forge::atomic_write(files.document.project() / "Assets/Example.flecs",
                                        "using flecs.meta\nstruct Position { x { member: {f32} } y "
                                        "{ member: {f32} } }\nExample { Position: {10, 20} }\n");
                    const auto script = forge::register_flecs_script(files.document.project(),
                                                                     "Assets/Example.flecs");
                    script_editor.open(files.document, script);
                    break;
                }
                case 22:
                    forge::ui::style(2);
                    break;
                case 23:
                    script_editor.request_close();
                    SDL_SetWindowSize(window.get(), 960, 640);
                    workspace.reset = true;
                    ImGui::SetWindowFocus("###Scene");
                    break;
                case 24:
                    workspace.bottom_folded = false;
                    break;
                case 25:
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    workspace.reset = true;
                    break;
                case 26: {
                    std::string image(18, '\0');
                    image[2] = 2;
                    image[12] = 2;
                    image[14] = 2;
                    image[16] = 24;
                    image[17] = 32;
                    for (unsigned char value : std::array<unsigned char, 12>{
                             0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255})
                        image.push_back(char(value));
                    forge::atomic_write(files.document.project() / "Assets/texture.tga", image);
                    texture_imports.open(files.document, "Assets/texture.tga");
                    texture_imports.request_save();
                    break;
                }
                case 27:
                    forge::ui::style(2);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    break;
                case 28: {
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    auto source = forge::MaterialDocument::create(files.document.writer_guard(),
                                                                  "Assets/Preview.material.json");
                    source->edit(source->revision(), "Preview factors", [](auto& j) {
                        j["overrides"]["parameters"]["baseColorFactor"] = {
                            {"type", unsigned(forge::MaterialParameterType::LinearColor4)},
                            {"value", {.04, .3, .7, 1}}};
                        j["overrides"]["parameters"]["metallicFactor"] = {{"type", 0},
                                                                          {"value", {0}}};
                        j["overrides"]["parameters"]["roughnessFactor"] = {{"type", 0},
                                                                           {"value", {.25}}};
                    });
                    source->save();
                    material_editor.open(files.document, source->locator());
                    // The preview can render an unpublished draft. Content tiles
                    // require the cooked revision produced by the real Save route.
                    material_editor.request_save();
                    break;
                }
                case 29:
                    forge::ui::style(2);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    break;
                case 30: {
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    // Two asymmetric placements, including a reflection. This is
                    // deterministic source data, not a pre-rendered preview.
                    std::string vertices;
                    for (float value : std::array<float, 9>{-1, -1, 0, 1, -1, 0, 0, 1, 0}) {
                        const auto bits = std::bit_cast<std::uint32_t>(value);
                        for (unsigned byte = 0; byte < 4; ++byte)
                            vertices.push_back(char((bits >> (byte * 8)) & 255));
                    }
                    const auto source = forge::Json::parse(R"({
                    "asset":{"version":"2.0"},
                    "extensionsUsed":["KHR_materials_unlit"],
                    "buffers":[{"uri":"preview.bin","byteLength":36}],
                    "bufferViews":[{"buffer":0,"byteLength":36}],
                    "accessors":[{"bufferView":0,"componentType":5126,"count":3,
                        "type":"VEC3","min":[-1,-1,0],"max":[1,1,0]}],
                    "materials":[{"name":"Orange","doubleSided":true,
                        "extensions":{"KHR_materials_unlit":{}},
                        "pbrMetallicRoughness":{"baseColorFactor":[0.8,0.2,0.04,1]}}],
                    "meshes":[{"name":"Triangle","primitives":[{
                        "attributes":{"POSITION":0},"material":0}]}],
                    "nodes":[{"mesh":0,"translation":[-2,0,0]},
                        {"mesh":0,"translation":[2,0,0],"scale":[-1,1,1]}],
                    "scenes":[{"nodes":[0,1]}],"scene":0})");
                    forge::atomic_write(files.document.project() / "Assets/preview.bin", vertices);
                    forge::atomic_write(files.document.project() / "Assets/preview.gltf",
                                        source.dump());
                    model_imports.open(files.document, "Assets/preview.gltf");
                    model_imports.request_save();
                    break;
                }
                case 31:
                case 33:
                case 35:
                    forge::ui::style(2);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    break;
                case 32:
                case 34: {
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    const std::string type = fixture.stage == 32 ? "mesh" : "material";
                    const auto& records = mesh_resources->catalog()->records();
                    const auto found =
                        std::find_if(records.begin(), records.end(), [&](const auto& entry) {
                            return entry.second.type == type && entry.second.subasset &&
                                   entry.second.source.generic_string() == "Assets/preview.gltf";
                        });
                    if (found == records.end())
                        throw std::runtime_error("Preview fixture generated member is missing");
                    asset_view_document.open(files.document.project(), found->second);
                    break;
                }
                case 36: {
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    content.load_settings(
                        {{"grid", true}, {"folder_tree", false}, {"tile_size", 120}});
                    workspace.content = true;
                    workspace.bottom_folded = false;
                    if (auto* w = ImGui::FindWindowByName("Content"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("Content", {20, 60});
                    ImGui::SetWindowSize("Content", {1360, 760});
                    ImGui::SetWindowFocus("Content");
                    break;
                }
                case 37:
                    forge::ui::style(2);
                    SDL_SetWindowSize(window.get(), 960, 640);
                    ImGui::SetWindowPos("Content", {15, 45});
                    ImGui::SetWindowSize("Content", {930, 580});
                    break;
                case 38: {
                    if (texture_imports.dirty() || material_editor.dirty()) {
                        if (texture_imports.dirty() && !texture_imports.pending())
                            texture_imports.request_save();
                        if (material_editor.dirty() && !material_editor.pending())
                            material_editor.request_save();
                        ready = false;
                        break;
                    }
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    auto reviewed = forge::empty_scene();
                    const auto id = reviewed.at("asset_id").get<forge::AssetId>();
                    forge::atomic_write(files.document.project() / "Assets/File-review.scene.json",
                                        reviewed.dump(2));
                    if (!content_files.begin({id, "scene", "Assets/File-review.scene.json", 3, {}},
                                             forge::AssetFileAction::Delete))
                        throw std::runtime_error("Content fixture could not open its file review");
                    content_files.prepare_review();
                    break;
                }
                case 39:
                    forge::ui::style(2);
                    SDL_SetWindowSize(window.get(), 960, 640);
                    ImGui::SetWindowPos("Asset source files", {15, 15});
                    ImGui::SetWindowSize("Asset source files", {930, 610});
                    break;
                case 40:
                    forge::ui::style(1.5f);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    ImGui::SetWindowPos("Asset source files", {300, 90});
                    ImGui::SetWindowSize("Asset source files", {1050, 855});
                    break;
                case 41:
                    // The reviewed delete was never committed. Release the test's
                    // draft before preparing independent document captures.
                    content_files = forge::ContentFiles{};
                    content_imports.suspend(false);
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    if (auto* w = ImGui::FindWindowByName("Content"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    workspace.content = false;
                    workspace.reset = true;
                    ImGui::SetWindowFocus("###Texture import");
                    break;
                case 42:
                    ImGui::SetWindowFocus("###Material");
                    break;
                case 43:
                    ImGui::SetWindowFocus("###Model import");
                    break;
                case 44:
                case 45: {
                    const std::string type = fixture.stage == 44 ? "mesh" : "material";
                    const auto& records = mesh_resources->catalog()->records();
                    const auto found =
                        std::find_if(records.begin(), records.end(), [&](const auto& entry) {
                            return entry.second.type == type && entry.second.subasset &&
                                   entry.second.source.generic_string() == "Assets/preview.gltf";
                        });
                    if (found == records.end())
                        throw std::runtime_error("150% preview fixture member is missing");
                    asset_view_document.open(files.document.project(), found->second);
                    break;
                }
                case 46:
                    workspace.content = true;
                    workspace.bottom_folded = false;
                    if (auto* w = ImGui::FindWindowByName("Content"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("Content", {30, 70});
                    ImGui::SetWindowSize("Content", {1860, 960});
                    ImGui::SetWindowFocus("Content");
                    break;
                case 47: {
                    workspace.content = false;
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    workspace.reset = true;
                    std::string wav;
                    auto put = [&](unsigned value, unsigned count) {
                        for (unsigned i = 0; i < count; ++i)
                            wav.push_back(char((value >> (i * 8)) & 255));
                    };
                    wav += "RIFF";
                    put(36 + 480 * 2, 4);
                    wav += "WAVEfmt ";
                    put(16, 4);
                    put(1, 2);
                    put(1, 2);
                    put(48000, 4);
                    put(96000, 4);
                    put(2, 2);
                    put(16, 2);
                    wav += "data";
                    put(480 * 2, 4);
                    for (unsigned i = 0; i < 480; ++i)
                        put(i % 2 ? 4096 : 2048, 2);
                    forge::atomic_write(files.document.project() / "Assets/sound.wav", wav);
                    audio_imports.open(files.document, "Assets/sound.wav");
                    audio_imports.request_save();
                    break;
                }
                case 48:
                case 51:
                case 54:
                    forge::ui::style(1.5f);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    workspace.reset = true;
                    break;
                case 49:
                case 52:
                case 55:
                    forge::ui::style(2);
                    workspace.reset = true;
                    break;
                case 50: {
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    workspace.reset = true;
                    const forge::Json shader{
                        {"format", "forge.shader"},
                        {"version", 1},
                        {"asset_id", forge::AssetId::generate()},
                        {"source_root", "Shaders"},
                        {"permutations", {{"QUALITY", {"LOW", "HIGH"}}}},
                        {"stages",
                         forge::Json::array({{{"stage", "vertex"}, {"source", "preview.hlsl"}}})}};
                    forge::atomic_write(files.document.project() / "Assets/Preview.shader.json",
                                        shader.dump(2));
                    forge::atomic_write(files.document.project() / "Shaders/preview.hlsl",
                                        "float4 main(float3 p:ATTRIB0):SV_POSITION { return "
                                        "float4(p,1); }\n");
                    shader_imports.open(files.document, "Assets/Preview.shader.json");
                    shader_imports.edit_setting("permutation", forge::Json{{"QUALITY", "HIGH"}});
                    shader_imports.request_save();
                    break;
                }
                case 53:
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1440, 900);
                    resource_inspector.visible = true;
                    break;
                case 56:
                case 57:
                case 58: {
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    resource_inspector.visible = false;
                    const float scale = fixture.stage == 56   ? 1.f
                                        : fixture.stage == 57 ? 1.5f
                                                              : 2.f;
                    forge::ui::style(scale);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    if (fixture.stage == 56)
                        editor.selection.select_entity(
                            forge::authoring_command(
                                scene, "entity.create",
                                {{"name", "Picker cube"}, {"recipe", "primitive.0"}})
                                .at("selected"));
                    editor.task.owner = forge::ui::DocumentTask::Scene;
                    workspace.inspector = true;
                    if (auto* w = ImGui::FindWindowByName("Inspector"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("Inspector", {900, 55});
                    ImGui::SetWindowSize("Inspector", {990, 1000});
                    component_inspector.fixture_focus_component = "forge.mesh_renderer";
                    forge::fixture_open_mesh_picker = true;
                    break;
                }
                case 59: {
                    forge::fixture_open_mesh_picker = false;
                    component_inspector.fixture_focus_component.clear();
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    auto source = gltf_variant_fixture();
                    source.document["extensionsUsed"].push_back("KHR_materials_unlit");
                    source.document["extensionsUsed"].push_back("VENDOR_optional_fixture");
                    for (auto& material : source.document["materials"]) {
                        material["doubleSided"] = true;
                        material["extensions"]["KHR_materials_unlit"] = forge::Json::object();
                    }
                    source.document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {
                        .1, .6, .8, 1};
                    source.document["materials"][1]["pbrMetallicRoughness"]["baseColorFactor"] = {
                        .8, .3, .1, 1};
                    forge::atomic_write(files.document.project() / "Assets/lods.gltf",
                                        source.document.dump());
                    const auto bytes = source.buffers[0].bytes();
                    forge::atomic_write(
                        files.document.project() / "Assets/instances.bin",
                        std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                    model_imports.open(files.document, "Assets/lods.gltf");
                    model_imports.request_save();
                    if (auto* w = ImGui::FindWindowByName("###Model import"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("###Model import", {30, 55});
                    ImGui::SetWindowSize("###Model import", {1800, 970});
                    break;
                }
                case 60: {
                    const auto& records = mesh_resources->catalog()->records();
                    const auto& owner = records.at(model_imports.selected_asset());
                    const auto edge = std::find_if(
                        owner.dependency_edges.begin(), owner.dependency_edges.end(),
                        [](const auto& e) { return e.role == "model.member:/lods/0"; });
                    if (edge == owner.dependency_edges.end())
                        throw std::runtime_error("LOD fixture combined member is missing");
                    asset_view_document.open(files.document.project(), records.at(edge->target));
                    if (auto* w = ImGui::FindWindowByName("###Asset viewer"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("###Asset viewer", {30, 55});
                    ImGui::SetWindowSize("###Asset viewer", {1800, 970});
                    break;
                }
                case 61:
                    forge::ui::style(1.5f);
                    break;
                case 62:
                    forge::ui::style(2);
                    break;
                case 63: {
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    forge::SurfaceShaderDefinition definition;
                    definition.parameters["tint"] = {forge::MaterialParameterType::LinearColor4,
                                                     {.12f, .65f, .9f, 1}};
                    definition.parameters["intensity"] = {forge::MaterialParameterType::Scalar,
                                                          {1}};
                    const forge::Json shader{
                        {"format", "forge.shader"},
                        {"version", 2},
                        {"asset_id", forge::AssetId::generate()},
                        {"source_root", "Shaders"},
                        {"stages", forge::Json::array({{{"stage", "pixel"},
                                                        {"source", "custom-surface.hlsl"},
                                                        {"entry", "Shade"}}})},
                        {"surface", forge::surface_definition_document(definition)}};
                    forge::atomic_write(files.document.project() /
                                            "Assets/CustomSurface.shader.json",
                                        shader.dump(2));
                    forge::atomic_write(
                        files.document.project() / "Shaders/custom-surface.hlsl",
                        "float4 Shade(ForgeSurfaceInput input){float4 c=ForgeParameter_tint();"
                        "c.rgb*=ForgeParameter_intensity()*(.3+.7*abs(input.Normal.y));return "
                        "c;}\n");
                    shader_imports.open(files.document, "Assets/CustomSurface.shader.json");
                    if (auto* w = ImGui::FindWindowByName("###Shader import"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("###Shader import", {30, 55});
                    ImGui::SetWindowSize("###Shader import", {1800, 970});
                    shader_imports.request_save();
                    break;
                }
                case 64: {
                    auto source = forge::MaterialDocument::create(
                        files.document.writer_guard(), "Assets/CustomSurface.material.json");
                    source->edit(source->revision(), "Select surface Shader", [&](auto& j) {
                        j["version"] = 2;
                        j["overrides"]["shader"] =
                            forge::AssetRef<forge::ShaderAsset>{shader_imports.selected_asset()};
                    });
                    source->save();
                    material_editor.open(files.document, source->locator());
                    material_editor.request_save();
                    if (auto* w = ImGui::FindWindowByName("###Material"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("###Material", {30, 55});
                    ImGui::SetWindowSize("###Material", {1800, 970});
                    break;
                }
                case 65:
                    forge::ui::style(1.5f);
                    break;
                case 66:
                    forge::ui::style(2);
                    break;
                case 67:
                case 68:
                case 69: {
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1.f + .5f * float(fixture.stage - 67));
                    if (auto* w = ImGui::FindWindowByName("###Model import"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("###Model import", {30, 55});
                    ImGui::SetWindowSize("###Model import", {1800, 970});
                    ImGui::SetWindowFocus("###Model import");
                    model_imports.fixture_open_variant = true;
                    break;
                }
                case 70:
                case 71:
                case 72: {
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1.f + .5f * float(fixture.stage - 70));
                    if (fixture.stage == 70) {
                        if (!model_imports.placement_ready()) {
                            ready = false;
                            break;
                        }
                        (void)model_imports.place(files.document);
                    }
                    workspace.inspector = true;
                    if (auto* w = ImGui::FindWindowByName("Inspector"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("Inspector", {30, 55});
                    ImGui::SetWindowSize("Inspector", {1200, 970});
                    ImGui::SetWindowFocus("Inspector");
                    forge::ui::fixture_open_model_variant = true;
                    break;
                }
                case 73:
                case 74:
                case 75: {
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1.f + .5f * float(fixture.stage - 73));
                    ImGui::SetWindowFocus("###Model import");
                    model_imports.fixture_open_notes = true;
                    break;
                }
                case 76:
                case 79: {
                    const unsigned version = fixture.stage == 76 ? 1u : 2u;
                    if (!fixture.component_inspection_requested) {
                        if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                            ImGui::ClosePopupToLevel(0, true);
                        auto settings = files.document.settings().document();
                        settings["modules"] =
                            forge::Json::array({{{"id", "project.capture"},
                                                 {"implementation", "1"},
                                                 {"sdk", "experimental-1"},
                                                 {"fingerprint", std::string(64, 'a')},
                                                 {"library", "capture.dll"},
                                                 {"dependencies", {"forge.transforms"}}}});
                        files.document.settings().save(settings);
                        forge::atomic_write(files.document.project() / "fixture-manifest.json",
                                            forge::test::authored_capture_manifest(version).dump());
                        forge::atomic_write(files.document.project() / "fixture.mode", "manifest");
                        authored_components.inspect(files.document,
                                                    std::filesystem::path(base) /
                                                        "forge_schema_worker_fixture.exe");
                        fixture.component_inspection_requested = true;
                    }
                    if (authored_components.busy()) {
                        ready = false;
                        break;
                    }
                    const auto schema = scene.schema();
                    const auto& components = schema.at("components");
                    if (std::none_of(components.begin(), components.end(), [](const auto& item) {
                            return item.at("id") == "project.pilot";
                        }))
                        throw std::runtime_error("Custom component capture admission failed");
                    fixture.component_inspection_requested = false;
                    if (version == 1) {
                        component_inspector.fixture_focus_component = "project.pilot";
                        const std::string id =
                            forge::authoring_command(scene, "entity.create", {{"name", "Pilot"}})
                                .at("selected");
                        forge::authoring_command(scene, "component.add",
                                                 {{"entity", id}, {"component", "project.pilot"}});
                        editor.selection.select_entity(id);
                    }
                    forge::ui::style(1);
                    workspace.inspector = workspace.build = true;
                    // Earlier small-window captures intentionally folded the
                    // supporting workspace. Reveal it before capturing its tools.
                    if (version == 2)
                        workspace.bottom_folded = false;
                    const char* target = version == 1 ? "Inspector" : "###Native";
                    if (auto* w = ImGui::FindWindowByName(target))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos(target, {30, 55});
                    ImGui::SetWindowSize(target, {1400, 960});
                    break;
                }
                case 77:
                case 80:
                    forge::ui::style(1.5f);
                    break;
                case 78:
                case 81:
                    forge::ui::style(2);
                    break;
                case 82:
                case 83:
                case 84:
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1.f + .5f * float(fixture.stage - 82));
                    authored_components.fixture_open_migration = true;
                    break;
                case 85:
                case 88:
                case 91: {
                    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
                        ImGui::ClosePopupToLevel(0, true);
                    forge::ui::style(1);
                    const char* recipe = fixture.stage == 85   ? "render.camera"
                                         : fixture.stage == 88 ? "render.light"
                                                               : "primitive.0";
                    component_inspector.fixture_focus_component =
                        fixture.stage == 85   ? "forge.camera"
                        : fixture.stage == 88 ? "forge.light"
                                              : "forge.mesh_renderer";
                    editor.selection.select_entity(
                        forge::authoring_command(scene, "entity.create", {{"recipe", recipe}})
                            .at("selected"));
                    workspace.build = false;
                    workspace.inspector = true;
                    if (auto* w = ImGui::FindWindowByName("Inspector"))
                        ImGui::SetWindowDock(w, 0, ImGuiCond_Always);
                    ImGui::SetWindowPos("Inspector", {30, 55});
                    ImGui::SetWindowSize("Inspector", {1400, 960});
                    break;
                }
                case 86:
                case 89:
                case 92:
                case 95:
                case 98:
                case 101:
                    forge::ui::style(1.5f);
                    break;
                case 87:
                case 90:
                case 93:
                case 96:
                case 99:
                case 102:
                    forge::ui::style(2);
                    break;
                case 94:
                    forge::ui::style(1);
                    scene_lighting_open = true;
                    ImGui::SetWindowPos("Scene lighting", {30, 55});
                    ImGui::SetWindowSize("Scene lighting", {1300, 950});
                    break;
                case 97: {
                    scene_lighting_open = false;
                    forge::ui::style(1);
                    const auto incoming = fixture.output / "source-input";
                    std::filesystem::create_directories(incoming);
                    for (const auto* file : {"preview.gltf", "preview.bin", "texture.tga"})
                        std::filesystem::copy_file(
                            files.document.project() / "Assets" / file, incoming / file,
                            std::filesystem::copy_options::overwrite_existing);
                    source_import.select(files.document,
                                         {incoming / "preview.gltf", incoming / "texture.tga"});
                    break;
                }
                case 100:
                    forge::ui::style(1);
                    source_import.prepare();
                    ready = source_import.review_ready();
                    break;
                }
                fixture.prepared = ready;
            }
#endif
            const auto panels_before = workspace.settings();
            if (forge::ui::begin_toolbar()) {
                const bool narrow_menu =
                    ImGui::GetMainViewport()->WorkSize.x < 900 * forge::ui::interface_scale;
                const bool menu_open = !narrow_menu || ImGui::BeginMenu("Menu");
                if (menu_open) {
                    files.menu([&] { actions.item("save"); }, scene_task && !edit_locked);
                    if (ImGui::BeginMenu("Edit")) {
                        actions.item("save");
                        actions.item("undo");
                        actions.item("redo");
                        ImGui::Separator();
                        actions.item("rename");
                        actions.item("add_component");
                        if (ImGui::MenuItem("Command palette", "Ctrl+Shift+P"))
                            commands.open_palette();
                        forge::ui::help("Search all currently registered editor actions.");
                        ImGui::Separator();
                        preferences_menu();
                        ImGui::EndMenu();
                    }
                    const bool entity_menu = ImGui::BeginMenu("Entity");
                    FORGE_UI_PROBE("menu:Entity");
                    if (entity_menu) {
                        const bool create_submenu = ImGui::BeginMenu("Create");
                        FORGE_UI_PROBE("menu:Create");
                        if (create_submenu) {
                            create_menu();
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        actions.item("Entity / Duplicate subtree", "Duplicate subtree");
                        actions.item("Entity / Delete subtree", "Delete subtree");
                        actions.item("Entity / Move to scene root", "Move to scene root");
                        if (ImGui::BeginMenu("Transform tools")) {
                            for (auto id :
                                 {"tool.select", "tool.move", "tool.rotate", "tool.scale"})
                                actions.item(id);
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Run")) {
                        FORGE_UI_PROBE("menu:Run");
                        actions.item("game.export");
                        ImGui::Separator();
                        for (auto id : {"play", "pause", "step", "stop", "recover"})
                            actions.item(id);
                        ImGui::EndMenu();
                    }
                    FORGE_UI_PROBE("menu:Run");
                    if (workspace.menu())
                        perform(save_preferences);
                    if (ImGui::BeginMenu("Assets")) {
                        FORGE_UI_PROBE("menu:Assets");
                        for (const auto& [id, label] :
                             {std::pair{"asset.import", "Import files..."},
                              {"asset.open", "Open selected"},
                              {"asset.place", "Place selected in Scene"},
                              {"asset.collision", "Create Collision from Mesh..."},
                              {"asset.reimport", "Reimport selected"},
                              {"asset.move", "Rename / Move source..."},
                              {"asset.duplicate", "Duplicate source..."},
                              {"asset.delete", "Delete source..."},
                              {"asset.cache", "Derived cache..."}})
                            actions.item(id, label);
                        ImGui::EndMenu();
                    }
                    FORGE_UI_PROBE("menu:Assets");
                    commands.menu([&] {
                        ecs_workspace.menu();
                        automation.menu();
                        performance.menu();
                        resource_inspector.menu();
                        project_settings.menu();
                        actions.item("scene.lighting", "Scene lighting...");
                    });
                    forge::ui::help_menu(std::filesystem::path(base), message);
                    if (narrow_menu)
                        ImGui::EndMenu();
                }
                forge::ui::end_toolbar();
            }
            const auto* main_viewport = ImGui::GetMainViewport();
            const float toolbar_height = ImGui::GetFrameHeight() + 8 * forge::ui::interface_scale;
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2{8 * forge::ui::interface_scale, 4 * forge::ui::interface_scale});
            if (ImGui::BeginViewportSideBar(
                    "##global-actions", ImGui::GetMainViewport(), ImGuiDir_Up, toolbar_height,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoFocusOnAppearing)) {
                using forge::ui::Icon;
                actions.icon("save", Icon::Save);
                ImGui::SameLine();
                actions.icon("undo", Icon::Undo);
                ImGui::SameLine();
                actions.icon("redo", Icon::Redo);
                forge::ui::tool_separator();
                actions.icon("play", Icon::Play, play.active() && !play.paused());
                ImGui::SameLine();
                actions.icon("pause", play.paused() ? Icon::Play : Icon::Pause,
                             play.active() && play.paused());
                ImGui::SameLine();
                actions.icon("step", Icon::Step);
                ImGui::SameLine();
                actions.icon("stop", Icon::Stop);
                forge::ui::tool_separator();
                if (forge::ui::icon_button("##global-more", Icon::More, "More global actions"))
                    ImGui::OpenPopup("Global actions");
                if (ImGui::BeginPopup("Global actions")) {
                    for (auto id :
                         {"save", "undo", "redo", "play", "pause", "step", "stop", "recover"})
                        actions.item(id);
                    ImGui::EndPopup();
                }
                if (main_viewport->WorkSize.x > 650 * forge::ui::interface_scale) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Save: %s", editor.task.name());
                    forge::ui::help("Ctrl+S saves this task. Undo/Redo belong to the same task; "
                                    "unavailable for prefab/settings drafts.");
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
#ifdef FORGE_UI_FIXTURE
            // Fixture windows must respect the scaled toolbar/status work area.
            // Fixed pixel positions from the 100% stage hide their title at 200%.
            if (const auto* target = fixture.focused_document())
                forge::test::fit_capture_window(target, forge::ui::interface_scale);
#endif
            if (!edit_locked)
                files.shortcuts(scene_task);
            if (!edit_locked &&
                !ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                    actions.invoke(ImGui::GetIO().KeyShift ? "redo" : "undo");
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                    actions.invoke("redo");
                if (ImGui::IsKeyPressed(ImGuiKey_D, false))
                    actions.invoke("Entity / Duplicate subtree");
            }
            if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false) &&
                    !game_input.captured() && !modal.active() && !scene_tools.move.active()) {
                    workspace.toggle_bottom();
                    perform(save_preferences);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F6, false))
                    actions.invoke("pause");
                if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
                    actions.invoke("step");
            }
            const auto lighting_catalog = mesh_resources ? mesh_resources->catalog() : nullptr;
            forge::ui::scene_lighting(scene_lighting_open, scene, lighting_catalog.get(),
                                      edit_locked);
            ecs_workspace.draw(scene, editor.problems);
            commands.draw(scene, selected, message, scene_tools.snap_step, camera.target,
                          blockout.at_view_target, edit_locked);
            files.draw_dialogs();
            animation_tools.poll(files.document, message);
            try {
                collision_editor.poll(files.document, message);
                if (auto catalog = collision_editor.take_catalog()) {
                    content_imports.catalog_changed(catalog);
                    play.model_assets_changed();
                    content.refresh(files);
                }
                material_editor.poll(files.document, message);
                if (auto catalog = material_editor.take_catalog()) {
                    content_imports.catalog_changed(catalog);
                    if (mesh_resources)
                        mesh_resources->catalog(catalog);
                    content.refresh(files);
                }
                audio_imports.poll(files.document, message);
                if (auto catalog = audio_imports.take_catalog()) {
                    content_imports.catalog_changed(catalog);
                    if (mesh_resources)
                        mesh_resources->catalog(catalog);
                    material_editor.asset_catalog_changed(catalog);
                    content.refresh(files);
                }
                texture_imports.poll(files.document, message);
                if (auto catalog = texture_imports.take_catalog()) {
                    content_imports.catalog_changed(catalog);
                    material_editor.asset_catalog_changed(catalog);
                    if (mesh_resources)
                        mesh_resources->catalog(std::move(catalog));
                    content.refresh(files);
                }
                shader_imports.poll(files.document, message);
                if (auto catalog = shader_imports.take_catalog()) {
                    content_imports.catalog_changed(catalog);
                    material_editor.asset_catalog_changed(catalog);
                    if (mesh_resources)
                        mesh_resources->catalog(std::move(catalog));
                    content.refresh(files);
                }
                model_imports.poll(files.document, message);
                if (auto catalog = model_imports.take_catalog()) {
                    content_imports.catalog_changed(catalog);
                    material_editor.asset_catalog_changed(catalog);
                    play.model_assets_changed();
                    if (mesh_resources)
                        mesh_resources->catalog(std::move(catalog));
                    content.refresh(files);
                }
            } catch (const std::exception& e) {
                message = e.what();
                forge::ui::report_error("asset_import", message);
            }
            content_files.poll(files.document, scene, content_imports, message);
            content_imports.suspend(game_export.busy() || runtime_dependencies.busy() ||
                                    content_files.busy() || source_import.busy() ||
                                    cache_tools.busy());
            game_export.draw(window.get(), files.document, scene,
                             asset_document_locked || content_files.busy() ||
                                 source_import.busy() || cache_tools.busy(),
                             files.document.dirty() || documents.source_drafts_dirty() ||
                                 runtime_dependencies.dirty(),
                             [&] { project_settings.open(); });
            cache_tools.poll(files.document, content_imports);
            source_import.poll(files.document, content_imports, message);
            if (!game_export.writing() && !runtime_dependencies.busy())
                content_imports.poll(files.document, message);
            // Documents and typed pickers also consume the catalog. Finishing
            // its async refresh must not depend on Content being visible.
            content.poll(files);
            content.source_snapshot(content_imports.sources());
            if (content.take_settings_changed())
                perform(save_preferences);
            navigation_tools.poll(scene, files.document, play.active(), message);
            automation.draw(scene, files.document, automation_busy);
            const auto title = std::string(files.document.dirty() ? "* " : "") +
                               files.document.name() + " / " +
                               (files.document.path().empty()
                                    ? "Untitled"
                                    : forge::path_text(files.document.path().filename())) +
                               " | F.O.R.G.E. | Build: " + forge::build_id;
            if (title != current_title) {
                SDL_SetWindowTitle(window.get(), title.c_str());
                current_title = title;
            }
            if (scene_tools.move.active() &&
                (!scene_tools.move.valid_for(scene) ||
                 !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) || play.active() ||
                 files.busy()))
                scene_tools.move.cancel();
            if (modal.active() && (!modal.gesture.valid(scene, selected) ||
                                   !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) ||
                                   play.active() || files.busy()))
                modal.cancel();
            blockout.check(scene);
            if (!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) || files.busy())
                blockout.cancel();
            const auto& doc = authoring_snapshot.document(scene);
            if (workspace.hierarchy) {
                if (ImGui::Begin("Hierarchy###World", &workspace.hierarchy)) {
                    if (forge::ui::icon_button(
                            "##hierarchy-add", forge::ui::Icon::Add,
                            "Add Entity\nCreate an entity in the Scene; one scene Undo step."))
                        ImGui::OpenPopup("Hierarchy create");
#ifdef FORGE_UI_FIXTURE
                    if (fixture.hierarchy_create) {
                        ImGui::OpenPopup("Hierarchy create");
                        fixture.hierarchy_create = false;
                    }
#endif
                    if (ImGui::BeginPopup("Hierarchy create")) {
                        create_menu();
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputTextWithHint("##entity-filter", "Search names or IDs...",
                                             hierarchy_filter, sizeof(hierarchy_filter));
                    FORGE_UI_PROBE("hierarchy:search");
                    forge::ui::help(
                        "Filter entity names and IDs; matching descendants retain their "
                        "ancestors. ASCII case-insensitive.");
                    int expand = 0;
                    if (forge::ui::button("Expand all", "Expand all hierarchy branches."))
                        expand = 1;
                    forge::ui::next_text_button("Collapse all");
                    if (forge::ui::button(
                            "Collapse all",
                            "Collapse all hierarchy branches. Search keeps matching paths open."))
                        expand = -1;
                    editor.task.focus(forge::ui::DocumentTask::Scene);
                    forge::ui::hierarchy(doc, selected, hierarchy_filter, expand, &scene,
                                         edit_locked);
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        ImGui::OpenPopup("Entity actions");
                    if (ImGui::BeginPopup("Entity actions")) {
                        if (ImGui::BeginMenu("Create")) {
                            create_menu();
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        actions.item("rename");
                        actions.item("Entity / Duplicate subtree", "Duplicate subtree");
                        actions.item("Entity / Delete subtree", "Delete subtree");
                        actions.item("add_component");
                        actions.item("Entity / Move to scene root", "Move to scene root");
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
                        actions.invoke("rename");
                    if (ImGui::IsWindowFocused() && !edit_locked && !ImGui::GetIO().WantTextInput &&
                        !ImGui::IsAnyItemActive() && !selected.empty() &&
                        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                        actions.invoke("Entity / Delete subtree");
                    }
                }
                ImGui::End();
            }
            if (workspace.inspector) {
                if (ImGui::Begin("Inspector", &workspace.inspector)) {
                    ImGui::BeginDisabled(play.active() || native->busy() || files.busy() ||
                                         scene_tools.move.active() || modal.active());
                    if (editor.selection.kind() != forge::ui::SelectionKind::DocumentItem)
                        editor.task.focus(editor.selection.kind() ==
                                                      forge::ui::SelectionKind::PrefabMember &&
                                                  prefab_editor.is_open()
                                              ? forge::ui::DocumentTask::Prefab
                                              : forge::ui::DocumentTask::Scene);
                    if (editor.selection.kind() == forge::ui::SelectionKind::DocumentItem) {
                        if (!documents.inspect(editor.selection.document(),
                                               editor.selection.member())) {
                            editor.selection.clear();
                            ImGui::TextUnformatted("No inspectable document target.");
                        }
                    } else if (editor.selection.kind() == forge::ui::SelectionKind::Asset ||
                               editor.selection.kind() == forge::ui::SelectionKind::PrefabMember) {
                        if (editor.selection.kind() == forge::ui::SelectionKind::PrefabMember) {
                            ImGui::TextWrapped("Prefab member selected. Edit its source in the "
                                               "Prefab source window.");
                            forge::ui::help("This selection belongs to the independent prefab "
                                            "draft, not an authored scene instance.");
                        }
                        content.inspect(files, editor.selection, [&](forge::AssetId asset) {
                            prefab_editor.edit_source(files.document, asset);
                        });
                    } else if (selected.empty()) {
                        ImGui::TextWrapped(
                            "Select an object in the Scene or Hierarchy to edit its properties.");
                        forge::ui::help("Click a scene object or hierarchy row.");
                    }
                    for (const auto& e : doc["entities"])
                        if (e.at("id") == selected) {
                            ImGui::TextWrapped("%s",
                                               e.at("name").get_ref<const std::string&>().c_str());
                            forge::ui::help("This Inspector edits the authored entity; runtime "
                                            "values are shown in Game presentation.");
                            ImGui::TextDisabled("%s", e.contains("prefab_instance") ||
                                                              e.contains("prefab_member")
                                                          ? "PREFAB INSTANCE"
                                                          : "ENTITY");
                            const auto name = e.at("name").get<std::string>();
                            if (name_entity != selected || authored_name != name) {
                                SDL_strlcpy(entity_name, name.c_str(), sizeof(entity_name));
                                name_entity = selected;
                                authored_name = name;
                            }
                            try {
                                forge::ui::property_label_row("Name");
                                if (editor.rename_entity) {
                                    ImGui::SetKeyboardFocusHere();
                                    editor.rename_entity = false;
                                }
                                if (ImGui::InputText("##Name", entity_name, sizeof(entity_name),
                                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                                         (e.contains("prefab_member")
                                                              ? ImGuiInputTextFlags_ReadOnly
                                                              : 0))) {
                                    forge::authoring_command(
                                        scene, "entity.rename",
                                        {{"entity", selected}, {"name", entity_name}});
                                }
                                FORGE_UI_PROBE("inspector:name");
                                forge::ui::help(e.contains("prefab_member")
                                                    ? "This name follows the prefab. Use Open "
                                                      "prefab source to rename this member."
                                                    : "Rename this entity. Press Enter to commit "
                                                      "one undoable edit.");
                                if (ImGui::TreeNode("Details")) {
                                    ImGui::Text(
                                        "ID: %.8s...%s", selected.c_str(),
                                        selected
                                            .substr(selected.size() > 4 ? selected.size() - 4 : 0)
                                            .c_str());
                                    forge::ui::help(selected.c_str());
                                    ImGui::SameLine();
                                    if (forge::ui::button("Copy ID",
                                                          "Copy full persistent EntityId."))
                                        ImGui::SetClipboardText(selected.c_str());
                                    forge::ui::help("Stable authored identity. Renaming does not "
                                                    "change this ID.");
                                    ImGui::TreePop();
                                }
                                forge::ui::help("Technical identity for this object.");
                                const auto parent = e.value("parent", std::string{});
                                std::string parent_name = "Scene root";
                                for (const auto& candidate : doc["entities"])
                                    if (candidate.at("id") == parent)
                                        parent_name = candidate.at("name").get<std::string>();
                                ImGui::BeginDisabled(e.contains("prefab_member"));
                                forge::ui::property_label_row("Parent");
                                const bool choose_parent =
                                    ImGui::BeginCombo("##Parent", parent_name.c_str());
                                ImGui::EndDisabled();
                                forge::ui::help(
                                    "Parent spatially with world placement preserved. The child "
                                    "then follows its parent. Unrepresentable local shear and "
                                    "cycles are rejected.");
                                std::string new_parent = parent;
                                if (choose_parent) {
                                    if (ImGui::Selectable("Scene root", parent.empty()))
                                        new_parent.clear();
                                    forge::ui::help("Place this entity at the scene root.");
                                    for (const auto& candidate : doc["entities"]) {
                                        const auto id = candidate.at("id").get<std::string>();
                                        if (id == selected)
                                            continue;
                                        ImGui::PushID(id.c_str());
                                        if (ImGui::Selectable(candidate.at("name")
                                                                  .get_ref<const std::string&>()
                                                                  .c_str(),
                                                              id == parent))
                                            new_parent = id;
                                        forge::ui::help(
                                            "Use this entity as the parent. A descendant "
                                            "cannot become its ancestor's parent.");
                                        ImGui::PopID();
                                    }
                                    ImGui::EndCombo();
                                }
                                if (new_parent != parent) {
                                    forge::authoring_command(
                                        scene, "entity.reparent",
                                        {{"entity", selected}, {"parent", new_parent}});
                                }
                            } catch (const std::exception& ex) {
                                message = ex.what();
                                editor.problems.report({"entity/" + selected,
                                                        "Error",
                                                        message,
                                                        selected,
                                                        {},
                                                        "Name / Parent",
                                                        scene.asset_id()});
                                forge::ui::field_error(message);
                            }
                            blockout.draw(scene, selected, message);
                            ImGui::BeginDisabled(blockout.active());
                            component_inspector.draw(scene, files.document, selected);
                            prefab_editor.inspector(scene, files.document, e);
                            ImGui::EndDisabled();
                        }
                    ImGui::EndDisabled();
                } else {
                    blockout.cancel();
                }
                ImGui::End();
            } else {
                blockout.cancel();
            }
            game_visible = false;
            for (const bool game_view : {false, true}) {
                auto& view_open = game_view ? workspace.game : workspace.scene;
                auto& view_camera = camera;
                auto& view_cache = game_view ? game_preview_snapshot : preview_snapshot;
                if (!view_open)
                    continue;
                if (game_view)
                    if (auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("###Scene")))
                        ImGui::SetNextWindowDockID(settings->DockId, ImGuiCond_FirstUseEver);
                if (game_view && focus_game) {
                    ImGui::SetNextWindowFocus();
                    focus_game = false;
                }

                const auto scene_title =
                    std::string("Scene — ") +
                    (files.document.path().empty()
                         ? "Untitled"
                         : forge::path_text(files.document.path().filename())) +
                    (files.document.dirty() ? " *" : "") + "###Scene";
                const bool view_visible =
                    ImGui::Begin(game_view ? "Game" : scene_title.c_str(), &view_open,
                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
                FORGE_UI_TAB_PROBE(game_view ? "tab:Game" : "tab:Scene");
                if (view_visible) {
                    if (game_view)
                        game_visible = true;
                    editor.task.focus(forge::ui::DocumentTask::Scene);
                    if (game_view) {
                        ImGui::TextUnformatted(play.can_recover() ? "CRASHED / Recovery available"
                                               : !play.active()   ? "STOPPED"
                                               : !play.ready()    ? "STARTING"
                                               : play.paused()    ? "PAUSED"
                                                                  : "PLAYING");
                        forge::ui::help("Runtime presentation from the isolated Play process.");
                    }
                    if (game_view && play.active()) {
                        game_input.controls(play);
                        runtime_ui.controls(play);
                    }
                    bool frame_selected = false, fit_scene = false;
                    if (!game_view) {
                        scene_asset_drop.controls();
                        if (forge::ui::icon_button(
                                "##scene-add", forge::ui::Icon::Add,
                                "Add Entity\nCreate at World Origin or the Scene view target."))
                            ImGui::OpenPopup("Scene create");
#ifdef FORGE_UI_FIXTURE
                        if (fixture.scene_create) {
                            ImGui::OpenPopup("Scene create");
                            fixture.scene_create = false;
                        }
#endif
                        if (ImGui::BeginPopup("Scene create")) {
                            create_menu();
                            ImGui::EndPopup();
                        }
                        forge::ui::tool_separator();
                        try {
                            using forge::ui::Icon;
                            const auto active_tool =
                                modal.active()
                                    ? (modal.gesture.mode() == forge::TransformGesture::Mode::Scale
                                           ? 3
                                           : 2)
                                    : (scene_tools.move_tool ? 1 : 0);
                            for (auto [id, icon, tool] :
                                 {std::tuple{"tool.select", Icon::Select, 0},
                                  {"tool.move", Icon::Move, 1},
                                  {"tool.rotate", Icon::Rotate, 2},
                                  {"tool.scale", Icon::Scale, 3}}) {
                                if (tool)
                                    forge::ui::toolbar_next();
                                actions.icon(id, icon, active_tool == tool);
                            }
                            forge::ui::tool_separator();
                            if (forge::ui::icon_button(
                                    "##snap", Icon::Snap,
                                    "Snap\nSnap moved positions to the grid. Ctrl temporarily "
                                    "snaps. Spacing is under View.",
                                    scene_tools.snap)) {
                                scene_tools.snap = !scene_tools.snap;
                                perform(save_preferences);
                            }
                            forge::ui::tool_separator();
                            if (forge::ui::icon_button(
                                    "##view", Icon::View,
                                    "View\nCamera, grid, snapping and overlays."))
                                ImGui::OpenPopup("##scene-view");
                            FORGE_UI_PROBE("scene:view-menu");
                            if (ImGui::BeginPopup("##scene-view")) {
                                ImGui::TextWrapped("Move / Rotate: World | Scale: Local");
                                forge::ui::help("Manipulator orientation, independent of the "
                                                "object's spatial parent binding.");
                                frame_selected = ImGui::MenuItem("Frame selected", "F");
                                FORGE_UI_PROBE("scene:frame");
                                forge::ui::help(
                                    "Center the camera on the selected visible object.");
                                fit_scene = ImGui::MenuItem("Fit scene");
                                FORGE_UI_PROBE("scene:fit");
                                forge::ui::help("Frame all visible objects.");
                                if (ImGui::MenuItem("Reset view"))
                                    view_camera = {};
                                forge::ui::help("Reset the editor camera without editing objects.");
                                if (ImGui::MenuItem("Save view"))
                                    perform([&] {
                                        forge::save_view(files.document, view_camera);
                                        message = "View bookmark saved";
                                    });
                                forge::ui::help("Remember one camera bookmark for this scene.");
                                if (ImGui::MenuItem("Restore view"))
                                    perform([&] {
                                        message = forge::restore_view(files.document, view_camera)
                                                      ? "View restored"
                                                      : "No saved view";
                                    });
                                forge::ui::help("Return to the saved camera bookmark.");
                                ImGui::Separator();
                                bool changed =
                                    ImGui::Checkbox("Orientation gizmo", &orientation.visible);
                                forge::ui::help("Show the clickable world-axis navigation widget.");
                                navigation_tools.overlay_control();
                                changed |=
                                    ImGui::Checkbox("Selected collision", &physics_overlay.visible);
                                FORGE_UI_PROBE("physics:overlay");
                                forge::ui::help(
                                    "Scene-only native collision wireframe. Select a body or "
                                    "character. Green: prepared geometry; amber: pending/stale; "
                                    "red: rejected; grey: disabled. This preview does not replace "
                                    "Play validation.");
                                changed |= ImGui::Checkbox("Camera / light helpers",
                                                           &spatial_helpers.visible);
                                forge::ui::help("Scene-only camera and light icons. Icons can be "
                                                "picked through geometry; move handles take "
                                                "priority. Hidden hierarchy branches omit helpers; "
                                                "locked icons cannot be picked.");
                                changed |= ImGui::SliderFloat("Helper size", &spatial_helpers.size,
                                                              16, 40, "%.0f px");
                                forge::ui::help(
                                    "Constant logical icon size, scaled with the interface.");
                                changed |= ImGui::DragFloat("Guide distance",
                                                            &spatial_helpers.extent, .5f, 1, 1000,
                                                            "%.1f m", ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help(
                                    "Maximum Scene guide length. Open camera rays do not invent a "
                                    "far plane; labels identify capped or unlimited light ranges.");
                                changed |= ImGui::Checkbox("Grid", &scene_tools.grid);
                                forge::ui::help(
                                    "Show the world-anchored XZ grid at Y=0, fading toward "
                                    "the horizon.");
                                changed |= ImGui::DragFloat("Snap spacing", &scene_tools.snap_step,
                                                            .05f, .01f, 1000, "%.2f",
                                                            ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help("Move snap spacing in world units.");
                                changed |= ImGui::DragFloat("Grid spacing", &scene_tools.grid_step,
                                                            .1f, .1f, 1000, "%.1f",
                                                            ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help(
                                    "Base grid spacing in world units. Coarser divisions "
                                    "fade in with distance; snap spacing is independent.");
                                changed |=
                                    ImGui::DragFloat("Fly speed", &scene_tools.fly_speed, .2f, .1f,
                                                     1000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help("RMB flight speed in world units per second.");
                                changed |= ImGui::SliderFloat("Exposure", &scene_tools.exposure,
                                                              -20, 20, "%.2f EV");
                                forge::ui::help(
                                    "Scene preview exposure in stops. +1 doubles light; -1 halves "
                                    "it. PBR Neutral tone mapping; editor overlays are unchanged. "
                                    "This preference does not edit game cameras.");
                                if (changed)
                                    perform(save_preferences);
                                ImGui::EndPopup();
                            }
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                    }
                    if (!game_view) {
                        forge::ui::next_text_button("Preview light");
                        if (ImGui::Checkbox("Preview light", &preview_lighting))
                            perform(save_preferences);
                        FORGE_UI_PROBE("preview-light");
                        forge::ui::help(
                            "Personal Scene preview lighting. Turn off to judge authored lights "
                            "and environment. Never changes the scene, Game view, Save, or Undo.");
                    }
                    viewport.preview_lighting(preview_lighting);
                    view_camera.fly_speed = scene_tools.fly_speed;
                    viewport.exposure(scene_tools.exposure);
                    if (game_view && !play.ready()) {
                        ImGui::TextWrapped("%s", play.status().c_str());
                        ImGui::TextWrapped("Use Play to start the isolated runtime. Scene remains "
                                           "your authored workspace.");
                        if (play.can_recover())
                            actions.button("recover");
                        ImGui::End();
                        continue;
                    }
                    auto read_preview = [&]() -> const forge::Json& {
                        return view_cache.get(
                            game_view ? play.snapshot_version() : scene.revision(), game_view,
                            blockout.active() || scene_tools.move.active() || modal.active(), [&] {
                                if (game_view)
                                    return play.ready() ? play.effective_snapshot()
                                                        : scene.effective_document();
                                auto source = authoring_snapshot.document(scene);
                                if (blockout.active())
                                    source = blockout.preview(source);
                                if (scene_tools.move.active())
                                    source = scene_tools.move.preview(source);
                                if (modal.active())
                                    source = modal.preview(source);
                                return (blockout.active() || scene_tools.move.active() ||
                                        modal.active())
                                           ? scene.preview_document(source)
                                           : authoring_snapshot.effective(scene);
                            });
                    };
                    const auto& preview = read_preview();
                    auto size = ImGui::GetContentRegionAvail();
                    if (size.x > 1 && size.y > 1) {
                        if (!game_view && frame_selected &&
                            (selected.empty() ||
                             !viewport.frame(preview, selected, view_camera, size.x / size.y)))
                            message = "Cannot frame selection: geometry is not ready or exceeds "
                                      "camera range.";
                        if (!game_view && fit_scene &&
                            !viewport.frame(preview, "", view_camera, size.x / size.y))
                            message = "Cannot fit scene: visible geometry is not ready or exceeds "
                                      "camera range.";
                        const auto image_origin = ImGui::GetCursorScreenPos();
#ifdef FORGE_UI_FIXTURE
                        if (!game_view)
                            fixture.scene_image_y = image_origin.y;
#endif
                        const bool focused =
                            (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
                        const bool popup = ImGui::IsPopupOpen(
                            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
                        if (game_view)
                            game_input.viewport(image_origin, size);
                        const bool gizmo =
                            !game_view &&
                            orientation.input(view_camera, image_origin, size,
                                              focused && !game_input.captured() &&
                                                  !modal.active() && !scene_tools.move.active() &&
                                                  !popup);
                        ImGui::SetCursorScreenPos(image_origin);
                        forge::ui::ViewportInput input;
                        const bool was_modal = modal.active();
                        if (forge::ui::camera_controls(view_camera, size, focused, &input,
                                                       game_view || gizmo || was_modal || popup ||
                                                           game_input.captured())) {
                            if (selected.empty() ||
                                !viewport.frame(preview, selected, view_camera, size.x / size.y))
                                message =
                                    "Cannot frame selection: geometry is not ready or exceeds "
                                    "camera range.";
                        }
                        const bool can_edit =
                            !game_view && focused && !play.active() && !native->busy() &&
                            !files.busy() && !blockout.active() &&
                            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                             ImGuiPopupFlags_AnyPopupLevel);
                        scene_asset_drop.target(
                            scene, files, content, view_camera, image_origin, size,
                            can_edit && !modal.active() && !scene_tools.move.active() && !gizmo);
                        const auto mouse = ImGui::GetIO().MousePos;
                        const bool over_image =
                            ImGui::IsWindowHovered() && mouse.x >= image_origin.x &&
                            mouse.y >= image_origin.y && mouse.x < image_origin.x + size.x &&
                            mouse.y < image_origin.y + size.y;
                        if (!game_view)
                            modal.input(scene, selected, view_camera, image_origin, size,
                                        over_image && !gizmo,
                                        can_edit && !scene_tools.move.active(), message);
                        unsigned guide_width = unsigned(std::max(1.f, size.x)),
                                 guide_height = unsigned(std::max(1.f, size.y));
                        if (auto* output = game_viewport.output()) {
                            const auto& target = output->GetTexture()->GetDesc();
                            guide_width = target.Width;
                            guide_height = target.Height;
                        }
                        if (!game_view) {
                            const auto& helper_snapshot = read_preview();
                            spatial_helpers.update(helper_snapshot, view_cache.generation(),
                                                   guide_width, guide_height);
                        }
                        const bool previous_move_tool = scene_tools.move_tool;
                        if (!game_view)
                            scene_tools.input(
                                scene, view_camera, selected, image_origin, size, input,
                                can_edit && !gizmo && !was_modal && !modal.active(), message,
                                [&](const forge::Json& snapshot, float x, float y) {
                                    if (auto hit = spatial_helpers.pick(view_camera, size, {x, y});
                                        !hit.empty())
                                        return hit;
                                    return viewport.pick(snapshot, view_camera,
                                                         unsigned(std::max(1.f, size.x)),
                                                         unsigned(std::max(1.f, size.y)), x, y,
                                                         5 * forge::ui::interface_scale);
                                });
                        if (!game_view && input.activated &&
                            ImGui::IsMouseDown(ImGuiMouseButton_Left) && can_edit && !gizmo &&
                            !was_modal && !modal.active())
                            editor.selection.select_entity(selected);
                        if (previous_move_tool != scene_tools.move_tool)
                            perform(save_preferences);
                        const auto& rendered = read_preview();
                        // SceneTools may have changed the preview during this frame.
                        if (!game_view)
                            spatial_helpers.update(rendered, view_cache.generation(), guide_width,
                                                   guide_height);
                        const float render_scale =
                            std::min(1.0f, 4096.0f / std::max(size.x, size.y));
                        const auto scene_submit = forge::ui::Performance::Clock::now();
                        const auto render_width = unsigned(std::max(1.0f, size.x * render_scale));
                        const auto render_height = unsigned(std::max(1.0f, size.y * render_scale));
                        auto* texture =
                            game_view
                                ? game_viewport.game(context, rendered, render_width, render_height)
                                : viewport.render(context, rendered, render_width, render_height,
                                                  view_camera, view_cache.generation(),
                                                  performance.continuous,
                                                  {scene_tools.grid, scene_tools.grid_step});
                        performance.scene_ms = forge::ui::Performance::milliseconds(
                            scene_submit, forge::ui::Performance::Clock::now());
                        if (game_view)
                            runtime_ui.draw(play, context, texture, image_origin, size,
                                            unsigned(std::max(1.0f, size.x * render_scale)),
                                            unsigned(std::max(1.0f, size.y * render_scale)));
                        ImGui::GetWindowDrawList()->AddImage(
                            ImTextureRef{reinterpret_cast<ImTextureID>(texture)}, image_origin,
                            {image_origin.x + size.x, image_origin.y + size.y});
                        if (!game_view)
                            spatial_helpers.draw(view_camera, selected, image_origin, size,
                                                 modal.active() ? ImGui::GetTextLineHeight() + 20
                                                                : 0);
                        if (!game_view && mesh_resources)
                            physics_overlay.draw(rendered, selected, view_camera, image_origin,
                                                 size, files.document.project(),
                                                 mesh_resources->catalog(), play.physics_status());
                        play.physics_debug_selection =
                            physics_overlay.visible && !selected.empty()
                                ? forge::Json{{"scene", scene.asset_id()}, {"entity", selected}}
                                : forge::Json();
                        if (!game_view)
                            scene_tools.draw(rendered, view_camera, selected, image_origin, size,
                                             can_edit && !modal.active());
                        const auto animation_debug = forge::prepare_animation_debug(
                            rendered,
                            game_view ? game_viewport.game_scene() : viewport.render_scene());
                        if (game_view) {
                            for (const auto& active : game_viewport.cameras()) {
                                const auto& area = active.view.viewport;
                                const ImVec2 low{image_origin.x + size.x * area.x / render_width,
                                                 image_origin.y + size.y * area.y / render_height};
                                const ImVec2 high{low.x + size.x * area.width / render_width,
                                                  low.y + size.y * area.height / render_height};
                                ImGui::GetWindowDrawList()->PushClipRect(low, high, true);
                                const forge::GameDebugView debug{active.view, render_width,
                                                                 render_height};
                                forge::draw_animation_debug(animation_debug, debug, image_origin,
                                                            size);
                                navigation_tools.draw(rendered, debug, image_origin, size);
                                ImGui::GetWindowDrawList()->PopClipRect();
                            }
                        } else {
                            forge::draw_animation_debug(animation_debug, view_camera, image_origin,
                                                        size);
                            navigation_tools.draw(rendered, view_camera, image_origin, size);
                        }
                        if (!game_view)
                            orientation.draw(view_camera, image_origin, size);
                        if (!game_view)
                            modal.draw(image_origin, size);
                        ImGui::GetWindowDrawList()->AddText(
                            {image_origin.x + 8,
                             image_origin.y + 8 +
                                 (modal.active() ? ImGui::GetTextLineHeight() + 20 : 0)},
                            IM_COL32(185, 200, 215, 255),
                            (game_view
                                 ? (game_viewport.cameras().empty()
                                        ? std::string("No active Camera. Use Entity > Create > "
                                                      "Rendering > Camera.")
                                        : "Game cameras: " +
                                              std::to_string(game_viewport.cameras().size()))
                                 : std::string(view_camera.view_name()) + " | Perspective")
                                .c_str());

                    } else if (!game_view) {
                        scene_tools.move.cancel();
                        modal.cancel();
                    }
                } else if (!game_view) {
                    scene_tools.move.cancel();
                    modal.cancel();
                }
                ImGui::End();
            }
            if (!workspace.scene) {
                scene_tools.move.cancel();
                modal.cancel();
            }
            if (!game_visible)
                game_input.release(play);
            if (content_thumbnails) {
                if (content_thumbnails->project() != files.document.project())
                    content_thumbnails.reset(); // Previous frame was already submitted.
                else
                    content_thumbnails->begin(mesh_resources ? mesh_resources->catalog() : nullptr);
            }
            if (workspace.content && !workspace.bottom_folded) {
                ImGui::BeginDisabled(modal.active() || scene_tools.move.active() ||
                                     blockout.active());
                content.draw(
                    files, &workspace.content,
                    [&] { prefab_editor.content(scene, files.document, selected, edit_locked); },
                    [&] {
                        material_editor.content(files.document, edit_locked);
                        collision_editor.content(files.document, edit_locked);
                        texture_imports.content(files.document, edit_locked);
                        audio_imports.content(files.document, edit_locked);
                        model_imports.content(files.document, edit_locked);
                        shader_imports.content(files.document, edit_locked);
                        script_editor.content(files.document, edit_locked, editor.selection,
                                              message);
                        animation_tools.content(files.document, edit_locked, message);
                        runtime_ui_tools.content(files.document, edit_locked, message,
                                                 runtime_ui.asset_snapshot());
                        navigation_tools.content(scene, files.document, edit_locked, message);
                    },
                    edit_locked);
                ImGui::EndDisabled();
            }
            script_editor.poll(files.document, editor.problems);
            documents.draw();
            cache_tools.draw(
                files.busy() || files.changed || scene_tools.move.active() || modal.active() ||
                blockout.active() || play.active() || native->busy() ||
                documents.source_drafts_dirty() || texture_imports.pending() ||
                audio_imports.pending() || model_imports.pending() || shader_imports.pending() ||
                material_editor.pending() || source_import.busy() || animation_tools.pending() ||
                navigation_tools.pending() || script_editor.pending() ||
                authored_components.busy() || content_files.busy());
            content_files.draw(content_imports);
            source_import.draw(play.active() || native->busy() || content_files.busy() ||
                               authored_components.busy() || scene_asset_drop.busy());
            script_editor.draw_source_viewer(files.document);
            diagnostic_source.draw(files.document.project());
            if (editor.reveal_content) {
                workspace.content = true;
                workspace.bottom_folded = false;
            }
            if (workspace.problems && !workspace.bottom_folded) {
                if (auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("Console")))
                    ImGui::SetNextWindowDockID(settings->DockId, ImGuiCond_FirstUseEver);
                if (editor.problems.draw(
                        editor.selection, &workspace.problems,
                        [&](const forge::ui::Problem& problem) {
                            try {
                                if (std::filesystem::u8path(problem.source).extension() == ".flecs")
                                    script_editor.navigate_source(files.document, problem.source,
                                                                  problem.line, problem.column);
                                else
                                    diagnostic_source.open(files.document.project(), problem);
                            } catch (const std::exception& e) {
                                message = e.what();
                            }
                        })) {
                    workspace.inspector = true;
                    editor.task.owner = forge::ui::DocumentTask::Scene;
                }
            }
            if (auto* console = ImGui::FindWindowSettingsByID(ImHashStr("Console")))
                ImGui::SetNextWindowDockID(console->DockId, ImGuiCond_FirstUseEver);
            if (workspace.build && !workspace.bottom_folded) {
                if (ImGui::Begin("Gameplay Code###Native", &workspace.build)) {
                    forge::ui::heading("Gameplay", "C++ gameplay compiles outside the editor. Only "
                                                   "isolated runtimes load gameplay DLLs.");
                    if (files.document.settings().requires_native_sdk()) {
                        ImGui::TextWrapped(
                            "Exact SDK project — registrations load when Play starts.");
                        forge::ui::help("The runtime validates every declared module and its exact "
                                        "SDK fingerprint before creating the play world.");
                        ImGui::BeginDisabled(play.active());
                        if (ImGui::InputText("Native SDK folder", exact_sdk_root,
                                             sizeof(exact_sdk_root)))
                            perform(save_preferences);
                        forge::ui::help("Machine-local SDK installation containing bin and sdk. "
                                        "Leave blank to use NativeSdk beside the editor. Must be "
                                        "built from the same FORGE source as this editor.");
                        ImGui::EndDisabled();
                        ImGui::TextWrapped(
                            "Stop Play, build your project modules with the matching "
                            "installed SDK, then press Play. Changes restart from "
                            "the authored scene; arbitrary C++ state is not restored.");
                        forge::ui::help(
                            "Use your project's CMake build in an external developer "
                            "terminal. Keep the last good module when compilation fails. "
                            "Rich SDK registration is restart-bound, not ABI1 hot reload.");
                        ImGui::TextWrapped("Runtime-only types remain in gameplay. Types that "
                                           "explicitly opt into authoring can be inspected below.");
                        forge::ui::help("Flecs reflection alone does not make arbitrary native "
                                        "C++ objects serializable or safely editable.");
                        const auto sdk_root = exact_sdk_root[0]
                                                  ? std::filesystem::u8path(exact_sdk_root)
                                                  : std::filesystem::path(base) / "NativeSdk";
                        authored_components.draw(scene, files.document, prefab_editor,
                                                 sdk_root / "bin/forge_runtime.exe",
                                                 play.active() || native->busy() ||
                                                     modal.active() || scene_tools.move.active() ||
                                                     blockout.active() || content_files.busy());
                    } else {
                        ImGui::BeginDisabled(native->busy() || modal.active() ||
                                             scene_tools.move.active() || blockout.active());
                        try {
                            if (forge::ui::button(
                                    "Create source",
                                    "Create Native/gameplay.cpp and CMakeLists.txt. Existing "
                                    "files are never overwritten."))
                                native->create_source();
                            if (forge::ui::button(
                                    "Build & Reload",
                                    "Incrementally compile, probe a unique candidate DLL, then "
                                    "reload "
                                    "at a "
                                    "runtime boundary. Failed builds retain the previous module."))
                                native->build();
                            if (ImGui::TreeNode("Compiler setup")) {
                                forge::ui::help(
                                    "Advanced executable paths for local gameplay compilation.");
                                if (ImGui::InputText("CMake", cmake_path, sizeof(cmake_path))) {
                                    native->cmake = cmake_path;
                                    perform(save_preferences);
                                }
                                forge::ui::help(
                                    "CMake executable path or command on PATH. Version 3.24 or "
                                    "newer required.");
                                if (ImGui::InputText("Ninja", ninja_path, sizeof(ninja_path))) {
                                    native->ninja = ninja_path;
                                    perform(save_preferences);
                                }
                                forge::ui::help("Ninja executable path or command on PATH. Launch "
                                                "FORGE from an "
                                                "x64 Native "
                                                "Tools command prompt to provide the MSVC compiler "
                                                "environment.");
                                ImGui::TreePop();
                            }
                            forge::ui::help("Expand compiler and build-tool paths.");
                            if (ImGui::Checkbox("Build on save", &auto_build)) {
                                native->auto_build = auto_build;
                                perform(save_preferences);
                            }
                            forge::ui::help("Watch C/C++ headers, sources and CMake files in "
                                            "Native. Builds after "
                                            "saves settle; play continues during compilation.");
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                        ImGui::EndDisabled();
                        ImGui::TextWrapped("%s", native->source_path().c_str());
                        forge::ui::help(
                            "Edit this source in your code editor. The sample moves entities "
                            "along the X axis.");
                        if (!native->log().empty()) {
                            forge::ui::heading(
                                "Build output",
                                "Recent compiler output. The complete current log is in "
                                "Project/.forge/native/build.log.");
                            ImGui::PushFont(log_font);
                            ImGui::TextUnformatted(native->log().c_str());
                            ImGui::PopFont();
                            forge::ui::help(
                                "Compiler diagnostics and build/validation results. Scroll "
                                "horizontally for long source locations.");
                        }
                        ImGui::TextWrapped("%s", native->status().c_str());
                        forge::ui::help("Native build and candidate activation status.");
                        ImGui::TextWrapped(
                            "Windows: use Run-Forge-Dev.cmd or an x64 Native Tools command prompt. "
                            "Requires Visual Studio C++ tools, CMake and Ninja.");
                        forge::ui::help(
                            "These tools compile your gameplay locally. They are not needed to "
                            "open the editor.");
                    }
                }
                ImGui::End();
            }
            editor.record_status("Editor", message);
            editor.record_status("Files", files.status);
            editor.record_status("Runtime", play.status());
            editor.record_status("Build", native->status());
            if (!runtime_ui.diagnostic().empty())
                editor.problems.ingest({"runtime-ui/" + runtime_ui.diagnostic(),
                                        "Error",
                                        runtime_ui.diagnostic(),
                                        {},
                                        {},
                                        "Runtime UI",
                                        {}});
            if (!native->error().empty())
                editor.problems.ingest({"build/" + native->error(),
                                        "Error",
                                        native->error(),
                                        {},
                                        native->source_path(),
                                        {},
                                        {}});
            if (!files.error.empty())
                editor.problems.ingest({"files/" + files.error,
                                        "Error",
                                        files.error,
                                        {},
                                        forge::path_text(files.document.path()),
                                        {},
                                        scene.asset_id()});
            if (workspace.console && !workspace.bottom_folded) {
                if (ImGui::Begin("Console", &workspace.console,
                                 ImGuiWindowFlags_HorizontalScrollbar)) {
                    forge::ui::heading("Session log",
                                       "Chronological status changes for this session. Problems "
                                       "keeps actionable errors and warnings.");
                    if (forge::ui::button(
                            "Clear log",
                            "Clear the editor's displayed session log; project data is unchanged."))
                        editor.log.clear();
                    ImGui::PushFont(log_font);
                    for (const auto& line : editor.log)
                        ImGui::TextWrapped("%s", line.c_str());
                    ImGui::PopFont();
                    forge::ui::heading("Current details",
                                       "Current file/runtime state and low-level logs.");
                    ImGui::TextWrapped("%s", message.c_str());
                    forge::ui::help("Latest operation result.");
                    ImGui::TextWrapped("%s", files.status.c_str());
                    forge::ui::help("Latest project, file, autosave, or recovery result.");
                    ImGui::TextWrapped("Project: %s",
                                       forge::path_text(files.document.project()).c_str());
                    forge::ui::help("Current project root. Native builds, scenes, and recovery "
                                    "snapshots belong to this project.");
                    if (!play.log().empty()) {
                        ImGui::TextWrapped("%s", play.log().c_str());
                        forge::ui::help(
                            "Recent runtime error output, limited to 64 KiB per play session.");
                    }
                    ImGui::TextWrapped("%s", play.status().c_str());
                    if (play.active()) {
                        forge::draw_input_monitor(play);
                        const auto& timing = play.timing();
                        ImGui::Text(
                            "Tick: %llu | Fixed: %.0f Hz | Dropped: %llu | Clamped: %.3f s",
                            static_cast<unsigned long long>(timing.value("tick", std::uint64_t{})),
                            timing.value("simulation_hz", 60.0),
                            static_cast<unsigned long long>(
                                timing.value("dropped_ticks", std::uint64_t{})),
                            timing.value("clamped_seconds", 0.0));
                        forge::ui::help(
                            "Authoritative runtime tick count. Step adds exactly one. Overload "
                            "drops whole-tick debt; clamped seconds report long stalls. Fixed "
                            "timing alone does not guarantee deterministic gameplay.");
                    }
                    forge::ui::help("Play process status. A runtime failure leaves the editor and "
                                    "authored scene available.");
                }
                ImGui::End();
            }

            if (documents.consume_close_cancellation())
                pending_switch.reset();
            if (pending_switch && documents.close_pending_sources()) {
                auto action = *pending_switch;
                pending_switch.reset();
                files.request(action);
            }
            auto collect_diagnostics = [&](const auto& records) {
                for (const auto& d : records) {
                    const auto severity = d.value("severity", std::string{});
                    if (severity != "error" && severity != "warning" && severity != "fatal")
                        continue;
                    const forge::Json ctx = d.value("context", forge::Json::object());
                    forge::ui::Problem problem;
                    problem.key = d.dump();
                    problem.text = d.value("text", std::string{});
                    problem.severity = severity;
                    problem.entity = ctx.value("entity", std::string{});
                    problem.source = ctx.value("source", std::string{});
                    problem.property = ctx.value("property", std::string{});
                    if (ctx.contains("asset"))
                        problem.asset = ctx.at("asset").get<forge::AssetId>();
                    editor.problems.ingest(std::move(problem));
                }
            };
            collect_diagnostics(scene_engine.services().diagnostics());
            collect_diagnostics(play.diagnostics());
            if (const auto* rendered = viewport.meshes()) {
                forge::Json records = forge::Json::array();
                for (const auto& diagnostic : rendered->diagnostics())
                    records.push_back(forge::diagnostic_json(diagnostic));
                collect_diagnostics(records);
            }
            if (play.ready()) {
                forge::Json records = forge::Json::array();
                for (const auto& diagnostic : game_viewport.diagnostics())
                    records.push_back(forge::diagnostic_json(diagnostic));
                collect_diagnostics(records);
            }
            if (panels_before != workspace.settings()) {
                try {
                    perform(save_preferences);
                } catch (const std::exception& e) {
                    message = e.what();
                }
            }
            performance.draw(viewport.redraws, viewport.retained);
            resource_inspector.draw(mesh_resources, presentation);
            // Apply default focus after all first-use dock tabs have been created.
            if (rebuilt_workspace)
                ImGui::SetWindowFocus("Content");
#ifdef FORGE_UI_FIXTURE
            if (const auto* target = fixture.focused_document())
                if (fixture.stage < 56 || (fixture.stage >= 59 && fixture.stage <= 66) ||
                    (fixture.stage >= 73 && fixture.stage < 82) ||
                    (fixture.stage >= 85 && fixture.stage <= 96))
                    ImGui::SetWindowFocus(target);
            // Count textures used by this UI frame, before advance can finish
            // another tile. A newly completed image appears on the next frame.
            const auto drawn_thumbnail_count =
                content_thumbnails ? content_thumbnails->ready_count() : 0;
#endif
            if (content_thumbnails)
                content_thumbnails->advance();
            const auto ui_submit = forge::ui::Performance::Clock::now();
            auto* rtv = swap->GetCurrentBackBufferRTV();
            context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[] = {0.04f, 0.05f, 0.06f, 1};
            context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            ImGui::PopItemFlag();
            gui->Render(context);
#ifdef FORGE_UI_FIXTURE
            if (fixture.workflow) {
                forge::Json selected_preview = forge::Json::object();
                if (!selected.empty()) {
                    auto effective = scene.effective_document();
                    for (const auto& entity : effective.at("entities"))
                        if (entity.at("id") == selected)
                            selected_preview = entity;
                }
                forge::Json disk;
                if (std::ifstream file(files.document.path()); file)
                    file >> disk;
                forge::Json problems = forge::Json::array();
                for (const auto& problem : editor.problems.items())
                    problems.push_back({{"severity", problem.severity},
                                        {"text", problem.text},
                                        {"entity", problem.entity},
                                        {"asset", problem.asset ? forge::Json(problem.asset)
                                                                : forge::Json(nullptr)},
                                        {"source", problem.source}});
                const forge::Json state{{"problems", problems},
                                        {"scene", scene.document()},
                                        {"selected", selected},
                                        {"selected_preview", selected_preview},
                                        {"dirty", files.document.dirty()},
                                        {"disk", disk},
                                        {"playing", play.active()},
                                        {"control_ready", play.control_ready()},
                                        {"paused", play.paused()},
                                        {"tick", play.timing().value("tick", std::uint64_t{0})},
                                        {"cameras", game_viewport.cameras().size()},
                                        {"ui_scale", forge::ui::interface_scale},
                                        {"status", message}};
                auto observed = state;
                observed["collision_preview_ready"] = physics_overlay.ready();
                observed["physics"] = play.physics_status();
                observed["collision_document_ready"] =
                    collision_editor.document() && !collision_editor.dirty();
                if (collision_editor.document())
                    observed["collision_source"] = collision_editor.document()->source().document;
                observed["model_ready"] =
                    model_imports.placement_ready() && !model_imports.pending();
                observed["source_imported"] =
                    source_import.finished() && source_import.published_count() == 1;
                observed["model_asset"] = model_imports.selected_asset()
                                              ? forge::Json(model_imports.selected_asset())
                                              : forge::Json(nullptr);
                observed["model_generation"] = model_imports.selection_generation();
                observed["project"] = forge::path_utf8(files.document.project());
                observed["material_document"] = material_editor.document()
                                                    ? material_editor.document()->source().document
                                                    : forge::Json(nullptr);
                observed["material_dirty"] = material_editor.dirty();
                observed["material_disk"] =
                    material_editor.document()
                        ? forge::read_json(files.document.project() /
                                           material_editor.document()->locator())
                        : forge::Json(nullptr);
                observed["export_output"] = game_export.output();
                observed["export_error"] = game_export.error();
                observed["dependencies_busy"] = runtime_dependencies.busy();
                observed["dependencies_dirty"] = runtime_dependencies.dirty();
                observed["failed_imports"] = forge::Json::array();
                observed["selected_asset"] = editor.selection.asset()
                                                 ? forge::Json(editor.selection.asset())
                                                 : forge::Json(nullptr);
                for (const auto& [id, activity] : content_imports.activity())
                    if (activity == forge::AssetJobState::Failed)
                        observed["failed_imports"].push_back(id);
                input_workflow.finish(
                    observed,
                    [&](const std::string& name) {
                        fixture.capture(device, context, rtv, true, name);
                    },
                    [&](forge::Json record) {
                        record["build"] = forge::build_id;
                        record["source_commit"] = forge::source_commit;
                        record["platform"] = "Windows / D3D12 WARP";
                        record["window_pixels"] = {width, height};
                        record["visual_review"] = "Pending human/agent image inspection";
                        forge::atomic_write(fixture.output / "workflow.json", record.dump(2));
                    });
                if (input_workflow.done())
                    running = false;
            } else {
                ++fixture.frames;
                bool captured_document_visible = true;
                if (const auto* target = fixture.focused_document()) {
                    const auto* w = ImGui::FindWindowByName(target);
                    captured_document_visible =
                        w && w->Active && !w->Hidden && (!w->DockIsActive || w->DockTabIsVisible);
                }
                if (fixture.stage >= 56 && fixture.stage <= 58) {
                    const auto* popup = ImGui::FindWindowByName("##Combo_00");
                    captured_document_visible &=
                        forge::fixture_open_mesh_picker && popup && popup->Active && !popup->Hidden;
                }
                if (fixture.stage >= 67 && fixture.stage <= 72) {
                    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                    captured_document_visible &= !model_imports.fixture_open_variant &&
                                                 !forge::ui::fixture_open_model_variant &&
                                                 !popups.empty() && popups.back().Window &&
                                                 popups.back().Window->Active &&
                                                 !popups.back().Window->Hidden;
                }
                if (fixture.stage >= 73)
                    captured_document_visible &= !model_imports.fixture_open_notes;
                if (fixture.stage >= 82 && fixture.stage <= 84) {
                    const auto* popup = ImGui::FindWindowByName("Migrate component values");
                    captured_document_visible &= !authored_components.fixture_open_migration &&
                                                 popup && popup->Active && !popup->Hidden;
                }
                if (fixture.stage >= 97) {
                    const auto* popup = ImGui::FindWindowByName("Import source files");
                    captured_document_visible &= popup && popup->Active && !popup->Hidden;
                    if (!source_import.diagnostic().empty())
                        throw std::runtime_error("Source import capture failed: " +
                                                 source_import.diagnostic());
                }
                if (fixture.stage >= 47 && fixture.stage <= 49 &&
                    !audio_imports.diagnostic().empty())
                    throw std::runtime_error("Audio import fixture failed: " +
                                             audio_imports.diagnostic());
                if (fixture.stage >= 50 && !shader_imports.diagnostic().empty())
                    throw std::runtime_error("Shader import fixture failed: " +
                                             shader_imports.diagnostic());
                if ((fixture.stage == 30 || fixture.stage == 31) && model_viewer &&
                    !model_viewer->error().empty())
                    throw std::runtime_error("Model preview fixture failed: " +
                                             model_viewer->error());
                if (fixture.stage >= 32 && fixture.stage <= 35 &&
                    !asset_view_document.error().empty())
                    throw std::runtime_error("Member preview fixture failed: " +
                                             asset_view_document.error());
                if (fixture.prepared && fixture.frames > 12 && captured_document_visible &&
                    ((fixture.stage != 26 && fixture.stage != 27 && fixture.stage != 41) ||
                     (texture_viewer && texture_viewer->ready())) &&
                    (fixture.stage != 6 || (play.control_ready() && !play.paused())) &&
                    (fixture.stage != 7 || (play.paused() && game_input.captured())) &&
                    ((fixture.stage != 28 && fixture.stage != 29 && fixture.stage != 42) ||
                     (material_preview && !material_preview->pending())) &&
                    ((fixture.stage != 30 && fixture.stage != 31 && fixture.stage != 43) ||
                     (model_viewer && model_viewer->ready())) &&
                    (((fixture.stage < 32 || fixture.stage > 35) && fixture.stage != 44 &&
                      fixture.stage != 45) ||
                     asset_view_document.ready()) &&
                    ((fixture.stage != 36 && fixture.stage != 37 && fixture.stage != 46) ||
                     (content_thumbnails && drawn_thumbnail_count >= 5)) &&
                    ((fixture.stage < 38 || fixture.stage > 40) ||
                     (content_files.operation() &&
                      content_files.operation()->state() == forge::AssetFileState::Review)) &&
                    ((fixture.stage < 47 || fixture.stage > 49) ||
                     (!audio_imports.dirty() && !audio_imports.pending() &&
                      audio_imports.diagnostic().empty() &&
                      content.record(audio_imports.selected_asset()) &&
                      content.record(audio_imports.selected_asset())
                          ->metadata.contains("forge.audio"))) &&
                    (fixture.stage != 59 || (!model_imports.dirty() && !model_imports.pending() &&
                                             model_viewer && model_viewer->ready())) &&
                    ((fixture.stage < 60 || fixture.stage > 62) || asset_view_document.ready()) &&
                    (fixture.stage < 64 ||
                     (material_preview && !material_preview->pending() &&
                      !material_editor.dirty() && !material_editor.pending())) &&
                    (fixture.stage < 50 || (!shader_imports.dirty() && !shader_imports.pending() &&
                                            shader_imports.diagnostic().empty()))) {
                    if ((fixture.stage == 28 || fixture.stage == 29) &&
                        !material_preview->diagnostics().empty())
                        throw std::runtime_error("Material editor fixture preview failed");
                    if (fixture.stage >= 64 && !material_preview->diagnostics().empty())
                        throw std::runtime_error("Custom surface fixture preview failed");
                    if ((fixture.stage == 26 || fixture.stage == 27) &&
                        !texture_viewer->error().empty())
                        throw std::runtime_error("Texture viewer fixture failed: " +
                                                 texture_viewer->error());
                    if (fixture.stage == 0) {
                        auto* content_window = ImGui::FindWindowByName("Content");
                        if (!content_window || !content_window->DockTabIsVisible)
                            throw std::runtime_error("Fresh workspace did not select Content");
                    }
                    if (fixture.stage == 0 || fixture.stage >= 17) {
                        forge::Json metrics;
                        for (const char* id :
                             {"##FORGE-toolbar", "##global-actions", "##FORGE-status", "###Scene"})
                            if (auto* w = ImGui::FindWindowByName(id))
                                metrics[id] = {{"x", w->Pos.x},
                                               {"y", w->Pos.y},
                                               {"width", w->Size.x},
                                               {"height", w->Size.y},
                                               {"content_y", w->DC.CursorStartPos.y}};
                        metrics["scale"] = forge::ui::interface_scale;
                        metrics["scene_image_y"] = fixture.scene_image_y;
                        forge::atomic_write(
                            fixture.output / ("chrome-" + std::to_string(fixture.stage) + ".json"),
                            metrics.dump(2));
                    }
                    fixture.capture(device, context, rtv);
                    if (fixture.stage == 103) {
                        check_thumbnail_cache(presentation, context, files.document.project(),
                                              mesh_resources);
                        play.stop();
                        running = false;
                    }
                }
            }
#endif
            const auto present = forge::ui::Performance::Clock::now();
            if (mesh_resources)
                mesh_resources->submit();
            if (!texture_imports.is_open() ||
                (texture_viewer && texture_viewer->project() != files.document.project()))
                texture_viewer.reset();
            texture_view_document.after_submission(files.document.project());
            if (!model_imports.is_open() ||
                (model_viewer && model_viewer->project() != files.document.project()))
                model_viewer.reset();
            asset_view_document.after_submission(files.document.project());
            if (content_thumbnails)
                content_thumbnails->after_submission();
#ifdef FORGE_UI_FIXTURE
            fixture.graphics_context(window.get(), 1, device, context);
#endif
            swap->Present(0);
#ifdef FORGE_UI_FIXTURE
            fixture.graphics_context(window.get(), 2);
#endif
            performance.finish(ui_submit, present);
        }
        if (startup_layout.save_enabled)
            ImGui::SaveIniSettingsToDisk(ini.c_str());
        context->Flush();
        context->WaitForIdle();
#ifdef FORGE_UI_FIXTURE
        fixture.validate_graphics();
#endif
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
#ifndef FORGE_UI_FIXTURE
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FORGE could not continue", e.what(),
                                 window.get());
#endif
        result = 1;
    }
    window.reset();
    SDL_Quit();
    return result;
}
