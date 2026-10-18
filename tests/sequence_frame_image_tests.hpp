#pragma once
#include "view_image_editor_tests.hpp"
#include <array>
namespace forge::test_sequence_frame_image {
using test_view_image_ui::check;
using test_view_image_ui::read;
using Task = ui::ViewImageTask;
// Controlled sampling boundary for the real GPU/file scheduler. The actual
// Timeline world, camera cuts and cursor restoration have separate owner tests.
struct Source final : ui::SequenceFrameSource {
    ui::SequenceFrameSourceInfo info;
    Json document{{"authored_sentinel", 123}};
    std::vector<double> samples;
    double cursor = 1.25, saved_cursor = 0;
    bool playing = true, saved_playing = false, held = false, valid = true;
    unsigned begins = 0, ends = 0;
    std::uint64_t token = 0;
    explicit Source(const std::filesystem::path& project) {
        info = {{AssetId::generate(), EntityId::generate()},
                6,
                7,
                11,
                std::make_shared<const AssetCatalog>(project)};
    }
    bool frame_export_ready() const override { return valid && !held; }
    ui::SequenceFrameSourceInfo frame_export_info() const override {
        check(frame_export_ready(), "Controlled sequence source is unavailable");
        return info;
    }
    Json frame_export_document() const override { return document; }
    std::uint64_t begin_frame_export() override {
        check(frame_export_ready(), "Overlapping sequence lease");
        held = true;
        saved_cursor = cursor;
        saved_playing = playing;
        playing = false;
        ++begins;
        return token = ++info.preview_generation;
    }
    bool frame_export_current(std::uint64_t value) const override {
        return valid && held && value == token;
    }
    void sample_frame_export(std::uint64_t value, double seconds) override {
        check(frame_export_current(value), "Stale sequence sample");
        cursor = seconds;
        samples.push_back(seconds);
    }
    void end_frame_export(std::uint64_t value) override {
        if (!held || value != token)
            return;
        held = false;
        ++ends;
        if (valid) {
            cursor = saved_cursor;
            playing = saved_playing;
        }
    }
};
struct Host {
    DiligentPresentation& presentation;
    Diligent::IDeviceContext* graphics;
    std::filesystem::path project;
    Source source;
    std::string message;
    std::array<Diligent::RefCntAutoPtr<Diligent::ITexture>, 3> textures;
    std::array<std::vector<std::uint8_t>, 3> pixels;
    Diligent::RefCntAutoPtr<Diligent::ITexture> resized, unsupported;
    Host(DiligentPresentation& owner, Diligent::IDeviceContext* context,
         const std::filesystem::path& root)
        : presentation(owner), graphics(context),
          project(std::filesystem::absolute(root / AssetId::generate().str())), source(project) {
        std::filesystem::create_directories(project);
        std::ofstream(project / "authored.scene.json") << "authored scene sentinel";
        for (unsigned i = 0; i < textures.size(); ++i) {
            // Nonuniform rows, columns and alpha distinguish orientation, stride,
            // sample order and accidental reuse of an earlier GPU image.
            for (unsigned y = 0; y < 3; ++y)
                for (unsigned x = 0; x < 7; ++x)
                    pixels[i].insert(pixels[i].end(), {static_cast<std::uint8_t>(20 + i * 70 + x),
                                                       static_cast<std::uint8_t>(y * 80 + x),
                                                       static_cast<std::uint8_t>(240 - i * 60 - y),
                                                       static_cast<std::uint8_t>((x + y) * 25)});
            Diligent::TextureDesc desc;
            desc.Name = "FORGE numbered-frame pixel oracle";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = 7;
            desc.Height = 3;
            desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
            Diligent::TextureSubResData row{pixels[i].data(), 7 * 4};
            Diligent::TextureData initial{&row, 1};
            owner.device()->CreateTexture(desc, &initial, &textures[i]);
            check(bool(textures[i]), "Sequence GPU texture unavailable");
            if (!i) {
                desc.Width = 1;
                owner.device()->CreateTexture(desc, &initial, &resized);
                desc.Format = Diligent::TEX_FORMAT_R8_UNORM;
                owner.device()->CreateTexture(desc, &initial, &unsupported);
            }
        }
    }
    auto scene() const { return source.info.controller.scene; }
    auto* view(bool resize = false) {
        auto* texture =
            resize ? resized.RawPtr()
                   : textures.at(std::min(2u, static_cast<unsigned>(source.cursor))).RawPtr();
        return texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }
    void submit() {
        presentation.end_frame(graphics);
        graphics->Flush();
    }
    void step(Task& task, bool resize = false) {
        task.poll(presentation, graphics, project, scene(), message);
        task.capture(Task::View::Game, presentation, graphics, view(resize), message);
        submit();
        task.poll(presentation, graphics, project, scene(), message);
    }
    template <class F> void until(Task& task, F&& done, bool resize = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!done()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Sequence image task completion timed out");
            step(task, resize);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void finish(Task& task, bool resize = false) {
        until(task, [&] { return !task.busy(); }, resize);
        check(!presentation.readback_pending() && !source.held,
              "Sequence image task retained native/source ownership");
    }
    void admitted(Task& task) {
        until(task, [&] { return source.held; });
    }
    void start(Task& task, const std::filesystem::path& folder,
               ui::SequenceFramePlan plan = {0, 1, 3}) {
        check(task.begin_sequence(source, plan, folder, project, scene(), message),
              "Sequence export did not start");
    }
    Json receipt(const std::filesystem::path& folder, const char* state, unsigned count) {
        const auto bytes = read(folder / "frames.json");
        auto value = Json::parse(bytes);
        check(value.at("state") == state && value.at("completed_frames") == count &&
                  value.at("frames").size() == count && value.at("scene") == scene().str() &&
                  value.at("sequence") == source.info.controller.entity.str() &&
                  !value.at("source_digest").get<std::string>().empty() &&
                  !value.at("catalog_digest").get<std::string>().empty(),
              "Sequence receipt lost completion/source identity");
        return value;
    }
    void exact(const std::filesystem::path& path, unsigned sample) {
        const auto bytes = read(path);
        ForgeImagePixels image{};
        const bool decoded =
            forge_png_decode(bytes.data(), bytes.size(), 4096, 1024, false, &image);
        const bool equal = decoded && image.width == 7 && image.height == 3 &&
                           image.size == pixels[sample].size() &&
                           std::memcmp(image.pixels, pixels[sample].data(), image.size) == 0;
        forge_image_pixels_free(&image);
        check(equal, "Numbered PNG differs from its independent sample pixel oracle");
    }
};
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                     const std::filesystem::path& root) {
    Host host(presentation, graphics, root);
    Task task;
    const auto authored = host.source.document;
    const auto catalog = host.source.info.catalog->document();
    const auto output = host.project / "complete";
    host.start(task, output);
    check(!task.selected_path(Task::View::Scene, host.project / "busy.png", host.project,
                              host.scene(), host.message),
          "Single image interrupted a frame batch");
    host.finish(task);
    const auto receipt = host.receipt(output, "complete", 3);
    check(receipt.at("width") == 7 && receipt.at("height") == 3 &&
              host.source.samples == std::vector<double>({0, 1, 2}) && host.source.cursor == 1.25 &&
              host.source.playing && host.source.begins == 1 && host.source.ends == 1,
          "Sequence batch changed sample order or failed to restore the source");
    for (unsigned i = 0; i < 3; ++i) {
        const auto name = ui::SequenceFramePlan{0, 1, 3}.filename(i);
        check(receipt.at("frames").at(i).at("file") == name &&
                  receipt.at("frames").at(i).at("time_seconds") == i,
              "Receipt sample times/filenames differ");
        host.exact(output / name, i);
    }
    check(!std::filesystem::exists(output / "frame-000003.png"), "Batch wrote an extra frame");
    const auto repeated = host.project / "repeat";
    host.start(task, repeated);
    host.finish(task);
    for (unsigned i = 0; i < 3; ++i) {
        const auto name = ui::SequenceFramePlan{0, 1, 3}.filename(i);
        check(read(output / name) == read(repeated / name), "Repeated fixed samples differ");
    }
    const auto previous = read(output / "frames.json");
    const auto begins = host.source.begins;
    host.start(task, output);
    host.finish(task);
    check(host.message.starts_with("Frame export failed:") &&
              read(output / "frames.json") == previous && host.source.begins == begins,
          "Existing output folder was replaced or sampled");
    check(!task.begin_sequence(host.source, {0, 0, 3}, host.project / "invalid", host.project,
                               host.scene(), host.message) &&
              !std::filesystem::exists(host.project / "invalid"),
          "Invalid settings wrote an output folder");
    host.start(task, host.project / "missing" / "parent");
    host.finish(task);
    check(host.message.starts_with("Frame export failed:") && host.source.begins == begins,
          "Folder creation failure acquired a preview lease");
    const auto single = host.project / "single";
    host.start(task, single, {1.5, 24, 1});
    host.finish(task);
    check(host.receipt(single, "complete", 1).at("frames").at(0).at("time_seconds") == 1.5,
          "One-frame explicit start was remapped");
    host.exact(single / "frame-000000.png", 1);

    const auto pending = host.project / "cancel-gpu";
    host.start(task, pending);
    const auto sample_count = host.source.samples.size();
    const auto admission_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (host.source.samples.size() == sample_count) {
        check(std::chrono::steady_clock::now() < admission_deadline,
              "Sequence sample admission timed out");
        task.poll(presentation, graphics, host.project, host.scene(), host.message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (bool ready : {true, true, false, true, true})
        task.capture(Task::View::Game, presentation, graphics, host.view(), host.message, ready);
    check(!presentation.readback_pending(), "Unready sample bypassed three stable renders");
    task.capture(Task::View::Game, presentation, graphics, host.view(), host.message);
    check(presentation.readback_pending(), "Ready sequence sample did not queue a GPU copy");
    task.cancel_sequence(host.message);
    task.poll(presentation, graphics, host.project, host.scene(), host.message);
    check(presentation.readback_pending(), "Cancelled GPU resources retired before submission");
    host.finish(task);
    host.receipt(pending, "cancelled", 0);
    check(!std::filesystem::exists(pending / "frame-000000.png"),
          "Cancelled GPU sample was published");

    const auto partial = host.project / "partial";
    host.start(task, partial);
    host.until(task, [&] { return std::filesystem::exists(partial / "frame-000000.png"); });
    task.cancel_sequence(host.message);
    host.finish(task);
    host.receipt(partial, "cancelled", 1);
    host.exact(partial / "frame-000000.png", 0);
    check(!std::filesystem::exists(partial / "frame-000001.png"), "Cancel admitted a later sample");

    const auto resize = host.project / "resize";
    host.start(task, resize);
    host.until(task, [&] { return std::filesystem::exists(resize / "frame-000000.png"); });
    host.finish(task, true);
    host.receipt(resize, "failed", 1);
    host.exact(resize / "frame-000000.png", 0);
    check(!std::filesystem::exists(resize / "frame-000001.png"), "Batch mixed frame dimensions");

    const auto blocked = host.project / "blocked-file";
    host.start(task, blocked);
    host.until(task, [&] { return std::filesystem::exists(blocked / "frame-000000.png"); });
    std::filesystem::create_directory(blocked / "frame-000001.png");
    std::ofstream(blocked / "frame-000001.png" / "keep") << "keep";
    host.finish(task);
    host.receipt(blocked, "failed", 1);
    host.exact(blocked / "frame-000000.png", 0);
    check(read(blocked / "frame-000001.png" / "keep") ==
              std::vector<unsigned char>({'k', 'e', 'e', 'p'}),
          "IO failure changed an existing destination");

    const auto changed = host.project / "changed-project";
    host.start(task, changed);
    task.poll(presentation, graphics, host.project, AssetId::generate(), host.message);
    host.finish(task);
    host.receipt(changed, "cancelled", 0);
    const auto reopened = host.project / "reopened";
    const auto before_reopen = host.source.begins;
    host.start(task, reopened);
    ++host.source.info.preview_generation;
    host.finish(task);
    host.receipt(reopened, "cancelled", 0);
    check(host.source.begins == before_reopen, "Late preparation acquired a reopened preview");

    const auto stale = host.project / "stale-source";
    host.start(task, stale);
    host.admitted(task);
    host.source.valid = false;
    host.source.cursor = 4.5; // Represents a replacement context's external state.
    host.source.playing = false;
    host.finish(task);
    host.receipt(stale, "cancelled", 0);
    check(host.source.cursor == 4.5 && !host.source.playing,
          "Stale capture restored into a replacement context");
    host.source.valid = true;
    host.source.cursor = 1.25;
    host.source.playing = true;

    const auto unavailable = host.project / "unavailable";
    host.start(task, unavailable);
    host.admitted(task);
    task.unavailable(Task::View::Game, host.message);
    host.finish(task);
    host.receipt(unavailable, "cancelled", 0);

    const auto bad_gpu = host.project / "unsupported-target";
    host.start(task, bad_gpu);
    host.admitted(task);
    auto* unsupported = host.unsupported->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    for (unsigned i = 0; i < 3; ++i)
        task.capture(Task::View::Game, presentation, graphics, unsupported, host.message);
    host.finish(task);
    host.receipt(bad_gpu, "failed", 0);

    const auto shutdown_prepare = host.project / "shutdown-prepare";
    {
        Task closing;
        host.start(closing, shutdown_prepare);
    }
    host.receipt(shutdown_prepare, "cancelled", 0);
    const auto shutdown_gpu = host.project / "shutdown-gpu";
    {
        Task closing;
        host.start(closing, shutdown_gpu);
        host.admitted(closing);
        for (unsigned i = 0; i < 3; ++i)
            closing.capture(Task::View::Game, presentation, graphics, host.view(), host.message);
        check(presentation.readback_pending(), "Shutdown case did not own a GPU readback");
    }
    host.receipt(shutdown_gpu, "cancelled", 0);
    check(!host.source.held && host.source.cursor == 1.25 && host.source.playing &&
              presentation.readback_pending(),
          "Shutdown failed to restore preview or retired GPU resources prematurely");
    // The standalone test presentation outlives the task, just as at editor
    // shutdown. Explicitly submit/retire its native ticket before the next case.
    presentation.cancel_readback();
    host.submit();
    const auto retirement = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (presentation.readback_pending()) {
        (void)presentation.take_readback(graphics);
        check(std::chrono::steady_clock::now() < retirement, "Shutdown ticket did not retire");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(host.source.document == authored && host.source.info.catalog->document() == catalog &&
              read(host.project / "authored.scene.json") ==
                  std::vector<unsigned char>({'a', 'u', 't', 'h', 'o', 'r', 'e', 'd',
                                              ' ', 's', 'c', 'e', 'n', 'e', ' ', 's',
                                              'e', 'n', 't', 'i', 'n', 'e', 'l'}) &&
              host.source.cursor == 1.25 && host.source.playing,
          "Frame exports changed authored/source/catalog state");
}
inline void exercise_ui(DiligentPresentation& presentation, Diligent::IDeviceContext* graphics,
                        const std::filesystem::path& root, float scale, ImVec2 display) {
    Host host(presentation, graphics, root);
    auto* previous_context = ImGui::GetCurrentContext();
    const float previous_scale = ui::interface_scale;
    const bool previous_observe = test::observe_ui;
    ImGui::CreateContext();
    struct Restore {
        ImGuiContext* context;
        float scale;
        bool observe;
        ~Restore() {
            ImGui::DestroyContext();
            ImGui::SetCurrentContext(context);
            ui::interface_scale = scale;
            test::observe_ui = observe;
            test::ui_targets.clear();
        }
    } restore{previous_context, previous_scale, previous_observe};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext editor;
    ui::ContextScope scope(editor);
    test::observe_ui = true;
    Task task;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);
        ImGui::Begin("Sequence image export fixture");
        task.draw(host.message);
        ImGui::End();
        ImGui::Render();
        host.step(task);
    };
    auto visible = [&](const test::UiTarget& box) {
        return box.minimum.x >= 0 && box.minimum.y >= 0 && box.maximum.x <= display.x &&
               box.maximum.y <= display.y && box.minimum.x >= box.clip_minimum.x &&
               box.minimum.y >= box.clip_minimum.y && box.maximum.x <= box.clip_maximum.x &&
               box.maximum.y <= box.clip_maximum.y;
    };
    auto target = [&](const std::string& name) {
        frame();
        frame();
        for (unsigned scroll = 0; scroll < 80; ++scroll) {
            check(test::ui_targets.contains(name), "Frame export control is missing");
            const auto box = test::ui_targets.at(name);
            if (visible(box)) {
                check(box.enabled, "Frame export control is unexpectedly disabled");
                return ImVec2{(box.minimum.x + box.maximum.x) / 2,
                              (box.minimum.y + box.maximum.y) / 2};
            }
            // Real wheel input in the settings child's clip rectangle.
            io.AddMousePosEvent((box.clip_minimum.x + box.clip_maximum.x) / 2,
                                (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? .25f : -.25f);
            frame();
        }
        const auto box = test::ui_targets.at(name);
        throw std::runtime_error(
            "Frame export control is clipped: " + name + " rect " + std::to_string(box.minimum.x) +
            "," + std::to_string(box.minimum.y) + ".." + std::to_string(box.maximum.x) + "," +
            std::to_string(box.maximum.y) + " clip " + std::to_string(box.clip_minimum.x) + "," +
            std::to_string(box.clip_minimum.y) + ".." + std::to_string(box.clip_maximum.x) + "," +
            std::to_string(box.clip_maximum.y));
    };
    auto click = [&](const std::string& name) {
        const auto point = target(name);
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto type = [&](const char* name, const char* value) {
        click(name);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    auto choose = [&] {
        check(task.choose_sequence(host.source, nullptr, host.project, host.scene(), host.message),
              "Sequence configuration did not open");
        for (unsigned i = 0; i < 6; ++i)
            frame();
    };
    choose();
    click("frame-export:cancel");
    host.finish(task);
    check(host.source.cursor == 1.25 && host.source.playing &&
              !std::filesystem::exists(host.project / "SequenceFrames"),
          "Configuration cancel changed the preview or wrote files");
    frame();
    choose();
    type("frame-export:count", "3");
    type("frame-export:fps", "1");
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SequenceFramesParent, path_utf8(host.project), {}, {}};
    const auto deliveries = FileDialog::fixture_deliveries;
    click("frame-export:browse");
    check(FileDialog::fixture_deliveries == deliveries + 1,
          "Frame folder picker did not deliver its injected selection");
    click("frame-export:start");
    host.finish(task);
    host.receipt(host.project / "SequenceFrames", "complete", 3);
    for (unsigned i = 0; i < 3; ++i)
        host.exact(host.project / "SequenceFrames" / ui::SequenceFramePlan{0, 1, 3}.filename(i), i);
    frame();
    choose();
    FileDialog::fixture_selection =
        FileDialog::Result{FileDialog::Kind::SequenceFramesParent, path_utf8(host.project), {}, {}};
    click("frame-export:browse");
    type("frame-export:name", "CancelledFrames");
    click("frame-export:start");
    click("frame-export:cancel");
    host.finish(task);
    const auto bytes = read(host.project / "CancelledFrames" / "frames.json");
    const auto cancelled = Json::parse(bytes);
    check(cancelled.at("state") == "cancelled" && cancelled.at("completed_frames") < 180 &&
              host.source.cursor == 1.25 && host.source.playing,
          "Progress cancel failed to stop admission and restore preview");
    check(!editor.log.empty() && editor.log.back().find("Image export:") != std::string::npos,
          "Frame export did not record timestamped editor feedback");
}
} // namespace forge::test_sequence_frame_image
