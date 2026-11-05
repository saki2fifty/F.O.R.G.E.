#include "asset_bytes.hpp"
#include "fixtures/ui_png.hpp"
#include "ui_test_renderer.hpp"
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    try {
        require(argc == 3, "Need luminance scratch directory and default font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto path = root / "Mask.rml";
        const auto source = [](const std::string& effect, bool dynamic = false) {
            return std::string("<rml><head><style>body,div{display:block;}body{margin:0;}"
                               "#sample{width:160px;height:80px;background-color:#ff0000;") +
                   (dynamic ? "" : "mask-image:" + effect + ";") +
                   "}</style></head><body><div id=\"sample\" " +
                   (dynamic ? "data-style-mask-image=\"'luminance(linear-gradient(' + angle + "
                              "'deg,#000,#fff))'\""
                            : "") +
                   "><div "
                   "style=\"width:20px;height:20px;background-color:green;\"/></div></body></rml>";
        };
        const auto write = [&](const std::string& text) { std::ofstream(path) << text; };
        const auto original = source({}, true);
        write(original);
        const std::array<unsigned char, 8> bitmap{0, 0, 0, 255, 255, 255, 255, 128};
        const auto png = test::encode_ui_png(bitmap.data(), 2, 1);
        std::ofstream image(root / "mask.png", std::ios::binary);
        image.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        image.close();
        const auto asset = register_ui_document(root, "Mask.rml");
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
        const auto publish = [&](double angle) {
            ui->publish(scene.reference("hud").entity, "angle", angle);
            ui->publish(scene.reference("surface").entity, "angle", angle);
        };
        publish(90);
        UiTestRenderer renderer;
        renderer.filters_supported = renderer.gradients_supported = renderer.masks_supported = true;
        {
            UiPresenter presenter(renderer, root,
                                  asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
            presenter.reset("luminance", 1);
            const auto snapshot = [&] { return ui->snapshot(scene, "luminance", 1, 40, true); };
            require(presenter.accept(snapshot()), presenter.diagnostic());
            require(!renderer.draws && !renderer.gradient_draws && !renderer.mask_saves &&
                        renderer.layers.empty() && renderer.filters.size() == 2,
                    "Luminance preparation drew or leaked ephemeral filters/layers");
            double now = 0;
            const auto draw = [&] {
                presenter.update(++now, 640, 360);
                presenter.update_output({output}, 320, 160);
                presenter.render();
                presenter.render_output({output});
                require(presenter.diagnostic().empty(), presenter.diagnostic());
                require(renderer.layers.empty() && renderer.filters.size() == 2,
                        "Luminance draw retained transient layers/mask filters");
            };
            draw();
            require(renderer.mask_saves == 2 && renderer.layer_pushes >= 6,
                    "Main and camera-output luminance masks did not use the native mask callback");
            auto* first = Rml::GetContext(0);
            publish(180);
            require(presenter.accept(snapshot()), presenter.diagnostic());
            draw();
            require(Rml::GetContext(0) == first, "Live luminance model recreated a context");
            for (const auto* effect : {"luminance(linear-gradient(90deg,#000,#fff))",
                                       "luminance(radial-gradient(circle closest-side,#000,#fff))",
                                       "luminance(repeating-conic-gradient(#000 0deg,#fff 180deg))",
                                       "luminance(horizontal-gradient(#000 #fff))",
                                       "luminance(image(mask.png contain))"}) {
                write(source(effect));
                require(presenter.reload(), presenter.diagnostic());
                draw();
            }
            const auto good = presenter.asset_snapshot()->sources;
            first = Rml::GetContext(0);
            for (const auto* bad :
                 {"luminance(luminance(image(mask.png)))", "luminance(none)", "luminance(named)",
                  "luminance(image(mask.png),image(mask.png))"}) {
                write(source(bad));
                require(!presenter.reload(), "Unsupported luminance source was accepted");
                require(Rml::GetContext(0) == first &&
                            presenter.asset_snapshot()->sources == good &&
                            renderer.layers.empty() && renderer.filters.size() == 2,
                        "Failed luminance reload replaced good contexts/resources");
            }
            write(source("luminance(" + std::string(4097, 'x') + ")"));
            require(!presenter.reload() && Rml::GetContext(0) == first,
                    "Oversized luminance source replaced the usable context");
            write("<rml><head><style>body,div{display:block;}div{width:160px;height:80px;"
                  "mask-image:luminance(linear-gradient(90deg,#000,#fff));}</style></head>"
                  "<body><div><div><div><div><div/></div></div></div></div></body></rml>");
            require(!presenter.reload() && Rml::GetContext(0) == first && renderer.layers.empty(),
                    "Deep luminance masks bypassed the shared layer admission");
            write(original);
            require(presenter.reload(), presenter.diagnostic());
            draw();
        }
        require(renderer.geometry.empty() && renderer.textures.empty() &&
                    renderer.filters.empty() && renderer.gradients.empty() &&
                    renderer.layers.empty(),
                "Luminance presenter shutdown retained native resources");
        std::cout << "Native luminance main/output, live/reload, delegation, non-drawing admission "
                     "and lifetime passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
