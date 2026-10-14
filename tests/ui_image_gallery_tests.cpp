#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include "ui_test_renderer.hpp"
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
void present(const std::filesystem::path& root, const Json& source,
             const std::vector<std::byte>& font) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("gallery59", 1);
        check(presenter.accept(ui->snapshot(scene, "gallery59", 1, 0, false)),
              "Gallery59 native UI admission failed");
        ui_protocol::CommandGate gate;
        gate.reset("gallery59", 1);
        double now = 0;
        for (const auto size : {std::pair{420, 640}, std::pair{1200, 900}}) {
            presenter.update(++now, size.first, size.second);
            presenter.render();
            auto* panel = Rml::GetContext(0)->GetDocument(0)->GetElementById("panel");
            check(panel != nullptr, "Gallery59 panel missing");
            const auto panel_position = panel->GetAbsoluteOffset();
            const auto panel_size = panel->GetBox().GetSize(Rml::BoxArea::Border);
            check(panel_position.x >= 0 && panel_position.y >= 0 &&
                      panel_position.x + panel_size.x <= size.first &&
                      panel_position.y + panel_size.y <= size.second,
                  "Gallery59 panel padding/border escaped the viewport");
            for (const auto* name : {"pause", "resume", "step"}) {
                auto* button = Rml::GetContext(0)->GetDocument(0)->GetElementById(name);
                check(button && button->IsVisible(true), "Gallery59 button unavailable");
                button->ScrollIntoView(false);
                presenter.update(++now, size.first, size.second);
                const auto p = button->GetAbsoluteOffset();
                const auto b = button->GetBox().GetSize(Rml::BoxArea::Border);
                check(p.x >= 0 && p.y >= 0 && p.x + b.x <= size.first && p.y + b.y <= size.second &&
                          p.y + b.y <= panel_position.y + panel_size.y + 1,
                      "Gallery59 controls unreachable at this layout");
                presenter.mouse_move(int(p.x + 8), int(p.y + 8), 0);
                presenter.mouse_button(0, true, 0);
                presenter.mouse_button(0, false, 0);
                presenter.update(++now, size.first, size.second);
                const auto command = presenter.pending_command();
                const std::string expected = std::string(name) == "pause"    ? "Pause"
                                             : std::string(name) == "resume" ? "Resume"
                                                                             : "Step";
                check(command && command->at("command") == expected,
                      "Gallery59 real pointer event lost its command");
                const auto ack = gate.dispatch(*command, [&](const Json& c) {
                    ui->command(scene, c, [&](const std::string& control) {
                        check(control == expected, "Gallery59 control dispatch changed");
                    });
                });
                check(ack.at("ok"), "Gallery59 command rejected by gameplay owner");
                presenter.acknowledge(ack);
            }
        }
        check(renderer.draws > 0 && presenter.diagnostic().empty(),
              "Gallery59 produced no valid native geometry");
        const auto& sources = presenter.asset_snapshot()->sources;
        check(std::any_of(sources.begin(), sources.end(),
                          [](const auto& s) {
                              return s.source.extension() == ".png" && s.details.at("width") == 128;
                          }) &&
                  std::any_of(sources.begin(), sources.end(),
                              [](const auto& s) {
                                  return s.source.extension() == ".jpg" &&
                                         s.details.at("width") == 512;
                              }),
              "Gallery59 did not actually admit both image formats");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Gallery59 native resources outlived the presenter");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch directory and font");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{scratch};
        const auto root = scratch / "source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto source = read_json(root / "Scenes/59-ui-png-jpeg.scene.json");
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        present(root, source, font);
        RuntimeUiInspector inspect = [&](AssetId id, std::stop_token) {
            UiTestRenderer renderer;
            UiPresenter p(renderer, root, font);
            return p.inspect_static({id});
        };
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease writer(root);
            prepare_runtime_content_catalog(writer, roots, inspect);
        }
        const auto exported = scratch / "export";
        package_runtime_content(root, exported, roots, {"windows", "d3d12"}, {}, {}, {}, inspect);
        const auto moved = scratch / "relocated";
        std::filesystem::rename(exported, moved);
        const auto shipped = open_runtime_content(moved, {"windows", "d3d12"});
        check(shipped.records().size() == 4, "Gallery59 closure lost or duplicated assets");
        check(read_json(root / "Scenes/59-ui-png-jpeg.scene.json") == source,
              "Gallery59 export changed the authored scene");
        std::filesystem::rename(root, scratch / "source-offline");
        present(moved, read_json(moved / shipped.records().at(roots[0]).source), font);
        std::cout << "Gallery59 native PNG/JPEG, pointer/layout/resource retirement and relocated "
                     "source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
