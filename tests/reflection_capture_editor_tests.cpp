#include "editor/capture_automation.hpp"
#include "editor/component_inspector.hpp"
#include "editor/reflection_capture_task.hpp"
#include "imgui_software_capture.hpp"
#include "reflection_capture_recording.hpp"
#include "texture_authoring.hpp"
#include <forge/engine_assets.hpp>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& worker, const std::filesystem::path& scratch,
              float scale) {
    const auto root = scratch / AssetId::generate().str();
    SceneDocument::create_project(root, "Reflection capture");
    EngineContext engine;
    Scene scene(engine.world());
    std::vector<std::string> recent;
    EditorFiles files(scene, nullptr, recent);
    files.start(root);
    const auto probe =
        authoring_command(scene, "entity.create", {{"recipe", "render.reflection_probe"}})
            .at("selected")
            .get<std::string>();
    const auto set = [&](const char* field, Json value) {
        authoring_command(scene, "property.set",
                          {{"entity", probe},
                           {"component", "forge.reflection_probe"},
                           {"field", field},
                           {"value", value}});
    };
    set("rotation_degrees", 45.);
    files.document.save();
    const auto before = scene.document();
    test::ImGuiSoftwareCapture capture(int(1080 * scale), int(900 * scale));
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    SourceImport source;
    const auto registry = texture_import_registry(worker);
    const AssetReimportRoute route{"forge.texture.container", desktop_texture_target(), registry,
                                   [](auto& candidate, const auto& plan, const auto&) {
                                       prepare_texture_publication(candidate, plan);
                                   }};
    source.routes = [route] { return std::vector{route}; };
    ContentImports imports;
    imports.routes = source.routes;
    auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    source.published = [&](AssetId, auto value) { catalog = std::move(value); };
    std::string message;
    ui::ViewImageTask image;
    unsigned jobs = 0;
    const auto start = [&](const std::string& entity, ui::ReflectionCaptureRequest settings,
                           bool configure) {
        auto task = std::make_unique<ui::ReflectionCaptureTask>(
            scene, files.document, source, entity, settings,
            [&](RenderScene snapshot, const auto& options, EntityId id) {
                check(snapshot.scene == scene.asset_id() && id.str() == probe &&
                          options.position == Double3{0, 1, 0},
                      "Capture did not use the authored probe scope");
                ++jobs;
                return std::make_unique<test::RecordingReflectionCapture>(options.size);
            },
            [&] { return catalog; }, [] { return std::string{}; }, configure);
        task->reveal(configure);
        check(image.begin_work(std::move(task), root, scene.asset_id(), message),
              "Image owner rejected the ready reflection operation");
    };
    ComponentInspector inspector;
    inspector.capture_reflection = [&](const std::string& entity) {
        ui::ReflectionCaptureRequest settings;
        settings.settings.size = 16;
        start(entity, settings, true);
    };
    auto frame = [&] {
        image.poll_work(nullptr, message);
        imports.suspend(source.busy());
        source.poll(files.document, imports, message);
        imports.poll(files.document, message);
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - 20, io.DisplaySize.y - 20}, ImGuiCond_Always);
        ImGui::Begin("Inspector");
        inspector.draw(scene, files.document, probe);
        ImGui::End();
        image.draw(message);
        source.draw(false);
        capture.render();
    };
    const auto wait = [&](auto done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        while (!done()) {
            frame();
            check(std::chrono::steady_clock::now() < deadline,
                  message + " " + source.operation().dump());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
    };
    const auto click = [&](const std::string& target) {
        frame();
        check(test::ui_targets.contains(target), "Missing capture control: " + target);
        const auto box = test::ui_targets.at(target);
        const float left = std::max(box.minimum.x, box.clip_minimum.x),
                    right = std::min(box.maximum.x, box.clip_maximum.x),
                    top = std::max(box.minimum.y, box.clip_minimum.y),
                    bottom = std::min(box.maximum.y, box.clip_maximum.y);
        check(box.enabled && right > left && bottom > top,
              "Disabled/clipped capture control: " + target);
        io.AddMousePosEvent((left + right) * .5f, (top + bottom) * .5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    click("reflection-capture:open");
    check(jobs == 0 && image.busy() && scene.document() == before,
          "Opening reflection settings started a job or changed the Scene");
    frame();
    if (scale == 1)
        capture.save(scratch / "reflection-capture-settings.png");
    click("reflection-capture:start");
    check(jobs == 1 && image.reflection_active() &&
              !image.selected_path(ui::ViewImageTask::View::Scene, root / "wrong.png", root,
                                   scene.asset_id(), message),
          "Reflection capture admitted a parallel PNG operation; jobs=" + std::to_string(jobs) +
              " state=" + CaptureAutomation::state(image, files).dump());
    wait([&] { return source.review_ready(); });
    check(!std::filesystem::exists(root / "Assets/ReflectionMap") && scene.document() == before,
          "Rendering/review changed files or the original map reference");
    if (scale == 1)
        capture.save(scratch / "reflection-capture-review.png");
    auto review = source.operation();
    check(review.at("generated").at("format") == "forge.reflection-capture" &&
              review.at("generated").at("map_rotation_degrees") == 0 &&
              CaptureAutomation::state(image, files).at("reflection").at("import_batch") ==
                  review.at("batch"),
          "Capture did not expose the existing generated-source review scope");
    source.automation_commit(review.at("batch"), review.at("review"), true);
    wait([&] { return !image.busy(); });
    auto status = CaptureAutomation::state(image, files);
    check(status.at("reflection").at("assigned") == true && source.published_count() == 1,
          "Captured map was not published/assigned: " + status.dump());
    const auto id = status.at("reflection").at("asset").get<AssetId>();
    const auto value = scene.entity(probe).get<ReflectionProbe>();
    check(value.texture.id == id && value.rotation_degrees == 0 && files.document.dirty(),
          "Map assignment lost the typed reference, yaw reset or save state");
    const auto assigned = scene.document();
    check(scene.undo() && scene.document() == before && scene.redo() &&
              scene.document() == assigned,
          "Map/yaw assignment did not use one ordinary Scene Undo step");
    files.document.save();
    files.document.open_scene(files.document.path());
    check(scene.document() == assigned, "Captured map did not survive save/reopen");
    source.automation_close(review.at("batch"));
    frame();
    const auto records = catalog->document().at("assets");
    ui::ReflectionCaptureRequest settings;
    settings.settings.size = 16;
    settings.destination = "Assets/StaleSceneMap";
    start(probe, settings, false);
    wait([&] { return source.review_ready(); });
    review = source.operation();
    set("priority", 7);
    const auto edited = scene.document();
    source.automation_commit(review.at("batch"), review.at("review"), true);
    wait([&] { return !image.busy(); });
    status = CaptureAutomation::state(image, files);
    check(status.at("reflection").at("state") == "unassigned" &&
              status.at("reflection").contains("asset") && scene.document() == edited &&
              scene.entity(probe).get<ReflectionProbe>().texture.id == id,
          "A changed Scene revision was silently rebased during publication");
    const auto current_records = catalog->document().at("assets");
    for (const auto& record : records)
        check(std::find(current_records.begin(), current_records.end(), record) !=
                  current_records.end(),
              "New-map capture rewrote a prior asset record");
    source.automation_close(review.at("batch"));
    frame();
    settings.destination = "Assets/CancelledMap";
    start(probe, settings, false);
    frame();
    check(image.cancel(message) && image.busy(),
          "Cancellation did not retain draining capture ownership");
    wait([&] { return !image.busy(); });
    check(scene.document() == edited && !std::filesystem::exists(root / settings.destination),
          "Cancelled capture changed the authored Scene or published a folder");
    CaptureAutomationProviders providers;
    providers.reflection = [&](const auto& entity, const auto& request) {
        start(entity, request, false);
    };
    ui::AutomationPermissions permissions;
    const auto discover = [&] {
        return CaptureAutomation::methods(image, scene, files, providers, message, permissions);
    };
    const auto contains = [](const auto& methods, const auto& name) {
        return std::any_of(methods.begin(), methods.end(),
                           [&](const auto& item) { return item.name == name; });
    };
    check(!contains(discover(), "capture.reflection.start"),
          "Read-only discovery exposed capture publication");
    permissions.export_game = true;
    check(!contains(discover(), "capture.cancel") && !contains(discover(), "capture.start"),
          "An empty output grant exposed output capture controls");
    permissions.export_game = false;
    permissions.assets = true;
    const auto methods = discover();
    check(contains(methods, "capture.reflection.start") && contains(methods, "capture.cancel") &&
              !contains(methods, "capture.start"),
          "Project asset grant depended on an unrelated PNG output grant");
    const auto denied_cancel = [&](const auto& entries) {
        const auto cancel = std::find_if(entries.begin(), entries.end(), [](const auto& item) {
            return item.name == "capture.cancel";
        });
        check(cancel != entries.end(), "Granted connection omitted guarded cancellation");
        bool denied = false;
        try {
            cancel->invoke({{"operation", CaptureAutomation::state(image, files).at("operation")}});
        } catch (const AuthoringError& error) {
            denied = error.code == "capability_denied";
        }
        check(denied && image.busy(), "Capture cancellation bypassed its own workflow permission");
    };
    settings.destination = "Assets/PermissionMap";
    start(probe, settings, false);
    ui::AutomationPermissions output_only;
    output_only.export_game = true;
    output_only.export_root = scratch;
    denied_cancel(CaptureAutomation::methods(image, scene, files, providers, message, output_only));
    check(image.cancel(message), "The UI could not cancel its reflection task");
    wait([&] { return !image.busy(); });
    check(image.selected_path(ui::ViewImageTask::View::Scene, root / "permission.png", root,
                              scene.asset_id(), message),
          "The image owner retained a cancelled task");
    denied_cancel(methods); // An asset grant cannot cancel an output-only PNG operation.
    check(image.cancel(message) && !image.busy(), "The UI could not cancel its pending PNG");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need asset worker and scratch directory");
        const auto scratch = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(scratch);
        for (const auto scale : {1.f, 1.5f, 2.f})
            exercise(std::filesystem::absolute(argv[1]), scratch, scale);
        std::cout << "Reflection capture actual controls, import/publication, scope, assignment, "
                     "Undo/Redo/save, cancellation and permissions passed at three scales; GPU "
                     "delivery recorded\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
