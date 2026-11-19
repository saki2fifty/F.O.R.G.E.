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
    const bool live = record >= 4;
    const bool live_session = record == 8 || record == 9;
    const bool live_pending_copy = record == 9;
    const bool live_scene_change = record == 7;
    const bool cancel_movie = record == 2 || record == 5;
    const bool run_movie = record != 0 && record != 6;
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
    if (live) {
        auto candidate = scene.document();
        unsigned sources = 0;
        for (auto& entity : candidate.at("entities"))
            if (auto& components = entity.at("components");
                components.contains("forge.audio_source")) {
                auto& source = components.at("forge.audio_source");
                source["play_on_start"] = source["loop"] = true;
                ++sources;
            }
        check(sources != 0, "Live recording fixture needs its existing audio source");
        scene.edit(candidate);
        files.document.save();
    }
    if (session_movie || live_scene_change || live_session) {
        check(!module.empty(), "Scene transition movie needs its original SDK example module");
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
    if (live) {
        request.recording_mode = "live";
        if (live_session)
            request.capture_scope = "session";
        if (live_session) {
            // Keep the software-rendered real-time workload below the existing
            // two-second mixer queue budget. The overflow path is not relaxed.
            request.output.width = 160;
            request.output.height = 90;
        }
        request.fps = 10;
        request.frames = live_scene_change ? 600 : live_session ? 60 : 15;
        play.configure(60, ProjectSettings(project).input(), {0, -9.81, 0}, project, true, true);
        play.set_headless_audio(true, true);
        play.set_user_data_override(root / "LiveUserData");
        play.start(path_utf8(runtime), scene.snapshot());
    }
    const auto original_launch = play.launch_id();
    const auto original_user_data = play.user_data_override();
    unsigned starts = 0, stops = 0;
    ui::GameRecordingTask* task = nullptr;
    std::set<unsigned> referenced;
    Json samples = Json::array();
    Json media_progress = Json::array();
    Json transport_timing = Json::array();
    const auto pump_play = [&] {
        const auto command = play.fixture_transport_state().value("command", "");
        const auto before = SDL_GetTicks();
        play.pump();
        const auto elapsed = SDL_GetTicks() - before;
        if (live_session && play.live_audio_status().is_object() && elapsed > 1)
            transport_timing.push_back(
                {{"milliseconds", before}, {"elapsed_ms", elapsed}, {"command", command}});
    };
    const auto observe_media = [&] {
        const auto& progress = play.live_audio_status();
        if (live_session && progress.is_object() &&
            (media_progress.empty() || media_progress.back().at("capture") != progress))
            media_progress.push_back({{"milliseconds", SDL_GetTicks()},
                                      {"capture", progress},
                                      {"transport", play.fixture_transport_state()}});
    };
    bool png_pending = false;
    const auto save_reference = [&](ITextureView* output, unsigned index) {
        check(!referenced.contains(index), "Game image was copied more than once");
        test::ViewImageReference reference;
        reference.queue(presentation.device(), graphics, output);
        const auto rgba = reference.read(graphics);
        const auto& extent = output->GetTexture()->GetDesc();
        save_view_png(root / ("reference-" + std::to_string(index) + ".png"), extent.Width,
                      extent.Height, rgba);
        samples.push_back(
            {{"frame", index}, {"timing", play.timing()}, {"world", play.effective_snapshot()}});
        referenced.insert(index);
    };
    ui::GameRecordingHost host;
    host.start = [&](const auto& user_data) {
        ++starts;
        play.configure(60, session_movie ? ProjectSettings(project).input() : InputMap{},
                       {0, -9.81, 0}, project, true, true);
        play.set_user_data_override(user_data);
        play.start(path_utf8(runtime), scene.snapshot(), false, Json(), true);
    };
    host.stop = [&] {
        ++stops;
        play.stop();
    };
    host.poll_images = [&](auto& task, auto* context, const auto& path, AssetId id, auto& status) {
        task.poll(presentation, context, path, id, status);
        png_pending = task.busy();
    };
    host.capture_image = [&](auto& image, auto& owner, auto* context, auto* texture, auto& status) {
        if (live)
            save_reference(texture,
                           task->state().at("captured_png_frames").template get<unsigned>());
        image.capture(ui::ViewImageTask::View::Game, owner, context, texture, status);
        png_pending = image.busy();
    };
    host.abandon_readback = [&] { presentation.cancel_readback(); };
    auto work = std::make_unique<ui::GameRecordingTask>(scene, files.document, play, worker,
                                                        request, std::move(host), true);
    task = work.get();
    std::string message;
    check(images.begin_work(std::move(work), project, scene.asset_id(), message),
          "Recording did not open");
    std::string observed_session;
    std::uint64_t release_epoch = 0;
    const auto frame = [&] {
        presentation.begin_frame();
        pump_play();
        observe_media();
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
            if (!live && ready && state.at("state") == "image" &&
                (session_movie ? index < unsigned(request.frames)
                               : (index == 0 || index == 15 || index == 30)) &&
                !referenced.contains(index)) {
                save_reference(output, index);
            }
            images.capture(ui::ViewImageTask::View::Game, presentation, graphics, output, message,
                           ready && !(live_pending_copy && task->state().at("state") == "image"));
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
    if (live) {
        const auto deadline = SDL_GetTicks() + 15000;
        while ((!play.control_ready() || play.paused()) && SDL_GetTicks() < deadline) {
            frame();
            SDL_Delay(1);
        }
        check(play.control_ready() && !play.paused(), "Live graphical Play did not become ready");
        if (live_scene_change || live_session)
            for (unsigned visit = 0; visit < 2; ++visit) {
                const auto generation = play.world_generation();
                play.input_event({"key.tab", 1});
                play.input_event({"key.tab", 0});
                const auto until = SDL_GetTicks() + 15000;
                while ((play.world_generation() == generation || !play.control_ready()) &&
                       SDL_GetTicks() < until) {
                    frame();
                    SDL_Delay(1);
                }
                check(play.world_generation() != generation && play.control_ready(),
                      "Scene transition fixture did not warm both existing Game worlds");
                for (unsigned i = 0; i < 8; ++i)
                    frame();
            }
        frame();
    }
    for (const char* key :
         {"game-recording:mode", "game-recording:parent", "game-recording:name",
          "game-recording:fps", "game-recording:frames", "game-recording:width",
          "game-recording:height", "game-recording:quality", "game-recording:input",
          "game-recording:scope", "game-recording:start", "game-recording:cancel"}) {
        const auto& box = test::ui_targets.at(key);
        const bool enabled = !live || std::string_view(key) != "game-recording:input";
        check(box.enabled == enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              std::string("Recording control disabled or clipped: ") + key);
    }
    test::ViewImageReference capture;
    capture.queue(presentation.device(), graphics, srv);
    save_view_png(root / "controls.png", width, height, capture.read(graphics));
    test::ImGuiInputDriver input(frame);
    if (live_session) {
        input.click("game-recording:scope");
        check(task->state().at("capture_scope") == "scene", "Native Scene scope toggle failed");
        input.click("game-recording:scope");
        check(task->state().at("capture_scope") == "session", "Native Across scenes toggle failed");
    }
    input.click(run_movie ? "game-recording:start" : "game-recording:cancel");
    if (run_movie) {
        const auto end = SDL_GetTicks() + 90000;
        bool cancelled = false, changed_scene = false;
        unsigned session_transitions = 0;
        std::uint64_t transition_generation = 0;
        const auto request_live_transition = [&] {
            if (live_pending_copy && !changed_scene && play.control_ready() &&
                task->state().at("state") == "image") {
                play.input_event({"key.tab", 1});
                play.input_event({"key.tab", 0});
                changed_scene = true;
            }
            if (live_session && !live_pending_copy && session_transitions < 2 && !png_pending &&
                play.control_ready() && !samples.empty() &&
                samples.back().at("world").at("asset_id") == play.snapshot().at("asset_id") &&
                play.world_generation() != transition_generation &&
                task->state().at("captured_png_frames") == samples.size()) {
                transition_generation = play.world_generation();
                play.input_event({"key.tab", 1});
                play.input_event({"key.tab", 0});
                ++session_transitions;
            }
        };
        while (images.busy() && SDL_GetTicks() < end) {
            frame();
            if (cancel_movie && !cancelled && task->state().at("captured_png_frames") != 0) {
                input.click("game-recording:cancel");
                cancelled = true;
            }
            if (live_scene_change && !changed_scene &&
                task->state().at("captured_png_frames") != 0) {
                play.input_event({"key.tab", 1});
                play.input_event({"key.tab", 0});
                changed_scene = true;
            }
            request_live_transition();
            // Service real IPC/PCM/file work between software render passes.
            // Keep actual mixer time, queue bounds and saved image provenance.
            const auto media_until = SDL_GetTicks() + 80;
            while (live_session && images.busy() && SDL_GetTicks() < media_until) {
                pump_play();
                observe_media();
                images.poll_work(graphics, message);
                request_live_transition();
                SDL_Delay(1);
            }
            SDL_Delay(1);
        }
        if (live_session) {
            asset_storage::replace(root / "media-progress.json", media_progress.dump(2));
            asset_storage::replace(root / "transport-timing.json", transport_timing.dump(2));
        }
        check(!images.busy() && task->state().at("state") ==
                                    (cancel_movie                             ? "cancelled"
                                     : live_scene_change || live_pending_copy ? "failed"
                                                                              : "complete"),
              "Rendered recording failed: " + task->state().dump() +
                  " | transport=" + play.fixture_transport_state().dump() + " | recording=" +
                  play.recording_status().dump() + " | loading=" + play.sdk_loading().dump());
        check((live_pending_copy ? changed_scene && referenced.empty()
               : cancel_movie    ? cancelled && !referenced.empty()
               : live            ? !referenced.empty()
                                 : referenced.size() == (session_movie ? 4u : 3u)) &&
                  play.active() == live,
              "Recording omitted sampled frames, cancellation or Play retirement");
        asset_storage::replace(root / "render-samples.json", samples.dump(2));
        if (!cancel_movie && !live_scene_change && !live_pending_copy)
            check(std::filesystem::file_size(root / "Movie/audio.s16le") ==
                      (live            ? (live_session ? 288000u : 72000u)
                       : session_movie ? 6400u
                                       : 49600u) *
                          4,
                  "Rendered recording PCM duration differs");
        else
            check(
                !std::filesystem::exists(root / "Movie/game.avi") &&
                    (std::filesystem::exists(root / "Movie/frame-000000.png") != live_pending_copy),
                "Native cancellation published a partial movie or lost its PNG");
        if (live) {
            const auto receipt =
                Json::parse(asset_storage::read(root / "Movie/recording.json").value());
            check(receipt.at("version") == (live_session ? 4 : 3) &&
                      receipt.at("mode") == "live_game" &&
                      receipt.at("captured_png_frames") == referenced.size() &&
                      play.live_audio_status().at("state") == "stopped" &&
                      play.live_audio_status().at("queued_frames") == 0,
                  "Live rendered recording lost saved images or left its tap active: " +
                      play.live_audio_status().dump() + " | " + task->state().dump());
            if (live_scene_change)
                check(
                    changed_scene && play.world_generation() != receipt.at("world_generation") &&
                        receipt.at("capture_scene") == authored.at("asset_id"),
                    "Live Scene guard did not retain the old image scope and admit the new world");
            for (const auto& sample : samples) {
                const auto& image = receipt.at("images").at(sample.at("frame").get<unsigned>());
                check(image.at("scene") == sample.at("world").at("asset_id") &&
                          image.at("simulation_tick") == sample.at("timing").at("tick"),
                      "Live PNG receipt differs from the actual copied Game world");
            }
            if (live_session && !live_pending_copy) {
                const auto& visits = receipt.at("source_visits");
                check(session_transitions == 2 && visits.size() == 3 &&
                          visits[0].at("scene") == visits[2].at("scene") &&
                          visits[0].at("scene") != visits[1].at("scene") &&
                          receipt.at("completed_frames") == 60 &&
                          receipt.at("audio_normalized") == true,
                      "Live session movie lost its repeated Scene visits or complete output");
                const auto origin = receipt.at("audio_source_start_frame").get<std::uint64_t>();
                for (unsigned i = 0; i < visits.size(); ++i) {
                    const auto source = visits[i].at("audio_frame").get<std::uint64_t>();
                    const auto& normalized = receipt.at("visits").at(i);
                    check(normalized.at("source_audio_frame") == source &&
                              normalized.at("audio_frame") ==
                                  (source > origin ? source - origin : 0),
                          "Live session movie rewrote its original PCM visit boundary");
                    check(
                        std::ranges::any_of(receipt.at("images"),
                                            [&](const Json& image) {
                                                return image.at("world_generation") ==
                                                           visits[i].at("generation") &&
                                                       image.at("scene") == visits[i].at("scene") &&
                                                       image.at("source") == visits[i].at("source");
                                            }),
                        "A live Scene visit has no actual saved image/source witness");
                }
            }
            if (live_pending_copy) {
                check(receipt.at("error") ==
                              "The recording world changed while its PNG was in flight" &&
                          receipt.at("images").empty() && receipt.at("frames").empty() &&
                          receipt.at("audio_source_start_frame").is_null() &&
                          receipt.at("source_visits").size() == 2 &&
                          play.world_generation() != receipt.at("world_generation"),
                      "An outstanding Game copy was relabeled or blocked gameplay adoption");
                for (const auto& visit : receipt.at("visits"))
                    check(visit.at("audio_frame").is_null(),
                          "A pre-image partial recording invented a normalized movie origin");
            }
            const auto pcm = asset_detail::read_bytes(root / "Movie/audio.s16le", 2 * 1024 * 1024);
            check(std::ranges::any_of(pcm, [](std::byte b) { return b != std::byte{}; }),
                  "Live rendered recording omitted the actual mixer tone");
        }
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
        check(!images.busy() && play.active() == live && !std::filesystem::exists(root / "Movie"),
              "Cancelling options launched Play or created output");
    }
    check(scene.snapshot() == authored && scene.revision() == revision,
          "Native recording altered the authored Scene");
    if (live)
        check(play.launch_id() == original_launch &&
                  play.user_data_override() == original_user_data && !play.paused() && !starts &&
                  !stops && !std::filesystem::exists(root / "Movie/UserData"),
              "Live recording replaced, stopped or redirected the original Play session");
    test::observe_ui = false;
    test::ui_targets.clear();
    staged.clear(play);
    runtime_ui.clear(play);
    if (live)
        play.stop();
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
        const bool live_only = argc == 7 && std::string_view(argv[6]) == "--live-only";
        const bool live_session_only =
            argc == 7 && std::string_view(argv[6]) == "--live-session-only";
        check(argc == 5 || argc == 6 || live_only || live_session_only ||
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
        if (argc >= 6 && !live_only && !live_session_only)
            exercise(presentation, graphics, std::filesystem::absolute(argv[1]),
                     std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                     root / "session", 1, false, 3, std::filesystem::absolute(argv[5]));
        if (argc != 7 || live_only)
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool narrow : {false, true})
                    exercise(presentation, graphics, std::filesystem::absolute(argv[1]),
                             std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                             root / ((narrow ? "live-narrow-" : "live-wide-") +
                                     std::to_string(int(scale * 100))),
                             scale, narrow, scale == 1 ? (narrow ? 5 : 4) : 6);
        if (argc >= 6 && (argc != 7 || live_only))
            exercise(presentation, graphics, std::filesystem::absolute(argv[1]),
                     std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                     root / "live-scene-change", 1, false, 7, std::filesystem::absolute(argv[5]));
        if (argc >= 6 && (argc != 7 || live_only || live_session_only)) {
            exercise(presentation, graphics, std::filesystem::absolute(argv[1]),
                     std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                     root / "live-session", 1, false, 8, std::filesystem::absolute(argv[5]));
            exercise(presentation, graphics, std::filesystem::absolute(argv[1]),
                     std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                     root / "live-pending-copy", 1, false, 9, std::filesystem::absolute(argv[5]));
        }
        std::cout << Json{{"output", path_utf8(root)},
                          {"native_layouts", live_only           ? 9
                                             : live_session_only ? 2
                                             : argc == 7         ? 1
                                             : argc >= 6         ? 16
                                                                 : 12},
                          {"offline_movie_frames", live_only || live_session_only ? 0
                                                   : argc == 7                    ? 4
                                                                                  : 31},
                          {"live_movie_frames", live_session_only        ? 60
                                                : live_only || argc != 7 ? 15
                                                                         : 0},
                          {"live_session_movie_frames",
                           argc >= 6 && (argc != 7 || live_only || live_session_only) ? 60 : 0},
                          {"backend", "software Vulkan"}}
                         .dump()
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
