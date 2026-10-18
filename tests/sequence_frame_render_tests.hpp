#pragma once
#include "editor/animation_timeline.hpp"
#include "frame_renderer.hpp"
#include "view_image_editor_tests.hpp"
#include <array>
namespace forge::test_sequence_frame_render {
using test_view_image_ui::check;
using test_view_image_ui::read;
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                     const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto project_root = std::filesystem::absolute(scratch / AssetId::generate().str());
    std::filesystem::create_directories(project_root.parent_path());
    std::filesystem::copy(gallery, project_root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(project_root);
    project.open_scene("Scenes/70-sequence-audio-cues.scene.json");
    EntityRef controller;
    EntityId front, side;
    const auto snapshot = scene.snapshot();
    for (const auto& row : snapshot.at("entities")) {
        const auto& components = row.at("components");
        if (components.contains("forge.animation_sequence"))
            controller = {scene.asset_id(), row.at("id").get<EntityId>()};
        if (components.contains("forge.camera_shot")) {
            const auto& shot = components.at("forge.camera_shot");
            (shot.at("start_seconds") == 0 ? front : side) =
                shot.at("camera").get<EntityRef>().entity;
        }
    }
    check(bool(controller.entity) && bool(front) && bool(side),
          "Sequence render Gallery source is incomplete");
    AnimationTimelineEditor timeline;
    std::string message;
    timeline.open_sequence(project, scene, controller);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!timeline.frame_export_ready()) {
        timeline.poll(project, scene, false, 0, message);
        check(std::chrono::steady_clock::now() < deadline, "Sequence render clips stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto original = scene.document();
    const auto revision = scene.revision();
    const auto authored_files = [&] {
        std::map<std::filesystem::path, std::vector<unsigned char>> files;
        for (const char* path : {"forge.project.json", "forge.assets.json"})
            files[path] = read(project_root / path);
        for (const char* directory : {"Scenes", "Assets"})
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(project_root / directory))
                if (entry.is_regular_file())
                    files[entry.path().lexically_relative(project_root)] = read(entry.path());
        return files;
    };
    const auto before = authored_files();
    const auto catalog = timeline.frame_export_info().catalog;
    const auto host = std::make_shared<MeshResourceHost>(presentation, graphics, project_root);
    host->catalog(catalog);
    FrameRenderer renderer(presentation);
    renderer.resources(host);
    const ui::SequenceFramePlan plan{0, 2. / 3, 3}; // Exact samples0,1.5,3: includes the cut.
    unsigned width = 384, height = 216;
    const auto submit = [&] {
        host->submit();
        presentation.end_frame(graphics);
        graphics->Flush();
        // The actual editor's swap-chain Present performs these operations.
        // An offscreen Flush alone never retires per-frame dynamic allocations.
        graphics->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    auto render = [&] {
        presentation.begin_frame();
        // Main uses this same owner projection and Scene transform resolution.
        // Resolving an already effective snapshot would leave stale world matrices.
        const auto source = scene.preview_document(timeline.preview(scene.document()));
        auto* image = renderer.game(graphics, source, width, height);
        check(image && renderer.cameras().size() == 1,
              "Sequence preview did not produce one Game camera");
        return image;
    };
    const ui::SequenceFrameOutput profiles[] = {{},
                                                {true, 320, 180},
                                                {true, 192, 256},
                                                {true, 1280, 720},
                                                {true, 1920, 1080},
                                                {true, 1024, 1024}};
    for (unsigned profile = 0; profile < std::size(profiles); ++profile) {
        const auto output = profiles[profile];
        width = output.fixed ? unsigned(output.width) : 384;
        height = output.fixed ? unsigned(output.height) : 216;
        // Independent synchronous display reads from explicit owner samples, before
        // exercising the asynchronous task. This does not share its readback/fence.
        std::array<std::vector<std::uint8_t>, 3> expected;
        const auto lease = timeline.begin_frame_export();
        for (unsigned i = 0; i < expected.size(); ++i) {
            timeline.sample_frame_export(lease, plan.sample(i));
            Diligent::ITextureView* image = nullptr;
            unsigned stable = 0;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (stable < 3) {
                image = render();
                stable = renderer.pending() ? 0 : stable + 1;
                check(std::chrono::steady_clock::now() < deadline,
                      "Sequence render resources stalled");
                submit();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(renderer.diagnostics().empty() &&
                      renderer.cameras().front().entity == (i < 2 ? front : side),
                  "Actual Game frame lost a sequence camera cut or emitted diagnostics");
            test::ViewImageReference reference;
            reference.queue(presentation.device(), graphics, image);
            expected[i] = reference.read(graphics);
            save_view_png(scratch / ("sequence-reference-" + std::to_string(profile) + "-" +
                                     std::to_string(i) + ".png"),
                          width, height, expected[i]);
        }
        timeline.end_frame_export(lease);
        check(expected[0] != expected[1] && expected[1] != expected[2],
              "Actual animation/camera samples produced identical images");

        ui::ViewImageTask task; // Borrowed Timeline and native presentation outlive it.
        const auto folder = scratch / ("frames-" + AssetId::generate().str());
        check(task.begin_sequence(timeline, plan, folder, project_root, scene.asset_id(), message,
                                  output),
              "Actual Timeline frame export did not start");
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        unsigned simulated_window = 0;
        while (task.busy()) {
            timeline.poll(project, scene, false, .25, message);
            task.poll(presentation, graphics, project_root, scene.asset_id(), message);
            if (task.sequence_active() && timeline.preview_active()) {
                if (output.fixed) {
                    const auto requested = task.sequence_extent();
                    check(requested == ui::SequenceFrameExtent{width, height},
                          "Real Timeline lost fixed rendering dimensions");
                    const auto window = simulated_window++;
                    const auto fit = ui::fit_sequence_frame(*requested, window % 2 ? 900.f : 400.f,
                                                            window % 3 ? 350.f : 900.f);
                    check(fit.width > 0 && fit.height > 0 && fit.x >= 0 && fit.y >= 0,
                          "Fixed image could not fit a changing editor preview rectangle");
                }
                auto* image = render();
                task.capture(ui::ViewImageTask::View::Game, presentation, graphics, image, message,
                             !renderer.pending() && renderer.diagnostics().empty());
            }
            submit();
            task.poll(presentation, graphics, project_root, scene.asset_id(), message);
            check(std::chrono::steady_clock::now() < deadline, "Actual Timeline PNG batch stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto receipt = Json::parse(read(folder / "frames.json"));
        check(receipt.at("state") == "complete" && receipt.at("completed_frames") == 3 &&
                  receipt.at("width") == width && receipt.at("height") == height &&
                  receipt.at("size_mode") == (output.fixed ? "fixed" : "current_view"),
              "Actual Timeline frame receipt is incomplete");
        for (unsigned i = 0; i < expected.size(); ++i) {
            const auto bytes = read(folder / plan.filename(i));
            ForgeImagePixels decoded{};
            const bool ok =
                forge_png_decode(bytes.data(), bytes.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                                 FORGE_VIEW_IMAGE_PIXEL_LIMIT, false, &decoded);
            const bool equal = ok && decoded.width == width && decoded.height == height &&
                               decoded.size == expected[i].size() &&
                               std::memcmp(decoded.pixels, expected[i].data(), decoded.size) == 0;
            forge_image_pixels_free(&decoded);
            check(equal, "Actual Timeline PNG differs from independently sampled Game display");
            check(receipt.at("frames").at(i).at("time_seconds") == plan.sample(i),
                  "Actual Timeline receipt remapped explicit sequence times");
        }
    }
    check(scene.document() == original && scene.revision() == revision &&
              !timeline.preview_active() && !presentation.readback_pending() &&
              authored_files() == before &&
              AssetCatalog::open_project(project_root).document() == catalog->document(),
          "Actual Timeline frame export changed authored state/catalog or leaked preview");
}
} // namespace forge::test_sequence_frame_render
