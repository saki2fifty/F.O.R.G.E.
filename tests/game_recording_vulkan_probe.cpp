#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "ImGuiImplDiligent.hpp"
#include "editor/files.hpp"
#include "editor/game_recording_task.hpp"
#include "editor/play_presentation.hpp"
#include "editor/sdk_platform_effects.hpp"
#include "fixtures/game_recording_gallery.hpp"
#include "imgui_input_driver.hpp"
#include "input_recording_files.hpp"
#include "input_recording_protocol.hpp"
#include "runtime_dependencies.hpp"
#include "ui_test_renderer.hpp"
#include "view_image_capture_reference.hpp"
#include <forge/game_settings.hpp>
#include <iostream>
#include <set>

using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
AssetId author_gallery(const std::filesystem::path& project) {
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    const auto bytes = asset_detail::read_bytes(font, 4 * 1024 * 1024);
    const RuntimeUiInspector inspect = [&](AssetId id, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, project, bytes);
        return presenter.inspect_static({id});
    };
    return test::author_game_recording_gallery(project, inspect);
}
void exercise(DiligentPresentation& presentation, IDeviceContext* graphics,
              const std::filesystem::path& gallery, const std::filesystem::path& runtime,
              const std::filesystem::path& worker, const std::filesystem::path& root, float scale,
              bool narrow, int record, const std::filesystem::path& module = {}) {
    const bool session_movie = record == 3;
    const auto project = root / "Project";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, project, std::filesystem::copy_options::recursive);
    if (!std::filesystem::exists(project / test::game_recording_scene_path))
        (void)author_gallery(project);
    EngineContext engine;
    Scene scene(engine.world());
    std::vector<std::string> recent;
    EditorFiles files(scene, nullptr, recent);
    files.start(project);
    files.document.open_scene(test::game_recording_scene_path);
    auto settings = files.document.settings().document();
    settings["game"] = default_game_settings(
        "org.forge.recording-render-" + AssetId::generate().str(), "Recording rendering fixture");
    files.document.save_settings(settings);
    if (session_movie) {
        check(!module.empty(), "Session movie needs its original SDK example module");
        std::filesystem::copy_file(module, project / module.filename());
        const auto runtime_path = path_utf8(runtime);
        const char* arguments[]{runtime_path.c_str(), "--sdk-info", nullptr};
        auto* process = SDL_CreateProcess(arguments, true);
        check(process, SDL_GetError());
        std::size_t size = 0;
        int code = -1;
        auto* output = SDL_ReadProcess(process, &size, &code);
        SDL_DestroyProcess(process);
        const std::unique_ptr<void, decltype(&SDL_free)> bytes(output, &SDL_free);
        check(output && code == 0 && size <= 65536, "Runtime SDK identity query failed");
        const auto sdk =
            Json::parse(static_cast<const char*>(output), static_cast<const char*>(output) + size);
        settings["modules"] =
            Json::array({{{"id", "project.session_input"},
                          {"implementation", "1"},
                          {"sdk", "experimental-1"},
                          {"fingerprint", sdk.at("fingerprint")},
                          {"library", path_utf8(module.filename())},
                          {"dependencies", {"forge.input", "forge.transforms", "forge.game"}}}});
        files.document.save_settings(settings);
        Json hud, tone;
        const auto recording_source = scene.snapshot();
        for (const auto& entity : recording_source.at("entities")) {
            const auto& components = entity.at("components");
            if (components.contains("forge.ui_document"))
                hud = components.at("forge.ui_document");
            if (tone.is_null() && components.contains("forge.audio_source"))
                tone = components.at("forge.audio_source");
        }
        check(!hud.is_null() && !tone.is_null(), "Movie fixture needs its existing HUD and tone");
        tone["play_on_start"] = tone["loop"] = true;
        const std::array paths{"Scenes/235-session-input-a.scene.json",
                               "Scenes/236-session-input-b.scene.json"};
        std::array<Json, 2> snapshots;
        std::array<AssetId, 2> ids;
        for (unsigned i = 0; i < paths.size(); ++i) {
            files.document.open_scene(paths[i]);
            const auto entity =
                authoring_command(scene, "entity.create",
                                  {{"recipe", "empty"}, {"name", "Movie HUD and tone"}})
                    .at("selected")
                    .get<std::string>();
            auto candidate = scene.document();
            for (auto& row : candidate.at("entities"))
                if (row.at("id") == entity) {
                    row["components"]["forge.ui_document"] = hud;
                    row["components"]["forge.audio_source"] = tone;
                    row["components"]["forge.audio_source"]["pitch"] = i ? 1.5 : .75;
                }
            scene.edit(candidate);
            files.document.save();
            snapshots[i] = scene.snapshot();
            ids[i] = scene.asset_id();
        }
        auto catalog = AssetCatalog::open_project(project);
        for (unsigned i = 0; i < ids.size(); ++i)
            catalog = declare_runtime_dependencies(*files.document.writer_guard(), ids[i],
                                                   {{ids[1 - i],
                                                     "scene",
                                                     AssetDependencyKind::Runtime,
                                                     "declared:Session movie next Scene",
                                                     {}}},
                                                   catalog.document());
        RuntimeInput first, second, again;
        const InputMap map(resolve_game_settings(settings.at("game"), Json::object(),
                                                 ProjectSettings(project).input())
                               .at("input_map"));
        for (auto* input : {&first, &second, &again})
            input->configure(map);
        const auto fingerprint = sdk.at("fingerprint").get<std::string>();
        first.begin_session_input_recording(
            input_recording_source(snapshots[0], project, fingerprint), 60, ids[0]);
        const auto visit = [](RuntimeInput& input, bool change) {
            input.submit({{"key.space", 1}, {"key.space", 0}});
            input.latch(1);
            if (change)
                input.submit({{"key.tab", 1}, {"key.tab", 0}});
            input.latch(2);
        };
        visit(first, true);
        second.inherit_session_input_recording(
            first, input_recording_source(snapshots[1], project, fingerprint), 60, ids[1]);
        visit(second, true);
        again.inherit_session_input_recording(
            second, input_recording_source(snapshots[0], project, fingerprint), 60, ids[0]);
        visit(again, false);
        again.finish_input_recording();
        write_input_recording(project, "Recordings/movie.json", again.input_recording_document(),
                              false);
        files.document.open_scene(paths[0]);
    }
    const auto authored = scene.snapshot();
    const auto revision = scene.revision();
    Diligent::ImGuiImplDiligent gui(
        {presentation.device(), TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_UNKNOWN});
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    ui::interface_scale = scale;
    ui::style(scale);
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(ImGui::GetIO().Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16), "Missing UI font");
    ImGui::GetIO().DeltaTime = 1.f / 60;
    const unsigned width = unsigned((narrow ? 480 : 720) * scale), height = unsigned(900 * scale);
    ImGui::GetIO().DisplaySize = {float(width), float(height)};
    ui::EditorUiContext editor;
    ui::ContextScope scope(editor);
    test::observe_ui = true;
    TextureDesc desc;
    desc.Name = "FORGE native game recording controls";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> controls;
    presentation.device()->CreateTexture(desc, nullptr, &controls);
    check(bool(controls), "Recording control target unavailable");
    auto* rtv = controls->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* srv = controls->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    PlaySession play;
    GameInput input_owner;
    RuntimeUiHost runtime_ui(nullptr, presentation.device(), font);
    SdkPlatformEffects platform_effects(play, input_owner,
                                        [&](const std::string& direction, std::string& error) {
                                            runtime_ui.checked_navigate(direction, error);
                                        });
    auto resources = std::make_shared<MeshResourceHost>(presentation, graphics, project);
    resources->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    FrameRenderer* renderer = nullptr;
    PlayPresentation staged;
    staged.adopt_active_renderer(&renderer, std::make_unique<FrameRenderer>(presentation));
    staged.set_active_ui(&runtime_ui);
    staged.set_context(graphics);
    staged.set_presentation(&presentation);
    staged.set_mesh_resource_host(resources);
    ui::ViewImageTask images(worker);
    ui::GameRecordingRequest request;
    request.destination = root / "Movie";
    request.frames = 31;
    request.fps = 30;
    request.output.width = 640;
    request.output.height = 360;
    if (session_movie) {
        request.capture_scope = "session";
        request.input_recording = "Recordings/movie.json";
        request.frames = 4;
    }
    ui::GameRecordingHost host;
    host.start = [&](const auto& user_data) {
        play.configure(60, session_movie ? ProjectSettings(project).input() : InputMap{},
                       {0, -9.81, 0}, project, true, true);
        play.set_user_data_override(user_data);
        play.start(path_utf8(runtime), scene.snapshot(), false, Json(), true);
    };
    host.stop = [&] { play.stop(); };
    host.poll_images = [&](auto& task, auto* context, const auto& path, AssetId id, auto& status) {
        task.poll(presentation, context, path, id, status);
    };
    host.capture_image = [](auto& task, auto& owner, auto* context, auto* texture, auto& status) {
        task.capture(ui::ViewImageTask::View::Game, owner, context, texture, status);
    };
    host.abandon_readback = [&] { presentation.cancel_readback(); };
    auto work = std::make_unique<ui::GameRecordingTask>(scene, files.document, play, worker,
                                                        request, std::move(host), true);
    auto* task = work.get();
    std::string message;
    check(images.begin_work(std::move(work), project, scene.asset_id(), message),
          "Recording did not open");
    std::string observed_session;
    std::uint64_t release_epoch = 0;
    std::set<unsigned> referenced;
    Json samples = Json::array();
    const auto frame = [&] {
        presentation.begin_frame();
        play.pump();
        if (!play.session().empty() && observed_session != play.session()) {
            if (play.submit_editor_observation(false, "KeyboardMouse"))
                observed_session = play.session();
        }
        if (release_epoch &&
            (!play.sdk_release_required() || play.editor_epoch_confirmed() >= release_epoch))
            release_epoch = 0;
        if (play.sdk_release_required() && !release_epoch &&
            input_owner.submit_external_release_observation(play))
            release_epoch = play.current_effective_epoch();
        platform_effects.pump(true);
        Json platform_acks;
        (void)platform_effects.take_pending_ack(platform_acks);
        check(!platform_effects.stop_requested(), "SDK platform acknowledgement was rejected");
        (void)staged.poll(play, project);
        if (auto failure = staged.take_commit_failure(); !failure.empty())
            throw std::runtime_error(failure);
        images.poll_work(graphics, message);
        runtime_ui.sync(play, project, true);
        if (play.ready()) {
            const auto extent =
                images.sequence_extent().value_or(ui::SequenceFrameExtent{640, 360});
            runtime_ui.prepare_frame(play, {0, 0}, {float(extent.width), float(extent.height)},
                                     extent.width, extent.height);
            auto source = extract_render_scene(play.effective_snapshot());
            play.bind_tile_maps(source);
            const auto sample = images.temporal_sample();
            auto* output = renderer->game(graphics, std::move(source), extent.width, extent.height,
                                          play.timing().at("presentation_seconds").get<double>(),
                                          sample ? &*sample : nullptr);
            check(output, "Recording has no Game display");
            runtime_ui.draw(play, graphics,
                            output->GetTexture()->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
            check(runtime_ui.diagnostic().empty(), "Runtime UI: " + runtime_ui.diagnostic());
            const bool ready = !renderer->pending() && renderer->diagnostics().empty();
            const auto state = task->state();
            const auto index = state.at("completed_frames").get<unsigned>();
            if (ready && state.at("state") == "image" &&
                (session_movie ? index < unsigned(request.frames)
                               : (index == 0 || index == 15 || index == 30)) &&
                !referenced.contains(index)) {
                test::ViewImageReference reference;
                reference.queue(presentation.device(), graphics, output);
                const auto rgba = reference.read(graphics);
                save_view_png(root / ("reference-" + std::to_string(index) + ".png"), extent.width,
                              extent.height, rgba);
                samples.push_back({{"frame", index},
                                   {"timing", play.timing()},
                                   {"world", play.effective_snapshot()}});
                referenced.insert(index);
            }
            images.capture(ui::ViewImageTask::View::Game, presentation, graphics, output, message,
                           ready);
        }
        test::ui_targets.clear();
        gui.NewFrame(width, height, SURFACE_TRANSFORM_IDENTITY);
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Always);
        images.draw(message);
        graphics->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{.035f, .06f, .07f, 1};
        graphics->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        gui.Render(graphics);
        resources->submit();
        presentation.end_frame(graphics);
        graphics->Flush();
        graphics->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        images.poll_work(graphics, message);
    };
    for (unsigned i = 0; i < 8; ++i)
        frame();
    for (const char* key :
         {"game-recording:parent", "game-recording:name", "game-recording:fps",
          "game-recording:frames", "game-recording:width", "game-recording:height",
          "game-recording:quality", "game-recording:input", "game-recording:scope",
          "game-recording:start", "game-recording:cancel"}) {
        const auto& box = test::ui_targets.at(key);
        check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              std::string("Recording control disabled or clipped: ") + key);
    }
    test::ViewImageReference capture;
    capture.queue(presentation.device(), graphics, srv);
    save_view_png(root / "controls.png", width, height, capture.read(graphics));
    test::ImGuiInputDriver input(frame);
    input.click(record ? "game-recording:start" : "game-recording:cancel");
    if (record) {
        const auto end = SDL_GetTicks() + 90000;
        bool cancelled = false;
        while (images.busy() && SDL_GetTicks() < end) {
            frame();
            if (record == 2 && !cancelled && task->state().at("captured_png_frames") != 0) {
                input.click("game-recording:cancel");
                cancelled = true;
            }
            SDL_Delay(1);
        }
        check(!images.busy() &&
                  task->state().at("state") == (record == 2 ? "cancelled" : "complete"),
              "Rendered recording failed: " + task->state().dump() +
                  " | transport=" + play.fixture_transport_state().dump() + " | recording=" +
                  play.recording_status().dump() + " | loading=" + play.sdk_loading().dump());
        check((record == 2 ? cancelled && !referenced.empty()
                           : referenced.size() == (session_movie ? 4u : 3u)) &&
                  !play.active(),
              "Recording omitted sampled frames, cancellation or Play retirement");
        asset_storage::replace(root / "render-samples.json", samples.dump(2));
        if (record != 2)
            check(std::filesystem::file_size(root / "Movie/audio.s16le") ==
                      (session_movie ? 6400u : 49600u) * 4,
                  "Rendered recording PCM duration differs");
        else
            check(!std::filesystem::exists(root / "Movie/game.avi") &&
                      std::filesystem::exists(root / "Movie/frame-000000.png"),
                  "Native cancellation published a partial movie or lost its PNG");
        if (session_movie) {
            const auto receipt =
                Json::parse(asset_storage::read(root / "Movie/recording.json").value());
            check(receipt.at("visits").size() == 3 && receipt.at("frames").size() == 4 &&
                      receipt.at("frames")[0].at("scene") == receipt.at("frames")[3].at("scene") &&
                      receipt.at("frames")[0].at("scene") != receipt.at("frames")[1].at("scene"),
                  "Rendered movie lost its repeated Scene image provenance");
            for (const auto& sample : samples) {
                const auto frame = sample.at("frame").get<unsigned>();
                check(receipt.at("frames").at(frame).at("scene") ==
                              sample.at("world").at("asset_id") &&
                          receipt.at("frames").at(frame).at("simulation_tick") ==
                              sample.at("timing").at("tick"),
                      "PNG Scene/tick receipt differs from the actual rendered world");
            }
            const auto pcm = asset_detail::read_bytes(root / "Movie/audio.s16le", 65536);
            check(std::ranges::any_of(pcm, [](std::byte b) { return b != std::byte{}; }),
                  "Rendered session movie omitted its autoplay audio");
        }
    } else {
        frame();
        check(!images.busy() && !play.active() && !std::filesystem::exists(root / "Movie"),
              "Cancelling options launched Play or created output");
    }
    check(scene.snapshot() == authored && scene.revision() == revision,
          "Native recording altered the authored Scene");
    test::observe_ui = false;
    test::ui_targets.clear();
    staged.clear(play);
    runtime_ui.clear(play);
    graphics->WaitForIdle();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            const auto root = std::filesystem::absolute(argv[2]);
            const auto id = author_gallery(root);
            std::cout << Json{{"scene", id}, {"path", test::game_recording_scene_path}}.dump()
                      << '\n';
            return 0;
        }
        check(argc == 5 || argc == 6 ||
                  (argc == 7 && std::string_view(argv[6]) == "--session-only"),
              "Need Gallery, matching SDK runtime, asset worker, output root and optional module");
        const auto root = std::filesystem::absolute(argv[4]) / AssetId::generate().str();
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> graphics;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &graphics);
        check(device && graphics, "Recording Vulkan device unavailable");
        DiligentPresentation presentation(device);
        if (argc != 7)
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool narrow : {false, true})
                    exercise(
                        presentation, graphics, std::filesystem::absolute(argv[1]),
                        std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                        root / ((narrow ? "narrow-" : "wide-") + std::to_string(int(scale * 100))),
                        scale, narrow, scale == 1 ? (narrow ? 2 : 1) : 0);
        if (argc >= 6)
            exercise(presentation, graphics, std::filesystem::absolute(argv[1]),
                     std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                     root / "session", 1, false, 3, std::filesystem::absolute(argv[5]));
        std::cout << Json{{"output", path_utf8(root)},
                          {"native_layouts", argc == 7 ? 1 : 6},
                          {"rendered_game_frames", argc == 7 ? 4 : 31},
                          {"backend", "software Vulkan"}}
                         .dump()
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
