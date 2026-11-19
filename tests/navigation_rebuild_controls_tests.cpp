#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "navigation_rebuild_access.hpp"
#include "navigation_rebuild_controls.hpp"
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
#include <tuple>

using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void run(const std::filesystem::path& scratch, const std::filesystem::path& worker) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root);
    EngineContext authored;
    Scene source(authored.world());
    const auto floor = authoring_command(source, "entity.create",
                                         {{"recipe", "primitive.3"}, {"name", "Walkable floor"}})
                           .at("selected")
                           .get<std::string>();
    source.entity(floor).set<LocalScale>({20, 1, 20}).set<NavigationSurface>({true});
    const auto effective = source.effective_document();
    const auto asset = prepare_navigation(root, effective, {}, worker).publish(effective);
    const auto catalog = AssetCatalog::open_project(root);
    EngineContext engine(WorldRole::Runtime, false, {navigation_module(root, worker)});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    scene.restore_snapshot(source.snapshot());
    const auto saved = scene.snapshot();
    const auto owner = std::static_pointer_cast<NavigationRuntime>(engine.services().navigation());
    for (const auto& [name, width, scale] : {std::tuple{"wide-100", 800, 1.f},
                                             {"narrow-100", 360, 1.f},
                                             {"wide-150", 1000, 1.5f},
                                             {"wide-200", 1200, 2.f}}) {
        const auto token = owner->request_rebuild({asset.id});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (owner->rebuild_state(token).phase <= NavigationRebuildPhase::Building) {
            owner->poll_rebuild();
            check(std::chrono::steady_clock::now() < deadline, "Capture rebuild timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(owner->rebuild_state(token).phase == NavigationRebuildPhase::Applied,
              "Capture requires a real admitted runtime rebuild");
        const auto height = int(310 * scale);
        test::ImGuiSoftwareCapture capture(width, height);
        ui::style(scale);
        ui::NavigationRebuildControls controls;
        bool available = true, pending = false;
        unsigned starts = 0, cancels = 0;
        test::observe_ui = true;
        const auto frame = [&] {
            test::ui_targets.clear();
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({float(width), float(height)}, ImGuiCond_Always);
            ImGui::Begin("Game");
            ImGui::TextUnformatted("PAUSED");
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            const auto request =
                controls.draw(&catalog, scene.asset_id(), NavigationRebuildAccess::state(*owner),
                              available, pending, {});
            if (request) {
                if (request->operation == "start") {
                    ++starts;
                    check(request->asset == asset.id, "Native selector changed logical identity");
                    (void)owner->request_rebuild({request->asset});
                } else {
                    ++cancels;
                    check(owner->cancel_rebuild(request->token), "Native cancel lost its token");
                }
            }
            ImGui::End();
            capture.render();
        };
        test::ImGuiInputDriver driver(frame);
        frame();
        frame();
        for (const auto* key : {"navigation_rebuild:start", "navigation_rebuild:cancel"}) {
            const auto& control = test::ui_targets.at(key);
            check(control.minimum.x >= control.clip_minimum.x &&
                      control.maximum.x <= control.clip_maximum.x &&
                      control.minimum.y >= control.clip_minimum.y &&
                      control.maximum.y <= control.clip_maximum.y,
                  std::string("Navigation control clipped: ") + key + " in " + name);
        }
        capture.save(scratch / (std::string(name) + ".png"));
        driver.click("navigation_rebuild:start");
        frame();
        check(starts == 1 && owner->rebuild_state()->phase == NavigationRebuildPhase::Queued &&
                  !test::ui_targets.at("navigation_rebuild:start").enabled &&
                  test::ui_targets.at("navigation_rebuild:cancel").enabled,
              "Native start/pure observation or active-operation guards failed");
        pending = true;
        frame();
        check(!test::ui_targets.at("navigation_rebuild:cancel").enabled,
              "Pending transport allowed a second native mutation");
        pending = false;
        driver.click("navigation_rebuild:cancel");
        check(cancels == 1 && owner->rebuild_state()->phase == NavigationRebuildPhase::Cancelled,
              "Native queued cancellation did not reach the existing owner");
        available = false;
        frame();
        check(!test::ui_targets.at("navigation_rebuild:start").enabled &&
                  !test::ui_targets.at("navigation_rebuild:cancel").enabled &&
                  scene.snapshot() == saved,
              "Unavailable native controls admitted work or modified authored Scene values");
        available = true;
        const auto retiring = owner->request_rebuild({asset.id});
        owner->poll_rebuild();
        check(owner->cancel_rebuild(retiring), "Building cancellation was not accepted");
        frame();
        check(NavigationRebuildAccess::state(*owner).at("retiring").get<bool>() &&
                  !test::ui_targets.at("navigation_rebuild:start").enabled &&
                  !test::ui_targets.at("navigation_rebuild:cancel").enabled,
              "Terminal worker cleanup admitted another native request or progressed on read");
        const auto cleanup = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (owner->rebuild_pending()) {
            owner->poll_rebuild();
            check(std::chrono::steady_clock::now() < cleanup, "Cancelled worker failed to retire");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
        check(test::ui_targets.at("navigation_rebuild:start").enabled,
              "Completed cleanup did not restore native request availability");
        test::observe_ui = false;
    }
    std::filesystem::remove_all(root);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need capture output and matching native worker");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        run(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
        SDL_Quit();
        std::cout
            << "Four native navigation layouts, real worker observations and input guards passed\n";
        return 0;
    } catch (const std::exception& e) {
        SDL_Quit();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
