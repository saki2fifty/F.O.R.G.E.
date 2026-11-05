#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    try {
        check(argc == 3, "Need gradient scratch and default font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto path = root / "Gradient.rml";
        const std::string original = R"rml(<rml><head><style>
body,div {display:block;} body {margin:0;font-family:Lato;}
#panel {width:160px;height:80px;border-radius:6px;}
</style></head><body><div id="panel" data-style-decorator="'linear-gradient(' + angle + 'deg, #ff0000, #0000ff)'"/></body></rml>)rml";
        const auto write = [&](const std::string& source) { std::ofstream(path) << source; };
        write(original);
        const auto asset = register_ui_document(root, "Gradient.rml");
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
        renderer.gradients_supported = true;
        {
            UiPresenter presenter(renderer, root,
                                  asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
            presenter.reset("gradient-ui", 1);
            const auto snapshot = [&] { return ui->snapshot(scene, "gradient-ui", 1, 40, true); };
            const bool accepted = presenter.accept(snapshot());
            check(accepted, presenter.diagnostic().c_str());
            check(renderer.gradient_draws == 0 && renderer.gradients.size() == 2 &&
                      Rml::GetNumContexts() == 2,
                  "Gradient preparation drew or failed to create both groups");
            double now = 0;
            const auto draw = [&] {
                presenter.update(++now, 640, 360, 2);
                presenter.update_output({output}, 320, 160);
                presenter.render();
                presenter.render_output({output});
            };
            draw();
            check(renderer.gradient_draws >= 2,
                  "Native gradient callbacks did not draw both groups");
            auto* first = Rml::GetContext(0);
            auto* second = Rml::GetContext(1);
            publish(180);
            const bool updated = presenter.accept(snapshot());
            check(updated, presenter.diagnostic().c_str());
            draw();
            check(Rml::GetContext(0) == first && Rml::GetContext(1) == second &&
                      std::any_of(renderer.gradients.begin(), renderer.gradients.end(),
                                  [](const auto& row) {
                                      return std::abs(row.second.line[2]) < 1e-5 &&
                                             row.second.line[3] > .01f;
                                  }),
                  "Paused gradient model update lost its projection or rebuilt contexts");
            const auto sources = presenter.asset_snapshot()->sources;
            const auto count = renderer.gradients.size();
            const std::string unsupported =
                "<rml><head><style>body,div{display:block;}body{font-family:Lato;}div{width:160px;"
                "height:80px;decorator:radial-gradient(circle "
                "70000px,#ff0000,#0000ff);}</style></head><body><div/"
                "></body></rml>";
            std::string many = "linear-gradient(90deg";
            for (unsigned i = 0; i < 17; ++i)
                many += ", #ff0000";
            many += ')';
            const auto excessive = "<rml><head><style>body {font-family:Lato;} "
                                   "div{width:160px;height:80px;decorator:" +
                                   many + ";}</style></head><body><div/></body></rml>";
            for (const auto& source : {unsupported, excessive}) {
                const auto draws = renderer.gradient_draws;
                write(source);
                check(!presenter.reload() && !presenter.diagnostic().empty(),
                      "Unsupported gradient source replaced the complete predecessor");
                check(renderer.gradient_draws == draws && renderer.gradients.size() == count &&
                          presenter.asset_snapshot()->sources == sources &&
                          Rml::GetContext(0) == first && Rml::GetContext(1) == second,
                      "Failed gradient candidate drew or retained resources");
                draw();
            }
            write(original);
            check(presenter.reload(), "Corrected gradient source did not reload");
            draw();
            for (const bool radial : {false, true}) {
                for (const bool repeating : {false, true}) {
                    auto curved = original;
                    const std::string expression =
                        "'linear-gradient(' + angle + 'deg, #ff0000, #0000ff)'";
                    const auto position = curved.find(expression);
                    check(position != curved.npos, "Gradient model fixture expression missing");
                    const auto name = std::string(repeating ? "repeating-" : "") +
                                      (radial ? "radial-gradient(circle " : "conic-gradient(from ");
                    curved.replace(
                        position, expression.size(),
                        "'" + name + "' + angle + '" +
                            (radial ? "px, #ff0000, #0000ff)'" : "deg, #ff0000, #0000ff)'"));
                    write(curved);
                    check(presenter.reload(), "Curved gradient source failed native admission");
                    draw();
                    auto* main_context = Rml::GetContext(0);
                    auto* output_context = Rml::GetContext(1);
                    publish(45);
                    const bool tuned = presenter.accept(snapshot());
                    check(tuned, presenter.diagnostic().c_str());
                    draw();
                    check(
                        Rml::GetContext(0) == main_context &&
                            Rml::GetContext(1) == output_context &&
                            renderer.gradients.size() == 2 &&
                            std::all_of(renderer.gradients.begin(), renderer.gradients.end(),
                                        [&](const auto& row) {
                                            return row.second.kind ==
                                                       (radial ? ui_gradient_detail::Kind::Radial
                                                               : ui_gradient_detail::Kind::Conic) &&
                                                   row.second.repeating == repeating &&
                                                   std::abs(row.second.line[2] -
                                                            (radial ? 1.f / 45 : .125f)) < 1e-6;
                                        }),
                        "Curved paused model update rebuilt contexts or lost shape/repeat values");
                }
            }
            write(original);
            check(presenter.reload(), "Linear source did not reload after curved gradients");
            draw();
            auto candidate = snapshot();
            candidate["revision"] = candidate.at("revision").get<unsigned>() + 1;
            const auto live = renderer.gradients.size();
            const auto draws = renderer.gradient_draws;
            const auto ticket = presenter.prepare(candidate);
            check(presenter.prepared(ticket) && Rml::GetNumContexts() == 4 &&
                      renderer.gradient_draws == draws && renderer.gradients.size() > live,
                  "Staged gradient candidate omitted groups or drew");
            presenter.cancel_prepared(ticket);
            check(Rml::GetNumContexts() == 2 && renderer.gradients.size() == live,
                  "Cancelled gradient candidate retained handles");
            presenter.reset("retired", 2);
            check(Rml::GetNumContexts() == 0 && renderer.gradients.empty() &&
                      renderer.geometry.empty() && renderer.textures.empty(),
                  "Gradient retirement retained native resources");
        }
        std::filesystem::remove_all(root);
        std::cout << "Native gradient main/output groups, paused models, source retention, staging "
                     "and retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
