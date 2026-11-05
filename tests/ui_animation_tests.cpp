#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include "ui_test_renderer.hpp"
#include <forge/runtime.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read_json(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}
void present(const std::filesystem::path& root, const Json& source,
             const std::vector<std::byte>& font, bool reload) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto authored = scene.snapshot();
    auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    RuntimeClock clock;
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("ui-motion", 1);
        const auto snapshot = [&] {
            return ui->snapshot(scene, "ui-motion", 1, clock.tick(), clock.paused());
        };
        check(presenter.accept(snapshot()), presenter.diagnostic().c_str());
        auto* doc = Rml::GetContext(0)->GetDocument(0);
        auto* fill = doc->GetElementById("fill");
        auto* marker = doc->GetElementById("marker");
        auto* state = doc->GetElementById("state");
        check(fill && marker && state, "Animated Gallery elements missing");
        double now = 0;
        const auto frame = [&] {
            now += .05;
            clock.advance(RuntimeClock::Time{} +
                              std::chrono::duration_cast<RuntimeClock::Clock::duration>(
                                  std::chrono::duration<double>(now)),
                          [](float) { throw std::runtime_error("Held gameplay advanced"); });
            check(presenter.accept(snapshot()), "Repeated copied UI snapshot rejected");
            presenter.update(now, 420, 640);
            presenter.render();
        };
        const auto progress = [&](int frames) {
            for (int i = 0; i < frames; ++i)
                frame();
        };
        const auto close = [](float actual, float expected) {
            check(std::abs(actual - expected) < .02f, "Native UI property progression differs");
        };
        progress(10);
        close(fill->GetProperty<float>("width"), 140);
        close(fill->GetProperty<float>("opacity"), .6f);
        const auto midpoint = fill->GetProperty<float>("width");
        presenter.update(now, 420, 640);
        close(fill->GetProperty<float>("width"), midpoint);
        progress(10);
        close(fill->GetProperty<float>("width"), 240);
        close(fill->GetProperty<float>("opacity"), 1);
        progress(10);
        close(fill->GetProperty<float>("width"), 140);
        close(fill->GetProperty<float>("opacity"), .6f);
        check(clock.paused() && clock.tick() == 0 && state->IsClassSet("paused"),
              "Presentation animation changed paused gameplay");
        const auto p = doc->GetElementById("panel")->GetAbsoluteOffset();
        const auto size = doc->GetElementById("panel")->GetBox().GetSize(Rml::BoxArea::Border);
        check(p.x >= 0 && p.y >= 0 && p.x + size.x <= 420 && p.y + size.y <= 640,
              "Animated panel escaped portrait viewport");

        ui_protocol::CommandGate gate;
        gate.reset("ui-motion", 1);
        auto* button = doc->GetElementById("resume");
        const auto position = button->GetAbsoluteOffset();
        presenter.mouse_move(int(position.x + 8), int(position.y + 8), 0);
        presenter.mouse_button(0, true, 0);
        presenter.mouse_button(0, false, 0);
        const auto command = presenter.pending_command();
        if (!command) {
            const auto box = button->GetBox().GetSize(Rml::BoxArea::Border);
            std::cerr << "Resume button " << position.x << ',' << position.y << " size " << box.x
                      << ',' << box.y << " visible " << button->IsVisible(true) << '\n';
        }
        check(command && command->at("command") == "Resume", "Animated UI lost pointer command");
        const auto ack = gate.dispatch(*command, [&](const Json& c) {
            ui->command(scene, c, [&](const std::string& action) {
                check(action == "Resume", "Unexpected UI control");
                clock.set_time_scale(0);
                clock.resume(RuntimeClock::Time{});
            });
        });
        check(ack.at("ok"), "Animated UI control rejected");
        presenter.acknowledge(ack);
        frame();
        check(!state->IsClassSet("paused"), "Copied state did not change native class");
        const auto start_red = state->GetProperty<Rml::Colourb>("background-color").red;
        progress(4);
        const auto middle_red = state->GetProperty<Rml::Colourb>("background-color").red;
        check(middle_red < start_red && middle_red > 40,
              "State color transition did not interpolate");
        progress(5);
        check(state->GetProperty<Rml::Colourb>("background-color").red == 40 &&
                  button->GetProperty<Rml::Colourb>("background-color").red == 89,
              "State/hover transition did not reach target");
        check(!clock.paused() && clock.time_scale() == 0 && clock.tick() == 0,
              "Zero gameplay rate lost independent UI motion");
        check(fill->GetProperty<float>("width") > 40, "UI keyframes stopped at zero gameplay rate");
        check(marker->GetProperty("transform") && renderer.transforms > 0,
              "Animated transform did not reach the rendering adapter");

        if (reload) {
            const auto path = root / "Assets/UI/Motion/hud.rcss";
            const auto bytes = asset_detail::read_bytes(path, 256 * 1024);
            const std::string original(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            const auto previous = presenter.presentation_revision();
            const auto sources = presenter.asset_snapshot()->sources;
            for (const auto* invalid :
                 {"#fill {transition:opacity 0.4s linear;}",
                  "#fill {animation:1s linear-in bad;} @keyframes bad {from "
                  "{box-shadow:#0008 2px 2px;} to {box-shadow:#0008 8px 8px;}}",
                  "@keyframes broken {from {width:40px;}"}) {
                write(path, original + invalid);
                if (presenter.reload() || presenter.diagnostic().empty())
                    throw std::runtime_error(
                        std::string("Invalid animated replacement published: ") + invalid);
                check(presenter.presentation_revision() == previous &&
                          Rml::GetContext(0)->GetDocument(0) == doc &&
                          presenter.asset_snapshot()->sources == sources,
                      "Failed animation reload replaced usable UI/resources");
                const auto before = fill->GetProperty<float>("width");
                frame();
                check(fill->GetProperty<float>("width") != before,
                      "Failed reload stopped last-good keyframes");
            }
            write(path, original);
            check(presenter.reload() && presenter.presentation_revision() == previous + 1 &&
                      presenter.diagnostic().empty(),
                  "Valid animation reload did not publish");
            check(Rml::GetContext(0)->GetDocument(0) != doc && clock.tick() == 0,
                  "Reload retained old DOM or changed gameplay");
            progress(10);
            close(Rml::GetContext(0)->GetDocument(0)->GetElementById("fill")->GetProperty<float>(
                      "width"),
                  140);
        }
        presenter.update(now, 1200, 900, 1.5f);
        presenter.render();
        check(renderer.draws > 0 && presenter.diagnostic().empty() && scene.snapshot() == authored,
              "UI animation changed authored state or produced no valid rendering");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(), "Animated UI leaked resources");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch directory and font");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(scratch);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{scratch};
        const auto root = scratch / "source";
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto scene_path = "Scenes/100-ui-animation.scene.json";
        const auto source = read_json(root / scene_path);
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        present(root, source, font, true);
        RuntimeUiInspector inspect = [&](AssetId id, std::stop_token) {
            UiTestRenderer renderer;
            UiPresenter presenter(renderer, root, font);
            return presenter.inspect_static({id});
        };
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease writer(root);
            prepare_runtime_content_catalog(writer, roots, inspect);
        }
        const auto destination = scratch / "export";
        package_runtime_content(root, destination, roots, {"windows", "d3d12"}, {}, {}, {},
                                inspect);
        const auto moved = scratch / "relocated";
        std::filesystem::rename(destination, moved);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(catalog.records().size() == 3, "Animated UI closure lost/duplicated content");
        check(read_json(root / scene_path) == source, "UI export modified the authored Scene");
        std::filesystem::rename(root, scratch / "original-offline");
        present(moved, read_json(moved / catalog.records().at(roots[0]).source), font, false);
        std::cout << "Native UI keyframes, transitions, paused/zero gameplay, safe reload and "
                     "relocated content passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
