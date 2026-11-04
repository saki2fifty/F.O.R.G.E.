#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need UI filter scratch and default font");
        const Rml::Dictionary valid{{"sigma", Rml::Variant(2.f)},
                                    {"offset", Rml::Variant(Rml::Vector2f{-4, 8})},
                                    {"color", Rml::Variant(Rml::Colourb{30, 60, 90, 128})}};
        for (const auto* key : {"sigma", "offset", "color", "extra"}) {
            auto invalid = valid;
            if (std::string_view(key) == "extra")
                invalid["extra"] = Rml::Variant(0.f);
            else
                invalid[key] = Rml::Variant(Rml::String("wrong type"));
            bool rejected = false;
            try {
                (void)ui_filter_detail::admit_filter("drop-shadow", invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, "Malformed native shadow payload was admitted");
        }
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto path = root / "Filtered.rml";
        const std::string original = R"rml(<rml><head><style>
body, div, p {display:block;} body {margin:0; font-family:Lato; font-size:16px;}
#outer {width:180px; height:100px; filter:blur(1px) contrast(1) drop-shadow(#45678980 -4px 8px 2px);}
#panel {width:160px; height:80px; background-color:#459edc;}
</style></head><body><div id="outer"><div id="panel" data-style-filter="'opacity(' + amount + ') drop-shadow(#456789 -4px 8px ' + amount + 'px)'">
<p>Tick: {{tick}}</p></div></div></body></rml>)rml";
        const auto write = [&](const std::string& source) { std::ofstream(path) << source; };
        write(original);
        const auto asset = register_ui_document(root, "Filtered.rml");
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
        const auto publish = [&](double amount) {
            ui->publish(scene.reference("hud").entity, "amount", amount);
            ui->publish(scene.reference("surface").entity, "amount", amount);
        };
        publish(.5);
        UiTestRenderer renderer;
        renderer.filters_supported = true;
        {
            UiPresenter presenter(renderer, root,
                                  asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
            presenter.reset("filtered-ui", 1);
            const auto snapshot = [&] { return ui->snapshot(scene, "filtered-ui", 1, 40, true); };
            auto state = snapshot();
            const bool accepted = presenter.accept(state);
            check(accepted, presenter.diagnostic().c_str());
            check(renderer.layer_pushes == 0 && renderer.composites == 0 &&
                      renderer.filters.size() >= 6 && Rml::GetNumContexts() == 2,
                  "UI preparation drew layers or failed to compile both groups");
            double now = 0;
            const auto draw = [&] {
                presenter.update(++now, 640, 360, 2);
                presenter.update_output({output}, 320, 160);
                presenter.render();
                presenter.render_output({output});
                check(renderer.layers.empty(), "Native filter draw left a recording layer");
            };
            draw();
            check(renderer.layer_pushes >= 4 && renderer.composites >= 4,
                  "RML filters did not compose main and output documents");
            check(std::any_of(renderer.filters.begin(), renderer.filters.end(),
                              [](const auto& item) {
                                  return item.second.first == "drop-shadow" &&
                                         item.second.second == 2;
                              }),
                  "Native shadow parser did not produce its typed admitted payload");
            auto* first = Rml::GetContext(0);
            auto* second = Rml::GetContext(1);
            publish(.25);
            state = snapshot();
            const bool updated = presenter.accept(state);
            check(updated, presenter.diagnostic().c_str());
            draw();
            check(first == Rml::GetContext(0) && second == Rml::GetContext(1),
                  "Changing a filter's model value rebuilt native contexts");
            check(std::any_of(renderer.filters.begin(), renderer.filters.end(),
                              [](const auto& item) {
                                  return item.second.first == "opacity" &&
                                         item.second.second == .25f;
                              }),
                  "A paused model binding did not reach native filter parameters");
            check(std::any_of(renderer.filters.begin(), renderer.filters.end(),
                              [](const auto& item) {
                                  return item.second.first == "drop-shadow" &&
                                         item.second.second == .25f;
                              }),
                  "A paused model binding did not update the typed shadow sigma");

            const auto resources = presenter.asset_snapshot()->sources;
            const auto live_filters = renderer.filters.size();
            std::string deep = "<rml><head><style>body,div {display:block; "
                               "font-family:Lato;}</style></head><body>";
            for (unsigned i = 0; i < 5; ++i)
                deep += "<div style=\"width:120px;height:80px;filter:blur(1px);\">";
            deep += "nested";
            for (unsigned i = 0; i < 5; ++i)
                deep += "</div>";
            deep += "</body></rml>";
            auto excessive = original;
            const auto at = excessive.find("filter:blur(1px) contrast(1)");
            std::string chain = "filter:";
            for (unsigned i = 0; i < 9; ++i)
                chain += "opacity(.5) ";
            excessive.replace(
                at,
                std::string("filter:blur(1px) contrast(1) drop-shadow(#45678980 -4px 8px 2px);")
                    .size(),
                chain + ';');
            auto invalid_blur = original;
            invalid_blur.replace(invalid_blur.find("blur(1px)"), 9, "blur(17px)");
            auto invalid_shadow = original;
            invalid_shadow.replace(invalid_shadow.find("-4px 8px"), 8, "-129px 8px");
            for (const auto& source : {deep, excessive, invalid_blur, invalid_shadow}) {
                const auto pushes = renderer.layer_pushes;
                write(source);
                check(!presenter.reload() && !presenter.diagnostic().empty(),
                      "Unsupported/deep filter source replaced the complete predecessor");
                check(renderer.layer_pushes == pushes && renderer.filters.size() == live_filters &&
                          presenter.asset_snapshot()->sources == resources &&
                          first == Rml::GetContext(0) && second == Rml::GetContext(1),
                      "Failed candidate touched framebuffer routing or retained filter resources");
                draw();
            }
            write(original);
            check(presenter.reload(), "Corrected filter source did not reload");
            draw();
            renderer.fail_layer = true;
            presenter.render();
            check(presenter.diagnostic().find("fixture.filter.layer-allocation") !=
                          std::string::npos &&
                      renderer.layers.empty(),
                  "Main filter failure escaped instead of leaving a balanced diagnostic");
            bool rejected = false;
            try {
                presenter.render_output({output});
            } catch (const std::exception& error) {
                rejected = std::string(error.what()).find("fixture.filter.layer-allocation") !=
                           std::string::npos;
            }
            check(rejected && renderer.layers.empty(),
                  "A renderer fault escaped before native layers balanced or went unnoticed");
            renderer.fail_layer = false;
            draw();
            state = snapshot();
            state["revision"] = state.at("revision").get<std::uint64_t>() + 1;
            const auto count = renderer.filters.size();
            const auto pushes = renderer.layer_pushes;
            const auto ticket = presenter.prepare(state);
            check(presenter.prepared(ticket) && Rml::GetNumContexts() == 4 &&
                      renderer.layer_pushes == pushes && renderer.filters.size() > count,
                  "Staged filtered UI drew or omitted its output groups");
            presenter.cancel_prepared(ticket);
            check(Rml::GetNumContexts() == 2 && renderer.filters.size() == count,
                  "Cancelled filter candidates retained contexts or handles");
            presenter.reset("retired", 2);
            check(Rml::GetNumContexts() == 0 && renderer.filters.empty() &&
                      renderer.geometry.empty() && renderer.textures.empty(),
                  "Filtered UI reset retained native resources");
        }
        std::filesystem::remove_all(root);
        std::cout << "Native filtered RML, paused models, main/output groups, candidate retention, "
                     "callback failure and graceful retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
