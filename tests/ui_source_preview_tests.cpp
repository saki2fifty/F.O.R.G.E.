#include "asset_bytes.hpp"
#include "ui_markup_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <cmath>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid preview operation accepted");
}
Json find(const Json& layout, const char* id) {
    for (const auto& element : layout.at("elements"))
        if (element.at("id") == id)
            return element;
    throw std::runtime_error("Missing preview element");
}
void presets(UiPresenter& presenter, AssetId asset) {
    const std::string source = R"rml(<rml><head><style>
body {display:block;position:relative;width:100%;height:100%;font-family:Lato;}
div {display:block;}
#parent {position:absolute;left:40px;top:30px;width:75%;height:70%;padding:10px;border:4px #fff;}
</style></head><body><div id="parent"><div id="target" style="box-sizing:border-box;padding:3px;border:2px #fff;"></div></div></body></rml>)rml";
    const auto nodes = inspect_ui_markup(source);
    const auto target = nodes.size() - 1;
    for (const auto& [preset, label] : ui_responsive_layout_presets) {
        const auto candidate = set_ui_markup_layout_preset(source, target, preset, 120, 40, 8, 6);
        for (const auto size : {Rml::Vector2i{800, 600}, Rml::Vector2i{480, 360}}) {
            check(presenter.preview_document({asset}, "layout.rml", candidate, size.x, size.y),
                  "Responsive preset did not load in the native preview");
            auto* document = Rml::GetContext(0)->GetDocument(0);
            auto* parent = document->GetElementById("parent");
            auto* child = document->GetElementById("target");
            const auto containing = parent->GetBox().GetSize(Rml::BoxArea::Padding);
            const auto origin = parent->GetAbsoluteOffset(Rml::BoxArea::Padding);
            const auto offset = child->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto extent = child->GetBox().GetSize(Rml::BoxArea::Border);
            const auto selected = std::ranges::find(ui_responsive_layout_presets, preset,
                                                    [](const auto& row) { return row.first; });
            const auto number = std::size_t(selected - ui_responsive_layout_presets.begin());
            const auto x = number % 4, y = number / 4;
            const auto expected_width = x == 3 ? containing.x - 16 : 120;
            const auto expected_height = y == 3 ? containing.y - 12 : 40;
            const auto expected_left = x == 1   ? (containing.x - expected_width) / 2 + 8
                                       : x == 2 ? containing.x - expected_width - 8
                                                : 8;
            const auto expected_top = y == 1   ? (containing.y - expected_height) / 2 + 6
                                      : y == 2 ? containing.y - expected_height - 6
                                               : 6;
            check(std::abs(extent.x - expected_width) < .6f &&
                      std::abs(extent.y - expected_height) < .6f &&
                      std::abs(offset.x - origin.x - expected_left) < .6f &&
                      std::abs(offset.y - origin.y - expected_top) < .6f,
                  (std::string("Native responsive bounds disagree for ") + std::string(preset))
                      .c_str());
            if (x != 0 || y != 0)
                check(!find(presenter.preview_layout(), "target").at("editable").get<bool>(),
                      "Responsive preset incorrectly enabled the plain-pixel drag adapter");
        }
    }
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font, bool shaping) {
    std::filesystem::create_directories(root);
    const std::string saved = "<rml><head><style>body {font-family:Lato;font-size:20px;}"
                              "</style></head><body><p>Saved source</p></body></rml>";
    std::ofstream(root / "layout.rml") << saved;
    const auto baseline = asset_detail::read_bytes(root / "layout.rml", 65536);
    const auto asset = register_ui_document(root, "layout.rml");
    const auto index = asset_detail::read_bytes(root / "forge.assets.json", 1024 * 1024);
    const std::string draft = R"rml(<rml><head><style>
body {display:block;position:relative;width:100%;height:100%;font-family:Lato;font-size:20px;}
div {display:block;}
#parent {position:absolute;left:80px;top:70px;width:500px;height:300px;padding:10px;border:4px #ffffff;}
#panel {position:absolute;left:20px;top:30px;width:100px;height:60px;margin:5px;padding:3px;border:2px #ffffff;}
#relative {position:relative;width:100px;height:40px;}
#percent {position:absolute;left:10%;top:10px;width:100px;height:40px;}
#transformed {position:absolute;left:300px;top:10px;width:50px;height:40px;transform:rotate(10deg);}
#limited {position:absolute;left:360px;top:10px;width:50px;height:40px;min-width:60px;}
</style></head><body><div id="parent"><div id="panel">Draft</div></div>
<div id="relative">Relative</div><div id="percent">Percent</div>
<div id="transformed">Transform</div><div id="limited">Limited</div>
<div id="duplicate"></div><div id="duplicate"></div></body></rml>)rml";
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr, shaping);
        check(presenter.preview_layout().is_null(), "Empty preview has geometry");
        check(presenter.preview_document({asset.id}, "layout.rml", draft, 800, 600),
              "Draft preview did not load");
        auto layout = presenter.preview_layout();
        const auto cached_revision = presenter.presentation_revision();
        check(presenter.preview_document({asset.id}, "layout.rml", draft, 800, 600, 1, false) &&
                  presenter.presentation_revision() == cached_revision &&
                  presenter.preview_layout().at("source_state") ==
                      asset_detail::content_digest(
                          std::as_bytes(std::span(draft.data(), draft.size()))),
              "Identical preview request rebuilt or lost its admitted source digest");
        const auto panel = find(layout, "panel");
        check(panel.at("editable").get<bool>(), panel.at("reason").get<std::string>().c_str());
        check(panel.at("pixels") ==
                  Json{{"left", 20.f}, {"top", 30.f}, {"width", 100.f}, {"height", 60.f}},
              "CSS pixel properties changed");
        check(panel.at("bounds").at(2) == 110.f && panel.at("bounds").at(3) == 70.f,
              "Native border box omitted padding or border");
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* native = document->GetElementById("panel");
        const auto offset = native->GetAbsoluteOffset(Rml::BoxArea::Border);
        check(panel.at("bounds").at(0) == offset.x && panel.at("bounds").at(1) == offset.y,
              "Preview did not use native absolute coordinates");
        check(presenter.preview_hit(offset.x + 8, offset.y + 8) == "panel",
              "Native hit did not select the authored parent of text");
        check(presenter.preview_hit(-1, -1).empty(), "Off-canvas hit accepted");
        for (const auto* id : {"relative", "percent", "transformed", "limited", "duplicate"})
            check(!find(layout, id).at("editable").get<bool>(),
                  "Unsupported layout admitted for canvas edits");
        check(!presenter.asset_snapshot() && presenter.document_count() == 0 &&
                  !presenter.pending_command() && !presenter.wants_text(),
              "Preview exposed runtime content or input");
        check(!presenter.mouse_move(int(offset.x + 8), int(offset.y + 8), 0) &&
                  !presenter.mouse_button(0, true, 0) &&
                  !presenter.key(Rml::Input::KI_TAB, true, 0) && !presenter.text("x"),
              "Preview accepted gameplay field input");
        presenter.render();
        check(renderer.draws > 0, "Preview produced no native draws");
        const auto revision = presenter.presentation_revision();
        check(!presenter.preview_document({asset.id}, "layout.rml", "<rml>", 800, 600) &&
                  presenter.presentation_revision() == revision &&
                  presenter.preview_layout().at("stale").get<bool>() &&
                  presenter.preview_hit(offset.x + 8, offset.y + 8).empty(),
              "Invalid draft replaced or enabled stale geometry");
        check(!presenter.preview_document({asset.id}, "layout.rml", "<rml>", 800, 600, 1, false) &&
                  presenter.presentation_revision() == revision,
              "Repeated failed preview request replaced the last-good set");
        presenter.render();
        check(presenter.preview_document({asset.id}, "layout.rml", draft, 800, 600, 2) &&
                  find(presenter.preview_layout(), "panel").at("bounds") == panel.at("bounds"),
              "Physical pixel layout scaled with density");
        std::string image(21, '\0');
        image[2] = 2;
        image[12] = image[14] = 1;
        image[16] = 24;
        image[17] = 32;
        std::ofstream(root / "badge.tga", std::ios::binary).write(image.data(), image.size());
        check(!presenter.preview_document({asset.id}, "layout.rml",
                                          "<rml><body><img src=\"badge.tga\"/></body></rml>", 800,
                                          600) &&
                  presenter.diagnostic() == "UI resource has no admitted document origin",
              "Missing native document origin lost its first useful resource diagnostic");
        check(presenter.preview_document(
                  {asset.id}, "layout.rml",
                  "<rml><head></head><body><img id=\"badge\" src=\"badge.tga\"/></body></rml>", 800,
                  600) &&
                  find(presenter.preview_layout(), "badge").at("visible").get<bool>(),
              "Explicit native document header did not admit the relative image");
        rejects([&] { presenter.preview_document({asset.id}, "layout.rml", draft, 0, 600); });
        rejects([&] { presenter.accept(Json::object()); });
        rejects([&] { presenter.prepare(Json::object()); });
        presets(presenter, asset.id);
        presenter.reset("runtime", 1);
        check(presenter.preview_layout().is_null() && Rml::GetNumContexts() == 0,
              "Reset retained preview context");
        const auto entity = EntityId::generate();
        const Json state{{"version", 1},
                         {"session", "runtime"},
                         {"generation", 1u},
                         {"revision", 1u},
                         {"documents", Json::array({{{"entity", entity},
                                                     {"asset", asset.id},
                                                     {"instance", "runtime"},
                                                     {"visible", true},
                                                     {"layer", 0u},
                                                     {"model", Json::object()},
                                                     {"commands", Json::array()}}})}};
        if (!presenter.accept(state))
            throw std::runtime_error("Runtime handover: " + presenter.diagnostic());
        check(presenter.document_count() == 1 && presenter.asset_snapshot(),
              "Runtime handover failed");
        rejects([&] { presenter.preview_document({asset.id}, "layout.rml", draft, 800, 600); });
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Preview leaked render resources");
    check(asset_detail::read_bytes(root / "forge.assets.json", 1024 * 1024) == index &&
              asset_detail::read_bytes(root / "layout.rml", 65536) == baseline,
          "Preview changed saved source/catalog");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch and default UI font");
        const auto font = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (const bool shaping : {false, true})
            exercise(std::filesystem::path(argv[1]) / forge::AssetId::generate().str(), font,
                     shaping);
        std::cout << "Native UI source preview passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
