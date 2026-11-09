#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Mesh.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>

namespace {
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& latin,
              const std::filesystem::path& arabic) {
    using namespace forge;
    std::filesystem::create_directories(root / "Assets");
    std::filesystem::copy_file(arabic, root / "Assets/Arabic.ttf");
    const auto file = root / "Assets/Hud.rml";
    std::ofstream(file) << R"rml(<rml><head><style>
@font-face {font-family:ForgeArabic;src:"Assets/Arabic.ttf";}
body {font-family:Lato;font-size:32px;color:#eef3fa;}
p {display:block;margin:8px;} #arabic {font-family:ForgeArabic;-rmlui-direction:rtl;-rmlui-language:ar;}
#effects {font-effect:shadow(2px 1px #0008), outline(1px #f8c060);}
input {width:240px;height:48px;}
</style></head><body><p id="latin">office</p><p id="marks">á</p>
<p id="arabic">سلام</p><p id="effects">Glyph effects</p>
<input id="field" type="text" value="office"/></body></rml>)rml";
    const auto asset = register_ui_document(root, "Assets/Hud.rml");
    const auto fallback_bytes = asset_detail::read_bytes(arabic, 4 * 1024 * 1024);
    const auto entity = EntityId::generate();
    const Json snapshot{{"version", 1},
                        {"session", "shape-native"},
                        {"generation", 1u},
                        {"revision", 1u},
                        {"documents", Json::array({{{"entity", entity},
                                                    {"asset", asset.id},
                                                    {"instance", entity.str() + ":1"},
                                                    {"visible", true},
                                                    {"layer", 0u},
                                                    {"model", {{"tick", 1u}, {"paused", true}}},
                                                    {"commands", {"Pause"}}}})}};
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, latin, {}, nullptr, true);
        auto* font = Rml::GetFontEngineInterface();
        check(font != nullptr, "Presenter did not install its shaped font owner");
        presenter.reset("shape-native", 1);
        const bool accepted = presenter.accept(snapshot);
        check(accepted, presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
        check(renderer.draws && !renderer.textures.empty(), "Native shaped text did not render");
        check(presenter.asset_snapshot() && presenter.asset_snapshot()->sources.size() == 2,
              "Native font file bypassed admitted resource observation");
        const auto handle = font->GetFontFaceHandle("Lato", Rml::Style::FontStyle::Normal,
                                                    Rml::Style::FontWeight::Normal, 32);
        const Rml::String language = "en";
        Rml::TextShapingContext context{language};
        const auto joined = font->GetStringWidth(handle, "office", context);
        context.letter_spacing = 1;
        check(font->GetStringWidth(handle, "office", context) > joined,
              "Public font callback did not apply shaping and tracking");
        check(Rml::LoadFontFace({reinterpret_cast<const Rml::byte*>(fallback_bytes.data()),
                                 fallback_bytes.size()},
                                "FallbackArabic", Rml::Style::FontStyle::Normal,
                                Rml::Style::FontWeight::Normal, true),
              "Public memory fallback face failed to load");
        const auto arabic_handle = font->GetFontFaceHandle(
            "ForgeArabic", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 32);
        const Rml::String arabic_language = "ar";
        Rml::TextShapingContext rtl{arabic_language};
        rtl.text_direction = Rml::Style::Direction::Rtl;
        check(arabic_handle && font->GetStringWidth(handle, "سلام", rtl) ==
                                   font->GetStringWidth(arabic_handle, "سلام", rtl),
              "Whole-run font fallback did not preserve shaped Arabic metrics");
        auto* field = Rml::GetContext(0)->GetDocument(0)->GetElementById("field");
        check(field != nullptr, "Native shaped input field is missing");
        auto* input = static_cast<Rml::ElementFormControlInput*>(field);
        const auto before_input = input->GetValue();
        field->Focus();
        check(presenter.text("a"), "Native shaped input did not accept text");
        auto edited_input = input->GetValue();
        const auto inserted = edited_input.find('a');
        check(inserted != edited_input.npos, "Native shaped input did not insert the character");
        edited_input.erase(inserted, 1);
        check(edited_input == before_input, "Native shaped input changed the remaining text");
        presenter.update(.02, 800, 600);
        presenter.render();
        check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
        const auto live_textures = renderer.textures.size();
        for (int i = 1; i <= 30; ++i) {
            presenter.update(i * .02 + .02, 800, 600);
            presenter.render();
        }
        check(renderer.textures.size() == live_textures,
              "Unchanged native text allocated new glyph atlas resources");
        presenter.reset("shape-retired", 2);
        check(Rml::GetNumContexts() == 0, "Shaped native contexts survived retirement");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Shaped native glyph resources survived shutdown");
}
void weights(const std::filesystem::path& root, const std::vector<std::byte>& latin,
             const std::filesystem::path& variable) {
    using namespace forge;
    std::filesystem::create_directories(root / "Assets");
    std::filesystem::copy_file(variable, root / "Assets/Weight.ttf");
    std::ofstream(root / "Assets/Weights.rml") << R"rml(<rml><head><style>
@font-face {font-family:Variable;src:"Assets/Weight.ttf";}
body {font-family:Variable;font-size:100px;color:#fff;}
#light {font-weight:100;} #regular {font-weight:400;} #heavy {font-weight:900;}
</style></head><body><p id="light">H</p><p id="regular">H</p><p id="heavy">H</p></body></rml>)rml";
    const auto document = register_ui_document(root, "Assets/Weights.rml");
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, latin, {}, nullptr, true);
        const auto observed = presenter.inspect_static({document.id});
        check(observed.sources.size() == 2 && presenter.diagnostic().empty(),
              "Variable font bypassed native resource observation");
        const auto entity = EntityId::generate();
        presenter.reset("weight-native", 1);
        const Json snapshot{{"version", 1},
                            {"session", "weight-native"},
                            {"generation", 1u},
                            {"revision", 1u},
                            {"documents", Json::array({{{"entity", entity},
                                                        {"asset", document.id},
                                                        {"instance", entity.str()},
                                                        {"visible", true},
                                                        {"layer", 0u},
                                                        {"model", Json::object()},
                                                        {"commands", Json::array()}}})}};
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
        auto* font = Rml::GetFontEngineInterface();
        auto* context = Rml::GetContext(0);
        check(context && font, "Variable font context unavailable");
        const auto handle = [&](int weight, int size = 100) {
            return font->GetFontFaceHandle("Variable", Rml::Style::FontStyle::Normal,
                                           static_cast<Rml::Style::FontWeight>(weight), size);
        };
        const auto light = handle(100), regular = handle(400), heavy = handle(900);
        check(light && regular && heavy && light != regular && regular != heavy && light != heavy,
              "Variable weights shared one sized glyph handle");
        check(handle(1) == light && handle(1000) == heavy && handle(400) == regular,
              "Weight clamping or exact handle reuse differs");
        const Rml::String language = "en";
        const Rml::TextShapingContext shaping{language};
        const auto width = [&](Rml::FontFaceHandle face, int advance) {
            Rml::TexturedMeshList meshes;
            const auto result = font->GenerateString(context->GetRenderManager(), face, 0, "H", {},
                                                     {255, 255, 255, 255}, 1, shaping, meshes);
            check(result == advance && font->GetStringWidth(face, "H", shaping) == advance &&
                      meshes.size() == 1 && meshes[0].mesh.vertices.size() == 4,
                  "Variable raster geometry and shaped advance disagree");
            const auto& vertices = meshes[0].mesh.vertices;
            const auto [first, last] = std::minmax_element(
                vertices.begin(), vertices.end(),
                [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
            return last->position.x - first->position.x;
        };
        const auto thin_width = width(light, 60), normal_width = width(regular, 60),
                   bold_width = width(heavy, 60);
        check(thin_width < normal_width && normal_width < bold_width,
              "Font weight did not vary the original H outline");
        const auto middle = handle(650), small = handle(400, 50);
        const auto middle_width = width(middle, 60), small_width = width(small, 30);
        check(normal_width < middle_width && middle_width < bold_width &&
                  small_width < normal_width,
              "Interpolated weights or independent pixel sizes failed");
        const auto stable_textures = renderer.textures.size();
        for (int repeat = 0; repeat < 10; ++repeat) {
            check(width(light, 60) == thin_width && width(heavy, 60) == bold_width &&
                      width(regular, 60) == normal_width && width(small, 30) == small_width &&
                      width(middle, 60) == middle_width,
                  "Switching native weights/sizes changed a cached glyph design");
        }
        check(renderer.textures.size() == stable_textures && presenter.diagnostic().empty(),
              "Repeated variable designs allocated new resources or failed");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Variable font resources survived presenter retirement");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 5, "Need scratch, Latin, Arabic and original variable font");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto latin = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (int i = 0; i < 2; ++i) {
            exercise(root / forge::AssetId::generate().str(), latin, argv[3]);
            weights(root / forge::AssetId::generate().str(), latin, argv[4]);
        }
        std::cout << "Public native font interface, glyph/effect textures, admitted project font, "
                     "input, stable resources and cold presenter retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
