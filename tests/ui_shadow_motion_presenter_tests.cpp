#include "asset_bytes.hpp"
#include "ui_filter_profile.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/DecorationTypes.h>
#include <cmath>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
const std::string source = R"rml(<rml><head><style>
body,forge-shadow-panel {display:block;} body {margin:0;font-family:Lato;}
#panel {position:absolute;left:30px;top:30px;width:80px;height:50px;
 background-color:#49b;border-radius:6px;forge-shadow-color:#f008;
 animation:1s linear-in infinite glow;}
@keyframes glow {
 from {forge-shadow-blur:0px;forge-shadow-y:0px;forge-shadow-spread:0px;forge-shadow-color:#f008;}
 to {forge-shadow-blur:16px;forge-shadow-y:12px;forge-shadow-spread:4px;forge-shadow-color:#00f8;}
}
#transition {position:absolute;left:200px;top:40px;width:40px;height:30px;
 background-color:#acf;forge-shadow-color:#f008;font-size:10px;
 forge-shadow-blur:2dp;forge-shadow-spread:1em;
 transition:forge-shadow-x .4s linear-in,forge-shadow-color .4s linear-in;}
#transition.raised {forge-shadow-x:16px;forge-shadow-color:#00f8;}
</style></head><body><forge-shadow-panel id="panel" data-style-forge-shadow-x="offset + 'px'"/>
<forge-shadow-panel id="transition" data-class-raised="raised" data-style-forge-shadow-inset="inset"/>
</body></rml>)rml";
Rml::BoxShadow shadow(unsigned context, const char* id = "panel") {
    auto* native = Rml::GetContext(int(context));
    require(native && native->GetNumDocuments() == 1, "Expected one native document per view");
    const auto* panel = native->GetDocument(0)->GetElementById(id);
    require(panel, "Native shadow panel missing");
    const auto& list = const_cast<Rml::Element*>(panel)
                           ->GetProperty(Rml::PropertyId::BoxShadow)
                           ->value.GetReference<Rml::BoxShadowList>();
    require(list.size() == 1, "Shadow panel did not derive one native shadow");
    return list.front();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need shadow motion scratch and default font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
            }
        } cleanup{root};
        const auto path = root / "Motion.rml";
        const auto write = [&](const std::string& text) { std::ofstream(path) << text; };
        write(source);
        const auto asset = register_ui_document(root, "Motion.rml");
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
        auto publish = [&](double offset, bool raised = false, const char* inset = "outset") {
            for (const auto* id : {"hud", "surface"}) {
                ui->publish(scene.reference(id).entity, "offset", offset);
                ui->publish(scene.reference(id).entity, "raised", raised);
                ui->publish(scene.reference(id).entity, "inset", inset);
            }
        };
        publish(10);
        UiTestRenderer renderer;
        renderer.filters_supported = renderer.snapshots_supported = true;
        UiPresenter presenter(renderer, root, asset_detail::read_bytes(argv[2], 4 * 1024 * 1024));
        presenter.reset("motion", 1);
        const auto snapshot = [&] { return ui->snapshot(scene, "motion", 1, 40, true); };
        require(presenter.accept(snapshot()), presenter.diagnostic());
        require(renderer.snapshot_saves == 0, "Candidate preparation drew/captured a shadow");
        auto draw = [&](double now) {
            presenter.update(now, 640, 360, 1);
            presenter.update_output({output}, 320, 160);
            presenter.render();
            presenter.render_output({output});
        };
        auto* main = Rml::GetContext(0);
        auto* camera = Rml::GetContext(1);
        for (unsigned frame = 0; frame < 180; ++frame) {
            const double now = double(frame) / 60;
            draw(now);
            for (unsigned context : {0u, 1u}) {
                const auto value = shadow(context);
                require(std::abs(value.offset_x.number - 10) < .001,
                        "Paused model-bound shadow offset changed");
                if (frame % 60) {
                    const double progress = std::fmod(now, 1.);
                    require(std::abs(value.blur_radius.number - 16 * progress) < .002 &&
                                std::abs(value.offset_y.number - 12 * progress) < .002 &&
                                std::abs(value.spread_distance.number - 4 * progress) < .002,
                            "Shadow motion lags native presentation time");
                    // Native color interpolation uses an approximate gamma of two.
                    require(
                        std::abs(value.color.red - 136 * std::sqrt(1 - progress)) < 2 &&
                            std::abs(value.color.blue - 136 * std::sqrt(progress)) < 2 &&
                            std::abs(int(value.color.alpha) - 136) <= 1,
                        "Shadow color differs from the independent premultiplied oracle: frame=" +
                            std::to_string(frame) + " rgba=" + std::to_string(value.color.red) +
                            "," + std::to_string(value.color.green) + "," +
                            std::to_string(value.color.blue) + "," +
                            std::to_string(value.color.alpha));
                }
            }
            require(renderer.snapshot_textures.size() <= 4,
                    "Animated shadow cached textures accumulated");
        }
        require(renderer.snapshot_saves > 100, "Presentation animation did not redraw shadows");
        publish(-5, true, "inset");
        require(presenter.accept(snapshot()), presenter.diagnostic());
        draw(3);
        for (unsigned frame = 1; frame <= 4; ++frame)
            draw(3 + frame * .05);
        for (unsigned context : {0u, 1u}) {
            const auto value = shadow(context, "transition");
            require(std::abs(value.offset_x.number - 8) < .002 && value.inset &&
                        std::abs(value.color.red - 136 / std::sqrt(2.)) < 2 &&
                        std::abs(value.color.blue - 136 / std::sqrt(2.)) < 2,
                    "Class-driven shadow transition did not interpolate in the current frame: x=" +
                        std::to_string(value.offset_x.number) + " inset=" +
                        std::to_string(value.inset) + " red=" + std::to_string(value.color.red) +
                        " blue=" + std::to_string(value.color.blue));
        }
        draw(3.25);
        require(shadow(0).offset_x.number == -5 && shadow(1).offset_x.number == -5 &&
                    Rml::GetContext(0) == main && Rml::GetContext(1) == camera,
                "Live shadow values rebuilt views or retained stale offsets");
        for (unsigned frame = 6; frame <= 10; ++frame)
            draw(3 + frame * .05);
        for (unsigned context : {0u, 1u})
            require(shadow(context, "transition").offset_x.number == 16 &&
                        shadow(context, "transition").color.blue == 136,
                    "Shadow transition did not finish");
        for (const auto* property :
             {"forge-shadow-y:9000px;", "forge-shadow-spread:-9000px;", "forge-shadow-blur:33px;",
              "forge-shadow-blur:-1px;", "forge-shadow-y:20%;"}) {
            auto invalid = source;
            invalid.insert(invalid.find("</style>"), std::string("#transition {") + property + "}");
            write(invalid);
            const auto before = renderer.snapshot_textures;
            require(!presenter.reload() && !presenter.diagnostic().empty() &&
                        renderer.snapshot_textures == before && Rml::GetContext(0) == main &&
                        Rml::GetContext(1) == camera,
                    std::string("Invalid shadow panel replaced usable source: ") + property);
        }
        auto excessive = source;
        for (unsigned panel = 0; panel < 24; ++panel)
            excessive.insert(excessive.find("</body>"), "<forge-shadow-panel/>");
        write(excessive);
        require(!presenter.reload() && Rml::GetNumContexts() == 2,
                "Excessive shadow panel view replaced usable source or retained a candidate");
        write(source);
        require(presenter.reload(), presenter.diagnostic());
        draw(3.75);
        auto candidate = snapshot();
        candidate["revision"] = candidate.at("revision").get<unsigned>() + 1;
        for (auto& document : candidate["documents"])
            document["model"]["offset"] = 12;
        const auto live = renderer.snapshot_textures;
        const auto captures = renderer.snapshot_saves;
        const auto ticket = presenter.prepare(candidate);
        require(presenter.prepared(ticket) && Rml::GetNumContexts() == 4 &&
                    renderer.snapshot_textures == live && renderer.snapshot_saves == captures,
                "Staged shadow motion captured or retired usable views");
        presenter.cancel_prepared(ticket);
        require(Rml::GetNumContexts() == 2 && renderer.snapshot_textures == live,
                "Cancelled shadow motion retained views or retired live textures");
        presenter.update(4, 900, 600, 2);
        presenter.update_output({output}, 640, 320, 2);
        presenter.render();
        presenter.render_output({output});
        require(presenter.diagnostic().empty(), presenter.diagnostic());
        for (unsigned context : {0u, 1u})
            require(shadow(context, "transition").blur_radius.number == 4 &&
                        shadow(context, "transition").spread_distance.number == 10,
                    "Shadow panel dp/font-relative lengths ignored current viewport style");
        presenter.reset("retired", 2);
        require(Rml::GetNumContexts() == 0 && renderer.snapshot_textures.empty() &&
                    renderer.filters.empty(),
                "Shadow motion retirement leaked native resources");
        std::cout
            << "Shadow panel native presentation time/models/main-output/cache/last-good passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
