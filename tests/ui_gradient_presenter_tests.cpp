#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
#include <vector>
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
            renderer.filters_supported = true;
            renderer.masks_supported = true;
            for (const auto* shape :
                 {"linear-gradient(", "radial-gradient(circle ", "conic-gradient(from "}) {
                publish(45);
                auto masked = original;
                masked.replace(masked.find("data-style-decorator"),
                               std::string("data-style-decorator").size(), "data-style-mask-image");
                masked.replace(masked.find("#panel {"), std::string("#panel {").size(),
                               "#panel {background-color:#ff0000;");
                const auto start = masked.find("linear-gradient(");
                masked.replace(start, std::string("linear-gradient(").size(), shape);
                if (std::string_view(shape).starts_with("radial"))
                    masked.replace(masked.find("deg, #ff0000"), 3, "px");
                const auto saves = renderer.mask_saves;
                write(masked);
                check(presenter.reload() && renderer.mask_saves == saves &&
                          renderer.filters.empty(),
                      "Mask preparation captured a framebuffer or retained its temporary filter");
                draw();
                check(renderer.mask_saves >= saves + 2 && renderer.filters.empty() &&
                          renderer.layers.empty(),
                      "Main/output alpha masks did not draw and retire their filters/layers");
                auto* context = Rml::GetContext(0);
                publish(90);
                check(presenter.accept(snapshot()), "Live mask model was not accepted");
                draw();
                check(Rml::GetContext(0) == context && renderer.filters.empty(),
                      "Live mask model replaced a context or retained an ephemeral filter");
            }
            auto excessive_mask = original;
            excessive_mask.replace(excessive_mask.find("data-style-decorator"),
                                   std::string("data-style-decorator").size(),
                                   "data-style-mask-image");
            excessive_mask.replace(excessive_mask.find("#panel {"), std::string("#panel {").size(),
                                   "#panel {filter:opacity(1) opacity(1) opacity(1) opacity(1) "
                                   "opacity(1) opacity(1) opacity(1) opacity(1);");
            const auto mask_context = Rml::GetContext(0);
            const auto mask_saves = renderer.mask_saves;
            write(excessive_mask);
            check(!presenter.reload() && !presenter.diagnostic().empty() &&
                      Rml::GetContext(0) == mask_context && renderer.mask_saves == mask_saves &&
                      renderer.filters.empty(),
                  "Mask plus eight filters bypassed chain admission or replaced the last good "
                  "source");
            write(original);
            check(presenter.reload(), "Linear source did not reload after masked gradients");
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
            const auto material = AssetId::generate();
            const auto effect_source =
                std::string("<rml><head><style>body,div{display:block;}body{margin:0;}"
                            "#panel{width:160px;height:80px;decorator:shader(\"material:") +
                material.str() + "\");}</style></head><body><div id=\"panel\"/></body></rml>";
            renderer.effects_supported = true;
            write(effect_source);
            check(presenter.reload(), presenter.diagnostic().c_str());
            check(renderer.gradients.empty() && renderer.effects.size() == 2,
                  "Native shader decorator did not prepare the main/output element handles");
            for (const auto& [handle, effect] : renderer.effects) {
                (void)handle;
                check(effect.material.id == material &&
                          effect.dimensions == std::array{160.f, 80.f},
                      "Native shader decorator changed its Material or element dimensions");
            }
            const auto* assets = presenter.asset_snapshot();
            check(assets && std::any_of(
                                assets->sources.begin(), assets->sources.end(),
                                [](const auto& source) {
                                    return source.source == "Gradient.rml" &&
                                           source.details.at("runtime_features").at("effects") == 1;
                                }),
                  "Native shader source did not record its required runtime feature");
            check(renderer.effect_draws == 0, "UI effect preparation drew a framebuffer");
            draw();
            check(renderer.effect_draws >= 2, "Main/output native effect draws were omitted");
            const auto effects_drawn = renderer.effect_draws;
            std::vector<std::uintptr_t> live_effects;
            for (const auto& [handle, effect] : renderer.effects) {
                (void)effect;
                live_effects.push_back(handle);
            }
            const auto effect_ticket = presenter.prepare(candidate);
            check(presenter.prepared(effect_ticket) && renderer.effects.size() == 4 &&
                      renderer.effect_draws == effects_drawn,
                  "Staged UI effect candidate drew or omitted its handles");
            const auto handles = presenter.prepared_shader_handles(effect_ticket);
            check(handles.size() == 2 && std::all_of(handles.begin(), handles.end(),
                                                     [&](auto h) {
                                                         return renderer.effects.contains(h) &&
                                                                std::find(live_effects.begin(),
                                                                          live_effects.end(),
                                                                          h) == live_effects.end();
                                                     }),
                  "UI candidate readiness included the visible set or omitted an output");
            presenter.cancel_prepared(effect_ticket);
            check(renderer.effects.size() == 2,
                  "Cancelled UI effect candidate retained its native handles");
            bool stale_handles = false;
            try {
                (void)presenter.prepared_shader_handles(effect_ticket);
            } catch (const std::exception&) {
                stale_handles = true;
            }
            check(stale_handles, "Retired candidate exposed stale native shader observations");
            write("<rml><head><style>body,div{display:block;}div{width:80px;height:40px;"
                  "background-color:red;}</style></head><body><div/></body></rml>");
            const auto plain_ticket = presenter.prepare(candidate);
            check(presenter.prepared_shader_handles(plain_ticket).empty() &&
                      renderer.effects.size() == 2 && renderer.effect_draws == effects_drawn,
                  "Plain candidate borrowed live effect readiness or replaced visible UI");
            presenter.cancel_prepared(plain_ticket);
            auto invalid_effect = effect_source;
            invalid_effect.replace(invalid_effect.find("material:"), 9, "invalid:");
            const auto effect_context = Rml::GetContext(0);
            write(invalid_effect);
            check(!presenter.reload() && Rml::GetContext(0) == effect_context &&
                      renderer.effects.size() == 2,
                  "Invalid native UI effect replaced the last usable documents");
            presenter.reset("retired", 2);
            check(Rml::GetNumContexts() == 0 && renderer.gradients.empty() &&
                      renderer.effects.empty() && renderer.geometry.empty() &&
                      renderer.textures.empty(),
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
