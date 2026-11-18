#pragma once
#include "editor/animation_timeline.hpp"
#include "editor/runtime_ui_host.hpp"
#include "frame_renderer.hpp"
#include "view_image_editor_tests.hpp"
#include <RmlUi/Core.h>
#include <array>

namespace forge::test_sequence_ui_render {
using test_view_image_ui::check;
using test_view_image_ui::read;
using Pixels = std::vector<std::uint8_t>;
inline std::array<Pixels, 3>
project_frames(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
               const std::filesystem::path& root, const std::filesystem::path& output,
               const std::filesystem::path& font, const std::filesystem::path& runtime) {
    std::filesystem::create_directories(output);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/151-sequence-ui.scene.json");
    EntityRef controller;
    const auto saved = scene.document();
    for (const auto& row : saved.at("entities")) {
        const auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<AnimationSequence>() && !actor.get<AnimationSequence>().sequence.entity)
            controller = scene.reference(row.at("id").get<std::string>());
    }
    check(bool(controller.entity), "Sequence UI root controller is missing");
    AnimationTimelineEditor timeline;
    timeline.open_sequence(project, scene, controller);
    std::string message;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!timeline.frame_export_ready()) {
        timeline.poll(project, scene, false, 0, message);
        check(std::chrono::steady_clock::now() < deadline, "Sequence UI clips stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto revision = scene.revision();
    const auto saved_source = read(root / "Assets/UI/Sequence/hud.rml");
    const auto saved_catalog = read(root / "forge.assets.json");
    const auto resources = std::make_shared<MeshResourceHost>(presentation, graphics, root);
    resources->catalog(timeline.frame_export_info().catalog);
    FrameRenderer renderer(presentation);
    renderer.resources(resources);
    PlaySession play;
    RuntimeUiHost ui(nullptr, presentation.device(), font);
    ui.resources(resources);
    constexpr unsigned width = 800, height = 600;
    constexpr ui::SequenceFramePlan plan{1, 2, 3};
    const auto submit = [&] {
        resources->submit();
        presentation.end_frame(graphics);
        graphics->Flush();
        graphics->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    // An empty UI observation must preserve the renderer-free path.
    auto empty = Json{{"format", "forge.ui.snapshot"},
                      {"version", 1},
                      {"session", "empty"},
                      {"generation", 1u},
                      {"revision", 1u},
                      {"documents", Json::array()}};
    check(ui.prepare_sequence_frame(play, root, empty, width, height) && !ui.presenter_alive(),
          "A Scene without UI created a native UI presenter");
    const auto render = [&] {
        presentation.begin_frame();
        ui.sync(play, root, true, ui.sequence_preview_active());
        const auto snapshot = timeline.frame_export_ui_snapshot();
        check(!snapshot.is_null(), "Sampled UI snapshot is unavailable");
        const bool ready = ui.prepare_sequence_frame(play, root, snapshot, width, height);
        check(ready, ui.diagnostic().c_str());
        // An unchanged observation is valid while waiting for the rest of the frame.
        check(ui.prepare_sequence_frame(play, root, snapshot, width, height),
              "Repeated sequence observation lost its native frame");
        auto document = scene.preview_document(timeline.preview(scene.document()));
        timeline.project_camera_shots(document);
        RenderTextureOverlay overlay = [&](auto* context, AssetId id, auto* image, unsigned w,
                                           unsigned h) {
            ui.draw_output(play, context, id, image, w, h);
        };
        auto* image = renderer.game(graphics, extract_render_scene(document), width, height, 0,
                                    nullptr, &overlay);
        check(image && ui.draw(play, graphics, image), "Sequence UI composition failed");
        check(ui.diagnostic().empty(), ui.diagnostic().c_str());
        return image;
    };
    const auto lease = timeline.begin_frame_export();
    const auto first = timeline.frame_export_ui_snapshot();
    const auto ui_asset = first.at("documents").at(0).at("asset").get<AssetId>();
    presentation.begin_frame();
    const auto source_preview =
        ui.preview_source(play, root, {ui_asset}, "Assets/UI/Sequence/hud.rml",
                          "<rml><head><style>body{width:100%;height:100%;background-color:#112233;}"
                          "</style></head><body></body></rml>",
                          320, 240, 1, graphics);
    check(!source_preview.at("stale").get<bool>(),
          "Source preview did not prepare before sequence");
    auto* retained = ui.preview_source_image();
    test::ViewImageReference source_reference;
    source_reference.queue(presentation.device(), graphics, retained);
    const auto source_pixels = source_reference.read(graphics);
    check(ui.prepare_sequence_frame(play, root, empty, width, height) &&
              ui.preview_source_image() == retained,
          "An empty sequence UI observation stole the source preview");
    submit();
    std::array<Pixels, 3> expected;
    float pulse_opacity = -1;
    for (unsigned i = 0; i < expected.size(); ++i) {
        timeline.sample_frame_export(lease, plan.sample(i));
        Diligent::ITextureView* image = nullptr;
        unsigned stable = 0;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (stable < 3) {
            image = render();
            stable = renderer.pending() ? 0 : stable + 1;
            submit();
            check(std::chrono::steady_clock::now() < deadline, "Sequence UI resources stalled");
        }
        if (i == 0) {
            test::ViewImageReference retired;
            retired.queue(presentation.device(), graphics, retained);
            check(retired.read(graphics) == source_pixels,
                  "Sequence handoff invalidated the borrowed source image");
            bool rejected = false;
            try {
                ui.preview_source(play, root, {ui_asset}, "Assets/UI/Sequence/hud.rml",
                                  "<rml><body></body></rml>", 320, 240, 1, graphics);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && ui.sequence_preview_active(),
                  "A background source request stole sequence presentation");
        }
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* fill = document->GetElementById("fill");
        auto* pulse = document->GetElementById("pulse");
        const float amount = .2f + .4f * i, expected_width = 80.f + 140.f * i;
        check(std::abs(fill->GetBox().GetSize().x - expected_width) < .05f &&
                  std::abs(fill->GetProperty<float>("opacity") - amount) < .001f &&
                  fill->IsClassSet("bright") == (i != 0),
              "Native width, opacity or class differs from the independently sampled clock");
        const auto pulse_now = pulse->GetProperty<float>("opacity");
        if (pulse_opacity < 0)
            pulse_opacity = pulse_now;
        check(pulse_now == pulse_opacity, "Stopped preview advanced the native CSS clock");
        const auto instance = timeline.frame_export_ui_snapshot()
                                  .at("documents")
                                  .at(0)
                                  .at("instance")
                                  .get<std::string>();
        check(ui.buttons(instance).is_null() && !ui.command(instance, "Pause", {}),
              "Sequence preview exposed live gameplay authority");
        bool rejected = false;
        try {
            ui.press_button(instance, "pause", 1);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && !ui.asset_snapshot(), "Preview published runtime actions or assets");
        test::ViewImageReference reference;
        reference.queue(presentation.device(), graphics, image);
        expected[i] = reference.read(graphics);
        save_view_png(output / ("display-" + std::to_string(i) + ".png"), width, height,
                      expected[i]);
        const auto origin = fill->GetAbsoluteOffset();
        const auto pixel = [&](int x) {
            const auto offset = (std::size_t(int(origin.y) + 16) * width + int(origin.x) + x) * 4;
            return std::array<int, 3>{expected[i].at(offset), expected[i].at(offset + 1),
                                      expected[i].at(offset + 2)};
        };
        const auto foreground = i == 0 ? std::array{72, 182, 213} : std::array{233, 183, 91};
        const auto background = std::array{16, 29, 41};
        const auto color = pixel(20), outside = pixel(int(expected_width) + 8);
        for (unsigned c = 0; c < 3; ++c)
            check(std::abs(color[c] - (foreground[c] * amount + background[c] * (1 - amount))) <=
                          3 &&
                      std::abs(outside[c] - background[c]) <= 2,
                  "Actual sequence UI pixels do not match width/opacity/class composition");
    }
    auto invalid = timeline.frame_export_ui_snapshot();
    invalid["errors"] = Json::array({{{"error", "Sequence fixture resource failure"}}});
    check(!ui.prepare_sequence_frame(play, root, invalid, width, height) &&
              !ui.sequence_frame_ready() && ui.diagnostic() == "Sequence fixture resource failure",
          "A failed UI resource observation remained a ready frame");
    auto output_snapshot = timeline.frame_export_ui_snapshot();
    const auto output_definition =
        Json::parse(read(root / "Assets/RenderTextures/Split.rendertexture.json"));
    const auto output_id = output_definition.at("asset_id").get<AssetId>();
    output_snapshot["version"] = ui_protocol::version;
    output_snapshot["documents"][0]["render_texture"] = output_id;
    if (!ui.prepare_sequence_frame(play, root, output_snapshot, width, height))
        throw std::runtime_error("Sequence output recovery: " + ui.diagnostic());
    Diligent::TextureDesc desc;
    desc.Name = "Stopped sequence UI output routing";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> target;
    presentation.device()->CreateTexture(desc, nullptr, &target);
    check(bool(target), "Sequence output texture allocation failed");
    auto* target_view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    presentation.begin_frame();
    graphics->SetRenderTargets(1, &target_view, nullptr,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float black[]{0, 0, 0, 1};
    graphics->ClearRenderTarget(target_view, black,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ui.draw_output(play, graphics, output_id, target_view, width, height);
    submit();
    test::ViewImageReference output_reference;
    output_reference.queue(presentation.device(), graphics, target_view);
    const auto output_pixels = output_reference.read(graphics);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < output_pixels.size(); i += 4)
        changed += output_pixels[i] != 0 || output_pixels[i + 1] != 0 || output_pixels[i + 2] != 0;
    check(changed > 10000 && ui.diagnostic().empty(),
          "Stopped sequence output routing did not draw native UI pixels");
    timeline.end_frame_export(lease);
    check(expected[0] != expected[1] && expected[1] != expected[2],
          "Distinct sequence UI samples produced identical images");
    ui::ViewImageTask task;
    const auto folder = output / "Frames";
    check(task.begin_sequence(timeline, plan, folder, root, scene.asset_id(), message,
                              {true, int(width), int(height)}),
          "UI frame export did not start");
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    unsigned withheld = 0;
    while (task.busy()) {
        task.poll(presentation, graphics, root, scene.asset_id(), message);
        if (task.sequence_active() && timeline.preview_active()) {
            auto* image = render();
            const bool ready = ++withheld > 4 && !renderer.pending();
            task.capture(ui::ViewImageTask::View::Game, presentation, graphics, image, message,
                         ready);
            if (!ready)
                check(!presentation.readback_pending(), "Pending UI published a partial PNG");
        }
        submit();
        check(std::chrono::steady_clock::now() < deadline, "Sequence UI PNG export stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto receipt = Json::parse(read(folder / "frames.json"));
    check(receipt.at("state") == "complete" && receipt.at("completed_frames") == 3,
          "UI sequence frame receipt is incomplete");
    for (unsigned i = 0; i < expected.size(); ++i) {
        const auto bytes = read(folder / plan.filename(i));
        ForgeImagePixels decoded{};
        const bool ok = forge_png_decode(bytes.data(), bytes.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                                         FORGE_VIEW_IMAGE_PIXEL_LIMIT, false, &decoded);
        const bool equal = ok && decoded.width == width && decoded.height == height &&
                           decoded.size == expected[i].size() &&
                           std::memcmp(decoded.pixels, expected[i].data(), decoded.size) == 0;
        forge_image_pixels_free(&decoded);
        check(equal, "Exported sequence UI PNG differs from independently sampled display");
    }
    if (!runtime.empty()) {
        play.configure(60, InputMap{}, {0, -9.81, 0}, root);
        play.start(path_utf8(runtime), scene.snapshot());
        ui.sync(play, root, true);
        check(!ui.sequence_preview_active(), "Play retained stopped sequence authority");
        const auto wait = [&](auto done) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!done() && play.active() && std::chrono::steady_clock::now() < end) {
                play.pump();
                ui.sync(play, root, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(done(), "Sequence/Play handoff did not reach its native runtime state");
        };
        wait([&] {
            return play.control_ready() && !play.ui_snapshot().is_null() &&
                   !play.ui_snapshot().at("documents").empty();
        });
        ui.prepare_frame(play, {0, 0}, {float(width), float(height)}, width, height);
        const auto instance =
            play.ui_snapshot().at("documents").at(0).at("instance").get<std::string>();
        check(!ui.buttons(instance).is_null() && ui.command(instance, "Pause", {}),
              "Runtime UI authority did not return after stopped preview");
        wait([&] { return play.control_ready() && play.paused(); });
        play.stop(); // Ordinary production Stop; no native-child leak-check claim.
        ui.sync(play, root, true);
        const auto resumed = timeline.begin_frame_export();
        timeline.sample_frame_export(resumed, 2);
        auto* image = render();
        check(image && ui.sequence_frame_ready(), "Sequence UI could not resume after Play");
        submit();
        timeline.end_frame_export(resumed);
    }
    ui.sync(play, root, true);
    check(!ui.presenter_alive() && !ui.sequence_preview_active() && !timeline.preview_active(),
          "Ending sequence preview retained its UI owner");
    const auto other_project = output / "Other Project";
    std::filesystem::create_directories(other_project);
    ui.set_project(other_project, play);
    check(!ui.presenter_alive() && !ui.asset_snapshot() && !ui.preview_source_image(),
          "Project switch retained a UI presentation owner or source authority");
    check(scene.document() == saved && scene.revision() == revision && !project.dirty() &&
              read(root / "Assets/UI/Sequence/hud.rml") == saved_source &&
              read(root / "forge.assets.json") == saved_catalog,
          "Sequence UI presentation changed authored data or history");
    return expected;
}
#include "clip_ui_render_tests.hpp"
inline void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                     const std::filesystem::path& gallery, const std::filesystem::path& output,
                     const std::filesystem::path& font, const std::filesystem::path& runtime = {}) {
    const auto scratch = output / AssetId::generate().str();
    const auto root = scratch / "Project";
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    DiligentPresentation presentation(device);
    const auto authored =
        project_frames(presentation, context, root, scratch / "authored", font, runtime);
    const auto clip = clip_frames(presentation, context, root, scratch / "clip", font);
    const auto relocated = scratch / "Relocated Project";
    std::filesystem::rename(root, relocated);
    const auto moved =
        project_frames(presentation, context, relocated, scratch / "relocated", font, runtime);
    check(authored == moved, "Relocating the project changed stopped UI display/export pixels");
    check(clip == clip_frames(presentation, context, relocated, scratch / "clip-relocated", font),
          "Relocating the project changed stopped clip Game pixels");
    context->WaitForIdle();
}
} // namespace forge::test_sequence_ui_render
