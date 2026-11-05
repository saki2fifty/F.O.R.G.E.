#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    try {
        check(argc == 3, "Need box-shadow scratch and default font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto path = root / "Shadow.rml";
        const std::string source = R"rml(<rml><head><style>
body,div {display:block;} body {margin:0;font-family:Lato;}
#panel {position:absolute;left:20px;top:20px;width:100px;height:40px;border-radius:6px;background-color:#e96644;}
</style></head><body><div id="panel" data-style-box-shadow="'#0008 8px 6px ' + blur + 'px 0px'"/></body></rml>)rml";
        const auto write = [&](const std::string& text) { std::ofstream(path) << text; };
        write(source);
        const auto asset = register_ui_document(root, "Shadow.rml");
        EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
        Scene scene(engine.world());
        scene.restore_snapshot(
            {{"version", 1},
             {"entities",
              Json::array(
                  {{{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}},
                   {{"id", "surface"}, {"name", "Surface"}, {"components", Json::object()}}})}});
        const auto output = AssetId::generate();
        scene.entity("hud").set<UiDocument>({{asset.id}, true, true, 0});
        scene.entity("surface").set<UiDocument>({{asset.id}, true, true, 1, {output}});
        const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
        const auto publish = [&](double blur) {
            ui->publish(scene.reference("hud").entity, "blur", blur);
            ui->publish(scene.reference("surface").entity, "blur", blur);
        };
        publish(4);
        UiTestRenderer renderer;
        renderer.filters_supported = true;
        renderer.snapshots_supported = true;
        {
            UiPresenter presenter(renderer, root,
                                  asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
            presenter.reset("box-shadow", 1);
            const auto snapshot = [&] { return ui->snapshot(scene, "box-shadow", 1, 40, true); };
            const bool accepted = presenter.accept(snapshot());
            check(accepted, presenter.diagnostic().c_str());
            check(
                renderer.snapshot_saves == 0 && renderer.snapshot_textures.empty() &&
                    renderer.layer_pushes == 0 && renderer.filters.empty(),
                "Box-shadow preparation captured/drew a framebuffer or retained temporary filters");
            double now = 0;
            const auto draw = [&] {
                presenter.update(++now, 640, 360, 1);
                presenter.update_output({output}, 320, 160);
                presenter.render();
                presenter.render_output({output});
            };
            draw();
            check(renderer.snapshot_saves > 0 && !renderer.snapshot_textures.empty() &&
                      renderer.filters.empty(),
                  "Native box-shadow callback did not retry after non-drawing preparation");
            const auto captures = renderer.snapshot_saves;
            draw();
            check(renderer.snapshot_saves == captures,
                  "Unchanged box shadows did not retain their native texture cache");
            auto* main = Rml::GetContext(0);
            auto* surface = Rml::GetContext(1);
            publish(8);
            const bool tuned = presenter.accept(snapshot());
            check(tuned, presenter.diagnostic().c_str());
            draw();
            check(Rml::GetContext(0) == main && Rml::GetContext(1) == surface &&
                      renderer.snapshot_saves > captures,
                  "Paused box-shadow model rebuilt contexts or failed to replace its native cache");
            const auto good_sources = presenter.asset_snapshot()->sources;
            const auto good_textures = renderer.snapshot_textures;
            const auto good_captures = renderer.snapshot_saves;
            auto excessive = source;
            const std::string expression = "'#0008 8px 6px ' + blur + 'px 0px'";
            excessive.replace(excessive.find(expression), expression.size(),
                              "'#0008 8px 6px 40px 0px'");
            write(excessive);
            check(
                !presenter.reload() && !presenter.diagnostic().empty() &&
                    Rml::GetContext(0) == main && Rml::GetContext(1) == surface &&
                    presenter.asset_snapshot()->sources == good_sources &&
                    renderer.snapshot_textures == good_textures &&
                    renderer.snapshot_saves == good_captures && renderer.filters.empty(),
                "Excessive box-shadow candidate captured, leaked or replaced the last usable set");
            draw();
            write(source);
            check(presenter.reload(), "Corrected box-shadow source failed to reload");
            draw();
            auto candidate = snapshot();
            candidate["revision"] = candidate.at("revision").get<unsigned>() + 1;
            for (auto& document : candidate["documents"])
                document["model"]["blur"] = 12;
            const auto live = renderer.snapshot_textures;
            const auto before = renderer.snapshot_saves;
            const auto ticket = presenter.prepare(candidate);
            check(presenter.prepared(ticket) && Rml::GetNumContexts() == 4 &&
                      renderer.snapshot_saves == before && renderer.snapshot_textures == live,
                  "Staged box-shadow candidate drew or cached a blank texture");
            presenter.cancel_prepared(ticket);
            check(Rml::GetNumContexts() == 2 && renderer.snapshot_textures == live,
                  "Cancelled box-shadow candidate retired usable textures or retained contexts");
            presenter.reset("retired", 2);
            check(Rml::GetNumContexts() == 0 && renderer.snapshot_textures.empty() &&
                      renderer.filters.empty(),
                  "Box-shadow retirement retained native callback snapshots");
        }
        std::filesystem::remove_all(root);
        std::cout << "Box-shadow native cold cache, non-drawing preparation, main/output models, "
                     "last-good and retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
